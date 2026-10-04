#pragma once
#include "homebrew/simulation.hpp"
#include "steam/service.h"
#include "app/library_controller.h"
#include "app/persistence_controller.h"
#include "steam/app_details_service.h"
#include <memory>
#include <optional>

namespace playden::homebrew {
// Main-thread bridge. The backend/repository outlive this object. IO stays on service
// workers; the presentation receives no credential tokens.
class Controller {
 public:
  Controller(Simulation& model, steam::AuthBackend& backend, SessionRepository& repository,
             LibraryService* library_service = nullptr, app::PersistenceService* persistence_service = nullptr,
             steam::AppDetailsService* app_details = nullptr);
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
  Simulation& model_;
  steam::AppDetailsService* app_details_ = nullptr;
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
