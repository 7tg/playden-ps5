#pragma once
#include "runtime/kit_manifest.h"
#include <string>

namespace playden::runtime {

// Development kit admitted by this build. The values mirror the "prospero_kit"
// entry in dependencies.lock.json (a host test checks they agree). The kit is
// not releasable: its Wine modules carry host build paths.
inline constexpr const char* kDevelopmentKitId = "hl2-fw1360-dev1";
inline constexpr const char* kDevelopmentKitRoot = "/data/playden/kits/hl2-fw1360-dev1";

// Independent trust anchors for the development kit. current_firmware is the
// running console's version (for example "13.60"); an empty value never matches.
KitManifestTrustedConfig DevelopmentKitTrust(const std::string& current_firmware);

}  // namespace playden::runtime
