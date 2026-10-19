#include <gtest/gtest.h>
#include <array>
#include <tuple>

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

TEST(ReadFreshness, ExperimentalFortyEightHertzBoundaries) {
    ReadFreshness reads(kStatus48HzExperimental.statusFreshness);
    reads.markStatus(kStart);
    EXPECT_EQ(reads.status(kStart + 75ms, true), Freshness::Fresh);
    EXPECT_EQ(reads.status(kStart + 76ms, true), Freshness::Delayed);
    EXPECT_EQ(reads.status(kStart + 200ms, true), Freshness::Delayed);
    EXPECT_EQ(reads.status(kStart + 201ms, true), Freshness::Stale);
    reads.markSettings(kStart + 1000ms);
    EXPECT_EQ(reads.status(kStart + 1000ms, true), Freshness::Stale);
    reads.markStatus(kStart + 1000ms);
    EXPECT_EQ(reads.status(kStart + 1000ms, true), Freshness::Fresh);
    reads.reset();
    EXPECT_EQ(reads.status(kStart + 1001ms, true), Freshness::Unavailable);
}

TEST(ReadFreshness, RealSchedulesKeepSettingsAtTwoHertz) {
    EXPECT_EQ(kStatus4Hz.statusPeriod, 250ms);
    EXPECT_EQ(kStatus4Hz.settingsEveryStatus, 2);
    EXPECT_EQ(kStatus8HzExperimental.statusPeriod, 125ms);
    EXPECT_EQ(kStatus8HzExperimental.settingsEveryStatus, 4);
    EXPECT_EQ(kStatus4Hz.statusPeriod * kStatus4Hz.settingsEveryStatus, 500ms);
    EXPECT_EQ(kStatus8HzExperimental.statusPeriod * kStatus8HzExperimental.settingsEveryStatus, 500ms);
    EXPECT_EQ(kStatus48HzExperimental.statusPeriod, std::chrono::nanoseconds(20'833'334));
    EXPECT_TRUE(kStatus48HzExperimental.preciseScheduling);
    EXPECT_EQ(kStatus48HzExperimental.settingsEveryStatus, 0);
}

TEST(ReadFreshness, FortyEightHertzScheduleSkipsMissedTicksWithoutBurst) {
    RealPollSchedule schedule(kStatus48HzExperimental);
    schedule.reset(kStart);
    const auto period = kStatus48HzExperimental.statusPeriod;
    EXPECT_EQ(schedule.nextStatusAfter(kStart), kStart + period);
    EXPECT_EQ(schedule.nextStatusAfter(kStart + period), kStart + period * 2);
    const auto late = kStart + 125ms;
    const auto next = schedule.nextStatusAfter(late);
    EXPECT_GT(next, late);
    EXPECT_LE(next - late, period);
    EXPECT_EQ(next, kStart + period * 6); // sixth deadline is 125 ms + 4 ns
    EXPECT_EQ(schedule.nextStatusAfter(late), next); // no catch-up request
    schedule.reset(kStart + 10s);                  // reconnect starts a new epoch
    EXPECT_EQ(schedule.nextStatusAfter(kStart + 10s), kStart + 10s + period);
}

TEST(ReadFreshness, FortyEightHertzSettingsDeadlineStaysIndependent) {
    RealPollSchedule schedule(kStatus48HzExperimental);
    schedule.reset(kStart);
    EXPECT_FALSE(schedule.settingsDue(kStart + 499ms));
    EXPECT_TRUE(schedule.settingsDue(kStart + 500ms));
    EXPECT_FALSE(schedule.settingsDue(kStart + 999ms));
    EXPECT_TRUE(schedule.settingsDue(kStart + 1000ms));
    EXPECT_TRUE(schedule.settingsDue(kStart + 5s)); // one read, never a catch-up burst
    EXPECT_FALSE(schedule.settingsDue(kStart + 5s));
    schedule.reset(kStart + 10s);
    EXPECT_FALSE(schedule.settingsDue(kStart + 10s));
    EXPECT_TRUE(schedule.settingsDue(kStart + 10s + 500ms));
}

TEST(ReadFreshness, FortyEightHertzSchedulesTwentySettingsReadsInTenSeconds) {
    RealPollSchedule schedule(kStatus48HzExperimental);
    schedule.reset(kStart);
    int settingsReads = 0;
    auto statusAt = kStart;
    for (int i = 0; i < 480; ++i) {
        statusAt = schedule.nextStatusAfter(statusAt);
        settingsReads += schedule.settingsDue(statusAt);
    }
    EXPECT_EQ(settingsReads, 20);
    EXPECT_EQ(statusAt, kStart + kStatus48HzExperimental.statusPeriod * 480);
}

TEST(ReadFreshness, ExperimentalSettingsRatesUseIndependentDeadlines) {
    const std::array<std::pair<std::chrono::nanoseconds, int>, 2> cases{{
        {125ms, 80}, {std::chrono::microseconds(62500), 160}}};
    for (auto [period, expected] : cases) {
        RealPollSchedule schedule(kStatus48HzExperimental, period);
        schedule.reset(kStart);
        int reads = 0;
        auto statusAt = kStart;
        for (int i = 0; i < 480; ++i) {
            statusAt = schedule.nextStatusAfter(statusAt);
            reads += schedule.settingsDue(statusAt);
        }
        EXPECT_EQ(reads, expected);
        EXPECT_EQ(statusAt, kStart + kStatus48HzExperimental.statusPeriod * 480);
        EXPECT_FALSE(schedule.settingsDue(statusAt)); // no duplicate or catch-up read
        schedule.reset(kStart + 20s);
        EXPECT_FALSE(schedule.settingsDue(kStart + 20s));
        EXPECT_TRUE(schedule.settingsDue(kStart + 20s + period));
    }
}

TEST(ReadFreshness, ExperimentalSettingsAgeDoesNotBorrowStatusTimestamp) {
    for (auto [window, fresh, stale] : {
             std::tuple{kSettings8HzFreshness, 250ms, 500ms},
             std::tuple{kSettings16HzFreshness, 125ms, 250ms}}) {
        ReadFreshness reads(kStatus48HzExperimental.statusFreshness, window);
        reads.markSettings(kStart);
        reads.markStatus(kStart + stale + 1ms);
        EXPECT_EQ(reads.settings(kStart + fresh, true), Freshness::Fresh);
        EXPECT_EQ(reads.settings(kStart + fresh + 1ms, true), Freshness::Delayed);
        EXPECT_EQ(reads.settings(kStart + stale, true), Freshness::Delayed);
        EXPECT_EQ(reads.settings(kStart + stale + 1ms, true), Freshness::Stale);
        EXPECT_EQ(reads.status(kStart + stale + 1ms, true), Freshness::Fresh);
        reads.markSettings(kStart + stale + 2ms);
        EXPECT_EQ(reads.settings(kStart + stale + 2ms, true), Freshness::Fresh);
        reads.reset();
        EXPECT_EQ(reads.settings(kStart + stale + 3ms, true), Freshness::Unavailable);
    }
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
