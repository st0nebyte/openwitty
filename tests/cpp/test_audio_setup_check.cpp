// Phase 8F: read-only desktop audio setup diagnosis (pure parts).
#include <gtest/gtest.h>

#include "desktop/AudioSetupCheck.hpp"

using namespace lewitt;

TEST(AudioSetupCheck, ModprobeConfigIsRecognisedOnlyForThisDevice) {
    EXPECT_TRUE(modprobeTextConfiguresQuirk("# comment\noptions snd_usb_audio quirk_flags=29c2:0004:ignore_ctl_error\n"));
    EXPECT_TRUE(modprobeTextConfiguresQuirk("options snd-usb-audio index=1 quirk_flags=29C2:0004:IGNORE_CTL_ERROR"));
    EXPECT_FALSE(modprobeTextConfiguresQuirk("# options snd_usb_audio quirk_flags=29c2:0004:ignore_ctl_error"));
    EXPECT_FALSE(modprobeTextConfiguresQuirk("options snd_usb_audio quirk_flags=1234:5678:ignore_ctl_error"));
    EXPECT_FALSE(modprobeTextConfiguresQuirk("options snd_hda_intel quirk_flags=29c2:0004:ignore_ctl_error"));
    EXPECT_FALSE(modprobeTextConfiguresQuirk("options snd_usb_audio ignore_ctl_error=1"));    // global: not ours
}

TEST(AudioSetupCheck, ModuleParameterIsRecognised) {
    EXPECT_TRUE(quirkFlagsParamActive("29c2:0004:ignore_ctl_error,(null),(null)"));
    EXPECT_FALSE(quirkFlagsParamActive("(null),(null)"));
    EXPECT_FALSE(quirkFlagsParamActive("29c2:0004:skip_iface_setup"));
}

TEST(AudioSetupCheck, FindingsExplainWhatIsMissing) {
    AudioSetupStatus ok;
    ok.card = 3;
    ok.volumeControl = true;
    ok.quirkConfigured = ok.quirkActive = ok.moduleLoaded = true;
    EXPECT_TRUE(audioSetupFindings(ok).empty());

    AudioSetupStatus broken = ok;
    broken.quirkConfigured = broken.quirkActive = false;
    broken.mixerLoadError = -6;
    broken.volumeControl = false;
    auto f = audioSetupFindings(broken);
    ASSERT_FALSE(f.empty());
    EXPECT_NE(f[0].find("compatibility quirk"), std::string::npos);

    AudioSetupStatus pending = broken;
    pending.quirkConfigured = true;                            // configured, not yet applied
    f = audioSetupFindings(pending);
    ASSERT_GE(f.size(), 2u);
    EXPECT_NE(f[1].find("plugged in again"), std::string::npos);

    AudioSetupStatus absent;
    EXPECT_NE(audioSetupFindings(absent)[0].find("not present"), std::string::npos);

    AudioSetupStatus transient = ok;
    transient.quirkConfigured = false;                          // set at runtime only
    EXPECT_NE(audioSetupFindings(transient)[0].find("next boot"), std::string::npos);
}
