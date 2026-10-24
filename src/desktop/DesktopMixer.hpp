// The desktop side of the speaker volume: the CONNECT 2's ALSA "PCM Playback" control, which
// PipeWire (and through it KDE) moves when the hardware-volume route is active. Abstract so the
// synchronization can be tested without ALSA.
#pragma once
#include <optional>
#include <string>

#include "core/VolumeSync.hpp"
#include "protocol/Types.hpp"

namespace lewitt {

class IDesktopMixer {
public:
    virtual ~IDesktopMixer() = default;
    // Find the CONNECT 2 card (by USB id, never by card number) and open its control device.
    virtual Status open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual int cardIndex() const = 0;               // -1 when closed
    virtual std::optional<DesktopLevel> read() = 0;  // nullopt: control missing / card gone
    virtual Status write(const DesktopLevel& d) = 0;
    // Wait up to timeoutMs for control events and drain them.
    //   1 = at least one event for a watched control, 0 = timeout / unrelated events, -1 = card gone.
    virtual int waitForEvent(int timeoutMs) = 0;
};

// Pure helpers (unit-tested): the CONNECT 2 card index from /proc/asound/cardN/usbid, and the
// linear mapping between ALSA raw steps and dB for a control with a linear dB range.
std::optional<int> findCardByUsbId(const std::string& procAsoundRoot, const std::string& usbId);
double rawToDb(long raw, long rawMin, long rawMax, double dbMin, double dbMax);
long dbToRaw(double db, long rawMin, long rawMax, double dbMin, double dbMax);

}  // namespace lewitt
