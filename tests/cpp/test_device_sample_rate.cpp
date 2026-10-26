// Phase 10B: the CONNECT 2's real sample rate from fixture /proc/asound trees (no device needed).
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "desktop/DeviceSampleRate.hpp"

using namespace lewitt;
namespace fs = std::filesystem;

namespace {
struct Proc {
    fs::path root = fs::temp_directory_path() / ("ow-proc-" + std::to_string(getpid()) + "-" + std::to_string(rand()));
    Proc() { fs::create_directories(root); }
    ~Proc() { fs::remove_all(root); }
    void file(const std::string& rel, const std::string& text) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel) << text;
    }
    void card(int n, const char* usbid) { file("card" + std::to_string(n) + "/usbid", std::string(usbid) + "\n"); }
    void hw(int n, const char* dir, const std::string& text) { file("card" + std::to_string(n) + "/pcm0" + dir + "/sub0/hw_params", text); }
    std::string path() const { return root.string(); }
};
std::string open(unsigned hz) {
    return "access: MMAP_INTERLEAVED\nformat: S32_LE\nsubformat: STD\nchannels: 2\nrate: " + std::to_string(hz) + " (" +
           std::to_string(hz) + "/1)\nperiod_size: 1024\nbuffer_size: 2048\n";
}
const char* kStopped = "Lewitt GmbH CONNECT 2\n\nPlayback:\n  Status: Stop\n  Interface 1\n\nCapture:\n  Status: Stop\n";
std::string running(unsigned hz) {
    return "Lewitt GmbH CONNECT 2\n\nPlayback:\n  Status: Running\n    Interface = 1\n    Momentary freq = " +
           std::to_string(hz) + " Hz (0x6.0000)\n\nCapture:\n  Status: Stop\n";
}
}  // namespace

TEST(DeviceSampleRate, ParsersAcceptOnlyClearValues) {
    EXPECT_EQ(parseHwParamsRate(open(96000)), 96000u);
    EXPECT_FALSE(parseHwParamsRate("closed\n"));
    EXPECT_EQ(parseHwParamsRate("rate: 44100 (88200/2)\n"), 44100u);                  // exact ratio: accepted
    EXPECT_FALSE(parseHwParamsRate("rate: 44100 (441001/10)\n"));                      // fractional: not trusted
    EXPECT_FALSE(parseHwParamsRate(""));
    EXPECT_EQ(parseStreamMomentaryRate(running(48000), "Playback"), 48000u);
    EXPECT_FALSE(parseStreamMomentaryRate(running(48000), "Capture"));                 // that direction is stopped
    EXPECT_FALSE(parseStreamMomentaryRate(kStopped, "Playback"));
}

TEST(DeviceSampleRate, IdleDeviceIsUnknownNeverAGuessedDefault) {
    Proc p;
    p.card(3, "29c2:0004");
    p.hw(3, "p", "closed\n");
    p.hw(3, "c", "closed\n");
    p.file("card3/stream0", kStopped);
    auto r = readConnect2SampleRate(p.path());
    EXPECT_FALSE(r.hz.has_value());
    EXPECT_NE(r.reason.find("no stream open"), std::string::npos);
}

TEST(DeviceSampleRate, OpenStreamGivesTheDeviceRateIncluding96kHz) {
    Proc p;
    p.card(3, "29c2:0004");
    p.hw(3, "p", open(96000));
    p.hw(3, "c", "closed\n");
    p.file("card3/stream0", running(96000));
    EXPECT_EQ(readConnect2SampleRate(p.path()).hz, 96000u);
}

TEST(DeviceSampleRate, TheCardIsTheConnect2NotAnotherOrTheDefaultOutput) {
    Proc p;
    p.card(0, "1234:5678");                     // some other (e.g. default) card with an open 48 kHz stream
    p.hw(0, "p", open(48000));
    EXPECT_FALSE(readConnect2SampleRate(p.path()).hz.has_value());
    p.card(3, "29c2:0004");
    p.hw(3, "c", open(44100));
    EXPECT_EQ(readConnect2SampleRate(p.path()).hz, 44100u);
}

TEST(DeviceSampleRate, MissingAmbiguousOrInconsistentIsUnknown) {
    Proc none;
    EXPECT_NE(readConnect2SampleRate(none.path()).reason.find("no CONNECT 2"), std::string::npos);
    Proc two;
    two.card(2, "29c2:0004");
    two.card(3, "29c2:0004");
    two.hw(3, "p", open(48000));
    EXPECT_FALSE(readConnect2SampleRate(two.path()).hz.has_value());      // never guess between two devices
    Proc split;
    split.card(3, "29c2:0004");
    split.hw(3, "p", open(48000));
    split.hw(3, "c", open(96000));
    EXPECT_FALSE(readConnect2SampleRate(split.path()).hz.has_value());
    Proc mismatch;
    mismatch.card(3, "29c2:0004");
    mismatch.hw(3, "p", open(48000));
    mismatch.file("card3/stream0", running(44100));
    EXPECT_FALSE(readConnect2SampleRate(mismatch.path()).hz.has_value());
    Proc jitter;                                  // a momentary rate within 1 % is the same rate
    jitter.card(3, "29c2:0004");
    jitter.hw(3, "p", open(48000));
    jitter.file("card3/stream0", running(47998));
    EXPECT_EQ(readConnect2SampleRate(jitter.path()).hz, 48000u);
}
