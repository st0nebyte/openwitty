#include "ControlLease.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace lewitt {
namespace {
constexpr const char* kLockName = "openwitty-connect2-control.lock";

Error leaseError(Errc code, const char* action) {
    return makeError(code, std::string("control lease: ") + action + ": " + std::strerror(errno));
}
}  // namespace

ControlLease::ControlLease(ControlLease&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

ControlLease& ControlLease::operator=(ControlLease&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) close(fd_);
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

ControlLease::~ControlLease() { if (fd_ >= 0) close(fd_); }

Result<ControlLease> ControlLease::acquireInDirectory(const std::string& directory) {
    const int dir = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dir < 0) return leaseError(Errc::AccessDenied, "open private runtime directory");
    struct stat ds{};
    if (fstat(dir, &ds) != 0 || !S_ISDIR(ds.st_mode) || ds.st_uid != geteuid() || (ds.st_mode & 0077)) {
        close(dir);
        return makeError(Errc::AccessDenied,
                         "control lease: runtime directory must be owned by this user and private (0700)");
    }
    const int fd = openat(dir, kLockName, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
    close(dir);
    if (fd < 0) return leaseError(Errc::AccessDenied, "open lock file");
    struct stat fs{};
    if (fstat(fd, &fs) != 0 || !S_ISREG(fs.st_mode) || fs.st_uid != geteuid() || (fs.st_mode & 0077)) {
        close(fd);
        return makeError(Errc::AccessDenied,
                         "control lease: lock file must be a private regular file owned by this user");
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int saved = errno;
        close(fd);
        if (saved == EWOULDBLOCK || saved == EAGAIN)
            return makeError(Errc::ControlInUse,
                             "CONNECT 2 control is already in use by another OpenWitty process; "
                             "close that client or wait for it to exit");
        errno = saved;
        return leaseError(Errc::Io, "acquire lock");
    }
    return ControlLease(fd);
}

Result<ControlLease> ControlLease::acquireForCurrentSession() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    const std::string directory = runtime && *runtime ? runtime
                                  : "/run/user/" + std::to_string(geteuid());
    return acquireInDirectory(directory);
}

}  // namespace lewitt
