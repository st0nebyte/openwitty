// Single-instance ownership under real cross-process competition. Every competitor is a separate process
// (tests/gui/instance_race_helper.cpp) that waits at a file barrier, so all of them start their claim at the
// same moment; the flock() guarantees one owner whatever the scheduling. Each test has its own runtime
// directory. No GUI, no device.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <memory>
#include <sys/stat.h>

#include "InstanceChannel.hpp"

namespace {
QCoreApplication* app() {
    static int argc = 1;
    static char name[] = "lewitt_instance_race_tests";
    static char* argv[] = {name, nullptr};
    static QCoreApplication* a = new QCoreApplication(argc, argv);
    return a;
}

struct Competitor {
    std::unique_ptr<QProcess> p;
    QString out;
    QString message;
    void pump() { out += QString::fromUtf8(p->readAllStandardOutput()); }
    bool has(const QString& line) {
        pump();
        return out.split('\n').contains(line);
    }
    QString role() {
        pump();
        for (const QString& l : out.split('\n'))
            if (l == "PRIMARY" || l == "FORWARDED" || l.startsWith("FAILED")) return l;
        return {};
    }
    QStringList got() {
        pump();
        QStringList g;
        for (const QString& l : out.split('\n'))
            if (l.startsWith("GOT ")) g << l.mid(4);
        return g;
    }
};

struct Race {
    QTemporaryDir run;
    int barriers{0};
    std::vector<std::unique_ptr<Competitor>> all;

    Race() { app(); }
    QString barrierPath(int n) const { return run.path() + QString("/go-%1").arg(n); }

    // Starts `count` competitors behind barrier `barrier` and waits until all of them are READY.
    std::vector<Competitor*> start(int count, const QString& name, const QString& mode, int holdMs, int lockDelayMs,
                                   int barrier) {
        std::vector<Competitor*> started;
        for (int i = 0; i < count; ++i) {
            auto c = std::make_unique<Competitor>();
            c->message = QString("activate\nmsg-%1-%2").arg(barrier).arg(i);
            c->p = std::make_unique<QProcess>();
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("XDG_RUNTIME_DIR", run.path());
            c->p->setProcessEnvironment(env);
            c->p->start(LEWITT_RACE_HELPER, {name, mode, barrierPath(barrier), QString::number(holdMs),
                                             QString::number(lockDelayMs), c->message});
            started.push_back(c.get());
            all.push_back(std::move(c));
        }
        for (Competitor* c : started) {
            QElapsedTimer t;
            t.start();
            while (!c->has("READY") && t.elapsed() < 5000) c->p->waitForReadyRead(50);
            EXPECT_TRUE(c->has("READY"));
        }
        return started;
    }
    void release(int barrier) {
        QFile f(barrierPath(barrier));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    }
    void waitAll(int ms = 15000) {
        for (auto& c : all) {
            c->p->waitForFinished(ms);
            c->pump();
        }
    }
    bool waitLine(Competitor* c, const QString& line, int ms = 5000) {
        QElapsedTimer t;
        t.start();
        while (!c->has(line) && t.elapsed() < ms) c->p->waitForReadyRead(25);
        return c->has(line);
    }
};

// Exactly one owner; every other competitor forwarded; the owner received exactly their messages.
void expectOneOwner(const std::vector<Competitor*>& cs) {
    int primaries = 0;
    Competitor* owner = nullptr;
    QStringList forwarded;
    for (Competitor* c : cs) {
        const QString r = c->role();
        if (r == "PRIMARY") ++primaries, owner = c;
        else {
            EXPECT_EQ(r, "FORWARDED") << c->out.toStdString();
            forwarded << QString(c->message).replace('\n', '|');   // the helper prints line breaks as '|' 
        }
    }
    ASSERT_EQ(primaries, 1);
    QStringList got = owner->got();
    got.sort();
    forwarded.sort();
    EXPECT_EQ(got, forwarded);
}

ino_t socketInode(const QString& name) {
    struct stat st {};
    return ::stat(QFile::encodeName(InstanceChannel::socketPath(name)).constData(), &st) == 0 ? st.st_ino : 0;
}
}  // namespace

TEST(InstanceRace, TwoGuiInstancesStartingTogether) {
    Race r;
    auto cs = r.start(2, "gui", "claim", 1500, 0, 1);
    r.release(1);
    r.waitAll();
    expectOneOwner(cs);
}

TEST(InstanceRace, EightGuiInstancesStartingTogether) {
    Race r;
    auto cs = r.start(8, "gui", "claim", 2500, 0, 1);
    r.release(1);
    r.waitAll();
    expectOneOwner(cs);
}

TEST(InstanceRace, TwoTrayInstancesStartingTogether) {
    Race r;
    auto cs = r.start(2, "tray", "claim", 1500, 0, 1);
    r.release(1);
    r.waitAll();
    expectOneOwner(cs);
}

TEST(InstanceRace, StaleSocketOfACrashedOwnerIsRecovered) {
    Race r;
    auto crashed = r.start(1, "gui", "crash", 0, 0, 1);
    r.release(1);
    crashed[0]->p->waitForFinished(5000);
    EXPECT_EQ(crashed[0]->p->exitCode(), 9);
    qputenv("XDG_RUNTIME_DIR", r.run.path().toUtf8());
    ASSERT_TRUE(QFileInfo::exists(InstanceChannel::socketPath("gui")));      // stale socket left behind
    auto cs = r.start(3, "gui", "claim", 1500, 0, 2);
    r.release(2);
    for (auto* c : cs) c->p->waitForFinished(15000);
    expectOneOwner(cs);
}

TEST(InstanceRace, LiveOwnerSocketIsNeverRemovedOrReplaced) {
    Race r;
    qputenv("XDG_RUNTIME_DIR", r.run.path().toUtf8());
    auto owner = r.start(1, "gui", "claim", 4000, 0, 1);
    r.release(1);
    ASSERT_TRUE(r.waitLine(owner[0], "PRIMARY"));
    const ino_t before = socketInode("gui");
    ASSERT_NE(before, ino_t(0));
    InstanceChannel inProcess("gui");
    EXPECT_EQ(inProcess.acquire(), InstanceChannel::Acquire::Busy);
    auto cs = r.start(6, "gui", "claim", 0, 0, 2);
    r.release(2);
    for (auto* c : cs) {
        c->p->waitForFinished(10000);
        EXPECT_EQ(c->role(), "FORWARDED") << c->out.toStdString();
    }
    EXPECT_EQ(socketInode("gui"), before);
    owner[0]->p->waitForFinished(10000);
    EXPECT_TRUE(owner[0]->has(QString("SOCKET %1").arg(qulonglong(before))));
    EXPECT_EQ(owner[0]->got().size(), 6);
}

// The race of the review: an owner holds the lock but is not listening yet. Competitors must wait for it,
// not remove anything, not become a second owner, and still reach it.
TEST(InstanceRace, CompetitorsWaitForAnOwnerThatIsStillStarting) {
    Race r;
    auto owner = r.start(1, "gui", "claim", 3000, 800, 1);
    r.release(1);
    ASSERT_TRUE(r.waitLine(owner[0], "LOCKED"));
    auto cs = r.start(4, "gui", "claim", 0, 0, 2);
    r.release(2);
    for (auto* c : cs) {
        c->p->waitForFinished(10000);
        EXPECT_EQ(c->role(), "FORWARDED") << c->out.toStdString();
    }
    owner[0]->p->waitForFinished(10000);
    EXPECT_EQ(owner[0]->role(), "PRIMARY");
    EXPECT_EQ(owner[0]->got().size(), 4);
}

TEST(InstanceRace, AnOwnerThatNeverAnswersMakesTheStartFailInsteadOfDuplicating) {
    Race r;
    qputenv("XDG_RUNTIME_DIR", r.run.path().toUtf8());
    auto owner = r.start(1, "gui", "claim", 0, 6000, 1);    // holds the lock, no socket for 6 s
    r.release(1);
    ASSERT_TRUE(r.waitLine(owner[0], "LOCKED"));
    InstanceChannel late("gui");
    QElapsedTimer t;
    t.start();
    EXPECT_EQ(late.claimOrForward("activate\n", 700), InstanceChannel::Role::Failed);
    EXPECT_GE(t.elapsed(), 650);
    EXPECT_LT(t.elapsed(), 3000);
    EXPECT_FALSE(late.owner());
    EXPECT_TRUE(late.lastError().contains("does not answer")) << late.lastError().toStdString();
    owner[0]->p->kill();
    owner[0]->p->waitForFinished(3000);
}
