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

 private:
  CmClient& cm_;
  DepotInstaller installer_;
};

// User-facing text for an install failure category.
const char* InstallFailureText(InstallError error);

}  // namespace playden::steam
