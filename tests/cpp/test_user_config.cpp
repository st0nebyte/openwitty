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
    EXPECT_FALSE(c.outputControl);
    EXPECT_EQ(parseUserConfig("").config, UserConfig{});
}

TEST(UserConfig, RoundTrip) {
    for (auto sync : {DesktopVolumeSync::Off, DesktopVolumeSync::Mirror})
        for (bool fb : {false, true})
            for (bool oc : {false, true}) {
                UserConfig c{sync, fb, oc};
                auto p = parseUserConfig(serializeUserConfig(c));
                EXPECT_EQ(p.config, c);
                EXPECT_TRUE(p.warnings.empty());
            }
}

TEST(UserConfig, InvalidOrUnknownValuesFallBackToTheSafeDefault) {
    auto p = parseUserConfig("desktop_volume_sync = always\nvendor_fallback = maybe\noutput_control=TRUE\nvolume = 11\nnonsense\n");
    EXPECT_EQ(p.config.desktopVolumeSync, DesktopVolumeSync::Off);
    EXPECT_FALSE(p.config.vendorFallback);
    EXPECT_FALSE(p.config.outputControl);      // values are case-sensitive: TRUE is not accepted
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
    UserConfig c{DesktopVolumeSync::Mirror, true, false};
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
        EXPECT_FALSE(p.config.outputControl) << text;
        EXPECT_EQ(p.config.desktopVolumeSync, DesktopVolumeSync::Off) << text;
        bool warned = false;
        for (const auto& w : p.warnings) warned |= w.find("more than once") != std::string::npos;
        EXPECT_TRUE(warned) << text;
    }
    // a duplicate of one key does not disable the others
    auto p = parseUserConfig("desktop_volume_sync = mirror\noutput_control = true\noutput_control = true\n");
    EXPECT_EQ(p.config.desktopVolumeSync, DesktopVolumeSync::Mirror);
    EXPECT_FALSE(p.config.outputControl);
}

TEST(UserConfig, MalformedFilesNeverEnableDeviceWrites) {
    for (const char* text : {"vendor_fallback true\n", "vendor_fallback: true\n", "= true\n", "vendor_fallback =\n",
                             "VENDOR_FALLBACK = true\n", "vendor_fallback = true # = false\noutput_control==true\n",
                             "\x01\x02garbage\n", "[other]\noutput_control = yes please\n"}) {
        auto p = parseUserConfig(text);
        EXPECT_FALSE(p.config.outputControl) << text;
        if (std::string(text).rfind("vendor_fallback = true # = false", 0) != 0) EXPECT_FALSE(p.config.vendorFallback) << text;
        EXPECT_FALSE(p.warnings.empty()) << text;
    }
    // comment handling: "true # = false" is the value true (documented '#' comment rule), clearly visible
    EXPECT_TRUE(parseUserConfig("vendor_fallback = true # = false\n").config.vendorFallback);
}
