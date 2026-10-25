#include "InstanceChannel.hpp"

#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>
#include <unistd.h>

InstanceChannel::InstanceChannel(QString name, QObject* parent) : QObject(parent), name_(std::move(name)) {
    // No QLocalServer socket options: with them Qt binds elsewhere and renames the socket into place, which
    // would silently replace a live instance's socket. The runtime directory is private (0700) anyway.
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* s = server_.nextPendingConnection()) {
            connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
            connect(s, &QLocalSocket::readyRead, this, [this, s] {
                if (!s->canReadLine()) return;
                QString msg = QString::fromUtf8(s->readAll());
                msg.replace(QStringLiteral("\x1e"), QStringLiteral("\n")).chop(1);   // record separator -> lines
                s->write("ok\n");
                s->flush();
                s->disconnectFromServer();
                emit messageReceived(msg);
            });
        }
    });
}

QString InstanceChannel::socketPath(const QString& name) {
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty() && QFileInfo(runtime).isDir() && QDir::isAbsolutePath(runtime))
        return runtime + "/openwitty-" + name + ".sock";
    return QDir::tempPath() + QString("/openwitty-%1-%2.sock").arg(getuid()).arg(name);
}

bool InstanceChannel::listen() {
    const QString path = socketPath(name_);
    {
        QLocalSocket probe;
        probe.connectToServer(path);
        if (probe.waitForConnected(300)) return false;    // another instance is alive: never replace it
    }
    if (server_.listen(path)) return true;
    QLocalServer::removeServer(path);                      // stale socket (or file) of a crashed instance
    return server_.listen(path);
}

bool InstanceChannel::send(const QString& name, const QString& message, int timeoutMs) {
    QLocalSocket s;
    s.connectToServer(socketPath(name));
    if (!s.waitForConnected(timeoutMs)) return false;
    QString wire = message;
    wire.replace('\n', QStringLiteral("\x1e"));
    s.write((wire + '\n').toUtf8());
    if (!s.waitForBytesWritten(timeoutMs)) return false;
    QByteArray reply;
    while (!reply.contains('\n') && s.waitForReadyRead(timeoutMs)) reply += s.readAll();
    return reply.startsWith("ok");
}
