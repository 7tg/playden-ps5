#pragma once

#include "installs/install_record.h"
#include "steam/app_details_service.h"
#include "steam/backend/app_info.h"
#include "steam/backend/cm_client.h"
#include "steam/backend/content_language.h"

#include <functional>

namespace playden::steam {

// App details from PICS: totals of the depots Playden would install (Windows, the chosen content
// language, public branch) and whether a Windows launch entry exists. Sizes are Steam's per-depot
// figures, so they are an estimate until the manifests are fetched.
class CmAppDetailsSource final : public AppDetailsSource {
 public:
  explicit CmAppDetailsSource(CmClient& client) : client_(client) {}
  // The licenses the library last read: sizes then count only the depots they grant.
  void SetLicensedDepots(const LicensedDepots* licensed) { licensed_ = licensed; }
  // The content language sizes are counted for; it has to be the one an install would use or
  // the free-space check is made against the wrong depots. Read on the details worker when a
  // fetch starts, so a change in Settings reaches the next one.
  void SetContentLanguage(std::function<std::string()> language) { language_ = std::move(language); }
  // An installed game's install record (none: not installed). Its details are selected with the
  // record's branch and language, its DLC apps' appinfo is read too, and they say whether an update
  // or new DLC is there (AppDetails::update). Called on the details worker.
  void SetInstalled(std::function<std::optional<installs::InstalledRecord>(std::uint32_t app)> installed) {
    installed_ = std::move(installed);
  }
  std::vector<AppDetails> Fetch(const StoredSession& session, const std::vector<GameId>& ids,
                                const std::atomic<bool>& cancel) override;

 private:
  CmClient& client_;
  const LicensedDepots* licensed_ = nullptr;
  std::function<std::string()> language_;
  std::function<std::optional<installs::InstalledRecord>(std::uint32_t)> installed_;
};

// Pure part, for tests: turns one parsed app buffer into details. With `licensed`, sizes count
// only the depots those licenses grant, as an install selects them. With `installed` (the game's
// install record), the record's branch and language select, and `update` compares the selection
// with the record; `dlc_apps` are the game's DLC apps' appinfo, for the depots only they list.
AppDetails DetailsFromAppInfo(std::uint32_t id, const std::string& buffer,
                              const std::optional<LicensedDepots::Grant>& licensed = std::nullopt,
                              std::string_view language = kDefaultContentLanguage,
                              const std::optional<installs::InstalledRecord>& installed = std::nullopt,
                              const std::vector<AppInfo>& dlc_apps = {});

// What `selected` (as an install of the record's branch and language selects now) changes against
// `record`. Recorded depots listed by an app whose appinfo is not in `listed` are not compared: they
// can't have left. Nothing for a record without depots.
AppDetails::Update CompareWithRecord(std::uint32_t id, const std::vector<DepotInfo>& selected,
                                     const installs::InstalledRecord& record, const std::set<std::uint32_t>& listed);

}  // namespace playden::steam
