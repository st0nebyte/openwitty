// When is it worth opening the device again? Qt-free, pure.
#pragma once
#include "protocol/Types.hpp"

namespace lewitt {

// Failures that will not fix themselves by trying again unchanged: a permission problem, a
// kernel-owned interface, an unsupported/ambiguous descriptor layout, "no such interface".
// They become worth retrying only after something observable changed (device list or node
// permissions - e.g. the user installed the udev rule and replugged).
constexpr bool isPersistentOpenError(Errc c) {
    return c == Errc::AccessDenied || c == Errc::Busy || c == Errc::NotSupported || c == Errc::Malformed ||
           c == Errc::NotFound;
}

constexpr bool shouldRetryOpen(Errc lastError, bool deviceListChanged) {
    return !isPersistentOpenError(lastError) || deviceListChanged;
}

}  // namespace lewitt
