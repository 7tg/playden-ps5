#pragma once

#include "runtime/kit_manifest.h"

namespace playden::runtime {

// Bind admission to the production SHA-256 and no-follow file hashing backend.
// Caller-supplied hash callbacks are ignored; pins must come from the lock owner.
KitManifestResult VerifyInstalledKit(const std::filesystem::path& root,
                                    const KitManifestTrustedConfig& trusted);

}  // namespace playden::runtime
