#pragma once

#include "steam/app_details_service.h"
#include "steam/backend/cm_client.h"

namespace playden::steam {

// App details from PICS: totals of the depots Playden would install (Windows, English, public
// branch) and whether a Windows launch entry exists. Sizes are Steam's per-depot figures, so they
// are an estimate until the manifests are fetched.
class CmAppDetailsSource final : public AppDetailsSource {
 public:
  explicit CmAppDetailsSource(CmClient& client) : client_(client) {}
  // The licenses the library last read: sizes then count only the depots they grant.
  void SetLicensedDepots(const LicensedDepots* licensed) { licensed_ = licensed; }
  std::vector<AppDetails> Fetch(const StoredSession& session, const std::vector<GameId>& ids,
                                const std::atomic<bool>& cancel) override;

 private:
  CmClient& client_;
  const LicensedDepots* licensed_ = nullptr;
};

// Pure part, for tests: turns one parsed app buffer into details. With `licensed`, sizes count
// only the depots those licenses grant, as an install selects them.
AppDetails DetailsFromAppInfo(GameId id, const std::string& buffer,
                              const std::optional<LicensedDepots::Grant>& licensed = std::nullopt);

}  // namespace playden::steam
