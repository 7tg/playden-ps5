#pragma once
#include "homebrew/simulation.hpp"
#include "steam/service.h"
#include "app/library_controller.h"
#include "app/persistence_controller.h"
#include "steam/app_details_service.h"
#include "steam/install_service.h"
#include "steam/cloud_service.h"
#include "homebrew/runtime_host.hpp"
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>

namespace playden::homebrew {
// Main-thread bridge. The backend/repository outlive this object. IO stays on service
// workers; the presentation receives no credential tokens.
class Controller {
 public:
  Controller(Simulation& model, steam::AuthBackend& backend, SessionRepository& repository,
             LibraryService* library_service = nullptr, app::PersistenceService* persistence_service = nullptr,
             steam::AppDetailsService* app_details = nullptr, steam::InstallService* installs = nullptr,
             steam::CloudService* cloud = nullptr, RuntimeHost* runtime = nullptr);
  ~Controller();
  void Tick();
  void Shutdown();
  Controller(const Controller&) = delete;
  Controller& operator=(const Controller&) = delete;
 private:
  enum class Phase { Idle, Restore, SignIn, ClearLibrary, ClearOrganization, SignOut };
  void Begin();
  void SignOut(bool switch_account);
  void Restore();
  void AdvanceClear();
  void BeginOrganizationClear();
  void AdvanceOrganizationClear();
  void Apply(const steam::SteamEvent& event);
  void Update(AuthState state, const std::string& error = {}, bool clear = false,
              const std::string& challenge = {});
  void TickAppDetails();
  void TickInstalls();
  void TickCloud();
  void TickRuntime();
  Simulation& model_;
  steam::AppDetailsService* app_details_ = nullptr;
  steam::InstallService* installs_ = nullptr;
  std::uint64_t install_revision_ = 0;
  std::uint64_t install_generation_ = 0;
  steam::CloudService* cloud_ = nullptr;
  std::uint64_t cloud_revision_ = 0;
  std::map<GameId, std::string> game_roots_;  // installed games' folders, for Steam Cloud
  std::map<GameId, std::string> game_volumes_;  // installed games' volume ids
  RuntimeHost* runtime_ = nullptr;
  std::uint64_t runtime_revision_ = 0;
  std::uint64_t runtime_install_revision_ = 0;
  std::uint64_t runtime_cloud_revision_ = 0;
  std::string saves_root_;                    // per-game save state folders, on the primary drive
  // Save lifecycle around Play: games whose session just ended sync once their
  // folder is known; games syncing now cannot start.
  std::set<GameId> sync_after_session_;
  std::set<GameId> cloud_busy_;
  // Play requested but not yet visible in the runtime's launch state.
  std::set<GameId> play_queued_;
  std::uint64_t plays_requested_ = 0;
  std::optional<std::pair<GameId, runtime::LaunchState>> last_launch_;
  // A game whose title may be running: Cloud must not touch its saves.
  bool SessionActive(GameId id) const;
  std::chrono::steady_clock::time_point volume_refreshed_at_{};
  std::optional<GameId> requested_detail_;
  std::uint64_t requested_generation_ = 0;
  std::unique_ptr<steam::SteamService> service_;
  LibraryService* library_service_ = nullptr;
  std::unique_ptr<app::LibraryController> library_;
  std::unique_ptr<app::PersistenceController> persistence_;
  AuthState established_auth_ = AuthState::SignedOut;
  steam::RequestId request_ = 0;
  std::uint64_t generation_ = 0;
  Phase phase_ = Phase::Idle;
  bool begin_after_clear_ = false;
};
}
