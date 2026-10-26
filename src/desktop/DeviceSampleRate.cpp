#include "desktop/DeviceSampleRate.hpp"

#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>

#include "desktop/DesktopMixer.hpp"

namespace lewitt {

namespace {
std::string slurp(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::stringstream b;
    b << f.rdbuf();
    return b.str();
}
}  // namespace

std::optional<unsigned> parseHwParamsRate(const std::string& text) {
    static const std::regex re(R"((^|\n)rate:\s*([0-9]+)\s*\(([0-9]+)/([0-9]+)\))");
    std::smatch m;
    if (!std::regex_search(text, m, re)) return std::nullopt;
    const unsigned long num = std::stoul(m[3]), den = std::stoul(m[4]), hz = std::stoul(m[2]);
    if (den == 0 || hz == 0 || hz > 768000 || num % den != 0 || num / den != hz) return std::nullopt;   // fractional: not trusted
    return static_cast<unsigned>(hz);
}

std::optional<unsigned> parseStreamMomentaryRate(const std::string& text, const std::string& direction) {
    const auto start = text.find(direction + ":");
    if (start == std::string::npos) return std::nullopt;
    auto end = text.find(direction == "Playback" ? "\nCapture:" : "\nPlayback:", start);
    const std::string block = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (block.find("Status: Running") == std::string::npos) return std::nullopt;
    static const std::regex re(R"(Momentary freq = ([0-9]+) Hz)");
    std::smatch m;
    if (!std::regex_search(block, m, re)) return std::nullopt;
    return static_cast<unsigned>(std::stoul(m[1]));
}

DeviceSampleRate readConnect2SampleRate(const std::string& root, const std::string& usbId) {
    DeviceSampleRate r;
    const auto card = findCardByUsbId(root, usbId);
    if (!card) {
        r.reason = "no CONNECT 2 sound card";
        return r;
    }
    const std::string dir = root + "/card" + std::to_string(*card);
    const auto play = parseHwParamsRate(slurp(dir + "/pcm0p/sub0/hw_params"));
    const auto cap = parseHwParamsRate(slurp(dir + "/pcm0c/sub0/hw_params"));
    if (play && cap && *play != *cap) {
        r.reason = "playback and capture report different rates";
        return r;
    }
    const auto hw = play ? play : cap;
    if (!hw) {
        r.reason = "no stream open on the CONNECT 2 (the rate is set when audio starts)";
        return r;
    }
    // Cross-check with the measured USB rate when a stream runs; a clear mismatch makes it unknown.
    const std::string stream = slurp(dir + "/stream0");
    for (const char* d : {"Playback", "Capture"}) {
        const auto measured = parseStreamMomentaryRate(stream, d);
        if (measured && std::fabs(static_cast<double>(*measured) - *hw) > *hw * 0.01) {
            r.reason = "the measured USB rate disagrees with the configured rate";
            return r;
        }
    }
    r.hz = hw;
    r.reason = "kernel hw_params of the CONNECT 2";
    return r;
}

}  // namespace lewitt
