// Byte vectors here are hand-built from offsets/encodings read out of the
// official app's disassembly -- NOT produced by the encoder under test.
// Status: simulated (static-analysis derived). None verified on hardware.
#include <gtest/gtest.h>
#include <cmath>

#include "protocol/Messages.hpp"

using namespace lewitt;

namespace {
std::vector<std::uint8_t> blob(std::size_t n) { return std::vector<std::uint8_t>(n, 0); }
}

TEST(Types, MessageTable) {
    EXPECT_EQ(messageInfo(MessageId::Settings)->size, 0x80u);
    EXPECT_TRUE(messageInfo(MessageId::Settings)->writable);
    EXPECT_FALSE(messageInfo(MessageId::Status)->writable);
    EXPECT_FALSE(messageInfo(MessageId::Triggers)->readable);
    EXPECT_EQ(messageInfo(MessageId::Version)->size, 0x40u);
    EXPECT_EQ(messageInfo(static_cast<MessageId>(1)), nullptr);
}

TEST(Types, SetupFields) {
    EXPECT_EQ(wValueFor(MessageId::Settings), 0x0100);
    EXPECT_EQ(wValueFor(MessageId::Status), 0x0102);
    EXPECT_EQ(wValueFor(MessageId::Version), 0x0103);
    EXPECT_EQ(wValueFor(MessageId::Triggers), 0x0104);
    EXPECT_EQ(wIndexFor(0), 0x3300);
}

TEST(Q88, Conversions) {
    EXPECT_DOUBLE_EQ(q88ToDouble(0x5500), 85.0);
    EXPECT_DOUBLE_EQ(q88ToDouble(static_cast<std::int16_t>(0xC400)), -60.0);
    EXPECT_EQ(doubleToQ88(-60.0), static_cast<std::int16_t>(0xC400));
    EXPECT_EQ(doubleToQ88(0.5), 128);
    EXPECT_EQ(doubleToQ88(1e9), 32767);
    EXPECT_EQ(doubleToQ88(-1e9), -32768);
}

TEST(Settings, DecodeKnownOffsets) {
    auto b = blob(0x80);
    b[0] = 0x80; b[1] = 0x19;        // 0x1980 = 25.5 dB little-endian
    b[2] = 1; b[3] = 1; b[4] = 1; b[5] = 1; b[6] = 2; b[7] = 1; b[9] = 1;
    b[10] = 0x00; b[11] = 0x0A;      // 10 dB
    b[15] = 1;
    b[32] = 1; b[33] = 1;
    b[35] = 0x00; b[36] = 0xFF;      // raw -256 => balance +1.0 (sign inverted)
    auto m = SettingsMessage::fromBytes(b);
    ASSERT_TRUE(m.ok());
    const auto& s = m.value();
    EXPECT_DOUBLE_EQ(s.gainDb(0), 25.5);
    EXPECT_TRUE(s.mute(0)); EXPECT_TRUE(s.highPass(0)); EXPECT_TRUE(s.clipguard(0));
    EXPECT_TRUE(s.phantom1());
    EXPECT_EQ(s.preamp(0), PreampType::Vivid);
    EXPECT_TRUE(s.denoise(0)); EXPECT_TRUE(s.compress(0));
    EXPECT_DOUBLE_EQ(s.gainDb(1), 10.0);
    EXPECT_EQ(s.preamp(1), PreampType::Warm);
    EXPECT_TRUE(s.dim()); EXPECT_TRUE(s.mono());
    EXPECT_DOUBLE_EQ(s.balance(), 1.0);
}

TEST(Settings, RejectsWrongSizes) {
    for (std::size_t n : {std::size_t{0}, std::size_t{0x7F}, std::size_t{0x81}}) {
        auto r = SettingsMessage::fromBytes(blob(n));
        ASSERT_FALSE(r.ok());
    }
    EXPECT_EQ(SettingsMessage::fromBytes(blob(0x10)).error().code, Errc::ShortTransfer);
    EXPECT_EQ(SettingsMessage::fromBytes(blob(0x90)).error().code, Errc::Malformed);
}

TEST(Settings, GainRangeChecked) {
    SettingsMessage s;
    EXPECT_TRUE(s.setGainDb(0, 0.0).ok());
    EXPECT_TRUE(s.setGainDb(0, 85.0).ok());
    EXPECT_FALSE(s.setGainDb(0, -0.1).ok());
    EXPECT_FALSE(s.setGainDb(0, 85.1).ok());
    EXPECT_FALSE(s.setGainDb(0, std::nan("")).ok());
    EXPECT_FALSE(s.setGainDb(2, 10).ok());
    EXPECT_DOUBLE_EQ(s.gainDb(0), 85.0);  // failed sets did not change state
}

TEST(Settings, EditsPreserveUnknownBytes) {
    auto b = blob(0x80);
    for (std::size_t i = 0; i < b.size(); ++i) b[i] = static_cast<std::uint8_t>(0xA0 + (i % 7));
    auto s = SettingsMessage::fromBytes(b).value();
    ASSERT_TRUE(s.setGainDb(0, 30.0).ok());
    ASSERT_TRUE(s.setMute(1, true).ok());
    auto out = s.bytes();
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (i == 0 || i == 1 || i == 12) continue;  // the only bytes we touched
        EXPECT_EQ(out[i], b[i]) << "byte " << i << " changed";
    }
    EXPECT_EQ(rdI16(out, 0), 30 * 256);
    EXPECT_EQ(out[12], 1);
}

TEST(Settings, VolumeClamp) {
    SettingsMessage s;
    EXPECT_TRUE(s.setVolumeBDb(-60.0).ok());
    EXPECT_FALSE(s.setVolumeBDb(-60.5).ok());
    EXPECT_FALSE(s.setVolumeBDb(0.5).ok());
    EXPECT_EQ(s.bytes()[27], 0x00);
    EXPECT_EQ(s.bytes()[28], 0xC4);
}

TEST(Status, Decode) {
    auto b = blob(0x40);
    b[0] = 0x00; b[1] = 0xF6;        // -2560/256 = -10 dB
    b[9] = 0x00; b[10] = 0x80;       // far below -60 => clamped
    b[0x24] = 77;
    b[0x23] = 0x20;
    auto m = StatusMessage::fromBytes(b).value();
    EXPECT_DOUBLE_EQ(m.input1LevelDb(), -10.0);
    EXPECT_DOUBLE_EQ(m.input2LevelDb(), -60.0);
    EXPECT_EQ(m.autosetupProgress(), 77);
    EXPECT_EQ(m.autosetupResultRaw(), 2);
    b[0x23] = 0x60;
    EXPECT_EQ(StatusMessage::fromBytes(b).value().autosetupResultRaw(), 0);
    b[0x23] = 0x00;
    EXPECT_EQ(StatusMessage::fromBytes(b).value().autosetupResultRaw(), 1);
}

TEST(Status, PositiveLevelClampedToZero) {
    auto b = blob(0x40);
    b[0] = 0x00; b[1] = 0x05;  // +5 dB raw
    EXPECT_DOUBLE_EQ(StatusMessage::fromBytes(b).value().input1LevelDb(), 0.0);
}

TEST(Status, ShortRejected) {
    EXPECT_EQ(StatusMessage::fromBytes(blob(0x3F)).error().code, Errc::ShortTransfer);
    EXPECT_FALSE(VersionMessage::fromBytes(blob(3)).ok());
}

TEST(Trigger, Encoding) {
    auto t = TriggerMessage(Trigger::AutoGainBoth).bytes();
    ASSERT_EQ(t.size(), 0x40u);
    EXPECT_EQ(t[0], 3); EXPECT_EQ(t[1], 0);
    for (std::size_t i = 2; i < t.size(); ++i) EXPECT_EQ(t[i], 0);
    auto u = TriggerMessage(Trigger::Unknown0x103).bytes();
    EXPECT_EQ(u[0], 0x03); EXPECT_EQ(u[1], 0x01);
}
