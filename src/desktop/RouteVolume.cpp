#include "desktop/RouteVolume.hpp"

#include <algorithm>
#include <cmath>

#include "desktop/DesktopMixer.hpp"

namespace lewitt {

double gridStepAtOrBelow(double db, const VolumeGrid& g) {
    return rawToDb(dbToRaw(db, g.rawMin, g.rawMax, g.dbMin, g.dbMax, /*roundDown=*/true), g.rawMin, g.rawMax, g.dbMin,
                   g.dbMax);
}

std::optional<RouteVolumeRequest> planRouteMirror(double deviceDb, bool deviceMuted,
                                                  const std::optional<DesktopLevel>& desktop, const VolumeGrid& g,
                                                  double toleranceDb) {
    if (!std::isfinite(deviceDb)) return std::nullopt;
    const double step = gridStepAtOrBelow(deviceDb, g);
    const bool muteNeeded = deviceMuted && !(desktop && desktop->muted);
    if (desktop && std::fabs(desktop->db - step) <= toleranceDb && !muteNeeded) return std::nullopt;
    // Aim a tenth of a step below the target step: rounding up lands exactly on it, rounding to nearest too,
    // and float noise can no longer reach the step above. (PipeWire makes up the 0.05 dB with its software
    // volume, in the quieter direction.)
    const double aimDb = step - g.stepDb() / 10.0;
    RouteVolumeRequest r;
    r.linear = static_cast<float>(std::pow(10.0, aimDb / 20.0));
    r.stepDb = step;
    if (muteNeeded) r.mute = true;
    return r;
}

}  // namespace lewitt
