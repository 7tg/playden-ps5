#include "steam/backend/cm_app_details.h"

#include "steam/backend/app_info.h"

#include <algorithm>

namespace playden::steam {

AppDetails::Update CompareWithRecord(std::uint32_t id, const std::vector<DepotInfo>& selected,
                                     const installs::InstalledRecord& record, const std::set<std::uint32_t>& listed) {
  AppDetails::Update update;
  if (record.depots.empty()) return update;  // an imported copy, or a record that names no depots
  std::set<std::uint32_t> owners{id};
  for (const auto& depot : record.depots) owners.insert(depot.owner == 0 ? id : depot.owner);
  std::vector<DepotInfo> download;
  for (const auto& depot : selected) {
    const auto recorded = std::find_if(record.depots.begin(), record.depots.end(),
                                       [&](const installs::RecordedDepot& entry) { return entry.id == depot.id; });
    if (recorded == record.depots.end()) {
      // An empty one adds no files: Steam lists free DLC (and DLC folded into the game) as empty depots, which an
      // install from an older Playden doesn't record.
      if (depot.size == 0) continue;
      // A depot the install doesn't hold: content of DLC bought since, or new content of the game.
      const auto owner = depot.owner_app == 0 ? id : depot.owner_app;
      if (owner != id && !owners.count(owner)) update.new_dlc = true;
      else update.newer = true;
      download.push_back(depot);
    } else if (depot.manifest_gid.value_or(0) != recorded->gid) {
      update.newer = true;
      download.push_back(depot);
    }
  }
  for (const auto& entry : record.depots) {
    const auto source = entry.source == 0 ? id : entry.source;
    if (!listed.count(source)) continue;
    if (std::none_of(selected.begin(), selected.end(), [&](const DepotInfo& depot) { return depot.id == entry.id; }))
      update.newer = true;
  }
  std::uint64_t install = 0;
  DepotBytes(download, &install, &update.download_bytes);
  if (update.download_bytes == 0) update.download_bytes = install;
  return update;
}

AppDetails DetailsFromAppInfo(std::uint32_t id, const std::string& buffer,
                              const std::optional<LicensedDepots::Grant>& licensed,
                              std::string_view language,
                              const std::optional<installs::InstalledRecord>& installed,
                              const std::vector<AppInfo>& dlc_apps) {
  AppDetails details;
  details.id = GameId::Steam(id);
  const auto app = ParseAppInfo(id, buffer);
  if (!app) {
    details.reason = "Steam returned unreadable details for this game.";
    return details;
  }
  details.available = true;
  std::string reason;
  const std::set<std::uint32_t> owned{id};
  DepotSelection selection;
  selection.language = std::string(language);
  // An installed game is selected as an update of it would be: its record's branch and language.
  if (installed) {
    if (!installed->language.empty()) selection.language = installed->language;
    if (!installed->branch.empty()) selection.branch = installed->branch;
  }
  SelectionResult selected;
  // As an install selects them; without licenses, or when they grant none of it, every eligible depot.
  bool any = false;
  std::set<std::uint32_t> apps = owned;
  std::optional<std::set<std::uint32_t>> depots_owned;
  if (licensed) {
    apps.insert(licensed->apps.begin(), licensed->apps.end());
    depots_owned = licensed->depots;
    any = SelectDepots(*app, dlc_apps, apps, depots_owned, selection, &selected);
  }
  // Only a selection the licenses made can be compared with the record: without them every depot passes.
  if (any && installed) {
    std::set<std::uint32_t> listed{id};
    for (const auto& dlc : dlc_apps) listed.insert(dlc.app_id);
    details.update = CompareWithRecord(id, selected.depots, *installed, listed);
  }
  if (!any) {
    apps = owned;
    depots_owned.reset();
    if (!SelectDepots(*app, dlc_apps, owned, std::nullopt, selection, &selected)) {
      details.reason = "No Windows content to install.";
      return details;
    }
  }
  details.languages = selected.languages;
  const auto& depots = selected.depots;
  std::set<std::uint32_t> dlc;
  for (const auto& depot : depots)
    if (depot.owner_app != 0 && depot.owner_app != id) dlc.insert(depot.owner_app);
  details.dlc_count = static_cast<std::uint32_t>(dlc.size());
  std::uint64_t install = 0;
  std::uint64_t download = 0;
  DepotBytes(depots, &install, &download);
  if (install) details.install_bytes = install;
  if (download) details.download_bytes = download;
  // Versions, selected the same way as the public branch above.
  for (auto& offer : OfferedBranches(*app, dlc_apps, apps, depots_owned, selection.language)) {
    AppVersion version{std::move(offer.name), std::move(offer.description), offer.time_updated, offer.build_id,
                       offer.download_bytes, offer.install_bytes, {}};
    for (const auto& bytes : offer.languages)
      version.languages.push_back({bytes.language, bytes.download_bytes, bytes.install_bytes});
    details.versions.push_back(std::move(version));
  }
  std::vector<AppLaunch> launches;
  // An installed game starts as its branch does.
  const std::string branch = installed && !installed->branch.empty() ? installed->branch : std::string("public");
  details.windows_installable = EligibleLaunches(*app, owned, branch, &launches, &reason, &details.launches_any_branch);
  if (!details.windows_installable) details.reason = "No Windows launch configuration.";
  for (const auto& launch : launches)
    details.launches.push_back({launch.executable, launch.arguments, launch.working_directory,
                                launch.description.value_or(std::string())});
  return details;
}

std::vector<AppDetails> CmAppDetailsSource::Fetch(const StoredSession& session,
                                                  const std::vector<GameId>& ids,
                                                  const std::atomic<bool>& cancel) {
  // The service only queues Steam games.
  std::vector<std::uint32_t> apps;
  for (const auto& id : ids)
    if (const auto app = id.steam_app()) apps.push_back(*app);
  std::vector<std::uint32_t> app_ids(apps);
  if (app_ids.size() > kMaxAppInfoBatch) app_ids.resize(kMaxAppInfoBatch);
  const auto fetched = client_.FetchAppInfo(session, app_ids, cancel);
  std::vector<AppDetails> result;
  if (!fetched.ok()) return result;  // the service reports these as retryable
  const auto language = language_ ? language_() : std::string(kDefaultContentLanguage);
  for (const auto id : apps) {
    const auto found = fetched.buffers.find(id);
    if (found == fetched.buffers.end()) {
      AppDetails missing;
      missing.id = GameId::Steam(id);
      missing.reason = "Steam has no details for this game.";
      result.push_back(std::move(missing));
      continue;
    }
    const auto record = installed_ ? installed_(id) : std::nullopt;
    // An installed game's DLC apps list depots of their own: an update selects them too. A batch Steam
    // doesn't answer only leaves those DLC out of the comparison.
    std::vector<AppInfo> dlc_apps;
    if (record && !record->depots.empty())
      if (const auto app = ParseAppInfo(id, found->second)) {
        std::vector<std::uint32_t> dlc_ids;
        for (const auto dlc : app->dlc_app_ids)
          if (dlc != id && std::find(dlc_ids.begin(), dlc_ids.end(), dlc) == dlc_ids.end()) dlc_ids.push_back(dlc);
        for (std::size_t start = 0; start < dlc_ids.size() && !cancel.load(); start += kMaxAppInfoBatch) {
          const std::vector<std::uint32_t> batch(
              dlc_ids.begin() + static_cast<std::ptrdiff_t>(start),
              dlc_ids.begin() + static_cast<std::ptrdiff_t>(std::min(start + kMaxAppInfoBatch, dlc_ids.size())));
          const auto dlc_info = client_.FetchAppInfo(session, batch, cancel);
          if (!dlc_info.ok()) continue;
          for (const auto dlc : batch)
            if (const auto buffer = dlc_info.buffers.find(dlc); buffer != dlc_info.buffers.end())
              if (auto parsed = ParseAppInfo(dlc, buffer->second)) dlc_apps.push_back(std::move(*parsed));
        }
      }
    if (cancel.load()) return {};
    result.push_back(DetailsFromAppInfo(id, found->second, licensed_ ? licensed_->Get() : std::nullopt, language, record,
                                        dlc_apps));
  }
  return result;
}

}  // namespace playden::steam
