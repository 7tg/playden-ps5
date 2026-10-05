#pragma once

#include "platform/ps5/native_access.h"

namespace playden::ps5 {
// Initializes UserService and probes linked graphics entry points before the
// title-scoped grant. Display initialization must follow this single-threaded
// bootstrap so graphics threads cannot invalidate the helper credential check.
// All services created afterwards must use the returned paths.
NativeAccessResult PrepareNativeStartup(const std::string& title_id);
}
