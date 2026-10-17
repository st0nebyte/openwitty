#include <gtest/gtest.h>

#include "core/DeviceSession.hpp"
#include "transport/ReplayTransport.hpp"

using namespace lewitt;

namespace {
Bytes settings(std::uint8_t selected, bool allFourMuted = false, bool loopMuted = false) {
    Bytes b(SettingsMessage::kSize, 0);
    b[settings_off::kSelectedChannel] = selected;
    b[settings_off::kMute[0]] = b[settings_off::kMute[1]] = allFourMuted;
    b[settings_off::kSpeakerMute] = b[settings_off::kHeadphoneMute] = allFourMuted;
    b[settings_off::kLoopbackMute] = loopMuted;
    return b;
}
}  // namespace

TEST(ReplayTransport, SanitizedFramesAreDeterministicAndReadOnly) {
    auto t = std::make_unique<ReplayTransport>(std::vector<Bytes>{settings(1), settings(4)},
                                               std::vector<Bytes>{Bytes(StatusMessage::kSize, 0)});
    DeviceSession s(std::move(t));
    ASSERT_TRUE(s.connect().ok());
    ASSERT_TRUE(s.settings());
    ASSERT_TRUE(s.settings()->selectedChannel());
    EXPECT_EQ(*s.settings()->selectedChannel(), SelectedChannel::Microphone);
    ASSERT_TRUE(s.refreshSettings().ok());
    ASSERT_TRUE(s.settings()->selectedChannel());
    EXPECT_EQ(*s.settings()->selectedChannel(), SelectedChannel::Headphone);
    EXPECT_FALSE(s.writesEnabled());
    ASSERT_TRUE(s.edit([](SettingsMessage& m) { return m.setMute(0, true); }).ok());
    EXPECT_FALSE(s.commit().ok());
}

TEST(ReplayTransport, PanicMuteShapedSnapshotPreservesLoopbackAndSelection) {
    auto t = std::make_unique<ReplayTransport>(
        std::vector<Bytes>{settings(3, false, false), settings(3, true, false)},
        std::vector<Bytes>{Bytes(StatusMessage::kSize, 0)});
    DeviceSession s(std::move(t));
    ASSERT_TRUE(s.connect().ok());
    ASSERT_TRUE(s.refreshSettings().ok());
    const auto& value = *s.settings();
    EXPECT_TRUE(value.mute(0));
    EXPECT_TRUE(value.mute(1));
    EXPECT_TRUE(value.speakerMute());
    EXPECT_TRUE(value.headphoneMute());
    EXPECT_FALSE(value.loopbackMute());
    ASSERT_TRUE(value.selectedChannel());
    EXPECT_EQ(*value.selectedChannel(), SelectedChannel::Speaker);
}

TEST(ReplayTransport, UnknownSelectedChannelIsNotGuessed) {
    auto t = std::make_unique<ReplayTransport>(std::vector<Bytes>{settings(0x7f)});
    DeviceSession s(std::move(t));
    ASSERT_TRUE(s.connect().ok());
    EXPECT_EQ(s.settings()->selectedChannelRaw(), 0x7f);
    EXPECT_FALSE(s.settings()->selectedChannel().has_value());
}
