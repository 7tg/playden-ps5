#pragma once

#include "steam/backend/cloud_sync.h"
#include "steam/cloud_service.h"

#include <functional>
#include <map>

namespace playden::steam {

// CloudRunner over the Steam Cloud engine. A game's Wine prefix (for Windows user folders) lives
// at the explicitly supplied bottle path, with a legacy state-folder fallback.
class SteamCloudRunner final : public CloudRunner {
 public:
  SteamCloudRunner(CmClient& cm, HttpClient& http);
  explicit SteamCloudRunner(cloud::CloudHooks hooks);

  CloudReport Check(const StoredSession& session, const CloudLocation& location, const std::atomic<bool>& cancel) override;
  CloudReport Apply(const StoredSession& session, const CloudLocation& location, CloudResolution resolution,
                    bool confirm_account, const std::atomic<bool>& cancel) override;

  // Optional sanitized log: the Steam app ID, what happened, and a short user-facing message.
  void SetLog(std::function<void(GameId app, const char* event, const std::string& message)> log);

  static cloud::GameSaveLocation SaveLocation(const CloudLocation& location);

 private:
  cloud::CloudSync sync_;
  struct PendingPlan {
    cloud::CloudPlan plan;
    cloud::GameSaveLocation location;
  };
  std::map<GameId, PendingPlan> plans_;  // bind Apply to the location actually checked
  std::function<void(GameId, const char*, const std::string&)> log_;
  GameId current_app_ = 0;  // the game being checked or synced, for traces
};

}  // namespace playden::steam
