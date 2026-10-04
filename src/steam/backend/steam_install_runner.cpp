#include "steam/backend/steam_install_runner.h"

#include "steam/backend/app_info.h"

namespace playden::steam {

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

InstallRunner::Result SteamInstallRunner::Install(const StoredSession& session, GameId id, const std::string& root,
                                                  const std::atomic<bool>& cancel,
                                                  const std::function<void(std::uint64_t, std::uint64_t)>& progress) {
  Result result;
  const auto info = cm_.FetchAppInfo(session, {id}, cancel);
  if (cancel.load()) {
    result.canceled = true;
    return result;
  }
  if (!info.ok()) {
    result.failure = "Could not reach Steam. Resume to try again.";
    return result;
  }
  const auto buffer = info.buffers.find(id);
  const auto app = buffer == info.buffers.end() ? std::nullopt : ParseAppInfo(id, buffer->second);
  if (!app) {
    result.failure = "Steam has no install details for this game.";
    return result;
  }
  InstallTarget target;
  target.app_id = id;
  target.install_root = root;
  std::string reason;
  if (!SelectDepots(*app, {id}, std::nullopt, &target.depots, &reason)) {
    result.failure = "No Windows content to install.";
    return result;
  }
  // Keep Steam's listing order: it is the depot mounting order.
  std::vector<DepotInfo> ordered;
  for (const auto& depot : app->depots)
    for (const auto& selected : target.depots)
      if (selected.id == depot.id) ordered.push_back(selected);
  target.depots = ordered;
  const auto outcome = installer_.Run(session, target, cancel, [&](const InstallProgress& state) {
    if (progress) progress(state.download_done, state.download_total);
  });
  result.ok = outcome.error == InstallError::kNone;
  result.canceled = outcome.error == InstallError::kCanceled;
  result.failure = InstallFailureText(outcome.error);
  result.install_bytes = outcome.progress.install_total;
  return result;
}

}  // namespace playden::steam
