// openwitty-volumed: keeps the desktop's view of the CONNECT 2 speaker in step with the device.
//
// The KDE/PipeWire volume of the CONNECT 2 sink ends up in the card's ALSA "PCM Playback" control and,
// in the normal case (verified on an ordinary boot, Phase 8E), reaches the device through the kernel:
// KDE/PipeWire stay authoritative for slider moves. The kernel cannot read the speaker level back
// ("broken GET_CUR"), so it never learns about hardware-knob changes; this daemon reads the device
// through the vendor protocol (interface 3) and mirrors such changes to the desktop.
//
//   openwitty-volumed                    mirror mode (default): device opened READ-ONLY; knob changes are
//                                        mirrored to the ALSA control under the alignment safety rules
//                                        (fresh re-read, round down, never unmute); a desktop change the
//                                        device does not follow is reported, never corrected
//   openwitty-volumed --vendor-fallback  additionally applies such a change through the vendor settings
//                                        message (speaker bytes only; for the verified exceptional state
//                                        in which the device ignores Feature Unit 2). Needs
//                                        LEWITT_ALLOW_HARDWARE_WRITE=1 and LEWITT_ALLOW_WRITE_SPEAKER_VOLUME=1
//   openwitty-volumed --dry-run          writes nothing at all (neither device nor ALSA), logs decisions
//   openwitty-volumed --check            diagnosis of the desktop audio setup: no OpenWitty vendor USB access
//                                        and no intentional hardware SET; it does open the ALSA mixer,
//                                        which can make the kernel issue USB control reads to the device
// Options: --max-increase-db N (0..20, default 10, fallback only)  --poll-ms N (250..5000, default 1000)
// Policy: core/VolumeSync.hpp (startup is read-only, bounded increases, no retries of unverified
// writes, knob changes win over older desktop requests).
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
#include <alsa/asoundlib.h>

#include "desktop/AlsaDesktopMixer.hpp"
#include "desktop/AudioSetupCheck.hpp"
#include "desktop/DesktopVolumeLink.hpp"
#include "transport/ControlLease.hpp"
#include "transport/UsbTransport.hpp"

using namespace lewitt;

namespace {
volatile std::sig_atomic_t gStop = 0;
void onSignal(int) { gStop = 1; }

enum class Mode { Mirror, Fallback, DryRun, Check };

struct Args {
    Mode mode{Mode::Mirror};
    bool writes{false};               // vendor writes (fallback mode only)
    double maxIncreaseDb{10.0};
    int pollMs{1000};
    bool ok{true};
};

Args parse(int argc, char** argv) {
    Args a;
    int modeFlags = 0;
    bool increaseGiven = false, pollGiven = false;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--vendor-fallback") a.mode = Mode::Fallback, ++modeFlags;
        else if (s == "--dry-run") a.mode = Mode::DryRun, ++modeFlags;
        else if (s == "--check") a.mode = Mode::Check, ++modeFlags;
        else if (s == "--max-increase-db" && i + 1 < argc) {
            increaseGiven = true;
            char* end = nullptr;
            a.maxIncreaseDb = std::strtod(argv[++i], &end);
            if (!end || *end || !std::isfinite(a.maxIncreaseDb) || a.maxIncreaseDb < 0 || a.maxIncreaseDb > 20) a.ok = false;
        } else if (s == "--poll-ms" && i + 1 < argc) {
            pollGiven = true;
            char* end = nullptr;
            const long v = std::strtol(argv[++i], &end, 10);
            if (!end || *end || v < 250 || v > 5000) a.ok = false;
            a.pollMs = static_cast<int>(v);
        } else {
            a.ok = false;
        }
    }
    // Exactly one mode, decided once: no flag silently overrides another, and write authorization is
    // derived from the final mode only (never left over from an earlier flag).
    if (modeFlags > 1) a.ok = false;
    if (increaseGiven && a.mode != Mode::Fallback) a.ok = false;    // only the fallback ever raises a level
    if (pollGiven && a.mode == Mode::Check) a.ok = false;           // --check takes no options
    a.writes = a.ok && a.mode == Mode::Fallback;
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

int runCheck() {
    const AudioSetupStatus st = collectAudioSetup();
    auto row = [](const char* k, const std::string& v) { std::printf("%-32s %s\n", k, v.c_str()); };
    row("CONNECT 2 sound card:", st.card ? "ALSA card " + std::to_string(*st.card) : "not found");
    row("ALSA mixer loads:", !st.card ? "-" : st.mixerLoadError ? snd_strerror(*st.mixerLoadError) : "yes");
    row("hardware volume control:", st.volumeControl ? "yes (PCM Playback Volume)" : "no");
    row("compatibility quirk configured:", st.quirkConfigured ? "yes" : "no");
    for (const auto& f : st.quirkFiles) row("", f);
    row("compatibility quirk active:", !st.moduleLoaded ? "snd-usb-audio not loaded" : st.quirkActive ? "yes" : "no");
    const auto findings = audioSetupFindings(st);
    if (findings.empty()) std::printf("\nDesktop hardware volume is available.\n");
    for (const auto& f : findings) std::printf("\n- %s\n", f.c_str());
    return findings.empty() ? 0 : 1;
}

void sleepMs(int ms) {
    for (int i = 0; i < ms / 50 && !gStop; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
}  // namespace

int main(int argc, char** argv) {
    Args a = parse(argc, argv);
    if (!a.ok) {
        say("usage: openwitty-volumed [--poll-ms 250..5000]                     (mirror mode, device read-only)\n"
            "       openwitty-volumed --vendor-fallback [--max-increase-db 0..20] [--poll-ms 250..5000]\n"
            "       openwitty-volumed --dry-run [--poll-ms 250..5000]\n"
            "       openwitty-volumed --check\n"
            "Exactly one mode flag at most; --max-increase-db only with --vendor-fallback.");
        return 64;
    }
    if (geteuid() == 0) {
        say("refusing to run as root: run openwitty-volumed in the desktop user's session");
        return 2;
    }
    if (a.mode == Mode::Check) return runCheck();
    if (a.writes && !(envAuthorized("LEWITT_ALLOW_HARDWARE_WRITE") && envAuthorized("LEWITT_ALLOW_WRITE_SPEAKER_VOLUME"))) {
        say("refused: --vendor-fallback also needs LEWITT_ALLOW_HARDWARE_WRITE=1 and "
            "LEWITT_ALLOW_WRITE_SPEAKER_VOLUME=1 (exactly \"1\")");
        return 2;
    }
    std::signal(SIGTERM, onSignal);
    std::signal(SIGINT, onSignal);
    std::signal(SIGHUP, onSignal);

    VolumeSyncConfig cfg;
    cfg.maxIncreaseDb = a.maxIncreaseDb;
    LinkOptions opt;
    opt.deviceWrites = a.mode == Mode::Fallback ? DeviceWriteMode::Fallback
                     : a.mode == Mode::Mirror   ? DeviceWriteMode::Native
                                                : DeviceWriteMode::None;
    opt.desktopWrites = a.mode != Mode::DryRun;   // a dry run changes nothing at all, not even ALSA
    say(std::string("openwitty-volumed: ") +
        (a.mode == Mode::Fallback ? "mirror + vendor fallback (device writes: speaker volume/mute only)"
         : a.mode == Mode::Mirror ? "mirror mode (device read-only; desktop control follows the device)"
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
