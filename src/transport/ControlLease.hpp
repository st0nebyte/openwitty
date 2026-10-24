// Cooperative, per-user ownership of the CONNECT 2 control interface.
// The kernel still arbitrates usbfs claims; this only coordinates OpenWitty processes.
#pragma once
#include <chrono>
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
    static std::string currentSessionDirectory();

    // Cooperative hand-over between OpenWitty processes (Phase 8D). A client that finds the lease
    // taken (e.g. the GUI while openwitty-volumed holds it) leaves a yield request; a long-running
    // holder checks yieldRequested() regularly, releases the device and stays away while the
    // request is fresh. Requests older than maxAge are stale (crashed requester) and ignored.
    // Same private-directory rules as the lock file; never follows symlinks.
    static Status requestYieldInDirectory(const std::string& directory);
    static bool yieldRequestedInDirectory(const std::string& directory, std::chrono::seconds maxAge);
    static void clearYieldRequestInDirectory(const std::string& directory);

private:
    explicit ControlLease(int fd) : fd_(fd) {}
    int fd_{-1};
};

}  // namespace lewitt
