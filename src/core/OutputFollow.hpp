// Phase 12C: "Follow active output" (owner spec, docs/FEATURE_COMPLETION_ROADMAP.md Phase 12). The CONNECT 2 routes the
// native (kernel/desktop) volume to the output byte 59 names (0 none, 1 speakers, 2 headphones). OpenWitty keeps that
// mapping on the audible output, decided from the device's own speaker/headphone mutes - never from the desktop's mute.
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "core/DeviceSession.hpp"

namespace lewitt {

enum class FollowChoice { Speakers, Headphones, Keep, Disable };

struct FollowConfig {
    FollowChoice bothOn{FollowChoice::Speakers};   // both outputs unmuted: Speakers | Headphones | Keep
    FollowChoice bothMuted{FollowChoice::Keep};    // both muted: Keep | Speakers | Headphones | Disable (mapping none)
    bool moveFocus{true};                          // also move the touch-wheel focus (byte 56) to that output
};

// The byte-59 value OpenWitty wants. `current` is byte 59 now; `lastActive` the last speakers/headphones value (1/2)
// seen, used by Keep when the current value is none.
std::uint8_t followMapping(bool speakerMuted, bool headphoneMuted, std::uint8_t current, std::uint8_t lastActive,
                           const FollowConfig& c);

// Applies followMapping() to a session after each fresh settings read: at most one verified SET of byte 59 per state,
// never retried (a failed change is only tried again after the device state changed).
class OutputFollower {
public:
    using Logger = std::function<void(const std::string&)>;
    OutputFollower(DeviceSession& session, FollowConfig cfg, PollPolicy verify, Logger log = {});
    // Call right after a successful settings refresh. Returns true when a SET was sent.
    bool step();
    std::uint8_t lastActive() const { return lastActive_; }
    int writes() const { return writes_; }

private:
    DeviceSession& session_;
    FollowConfig cfg_;
    PollPolicy verify_;
    Logger log_;
    std::uint8_t lastActive_{1};
    std::optional<std::uint32_t> failedState_;   // device state whose change failed (no retry for it)
    bool focusUnsupported_{false};               // the device did not keep a focus write: stop trying
    void moveFocus(std::uint8_t mapping);
    int writes_{0};
};

}  // namespace lewitt
