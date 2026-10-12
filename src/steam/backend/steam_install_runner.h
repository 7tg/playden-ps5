#pragma once

#include "steam/backend/cdn_client.h"
#include "steam/backend/cm_client.h"
#include "steam/backend/depot_installer.h"
#include "steam/install_service.h"

namespace playden::steam {

// Installs a Steam game: PICS app info for the depot selection (Windows, English, public
// branch), then the resumable depot installer.
class SteamInstallRunner final : public InstallRunner {
 public:
  SteamInstallRunner(CmClient& cm, CdnClient& cdn) : cm_(cm), installer_(cm, cdn) {}
  Result Install(const StoredSession& session, GameId id, const std::string& root,
                 const std::atomic<bool>& cancel,
                 const std::function<void(std::uint64_t, std::uint64_t)>& progress) override;
  bool Remove(const std::string& root) override { return RemoveManagedInstall(root); }
  // Optional sanitized log: the Steam app ID, step, error category and a short detail; never
  // tokens or account identifiers. Called on the install worker.
  using LogFn = std::function<void(GameId app, const char* step, const char* category, const char* detail)>;
  void SetLog(LogFn log) {
    log_ = log;
    installer_.SetTrace([this, log](const char* step) { if (log) log(current_app_.load(), "trace", step, ""); });
  }

 private:
  CmClient& cm_;
  DepotInstaller installer_;
  LogFn log_;
  std::atomic<GameId> current_app_{0};
};

// User-facing text for an install failure category.
const char* InstallFailureText(InstallError error);

}  // namespace playden::steam
