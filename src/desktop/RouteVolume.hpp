// Phase 9C proof of concept - NOT used by the application. If OpenWitty ever mirrors the knob through the
// CONNECT 2's PipeWire route (so WirePlumber persists the level, "save = true") instead of the raw ALSA
// control, the requested route volume must land exactly on the hardware step the device already has:
// PipeWire's ALSA layer rounds playback volumes UP to the next hardware step (acp/alsa-mixer.c,
// rounding = +1; "nearest" with deferred volume), so a value a hair above the step - float noise in the
// linear <-> dB conversion is enough - would make the speaker one step (0.5 dB) louder than the knob.
#pragma once
#include <optional>

#include "core/VolumeSync.hpp"

namespace lewitt {

struct VolumeGrid {           // the ALSA control's raw range and its dB range (linear TLV)
    long rawMin{0}, rawMax{120};
    double dbMin{-60.0}, dbMax{0.0};
    double stepDb() const { return (dbMax - dbMin) / static_cast<double>(rawMax - rawMin); }
};

struct RouteVolumeRequest {
    float linear{0};               // SPA_PROP_channelVolumes value (linear amplitude), all channels
    std::optional<bool> mute;      // only ever `true`: a route request never unmutes
    double stepDb{0};              // the hardware step the request is meant to land on
};

// The hardware step at or below `db` (never above).
double gridStepAtOrBelow(double db, const VolumeGrid& g);

// What to send to the route so the desktop shows the device level: nothing when the desktop already shows
// it; never louder than the device; never an unmute (a muted desktop stays muted, as in alignDesktop()).
std::optional<RouteVolumeRequest> planRouteMirror(double deviceDb, bool deviceMuted,
                                                  const std::optional<DesktopLevel>& desktop, const VolumeGrid& g,
                                                  double toleranceDb = 0.25);

}  // namespace lewitt
