#pragma once

#include "steam/backend/cloud_mapping.h"
#include "steam/backend/cloud_messages.h"
#include "steam/backend/cm_client.h"
#include "steam/backend/http_client.h"
#include "steam/session_store.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace playden::steam::cloud {

// Seams for tests; the defaults use CmClient and HttpClient.
struct CloudHooks {
  std::function<CmError(const StoredSession&, std::uint32_t deadline_ms, const std::atomic<bool>&,
                        const std::function<CmError(const CmClient::ServiceCall&, std::uint64_t steam_id)>&)> session;
  // The app's Steam Cloud declaration from PICS; nothing when Steam does not describe the app.
  std::function<std::optional<CloudSaveInfo>(const StoredSession&, std::uint32_t app_id, const std::atomic<bool>&,
                                             CmError* error)> app_info;
  std::function<HttpResponse(const HttpRequest&, const std::atomic<bool>&)> http;
  // Unix time for baselines and backup folder names.
  std::function<std::int64_t()> now;
};

// Where one game's saves live on this device.
struct GameSaveLocation {
  std::uint32_t app_id = 0;
  std::string game_root;    // the installed game
  std::string prefix_root;  // folder holding drive_c for the game's Windows user folders
  std::string state_root;   // the sync baseline and backups; outlives the install
};

struct FileFact {
  bool present = false;
  std::vector<std::uint8_t> sha1;
  std::uint64_t size = 0;
  std::uint64_t time = 0;
};

enum class Action { kUnchanged, kDownload, kUpload, kDeleteLocal, kDeleteRemote, kConflict, kUnavailable };

struct Decision {
  std::string remote_name;
  LocalSavePath local;
  FileFact local_fact;
  FileFact remote_fact;
  Action action = Action::kUnchanged;
};

enum class PlanStatus {
  kUpToDate,       // nothing to transfer
  kReady,          // transfers can run without a choice
  kConflict,       // the player picks local or Cloud
  kNeedsAccount,   // remote changes wait for the player to confirm this Steam account
  kUnsupported,    // no Cloud saves, or locations Playden cannot map
  kFailed,
};

struct CloudPlan {
  PlanStatus status = PlanStatus::kFailed;
  std::string message;  // short, user-facing
  std::uint64_t change_number = 0;
  std::string account_key;
  bool requires_account_confirmation = false;
  std::vector<Decision> decisions;
  std::size_t downloads = 0, uploads = 0, local_deletes = 0, remote_deletes = 0, conflicts = 0;
  // For the conflict dialog: what each side holds.
  std::size_t local_files = 0, remote_files = 0;
  std::uint64_t local_bytes = 0, remote_bytes = 0, local_latest = 0, remote_latest = 0;
};

enum class Resolution { kAutomatic, kUseLocal, kUseCloud };

struct CloudOutcome {
  bool ok = false;
  std::string message;  // short, user-facing
  std::size_t downloaded = 0, uploaded = 0, removed_local = 0, removed_remote = 0;
};

inline constexpr std::size_t kMaxLocalSaveFiles = 10'000;
inline constexpr std::uint64_t kMaxLocalSaveBytes = 512ULL * 1024 * 1024;
inline constexpr const char* kMachineName = "Playden";

std::string AccountKey(std::uint64_t steam_id);

// Steam Cloud save sync for one game, run synchronously on a worker thread. Timestamps never pick
// a winner: a file changed on both sides since the last sync is a conflict for the player. Local
// files are backed up under state_root before they are replaced or removed.
class CloudSync {
 public:
  CloudSync(CmClient& cm, HttpClient& http);
  explicit CloudSync(CloudHooks hooks);

  CloudPlan Check(const StoredSession& session, const GameSaveLocation& location, const std::atomic<bool>& cancel);
  // Carries out a plan from Check. `resolution` settles conflicts; remote changes on a plan that
  // requires it run only with `confirm_account`.
  CloudOutcome Apply(const StoredSession& session, const GameSaveLocation& location, const CloudPlan& plan,
                     Resolution resolution, bool confirm_account, const std::atomic<bool>& cancel);

 private:
  CloudHooks hooks_;
};

}  // namespace playden::steam::cloud
