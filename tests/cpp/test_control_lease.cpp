#include <gtest/gtest.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "core/RetryPolicy.hpp"
#include "transport/ControlLease.hpp"
#include "transport/UsbTransport.hpp"

using namespace lewitt;

namespace {
struct PrivateDirectory {
    char path[64]{"/tmp/openwitty-lease-test-XXXXXX"};
    PrivateDirectory() { EXPECT_NE(mkdtemp(path), nullptr); }
    ~PrivateDirectory() {
        const std::string file = std::string(path) + "/openwitty-connect2-control.lock";
        unlink(file.c_str());
        rmdir(path);
    }
};
}  // namespace

TEST(ControlLease, OnlyOneOpenWittyOwnerAndReleaseOnClose) {
    PrivateDirectory dir;
    auto first = ControlLease::acquireInDirectory(dir.path);
    ASSERT_TRUE(first.ok());
    auto second = ControlLease::acquireInDirectory(dir.path);
    ASSERT_FALSE(second.ok());
    EXPECT_EQ(second.error().code, Errc::ControlInUse);
    std::optional<ControlLease> held(std::move(first.value()));
    held.reset();
    EXPECT_TRUE(ControlLease::acquireInDirectory(dir.path).ok());
    EXPECT_FALSE(isPersistentOpenError(Errc::ControlInUse));
    EXPECT_TRUE(shouldRetryOpen(Errc::ControlInUse, false));
}

TEST(ControlLease, ProcessExitReleasesLeaseDespiteExistingFile) {
    PrivateDirectory dir;
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        auto lease = ControlLease::acquireInDirectory(dir.path);
        _exit(lease.ok() ? 0 : 1);  // kernel closes the fd even without C++ destruction
    }
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);
    EXPECT_TRUE(ControlLease::acquireInDirectory(dir.path).ok());
}

TEST(ControlLease, SeparateProcessIsRefusedWithoutOpeningUsb) {
    PrivateDirectory dir;
    auto held = ControlLease::acquireInDirectory(dir.path);
    ASSERT_TRUE(held.ok());
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        auto contender = ControlLease::acquireInDirectory(dir.path);
        _exit(!contender && contender.error().code == Errc::ControlInUse ? 0 : 1);
    }
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ControlLease, RejectsNonPrivateDirectory) {
    PrivateDirectory dir;
    ASSERT_EQ(chmod(dir.path, 0755), 0);
    auto lease = ControlLease::acquireInDirectory(dir.path);
    EXPECT_FALSE(lease.ok());
    if (!lease) EXPECT_EQ(lease.error().code, Errc::AccessDenied);
    ASSERT_EQ(chmod(dir.path, 0700), 0);
}

#ifdef LEWITT_HAVE_USB
TEST(ControlLease, UsbTransportFailsBeforeUsbIoWhenSessionLeaseHeld) {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime || !*runtime) GTEST_SKIP() << "no private runtime directory";
    auto held = ControlLease::acquireForCurrentSession();
    // A live GUI may already own this user's lease; either it or this test can hold it.
    if (!held) ASSERT_EQ(held.error().code, Errc::ControlInUse);
    UsbTransport usb(readOnlyHardwareOptions());
    auto opened = usb.open();
    ASSERT_FALSE(opened.ok());
    EXPECT_EQ(opened.error().code, Errc::ControlInUse);
    EXPECT_FALSE(usb.isOpen());
}
#endif
