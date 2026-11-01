// Readable channel names for the CONNECT 2's PipeWire ports (owner wish, 2026-11-01). PipeWire names the ports after
// channel positions (capture_FL ... capture_RR); audio apps show a JACK "pretty name" instead when one is set. These
// labels live in PipeWire's "default" metadata (http://jackaudio.org/metadata/pretty-name) and are display-only: no
// device access, no audio routing change. They vanish when the ports are recreated (replug, PipeWire restart), so the
// background service sets them again whenever they are missing.
//
// 4-channel capture (verified order, LB-1/LB-2; positions FL FR RL RR): 1 Microphone, 2 Instrument, 3 Loopback L,
// 4 Loopback R. Stereo playback: Output L, Output R.
#pragma once
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lewitt {

inline constexpr const char* kPrettyNameKey = "http://jackaudio.org/metadata/pretty-name";

struct PwObject {
    int id{-1};
    std::map<std::string, std::string> props;
};
// Objects of a `pw-cli ls Node` / `pw-cli ls Port` listing.
std::vector<PwObject> parsePwCliList(const std::string& text);
// Pretty names set in `pw-metadata` (default metadata) output: object id -> name.
std::map<int, std::string> parsePrettyNames(const std::string& text);

struct PortName {
    int portId;
    std::string name;
};
// The names the CONNECT 2's ports should carry. Only the capture node with exactly the ports FL FR RL RR and the
// playback node with FL FR are named (another profile is left alone).
std::vector<PortName> wantedPortNames(const std::vector<PwObject>& nodes, const std::vector<PwObject>& ports);
// The wanted names that are not set yet (or set to something else).
std::vector<PortName> missingPortNames(const std::vector<PortName>& wanted, const std::map<int, std::string>& current);

// Runs argv (no shell); returns the exit status (-1 if it could not run) and the standard output in `out`.
using CommandRunner = std::function<int(const std::vector<std::string>& argv, std::string* out)>;
int runProgram(const std::vector<std::string>& argv, std::string* out);

// Reads PipeWire's state and sets the missing names. Returns how many were set; -1 if PipeWire could not be read.
int applyPortNames(const CommandRunner& run = runProgram);
// Removes the names OpenWitty set (only where the port still carries exactly OpenWitty's name). Returns how many.
int clearPortNames(const CommandRunner& run = runProgram);

// PipeWire's clock rate from `pw-metadata -n settings 0 clock.rate` output; nullopt if absent or implausible.
std::optional<unsigned> parseClockRate(const std::string& text);

}  // namespace lewitt
