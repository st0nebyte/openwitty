#include "core/OutputFollow.hpp"

namespace lewitt {

namespace {
std::uint8_t choose(FollowChoice c, std::uint8_t current, std::uint8_t lastActive) {
    switch (c) {
        case FollowChoice::Speakers: return 1;
        case FollowChoice::Headphones: return 2;
        case FollowChoice::Disable: return 0;
        case FollowChoice::Keep: break;
    }
    return (current == 1 || current == 2) ? current : (lastActive == 2 ? 2 : 1);
}
const char* name(std::uint8_t v) { return v == 1 ? "speakers" : v == 2 ? "headphones" : "none"; }
}  // namespace

std::string deviceKey(const std::string& serial) {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char ch : serial) { h ^= ch; h *= 1099511628211ULL; }
    static const char* hex = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) { out[static_cast<std::size_t>(i)] = hex[h & 0xF]; h >>= 4; }
    return out;
}

std::optional<std::uint8_t> buttonFollowBase(const UserConfig& c, const std::string& key) {
    for (const auto& e : c.buttonFollow)
        if (e.size() == 18 && e.compare(0, 16, key) == 0) return static_cast<std::uint8_t>(e[17] - '0');
    return std::nullopt;
}

bool followActive(const UserConfig& c, const std::string& key, std::uint8_t nativeButton) {
    if (c.followActiveOutput) return true;
    const auto b = buttonFollowBase(c, key);
    return b && *b == nativeButton;
}

UserConfig withButtonFollow(UserConfig c, const std::string& key, std::optional<std::uint8_t> base) {
    std::vector<std::string> kept;
    for (const auto& e : c.buttonFollow)
        if (!(e.size() == 18 && e.compare(0, 16, key) == 0)) kept.push_back(e);
    if (base && (*base == 1 || *base == 2)) kept.push_back(key + ":" + std::to_string(*base));
    c.buttonFollow = kept;
    return c;
}

std::uint8_t followMapping(bool speakerMuted, bool headphoneMuted, std::uint8_t current, std::uint8_t lastActive,
                           const FollowConfig& c) {
    if (!speakerMuted && headphoneMuted) return 1;
    if (speakerMuted && !headphoneMuted) return 2;
    if (!speakerMuted && !headphoneMuted)
        return choose(c.bothOn == FollowChoice::Disable ? FollowChoice::Speakers : c.bothOn, current, lastActive);
    return choose(c.bothMuted, current, lastActive);
}

OutputFollower::OutputFollower(DeviceSession& session, FollowConfig cfg, PollPolicy verify, Logger log)
    : session_(session), cfg_(cfg), verify_(verify), log_(std::move(log)) {}

bool OutputFollower::step() {
    const auto& s = session_.settings();
    if (!s || session_.state() != ConnectionState::Connected) return false;
    const std::uint8_t current = s->nativeVolumeMapping();
    if (current == 1 || current == 2) lastActive_ = current;
    const bool spk = s->speakerMute(), hp = s->headphoneMute();
    const std::uint8_t want = followMapping(spk, hp, current, lastActive_, cfg_);
    const std::uint32_t state = (spk ? 1u : 0u) | (hp ? 2u : 0u) | (std::uint32_t(current) << 2) |
                                (std::uint32_t(want) << 10);
    if (want == current) { failedState_.reset(); return false; }
    if (failedState_ == state) return false;                    // no retry for the same situation
    if (session_.pendingSettings()) return false;               // never piggyback on another unconfirmed edit
    auto e = session_.edit([&](SettingsMessage& m) { return m.setNativeVolumeMapping(want); });
    if (!e) {
        if (log_) log_(std::string("follow active output: not staged: ") + e.error().message);
        failedState_ = state;
        return false;
    }
    WriteResult r = session_.commitAndVerify(verify_);
    if (r.state != WriteState::Verified) session_.discardPending();
    ++writes_;
    if (log_) log_(std::string("follow active output: volume target ") + name(current) + " -> " + name(want) + ": " +
                   writeStateName(r.state));
    if (r.state == WriteState::Verified) {
        failedState_.reset();
        lastActive_ = want ? want : lastActive_;
        if (cfg_.moveFocus && want != 0) moveFocus(want);
    } else {
        failedState_ = state;
    }
    return true;
}

// Its own SET after the verified mapping change: a device that does not keep it cannot spoil that change.
void OutputFollower::moveFocus(std::uint8_t mapping) {
    if (focusUnsupported_ || session_.pendingSettings() || !session_.settings()) return;
    const std::uint8_t focus = mapping == 1 ? 3 : 4;
    if (session_.settings()->selectedChannelRaw() == focus) return;
    auto e = session_.edit([&](SettingsMessage& m) { return m.setSelectedChannelRaw(focus); });
    if (!e) {
        if (log_) log_("follow active output: focus not staged: " + e.error().message);
        focusUnsupported_ = true;
        return;
    }
    WriteResult r = session_.commitAndVerify(verify_);
    if (r.state != WriteState::Verified) {
        session_.discardPending();
        focusUnsupported_ = true;    // logged once, not retried
    }
    ++writes_;
    if (log_) log_(std::string("follow active output: touch focus -> ") + (focus == 3 ? "speakers" : "headphones") + ": " +
                   writeStateName(r.state) + (r.state == WriteState::Verified ? "" : " (not tried again)"));
}

}  // namespace lewitt
