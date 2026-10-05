#pragma once

#include "steam/backend/app_info.h"
#include "steam/backend/cdn_client.h"
#include "steam/backend/cm_client.h"
#include "steam/backend/depot_manifest.h"
#include "steam/session_store.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace playden::steam {

enum class InstallError {
  kNone,
  kCanceled,
  kNoSession,
  kRejected,   // Steam refused a depot key or manifest code
  kNetwork,    // retryable transport trouble
  kCorrupt,    // a chunk or manifest failed verification
  kNoSpace,
  kStorage,    // a filesystem operation failed
  kProtocol,
};
const char* InstallErrorName(InstallError error);

struct InstallTarget {
  std::uint32_t app_id = 0;
  std::string install_root;          // absolute directory owned by this install
  std::vector<DepotInfo> depots;     // selected depots, in Steam's listing (mounting) order
};

struct InstallProgress {
  std::uint64_t download_total = 0;  // compressed bytes of every chunk to write
  std::uint64_t download_done = 0;   // includes chunks already present from a previous run
  std::uint64_t install_total = 0;   // file bytes after merging depots
  std::uint32_t files_total = 0;
};

struct InstallOutcome {
  InstallError error = InstallError::kProtocol;
  InstallProgress progress;
  std::string detail;  // short and identifier-free
};

// Seams for tests; the defaults use CmClient, CdnClient and the real filesystem.
struct InstallerHooks {
  std::function<DepotAccessResult(const StoredSession&, std::uint32_t app_id,
                                  const std::vector<DepotRequest>&, const std::atomic<bool>&)> access;
  std::function<CdnError(std::uint32_t cell_id, const std::atomic<bool>&, std::vector<ContentServer>*)> servers;
  // Returns a decrypted, validated manifest for one granted depot.
  std::function<InstallError(const ContentServer&, const DepotGrant&, const std::atomic<bool>&, DepotManifest*)> manifest;
  std::function<CdnError(const ContentServer&, std::uint32_t depot_id, const std::string& sha_hex,
                         std::size_t max_bytes, const std::atomic<bool>&, std::vector<std::uint8_t>*)> chunk;
  std::function<std::uint64_t(const std::string& path)> free_bytes;
  // Optional: the last CDN failure detail for logs.
  std::function<std::string()> last_failure;
  // Optional: step markers for diagnosing native failures (no identifiers).
  std::function<void(const char* step)> trace;
};

// Downloads and assembles an app's depots into install_root. Safe to run again after any
// interruption: finished chunks recorded in the journal are not fetched twice. Runs
// synchronously on the caller's worker thread.
class DepotInstaller {
 public:
  DepotInstaller(CmClient& cm, CdnClient& cdn);
  explicit DepotInstaller(InstallerHooks hooks);

  InstallOutcome Run(const StoredSession& session, const InstallTarget& target,
                     const std::atomic<bool>& cancel,
                     const std::function<void(const InstallProgress&)>& on_progress = {});

  static constexpr const char* kJournalDir = ".playden-download";
  static constexpr const char* kInstallRecord = ".playden-install.json";
  static constexpr const char* kFileList = ".playden-files";
  static constexpr int kChunkAttempts = 3;

 private:
  InstallerHooks hooks_;
 public:
  void SetTrace(std::function<void(const char*)> trace) { hooks_.trace = std::move(trace); }
};

// Removes a managed install directory: only paths under install_root, and only when it holds
// Playden's install record or download journal.
bool RemoveManagedInstall(const std::string& install_root);

}  // namespace playden::steam
