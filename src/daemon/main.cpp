// openwitty-volumed: keeps the desktop's view of the CONNECT 2 speaker in step with the device.
//
// The KDE/PipeWire volume of the CONNECT 2 sink ends up in the card's ALSA "PCM Playback" control and,
// in the normal case (verified on an ordinary boot, Phase 8E), reaches the device through the kernel:
// KDE/PipeWire stay authoritative for slider moves. The kernel cannot read the speaker level back
// ("broken GET_CUR"), so it never learns about hardware-knob changes; this daemon reads the device
// through the vendor protocol (interface 3) and mirrors such changes to the desktop.
//
//   openwitty-volumed                    follows the user configuration ($XDG_CONFIG_HOME/openwitty/
//                                        openwitty.conf, written by the GUI) and its changes while running:
//                                        off = idle, mirror, or mirror + vendor fallback. Without a file: idle.
//   openwitty-volumed --mirror           mirror mode regardless of the configuration: device opened READ-ONLY; knob changes are
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
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>

#include "core/Authorization.hpp"
#include "core/DeviceSession.hpp"
#include "core/OutputControl.hpp"
#include "core/UserConfig.hpp"
#include <alsa/asoundlib.h>

#include "desktop/AlsaDesktopMixer.hpp"
#include "desktop/AudioSetupCheck.hpp"
#include "core/OutputFollow.hpp"
#include "desktop/DesktopVolumeLink.hpp"
#include "desktop/PortNames.hpp"
#include "transport/ControlLease.hpp"
#include "transport/UsbTransport.hpp"

using namespace lewitt;

namespace {
volatile std::sig_atomic_t gStop = 0;
void onSignal(int) { gStop = 1; }

enum class Mode { Mirror, Fallback, DryRun, Check };

struct Args {
    Mode mode{Mode::Mirror};
    bool explicitMode{false};         // a mode flag was given: the configuration file does not apply
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
        else if (s == "--mirror") a.mode = Mode::Mirror, ++modeFlags;
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
    a.explicitMode = modeFlags == 1;
    if (increaseGiven && a.mode != Mode::Fallback) a.ok = false;    // only the fallback ever raises a level
    if (pollGiven && a.mode == Mode::Check) a.ok = false;           // --check takes no options
    a.writes = a.ok && a.mode == Mode::Fallback;
    return a;
}

// True only if stderr IS the journal stream systemd announced (JOURNAL_STREAM="<dev>:<inode>", see
// systemd.exec(5)); an inherited variable with a different stderr does not count.
bool stderrIsJournal() {
    const char* js = std::getenv("JOURNAL_STREAM");
    unsigned long long dev = 0, ino = 0;
    if (!js || std::sscanf(js, "%llu:%llu", &dev, &ino) != 2) return false;
    struct stat st{};
    return fstat(STDERR_FILENO, &st) == 0 && static_cast<unsigned long long>(st.st_dev) == dev &&
           static_cast<unsigned long long>(st.st_ino) == ino;
}

// Under systemd the journal timestamps every line; elsewhere we add wall-clock time (ms).
void say(const std::string& m) {
    static const bool journal = stderrIsJournal();
    if (journal) {
        std::fprintf(stderr, "%s\n", m.c_str());
        return;
    }
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    char ts[16];
    std::strftime(ts, sizeof ts, "%H:%M:%S", std::localtime(&t));
    std::fprintf(stderr, "%s.%03lld %s\n", ts, static_cast<long long>(ms), m.c_str());
}

// Log a condition once until it changes (no journal spam while waiting).
struct OnceLog {
    std::string last;
    void operator()(const std::string& m) {
        if (m != last) say(m);
        last = m;
    }
};

std::unique_ptr<DeviceSession> makeSession(bool writes, bool follow) {
    if (writes || follow) {
        // vendor fallback: speaker bytes; follow active output (Phase 12C, confirmed in the GUI): byte 59 only
        WritePolicy p = writes ? outputWritePolicy(/*speaker=*/true, /*headphone=*/false) : WritePolicy{};
        if (follow) p = withFollowOutput(p);
        return std::make_unique<DeviceSession>(std::make_unique<UsbTransport>(outputControlHardwareOptions()), p);
    }
    return std::make_unique<DeviceSession>(std::make_unique<UsbTransport>(readOnlyHardwareOptions()), WritePolicy{});
}

FollowConfig followConfig(const UserConfig& c) {
    auto map = [](FollowPref f) {
        return f == FollowPref::Speakers ? FollowChoice::Speakers : f == FollowPref::Headphones ? FollowChoice::Headphones
             : f == FollowPref::Keep ? FollowChoice::Keep : FollowChoice::Disable;
    };
    return FollowConfig{map(c.followBothOn), map(c.followBothMuted), c.followMoveFocus};
}

// What the daemon is doing right now: from an explicit flag, or from the user configuration.
enum class Effective { Idle, Mirror, Fallback, DryRun };

Effective fromFlags(Mode m) {
    return m == Mode::Fallback ? Effective::Fallback : m == Mode::DryRun ? Effective::DryRun : Effective::Mirror;
}
Effective fromConfig(const UserConfig& c) {
    if (c.desktopVolumeSync == DesktopVolumeSync::Off) return Effective::Idle;
    return c.vendorFallback ? Effective::Fallback : Effective::Mirror;
}
const char* describe(Effective e) {
    switch (e) {
        case Effective::Idle: return "idle (desktop volume sync is off in the settings)";
        case Effective::Mirror: return "mirror mode (device read-only; desktop control follows the device)";
        case Effective::Fallback: return "mirror + vendor fallback (device writes: speaker volume/mute only)";
        case Effective::DryRun: return "dry run (no writes)";
    }
    return "?";
}

// Re-reads the user configuration when the file changes (checked every loop iteration, <= 250 ms).
struct ConfigWatch {
    std::string path = userConfigPath();
    bool exists{false};
    struct timespec mtime{};
    off_t size{-1};
    UserConfig config;
    bool poll() {                                     // true when (re)loaded
        struct stat st{};
        const bool e = !path.empty() && stat(path.c_str(), &st) == 0;
        if (e == exists && (!e || (st.st_mtim.tv_sec == mtime.tv_sec && st.st_mtim.tv_nsec == mtime.tv_nsec &&
                                   st.st_size == size)) && size != -1)
            return false;
        exists = e;
        mtime = e ? st.st_mtim : timespec{};
        size = e ? st.st_size : 0;
        auto parsed = loadUserConfig(path);
        for (const auto& w : parsed.warnings) say("settings: " + w);
        config = parsed.config;
        return true;
    }
};

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
        say("usage: openwitty-volumed [--poll-ms 250..5000]                     (follows the user settings)\n"
            "       openwitty-volumed --mirror [--poll-ms 250..5000]            (device read-only)\n"
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
    // Without a mode flag the user configuration decides (Settings > Desktop integration in the GUI);
    // a vendor fallback chosen there is the user's explicit authorization, so no environment variables.
    ConfigWatch watch;
    Effective eff = Effective::Idle;
    if (a.explicitMode) {
        eff = fromFlags(a.mode);
        say(std::string("openwitty-volumed: ") + describe(eff));
    } else {
        watch.poll();
        eff = fromConfig(watch.config);
        say(std::string("openwitty-volumed: ") + describe(eff) + " - settings: " +
            (watch.path.empty() ? std::string("(none)") : watch.path));
    }
    auto linkOptions = [&] {
        LinkOptions opt;
        opt.deviceWrites = eff == Effective::Fallback ? DeviceWriteMode::Fallback
                         : eff == Effective::Mirror   ? DeviceWriteMode::Native
                                                      : DeviceWriteMode::None;
        opt.desktopWrites = eff != Effective::DryRun;   // a dry run changes nothing at all, not even ALSA
        return opt;
    };

    std::unique_ptr<DeviceSession> session;
    AlsaDesktopMixer mixer;
    std::unique_ptr<DesktopVolumeLink> link;
    std::unique_ptr<OutputFollower> follower;
    // Follow active output: only from the user settings (never from a mode flag), and not in a dry run.
    auto followWanted = [&] { return !a.explicitMode && eff != Effective::DryRun && followWritesWanted(watch.config); };
    bool follow = followWanted();
    OnceLog wait;
    std::string lastNotice;
    auto nextPoll = DesktopVolumeLink::Clock::now();
    auto nextNames = DesktopVolumeLink::Clock::now();
    const std::string dir = ControlLease::currentSessionDirectory();

    auto release = [&] {
        follower.reset();
        link.reset();
        if (session) session->disconnect();
        session.reset();
    };

    while (!gStop) {
        if (!a.explicitMode && watch.poll()) {
            const Effective e = fromConfig(watch.config);
            const bool f = !a.explicitMode && e != Effective::DryRun && followWritesWanted(watch.config);
            if (e != eff || f != follow) {
                say(std::string("settings changed: ") + describe(e) + (f ? " + follow active output" : ""));
                release();                    // the next start is read-only again, with the new mode
                mixer.close();
                eff = e;
                follow = f;
                wait.last.clear();
            } else if (follower) {
                follower = std::make_unique<OutputFollower>(*session, followConfig(watch.config), PollPolicy{}, say);
            }
        }
        // Readable CONNECT 2 channel names in audio apps (display-only PipeWire metadata; set again after a replug or
        // a PipeWire restart). From the user settings only, never in a dry run.
        if (!a.explicitMode && eff != Effective::DryRun && watch.config.nameChannels &&
            DesktopVolumeLink::Clock::now() >= nextNames) {
            nextNames = DesktopVolumeLink::Clock::now() + std::chrono::seconds(10);
            if (const int n = applyPortNames(); n > 0) say("channel names set in audio apps: " + std::to_string(n));
        }
        if (eff == Effective::Idle) {
            release();
            if (mixer.isOpen()) mixer.close();
            wait("idle: desktop volume sync is off in the settings");
            sleepMs(1000);
            continue;
        }
        if (ControlLease::yieldRequestedInDirectory(dir, std::chrono::seconds(30))) {
            if (session) say("another OpenWitty client asked for the device: releasing it");
            release();
            wait("waiting: device handed over to another OpenWitty client");
            sleepMs(1000);
            continue;
        }
        if (!session || session->needsReconnect()) {
            link.reset();
            session = makeSession(eff == Effective::Fallback, follow);
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
            link = std::make_unique<DesktopVolumeLink>(*session, mixer, cfg, linkOptions(), say);
            if (!link->start(DesktopVolumeLink::Clock::now())) {
                release();
                continue;
            }
            wait.last.clear();
            nextPoll = DesktopVolumeLink::Clock::now() + std::chrono::milliseconds(a.pollMs);
            if (follow) {
                follower = std::make_unique<OutputFollower>(*session, followConfig(watch.config), PollPolicy{}, say);
                say("follow active output: on");
            }
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
            // Follow active output first (own fresh read), so the link never mirrors the old output's state after a
            // custom-button press; it then re-targets on its own read. Within one poll (<= 250 ms).
            // Active: the manual switch, or this device's extended custom-button mode whose base it still holds (12D).
            if (follower && session->refreshSettings() && session->settings() &&
                followActive(watch.config, deviceKey(session->info().serial), session->settings()->customButton()))
                follower->step();
            link->onPollTick(now);            // fresh settings read
            nextPoll = now + std::chrono::milliseconds(follower ? std::min(a.pollMs, 250) : a.pollMs);
        }
        if (link->notice() != lastNotice) {
            say(link->notice().empty() ? "notice cleared: desktop and device agree again" : "notice: " + link->notice());
            lastNotice = link->notice();
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
