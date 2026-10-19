#pragma once
#include "runtime/kit_manifest.h"
#include <string>

namespace playden::runtime {

// Playden uses whichever kit is installed at kDevelopmentKitRoot, as long as it is
// complete and its files match its own manifest. The values below describe the kit
// this build was tested with; they mirror the "prospero_kit" entry in
// dependencies.lock.json (a host test checks they agree) and are only shown next to
// the installed kit (Settings > About).
inline constexpr const char* kDevelopmentKitId = "hl2-fw1360-dev1";
inline constexpr const char* kDevelopmentKitRoot = "/data/playden/kits/hl2-fw1360-dev1";
inline constexpr const char* kExpectedKitManifestSha256 =
    "ff7b2728c6f17d0fc3ee6c22c4446b9e80f69082b64c42afc59e07f0c0552cef";

// What this build accepts from a kit, on any console firmware.
KitAdmissionConfig DevelopmentKitAdmission();

}  // namespace playden::runtime
