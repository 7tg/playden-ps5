#pragma once

#include "platform/ps5/native_access.h"

namespace hui::ps5 { class Display; }

namespace playden::ps5 {
// Initializes UserService and graphics before requesting the title-scoped grant.
// All services created afterwards must use the returned paths.
NativeAccessResult PrepareNativeStartup(hui::ps5::Display& display,
                                       const std::string& title_id);
}
