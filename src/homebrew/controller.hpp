#pragma once
#include "homebrew/simulation.hpp"
#include "homebrew/uninstall_flow.hpp"
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
  // Steam's Windows launch options per game, from app details. A game Playden ships
  // no recipe for is prepared from them; Prepare waits while they are fetched.
  std::map<GameId, std::vector<steam::LaunchOption>> launch_options_;
  std::map<GameId, std::string> launch_options_reason_;
  std::set<GameId> prepare_after_details_;
  // announce: tell the player when this preparation leaves the game ready to play
  // (waiting for the PS5 to register it); the startup check does not.
  void SubmitPrepare(GameId id, bool announce = true);
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
  // Play checks Cloud saves first: games waiting for that check (true once the
  // sync was issued), games whose check passed and can start, games the player
  // may start without syncing after a failed check, and what Play shows meanwhile.
  std::map<GameId, bool> sync_before_play_;
  std::vector<GameId> plays_ready_;
  std::set<GameId> play_without_sync_;
  // Games are prepared without a button press: once their download finishes (games
  // downloading now, then games waiting to be prepared), and when Play finds one not
  // prepared yet. Play then starts it once it is Ready; the refusal seen when Prepare
  // was queued tells a new refusal from an old one.
  std::set<GameId> downloading_;
  std::set<GameId> auto_prepare_;
  struct PlayAfterPrepare {
    bool queued = false;
    bool seen_preparing = false;
    std::string refusal;
    std::chrono::steady_clock::time_point queued_at{};
  };
  std::map<GameId, PlayAfterPrepare> play_after_prepare_;
  // Preparations the player started (Play, Prepare, a finished download): when one
  // ends waiting for the PS5, the model asks whether to quit to Home to play it.
  struct ReadyWatch {
    bool seen_preparing = false;
    std::chrono::steady_clock::time_point submitted_at{};
  };
  std::map<GameId, ReadyWatch> ready_watch_;
  // Titles waiting for the PS5 when Playden started are checked once (query only).
  bool startup_awaiting_known_ = false;
  std::set<GameId> startup_awaiting_;
  // Uninstall order: Cloud saves, then the PS5 title, then the download.
  UninstallFlow uninstalls_;
  void BeginUninstall(GameId id, bool delete_saves);
  void StopUninstall(GameId id, const std::string& reason);
  void TickUninstalls();
  std::uint64_t kept_revision_ = 0;
  std::vector<LiveKeptBottle> kept_projected_;
  std::map<GameId, std::string> play_notes_;
  std::uint64_t play_notes_revision_ = 0;
  std::uint64_t runtime_notes_revision_ = 0;
  std::map<GameId, CloudStatus> cloud_status_;
  // Cloud requests wait for the console to confirm the game's title is stopped:
  // asked once the activity check is queued, answered by a newer serial.
  struct HeldCloudRequest {
    CloudRequest request;
    bool asked = false;
    std::uint64_t after = 0;
  };
  std::vector<HeldCloudRequest> cloud_held_;
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
