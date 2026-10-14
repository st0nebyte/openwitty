// lewitt-cli: diagnostic tool. Read-only. No write command exists.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <libusb.h>

#include "core/DeviceSession.hpp"
#include "transport/MockTransport.hpp"
#include "transport/UsbTransport.hpp"

using namespace lewitt;

static void hex(std::span<const std::uint8_t> b) {
    for (auto c : b) std::printf("%02x", c);
    std::printf("\n");
}

static void printSettings(const SettingsMessage& s) {
    for (int i = 0; i < 2; ++i)
        std::printf("input%d: gain=%.2f dB mute=%d hpf=%d clipguard=%d preamp=%u denoise=%d compress=%d\n",
                    i + 1, s.gainDb(i), s.mute(i), s.highPass(i), s.clipguard(i), s.preampRaw(i),
                    s.denoise(i), s.compress(i));
    std::printf("phantom(input1)=%d volA=%.2f dB volB=%.2f dB dim=%d mono=%d balance=%.2f button=%u\n",
                s.phantom1(), s.volumeADb(), s.volumeBDb(), s.dim(), s.mono(), s.balance(),
                s.customButton());
    std::printf("raw=");
    hex(s.bytes());
}


// Read-only experiment matrix for the Linux access problem. Every transfer is
// a device-to-host GET of the (read-only) version message. Nothing is detached,
// claimed on purpose, reset, or written. Dry-run unless --execute is given.
static int probe(bool execute) {
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
    if (!dev) { std::puts("no LEWITT device present"); if (n >= 0) libusb_free_device_list(list, 1); libusb_exit(ctx); return 1; }
    libusb_config_descriptor* cfg = nullptr;
    libusb_get_active_config_descriptor(dev, &cfg);
    libusb_device_handle* h = nullptr;
    if (int rc = libusb_open(dev, &h); rc != 0) {
        std::fprintf(stderr, "open: %s\n", libusb_error_name(rc));
        return 1;
    }
    std::vector<Variant> vars;
    int ac = -1;
    for (int i = 0; i < cfg->bNumInterfaces; ++i) {
        const auto& a = cfg->interface[i].altsetting[0];
        if (a.bInterfaceClass == 1 && a.bInterfaceSubClass == 1) ac = a.bInterfaceNumber;
    }
    if (ac >= 0) vars.push_back({"interface recipient, AudioControl (kernel-owned)", kBmGet, ac});
    // class request, DEVICE recipient: usbfs does not require a claim for this
    vars.push_back({"device recipient (class)", std::uint8_t(0xA0), ac < 0 ? 0 : ac});
    for (int i = 0; i < cfg->bNumInterfaces; ++i) {
        int num = cfg->interface[i].altsetting[0].bInterfaceNumber;
        if (num == ac) continue;
        if (libusb_kernel_driver_active(h, num) == 1) continue;  // never touch bound interfaces
        vars.push_back({"interface recipient, UNBOUND interface", kBmGet, num});
    }
    std::printf("planned read-only probes (GET version, wLength 0x40):\n");
    int idx = 0;
    for (auto& v : vars) {
        for (std::uint8_t req : {kReqUac2Mem, kReqUac1GetMem}) {
            std::printf("  #%d %-45s bm=%02x req=%02x wValue=%04x wIndex=%04x\n", ++idx, v.what, v.bm, req,
                        wValueFor(MessageId::Version), wIndexFor(std::uint8_t(v.ifnum)));
            if (!execute) continue;
            std::uint8_t buf[0x40] = {};
            int rc = libusb_control_transfer(h, v.bm, req, wValueFor(MessageId::Version),
                                             wIndexFor(std::uint8_t(v.ifnum)), buf, sizeof buf, kTimeoutMs);
            if (rc < 0) std::printf("      -> %s\n", libusb_error_name(rc));
            else { std::printf("      -> %d bytes: ", rc); for (int k = 0; k < rc; ++k) std::printf("%02x", buf[k]); std::puts(""); }
        }
    }
    if (!execute) std::puts("dry run; pass --execute to perform these GET transfers");
    libusb_free_config_descriptor(cfg);
    libusb_close(h);
    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: lewitt-cli list | probe [--execute] | read <version|status|settings> [--mock] [--uac1] [--detach]\n"
                     "  --detach is disruptive (drops the sound card) and also needs "
                     "LEWITT_ALLOW_DISRUPTIVE=1\n");
        return 64;
    }
    std::string cmd = argv[1];
    if (cmd == "list") {
        auto d = UsbTransport::enumerate();
        if (d.empty()) { std::puts("no LEWITT USB device (VID 0x29c2) present"); return 1; }
        for (auto& x : d) std::printf("%04x:%04x\n", x.vid, x.pid);
        return 0;
    }
    if (cmd == "probe") {
        bool exec = false;
        for (int i = 2; i < argc; ++i) if (!std::strcmp(argv[i], "--execute")) exec = true;
        return probe(exec);
    }
    if (cmd != "read" || argc < 3) return 64;
    std::string what = argv[2];
    bool mock = false, uac1 = false, detach = false;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--mock")) mock = true;
        else if (!std::strcmp(argv[i], "--uac1")) uac1 = true;
        else if (!std::strcmp(argv[i], "--detach")) detach = true;
    }
    if (detach && !std::getenv("LEWITT_ALLOW_DISRUPTIVE")) {
        std::fprintf(stderr, "refused: --detach requires LEWITT_ALLOW_DISRUPTIVE=1\n");
        return 2;
    }
    std::unique_ptr<IDeviceTransport> t;
    if (mock) t = std::make_unique<MockTransport>();
    else {
        UsbOptions o;
        o.request = uac1 ? MemRequest::Uac1Mem : MemRequest::Uac2Mem;
        o.allowDetachKernelDriver = detach;
        t = std::make_unique<UsbTransport>(o);
    }
    if (auto s = t->open(); !s) {
        std::fprintf(stderr, "open failed: %s\n", s.error().message.c_str());
        return 1;
    }
    MessageId id = what == "version" ? MessageId::Version
                   : what == "status" ? MessageId::Status : MessageId::Settings;
    auto r = t->read(id);
    if (!r) {
        std::fprintf(stderr, "read failed (%d): %s\n", static_cast<int>(r.error().code),
                     r.error().message.c_str());
        return 1;
    }
    if (id == MessageId::Settings) {
        auto m = SettingsMessage::fromBytes(r.value());
        if (!m) { std::fprintf(stderr, "%s\n", m.error().message.c_str()); return 1; }
        printSettings(m.value());
    } else {
        hex(r.value());
    }
    return 0;
}
