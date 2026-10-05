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
  // Optional sanitized log: step, error category and a short detail; never tokens or IDs of accounts.
  void SetLog(std::function<void(const char* step, const char* category, const char* detail)> log) {
    log_ = log;
    installer_.SetTrace([log](const char* step) { if (log) log("trace", step, ""); });
  }

 private:
  CmClient& cm_;
  DepotInstaller installer_;
  std::function<void(const char*, const char*, const char*)> log_;
};

// User-facing text for an install failure category.
const char* InstallFailureText(InstallError error);

}  // namespace playden::steam
