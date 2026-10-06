#pragma once

#include "runtime/game_install.h"
#include "runtime/package_installer.h"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace playden::runtime {

struct KitTitleTrustPin {
  std::string helper_sha256;
  std::string protocol_sha256;
};

// Trust data is supplied by the dependency lock owner. Values reported by the
// kit manifest are never promoted to trust anchors.
struct KitManifestTrustedConfig {
  std::string manifest_sha256;
  std::string current_firmware;
  std::string required_architecture = "x86_64+wow64-i386";
  std::map<std::string, std::map<std::string, std::string>> sources;
  std::map<std::string, KitTitleTrustPin> title_pins;
  std::vector<std::string> allowed_capabilities;
  std::function<std::optional<std::string>(const std::filesystem::path&)> hash_file;
  std::function<std::optional<std::string>(std::string_view)> hash_bytes;
  // Independent digest of the reviewed publication-provider provenance JSON.
  // Required when the owner allows playden-publication-guard-v1; never derived
  // from the kit's own claim or a runtime source checkout.
  std::optional<std::string> publication_provider_sha256;
};

enum class KitManifestStatus {
  kVerified,
  kMalformed,
  kIntegrityFailure,
  kUnsupportedSchema,
  kUnsupportedKit,
  kUnsupportedFirmware,
  kUnsupportedArchitecture,
  kUnsupportedSource,
};

struct KitManifestResult {
  KitManifestStatus status = KitManifestStatus::kMalformed;
  std::string reason;
  std::optional<VerifiedKitView> view;
  std::vector<std::string> firmware;
  std::vector<std::string> capabilities;
  std::vector<std::string> available_variant_ids;
  std::vector<RuntimeArtifactIdentity> artifacts;
};

KitManifestResult VerifyKitManifest(const std::filesystem::path& root,
                                    const KitManifestTrustedConfig& trusted);

}  // namespace playden::runtime
