#include "rtc_session.h"
#include "diagnostics.h"
#include "track_publication_state.h"
#include <QThread>
#include <QTimer>
#include <QPointer>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <QElapsedTimer>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <functional>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <set>
#include <livekit/livekit.h>

namespace {
struct SdkLifetime {
    SdkLifetime() { livekit::initialize(livekit::LogLevel::Warn); }
    ~SdkLifetime() { livekit::shutdown(); }
};

bool isPublicationTimeout(const QString &detail) {
    return detail.contains("track publication timed out", Qt::CaseInsensitive);
}

double mediaRttMs(const livekit::SessionStats &stats) {
    double selectedRtt = -1.0;
    double reportRtt = -1.0;
    for (const auto *records : {&stats.publisher_stats, &stats.subscriber_stats}) {
        std::set<std::string> selectedPairs;
        for (const auto &record : *records) {
            if (const auto *transport = std::get_if<livekit::RtcTransportStats>(&record.stats))
                if (!transport->transport.selected_candidate_pair_id.empty())
                    selectedPairs.insert(transport->transport.selected_candidate_pair_id);
        }
        for (const auto &record : *records) {
            if (const auto *pair = std::get_if<livekit::RtcCandidatePairStats>(&record.stats)) {
                const auto &p = pair->candidate_pair;
                const bool selected = selectedPairs.empty()
                    ? p.nominated && p.state == livekit::IceCandidatePairState::Succeeded
                    : selectedPairs.count(pair->rtc.id) != 0;
                if (selected && std::isfinite(p.current_round_trip_time)
                        && p.current_round_trip_time >= 0.0 && p.responses_received > 0)
                    selectedRtt = qMax(selectedRtt, p.current_round_trip_time * 1000.0);
            } else if (const auto *remote = std::get_if<livekit::RtcRemoteInboundRtpStats>(&record.stats)) {
                const double seconds = remote->remote_inbound.round_trip_time;
                if (std::isfinite(seconds) && seconds > 0.0)
                    reportRtt = qMax(reportRtt, seconds * 1000.0);
            }
        }
    }
    return selectedRtt >= 0.0 ? selectedRtt : reportRtt;
}
}
struct RtcSession::Impl : livekit::RoomDelegate {
    RtcSession *owner;
    QThread thread;
    QObject *worker;
    QTimer *feedback;
    std::unique_ptr<livekit::Room> room;
    std::shared_ptr<livekit::EncodedVideoSource> video;
    std::shared_ptr<livekit::LocalVideoTrack> videoTrack;
    std::shared_ptr<livekit::AudioSource> audio;
    std::shared_ptr<livekit::LocalAudioTrack> audioTrack;
    std::atomic_int remoteAudioPending {0};
    int videoWidth=0, videoHeight=0, videoFps=0, videoBitrate=0;
    bool videoScreen=false;
    bool waitingForKeyframe=true;
    std::atomic<quint64> generation {0};
    std::optional<std::future<livekit::SessionStats>> statsFuture;
    int statsTicks = 0;
    quint64 lastStatsBytes = 0;
    quint64 lastStatsFrames = 0;
    qint64 lastStatsMs = 0;
    TrackPublicationState videoPublication;
    std::atomic_bool starting {false};
    std::mutex framesMutex;
    struct Mailbox { QImage image; bool pending=false; };
    std::unordered_map<std::string,Mailbox> mailboxes;
    explicit Impl(RtcSession *o): owner(o),worker(new QObject),feedback(new QTimer(worker)) {
        worker->moveToThread(&thread);
        QObject::connect(&thread,&QThread::finished,worker,&QObject::deleteLater);
        feedback->setInterval(100);
        QObject::connect(feedback,&QTimer::timeout,worker,[this] {
            if(video) try {
                auto f=video->takeFeedback();
                if(f.keyframe_requested) post([](RtcSession *o){Q_EMIT o->SIG_keyframeRequested();});
                if(f.rate_control && f.rate_control->target_bitrate_bps>0) {
                    const int rate=static_cast<int>(std::min<std::uint64_t>(f.rate_control->target_bitrate_bps,72000000));
                    post([rate](RtcSession *o){Q_EMIT o->SIG_bitrateRequested(rate);});
                }
            } catch(const std::exception &e){ report(e.what()); }
            if (++statsTicks >= 10) {
                statsTicks = 0;
                pollStats();
            }
        });
        thread.start();
    }
    ~Impl(){
        QMetaObject::invokeMethod(worker,[this]{cleanup();},Qt::BlockingQueuedConnection);
        thread.quit();thread.wait();
    }
    // Capture the generation at the source of each callback; old rooms cannot update the UI.
    void post(std::function<void(RtcSession*)> fn) {
        const quint64 g=generation;
        QMetaObject::invokeMethod(owner,[o=owner,g,fn=std::move(fn)] {
            if(o->m_generation.load()==g)fn(o);
        },Qt::QueuedConnection);
    }
    void report(const char *error) {
        const QString detail=QString::fromUtf8(error);
        DiagnosticLogger::instance().logEvent(QStringLiteral("livekit_error"),
                                              {{QStringLiteral("detail"), detail}});
        post([detail](RtcSession *o){o->setState(State::Failed,detail);});
    }
    void pollStats() {
        if (!room) return;
        if (statsFuture) {
            if (statsFuture->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                try {
                    const livekit::SessionStats stats = statsFuture->get();
                    const double rttMs = mediaRttMs(stats);
                    double lossPercent = -1.0;
                    double jitterMs = -1.0;
                    quint64 bytesSent = 0;
                    quint64 framesSent = 0;
                    for (const auto &record : stats.publisher_stats) {
                        std::visit([&](const auto &typed) {
                            using T = std::decay_t<decltype(typed)>;
                            if constexpr (std::is_same_v<T, livekit::RtcOutboundRtpStats>) {
                                if (typed.stream.kind == "video" || typed.outbound.active) {
                                    bytesSent = qMax(bytesSent, typed.sent.bytes_sent);
                                    framesSent = qMax(framesSent,
                                                      static_cast<quint64>(typed.outbound.frames_sent));
                                }
                            } else if constexpr (std::is_same_v<T, livekit::RtcRemoteInboundRtpStats>) {
                                if (typed.stream.kind == "video") {
                                    if (typed.received.packets_received > 0 ||
                                        typed.remote_inbound.fraction_lost >= 0.0) {
                                        lossPercent = typed.remote_inbound.fraction_lost * 100.0;
                                    }
                                }
                            } else if constexpr (std::is_same_v<T, livekit::RtcInboundRtpStats>) {
                                if (typed.stream.kind == "audio" || typed.stream.kind == "video") {
                                    if (typed.received.jitter > 0.0) {
                                        jitterMs = typed.received.jitter * 1000.0;
                                    }
                                }
                            }
                        }, record.stats);
                    }
                    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                    double bitrate = -1.0;
                    quint64 frameDelta = 0;
                    quint64 byteDelta = 0;
                    if (lastStatsMs > 0 && nowMs > lastStatsMs && bytesSent >= lastStatsBytes) {
                        byteDelta = bytesSent - lastStatsBytes;
                        frameDelta = framesSent >= lastStatsFrames ? framesSent - lastStatsFrames : 0;
                        bitrate = static_cast<double>(byteDelta) * 8000.0
                                / static_cast<double>(nowMs - lastStatsMs);
                    }
                    lastStatsMs = nowMs;
                    lastStatsBytes = bytesSent;
                    lastStatsFrames = framesSent;
                    DiagnosticLogger::instance().recordLiveKitStats(
                                rttMs, lossPercent, jitterMs, bitrate,
                                frameDelta, byteDelta,
                                owner->m_videoPending.load() ? 1 : 0);
                    const int latency = rttMs >= 0.0 && rttMs < 2147483647.0 ? qRound(rttMs) : -1;
                    post([latency](RtcSession *o) {
                        if (o->state() != State::Connected) return;
                        Q_EMIT o->SIG_latencyMeasured(latency);
                        DiagnosticLogger::instance().logEvent(QStringLiteral("media_latency"),
                                                              {{QStringLiteral("rtt_ms"), latency}});
                    });
                } catch (const std::exception &e) {
                    DiagnosticLogger::instance().logEvent(
                                QStringLiteral("livekit_stats_error"),
                                {{QStringLiteral("detail"), QString::fromUtf8(e.what())}});
                }
                statsFuture.reset();
            }
        }
        if (!statsFuture) {
            try {
                statsFuture = room->getStats();
            } catch (const std::exception &e) {
                DiagnosticLogger::instance().logEvent(
                            QStringLiteral("livekit_stats_request_error"),
                            {{QStringLiteral("detail"), QString::fromUtf8(e.what())}});
            }
        }
    }
    void cleanup() {
        feedback->stop();
        statsFuture.reset();
        statsTicks = 0;
        lastStatsBytes = lastStatsFrames = 0;
        lastStatsMs = 0;
        videoPublication.reset();
        if(room){room->setDelegate(nullptr);room->disconnect();}
        videoTrack.reset();video.reset();audioTrack.reset();audio.reset();room.reset();
        videoWidth=videoHeight=0;waitingForKeyframe=true;
        starting = false;
        std::lock_guard<std::mutex> lock(framesMutex);mailboxes.clear();
    }
    void begin(Config config,quint64 g,int attempt = 0) {
        if(owner->m_generation.load()!=g)return;
        cleanup();generation=g;
        if(owner->m_generation.load()!=g)return;
        starting = true;
        QString phase = "connect";
        QElapsedTimer phaseTimer;
        phaseTimer.start();
        DiagnosticLogger::instance().logEvent(QStringLiteral("livekit_connect_started"),
                                              {{QStringLiteral("attempt"), attempt + 1}});
        try {
            static SdkLifetime runtime;
            (void)runtime;
            room=std::make_unique<livekit::Room>();room->setDelegate(this);
            livekit::RoomOptions options;options.auto_subscribe=true;
            options.connect_timeout=std::chrono::seconds(10);
            if(!room->connect(config.gatewayUrl.toString().toStdString(),config.liveKitToken.toStdString(),options)){
                cleanup();report("Unable to connect to LiveKit");return;
            }
            if(owner->m_generation.load()!=g){cleanup();return;}
            DiagnosticLogger::instance().logEvent(QStringLiteral("livekit_connect_succeeded"),
                                                  {{QStringLiteral("elapsed_ms"), phaseTimer.elapsed()}});
            phase = "audio_publish";
            phaseTimer.restart();
            DiagnosticLogger::instance().logEvent(QStringLiteral("livekit_audio_track_publish_started"),
                                                  {{QStringLiteral("attempt"), attempt + 1}});
            audio=std::make_shared<livekit::AudioSource>(48000,1,0);
            if(auto lp=room->localParticipant().lock())audioTrack=lp->publishAudioTrack("meeting-audio",audio,livekit::TrackSource::SOURCE_MICROPHONE);
            if(!audioTrack)throw std::runtime_error("Unable to publish audio track");
            if(owner->m_generation.load()!=g){cleanup();return;}
            DiagnosticLogger::instance().logEvent(QStringLiteral("livekit_audio_track_publish_succeeded"),
                                                  {{QStringLiteral("elapsed_ms"), phaseTimer.elapsed()}});
            for(const auto &weak:room->remoteParticipants())if(auto p=weak.lock())participant(p.get());
            feedback->start();
            starting = false;
            post([](RtcSession *o){o->setState(State::Connected,QStringLiteral("音视频已连接"));});
        }catch(const std::exception &e){
            const QString detail = QString::fromUtf8(e.what());
            DiagnosticLogger::instance().logEvent(QStringLiteral("livekit_start_failed"),
                {{QStringLiteral("phase"), phase}, {QStringLiteral("attempt"), attempt + 1},
                 {QStringLiteral("elapsed_ms"), phaseTimer.elapsed()}, {QStringLiteral("detail"), detail}});
            cleanup();
            if(owner->m_generation.load()!=g)return;
            if (phase == "audio_publish" && isPublicationTimeout(detail) && attempt == 0) {
                post([](RtcSession *o) { o->setState(State::Connecting,
                    QStringLiteral("音频轨道发布超时，正在重新建立媒体连接（第 2 次尝试）…")); });
                QTimer::singleShot(1000, worker, [this, config, g, attempt] {
                    if(owner->m_generation.load()==g)begin(config, g, attempt + 1);
                });
            } else {
                const QString message = phase == "audio_publish"
                    ? QStringLiteral("音频轨道发布失败，服务器未完成确认，请稍后重新加入房间。")
                    : QStringLiteral("无法建立音视频连接，请检查网络或服务器后重新加入房间。");
                report(message.toUtf8().constData());
            }
        }
    }
    void participant(livekit::RemoteParticipant *p) {
        if(!p)return;
        const QString id=QString::fromStdString(p->identity()),name=QString::fromStdString(p->name());
        post([id,name](RtcSession *o){Q_EMIT o->SIG_participantJoined(id,name);});
    }
    void onParticipantConnected(livekit::Room&,const livekit::ParticipantConnectedEvent &e) override {participant(e.participant);}
    void onParticipantDisconnected(livekit::Room&,const livekit::ParticipantDisconnectedEvent &e) override {
        if(!e.participant)return;const QString id=QString::fromStdString(e.participant->identity());
        post([id](RtcSession *o){Q_EMIT o->SIG_participantLeft(id);});
    }
    void onTrackSubscribed(livekit::Room &r,const livekit::TrackSubscribedEvent &e) override {
        if(!e.participant||!e.track)return;
        const std::string id=e.participant->identity(),name=e.track->name();
        const QString qid=QString::fromStdString(id);
        if(e.track->kind()==livekit::TrackKind::KIND_VIDEO) {
            livekit::VideoStream::Options opts;opts.capacity=2;opts.format=livekit::VideoBufferType::BGRA;
            r.setOnVideoFrameCallback(id,name,[this,id,qid](const livekit::VideoFrame &f,std::int64_t) {
                if(f.type()!=livekit::VideoBufferType::BGRA||f.width()<1||f.height()<1||f.dataSize()<std::size_t(f.width())*f.height()*4)return;
                bool schedule=false;
                {
                    std::lock_guard<std::mutex> lock(framesMutex);
                    auto &box=mailboxes[id];box.image=QImage(f.data(),f.width(),f.height(),f.width()*4,QImage::Format_ARGB32).copy();
                    if(!box.pending){box.pending=true;schedule=true;}
                }
                if(schedule)post([this,id,qid](RtcSession *o){
                    QImage image;
                    {std::lock_guard<std::mutex> lock(framesMutex);auto it=mailboxes.find(id);if(it==mailboxes.end())return;image=std::move(it->second.image);it->second.pending=false;}
                    Q_EMIT o->SIG_remoteVideoFrame(qid,image);
                });
            },opts);
        }else if(e.track->kind()==livekit::TrackKind::KIND_AUDIO){
            livekit::AudioStream::Options opts;opts.capacity=5;
            r.setOnAudioFrameCallback(id,name,[this,qid](const livekit::AudioFrame &f){
                if(f.sampleRate()<=0||f.numChannels()<1||f.samplesPerChannel()<1||remoteAudioPending.load()>=12)return;
                const int count=static_cast<int>(static_cast<qint64>(f.samplesPerChannel())*48000/f.sampleRate());
                if(count<1||count>4800)return;
                QByteArray pcm(count*2,0);
                for(int i=0;i<count;++i){
                    const int source=qMin(f.samplesPerChannel()-1,static_cast<int>(static_cast<qint64>(i)*f.sampleRate()/48000));
                    int sample=0;for(int c=0;c<f.numChannels();++c)sample+=f.data()[source*f.numChannels()+c];
                    const auto mono=static_cast<std::int16_t>(sample/f.numChannels());std::memcpy(pcm.data()+i*2,&mono,2);
                }
                const quint64 g=generation.load();++remoteAudioPending;
                QMetaObject::invokeMethod(owner,[this,o=owner,g,qid,pcm]{
                    --remoteAudioPending;if(o->m_generation.load()==g)Q_EMIT o->SIG_remoteAudio(qid,pcm);
                },Qt::QueuedConnection);
            },opts);
        }
    }
    void onTrackUnsubscribed(livekit::Room&,const livekit::TrackUnsubscribedEvent &e) override {
        if(e.participant&&e.track&&e.track->kind()==livekit::TrackKind::KIND_VIDEO){const QString id=QString::fromStdString(e.participant->identity());post([id](RtcSession *o){Q_EMIT o->SIG_remoteVideoStopped(id);});}
    }
    void onReconnecting(livekit::Room&,const livekit::ReconnectingEvent&) override {
        DiagnosticLogger::instance().logEvent(QStringLiteral("livekit_reconnecting"));
        if (!starting) post([](RtcSession *o){o->setState(State::Reconnecting,QStringLiteral("正在恢复音视频连接…"));});
    }
    void onReconnected(livekit::Room&,const livekit::ReconnectedEvent&) override {
        DiagnosticLogger::instance().logEvent(QStringLiteral("livekit_reconnected"));
        if (!starting) post([](RtcSession *o){o->setState(State::Connected,QStringLiteral("音视频连接已恢复"));Q_EMIT o->SIG_keyframeRequested();});
    }
    void onDisconnected(livekit::Room&,const livekit::DisconnectedEvent&) override {
        DiagnosticLogger::instance().logEvent(QStringLiteral("livekit_disconnected"));
        if (!starting) post([](RtcSession *o){o->setState(State::Failed,QStringLiteral("音视频连接已断开，请重新加入房间"));});
    }
    void capture(QByteArray data,bool key,int w,int h,int fps,int bitrate,bool screen,quint64 g) {
        struct Pending {RtcSession *o;~Pending(){o->m_videoPending=false;}} pending{owner};
        if(g!=generation||owner->m_generation.load()!=g||!room)return;
        try {
            auto lp=room->localParticipant().lock();if(!lp)return;
            const bool encodingLayoutChanged = w!=videoWidth || h!=videoHeight
                    || screen!=videoScreen || fps!=videoFps;
            const bool cameraBitrateChanged = !screen && bitrate != videoBitrate;
            if(encodingLayoutChanged || cameraBitrateChanged)videoPublication.reset();
            if(!videoPublication.ready()){
                // Check backoff before constructing a source/track. A constructed track is not a publication.
                if (!videoPublication.beginAttempt(QDateTime::currentMSecsSinceEpoch())) {
                    DiagnosticLogger::instance().recordDroppedFrame(QStringLiteral("video_publish_backoff"));
                    return;
                }
                if(videoTrack)lp->unpublishTrack(videoTrack->sid());
                videoTrack.reset();video.reset();waitingForKeyframe=true;
                videoWidth=w;videoHeight=h;videoScreen=screen;videoFps=fps;videoBitrate=bitrate;
                video=std::make_shared<livekit::EncodedVideoSource>(livekit::VideoCodec::H264,w,h);
                videoTrack=livekit::LocalVideoTrack::createLocalVideoTrack("meeting-video",video);
                livekit::TrackPublishOptions opts;opts.video_codec=livekit::VideoCodec::H264;
                opts.video_encoder=livekit::VideoEncoderBackend::PreEncoded;
                opts.simulcast=false;opts.source=screen?livekit::TrackSource::SOURCE_SCREENSHARE:livekit::TrackSource::SOURCE_CAMERA;
                opts.video_encoding=livekit::VideoEncodingOptions{static_cast<std::uint64_t>(bitrate),static_cast<double>(fps)};
                QElapsedTimer publishTimer;
                publishTimer.start();
                DiagnosticLogger::instance().logEvent(QStringLiteral("livekit_video_track_publish_started"),
                    {{QStringLiteral("attempt"), videoPublication.attempts()},
                     {QStringLiteral("width"), w}, {QStringLiteral("height"), h},
                     {QStringLiteral("fps"), fps}, {QStringLiteral("bitrate_bps"), bitrate}});
                try {
                    lp->publishTrack(videoTrack,opts);
                    videoPublication.succeeded();
                } catch (const std::exception &e) {
                    const QString detail = QString::fromUtf8(e.what());
                    videoPublication.failed(QDateTime::currentMSecsSinceEpoch());
                    DiagnosticLogger::instance().logEvent(
                                QStringLiteral("livekit_track_publish_error"),
                                {{QStringLiteral("detail"), detail},
                                 {QStringLiteral("phase"), QStringLiteral("video_publish")},
                                 {QStringLiteral("attempt"), videoPublication.attempts()},
                                 {QStringLiteral("elapsed_ms"), publishTimer.elapsed()},
                                 {QStringLiteral("width"), w},
                                 {QStringLiteral("height"), h},
                                 {QStringLiteral("fps"), fps},
                                 {QStringLiteral("bitrate_bps"), bitrate}});
                    videoTrack.reset();
                    video.reset();
                    waitingForKeyframe = true;
                    if (g != owner->m_generation.load()) return;
                    if (isPublicationTimeout(detail) && !videoPublication.exhausted()) {
                        post([](RtcSession *o){
                            Q_EMIT o->SIG_publicationStatus(QStringLiteral("视频轨道发布超时，正在等待重试…"));
                            Q_EMIT o->SIG_keyframeRequested();
                        });
                    } else {
                        report("视频轨道发布失败，服务器未完成确认，请稍后重新加入房间。");
                    }
                    return;
                }
                if (g != owner->m_generation.load()) return;
                DiagnosticLogger::instance().logEvent(QStringLiteral("livekit_video_track_publish_succeeded"),
                                                      {{QStringLiteral("elapsed_ms"), publishTimer.elapsed()},
                                                       {QStringLiteral("width"), w},
                                                       {QStringLiteral("height"), h},
                                                       {QStringLiteral("fps"), fps},
                                                       {QStringLiteral("bitrate_bps"), bitrate}});
                post([](RtcSession *o){Q_EMIT o->SIG_publicationStatus(QStringLiteral("视频轨道已发布"));});
            }
            if (!videoPublication.ready() || !videoTrack || !video) return;
            if(waitingForKeyframe&&!key){post([](RtcSession *o){Q_EMIT o->SIG_keyframeRequested();});return;}
            livekit::EncodedVideoSource::Frame frame;frame.is_keyframe=key;frame.width=w;frame.height=h;
            frame.data.assign(reinterpret_cast<const std::uint8_t*>(data.constData()),reinterpret_cast<const std::uint8_t*>(data.constData())+data.size());
            QElapsedTimer queueTimer;
            queueTimer.start();
            if(video->captureFrame(frame)){
                waitingForKeyframe=false;
                DiagnosticLogger::instance().recordLiveKitQueued(
                            queueTimer.nsecsElapsed() / 1000, data.size(),
                            owner->m_videoPending.load() ? 1 : 0);
                post([](RtcSession *o){Q_EMIT o->SIG_videoQueued();});
            }
            else{
                DiagnosticLogger::instance().recordDroppedFrame(QStringLiteral("livekit_queue_rejected"));
                waitingForKeyframe=true;post([](RtcSession *o){Q_EMIT o->SIG_keyframeRequested();});
            }
        }catch(const std::exception &e){report(e.what());}
    }
};
RtcSession::RtcSession(QObject *p):QObject(p),m_impl(std::make_unique<Impl>(this)){}
RtcSession::~RtcSession(){++m_generation;m_impl.reset();}
bool RtcSession::isAvailable(){return true;}
void RtcSession::setState(State s,const QString &d){m_state=s;Q_EMIT SIG_stateChanged(s,d);}
bool RtcSession::start(const Config &c){
    if((c.gatewayUrl.scheme()!="wss"&&c.gatewayUrl.scheme()!="ws")||c.gatewayUrl.host().isEmpty()||c.liveKitToken.isEmpty())return false;
    const quint64 g=++m_generation;setState(State::Connecting,QStringLiteral("正在连接音视频…"));
    QMetaObject::invokeMethod(m_impl->worker,[impl=m_impl.get(),c,g]{impl->begin(c,g);},Qt::QueuedConnection);return true;
}
void RtcSession::stop(){++m_generation;setState(State::Disconnected,QStringLiteral("已离开房间"));QMetaObject::invokeMethod(m_impl->worker,[impl=m_impl.get()]{impl->cleanup();},Qt::QueuedConnection);}
bool RtcSession::canAcceptVideo()const{return m_state==State::Connected&&!m_videoPending.load();}
bool RtcSession::publishH264(const QByteArray &b,bool key,int w,int h,int fps,int bitrate,bool screen){
    if(m_state!=State::Connected||b.isEmpty()||w<2||h<2)return false;
    bool expected=false;if(!m_videoPending.compare_exchange_strong(expected,true)){Q_EMIT SIG_keyframeRequested();return false;}
    const quint64 g=m_generation.load();
    QMetaObject::invokeMethod(m_impl->worker,[impl=m_impl.get(),b,key,w,h,fps,bitrate,screen,g]{impl->capture(b,key,w,h,fps,bitrate,screen,g);},Qt::QueuedConnection);return true;
}
void RtcSession::publishAudio(const QByteArray &pcm,int rate,int channels){
    if(m_state!=State::Connected||rate!=48000||channels!=1||pcm.isEmpty()||pcm.size()%2)return;
    if(m_audioPending.load()>=5)return; ++m_audioPending;
    const quint64 g=m_generation.load();
    QMetaObject::invokeMethod(m_impl->worker,[this,pcm,g]{
        try{if(g==m_generation.load()&&g==m_impl->generation&&m_impl->audio){
            std::vector<std::int16_t> samples(static_cast<std::size_t>(pcm.size()/2));std::memcpy(samples.data(),pcm.constData(),pcm.size());
            // AudioSource(queue=0) accepts exactly 10 ms per capture call.
            for(std::size_t off=0;off+480<=samples.size();off+=480) {
                std::vector<std::int16_t> chunk(samples.begin()+off,samples.begin()+off+480);
                m_impl->audio->captureFrame(livekit::AudioFrame(std::move(chunk),48000,1,480));
            }
        }}catch(const std::exception &e){m_impl->report(e.what());} --m_audioPending;
    },Qt::QueuedConnection);
}
void RtcSession::stopVideo(){const quint64 g=m_generation.load();QMetaObject::invokeMethod(m_impl->worker,[impl=m_impl.get(),g]{
    if(impl->generation!=g)return;
    try{if(impl->room&&impl->videoTrack)if(auto lp=impl->room->localParticipant().lock())lp->unpublishTrack(impl->videoTrack->sid());}catch(const std::exception &e){impl->report(e.what());}
    impl->videoTrack.reset();impl->video.reset();impl->waitingForKeyframe=true;
    impl->videoPublication.reset();
},Qt::QueuedConnection);}
