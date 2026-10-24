// openwitty-cli: read-only diagnostic tool, plus ONE narrowly-scoped experimental write command.
//
//   devices                 enumerate LEWITT devices from sysfs: descriptors, interfaces,
//                           endpoints, bound kernel drivers, usbfs node permissions (no USB I/O)
//   diagnose                devices + what each finding means for control access + next steps
//   read api|version|status|settings [--method M] [--request R] [--interface N] [--mock]
//                           one read-only control transfer; errors are classified
//   probe [--execute]       dry-run (default) or run a small matrix of GET-only experiments
//   list                    quick libusb enumeration (VID/PID only)
//   write led-brightness <0-100> [--execute] [--interface N] [--mock]
//   write instrument-mute <on|off> [--execute] [--interface N] [--mock]
//   write speaker-volume <-60..0 dB> [--execute] [--interface N] [--mock]
//   write speaker-mute <on|off> [--execute] [--interface N] [--mock]
//                           EXPERIMENTAL, gated single-field writes (settings byte 37 / byte 12 /
//                           bytes 20-21 / byte 22
//                           only; see cmdWriteField below for the full safety rationale). Without
//                           --execute this only computes and prints a dry-run proposal; no USB
//                           write is ever sent. With --execute against real hardware it additionally
//                           requires LEWITT_ALLOW_HARDWARE_WRITE=1, and instrument-mute also
//                           LEWITT_ALLOW_WRITE_INSTRUMENT_MUTE=1, speaker-volume
//                           and speaker-mute LEWITT_ALLOW_WRITE_SPEAKER_VOLUME=1. There is no other write command,
//                           no generic field/byte setter, and no trigger-message write here.
//
// Methods:  interface (default) | device-recipient | detach (DISRUPTIVE, needs
//           LEWITT_ALLOW_DISRUPTIVE=1 as well).  Requests: uac2 (default) | uac1.
// --interface auto selects the control interface from the descriptors exactly like the GUI does
// (unbound vendor-specific ff/f0, never DFU, fails closed). An explicit N pointing at a DFU
// (class 0xFE) interface is refused by the transport.
// --interface N overrides which interface number goes in wIndex (default: auto-discover the
// AudioControl interface). Hardware session 2026-10-14 found the AudioControl interface (0) fails
// with an IO error while the kernel driver owns it; an unbound vendor-specific interface (3 on the
// CONNECT 2) answers read-only GETs instead (docs/USB_ACCESS.md §3b). This never claims or detaches
// anything explicitly regardless of interface number. usbfs can still claim implicitly for an
// interface-recipient GET. Never pass the DFU interface (4 on the tested CONNECT 2) here.
#ifdef LEWITT_HAVE_USB
#include <libusb.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/DeviceManager.hpp"
#include "core/DeviceSession.hpp"
#include "transport/MockTransport.hpp"
#include "core/Authorization.hpp"
#include "transport/ControlLease.hpp"
#include "transport/UsbDiagnostics.hpp"
#include "transport/UsbTransport.hpp"
#include "lewitt/version.hpp"

using namespace lewitt;

namespace {

void hex(std::span<const std::uint8_t> b) {
    for (auto c : b) std::printf("%02x", c);
    std::printf("\n");
}

void printSettings(const SettingsMessage& s) {
    for (int i = 0; i < 2; ++i)
        std::printf("%s: gain=%.2f dB mute=%d lowcut=%d clipguard=%d preamp=%u denoise=%d comp=%d\n",
                    i == 0 ? "microphone" : "instrument", s.gainDb(i), s.mute(i), s.highPass(i), s.clipguard(i),
                    s.preampRaw(i), s.denoise(i), s.compress(i));
    std::printf("phantom=%d  speaker=%.2f dB%s  headphone=%.2f dB%s  dim=%d mono=%d mix=%.2f monitor=%u button=%u\n",
                s.phantom1(), s.speakerVolumeDb(), s.speakerMute() ? " (muted)" : "", s.headphoneVolumeDb(),
                s.headphoneMute() ? " (muted)" : "", s.dim(), s.mono(), s.inputPlaybackMix(),
                unsigned(s.monitorSource()), s.customButton());
    std::printf("raw=");
    hex(s.bytes());
}

int exitCodeFor(Errc c) {
    switch (c) {
        case Errc::AccessDenied: return 3;
        case Errc::Busy: return 4;
        case Errc::ControlInUse: return 4;
        case Errc::Stall: return 5;
        case Errc::Timeout: return 6;
        case Errc::Disconnected: case Errc::NotFound: return 7;
        case Errc::NotSupported: return 8;
        case Errc::ShortTransfer: case Errc::Malformed: return 9;
        default: return 1;
    }
}

void printError(const Error& e, const char* what) {
    std::fprintf(stderr, "%s failed [%s]: %s\n", what, errcName(e.code), e.message.c_str());
    auto sug = suggestionFor(e);
    if (!sug.empty()) std::fprintf(stderr, "  -> %s\n", sug.c_str());
}

int cmdDevices(bool analysis) {
    auto devs = scanSysfs(kLewittVendorId);
    if (devs.empty()) {
        std::puts("no LEWITT USB device (VID 0x29c2) present in sysfs");
        return 1;
    }
    for (const auto& r : devs) {
        std::fputs(formatReport(r).c_str(), stdout);
        auto c = DeviceManager::classify(r);
        std::printf("  mode: %s%s\n", c.mode == DeviceMode::Normal ? "normal (APP, PID 0x0004)"
                                      : c.mode == DeviceMode::MacOSStreaming ? "macOS streaming (APP, PID 0x0002)"
                                      : "unknown / not an APP mode the original accepts",
                    c.isConnect2 ? "" : "  [PID not in the known CONNECT 2 list]");
        if (!analysis) continue;
        std::puts("  analysis:");
        const UsbInterfaceInfo* ac = nullptr;
        for (const auto& i : r.interfaces)
            if (i.klass == 1 && i.subclass == 1) { ac = &i; break; }
        if (!ac) std::puts("    - no AudioControl interface found in the descriptors");
        else {
            std::printf("    - AudioControl interface is %d (%s)%s\n", ac->number,
                        ac->bcdAdc >= 0x0200 ? "UAC2" : ac->bcdAdc ? "UAC1" : "ADC version unknown",
                        ac->boundDriver.empty() ? "" : (", bound to " + ac->boundDriver).c_str());
            if (!ac->boundDriver.empty())
                std::puts("      => interface-recipient control requests from userspace will fail with EBUSY\n"
                          "         (usbfs must claim the interface first; verified in kernel devio.c).");
        }
        if (!r.nodeWritable)
            std::puts("    - usbfs node is not writable for this user => libusb_open fails with EACCES;\n"
                      "      an administrator-installed udev rule is required (separate from driver ownership).");
        else
            std::puts("    - usbfs node is writable: libusb_open should succeed.");
        std::puts("    - descriptors above come from sysfs; no control transfer was sent.");
    }
    return 0;
}

struct ReadArgs {
    std::string what;
    bool mock{false};
    AccessMethod method{AccessMethod::InterfaceRecipient};
    MemRequest request{MemRequest::Uac2Mem};
    int interfaceNumber{-1};   // -1 = legacy AudioControl discovery (default, unchanged)
    bool autoInterface{false}; // --interface auto: descriptor-driven vendor control interface, fails closed
    bool bad{false};
};

ReadArgs parseRead(int argc, char** argv, int first) {
    ReadArgs a;
    if (first < argc) a.what = argv[first];
    for (int i = first + 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--mock")) a.mock = true;
        else if (!std::strcmp(argv[i], "--method") && i + 1 < argc) {
            std::string m = argv[++i];
            if (m == "interface") a.method = AccessMethod::InterfaceRecipient;
            else if (m == "device-recipient") a.method = AccessMethod::DeviceRecipient;
            else if (m == "detach") a.method = AccessMethod::DetachAndClaim;
            else { std::fprintf(stderr, "unknown --method %s\n", m.c_str()); a.bad = true; }
        } else if (!std::strcmp(argv[i], "--request") && i + 1 < argc) {
            std::string r = argv[++i];
            if (r == "uac1") a.request = MemRequest::Uac1Mem;
            else if (r == "uac2") a.request = MemRequest::Uac2Mem;
            else { std::fprintf(stderr, "unknown --request %s\n", r.c_str()); a.bad = true; }
        } else if (!std::strcmp(argv[i], "--interface") && i + 1 < argc) {
            std::string v = argv[++i];
            if (v == "auto") a.autoInterface = true;      // descriptor-driven, fails closed
            else a.interfaceNumber = std::atoi(v.c_str());
        } else { std::fprintf(stderr, "unknown argument %s\n", argv[i]); a.bad = true; }
    }
    return a;
}

int cmdRead(const ReadArgs& a) {
    MessageId id = a.what == "api" ? MessageId::ApiVersion
                   : a.what == "version" ? MessageId::Version
                   : a.what == "status" ? MessageId::Status
                   : a.what == "settings" ? MessageId::Settings : static_cast<MessageId>(0xFF);
    if (static_cast<int>(id) == 0xFF) { std::fprintf(stderr, "read what? api|version|status|settings\n"); return 64; }
    if (a.method == AccessMethod::DetachAndClaim && !envAuthorized("LEWITT_ALLOW_DISRUPTIVE")) {
        std::fprintf(stderr,
                     "refused: --method detach drops the sound card (detaches snd-usb-audio) and also needs "
                     "LEWITT_ALLOW_DISRUPTIVE=1\n");
        return 2;
    }
    std::unique_ptr<IDeviceTransport> t;
    if (a.mock) t = std::make_unique<MockTransport>();
    else {
        // pre-flight from sysfs so the user learns the likely failure before libusb reports it
        for (const auto& r : scanSysfs(kLewittVendorId))
            if (!r.nodeWritable)
                std::fprintf(stderr, "note: %s is not writable for this user; opening will fail with EACCES\n",
                             r.usbfsNode.c_str());
        UsbOptions o;
        o.request = a.request;
        o.method = a.method;
        o.interfaceNumber = a.interfaceNumber;
        if (a.autoInterface) o.interfaceStrategy = InterfaceStrategy::VendorControlAuto;
        o.allowDisruptive = envAuthorized("LEWITT_ALLOW_DISRUPTIVE");   // checked above too
        t = std::make_unique<UsbTransport>(o);
    }
    if (auto s = t->open(); !s) { printError(s.error(), "open"); return exitCodeFor(s.error().code); }
    auto r = t->read(id);
    if (!r) { printError(r.error(), "read"); return exitCodeFor(r.error().code); }
    if (id == MessageId::Settings) {
        auto m = SettingsMessage::fromBytes(r.value());
        if (!m) { printError(m.error(), "decode"); return exitCodeFor(m.error().code); }
        printSettings(m.value());
    } else if (id == MessageId::ApiVersion) {
        auto m = ApiVersionMessage::fromBytes(r.value());
        if (!m) { printError(m.error(), "decode"); return exitCodeFor(m.error().code); }
        std::printf("api major=%u minor=%u (supported major: %u)%s\n", m.value().major(), m.value().minor(),
                    kSupportedApiMajor, m.value().major() == kSupportedApiMajor ? "" : "  ** UNSUPPORTED **");
        std::printf("raw=");
        hex(r.value());
    } else {
        hex(r.value());
    }
    return 0;
}

// Experimental, narrowly-scoped write path. The ONLY thing this can ever write is LED
// brightness (settings byte 37 / settings_off::kLedBrightness) via SettingsMessage::setLedBrightness,
// which clamps to 0..100 and touches no other byte. There is no generic "set any field" or
// "set any byte" command, and no trigger-message write command, on this CLI.
//
// Safety, all enforced below or by already-reviewed/tested DeviceSession/WriteGuard code it calls
// into (nothing here bypasses that machinery):
//   - handshake() (inside session.connect()) already verifies API major==1 and that the USB PID is
//     a recognized CONNECT 2 APP mode before any settings I/O happens.
//   - session.connect() performs a fresh settings read (refreshSettings()) immediately before any
//     edit is computed; nothing is cached across CLI invocations (each run is a fresh process).
//   - edit() copies the just-read full 128-byte struct and changes only byte 37; every other byte,
//     known or reserved, is preserved verbatim.
//   - Before even attempting a write, this command independently recomputes the before/after byte
//     diff and refuses (even with --execute) unless EXACTLY byte 37 differs — stricter than, and in
//     addition to, WriteGuard's generic verifyOnlyKnownFieldsChanged allowlist that commit() already
//     runs.
//   - commit() (existing, tested code) re-checks size/permission, refuses routing/trigger/mode-switch
//     writes, performs the one write, then re-reads settings to report the real post-write state.
//   - Real-hardware writes need BOTH `--execute` AND LEWITT_ALLOW_DISRUPTIVE-style explicit opt-in
//     (LEWITT_ALLOW_HARDWARE_WRITE=1) and a transport with allowWrites set only for this invocation;
//     without --execute this only ever reads and prints a dry-run proposal, no write is sent.
//   - Always interface 3 by default (the only interface confirmed to answer this device's protocol
//     without disturbing snd-usb-audio); never interface 4 (DFU) or interface 0 (kernel-owned, and
//     writes there aren't attempted by this command regardless).
//   - No detach, no claim beyond what UsbTransport's InterfaceRecipient path already does for reads
//     (none), no reset, no mode switch, no trigger words, no DFU.
#ifdef LEWITT_HAVE_USB
// ---- single-field experimental writes ------------------------------------------------------
// Two fields exist, each approved separately (see docs/HARDWARE_TEST_RESULTS.md):
//   led-brightness <0-100>   settings byte 37   (executed on hardware 2026-10-14, Phase 6C)
//   instrument-mute <on|off> settings byte 12   (Phase 6E: PREPARED, never executed on hardware)
//   speaker-volume <dB>      settings bytes 20-21, i16 Q8.8 in -60..0 dB (Phase 8C)
//   speaker-mute <on|off>    settings byte 22 (Phase 8C)
// All share one implementation so the safety rules cannot drift apart:
//   * the session's hardwareWriteOffsets contains exactly that field's offset (core-level, not
//     only here), so even a bug in this file cannot write any other byte on real hardware;
//   * the byte diff proposed vs. the fresh device snapshot must be exactly {offset}, checked here
//     again independently of WriteGuard;
//   * real hardware + --execute additionally needs LEWITT_ALLOW_HARDWARE_WRITE=1 *and* the
//     per-field variable named in FieldSpec::authEnv (an authorization is never inherited from
//     another field's experiment);
//   * commitAndVerify(): fresh snapshot + stability check immediately before the SET, exactly one
//     SET (never retried), bounded read-only polling afterwards, anything unexpected is reported
//     as ambiguous rather than "fixed";
//   * interface 3 only by default; no detach/claim/reset/mode-switch/trigger/DFU anywhere.
// Not atomic: the 128-byte SET can race physical actions on the device (e.g. a Panic Mute
// long-press). The stability check narrows but cannot close that window; see HARDWARE_TEST_RESULTS.
struct FieldSpec {
    const char* name;            // CLI sub-command
    std::size_t offset;          // the first settings byte this command may change
    const char* authEnv;         // extra per-field authorization for real hardware, or nullptr
    std::size_t length{1};       // number of bytes of the field (2 for Q8.8 volumes)
};

struct WriteArgs {
    int value{-1};               // percent for led-brightness, 0/1 for instrument-mute, 0 for volumes
    double db{0.0};              // dB for speaker-volume
    bool execute{false};
    int interfaceNumber{3};      // the only hardware-confirmed working interface for this device
    bool mock{false};
    bool bad{false};
};

WriteArgs parseWrite(int argc, char** argv, int first, const std::string& field) {
    WriteArgs a;
    if (first < argc) {
        std::string v = argv[first];
        if (field == "instrument-mute" || field == "speaker-mute") {
            if (v == "on" || v == "1") a.value = 1;
            else if (v == "off" || v == "0") a.value = 0;
            else { std::fprintf(stderr, "%s takes on|off\n", field.c_str()); a.bad = true; }
        } else if (field == "speaker-volume") {
            char* end = nullptr;
            a.db = std::strtod(v.c_str(), &end);
            if (end == v.c_str() || *end != '\0' || !std::isfinite(a.db)) {
                std::fprintf(stderr, "speaker-volume takes a finite dB value\n");
                a.bad = true;
            }
            else a.value = 0;
        } else {
            a.value = std::atoi(argv[first]);
        }
    }
    for (int i = first + 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--execute")) a.execute = true;
        else if (!std::strcmp(argv[i], "--mock")) a.mock = true;
        else if (!std::strcmp(argv[i], "--interface") && i + 1 < argc) a.interfaceNumber = std::atoi(argv[++i]);
        else { std::fprintf(stderr, "unknown argument %s\n", argv[i]); a.bad = true; }
    }
    return a;
}

const char* modeName(DeviceMode m) {
    switch (m) {
        case DeviceMode::Normal: return "normal (APP, PID 0x0004)";
        case DeviceMode::MacOSStreaming: return "macOS streaming (APP, PID 0x0002)";
        default: return "unknown / not a recognized CONNECT 2 APP mode";
    }
}

int cmdWriteField(const FieldSpec& f, const WriteArgs& a) {
    const bool isSpeakerMute = f.offset == settings_off::kSpeakerMute;
    const bool isMute = f.offset == settings_off::kMute[1] || isSpeakerMute;
    const bool isSpeakerVolume = f.offset == settings_off::kSpeakerVolume;
    if (isSpeakerVolume ? (!std::isfinite(a.db) || a.db < kVolumeMinDb || a.db > kVolumeMaxDb)
                        : isMute ? (a.value < 0 || a.value > 1) : (a.value < 0 || a.value > 100)) {
        std::fprintf(stderr, "%s: value out of range\n", f.name);
        return 64;
    }
    if (a.execute && !a.mock) {
        if (!envAuthorized("LEWITT_ALLOW_HARDWARE_WRITE")) {
            std::fprintf(stderr,
                         "refused: --execute against real hardware needs LEWITT_ALLOW_HARDWARE_WRITE=1 "
                         "(this sends a real MEM SET to the device; without it, nothing is written)\n");
            return 2;
        }
        if (f.authEnv && !envAuthorized(f.authEnv)) {
            std::fprintf(stderr,
                         "refused: writing '%s' on real hardware additionally needs %s=1. That "
                         "authorization is specific to this field and must be given explicitly for "
                         "each experiment.\n", f.name, f.authEnv);
            return 2;
        }
    }
    std::unique_ptr<IDeviceTransport> t;
    WritePolicy pol;
    if (a.mock) {
        t = std::make_unique<MockTransport>();
    } else {
        for (const auto& r : scanSysfs(kLewittVendorId))
            if (!r.nodeWritable)
                std::fprintf(stderr, "note: %s is not writable for this user; opening will fail with EACCES\n",
                             r.usbfsNode.c_str());
        UsbOptions o;
        o.method = AccessMethod::InterfaceRecipient;
        o.request = MemRequest::Uac2Mem;
        o.interfaceNumber = a.interfaceNumber;
        o.allowWrites = a.execute;   // transport-level: no write ioctl is even attempted on a dry run
        t = std::make_unique<UsbTransport>(o);
    }
    pol.allowHardwareWrites = a.execute;           // session-level gate; irrelevant for mock
    for (std::size_t i = 0; i < f.length; ++i)    // core-level scope: exactly this field's bytes
        pol.hardwareWriteOffsets.push_back(f.offset + i);
    DeviceSession session(std::move(t), pol);
    // connect(): opens, checks API major == 1 and a recognized CONNECT 2 APP-mode PID, reads settings.
    if (auto s = session.connect(); !s) { printError(s.error(), "connect"); return exitCodeFor(s.error().code); }
    std::printf("device: pid=0x%04x mode=%s api=%u.%u%s\n", session.info().pid, modeName(session.capabilities().mode),
                session.capabilities().apiMajor, session.capabilities().apiMinor,
                session.isSimulated() ? "  [SIMULATED]" : "");
    if (!session.isSimulated() && session.capabilities().mode != DeviceMode::Normal) {
        std::fprintf(stderr, "abort: the write experiments are only approved for APP mode PID 0x0004\n");
        return 8;
    }
    // Fresh snapshot immediately before the payload is prepared (connect() read one earlier).
    if (auto s = session.refreshSettings(); !s) { printError(s.error(), "refresh"); return exitCodeFor(s.error().code); }
    if (!session.settings()) { std::fprintf(stderr, "no settings available after connect\n"); return 1; }
    SettingsMessage before = *session.settings();
    auto editFn = [&](SettingsMessage& m) -> Status {
        if (isSpeakerVolume) return m.setSpeakerVolumeDb(a.db);
        if (isSpeakerMute) return m.setSpeakerMute(a.value != 0);
        return isMute ? m.setMute(1, a.value != 0) : m.setLedBrightness(a.value);
    };
    if (auto s = session.edit(editFn); !s) { printError(s.error(), "edit"); return exitCodeFor(s.error().code); }
    SettingsMessage proposed = *session.pendingSettings();
    auto bb = before.bytes();
    auto pb = proposed.bytes();
    std::vector<std::size_t> diffs;
    for (std::size_t i = 0; i < bb.size(); ++i)
        if (bb[i] != pb[i]) diffs.push_back(i);
    std::printf("field: %s (settings byte %zu)\n", f.name, f.offset);
    std::printf("request (if sent): bm=0x21 req=0x03 wValue=0x%04x wIndex=0x%04x wLength=0x%02zx timeout=500ms\n",
                wValueFor(MessageId::Settings), wIndexFor(static_cast<std::uint8_t>(a.interfaceNumber)), bb.size());
    std::printf("original byte %zu: 0x%02x\nproposed byte %zu: 0x%02x\n", f.offset, bb[f.offset], f.offset, pb[f.offset]);
    std::printf("changed byte offsets: ");
    for (auto i : diffs) std::printf("%zu ", i);
    std::puts(diffs.empty() ? "(none; already at this value)" : "");
    std::printf("original raw="); hex(bb);
    std::printf("proposed raw="); hex(pb);
    bool shapeOk = true;
    for (auto i : diffs)
        if (i < f.offset || i >= f.offset + f.length) shapeOk = false;
    if (!shapeOk) {
        std::fprintf(stderr, "abort: unexpected byte diff shape (expected only offsets %zu..%zu); refusing to write\n",
                     f.offset, f.offset + f.length - 1);
        return 1;
    }
    if (!a.execute) {
        std::puts("dry run only; no USB write was sent. Pass --execute (and, on real hardware, the "
                  "authorization variables named in the refusal message) to send this write.");
        return 0;
    }
    if (diffs.empty()) {
        std::puts("nothing to write: proposed value equals the current device value");
        return 0;
    }
    PollPolicy pp;
    pp.requireStableSnapshot = true;
    WriteResult r = session.commitAndVerify(pp);
    std::printf("write state: %s (convergence polls: %d)\n", writeStateName(r.state), r.pollAttempts);
    if (!r.unexpectedOffsets.empty()) {
        std::printf("unexpected offsets changed: ");
        for (auto i : r.unexpectedOffsets) std::printf("%zu ", i);
        std::puts("");
    }
    if (r.state == WriteState::Failed) { printError(r.error, "write-verify"); return exitCodeFor(r.error.code); }
    if (session.settings()) {
        std::printf("post-write byte %zu: 0x%02x\n", f.offset, session.settings()->bytes()[f.offset]);
        std::printf("post-write raw="); hex(session.settings()->bytes());
    }
    return r.state == WriteState::Verified ? 0 : 10;   // 10: sent but not confirmed (timed out / ambiguous)
}
#endif  // LEWITT_HAVE_USB

#ifndef LEWITT_HAVE_USB
int cmdProbe(bool) {
    std::puts("this build has no USB support (mock-only build); nothing to probe");
    return 1;
}
#else
// Read-only experiment matrix (GET of the 0x0A handshake). Dry run unless --execute.
int cmdProbe(bool execute) {
    struct Variant { const char* what; std::uint8_t bm; int ifnum; };
    // Dry-run discovery is passive. Executed control GETs share the same cooperative owner
    // as the normal CLI and GUI transport; unrelated clients are not covered by this lease.
    std::optional<ControlLease> lease;
    if (execute) {
        auto acquired = ControlLease::acquireForCurrentSession();
        if (!acquired) { printError(acquired.error(), "probe"); return exitCodeFor(acquired.error().code); }
        lease.emplace(std::move(acquired.value()));
    }
    libusb_context* ctx = nullptr;
    if (libusb_init(&ctx) != 0) return 1;
    libusb_device** list = nullptr;
    ssize_t n = libusb_get_device_list(ctx, &list);
    libusb_device* dev = nullptr;
    for (ssize_t i = 0; i < n && !dev; ++i) {
        libusb_device_descriptor d{};
        if (libusb_get_device_descriptor(list[i], &d) == 0 && d.idVendor == kLewittVendorId) dev = list[i];
    }
    if (!dev) {
        std::puts("no LEWITT device present");
        if (n >= 0) libusb_free_device_list(list, 1);
        libusb_exit(ctx);
        return 1;
    }
    libusb_config_descriptor* cfg = nullptr;
    libusb_get_active_config_descriptor(dev, &cfg);
    libusb_device_handle* h = nullptr;
    int orc = libusb_open(dev, &h);
    if (orc != 0 && execute) {
        printError(mapLibusbError(orc, "open"), "open");
        libusb_free_config_descriptor(cfg);
        libusb_free_device_list(list, 1);
        libusb_exit(ctx);
        return exitCodeFor(mapLibusbError(orc, "open").code);
    }
    std::vector<Variant> vars;
    int ac = -1;
    for (int i = 0; cfg && i < cfg->bNumInterfaces; ++i) {
        const auto& a = cfg->interface[i].altsetting[0];
        if (a.bInterfaceClass == 1 && a.bInterfaceSubClass == 1) ac = a.bInterfaceNumber;
    }
    if (ac >= 0) vars.push_back({"interface recipient, AudioControl (kernel-owned)", kBmGet, ac});
    vars.push_back({"device recipient (class)", std::uint8_t(0xA0), ac < 0 ? 0 : ac});
    for (int i = 0; cfg && h && i < cfg->bNumInterfaces; ++i) {
        int num = cfg->interface[i].altsetting[0].bInterfaceNumber;
        if (num == ac || libusb_kernel_driver_active(h, num) == 1) continue;   // never touch bound interfaces
        vars.push_back({"interface recipient, UNBOUND interface", kBmGet, num});
    }
    std::printf("planned read-only probes (GET api-version 0x0A, wLength 0x40)%s:\n",
                orc != 0 ? "  [device node not openable: dry run only]" : "");
    int idx = 0;
    for (auto& v : vars)
        for (std::uint8_t req : {kReqUac2Mem, kReqUac1GetMem}) {
            std::printf("  #%d %-45s bm=%02x req=%02x wValue=%04x wIndex=%04x\n", ++idx, v.what, v.bm, req,
                        wValueFor(MessageId::ApiVersion), wIndexFor(std::uint8_t(v.ifnum)));
            if (!execute || !h) continue;
            std::uint8_t buf[0x40] = {};
            int rc = libusb_control_transfer(h, v.bm, req, wValueFor(MessageId::ApiVersion),
                                             wIndexFor(std::uint8_t(v.ifnum)), buf, sizeof buf, kTimeoutMs);
            if (rc < 0) {
                auto e = mapLibusbError(rc, "control read");
                std::printf("      -> [%s] %s\n", errcName(e.code), e.message.c_str());
            } else {
                std::printf("      -> %d bytes: ", rc);
                for (int k = 0; k < rc; ++k) std::printf("%02x", buf[k]);
                std::puts("");
            }
        }
    if (!execute) std::puts("dry run; pass --execute to perform these GET transfers (no detach, no claim, no writes)");
    if (cfg) libusb_free_config_descriptor(cfg);
    if (h) libusb_close(h);
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return 0;
}
#endif  // LEWITT_HAVE_USB

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: openwitty-cli devices | diagnose | list | probe [--execute] |\n"
                     "                  read <api|version|status|settings> [--method interface|device-recipient|detach]\n"
                     "                       [--request uac2|uac1] [--interface N|auto] [--mock]\n"
                     "                  write led-brightness <0-100> [--execute] [--interface N] [--mock]\n"
                     "                  write instrument-mute <on|off> [--execute] [--interface N] [--mock]\n"
                     "this tool is read-only by default. --method detach is disruptive and needs\n"
                     "LEWITT_ALLOW_DISRUPTIVE=1. 'write led-brightness' is the ONE narrowly-scoped experimental\n"
                     "write command (nothing else can be written); without --execute it only prints a dry-run\n"
                     "proposal and sends no USB write; with --execute on real hardware it additionally needs\n"
                     "LEWITT_ALLOW_HARDWARE_WRITE=1.\n");
        return 64;
    }
    std::string cmd = argv[1];
    if (cmd == "--version" || cmd == "version") {
        std::printf("openwitty-cli %s (%s build)\n%s\n", LEWITT_VERSION_STRING, LEWITT_BUILD_FLAVOUR, LEWITT_NOTICE);
        return 0;
    }
    if (cmd == "devices") return cmdDevices(false);
    if (cmd == "diagnose") return cmdDevices(true);
    if (cmd == "list") {
        auto d = UsbTransport::enumerate();
        if (d.empty()) { std::puts("no LEWITT USB device (VID 0x29c2) present"); return 1; }
        for (auto& x : d) std::printf("%04x:%04x\n", x.vid, x.pid);
        return 0;
    }
    if (cmd == "probe") {
        bool exec = false;
        for (int i = 2; i < argc; ++i) if (!std::strcmp(argv[i], "--execute")) exec = true;
        return cmdProbe(exec);
    }
    if (cmd == "read") {
        auto a = parseRead(argc, argv, 2);
        if (a.bad || a.what.empty()) return 64;
        return cmdRead(a);
    }
#ifdef LEWITT_HAVE_USB
    if (cmd == "write") {
        static const FieldSpec kFields[] = {
            {"led-brightness", settings_off::kLedBrightness, nullptr},
            {"instrument-mute", settings_off::kMute[1], "LEWITT_ALLOW_WRITE_INSTRUMENT_MUTE"},
            {"speaker-volume", settings_off::kSpeakerVolume, "LEWITT_ALLOW_WRITE_SPEAKER_VOLUME", 2},
            {"speaker-mute", settings_off::kSpeakerMute, "LEWITT_ALLOW_WRITE_SPEAKER_VOLUME"},
        };
        const FieldSpec* spec = nullptr;
        if (argc >= 3)
            for (const auto& f : kFields)
                if (std::string(argv[2]) == f.name) spec = &f;
        if (!spec) {
            std::fprintf(stderr, "write what? only 'led-brightness <0-100>', 'instrument-mute <on|off>', "
                                 "'speaker-volume <-60..0>', 'speaker-mute <on|off>' exist\n");
            return 64;
        }
        auto a = parseWrite(argc, argv, 3, spec->name);
        if (a.bad || a.value < 0) return 64;
        return cmdWriteField(*spec, a);
    }
#endif
    std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
    return 64;
}
