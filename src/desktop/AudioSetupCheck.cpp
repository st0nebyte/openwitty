#include "desktop/AudioSetupCheck.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef LEWITT_HAVE_ALSA
#include <alsa/asoundlib.h>

#include "desktop/AlsaDesktopMixer.hpp"
#endif

namespace lewitt {

bool softwareInputMuteConfigured(const std::string& configHome) {
    if (configHome.empty()) return false;
    namespace fs = std::filesystem;
    const fs::path h(configHome);
    std::error_code ec;
    return fs::is_regular_file(h / "wireplumber/wireplumber.conf.d/52-openwitty-connect2-input.conf", ec) &&
           fs::is_regular_file(h / "alsa-card-profile/mixer/profile-sets/openwitty-connect2.conf", ec) &&
           fs::is_regular_file(h / "alsa-card-profile/mixer/paths/openwitty-connect2-input.conf", ec) &&
           fs::is_regular_file(h / "alsa-card-profile/mixer/paths/openwitty-connect2-output.conf", ec);
}

std::string userConfigHome() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] == '/') return xdg;
    const char* home = std::getenv("HOME");
    if (home && home[0] == '/') return std::string(home) + "/.config";
    return {};
}


namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}
}  // namespace

bool modprobeTextConfiguresQuirk(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = lower(line.substr(0, line.find('#')));
        std::istringstream w(line);
        std::string kw, mod;
        if (!(w >> kw >> mod) || kw != "options" || (mod != "snd_usb_audio" && mod != "snd-usb-audio")) continue;
        std::string opt;
        while (w >> opt)
            if (opt.rfind("quirk_flags=", 0) == 0 && opt.find("29c2:0004:") != std::string::npos &&
                opt.find("ignore_ctl_error") != std::string::npos)
                return true;
    }
    return false;
}

bool quirkFlagsParamActive(const std::string& value) {
    const std::string v = lower(value);
    std::istringstream in(v);
    std::string entry;
    while (std::getline(in, entry, ','))
        if (entry.find("29c2:0004:") != std::string::npos && entry.find("ignore_ctl_error") != std::string::npos)
            return true;
    return false;
}

std::vector<std::string> audioSetupFindings(const AudioSetupStatus& s) {
    std::vector<std::string> f;
    if (!s.card) {
        f.push_back("The CONNECT 2 is not connected as a sound card (unplugged, or its Linux driver is not active).");
        return f;
    }
    if (s.mixerLoadError) {
        f.push_back(s.quirkActive
            ? "The CONNECT 2's volume controls still fail to load although the kernel fix is active."
            : "The CONNECT 2's volume controls fail to load, so the desktop volume cannot control it. On Linux 7.3 "
              "a faulty microphone control on the device causes this; the kernel fix below (ignore_ctl_error) "
              "works around it.");
        if (s.quirkConfigured && !s.quirkActive)
            f.push_back("The kernel fix is installed but not active yet: replug the CONNECT 2 or reboot.");
    }
    if (!s.mixerLoadError && !s.volumeControl)
        f.push_back("The CONNECT 2 has no usable volume control, so the desktop volume cannot control it.");
    if (s.quirkActive && !s.quirkConfigured)
        f.push_back("The kernel fix is active now but not installed permanently; it is gone after the next reboot.");
    return f;
}

#ifdef LEWITT_HAVE_ALSA
AudioSetupStatus collectAudioSetup() {
    namespace fs = std::filesystem;
    AudioSetupStatus s;
    for (const char* dir : {"/etc/modprobe.d", "/run/modprobe.d", "/usr/local/lib/modprobe.d", "/usr/lib/modprobe.d",
                            "/lib/modprobe.d"}) {
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (e.path().extension() != ".conf") continue;
            std::ifstream in(e.path());
            std::stringstream b;
            b << in.rdbuf();
            if (modprobeTextConfiguresQuirk(b.str())) {
                s.quirkConfigured = true;
                s.quirkFiles.push_back(e.path().string());
            }
        }
    }
    {
        std::ifstream p("/sys/module/snd_usb_audio/parameters/quirk_flags");
        s.moduleLoaded = static_cast<bool>(p);
        std::string v;
        std::getline(p, v);
        s.quirkActive = quirkFlagsParamActive(v);
    }
    s.card = findCardByUsbId("/proc/asound", "29c2:0004");
    if (s.card) {
        snd_mixer_t* m = nullptr;
        const std::string name = "hw:" + std::to_string(*s.card);
        int e = snd_mixer_open(&m, 0);
        if (e == 0) e = snd_mixer_attach(m, name.c_str());
        if (e == 0) e = snd_mixer_selem_register(m, nullptr, nullptr);
        if (e == 0) e = snd_mixer_load(m);
        if (e < 0) s.mixerLoadError = e;
        if (m) snd_mixer_close(m);
        AlsaDesktopMixer mixer;
        s.volumeControl = static_cast<bool>(mixer.open());
    }
    return s;
}
#endif

}  // namespace lewitt
