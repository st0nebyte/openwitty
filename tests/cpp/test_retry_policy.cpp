// Phase 7A: persistent open errors are not retried until something observable changes.
#include <gtest/gtest.h>

#include "core/RetryPolicy.hpp"

using namespace lewitt;

TEST(RetryPolicy, PersistentErrorsAreNotRetriedWhileNothingChanged) {
    for (Errc c : {Errc::AccessDenied, Errc::Busy, Errc::NotSupported, Errc::Malformed, Errc::NotFound})
        EXPECT_FALSE(shouldRetryOpen(c, /*deviceListChanged=*/false)) << errcName(c);
}

TEST(RetryPolicy, PersistentErrorsAreRetriedOnceTheDeviceListOrPermissionsChange) {
    for (Errc c : {Errc::AccessDenied, Errc::Busy, Errc::NotSupported, Errc::Malformed, Errc::NotFound})
        EXPECT_TRUE(shouldRetryOpen(c, true)) << errcName(c);
}

TEST(RetryPolicy, TransientErrorsKeepBeingRetriedWithBackoff) {
    for (Errc c : {Errc::Timeout, Errc::Io, Errc::ShortTransfer, Errc::Disconnected, Errc::Ok})
        EXPECT_TRUE(shouldRetryOpen(c, false)) << errcName(c);
}
