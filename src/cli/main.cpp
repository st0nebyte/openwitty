// lewitt-cli: read-only diagnostic tool. There is NO write command.
//
//   devices                 enumerate LEWITT devices from sysfs: descriptors, interfaces,
//                           endpoints, bound kernel drivers, usbfs node permissions (no USB I/O)
//   diagnose                devices + what each finding means for control access + next steps
//   read api|version|status|settings [--method M] [--request R] [--mock]
//                           one read-only control transfer; errors are classified
//   probe [--execute]       dry-run (default) or run a small matrix of GET-only experiments
//   list                    quick libusb enumeration (VID/PID only)
//
// Methods:  interface (default) | device-recipient | detach (DISRUPTIVE, needs
//           LEWITT_ALLOW_DISRUPTIVE=1 as well).  Requests: uac2 (default) | uac1.
#ifdef LEWITT_HAVE_USB
#include <libusb.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/DeviceManager.hpp"
#include "core/DeviceSession.hpp"
#include "transport/MockTransport.hpp"
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
    if (a.method == AccessMethod::DetachAndClaim && !std::getenv("LEWITT_ALLOW_DISRUPTIVE")) {
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

#ifndef LEWITT_HAVE_USB
int cmdProbe(bool) {
    std::puts("this build has no USB support (mock-only build); nothing to probe");
    return 1;
}
#else
// Read-only experiment matrix (GET of the 0x0A handshake). Dry run unless --execute.
int cmdProbe(bool execute) {
    struct Variant { const char* what; std::uint8_t bm; int ifnum; };
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
                     "usage: lewitt-cli devices | diagnose | list | probe [--execute] |\n"
                     "                  read <api|version|status|settings> [--method interface|device-recipient|detach]\n"
                     "                       [--request uac2|uac1] [--mock]\n"
                     "read-only tool: it never writes to the device. --method detach is disruptive and also\n"
                     "needs LEWITT_ALLOW_DISRUPTIVE=1.\n");
        return 64;
    }
    std::string cmd = argv[1];
    if (cmd == "--version" || cmd == "version") {
        std::printf("lewitt-cli %s (%s build)\n%s\n", LEWITT_VERSION_STRING, LEWITT_BUILD_FLAVOUR, LEWITT_NOTICE);
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
    std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
    return 64;
}
