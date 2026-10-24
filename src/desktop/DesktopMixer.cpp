#include "desktop/DesktopMixer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

namespace lewitt {

std::optional<int> findCardByUsbId(const std::string& procAsoundRoot, const std::string& usbId) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<int> found;
    for (const auto& e : fs::directory_iterator(procAsoundRoot, ec)) {
        const std::string name = e.path().filename().string();
        if (name.size() < 5 || name.rfind("card", 0) != 0) continue;
        if (!std::all_of(name.begin() + 4, name.end(), [](unsigned char c) { return std::isdigit(c); })) continue;
        std::ifstream f(e.path() / "usbid");
        std::string id;
        if (!(f >> id)) continue;
        std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) { return std::tolower(c); });
        if (id == usbId) found.push_back(std::stoi(name.substr(4)));
    }
    if (found.size() != 1) return std::nullopt;     // absent, or ambiguous: never guess
    return found.front();
}

double rawToDb(long raw, long rawMin, long rawMax, double dbMin, double dbMax) {
    if (rawMax <= rawMin) return dbMin;
    raw = std::clamp(raw, rawMin, rawMax);
    return dbMin + (dbMax - dbMin) * static_cast<double>(raw - rawMin) / static_cast<double>(rawMax - rawMin);
}

long dbToRaw(double db, long rawMin, long rawMax, double dbMin, double dbMax) {
    if (rawMax <= rawMin || !(dbMax > dbMin) || !std::isfinite(db)) return rawMin;
    db = std::clamp(db, dbMin, dbMax);
    const double steps = (db - dbMin) / (dbMax - dbMin) * static_cast<double>(rawMax - rawMin);
    return std::clamp(rawMin + static_cast<long>(std::lround(steps)), rawMin, rawMax);
}

}  // namespace lewitt
