// openwitty-volumed: keeps the CONNECT 2 speaker output in step with the desktop volume.
//
// The KDE/PipeWire volume of the CONNECT 2 sink ends up in the card's ALSA "PCM Playback" control.
// The kernel cannot read the device's speaker level back (broken GET_CUR), so knob changes never reach
// the desktop, and in some device states (seen after software re-enumeration) the device ignores the
// UAC Feature Unit requests that control produces. It always honours the vendor settings message on
// interface 3. This daemon applies desktop volume/mute
// changes to the device through that message and mirrors hardware-knob changes back to the desktop.
// Policy: core/VolumeSync.hpp (startup is read-only, bounded increases, no retries of unverified
// writes, knob changes win over older desktop requests).
//
//   openwitty-volumed                      dry run: reads and logs decisions, writes nothing
//   openwitty-volumed --allow-hardware-writes
//                                          also needs LEWITT_ALLOW_HARDWARE_WRITE=1 and
//                                          LEWITT_ALLOW_WRITE_SPEAKER_VOLUME=1 (exactly "1")
// Options: --max-increase-db N (0..20, default 10)  --poll-ms N (250..5000, default 1000)
//
// Device ownership: the per-user ControlLease. When another OpenWitty client (the GUI) asks for the
// device with a yield request, the daemon releases it and waits until the lease is free again.
#include <algorithm>
#include <csignal>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

#include "core/Authorization.hpp"
#include "core/DeviceSession.hpp"
#include "core/OutputControl.hpp"
#include "desktop/AlsaDesktopMixer.hpp"
#include "desktop/DesktopVolumeLink.hpp"
#include "transport/ControlLease.hpp"
#include "transport/UsbTransport.hpp"

using namespace lewitt;

namespace {
volatile std::sig_atomic_t gStop = 0;
void onSignal(int) { gStop = 1; }

struct Args {
    bool writes{false};
    double maxIncreaseDb{10.0};
    int pollMs{1000};
    bool ok{true};
};

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--allow-hardware-writes") a.writes = true;
        else if (s == "--max-increase-db" && i + 1 < argc) {
            char* end = nullptr;
            a.maxIncreaseDb = std::strtod(argv[++i], &end);
            if (!end || *end || !std::isfinite(a.maxIncreaseDb) || a.maxIncreaseDb < 0 || a.maxIncreaseDb > 20) a.ok = false;
        } else if (s == "--poll-ms" && i + 1 < argc) {
            char* end = nullptr;
            const long v = std::strtol(argv[++i], &end, 10);
            if (!end || *end || v < 250 || v > 5000) a.ok = false;
            a.pollMs = static_cast<int>(v);
        } else {
            a.ok = false;
        }
    }
    return a;
}

void say(const std::string& m) { std::fprintf(stderr, "%s\n", m.c_str()); }

// Log a condition once until it changes (no journal spam while waiting).
struct OnceLog {
    std::string last;
    void operator()(const std::string& m) {
        if (m != last) say(m);
        last = m;
    }
};

std::unique_ptr<DeviceSession> makeSession(bool writes) {
    if (writes)
        return std::make_unique<DeviceSession>(std::make_unique<UsbTransport>(outputControlHardwareOptions()),
                                               outputWritePolicy(/*speaker=*/true, /*headphone=*/false));
    return std::make_unique<DeviceSession>(std::make_unique<UsbTransport>(readOnlyHardwareOptions()), WritePolicy{});
}

void sleepMs(int ms) {
    for (int i = 0; i < ms / 50 && !gStop; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
}  // namespace

int main(int argc, char** argv) {
    Args a = parse(argc, argv);
    if (!a.ok) {
        say("usage: openwitty-volumed [--allow-hardware-writes] [--max-increase-db 0..20] [--poll-ms 250..5000]");
        return 64;
    }
    if (geteuid() == 0) {
        say("refusing to run as root: run openwitty-volumed in the desktop user's session");
        return 2;
    }
    if (a.writes && !(envAuthorized("LEWITT_ALLOW_HARDWARE_WRITE") && envAuthorized("LEWITT_ALLOW_WRITE_SPEAKER_VOLUME"))) {
        say("refused: --allow-hardware-writes also needs LEWITT_ALLOW_HARDWARE_WRITE=1 and "
            "LEWITT_ALLOW_WRITE_SPEAKER_VOLUME=1 (exactly \"1\")");
        return 2;
    }
    std::signal(SIGTERM, onSignal);
    std::signal(SIGINT, onSignal);
    std::signal(SIGHUP, onSignal);

    VolumeSyncConfig cfg;
    cfg.maxIncreaseDb = a.maxIncreaseDb;
    LinkOptions opt;
    opt.hardwareWrites = a.writes;
    opt.desktopWrites = a.writes;              // a dry run changes nothing at all, not even ALSA
    say(std::string("openwitty-volumed: ") + (a.writes ? "hardware writes ENABLED (speaker volume/mute only)"
                                                       : "dry run (no writes)"));

    std::unique_ptr<DeviceSession> session;
    AlsaDesktopMixer mixer;
    std::unique_ptr<DesktopVolumeLink> link;
    OnceLog wait;
    std::string lastNotice;
    auto nextPoll = DesktopVolumeLink::Clock::now();
    const std::string dir = ControlLease::currentSessionDirectory();

    auto release = [&] {
        link.reset();
        if (session) session->disconnect();
        session.reset();
    };

    while (!gStop) {
        if (ControlLease::yieldRequestedInDirectory(dir, std::chrono::seconds(30))) {
            if (session) say("another OpenWitty client asked for the device: releasing it");
            release();
            wait("waiting: device handed over to another OpenWitty client");
            sleepMs(1000);
            continue;
        }
        if (!session || session->needsReconnect()) {
            link.reset();
            session = makeSession(a.writes);
            if (auto s = session->connect(); !s) {
                session.reset();
                wait(s.error().code == Errc::ControlInUse ? "waiting: device in use by another OpenWitty client"
                                                          : "waiting for the device: " + s.error().message);
                sleepMs(2000);
                continue;
            }
            say("device connected");
        }
        if (!mixer.isOpen()) {
            link.reset();
            if (auto s = mixer.open(); !s) {
                wait("waiting for the desktop control: " + s.error().message);
                sleepMs(2000);
                continue;
            }
            say("desktop control: ALSA card " + std::to_string(mixer.cardIndex()));
        }
        if (!link) {
            link = std::make_unique<DesktopVolumeLink>(*session, mixer, cfg, opt, say);
            if (!link->start(DesktopVolumeLink::Clock::now())) {
                release();
                continue;
            }
            wait.last.clear();
            nextPoll = DesktopVolumeLink::Clock::now() + std::chrono::milliseconds(a.pollMs);
        }
        const auto untilPoll = std::chrono::duration_cast<std::chrono::milliseconds>(
            nextPoll - DesktopVolumeLink::Clock::now()).count();
        const int r = mixer.waitForEvent(static_cast<int>(std::clamp<long long>(untilPoll, 0, 250)));
        const auto now = DesktopVolumeLink::Clock::now();
        if (r < 0) {
            say("desktop control gone (card removed?)");
            mixer.close();
            link.reset();
            continue;
        }
        if (r == 1) link->onDesktopEvent(now);
        if (now >= nextPoll) {
            link->onPollTick(now);
            nextPoll = now + std::chrono::milliseconds(a.pollMs);
        }
        if (link->notice() != lastNotice) {
            lastNotice = link->notice();
            if (!lastNotice.empty()) say("notice: " + lastNotice);
        }
        if (!link->healthy()) {
            say("device link lost: reconnecting (read-only start again)");
            release();
        }
    }
    release();
    say("openwitty-volumed: stopped");
    return 0;
}
