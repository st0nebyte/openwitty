// A per-user, per-session single-instance channel, so a second start of the GUI or the tray hands its
// request to the running one instead of starting another. It carries short text messages only and never
// touches the device (one device owner at a time is enforced separately by the control lease).
//
// Ownership is an exclusive flock() on $XDG_RUNTIME_DIR/openwitty-<name>.lock (a private 0700 directory),
// held for the owner's lifetime and released by the kernel when the process ends - also when it crashes.
// Only the lock holder may remove a socket file and listen on $XDG_RUNTIME_DIR/openwitty-<name>.sock, so a
// live instance's socket is never removed or replaced, and a socket left by a crashed owner is recognised
// as stale exactly because nobody holds the lock.
#pragma once
#include <QLocalServer>
#include <QObject>
#include <QString>
#include <functional>

class InstanceChannel : public QObject {
    Q_OBJECT
public:
    enum class Acquire { Owner, Busy, Error };          // Busy: another live process holds the lock
    enum class Send { Delivered, NoServer, NoAck };      // NoAck: written, but the owner did not confirm
    enum class Role { Primary, Forwarded, Failed };

    // name: "gui" or "tray".
    explicit InstanceChannel(QString name, QObject* parent = nullptr);
    ~InstanceChannel() override;

    static QString socketPath(const QString& name);
    static QString lockPath(const QString& name);

    // Take ownership: lock, then (still holding it) remove a stale socket and listen. `afterLock` runs
    // between taking the lock and listening (tests use it to widen that window).
    Acquire acquire(const std::function<void()>& afterLock = {});
    bool owner() const { return lockFd_ >= 0 && server_.isListening(); }
    QString lastError() const { return error_; }

    // The startup decision. Try to hand `message` to the owner; if there is no owner, become it. While
    // another process holds the lock but does not listen yet (it is still starting), keep retrying until
    // `deadlineMs`. A message that was written is never sent a second time. Failed = ownership could not
    // be obtained and the message was not confirmed: the caller must not start a second instance.
    Role claimOrForward(const QString& message, int deadlineMs = 10000, const std::function<void()>& afterLock = {});

    static Send sendMessage(const QString& name, const QString& message, int connectTimeoutMs, int ackTimeoutMs);
    static bool send(const QString& name, const QString& message, int timeoutMs = 1500) {
        return sendMessage(name, message, timeoutMs, timeoutMs) == Send::Delivered;
    }

signals:
    void messageReceived(const QString& message);

private:
    void release();
    QString name_;
    QLocalServer server_;
    int lockFd_{-1};
    QString error_;
};
