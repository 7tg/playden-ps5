#include "runtime/kit_admission.h"
#include "runtime/artifact_hash.h"

namespace playden::runtime {
KitManifestResult VerifyInstalledKit(const std::filesystem::path& root,
                                    const KitManifestTrustedConfig& trusted) {
  auto production = trusted;
  production.hash_file = HashArtifactFile;
  production.hash_bytes = HashArtifactBytes;
  return VerifyKitManifest(root, production);
}
}  // namespace playden::runtime
