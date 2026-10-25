// A per-user, per-session single-instance channel: a local socket in $XDG_RUNTIME_DIR (a private 0700
// directory), so a second start of the GUI or the tray can hand its request to the running one instead of
// starting another. It carries short text messages only and never touches the device.
#pragma once
#include <QLocalServer>
#include <QObject>
#include <QString>

class InstanceChannel : public QObject {
    Q_OBJECT
public:
    // name: "gui" or "tray".
    explicit InstanceChannel(QString name, QObject* parent = nullptr);
    // $XDG_RUNTIME_DIR/openwitty-<name>.sock, or <tmp>/openwitty-<uid>-<name>.sock without a runtime dir.
    static QString socketPath(const QString& name);
    // Become the primary instance. False if another instance answers on the socket (a stale socket file
    // left by a crashed instance is replaced).
    bool listen();
    bool listening() const { return server_.isListening(); }
    // Deliver one message to the running primary instance; true once it acknowledged it.
    static bool send(const QString& name, const QString& message, int timeoutMs = 1500);

signals:
    void messageReceived(const QString& message);

private:
    QString name_;
    QLocalServer server_;
};
