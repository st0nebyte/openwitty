// Phase 11A.4: generated PipeWire config for separate CONNECT 2 inputs.
#include <gtest/gtest.h>

#include <algorithm>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "desktop/SplitSources.hpp"

using namespace lewitt;

namespace {
const std::string kNode = "alsa_input.usb-Lewitt_GmbH_CONNECT_2_X000XXX00000-00.analog-surround-40";
}

TEST(SplitSources, AcceptsOnlyConnect2CaptureNodes) {
    EXPECT_TRUE(isConnect2CaptureNodeName(kNode));
    EXPECT_TRUE(isConnect2CaptureNodeName("alsa_input.usb-Lewitt_GmbH_CONNECT_2-00.pro-input-0"));
    EXPECT_FALSE(isConnect2CaptureNodeName("alsa_output.usb-Lewitt_GmbH_CONNECT_2-00.analog-stereo"));
    EXPECT_FALSE(isConnect2CaptureNodeName("alsa_input.pci-0000_0c_00.4.analog-stereo"));
    EXPECT_FALSE(isConnect2CaptureNodeName("alsa_input.usb-Lewitt_GmbH_CONNECT_2\" } ]\ncontext.exec = ["));
    EXPECT_FALSE(isConnect2CaptureNodeName(""));
    EXPECT_FALSE(renderSplitSourcesConfig("alsa_input.usb-Other-00").has_value());
}

TEST(SplitSources, ConfigMapsTheVerifiedChannelOrderAndRoundTrips) {
    const auto c = renderSplitSourcesConfig(kNode);
    ASSERT_TRUE(c.has_value());
    const std::string& s = *c;
    auto block = [&](const char* id) {
        const auto b = s.find(std::string("openwitty.connect2.") + id + ".capture");
        EXPECT_NE(b, std::string::npos) << id;
        return s.substr(b, 400);
    };
    EXPECT_NE(block("microphone").find("audio.position = [ FL ]"), std::string::npos);
    EXPECT_NE(block("instrument").find("audio.position = [ FR ]"), std::string::npos);
    EXPECT_NE(block("loopback").find("audio.position = [ RL RR ]"), std::string::npos);
    EXPECT_NE(s.find("\"CONNECT 2 Microphone\""), std::string::npos);
    EXPECT_EQ(std::string::npos, s.find("context.exec"));
    EXPECT_EQ(splitSourcesTarget(s), kNode);
    EXPECT_FALSE(splitSourcesTarget("context.modules = []").has_value());
    EXPECT_EQ(splitSourcesPath("/h/.config"), "/h/.config/pipewire/pipewire.conf.d/53-openwitty-connect2-split.conf");
    EXPECT_EQ(splitSourcesPath(""), "");
}

// The generated text is valid SPA-JSON (PipeWire's config syntax), when the parser is installed.
TEST(SplitSources, ConfigParsesAsSpaJson) {
    if (std::system("command -v spa-json-dump >/dev/null 2>&1") != 0) GTEST_SKIP() << "spa-json-dump not installed";
    const auto p = std::filesystem::temp_directory_path() / "ow-split-test.conf";
    std::ofstream(p) << *renderSplitSourcesConfig(kNode);
    const std::string cmd = "spa-json-dump '" + p.string() + "' > '" + p.string() + ".out' 2>&1";
    EXPECT_EQ(std::system(cmd.c_str()), 0);
    std::ifstream in(p.string() + ".out");
    const std::string out((std::istreambuf_iterator<char>(in)), {});
    EXPECT_NE(out.find("\"target.object\": \"" + kNode + "\""), std::string::npos) << out;
    EXPECT_NE(out.find("\"media.class\": \"Audio/Source\""), std::string::npos);
    std::filesystem::remove(p);
    std::filesystem::remove(p.string() + ".out");
}

TEST(SplitSources, TheMicrophoneIsThePreferredDefaultInputAndOnlyIt) {
    const auto c = renderSplitSourcesConfig(kNode);
    ASSERT_TRUE(c.has_value());
    std::size_t n = 0, pos = 0;
    while ((pos = c->find("priority.session =", pos)) != std::string::npos) { ++n; ++pos; }
    EXPECT_EQ(n, 1u);
    const auto mic = c->find("node.name = \"openwitty_connect2_microphone\"");
    const auto prio = c->find("priority.session = 2200");
    const auto inst = c->find("CONNECT 2 Instrument");
    ASSERT_NE(mic, std::string::npos);
    EXPECT_TRUE(prio > mic && prio < inst) << "in the microphone's playback props";
}
