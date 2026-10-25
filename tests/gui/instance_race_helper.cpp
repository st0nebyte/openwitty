// Test helper for tests/gui/test_instance_race.cpp: one competing instance. It prints READY, waits until the
// barrier file exists (all competitors are then released together), and claims the channel.
//   instance_race_helper <name> <mode> <barrier> <hold-ms> <after-lock-delay-ms> <message>
// mode: claim  - claimOrForward(); primary: report each received message for <hold-ms>, then exit
//       crash  - become owner, then _exit() without any cleanup (leaves a stale socket)
// Output lines: READY, LOCKED (owner, inside the lock-to-listen window), PRIMARY | FORWARDED | FAILED,
// GOT <message>, SOCKET <inode> (owner: inode of its socket at exit), DONE.
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>

#include "InstanceChannel.hpp"

static void say(const QString& s) {
    std::printf("%s\n", qPrintable(s));
    std::fflush(stdout);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    if (a.size() < 7) return 64;
    const QString name = a[1], mode = a[2], barrier = a[3], message = a[6];
    const int holdMs = a[4].toInt(), lockDelayMs = a[5].toInt();
    say("READY");
    for (int i = 0; i < 20000 && !QFileInfo::exists(barrier); ++i) QThread::usleep(500);
    InstanceChannel ch(name);
    auto afterLock = [&] {
        say("LOCKED");
        if (lockDelayMs > 0) QThread::msleep(lockDelayMs);
    };
    if (mode == "crash") {
        if (ch.acquire(afterLock) != InstanceChannel::Acquire::Owner) return 2;
        say("PRIMARY");
        _exit(9);   // no destructor: the socket file stays behind, the kernel drops the lock
    }
    switch (ch.claimOrForward(message, 8000, afterLock)) {
        case InstanceChannel::Role::Forwarded: say("FORWARDED"); return 0;
        case InstanceChannel::Role::Failed: say("FAILED " + ch.lastError()); return 1;
        case InstanceChannel::Role::Primary: break;
    }
    say("PRIMARY");
    QObject::connect(&ch, &InstanceChannel::messageReceived, [](const QString& m) { say("GOT " + QString(m).replace('\n', '|')); });
    QTimer::singleShot(holdMs, &app, [&] {
        struct stat st {};
        if (::stat(QFile::encodeName(InstanceChannel::socketPath(name)).constData(), &st) == 0)
            say(QString("SOCKET %1").arg(qulonglong(st.st_ino)));
        say("DONE");
        app.quit();
    });
    return app.exec();
}
