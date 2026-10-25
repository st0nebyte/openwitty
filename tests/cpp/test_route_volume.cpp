// Phase 9C proof of concept (offline): mirroring the knob through the PipeWire route instead of the ALSA
// control. These tests model how PipeWire's ALSA layer (ACP, spa/plugins/alsa/acp/alsa-mixer.c in PipeWire
// 1.6.10) turns a route's channelVolumes into a hardware step, and check that planRouteMirror() never lands
// above the device's level. Nothing here talks to PipeWire, ALSA or a device.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>

#include "desktop/RouteVolume.hpp"

using namespace lewitt;

namespace {
const VolumeGrid kConnect2{0, 120, -60.0, 0.0};   // PCM Playback Volume: 0..120, -60..0 dB, 0.5 dB steps

// ACP: channelVolumes (float, linear) -> pa_volume_t (cubic, PA_VOLUME_NORM = 0x10000) -> dB -> dB/100 as a
// long (C truncation toward zero) -> raw step, rounding up (+1) or to the nearest (deferred volume).
long acpRaw(float linear, bool nearest) {
    const double pa = std::round(std::cbrt(static_cast<double>(linear)) * 65536.0);
    const double db = 20.0 * std::log10(std::pow(pa / 65536.0, 3.0));
    const long alsaDb = static_cast<long>(db * 100.0);
    long best = kConnect2.rawMin;
    for (long raw = kConnect2.rawMin; raw <= kConnect2.rawMax; ++raw) {
        const long rawDb = std::lround((kConnect2.dbMin + raw * kConnect2.stepDb()) * 100.0);
        if (nearest) {
            const long bestDb = std::lround((kConnect2.dbMin + best * kConnect2.stepDb()) * 100.0);
            if (std::labs(rawDb - alsaDb) < std::labs(bestDb - alsaDb)) best = raw;
        } else if (rawDb >= alsaDb) {
            return raw;   // smallest step at or above the request
        }
    }
    return nearest ? best : kConnect2.rawMax;
}
double rawDb(long raw) { return kConnect2.dbMin + raw * kConnect2.stepDb(); }
}  // namespace

TEST(RouteVolumePoC, EveryHardwareStepIsReachedExactlyAndNeverOneLouder) {
    for (long raw = kConnect2.rawMin; raw <= kConnect2.rawMax; ++raw) {
        const double deviceDb = rawDb(raw);
        auto r = planRouteMirror(deviceDb, false, DesktopLevel{-60.0, false}, kConnect2);
        if (raw == kConnect2.rawMin) {
            EXPECT_FALSE(r.has_value());                 // the desktop already shows -60
            continue;
        }
        ASSERT_TRUE(r.has_value()) << deviceDb;
        EXPECT_DOUBLE_EQ(r->stepDb, deviceDb);
        EXPECT_EQ(acpRaw(r->linear, /*nearest=*/false), raw) << deviceDb << " dB, rounding up";
        EXPECT_EQ(acpRaw(r->linear, /*nearest=*/true), raw) << deviceDb << " dB, nearest";
        EXPECT_FALSE(r->mute.has_value());
    }
}

TEST(RouteVolumePoC, OffGridDeviceLevelsAreRoundedDownNeverUp) {
    // the vendor GET reports levels between control steps (e.g. -18.86 dB): the route gets the step below
    for (double db : {-59.99, -48.26, -23.88, -18.86, -9.34, -9.09, -0.01}) {
        auto r = planRouteMirror(db, false, std::nullopt, kConnect2);
        ASSERT_TRUE(r.has_value());
        EXPECT_LE(r->stepDb, db);
        EXPECT_GT(r->stepDb, db - kConnect2.stepDb());
        EXPECT_LE(rawDb(acpRaw(r->linear, false)), db) << db;
    }
}

TEST(RouteVolumePoC, NaiveConversionWouldBeLouderOnSomeSteps) {
    // Documents the hazard the 1/10-step margin removes: sending exactly 10^(step/20) as a float.
    int louder = 0;
    for (long raw = kConnect2.rawMin; raw <= kConnect2.rawMax; ++raw) {
        const float naive = static_cast<float>(std::pow(10.0, rawDb(raw) / 20.0));
        if (acpRaw(naive, false) > raw) ++louder;
    }
    std::printf("naive conversion lands one step louder on %d of %ld steps\n", louder,
                kConnect2.rawMax - kConnect2.rawMin + 1);
    SUCCEED();
}

TEST(RouteVolumePoC, NeverUnmutesAndMirrorsAHardwareMuteOnly) {
    // desktop muted, device unmuted: no request at all (a muted desktop is never unmuted)
    EXPECT_FALSE(planRouteMirror(-20.0, false, DesktopLevel{-20.0, true}, kConnect2).has_value());
    // device muted (Mute button), desktop not: mute=true
    auto r = planRouteMirror(-20.0, true, DesktopLevel{-20.0, false}, kConnect2);
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(r->mute.has_value());
    EXPECT_TRUE(*r->mute);
    // a level change while the desktop is muted keeps it muted (no mute field)
    auto q = planRouteMirror(-30.0, false, DesktopLevel{-20.0, true}, kConnect2);
    ASSERT_TRUE(q.has_value());
    EXPECT_FALSE(q->mute.has_value());
}

TEST(RouteVolumePoC, NothingToSendWhenTheDesktopAlreadyShowsTheLevel) {
    EXPECT_FALSE(planRouteMirror(-23.5, false, DesktopLevel{-23.5, false}, kConnect2).has_value());
    EXPECT_FALSE(planRouteMirror(-23.6, false, DesktopLevel{-24.0, false}, kConnect2).has_value());
    EXPECT_FALSE(planRouteMirror(std::nan(""), false, std::nullopt, kConnect2).has_value());
}

// The incident of Phase 9B: the login screen's WirePlumber applies its default sink volume 0.064 (no saved
// state) and ACP rounds it UP to the next step, -23.88 dB -> -23.50 dB - exactly the level read after login.
TEST(RouteVolumePoC, WirePlumberDefaultSinkVolumeLandsOnMinus23_5dB) {
    EXPECT_DOUBLE_EQ(rawDb(acpRaw(0.064f, /*nearest=*/false)), -23.5);
}
