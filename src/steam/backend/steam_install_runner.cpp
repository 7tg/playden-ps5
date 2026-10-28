#include "steam/backend/steam_install_runner.h"

#include "runtime/steamworks.h"
#include "steam/backend/app_info.h"

#include <algorithm>
#include <set>
#include <charconv>

namespace playden::steam {

void LogInstallTrace(const InstallLogFn& log, std::uint32_t app, const char* marker) {
  if (!log) return;
  if (std::string(marker) == "journal-reset") log(app, "journal", "reset", "");
  else if (std::string(marker) == "journal-legacy") log(app, "journal", "legacy", "");
  else log(app, "trace", marker, "");
}

std::string FormatInstallServerList(const std::vector<ContentServer>& servers, const char* preference) {
  const std::string pref = preference ? preference : "auto";
  std::string detail = "n=" + std::to_string(servers.size()) + " https=" +
      std::to_string(std::count_if(servers.begin(), servers.end(), [](const ContentServer& server) { return server.https; })) +
      " pref=" + (pref == "chosen" || pref == "missing" || pref == "fastest" ? pref : "auto") + " first=" +
      (servers.empty() ? "none" : CdnServerLabel(servers.front().host) + "@0") + " order=";
  for (std::size_t i = 0; i < std::min<std::size_t>(servers.size(), 8); ++i) {
    if (i) detail += ',';
    detail += CdnServerLabel(servers[i].host) + ':' + std::to_string(std::clamp(servers[i].priority, 0, 9));
  }
  return detail;
}

std::vector<std::string> FormatInstallServerStats(const std::vector<InstallServerStats>& servers) {
  std::vector<std::string> lines;
  std::string detail;
  for (std::size_t i = 0; i < std::min<std::size_t>(servers.size(), 9); ++i) {
    const auto& server = servers[i];
    if (!server.requests) continue;
    const auto& label = server.label;
    const bool known = label == "akamai" || label == "fastly" || label == "google" || label == "alibaba" ||
                       label == "edgenext" || label == "valve" || label == "cache" || label == "other";
    const auto token = std::string(known ? label : "cache") + '@' + std::to_string(std::min<std::size_t>(server.index, 8)) +
        ":req=" + std::to_string(server.requests) + ",fail=" + std::to_string(server.failures) +
        ",mb=" + std::to_string(server.bytes / 1000000) + ",ms=" + std::to_string(server.fetch_ms);
    // Leave room for timestamp, app, step and result in the native 640-byte log buffer.
    if (!detail.empty() && detail.size() + 1 + token.size() > 360) { lines.push_back(std::move(detail)); detail.clear(); }
    if (!detail.empty()) detail += ' ';
    detail += token;
  }
  if (!detail.empty()) lines.push_back(std::move(detail));
  if (lines.empty()) lines.push_back("n=0");
  return lines;
}

std::string FormatInstallStats(const char* phase, const InstallStats& stats) {
  std::string detail;
  const auto add = [&](const char* key, std::uint64_t value) {
    if (!detail.empty()) detail += ' ';
    detail += key; detail += '='; detail += std::to_string(value);
  };
  add("files_total", stats.files_total); add("download_total", stats.download_total);
  if (std::string(phase) == "shape") {
    add("chunks_pending", stats.chunks_pending); add("files_pending", stats.files_pending);
    add("bytes_pending", stats.bytes_pending);
    add("slots", stats.slots); add("ahead", stats.ahead_files);
    return detail;
  }
  add("dl_ms", stats.download_ms); add("cb", stats.compressed_bytes); add("ob", stats.original_bytes);
  add("ch", stats.chunks_done); add("sk", stats.chunks_skipped);
  add("fc", stats.files_created); add("dc", stats.folders_created);
  add("fetch", stats.fetch_ms); add("proc", stats.process_ms); add("create", stats.create_ms);
  add("write", stats.write_ms); add("jrnl", stats.journal_ms); add("back", stats.backoff_ms);
  add("req", stats.requests); add("conn", stats.new_connections);
  add("res", stats.resolve_ms); add("tcp", stats.connect_ms); add("tls", stats.tls_ms); add("ttfb", stats.first_byte_ms);
  add("retry", stats.chunk_retries); add("bad", stats.corrupt_retries);
  add("slots", stats.slots); add("reuse", stats.reuse);
  add("waiting", stats.peak_waiting); add("active", stats.peak_active);
  add("access", stats.access_ms); add("servers", stats.servers_ms); add("manifest", stats.manifests_ms); add("prep", stats.prepare_ms);
  add("http_ms", stats.total_ms); add("http_b", stats.bytes); add("fail", stats.failures);
  return detail;
}

std::optional<std::string> FastestServerCache::Get(std::uint64_t account) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (host_.empty()) return std::nullopt;
  if (account != account_ || Time() - at_ >= std::chrono::minutes(kMinutes)) {
    host_.clear();
    return std::nullopt;
  }
  return host_;
}

void FastestServerCache::Remember(std::uint64_t account, const std::string& host) {
  std::lock_guard<std::mutex> lock(mutex_);
  account_ = account;
  host_ = host;
  at_ = Time();
}

void FastestServerCache::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  host_.clear();
}

std::string SteamInstallRunner::Remove(const std::string& root) {
  std::string failure;
  const bool ok = RemoveManagedInstall(root, &failure);
  const auto slash = root.rfind('/');
  const auto name = root.substr(slash == std::string::npos ? 0 : slash + 1);
  std::uint32_t app = 0;
  const auto parsed = std::from_chars(name.data(), name.data() + name.size(), app);
  if (log_ && app != 0 && parsed.ec == std::errc{} && parsed.ptr == name.data() + name.size())
    log_(app, "install-removal", "done", ok ? "ok=1" : ("ok=0 " + failure).c_str());
  return ok ? std::string() : failure;
}

const char* InstallFailureText(InstallError error) {
  switch (error) {
    case InstallError::kNone: return "";
    case InstallError::kCanceled: return "";
    case InstallError::kNoSession: return "Sign in to Steam to install.";
    case InstallError::kRejected: return "Steam refused access to this game's content.";
    case InstallError::kNetwork: return "Network problem. Resume to try again.";
    case InstallError::kCorrupt: return "Downloaded content failed verification. Resume to try again.";
    case InstallError::kNoSpace: return "Not enough free space.";
    case InstallError::kStorage: return "Could not write the game files.";
    case InstallError::kProtocol: return "Steam returned unexpected data.";
  }
  return "Install failed.";
}

namespace {

// Comma-separated items after a key, split so each line fits the native log buffer with room for the
// timestamp, app and step.
std::vector<std::string> FamilyLines(const char* key, const std::vector<std::string>& items) {
  std::vector<std::string> lines;
  std::string line = key;
  bool first = true;
  for (const auto& item : items) {
    if (!first && line.size() + 1 + item.size() > 360) {
      lines.push_back(std::move(line));
      line = key;
      first = true;
    }
    line += (first ? "" : ",") + item;
    first = false;
  }
  if (first) line += "none";
  lines.push_back(std::move(line));
  return lines;
}

}  // namespace

InstallRunner::Result SteamInstallRunner::InstallApp(const StoredSession& session, std::uint32_t id, const std::string& root,
                                                  const std::atomic<bool>& cancel,
                                                  const std::function<void(std::uint64_t, std::uint64_t, std::uint64_t)>& progress,
                                                  const std::function<void(InstallStep)>& step) {
  Result result;
  current_app_.store(id);
  server_name_.clear();
  installer_.SetTuning(tuning_ ? tuning_() : DownloadTuning{});
  const auto account = session.steam_id;
  installer_.SetFastestCache([this, account] { return fastest_.Get(account); },
                             [this, account](const std::string& host) { fastest_.Remember(account, host); });
  auto log = [this, id](const char* step, const char* category, const char* detail) {
    if (log_) log_(id, step, category, detail);
  };
  const auto info = cm_.FetchAppInfo(session, {id}, cancel);
  if (cancel.load()) {
    result.canceled = true;
    return result;
  }
  if (!info.ok()) {
    log("app_info", CmErrorName(info.error), "");
    result.failure = "Could not reach Steam. Resume to try again.";
    result.retry_later = info.error == CmError::kNetwork || info.error == CmError::kTimeout;
    return result;
  }
  const auto buffer = info.buffers.find(id);
  const auto app = buffer == info.buffers.end() ? std::nullopt : ParseAppInfo(id, buffer->second);
  if (!app) {
    log("app_info", "missing", buffer == info.buffers.end() ? "no buffer" : "unparsable");
    result.failure = "Steam has no install details for this game.";
    return result;
  }
  InstallTarget target;
  target.app_id = id;
  target.install_root = root;
  std::string reason;
  // Like the reference, only depots this account's licenses grant are installed. Accounts with
  // thousands of licenses take a minute here (issue #47), so the player sees this step.
  if (step) step(InstallStep::kCheckingLicenses);
  const auto entitlements = cm_.FetchAcquisitionDates(session, cancel);
  if (cancel.load()) {
    result.canceled = true;
    return result;
  }
  if (!entitlements.ok()) {
    log("entitlements", CmErrorName(entitlements.error), "");
    result.failure = "Could not read this account's licenses. Resume to try again.";
    result.retry_later = entitlements.error == CmError::kNetwork || entitlements.error == CmError::kTimeout;
    return result;
  }
  if (entitlements.skipped_packages)
    log("entitlements", "partial", ("skipped_packages=" + std::to_string(entitlements.skipped_packages)).c_str());
  // A game a Steam family member shares: which borrowed packages carry it. steam-cm.log names each
  // package's owner (family step=license).
  const bool family_shared = entitlements.shared_apps.count(id) > 0;
  if (family_shared) {
    std::vector<std::string> packages;
    for (const auto& [package, apps] : entitlements.package_apps)
      if (std::find(apps.begin(), apps.end(), id) != apps.end()) packages.push_back(std::to_string(package));
    for (const auto& line : FamilyLines("packages=", packages)) log("family", "shared", line.c_str());
  }
  std::set<std::uint32_t> owned_apps = entitlements.owned_apps;
  owned_apps.insert(id);
  if (!SelectDepots(*app, owned_apps, entitlements.owned_depots, &target.depots, &reason)) {
    log("depots", "none", reason.c_str());
    result.failure = "No Windows content to install.";
    return result;
  }
  if (const auto missing = MissingBaseDepots(*app, entitlements.owned_depots, target.depots); !missing.empty()) {
    std::string ids;
    for (const auto depot : missing) ids += (ids.empty() ? "" : " ") + std::to_string(depot);
    log("depots", "partial", ("unowned=" + ids).c_str());
    // Which licenses carry the game and what they grant, against the app's depots.
    std::string packages;
    for (const auto& [package, apps] : entitlements.package_apps) {
      if (std::find(apps.begin(), apps.end(), id) == apps.end()) continue;
      packages += " " + std::to_string(package) + ":";
      const auto depots = entitlements.package_depots.find(package);
      if (depots != entitlements.package_depots.end())
        for (const auto depot : depots->second) packages += std::to_string(depot) + ",";
    }
    log("depots", "licenses", packages.c_str());
    std::string listed;
    for (const auto& depot : app->depots)
      listed += " " + std::to_string(depot.id) + ":" + std::to_string(depot.size) + ":" + depot.os_list + ":" +
                (depot.depot_from_app ? "from" + std::to_string(*depot.depot_from_app) : "") +
                (depot.shared_install ? "shared" : "") + (depot.is_dlc ? "dlc" : "") + ":" +
                (entitlements.owned_depots.count(depot.id) ? "owned" : "-") + ":" + depot.name;
    log("depots", "listed", listed.c_str());
    result.failure = "Steam didn't grant access to part of this game.";
    return result;
  }
  // Keep Steam's listing order: it is the depot mounting order.
  std::vector<DepotInfo> ordered;
  for (const auto& depot : app->depots)
    for (const auto& selected : target.depots)
      if (selected.id == depot.id) ordered.push_back(selected);
  target.depots = ordered;
  std::string selected_ids;
  for (const auto& depot : target.depots) selected_ids += (selected_ids.empty() ? "" : " ") + std::to_string(depot.id);
  log("depots", "selected", selected_ids.c_str());
  if (family_shared) {
    // Which packages grant each selected depot, as depot:package/package.
    std::vector<std::string> grants;
    for (const auto& depot : target.depots) {
      std::string grant = std::to_string(depot.id) + ":";
      std::size_t found = 0;
      for (const auto& [package, depots] : entitlements.package_depots)
        if (std::find(depots.begin(), depots.end(), depot.id) != depots.end())
          grant += (found++ ? "/" : "") + std::to_string(package);
      grants.push_back(found ? grant : grant + "none");
    }
    for (const auto& line : FamilyLines("depots=", grants)) log("family", "depots", line.c_str());
  }
  if (step) step(InstallStep::kDownloading);
  const auto outcome = installer_.Run(session, target, cancel, [&](const InstallProgress& state) {
    server_name_ = state.server_name;
    if (progress) progress(state.download_done, state.download_total, state.held_bytes);
  });
  if (!outcome.skipped_depots.empty()) {
    std::string skipped;
    for (const auto id : outcome.skipped_depots) skipped += (skipped.empty() ? "" : " ") + std::to_string(id);
    log("depots", "skipped", ("borrowed-refused=" + skipped).c_str());
  }
  log("install", InstallErrorName(outcome.error), outcome.detail.c_str());
  result.ok = outcome.error == InstallError::kNone;
  if (result.ok) {
    // What the Steamworks emulator needs later, while the licenses are at hand: installed depots and owned DLC.
    std::vector<std::uint32_t> depots, dlc;
    for (const auto& depot : target.depots)
      if (std::find(outcome.skipped_depots.begin(), outcome.skipped_depots.end(), depot.id) == outcome.skipped_depots.end())
        depots.push_back(depot.id);
    for (const auto app_id : app->dlc_app_ids)
      if (entitlements.owned_apps.count(app_id)) dlc.push_back(app_id);
    if (!runtime::WriteSteamworksFacts(root, depots, dlc)) log("steamworks_facts", "write-failed", root.c_str());
  }
  result.canceled = outcome.error == InstallError::kCanceled;
  result.retry_later = outcome.error == InstallError::kNetwork;
  result.failure = InstallFailureText(outcome.error);
  result.install_bytes = outcome.progress.install_total;
  return result;
}

}  // namespace playden::steam
