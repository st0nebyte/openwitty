// Optional higher/lower sample rates for apps (SR plan): PipeWire runs its graph at the rates listed in
// default.clock.allowed-rates (here [ 48000 ] by default), so the CONNECT 2 stays at 48 kHz and a 44.1 or 96 kHz app is
// resampled. This writes a small user config that allows the device's three rates; PipeWire then switches the graph
// (and the device) to an app's rate when nothing else forces another one. It applies to all sound cards, it changes
// nothing on the device, and it takes effect after restarting desktop audio.
#pragma once
#include <optional>
#include <string>
#include <vector>

namespace lewitt {

// File name inside $XDG_CONFIG_HOME/pipewire/pipewire.conf.d/.
inline constexpr const char* kSampleRatesFile = "54-openwitty-sample-rates.conf";

// The rates the CONNECT 2 supports (USB descriptor: 44.1, 48, 96 kHz).
const std::vector<int>& connect2SampleRates();

// The config text OpenWitty writes.
std::string renderSampleRatesConfig();

// Whether `text` is a config written by renderSampleRatesConfig() (so OpenWitty may remove it).
bool isOpenWittySampleRatesConfig(const std::string& text);

// Path of the file below `configHome` (empty if configHome is empty).
std::string sampleRatesPath(const std::string& configHome);

// The rates in `pw-metadata -n settings 0 clock.allowed-rates` output (value '[ 44100 48000 ]'); nullopt if absent.
std::optional<std::vector<int>> parseAllowedRates(const std::string& pwMetadataOutput);

}  // namespace lewitt
