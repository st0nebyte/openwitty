// Cooperative, per-user ownership of the CONNECT 2 control interface.
// The kernel still arbitrates usbfs claims; this only coordinates OpenWitty processes.
#pragma once
#include <string>

#include "protocol/Types.hpp"

namespace lewitt {

class ControlLease {
public:
    ControlLease(const ControlLease&) = delete;
    ControlLease& operator=(const ControlLease&) = delete;
    ControlLease(ControlLease&& other) noexcept;
    ControlLease& operator=(ControlLease&& other) noexcept;
    ~ControlLease();

    // Directory must be private to the current user. A pre-existing lock *file* is harmless:
    // flock belongs to the open fd and is released automatically on close/process exit.
    static Result<ControlLease> acquireInDirectory(const std::string& directory);
    static Result<ControlLease> acquireForCurrentSession();

private:
    explicit ControlLease(int fd) : fd_(fd) {}
    int fd_{-1};
};

}  // namespace lewitt
