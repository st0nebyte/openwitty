// SR: OpenWitty's optional PipeWire file that allows the CONNECT 2's rates (44.1 / 48 / 96 kHz).
#include <gtest/gtest.h>

#include "desktop/SampleRates.hpp"

using namespace lewitt;

TEST(SampleRates, ConfigAllowsExactlyTheDeviceRates) {
    const std::string t = renderSampleRatesConfig();
    EXPECT_NE(t.find("default.clock.allowed-rates = [ 44100 48000 96000 ]"), std::string::npos) << t;
    EXPECT_NE(t.find("context.properties = {"), std::string::npos);
    EXPECT_TRUE(isOpenWittySampleRatesConfig(t));
    EXPECT_FALSE(isOpenWittySampleRatesConfig("context.properties = { default.clock.allowed-rates = [ 48000 ] }\n"));
    EXPECT_FALSE(isOpenWittySampleRatesConfig(""));
}

TEST(SampleRates, PathLivesInTheUsersPipeWireConfig) {
    EXPECT_EQ(sampleRatesPath("/home/u/.config"), "/home/u/.config/pipewire/pipewire.conf.d/54-openwitty-sample-rates.conf");
    EXPECT_EQ(sampleRatesPath(""), "");
}

TEST(SampleRates, ParsesPwMetadataOutput) {
    auto r = parseAllowedRates("Found \"settings\" metadata 32\nupdate: id:0 key:'clock.allowed-rates' value:'[ 48000 ]' type:''\n");
    ASSERT_TRUE(r);
    EXPECT_EQ(*r, std::vector<int>({48000}));
    r = parseAllowedRates("update: id:0 key:'clock.allowed-rates' value:'[ 44100 48000 96000 ]' type:''");
    ASSERT_TRUE(r);
    EXPECT_EQ(*r, std::vector<int>({44100, 48000, 96000}));
    EXPECT_FALSE(parseAllowedRates("Found \"settings\" metadata 32\n"));
    r = parseAllowedRates("update: id:0 key:'clock.allowed-rates' value:'[ 99999999999999 ]' type:''");
    ASSERT_TRUE(r);
    EXPECT_TRUE(r->empty());   // absurd numbers are ignored, never overflow
}
