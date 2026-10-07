#include "runtime/kit_trust.h"

namespace playden::runtime {

KitAdmissionConfig DevelopmentKitAdmission(const std::string& current_firmware) {
  KitAdmissionConfig config;
  config.expected_manifest_sha256 = kExpectedKitManifestSha256;
  config.current_firmware = current_firmware;
  // The publication guard capability stays unused until native qualification.
  config.known_capabilities = {"dxvk", "offline_bottle", "per_game_title", "wow64_i386"};
  return config;
}

}  // namespace playden::runtime
