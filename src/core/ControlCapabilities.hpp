// Phase 10A: which GUI control may change the device under the session's write policy. Every control is mapped
// to exactly the settings bytes it writes (or the trigger sequence it sends); a control is editable only if ALL
// of them are authorized. Simulation: everything is editable (unchanged mock behaviour). Real hardware: only
// what WritePolicy::hardwareWriteOffsets / hardwareTriggers name - e.g. the confirmed "speaker/headphone
// control" authorizes the four output controls and nothing else. Qt-free; this module grants nothing itself.
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "core/DeviceSession.hpp"

namespace lewitt {

struct ControlFootprint {
    std::vector<std::size_t> settingsOffsets;   // bytes of the 128-byte settings message it changes
    std::vector<Trigger> triggers;               // trigger words it sends, in order
    bool simulationOnly{false};                  // never on real hardware in this build (e.g. device restarts)
};

// nullptr for an unknown key (never editable). Keys are the GUI's setValue()/trigger() names.
const ControlFootprint* controlFootprint(std::string_view key);
// All known keys (GUI controls and the workflow pseudo-keys "autosetup").
const std::vector<std::string>& controlKeys();
// May `key` change the device of a session with `policy` (`simulated` = the session's transport is simulated)?
bool controlWritable(std::string_view key, const WritePolicy& policy, bool simulated);

}  // namespace lewitt
