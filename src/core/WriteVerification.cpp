#include "WriteVerification.hpp"

namespace lewitt {

const char* writeStateName(WriteState s) {
    switch (s) {
        case WriteState::Prepared: return "prepared";
        case WriteState::Submitted: return "submitted";
        case WriteState::TransferCompleted: return "transfer-completed";
        case WriteState::AwaitingConvergence: return "awaiting-convergence";
        case WriteState::Verified: return "verified";
        case WriteState::VerificationTimedOut: return "verification-timed-out";
        case WriteState::Ambiguous: return "ambiguous";
        case WriteState::Failed: return "failed";
    }
    return "unknown";
}

}  // namespace lewitt
