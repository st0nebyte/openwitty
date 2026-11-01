#include <gtest/gtest.h>

#include "core/UserConfig.hpp"
#include "desktop/PortNames.hpp"

using namespace lewitt;

namespace {
// Shaped like `pw-cli ls Node` / `pw-cli ls Port` on the owner's system (serial replaced).
const char* kNodes =
    "\tid 97, type PipeWire:Interface:Node/3\n"
    " \t\tnode.description = \"CONNECT 2 (1 Mic, 2 Instrument, 3+4 Loopback)\"\n"
    " \t\tnode.name = \"alsa_input.usb-Lewitt_GmbH_CONNECT_2_X000XXX00000-00.analog-surround-40\"\n"
    "\tid 91, type PipeWire:Interface:Node/3\n"
    " \t\tnode.name = \"alsa_output.usb-Lewitt_GmbH_CONNECT_2_X000XXX00000-00.analog-stereo\"\n"
    "\tid 44, type PipeWire:Interface:Node/3\n"
    " \t\tnode.name = \"openwitty_connect2_loopback\"\n";
std::string port(int id, int node, const char* ch, const char* dir, bool monitor = false) {
    return "\tid " + std::to_string(id) + ", type PipeWire:Interface:Port/3\n"
           " \t\tnode.id = \"" + std::to_string(node) + "\"\n"
           " \t\taudio.channel = \"" + ch + "\"\n"
           " \t\tport.direction = \"" + dir + "\"\n" + (monitor ? " \t\tport.monitor = \"true\"\n" : "");
}
std::string ports() {
    return port(85, 97, "FL", "out") + port(86, 97, "FR", "out") + port(87, 97, "RL", "out") + port(88, 97, "RR", "out") +
           port(60, 91, "FL", "in") + port(61, 91, "FR", "in") + port(62, 91, "FL", "out", true) +
           port(63, 91, "FR", "out", true) + port(70, 44, "FL", "out");
}
struct FakePw {
    std::string meta = "Found \"default\" metadata 51\n"
                       "update: id:85 key:'http://jackaudio.org/metadata/pretty-name' value:'1 Microphone' type:''\n"
                       "update: id:86 key:'http://jackaudio.org/metadata/pretty-name' value:'Mine' type:''\n";
    std::vector<std::vector<std::string>> calls;
    bool pipewireDown{false};
    int operator()(const std::vector<std::string>& argv, std::string* out) {
        calls.push_back(argv);
        if (pipewireDown) return 1;
        std::string o;
        if (argv == std::vector<std::string>{"pw-cli", "ls", "Node"}) o = kNodes;
        else if (argv == std::vector<std::string>{"pw-cli", "ls", "Port"}) o = ports();
        else if (argv == std::vector<std::string>{"pw-metadata"}) o = meta;
        if (out) *out = o;
        return 0;
    }
    std::vector<std::vector<std::string>> sets() const {
        std::vector<std::vector<std::string>> r;
        for (const auto& c : calls) if (c.size() >= 3 && c[0] == "pw-metadata") r.push_back(c);
        return r;
    }
};
}  // namespace

TEST(PortNames, NamesTheFourCaptureChannelsAndTheTwoOutputsOnly) {
    const auto w = wantedPortNames(parsePwCliList(kNodes), parsePwCliList(ports()));
    std::map<int, std::string> m;
    for (const auto& p : w) m[p.portId] = p.name;
    EXPECT_EQ(m, (std::map<int, std::string>{{85, "1 Microphone"}, {86, "2 Instrument"}, {87, "3 Loopback L"},
                                             {88, "4 Loopback R"}, {60, "Output L"}, {61, "Output R"}}))
        << "monitor ports and the split loopback source are left alone";
}

TEST(PortNames, AnotherInputProfileIsLeftAlone) {
    const std::string stereo = port(85, 97, "FL", "out") + port(86, 97, "FR", "out");
    EXPECT_TRUE(wantedPortNames(parsePwCliList(kNodes), parsePwCliList(stereo)).empty());
}

TEST(PortNames, ParsesPrettyNamesAndSetsOnlyWhatIsMissing) {
    FakePw pw;
    EXPECT_EQ(parsePrettyNames(pw.meta), (std::map<int, std::string>{{85, "1 Microphone"}, {86, "Mine"}}));
    EXPECT_EQ(applyPortNames(std::ref(pw)), 5);
    const auto s = pw.sets();
    ASSERT_EQ(s.size(), 5u);
    for (const auto& c : s) EXPECT_NE(c[1], "85") << "already named";
    EXPECT_EQ(s[0], (std::vector<std::string>{"pw-metadata", "86", kPrettyNameKey, "2 Instrument"}))
        << "a different name is replaced; the name is one argument (no shell)";
}

TEST(PortNames, ClearRemovesOnlyOpenWittysOwnNames) {
    FakePw pw;
    EXPECT_EQ(clearPortNames(std::ref(pw)), 1);
    const auto s = pw.sets();
    ASSERT_EQ(s.size(), 1u);
    EXPECT_EQ(s[0], (std::vector<std::string>{"pw-metadata", "-d", "85", kPrettyNameKey}));
}

TEST(PortNames, NothingIsSetWhenPipeWireCannotBeRead) {
    FakePw pw;
    pw.pipewireDown = true;
    EXPECT_EQ(applyPortNames(std::ref(pw)), -1);
    EXPECT_TRUE(pw.sets().empty());
}

TEST(PortNames, SettingDefaultsOnAndRoundTrips) {
    EXPECT_TRUE(UserConfig{}.nameChannels);
    UserConfig c;
    c.nameChannels = false;
    const auto parsed = parseUserConfig(serializeUserConfig(c));
    EXPECT_FALSE(parsed.config.nameChannels);
    EXPECT_TRUE(parsed.warnings.empty());
}

TEST(PortNames, ClockRateIsReadFromPipeWireSettings) {
    EXPECT_EQ(parseClockRate("Found \"settings\" metadata 34\nupdate: id:0 key:'clock.rate' value:'48000' type:''\n"), 48000u);
    EXPECT_EQ(parseClockRate("update: id:0 key:'clock.rate' value:'96000' type:''"), 96000u);
    EXPECT_FALSE(parseClockRate("Found \"settings\" metadata 34\n").has_value());
    EXPECT_FALSE(parseClockRate("update: id:0 key:'clock.rate' value:'12' type:''").has_value());
    EXPECT_FALSE(parseClockRate("update: id:0 key:'clock.force-rate' value:'0' type:''").has_value());
}
