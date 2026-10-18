#include <gtest/gtest.h>

#include "core/ReadFreshness.hpp"
#include "core/PollCadence.hpp"

using namespace lewitt;
using namespace std::chrono_literals;

namespace {
const ReadFreshness::TimePoint kStart{10s};
}

TEST(ReadFreshness, SettingsBoundariesAtTwoHertz) {
    ReadFreshness reads;
    EXPECT_EQ(reads.settings(kStart, true), Freshness::Unavailable);
    reads.markSettings(kStart);
    EXPECT_EQ(reads.settings(kStart + 750ms, true), Freshness::Fresh);
    EXPECT_EQ(reads.settings(kStart + 751ms, true), Freshness::Delayed);
    EXPECT_EQ(reads.settings(kStart + 1500ms, true), Freshness::Delayed);
    EXPECT_EQ(reads.settings(kStart + 1501ms, true), Freshness::Stale);
}

TEST(ReadFreshness, StatusBoundariesAtFourHertz) {
    ReadFreshness reads;
    reads.markStatus(kStart);
    EXPECT_EQ(reads.status(kStart + 375ms, true), Freshness::Fresh);
    EXPECT_EQ(reads.status(kStart + 376ms, true), Freshness::Delayed);
    EXPECT_EQ(reads.status(kStart + 750ms, true), Freshness::Delayed);
    EXPECT_EQ(reads.status(kStart + 751ms, true), Freshness::Stale);
}

TEST(ReadFreshness, ExperimentalEightHertzBoundaries) {
    ReadFreshness reads(kStatus8HzExperimental.statusFreshness);
    reads.markStatus(kStart);
    EXPECT_EQ(reads.status(kStart + 188ms, true), Freshness::Fresh);
    EXPECT_EQ(reads.status(kStart + 189ms, true), Freshness::Delayed);
    EXPECT_EQ(reads.status(kStart + 375ms, true), Freshness::Delayed);
    EXPECT_EQ(reads.status(kStart + 376ms, true), Freshness::Stale);
    reads.markSettings(kStart);
    EXPECT_EQ(reads.settings(kStart + 750ms, true), Freshness::Fresh);
    EXPECT_EQ(reads.settings(kStart + 1500ms, true), Freshness::Delayed);
}

TEST(ReadFreshness, RealSchedulesKeepSettingsAtTwoHertz) {
    EXPECT_EQ(kStatus4Hz.statusPeriod, 250ms);
    EXPECT_EQ(kStatus4Hz.settingsEveryStatus, 2);
    EXPECT_EQ(kStatus8HzExperimental.statusPeriod, 125ms);
    EXPECT_EQ(kStatus8HzExperimental.settingsEveryStatus, 4);
    EXPECT_EQ(kStatus4Hz.statusPeriod * kStatus4Hz.settingsEveryStatus, 500ms);
    EXPECT_EQ(kStatus8HzExperimental.statusPeriod * kStatus8HzExperimental.settingsEveryStatus, 500ms);
}

TEST(ReadFreshness, SuccessfulStatusCannotRefreshFailedSettings) {
    ReadFreshness reads;
    reads.markSettings(kStart);
    reads.markStatus(kStart);
    reads.markStatus(kStart + 1000ms);  // settings GETs have failed meanwhile
    EXPECT_EQ(reads.settings(kStart + 1000ms, true), Freshness::Delayed);
    EXPECT_EQ(reads.status(kStart + 1000ms, true), Freshness::Fresh);
    reads.markStatus(kStart + 1750ms);
    EXPECT_EQ(reads.settings(kStart + 1750ms, true), Freshness::Stale);
    EXPECT_EQ(reads.status(kStart + 1750ms, true), Freshness::Fresh);
}

TEST(ReadFreshness, SuccessfulSettingsCannotRefreshFailedStatus) {
    ReadFreshness reads;
    reads.markSettings(kStart);
    reads.markStatus(kStart);
    reads.markSettings(kStart + 1000ms);
    EXPECT_EQ(reads.settings(kStart + 1000ms, true), Freshness::Fresh);
    EXPECT_EQ(reads.status(kStart + 1000ms, true), Freshness::Stale);
}

TEST(ReadFreshness, RecoveryRequiresSuccessfulReadOfThatStream) {
    ReadFreshness reads;
    reads.markSettings(kStart);
    reads.markStatus(kStart);
    const auto later = kStart + 2000ms;
    EXPECT_EQ(reads.settings(later, true), Freshness::Stale);
    EXPECT_EQ(reads.status(later, true), Freshness::Stale);
    reads.markSettings(later);
    EXPECT_EQ(reads.settings(later, true), Freshness::Fresh);
    EXPECT_EQ(reads.status(later, true), Freshness::Stale);
    reads.markStatus(later);
    EXPECT_EQ(reads.status(later, true), Freshness::Fresh);
}

TEST(ReadFreshness, DisconnectAndReconnectCannotReuseOldTimestamps) {
    ReadFreshness reads;
    reads.markSettings(kStart);
    reads.markStatus(kStart);
    EXPECT_EQ(reads.settings(kStart, false), Freshness::Unavailable);
    EXPECT_EQ(reads.status(kStart, false), Freshness::Unavailable);
    reads.reset();
    EXPECT_EQ(reads.settings(kStart + 1ms, true), Freshness::Unavailable);
    EXPECT_EQ(reads.status(kStart + 1ms, true), Freshness::Unavailable);
    reads.markSettings(kStart + 2ms);
    EXPECT_EQ(reads.settings(kStart + 2ms, true), Freshness::Fresh);
    EXPECT_EQ(reads.status(kStart + 2ms, true), Freshness::Unavailable);
    EXPECT_EQ(reads.settings(kStart + 1ms, true), Freshness::Unavailable);  // clock regression fails closed
}
