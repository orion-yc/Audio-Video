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
#include <cstdlib>
#include "appstyle.h"
// All dependency headers are already included. Expose only the kernel's state
// to this integration harness, without changing the production interface.
#define private public
#include "ckernal.h"
#undef private

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");
    applyAppStyle(app);
    app.setQuitOnLastWindowClosed(false);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost)) return 2;
    QSettings config(QApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
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

    kernel->m_loggedIn = true;
    kernel->m_userid = 7;
    memset(kernel->m_sessionToken, 'a', 32);
    kernel->m_sessionToken[32] = 0;
    int resets = 0;
    QObject::connect(kernel, &CKernal::SIG_resetVideoEncoder, &app, [&] { ++resets; });
    check(kernel->ensureAvChannelReady(1),
          "legacy server registration write enables video without acknowledgement");
    check(kernel->m_avChannelRegistered[1] && !kernel->m_avReconnectTimers[1]->isActive()
          && resets == 1, "legacy no-ack channel is immediately ready for frames");
    STRU_MEDIA_REGISTER_RS response = {};
    response.type = DEF_PACK_VIDEO_REGISTER_RS;
    response.userId = 7;
    memset(response.sessionToken, 'b', 32);
    kernel->handleMediaRegistration(1, reinterpret_cast<char*>(&response), sizeof(response));
    check(kernel->m_avChannelRegistered[1], "old session acknowledgement ignored");
    memcpy(response.sessionToken, kernel->m_sessionToken, SESSION_TOKEN_SIZE);
    response.result = 1;
    kernel->handleMediaRegistration(1, reinterpret_cast<char*>(&response), sizeof(response));
    check(!kernel->m_avChannelRegistered[1] && kernel->m_avReconnectTimers[1]->isActive(),
          "rejected registration schedules retry");
    response.result = 0;
    kernel->handleMediaRegistration(1, reinterpret_cast<char*>(&response), sizeof(response));
    check(kernel->m_avChannelRegistered[1] && !kernel->m_avReconnectTimers[1]->isActive()
          && resets == 1, "valid optional acknowledgement keeps media enabled");
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
    kernel->slot_destroy();
    return 0;
}
