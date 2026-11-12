#include <QObject>
#include <QAbstractSocket>
#include <QByteArray>
#include <QTcpSocket>
#include <QTimer>
#include <logindialog.h>
#include <demodialog.h>
#include "TcpClientMediator.h"
#include "packdef.h"
#include "roomdialog.h"
#include "setuserdialog.h"
#include <map>
#include "audio_read.h"
#include "system_audio_read.h"
#include "audio_write.h"
#include "video_read.h"
#include "desk_read.h"
#include "h264codec.h"
#include "video_encode_thread.h"
#include "workthread.h"
#include <atomic>
#include <memory>
#include <QApplication>
#include <QTcpServer>
#include <QSettings>
#include <QMessageBox>
#include <QPushButton>
#include <QFile>
#include <QFontDatabase>
#include <QEventLoop>
#include <QLabel>
#include <cstdlib>
#include "appstyle.h"
#include "ckernal.h"
#include "gateway_client.h"
#include "rtc_session.h"
#include "track_publication_state.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    applyAppStyle(app);
    app.setQuitOnLastWindowClosed(false);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost)) return 2;
    QSettings config(QApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
    config.setValue("Net/transport", "tcp");
    config.setValue("Net/host", "127.0.0.1");
    config.setValue("Net/port", server.serverPort());
    config.sync();
    auto* kernel = CKernal::getInstance();
    QFile report(QApplication::applicationDirPath() + "/results.txt");
    if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
    auto check = [&](bool ok, const char* label) {
        report.write(ok ? "PASS " : "FAIL ");
        report.write(label);
        report.write("\n");
        report.flush();
        if (!ok) std::_Exit(1);
    };

    TrackPublicationState publication;
    check(publication.beginAttempt(100) && !publication.ready() && !publication.beginAttempt(101),
          "track construction does not imply publication or allow a concurrent attempt");
    publication.failed(10100);
    check(!publication.beginAttempt(11000) && !publication.ready(),
          "publication backoff starts after a blocking timeout finishes");
    check(publication.beginAttempt(11600) && !publication.ready(),
          "retrying track stays unavailable until server acknowledgement");
    publication.failed(21600);
    check(publication.beginAttempt(23100), "second failed publication can retry after backoff");
    publication.failed(33100);
    check(publication.exhausted() && !publication.beginAttempt(99999),
          "persistent publication failures terminate after three attempts");
    publication.reset();
    check(publication.beginAttempt(0), "a new share resets publication retry state");
    publication.succeeded();
    check(publication.ready() && !publication.beginAttempt(10),
          "acknowledged track becomes ready and is not republished per frame");

    auto* latency = kernel->m_roomDialog->findChild<LatencyLabel*>();
    auto* shareStatus = kernel->m_roomDialog->findChild<QLabel*>("lb_deskStatus");
    check(latency && shareStatus && shareStatus->isHidden(),
          "sharing status starts hidden behind the latency badge");
    check(QMetaObject::invokeMethod(latency, "clicked") && !shareStatus->isHidden(),
          "latency badge reveals sharing status");
    check(QMetaObject::invokeMethod(latency, "clicked") && shareStatus->isHidden(),
          "latency badge hides sharing status again");

    kernel->m_loggedIn = true;
    kernel->m_userid = 7;
    memset(kernel->m_sessionToken, 'a', 32);
    kernel->m_sessionToken[32] = 0;
    int resets = 0;
    QObject::connect(kernel, &CKernal::SIG_resetVideoEncoder, &app, [&] { ++resets; });
    check(!kernel->ensureAvChannelReady(1) && kernel->m_avRegistrationPending[1]
          && !kernel->m_avChannelRegistered[1]
          && kernel->m_avRegistrationFallbackTimers[1]->isActive(),
          "video registration waits for acknowledgement before frames are sent");
    STRU_MEDIA_REGISTER_RS response = {};
    response.type = DEF_PACK_VIDEO_REGISTER_RS;
    response.userId = 7;
    memset(response.sessionToken, 'b', 32);
    kernel->handleMediaRegistration(1, reinterpret_cast<char*>(&response), sizeof(response));
    check(!kernel->m_avChannelRegistered[1] && kernel->m_avRegistrationPending[1],
          "old session acknowledgement ignored");
    memcpy(response.sessionToken, kernel->m_sessionToken, SESSION_TOKEN_SIZE);
    response.result = 0;
    char* queuedResponse = new char[sizeof(response)];
    memcpy(queuedResponse, &response, sizeof(response));
    kernel->m_tcpAV[1]->SIG_ReadyData(0, queuedResponse, sizeof(response));
    check(!kernel->m_avChannelRegistered[1] && kernel->m_avRegistrationPending[1],
          "media acknowledgement is marshalled to the UI thread");
    QCoreApplication::processEvents();
    check(kernel->m_avChannelRegistered[1] && !kernel->m_avReconnectTimers[1]->isActive()
          && !kernel->m_avRegistrationFallbackTimers[1]->isActive() && resets == 1,
          "valid registration acknowledgement enables media and requests an IDR");
    kernel->m_roomid = 123456;
    kernel->m_activeVideoSource = CKernal::VideoSource::Desktop;
    STRU_VIDEO_KEYFRAME_RQ keyFrameRequest = {};
    keyFrameRequest.type = DEF_PACK_VIDEO_KEYFRAME_RQ;
    keyFrameRequest.roomId = kernel->m_roomid;
    keyFrameRequest.requesterUserId = 8;
    kernel->handleVideoKeyFrameRequest(reinterpret_cast<char*>(&keyFrameRequest),
                                       sizeof(keyFrameRequest));
    check(resets == 2, "new viewer request emits a fresh desktop IDR");
    kernel->m_activeVideoSource = CKernal::VideoSource::None;
    kernel->m_tcpAV[1]->CloseNet();
    kernel->m_avChannelRegistered[1].store(false);
    check(!kernel->ensureAvChannelReady(1) && kernel->m_avRegistrationPending[1],
          "legacy registration stays pending during its acknowledgement window");
    kernel->m_avRegistrationFallbackTimers[1]->setInterval(1);
    kernel->m_avRegistrationFallbackTimers[1]->start();
    QEventLoop registrationFallback;
    QTimer::singleShot(50, &registrationFallback, &QEventLoop::quit);
    registrationFallback.exec();
    check(kernel->m_avChannelRegistered[1] && resets == 3,
          "legacy server fallback enables media with a new IDR");
    kernel->resetAuthenticationState();
    check(!kernel->m_avChannelRegistered[1], "reauthentication clears media readiness");

    check(kernel->m_controlSocket->state() == QAbstractSocket::ConnectedState ||
          kernel->m_controlSocket->waitForConnected(1000), "local control connection established");
    kernel->m_roomid = 123456;
    kernel->m_loginPending = true;
    STRU_LOGIN_RS login;
    login.userid = 7;
    login.result = login_success;
    memset(login.sessionToken, 'a', 32);
    login.sessionToken[32] = 0;
    kernel->slot_dealLoginRs(0, reinterpret_cast<char*>(&login), sizeof(login));
    check(kernel->m_roomid == 0 && kernel->m_roomRequest == CKernal::RoomRequest::Join &&
          kernel->m_pendingRoomId == 123456, "reauthentication requests room rejoin");
    STRU_JOINROOM_RS joined;
    joined.m_lResult = join_success;
    joined.m_RoomID = 123456;
    kernel->slot_dealJoinRoomRs(0, reinterpret_cast<char*>(&joined), sizeof(joined));
    kernel->allowLegacyAvRegistration(1);
    check(kernel->m_roomid == 123456 && kernel->m_avChannelRegistered[1],
          "room rejoin supports legacy servers without media acknowledgement");
    kernel->m_roomDialog->grab().save(QApplication::applicationDirPath() + "/room-status.png");
    kernel->slot_quitRoom();

    kernel->m_loginDeadline->setInterval(200);
    kernel->slot_loginCommit("12345678901", "test-password");
    const int initialRemaining = kernel->m_loginDeadline->remainingTime();
    kernel->beginControlRecovery();
    check(kernel->m_loginDeadline->remainingTime() <= initialRemaining,
          "connection recovery does not extend login deadline");
    // Close only this test's modal error dialog while the real deadline fires.
    QTimer dismiss;
    QObject::connect(&dismiss, &QTimer::timeout, &app, [] {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (auto* box = qobject_cast<QMessageBox*>(widget)) box->accept();
        }
    });
    dismiss.start(25);
    QEventLoop loop;
    QTimer::singleShot(500, &loop, &QEventLoop::quit);
    loop.exec();
    check(!kernel->m_loginPending && !kernel->m_controlReconnectTimer->isActive(),
          "total timeout stops retries");
    auto* button = kernel->m_loginDialog->findChild<QPushButton*>("pb_commit");
    check(button && button->isEnabled() && button->text() == QString::fromUtf8("登录"),
          "total timeout restores login button");

    kernel->m_useWebRtc = true;
    kernel->setupWebRtc();
    kernel->m_gateway->setBaseUrl(QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort())));
    kernel->m_roomid = 1;
    kernel->m_rtc->SIG_latencyMeasured(37);
    kernel->updateLatencyDisplay(1600);
    check(latency->text().contains("37 ms") && latency->text().contains(QString::fromUtf8("媒体")),
          "slow gateway cannot overwrite room media RTT");
    auto *loginLatency = kernel->m_loginDialog->findChild<LatencyLabel*>();
    check(loginLatency->text().contains("1600 ms") && loginLatency->text().contains(QString::fromUtf8("网关")),
          "gateway latency is labelled and high values are preserved");
    kernel->m_rtc->SIG_latencyMeasured(1250);
    check(latency->text().contains("1250 ms"), "high media RTT is preserved without clamping");
    kernel->m_rtc->SIG_stateChanged(RtcSession::State::Reconnecting, "reconnecting");
    check(latency->text().contains("--"), "reconnect clears stale media RTT");
    kernel->m_mediaLatencyClock.invalidate();
    kernel->slot_measureLatency();
    check(latency->text().contains("--"), "missing media samples do not retain old latency");
    kernel->m_desktopStatsTimer->stop();
    kernel->m_activeVideoSource = CKernal::VideoSource::Desktop;
    kernel->resetDesktopShareStats();
    int keyframeRequests = 0;
    QObject::connect(kernel, &CKernal::SIG_requestVideoKeyframe, &app, [&] { ++keyframeRequests; });
    const int resetsBefore = resets;
    for (int i = 0; i < 8; ++i) kernel->m_rtc->SIG_keyframeRequested();
    check(keyframeRequests == 1 && resets == resetsBefore,
          "repeated keyframe feedback is coalesced without recreating the encoder");
    for (int i = 0; i < 6; ++i) {
        kernel->m_desktopCapturedFrames = 60;
        kernel->slot_updateDesktopShareStats();
    }
    check(resets == resetsBefore && kernel->m_desktopNoEncodeIntervals == 0,
          "connecting media does not trigger the encoder stall watchdog");
    for (int i = 0; i < 59; ++i) kernel->m_rtc->SIG_videoQueued();
    kernel->m_desktopCapturedFrames = 60;
    kernel->m_desktopEncodedFrames = 60;
    kernel->slot_updateDesktopShareStats();
    check(kernel->m_webQueuedFrames.load() == 0 && shareStatus->text().contains(QString::fromUtf8("入队 59"))
          && !shareStatus->text().contains(QString::fromUtf8("发送 59")),
          "watchdog consumes LiveKit queue progress and labels it separately from network sending");
    kernel->m_activeVideoSource = CKernal::VideoSource::None;

    kernel->m_roomid = 0;
    kernel->webLogin("12345678901", "test-password");
    QEventLoop webTimeout;
    QTimer::singleShot(500, &webTimeout, &QEventLoop::quit);
    webTimeout.exec();
    check(!kernel->m_loginPending && button->isEnabled(), "HTTP login total deadline restores login button");
    kernel->slot_destroy();
    return 0;
}
