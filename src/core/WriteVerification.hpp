// Write-state machine for settings commits on real hardware.
//
// A successful USB transfer (commit() returning ok) is NOT proof the device's internal state
// actually reflects the write yet: Phase 6D (docs/HARDWARE_TEST_RESULTS.md) found a real CONNECT 2
// can answer a GET sent immediately after a SET with the pre-write value, while a GET a short time
// later (empirically ~1ms was already enough, far under the ~20ms first poll default below) returns
// the correct value. This header turns "the write ioctl didn't error" into a distinct, more
// conservative claim: "a subsequent read confirms the expected bytes changed and nothing else did."
#pragma once
#include <chrono>
#include <cstddef>
#include <functional>
#include <vector>

#include "protocol/Types.hpp"

namespace lewitt {

enum class WriteState {
    Prepared,              // edit staged (DeviceSession::edit), nothing sent yet
    Submitted,              // about to call the existing commit() path
    TransferCompleted,      // the USB write itself returned ok; device state NOT yet confirmed
    AwaitingConvergence,    // transfer ok, first readback didn't match yet; bounded polling (reads only)
    Verified,               // a read confirms exactly the expected bytes changed, nothing else
    VerificationTimedOut,   // polling budget exhausted without convergence; SET was never retried
    Ambiguous,               // a byte outside the intended diff changed during verification (e.g. a
                             // concurrent device-side edit); not auto-resolved, caller must decide
    Failed,                 // the USB transfer itself failed, or a read during verification failed
};

const char* writeStateName(WriteState s);

// Bounded, read-only convergence polling. Never retries the SET itself.
struct PollPolicy {
    // Delays between successive convergence-check reads, in order; total ~570ms by default.
    // Grounded in Phase 6D: LED brightness converged within ~1ms on real hardware, so this default
    // is deliberately more generous than observed, to leave margin for fields not yet timed.
    std::vector<std::chrono::milliseconds> delays{
        std::chrono::milliseconds(20),  std::chrono::milliseconds(50), std::chrono::milliseconds(100),
        std::chrono::milliseconds(200), std::chrono::milliseconds(200)};
    // Injectable for tests (default: a real sleep). Tests pass a no-op or counting stub so
    // convergence-state-machine behaviour can be exercised without wall-clock delay.
    std::function<void(std::chrono::milliseconds)> sleeper;
    // Settings SETs carry the whole 128-byte struct and are NOT atomic relative to physical
    // actions on the device (a hardware button press between our last read and the SET would be
    // silently reverted for every byte we resend). When true, commitAndVerify() reads the settings
    // twice back-to-back before writing and refuses to send anything if the two snapshots differ.
    // This narrows the race window; it cannot close it. Off by default (adds one read).
    bool requireStableSnapshot{false};
};

struct WriteResult {
    WriteState state{WriteState::Prepared};
    int pollAttempts{0};
    // Populated only when state == Ambiguous: settings-struct offsets that changed but were not
    // part of the intended write.
    std::vector<std::size_t> unexpectedOffsets;
    // Populated only when state == Failed.
    Error error;
};

}  // namespace lewitt
