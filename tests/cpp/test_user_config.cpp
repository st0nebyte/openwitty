// Phase 8G: the shared user configuration.
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

#include "core/UserConfig.hpp"

using namespace lewitt;

TEST(UserConfig, DefaultsSyncNothing) {
    UserConfig c;
    EXPECT_EQ(c.desktopVolumeSync, DesktopVolumeSync::Off);
    EXPECT_FALSE(c.vendorFallback);
    EXPECT_FALSE(c.followActiveOutput);
    EXPECT_EQ(parseUserConfig("").config, UserConfig{});
}

TEST(UserConfig, RoundTrip) {
    for (auto sync : {DesktopVolumeSync::Off, DesktopVolumeSync::Mirror})
        for (bool fb : {false, true})
            for (bool follow : {false, true}) {
                UserConfig c;
                c.desktopVolumeSync = sync;
                c.vendorFallback = fb;
                c.followActiveOutput = follow;
                auto p = parseUserConfig(serializeUserConfig(c));
                EXPECT_EQ(p.config, c);
                EXPECT_TRUE(p.warnings.empty());
            }
}

TEST(UserConfig, InvalidOrUnknownValuesFallBackToTheSafeDefault) {
    auto p = parseUserConfig("desktop_volume_sync = always\nvendor_fallback = maybe\nfollow_active_output=TRUE\nvolume = 11\nnonsense\n");
    EXPECT_EQ(p.config.desktopVolumeSync, DesktopVolumeSync::Off);
    EXPECT_FALSE(p.config.vendorFallback);
    EXPECT_FALSE(p.config.followActiveOutput);   // values are case-sensitive: TRUE is not accepted
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
                             "follow_active_output = true\n# later\nfollow_active_output = true\n",
                             "desktop_volume_sync = mirror\ndesktop_volume_sync = mirror\n"}) {
        auto p = parseUserConfig(text);
        EXPECT_FALSE(p.config.vendorFallback) << text;
        EXPECT_FALSE(p.config.followActiveOutput) << text;
        EXPECT_EQ(p.config.desktopVolumeSync, DesktopVolumeSync::Off) << text;
        bool warned = false;
        for (const auto& w : p.warnings) warned |= w.find("more than once") != std::string::npos;
        EXPECT_TRUE(warned) << text;
    }
    // a duplicate of one key does not disable the others
    auto p = parseUserConfig("desktop_volume_sync = mirror\nfollow_active_output = true\nfollow_active_output = true\n");
    EXPECT_EQ(p.config.desktopVolumeSync, DesktopVolumeSync::Mirror);
    EXPECT_FALSE(p.config.followActiveOutput);
}

TEST(UserConfig, MalformedFilesNeverEnableTheFallback) {
    for (const char* text : {"vendor_fallback true\n", "vendor_fallback: true\n", "= true\n", "vendor_fallback =\n",
                             "VENDOR_FALLBACK = true\n", "\x01\x02garbage\n", "[other]\nvendor_fallback = yes please\n"}) {
        auto p = parseUserConfig(text);
        EXPECT_FALSE(p.config.vendorFallback) << text;
        EXPECT_FALSE(p.warnings.empty()) << text;
    }
    // comment handling: "true # = false" is the value true (documented '#' comment rule), clearly visible
    EXPECT_TRUE(parseUserConfig("vendor_fallback = true # = false\n").config.vendorFallback);
}

// ---- Product mode (2026-11-01): the evaluation-build permissions are obsolete ---------------------------------

TEST(UserConfigProductMode, OldPermissionKeysAreIgnoredWithoutWarningAndDroppedAtTheNextSave) {
    const char* old = "[openwitty]\ndesktop_volume_sync = mirror\nspeaker_control = true\nheadphone_control = false\n"
                      "led_brightness_control = true\ninput_control = true\nprocessing_control = true\n"
                      "monitoring_control = true\nappearance_control = true\nphantom_control = true\n"
                      "output_modes_control = true\nmeasurement_control = true\nrouting_control = true\n"
                      "output_control = true\nfollow_active_output = true\n";
    auto p = parseUserConfig(old);
    EXPECT_TRUE(p.warnings.empty());
    EXPECT_EQ(p.config.desktopVolumeSync, DesktopVolumeSync::Mirror);   // unrelated settings preserved
    EXPECT_TRUE(p.config.followActiveOutput);                           // a feature switch, not a permission
    const std::string text = serializeUserConfig(p.config);
    for (const char* k : {"speaker_control", "headphone_control", "led_brightness_control", "input_control",
                          "processing_control", "monitoring_control", "appearance_control", "phantom_control",
                          "output_modes_control", "measurement_control", "routing_control", "output_control"}) {
        EXPECT_TRUE(isObsoletePermissionKey(k)) << k;
        EXPECT_EQ(text.find(k), std::string::npos) << k;
    }
    EXPECT_FALSE(isObsoletePermissionKey("follow_active_output"));
    EXPECT_FALSE(isObsoletePermissionKey("vendor_fallback"));
    EXPECT_EQ(parseUserConfig(text).config, p.config);
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
    next.followActiveOutput = true;
    EXPECT_FALSE(saveUserConfig(next, path).ok());
    EXPECT_EQ(loadUserConfig(path).config, before);     // old content intact, nothing half-written
    chmod((root / "openwitty").c_str(), 0700);
    fs::remove_all(root);
}
