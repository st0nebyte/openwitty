// GUI-thread -> worker -> GUI-thread latency and reconnect timing (mock backend, offscreen).
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <algorithm>
#include <cstdio>
#include <vector>

#include "DeviceController.hpp"

static bool waitFor(const std::function<bool()>& p, int ms = 5000) {
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 1); if (p()) return true; }
    return p();
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    DeviceController c;
    QElapsedTimer startup; startup.start();
    c.connectMock();
    waitFor([&] { return c.values().contains("in1.gain"); });
    std::printf("connect -> first state:        %lld ms\n", startup.elapsed());

    std::vector<double> lat;
    for (int i = 0; i < 300; ++i) {
        double target = 5.0 + (i % 60);
        QElapsedTimer t; t.start();
        c.setValue("in1.gain", target);
        waitFor([&] { return c.values()["in1.gain"].toDouble() == target; });
        lat.push_back(t.nsecsElapsed() / 1e6);
    }
    std::sort(lat.begin(), lat.end());
    std::printf("setValue -> state echo (n=300): median %.2f ms  p95 %.2f ms  max %.2f ms\n",
                lat[lat.size() / 2], lat[size_t(lat.size() * 0.95)], lat.back());

    std::vector<double> rc;
    for (int i = 0; i < 5; ++i) {
        QElapsedTimer t; t.start();
        c.simulateUnplug();
        waitFor([&] { return c.connection() == "Disconnected"; });
        c.simulateReplug();
        waitFor([&] { return c.connection() == "Connected"; });
        rc.push_back(t.nsecsElapsed() / 1e6);
    }
    std::sort(rc.begin(), rc.end());
    std::printf("unplug -> replug -> Connected (n=5): median %.1f ms  max %.1f ms\n", rc[2], rc.back());
    return 0;
}
