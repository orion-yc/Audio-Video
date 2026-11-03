// Real HTTPS control + native LiveKit media smoke test, with a viewer joining late.
// Run against a disposable server/database or remove the generated test accounts afterwards.
#include "gateway_client.h"
#include "rtc_session.h"
#include <QCoreApplication>
#include <QTimer>
#include <QFile>
#include <QBuffer>
#include <QImage>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <cmath>
#include <cstring>

int main(int argc,char **argv){
 QCoreApplication app(argc,argv);
 if(argc<4){qCritical()<<"Usage: webrtc_integration GATEWAY_URL LIVEKIT_URL H264_IDR_FILE [REPORT]";return 2;}
 QFile frameFile(QString::fromLocal8Bit(argv[3]));if(!frameFile.open(QIODevice::ReadOnly))return 2;const QByteArray frame=frameFile.readAll();
 GatewayClient a,b;RtcSession ra,rb;
 a.setBaseUrl(QUrl(QString::fromLocal8Bit(argv[1])));b.setBaseUrl(a.baseUrl());
 const QUrl livekit(QString::fromLocal8Bit(argv[2]));
 const QString suffix=QUuid::createUuid().toString(QUuid::Id128).left(10);
 const QString phoneA="webrtc-smoke-a-"+suffix,phoneB="webrtc-smoke-b-"+suffix;
 const QString password=QUuid::createUuid().toString();
 bool loggedA=false,loggedB=false,readyA=false,readyB=false,chatAB=false,chatBA=false,videoReceived=false,audioReceived=false,avatar=false,profile=false;
 bool joinBStarted=false,uploadStarted=false,chatStarted=false;QString room;
 qint64 aId=0,bId=0;int videoFrames=0,audioFrames=0;bool finished=false;
 QTimer media,chatRetry;media.setInterval(40);chatRetry.setInterval(500);
 auto finish=[&](bool success){
  if(finished)return;finished=true;media.stop();chatRetry.stop();
  qInfo().noquote()<<(success?"PASS":"FAIL")<<"HTTPS login/profile/avatar/chat and native H264/Opus late-join"<<"video frames"<<videoFrames<<"audio frames"<<audioFrames;
  if(argc>4){QFile report(QString::fromLocal8Bit(argv[4]));if(report.open(QIODevice::WriteOnly))report.write(QJsonDocument(QJsonObject{{"success",success},{"phone_a",phoneA},{"phone_b",phoneB},{"user_a",aId},{"user_b",bId},{"room",room},{"video_frames",videoFrames},{"audio_frames",audioFrames}}).toJson());}
  a.leaveRoom();b.leaveRoom();ra.stop();rb.stop();QTimer::singleShot(500,&app,[&,success]{app.exit(success?0:1);});
 };
 auto check=[&]{if(readyA&&readyB&&chatAB&&chatBA&&videoReceived&&audioReceived&&avatar&&profile)finish(true);};
 auto maybeJoin=[&]{if(readyA&&loggedB&&!joinBStarted){joinBStarted=true;QTimer::singleShot(1000,&app,[&]{if(finished)return;qInfo()<<"Joining viewer after publisher is already sending";b.joinRoom(room);});}};
 auto startRtc=[&](RtcSession &rtc,GatewayClient &gateway,QString id,QString token){RtcSession::Config c;c.gatewayUrl=livekit;c.roomId=id;c.liveKitToken=token;c.participantIdentity=QString("user-%1").arg(gateway.userId());if(!rtc.start(c))finish(false);};
 QObject::connect(&a,&GatewayClient::registered,&app,[&]{a.login(phoneA,password);});
 QObject::connect(&b,&GatewayClient::registered,&app,[&]{b.login(phoneB,password);});
 QObject::connect(&a,&GatewayClient::loggedIn,&app,[&](qint64 id,QString){loggedA=true;aId=id;a.createRoom();});
 QObject::connect(&b,&GatewayClient::loggedIn,&app,[&](qint64 id,QString){loggedB=true;bId=id;maybeJoin();});
 QObject::connect(&a,&GatewayClient::profileReceived,&app,[&](QJsonObject p){if(p.value("name").toString()=="Smoke Updated"){profile=true;check();}else if(!uploadStarted){uploadStarted=true;a.updateProfile("Smoke Updated",5,"hello");}});
 QObject::connect(&a,&GatewayClient::profileSaved,&app,[&]{QImage img(8,8,QImage::Format_RGB32);img.fill(Qt::red);QByteArray jpeg;QBuffer out(&jpeg);out.open(QIODevice::WriteOnly);if(!img.save(&out,"JPG")){finish(false);return;}a.uploadAvatar(jpeg);});
 QObject::connect(&a,&GatewayClient::avatarSaved,&app,[&]{a.fetchAvatar(aId);});
 QObject::connect(&a,&GatewayClient::avatarReceived,&app,[&](qint64 id,QByteArray data){if(id==aId&&!data.isEmpty()){avatar=true;check();}});
 QObject::connect(&a,&GatewayClient::roomJoined,&app,[&](QString id,QString token){room=id;startRtc(ra,a,id,token);});
 QObject::connect(&b,&GatewayClient::roomJoined,&app,[&](QString id,QString token){startRtc(rb,b,id,token);});
 QObject::connect(&ra,&RtcSession::SIG_stateChanged,&app,[&](RtcSession::State state,QString detail){qInfo()<<"publisher"<<int(state)<<detail;if(state==RtcSession::State::Connected){readyA=true;media.start();maybeJoin();}else if(state==RtcSession::State::Failed)finish(false);});
 QObject::connect(&rb,&RtcSession::SIG_stateChanged,&app,[&](RtcSession::State state,QString detail){qInfo()<<"viewer"<<int(state)<<detail;if(state==RtcSession::State::Connected){readyB=true;chatRetry.start();check();}else if(state==RtcSession::State::Failed)finish(false);});
 QObject::connect(&media,&QTimer::timeout,&app,[&]{
  if(ra.canAcceptVideo())ra.publishH264(frame,true,64,64,25,500000,true);
  QByteArray pcm(1920,0);static int index=0;
  for(int i=0;i<960;++i){const auto sample=static_cast<std::int16_t>(std::sin(index++*6.28318530718*440/48000)*3000);std::memcpy(pcm.data()+i*2,&sample,2);}
  ra.publishAudio(pcm);ra.publishAudio(pcm);
 });
 QObject::connect(&rb,&RtcSession::SIG_remoteVideoFrame,&app,[&](QString id,QImage img){if(id==QString("user-%1").arg(aId)&&img.width()==64&&img.height()==64){++videoFrames;videoReceived=true;check();}});
 QObject::connect(&rb,&RtcSession::SIG_remoteAudio,&app,[&](QString id,QByteArray pcm){if(id==QString("user-%1").arg(aId)&&pcm.size()>0){++audioFrames;for(qsizetype off=0;off+2<=pcm.size();off+=2){std::int16_t sample=0;std::memcpy(&sample,pcm.constData()+off,2);if(std::abs(static_cast<int>(sample))>100){audioReceived=true;break;}}check();}});
 QObject::connect(&chatRetry,&QTimer::timeout,&app,[&]{if(!chatAB)a.sendChat(QStringLiteral("你好 viewer 😀"));if(!chatBA)b.sendChat(QStringLiteral("你好 publisher 😀"));});
 QObject::connect(&a,&GatewayClient::chatReceived,&app,[&](qint64 id,QString text){if(id==bId&&text==QStringLiteral("你好 publisher 😀")){chatBA=true;check();}});
 QObject::connect(&b,&GatewayClient::chatReceived,&app,[&](qint64 id,QString text){if(id==aId&&text==QStringLiteral("你好 viewer 😀")){chatAB=true;check();}});
 for(auto *g:{&a,&b})QObject::connect(g,&GatewayClient::failed,&app,[&](QString operation,QString message){qWarning()<<operation<<message;if(operation!="chat"&&operation!="avatar-fetch")finish(false);});
 QTimer::singleShot(60000,&app,[&]{qWarning()<<"timeout flags"<<loggedA<<loggedB<<readyA<<readyB<<chatAB<<chatBA<<videoReceived<<audioReceived<<avatar<<profile;finish(false);});
 a.registerUser(phoneA,password,"Smoke A");b.registerUser(phoneB,password,"Smoke B");
 return app.exec();
}
