#include "gateway_client.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <cstdlib>
#include <functional>

namespace {
void waitMs(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
bool waitFor(const std::function<bool()> &condition, int timeout = 3000) {
    QElapsedTimer clock;
    clock.start();
    while (!condition() && clock.elapsed() < timeout) waitMs(10);
    return condition();
}
void respond(QTcpSocket *socket, int status, const QByteArray &body) {
    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Response\r\nContent-Type: application/json\r\nContent-Length: "
                  + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
    socket->disconnectFromHost();
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    QFile report(QCoreApplication::applicationDirPath() + "/results.txt");
    if (!report.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
    auto check = [&](bool ok, const char *label) {
        report.write(ok ? "PASS " : "FAIL ");
        report.write(label); report.write("\n"); report.flush();
        if (!ok) std::_Exit(1);
    };
    QTcpServer server;
    check(server.listen(QHostAddress::LocalHost), "local gateway listening");
    GatewayClient gateway;
    gateway.setBaseUrl(QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort())));
    std::function<void(QTcpSocket *, const QByteArray &)> handler;
    int logins = 0, registrations = 0, healthRequests = 0, failures = 0, loggedIn = 0;
    QString lastFailure;
    QList<int> latency;
    QList<QVariantMap> diagnostics;
    QObject::connect(&gateway, &GatewayClient::failed, &app, [&](const QString &, const QString &detail) {
        ++failures; lastFailure = detail;
    });
    QObject::connect(&gateway, &GatewayClient::loggedIn, &app, [&] { ++loggedIn; });
    QObject::connect(&gateway, &GatewayClient::latencyMeasured, &app, [&](int ms) { latency.append(ms); });
    QObject::connect(&gateway, &GatewayClient::requestDiagnostic, &app,
                     [&](const QVariantMap &fields) { diagnostics.append(fields); });
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        while (auto *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                QByteArray bytes = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", bytes);
                const int headersEnd = bytes.indexOf("\r\n\r\n");
                if (headersEnd < 0 || socket->property("handled").toBool()) return;
                int length = 0;
                for (const auto &line : bytes.left(headersEnd).split('\n'))
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (bytes.size() < headersEnd + 4 + length) return;
                socket->setProperty("handled", true);
                const QByteArray path = bytes.left(bytes.indexOf('\r')).split(' ').value(1);
                if (path == "/v1/auth/login") ++logins;
                if (path == "/v1/auth/register") ++registrations;
                if (path == "/healthz") ++healthRequests;
                if (path == "/v1/me/profile") respond(socket, 200, "{\"id\":7,\"name\":\"test\"}");
                else handler(socket, path);
            });
        }
    });
    const QByteArray success = "{\"token\":\"test-session-secret\",\"user\":{\"id\":7,\"name\":\"test\"}}";
    handler = [&](QTcpSocket *s, const QByteArray &) {
        respond(s, logins == 1 ? 520 : 200, logins == 1 ? QByteArray("{}") : success);
    };
    gateway.login("test-phone", "test-password-secret");
    check(waitFor([&] { return loggedIn == 1; }) && logins == 2 && failures == 0,
          "transient login 520 is retried once and succeeds");
    gateway.logout();

    int before = logins;
    handler = [&](QTcpSocket *s, const QByteArray &) { respond(s, 401, "{\"error\":\"invalid credentials\"}"); };
    gateway.login("test-phone", "test-password-secret");
    check(waitFor([&] { return failures == 1; }), "invalid credentials reported");
    waitMs(350);
    check(logins == before + 1 && lastFailure.contains(QString::fromUtf8("手机号或密码")),
          "invalid credentials are localized and never retried");

    handler = [&](QTcpSocket *s, const QByteArray &) { respond(s, 503, "{}"); };
    gateway.registerUser("test-phone", "test-password-secret", "test");
    check(waitFor([&] { return failures == 2; }), "registration server failure reported");
    waitMs(350);
    check(registrations == 1, "registration mutations are never replayed");

    before = logins;
    gateway.login("test-phone", "test-password-secret");
    check(waitFor([&] { return logins == before + 1; }), "login reaches transient failure");
    waitMs(60);
    gateway.logout();
    waitMs(350);
    check(logins == before + 1 && failures == 2, "logout cancels a scheduled login retry");

    handler = [&](QTcpSocket *s, const QByteArray &) { respond(s, 520, "{}"); };
    gateway.ping();
    check(waitFor([&] { return !latency.isEmpty(); }) && latency.last() == -2,
          "failed health request is unavailable, never a numeric latency");

    int initialHealth = healthRequests;
    int initialLatency = latency.size();
    handler = [&](QTcpSocket *s, const QByteArray &) {
        // Continual bytes avoid Qt's inactivity timeout; the absolute deadline must still fire.
        s->write("HTTP/1.1 200 OK\r\nContent-Length: 10000\r\n\r\n");
        auto *trickle = new QTimer(s);
        QObject::connect(trickle, &QTimer::timeout, s, [s] { s->write(" "); });
        trickle->start(200);
    };
    gateway.ping(); gateway.ping(); gateway.ping();
    check(waitFor([&] { return healthRequests == initialHealth + 1; }), "health request begins");
    check(waitFor([&] { return latency.size() > initialLatency; }, 5000)
          && latency.last() == -2 && healthRequests == initialHealth + 1,
          "health probes do not overlap and slow trickle hits absolute deadline");

    handler = [&](QTcpSocket *s, const QByteArray &) { respond(s, 200, "{\"status\":\"ok\"}"); };
    initialLatency = latency.size();
    gateway.ping();
    check(waitFor([&] { return latency.size() > initialLatency; }) && latency.last() >= 0,
          "health probe recovers after timeout");

    before = logins;
    handler = [&](QTcpSocket *s, const QByteArray &) {
        if (logins > before + 1) respond(s, 200, success);
    };
    gateway.login("test-phone", "test-password-secret");
    check(waitFor([&] { return loggedIn == 2; }, 18000) && logins == before + 2,
          "stalled login times out then retries successfully within total deadline");
    gateway.logout();

    before = logins;
    int initialFailure = failures;
    handler = [&](QTcpSocket *, const QByteArray &) {};
    gateway.login("test-phone", "test-password-secret");
    check(waitFor([&] { return failures > initialFailure; }, 18000) && logins == before + 2,
          "two stalled login attempts terminate with one failure");
    check(lastFailure.contains(QString::fromUtf8("超时")), "timeout error explains gateway and retry");
    gateway.logout();

    before = logins;
    handler = [&](QTcpSocket *s, const QByteArray &) {
        QTimer::singleShot(200, s, [s, success] { respond(s, 200, success); });
    };
    gateway.login("test-phone", "test-password-secret");
    check(waitFor([&] { return logins == before + 1; }), "cancellable login is in flight");
    gateway.logout();
    waitMs(350);
    check(loggedIn == 2 && !gateway.authenticated(), "cancelled request cannot authenticate late");

    QByteArray json;
    for (const auto &entry : diagnostics) json += QJsonDocument::fromVariant(entry).toJson();
    check(!json.contains("test-password-secret") && !json.contains("test-session-secret")
          && !json.contains("test-phone") && !json.contains("Authorization"),
          "request diagnostics exclude credentials and request bodies");
    bool timeoutStage = false;
    for (const auto &entry : diagnostics)
        if (entry.value("timed_out").toBool() && entry.contains("request_sent_ms")
                && entry.value("elapsed_ms").toInt() >= 3500) timeoutStage = true;
    check(timeoutStage, "diagnostics retain elapsed time and final network phase");
    return 0;
}
