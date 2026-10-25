#include "InstanceChannel.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QThread>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
QString runtimeBase(const QString& name, const char* suffix) {
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty() && QDir::isAbsolutePath(runtime) && QFileInfo(runtime).isDir())
        return runtime + "/openwitty-" + name + suffix;
    return QDir::tempPath() + QString("/openwitty-%1-%2%3").arg(getuid()).arg(name, QString::fromLatin1(suffix));
}
}  // namespace

InstanceChannel::InstanceChannel(QString name, QObject* parent) : QObject(parent), name_(std::move(name)) {
    // No QLocalServer socket options: with them Qt binds elsewhere and renames the socket into place.
    // The socket gets the umask's mode, so other users cannot connect (connecting needs write access).
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* s = server_.nextPendingConnection()) {
            connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
            auto handle = [this, s] {
                if (!s->canReadLine()) return;
                QString msg = QString::fromUtf8(s->readAll());
                msg.replace(QStringLiteral("\x1e"), QStringLiteral("\n")).chop(1);   // record separator -> lines
                s->write("ok\n");
                s->flush();
                s->disconnectFromServer();
                emit messageReceived(msg);
            };
            connect(s, &QLocalSocket::readyRead, this, handle);
            handle();   // the request may already be buffered (sent before our event loop ran)
        }
    });
}

InstanceChannel::~InstanceChannel() { release(); }

void InstanceChannel::release() {
    server_.close();          // removes our socket while we still hold the lock
    if (lockFd_ >= 0) {
        ::close(lockFd_);     // releases the lock
        lockFd_ = -1;
    }
}

QString InstanceChannel::socketPath(const QString& name) { return runtimeBase(name, ".sock"); }
QString InstanceChannel::lockPath(const QString& name) { return runtimeBase(name, ".lock"); }

InstanceChannel::Acquire InstanceChannel::acquire(const std::function<void()>& afterLock) {
    if (owner()) return Acquire::Owner;
    const QByteArray lp = QFile::encodeName(lockPath(name_));
    const int fd = ::open(lp.constData(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        error_ = QString("cannot open %1: %2").arg(QString::fromLocal8Bit(lp), QString::fromLocal8Bit(std::strerror(errno)));
        return Acquire::Error;
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_uid != ::getuid() || !S_ISREG(st.st_mode)) {
        ::close(fd);
        error_ = QString("%1 is not a lock file of this user").arg(QString::fromLocal8Bit(lp));
        return Acquire::Error;
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int e = errno;
        ::close(fd);
        if (e == EWOULDBLOCK) return Acquire::Busy;
        error_ = QString("cannot lock %1: %2").arg(QString::fromLocal8Bit(lp), QString::fromLocal8Bit(std::strerror(e)));
        return Acquire::Error;
    }
    lockFd_ = fd;     // the lock file itself is never deleted: unlinking it would break mutual exclusion
    if (afterLock) afterLock();
    const QString sock = socketPath(name_);
    QLocalServer::removeServer(sock);   // safe: we hold the lock, so whatever is there belongs to a dead owner
    if (!server_.listen(sock))
        error_ = QString("cannot listen on %1: %2 (other starts cannot bring this window to the front)")
                     .arg(sock, server_.errorString());
    return Acquire::Owner;
}

InstanceChannel::Role InstanceChannel::claimOrForward(const QString& message, int deadlineMs,
                                                      const std::function<void()>& afterLock) {
    QElapsedTimer t;
    t.start();
    for (;;) {
        const int remaining = std::max(500, int(deadlineMs - t.elapsed()));
        switch (sendMessage(name_, message, 200, remaining)) {
            case Send::Delivered: return Role::Forwarded;
            case Send::NoAck:
                error_ = "another instance accepted the request but did not confirm it";
                return Role::Failed;
            case Send::NoServer: break;
        }
        switch (acquire(afterLock)) {
            case Acquire::Owner: return Role::Primary;
            case Acquire::Error: return Role::Failed;
            case Acquire::Busy: break;       // the owner is still starting: its socket appears shortly
        }
        if (t.elapsed() >= deadlineMs) {
            error_ = "another instance owns the application but does not answer";
            return Role::Failed;
        }
        QThread::msleep(25);
    }
}

InstanceChannel::Send InstanceChannel::sendMessage(const QString& name, const QString& message, int connectTimeoutMs,
                                                   int ackTimeoutMs) {
    QLocalSocket s;
    s.connectToServer(socketPath(name));
    if (!s.waitForConnected(connectTimeoutMs)) return Send::NoServer;
    QString wire = message;
    wire.replace('\n', QStringLiteral("\x1e"));
    s.write((wire + '\n').toUtf8());
    if (!s.waitForBytesWritten(ackTimeoutMs)) return Send::NoAck;
    QByteArray reply;
    QElapsedTimer t;
    t.start();
    while (!reply.contains('\n') && t.elapsed() < ackTimeoutMs && s.waitForReadyRead(int(ackTimeoutMs - t.elapsed())))
        reply += s.readAll();
    return reply.startsWith("ok") ? Send::Delivered : Send::NoAck;
}
