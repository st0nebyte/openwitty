#include <gtest/gtest.h>

#include <cstdlib>
#include <optional>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "core/RetryPolicy.hpp"
#include "transport/ControlLease.hpp"
#include "transport/UsbDiagnostics.hpp"
#include "transport/UsbTransport.hpp"

using namespace lewitt;

namespace {
struct PrivateDirectory {
    char path[64]{"/tmp/openwitty-lease-test-XXXXXX"};
    PrivateDirectory() { EXPECT_NE(mkdtemp(path), nullptr); }
    ~PrivateDirectory() {
        const std::string file = std::string(path) + "/openwitty-connect2-control.lock";
        unlink(file.c_str());
        unlink((std::string(path) + "/openwitty-connect2-yield.request").c_str());
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
    const auto message = suggestionFor(makeError(Errc::ControlInUse, "held"));
    EXPECT_NE(message.find("OpenWitty"), std::string::npos);
    EXPECT_NE(message.find("not a USB permission problem"), std::string::npos);
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

// Phase 8D: cooperative hand-over (GUI asks openwitty-volumed to release the device).
TEST(ControlLease, YieldRequestIsFreshPrivateAndClearable) {
    PrivateDirectory dir;
    EXPECT_FALSE(ControlLease::yieldRequestedInDirectory(dir.path, std::chrono::seconds(30)));
    ASSERT_TRUE(ControlLease::requestYieldInDirectory(dir.path).ok());
    EXPECT_TRUE(ControlLease::yieldRequestedInDirectory(dir.path, std::chrono::seconds(30)));
    struct stat st{};
    ASSERT_EQ(stat((std::string(dir.path) + "/openwitty-connect2-yield.request").c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 0777, 0600u);
    ControlLease::clearYieldRequestInDirectory(dir.path);
    EXPECT_FALSE(ControlLease::yieldRequestedInDirectory(dir.path, std::chrono::seconds(30)));
}

TEST(ControlLease, StaleYieldRequestIsIgnored) {
    PrivateDirectory dir;
    ASSERT_TRUE(ControlLease::requestYieldInDirectory(dir.path).ok());
    const std::string f = std::string(dir.path) + "/openwitty-connect2-yield.request";
    struct timespec old[2]{};
    old[0].tv_sec = old[1].tv_sec = time(nullptr) - 120;
    ASSERT_EQ(utimensat(AT_FDCWD, f.c_str(), old, 0), 0);
    EXPECT_FALSE(ControlLease::yieldRequestedInDirectory(dir.path, std::chrono::seconds(30)));   // crashed requester
    ASSERT_TRUE(ControlLease::requestYieldInDirectory(dir.path).ok());                          // refreshed again
    EXPECT_TRUE(ControlLease::yieldRequestedInDirectory(dir.path, std::chrono::seconds(30)));
}

TEST(ControlLease, YieldRequestRefusesSymlinksAndSharedDirectories) {
    PrivateDirectory dir;
    const std::string f = std::string(dir.path) + "/openwitty-connect2-yield.request";
    ASSERT_EQ(symlink("/etc/hostname", f.c_str()), 0);
    EXPECT_FALSE(ControlLease::requestYieldInDirectory(dir.path).ok());
    EXPECT_FALSE(ControlLease::yieldRequestedInDirectory(dir.path, std::chrono::seconds(30)));
    unlink(f.c_str());
    ASSERT_EQ(chmod(dir.path, 0755), 0);
    EXPECT_FALSE(ControlLease::requestYieldInDirectory(dir.path).ok());
    EXPECT_FALSE(ControlLease::yieldRequestedInDirectory(dir.path, std::chrono::seconds(30)));
    chmod(dir.path, 0700);
}
