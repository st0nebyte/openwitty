// Phase 10B: the CONNECT 2's actual audio sample rate, read-only, from the kernel's view of the device's own PCMs
// (/proc/asound/cardN/pcm0p|pcm0c/sub0/hw_params, cross-checked with stream0). The card is found by its USB id,
// never as "the default output". PipeWire's graph rate is NOT used: it can differ from the device rate, and an idle
// (suspended) device has no rate at all. When no stream is open the rate is unknown - never a guessed 48 kHz.
// Reads plain files only; no USB, ALSA control or PipeWire access.
#pragma once
#include <optional>
#include <string>

namespace lewitt {

struct DeviceSampleRate {
    std::optional<unsigned> hz;   // set only when the kernel reports it for an open CONNECT 2 PCM
    std::string reason;           // why it is unknown (or where it came from)
};

// "rate: 48000 (48000/1)" in an open PCM's hw_params; nullopt for "closed" or anything unparsable.
std::optional<unsigned> parseHwParamsRate(const std::string& hwParamsText);
// One direction of /proc/asound/cardN/stream0 ("Playback:" or "Capture:"): the "Momentary freq" while Running.
std::optional<unsigned> parseStreamMomentaryRate(const std::string& stream0Text, const std::string& direction);

DeviceSampleRate readConnect2SampleRate(const std::string& procAsoundRoot = "/proc/asound",
                                        const std::string& usbId = "29c2:0004");

}  // namespace lewitt
