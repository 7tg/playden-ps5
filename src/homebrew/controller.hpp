#pragma once
#include "homebrew/simulation.hpp"
#include "steam/service.h"
#include <memory>

namespace playden::homebrew {
// Main-thread bridge. The backend/repository outlive this object. All IO stays
// inside SteamService's worker; the presentation receives no credential tokens.
class Controller {
 public:
  Controller(Simulation& model, steam::AuthBackend& backend, SessionRepository& repository);
  ~Controller();
  void Tick();
  void Shutdown();
  Controller(const Controller&) = delete;
  Controller& operator=(const Controller&) = delete;
 private:
  enum class Phase { Idle, Restore, SignIn, SignOut };
  void Begin();
  void SignOut(bool switch_account);
  void Apply(const steam::SteamEvent& event);
  void Update(AuthState state, const std::string& error = {}, bool clear = false,
              const std::string& challenge = {});
  Simulation& model_;
  std::unique_ptr<steam::SteamService> service_;
  steam::RequestId request_ = 0;
  std::uint64_t generation_ = 0;
  Phase phase_ = Phase::Idle;
  bool begin_after_clear_ = false;
};
}
