// Explicit opt-in checks for hardware-affecting actions (Qt-free).
#pragma once
#include <cstdlib>
#include <cstring>

namespace lewitt {

// An authorization environment variable counts only when it is set to exactly "1". Empty, "0",
// "true", "yes", "1 " or any other spelling does not count: a typo must never enable writes.
inline bool envFlagExactlyOne(const char* value) { return value != nullptr && std::strcmp(value, "1") == 0; }
inline bool envAuthorized(const char* name) { return envFlagExactlyOne(std::getenv(name)); }

}  // namespace lewitt
