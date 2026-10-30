#include "desktop/SampleRates.hpp"

#include <regex>

namespace lewitt {

namespace {
constexpr const char* kMarker = "# OpenWitty: allowed sample rates for the CONNECT 2";
}

const std::vector<int>& connect2SampleRates() {
    static const std::vector<int> r{44100, 48000, 96000};
    return r;
}

std::string renderSampleRatesConfig() {
    std::string rates;
    for (int r : connect2SampleRates()) rates += " " + std::to_string(r);
    return std::string(kMarker) + "\n"
           "# Lets PipeWire run at an app's rate (44.1 / 48 / 96 kHz) instead of resampling everything to 48 kHz.\n"
           "# Applies to all sound cards. Remove this file (or switch the option off in OpenWitty) to undo.\n"
           "context.properties = {\n"
           "    default.clock.allowed-rates = [" + rates + " ]\n"
           "}\n";
}

bool isOpenWittySampleRatesConfig(const std::string& text) { return text.rfind(kMarker, 0) == 0; }

std::string sampleRatesPath(const std::string& configHome) {
    if (configHome.empty()) return {};
    return configHome + "/pipewire/pipewire.conf.d/" + kSampleRatesFile;
}

std::optional<std::vector<int>> parseAllowedRates(const std::string& out) {
    static const std::regex line(R"(key:'clock\.allowed-rates'\s+value:'\[([^\]]*)\]')");
    std::smatch m;
    if (!std::regex_search(out, m, line)) return std::nullopt;
    std::vector<int> rates;
    const std::string body = m[1].str();
    static const std::regex num(R"(\d+)");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), num); it != std::sregex_iterator(); ++it)
        if (it->str().size() <= 7) rates.push_back(std::stoi(it->str()));   // never overflows
    return rates;
}

}  // namespace lewitt
