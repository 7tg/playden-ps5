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
    "8943e45e5e1b024f075e830753ab0ebcb52ae89134d4606365c53223c33aea97";

// What this build accepts from a kit. current_firmware is the running console's
// version (for example "13.60"); an empty value never matches.
KitAdmissionConfig DevelopmentKitAdmission(const std::string& current_firmware);

}  // namespace playden::runtime
