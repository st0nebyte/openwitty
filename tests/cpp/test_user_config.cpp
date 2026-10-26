// Phase 8G: the shared user configuration.
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

#include "core/UserConfig.hpp"

using namespace lewitt;

TEST(UserConfig, DefaultsWriteNothingAndSyncNothing) {
    UserConfig c;
    EXPECT_EQ(c.desktopVolumeSync, DesktopVolumeSync::Off);
    EXPECT_FALSE(c.vendorFallback);
    EXPECT_FALSE(c.speakerControl);
    EXPECT_FALSE(c.headphoneControl);
    EXPECT_FALSE(c.ledBrightnessControl);
    EXPECT_FALSE(c.legacyOutputControlPending);
    EXPECT_EQ(parseUserConfig("").config, UserConfig{});
}

TEST(UserConfig, RoundTrip) {
    for (auto sync : {DesktopVolumeSync::Off, DesktopVolumeSync::Mirror})
        for (bool fb : {false, true})
            for (int perms = 0; perms < 8; ++perms) {       // every combination of the three permissions
                UserConfig c;
                c.desktopVolumeSync = sync;
                c.vendorFallback = fb;
                c.speakerControl = perms & 1;
                c.headphoneControl = perms & 2;
                c.ledBrightnessControl = perms & 4;
                auto p = parseUserConfig(serializeUserConfig(c));
                EXPECT_EQ(p.config, c);
                EXPECT_TRUE(p.warnings.empty());
            }
}

TEST(UserConfig, InvalidOrUnknownValuesFallBackToTheSafeDefault) {
    auto p = parseUserConfig("desktop_volume_sync = always\nvendor_fallback = maybe\noutput_control=TRUE\nvolume = 11\nnonsense\n");
    EXPECT_EQ(p.config.desktopVolumeSync, DesktopVolumeSync::Off);
    EXPECT_FALSE(p.config.vendorFallback);
    EXPECT_FALSE(p.config.legacyOutputControlPending);   // values are case-sensitive: TRUE is not accepted
    EXPECT_EQ(p.warnings.size(), 5u);
}

TEST(UserConfig, CommentsHeaderAndSpacing) {
    auto p = parseUserConfig("# x\n[openwitty]\n  desktop_volume_sync=mirror   # trailing\n\nvendor_fallback = yes\n");
    EXPECT_EQ(p.config.desktopVolumeSync, DesktopVolumeSync::Mirror);
    EXPECT_TRUE(p.config.vendorFallback);
    EXPECT_TRUE(p.warnings.empty());
}

TEST(UserConfig, PathFollowsXdg) {
    const char* oldX = std::getenv("XDG_CONFIG_HOME");
    const char* oldH = std::getenv("HOME");
    const std::string savedX = oldX ? oldX : "", savedH = oldH ? oldH : "";
    setenv("XDG_CONFIG_HOME", "/x/cfg", 1);
    EXPECT_EQ(userConfigPath(), "/x/cfg/openwitty/openwitty.conf");
    setenv("XDG_CONFIG_HOME", "relative/cfg", 1);          // not absolute: ignored per the spec
    setenv("HOME", "/home/u", 1);
    EXPECT_EQ(userConfigPath(), "/home/u/.config/openwitty/openwitty.conf");
    unsetenv("XDG_CONFIG_HOME");
    EXPECT_EQ(userConfigPath(), "/home/u/.config/openwitty/openwitty.conf");
    if (oldX) setenv("XDG_CONFIG_HOME", savedX.c_str(), 1); else unsetenv("XDG_CONFIG_HOME");
    if (oldH) setenv("HOME", savedH.c_str(), 1); else unsetenv("HOME");
}

TEST(UserConfig, SaveIsAtomicPrivateAndLoadable) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("ow-cfg-" + std::to_string(getpid()));
    fs::remove_all(root);
    const std::string path = (root / "openwitty" / "openwitty.conf").string();
    EXPECT_EQ(loadUserConfig(path).config, UserConfig{});        // missing file: defaults, no warning
    EXPECT_TRUE(loadUserConfig(path).warnings.empty());
    UserConfig c;
    c.desktopVolumeSync = DesktopVolumeSync::Mirror;
    c.vendorFallback = true;
    ASSERT_TRUE(saveUserConfig(c, path).ok());
    EXPECT_EQ(loadUserConfig(path).config, c);
    struct stat st{};
    ASSERT_EQ(stat(path.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600u);
    ASSERT_EQ(stat((root / "openwitty").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0700u);
    for (const auto& e : fs::directory_iterator(root / "openwitty"))           // no temporary left behind
        EXPECT_EQ(e.path().filename(), "openwitty.conf");
    ASSERT_TRUE(saveUserConfig(UserConfig{}, path).ok());                       // overwrite (reset)
    EXPECT_EQ(loadUserConfig(path).config, UserConfig{});
    fs::remove_all(root);
}

TEST(UserConfig, InvalidValueAfterAValidOneFallsBackToTheSafeDefault) {
    auto p = parseUserConfig("desktop_volume_sync = off\ndesktop_volume_sync = mirror\ndesktop_volume_sync = bogus\n");
    EXPECT_EQ(p.config.desktopVolumeSync, DesktopVolumeSync::Off);      // never mirror after the warning says off
    auto q = parseUserConfig("vendor_fallback = true\nvendor_fallback = maybe\n");
    EXPECT_FALSE(q.config.vendorFallback);
}

TEST(UserConfig, DuplicatedKeysAreAmbiguousAndFailSafe) {
    for (const char* text : {"vendor_fallback = true\nvendor_fallback = true\n",
                             "vendor_fallback = false\nvendor_fallback = true\n",
                             "output_control = true\n# later\noutput_control = true\n",
                             "desktop_volume_sync = mirror\ndesktop_volume_sync = mirror\n"}) {
        auto p = parseUserConfig(text);
        EXPECT_FALSE(p.config.vendorFallback) << text;
        EXPECT_FALSE(p.config.legacyOutputControlPending) << text;
        EXPECT_EQ(p.config.desktopVolumeSync, DesktopVolumeSync::Off) << text;
        bool warned = false;
        for (const auto& w : p.warnings) warned |= w.find("more than once") != std::string::npos;
        EXPECT_TRUE(warned) << text;
    }
    // a duplicate of one key does not disable the others
    auto p = parseUserConfig("desktop_volume_sync = mirror\noutput_control = true\noutput_control = true\n");
    EXPECT_EQ(p.config.desktopVolumeSync, DesktopVolumeSync::Mirror);
    EXPECT_FALSE(p.config.legacyOutputControlPending);
}

TEST(UserConfig, MalformedFilesNeverEnableDeviceWrites) {
    for (const char* text : {"vendor_fallback true\n", "vendor_fallback: true\n", "= true\n", "vendor_fallback =\n",
                             "VENDOR_FALLBACK = true\n", "vendor_fallback = true # = false\noutput_control==true\n",
                             "\x01\x02garbage\n", "[other]\noutput_control = yes please\n"}) {
        auto p = parseUserConfig(text);
        EXPECT_FALSE(p.config.speakerControl || p.config.headphoneControl || p.config.ledBrightnessControl) << text;
        EXPECT_FALSE(p.config.legacyOutputControlPending) << text;
        if (std::string(text).rfind("vendor_fallback = true # = false", 0) != 0) EXPECT_FALSE(p.config.vendorFallback) << text;
        EXPECT_FALSE(p.warnings.empty()) << text;
    }
    // comment handling: "true # = false" is the value true (documented '#' comment rule), clearly visible
    EXPECT_TRUE(parseUserConfig("vendor_fallback = true # = false\n").config.vendorFallback);
}

// ---- Phase 10B: independent permissions and the legacy migration ----------------------------------------------

TEST(UserConfigPermissions, EachPermissionIsIndependentAndDuplicatesOrInvalidValuesFailClosed) {
    auto p = parseUserConfig("speaker_control = true\nheadphone_control = false\nled_brightness_control = true\n");
    EXPECT_TRUE(p.config.speakerControl);
    EXPECT_FALSE(p.config.headphoneControl);
    EXPECT_TRUE(p.config.ledBrightnessControl);
    EXPECT_TRUE(p.warnings.empty());
    for (const char* text : {"speaker_control = true\nspeaker_control = true\n", "speaker_control = yes please\n",
                             "speaker_control = TRUE\n", "speaker_control = true\nspeaker_control = maybe\n"}) {
        auto q = parseUserConfig(text);
        EXPECT_FALSE(q.config.speakerControl) << text;
        EXPECT_FALSE(q.warnings.empty()) << text;
    }
    auto d = parseUserConfig("led_brightness_control = true\nheadphone_control = true\nheadphone_control = true\n");
    EXPECT_TRUE(d.config.ledBrightnessControl);          // a duplicate of one key does not disable the others
    EXPECT_FALSE(d.config.headphoneControl);
}

TEST(UserConfigMigration, LegacyOutputControlFalseGrantsNothingAndAsksNothing) {
    const char* old = "[openwitty]\ndesktop_volume_sync = mirror\nvendor_fallback = false\noutput_control = false\n";
    auto p = parseUserConfig(old);
    EXPECT_EQ(p.config.desktopVolumeSync, DesktopVolumeSync::Mirror);   // unrelated settings preserved
    EXPECT_FALSE(p.config.vendorFallback);
    EXPECT_FALSE(p.config.speakerControl || p.config.headphoneControl || p.config.ledBrightnessControl);
    EXPECT_FALSE(p.config.legacyOutputControlPending);
    EXPECT_TRUE(p.warnings.empty());
}

TEST(UserConfigMigration, LegacyOutputControlTrueGrantsNothingButAsksForReconfirmation) {
    const char* old = "[openwitty]\ndesktop_volume_sync = mirror\nvendor_fallback = false\noutput_control = true\n";
    auto p = parseUserConfig(old);
    EXPECT_EQ(p.config.desktopVolumeSync, DesktopVolumeSync::Mirror);
    EXPECT_FALSE(p.config.speakerControl);
    EXPECT_FALSE(p.config.headphoneControl);
    EXPECT_FALSE(p.config.ledBrightnessControl);
    EXPECT_TRUE(p.config.legacyOutputControlPending);
    ASSERT_EQ(p.warnings.size(), 1u);
    EXPECT_NE(p.warnings[0].find("no longer grants anything"), std::string::npos);
    // the notice survives a save of unrelated settings, and still grants nothing after reloading
    const std::string text = serializeUserConfig(p.config);
    EXPECT_NE(text.find("output_control = true"), std::string::npos);
    EXPECT_NE(text.find("grants NOTHING"), std::string::npos);
    auto again = parseUserConfig(text);
    EXPECT_EQ(again.config, p.config);
    // once a new output permission is given, the legacy marker is gone
    UserConfig confirmed = p.config;
    confirmed.speakerControl = true;
    confirmed.legacyOutputControlPending = false;
    EXPECT_EQ(serializeUserConfig(confirmed).find("output_control"), std::string::npos);
    EXPECT_FALSE(parseUserConfig("output_control = true\nheadphone_control = true\n").config.legacyOutputControlPending);
}

TEST(UserConfigMigration, ANewFileNeverCarriesTheLegacyKeyAndOldDefaultsStayOff) {
    const std::string fresh = serializeUserConfig(UserConfig{});
    EXPECT_EQ(fresh.find("output_control"), std::string::npos);
    EXPECT_NE(fresh.find("speaker_control = false"), std::string::npos);
    EXPECT_NE(fresh.find("headphone_control = false"), std::string::npos);
    EXPECT_NE(fresh.find("led_brightness_control = false"), std::string::npos);
}

TEST(UserConfigMigration, FailedSaveLeavesTheFileUnchanged) {
    if (geteuid() == 0) GTEST_SKIP() << "directory permissions do not restrict root";
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("ow-cfg-ro-" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root / "openwitty");
    const std::string path = (root / "openwitty" / "openwitty.conf").string();
    UserConfig before;
    before.desktopVolumeSync = DesktopVolumeSync::Mirror;
    ASSERT_TRUE(saveUserConfig(before, path).ok());
    chmod((root / "openwitty").c_str(), 0500);          // directory not writable: the temporary file cannot be made
    UserConfig next = before;
    next.speakerControl = true;
    EXPECT_FALSE(saveUserConfig(next, path).ok());
    EXPECT_EQ(loadUserConfig(path).config, before);     // old content intact, nothing half-written
    chmod((root / "openwitty").c_str(), 0700);
    fs::remove_all(root);
}
