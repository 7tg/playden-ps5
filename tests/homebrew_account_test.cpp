#include "homebrew/controller.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

using namespace playden;
using namespace playden::homebrew;
using namespace playden::steam;
using namespace std::chrono_literals;
namespace {
StoredSession Session() { return {12345,"Synthetic account","fixture-refresh-secret","fixture-access-secret",{}}; }
class Repository final : public SessionRepository {
 public:
  mutable std::mutex mutex;
  mutable std::optional<StoredSession> value;
  mutable int clears = 0;
  bool fail_load = false, fail_clear = false;
  Result<std::optional<StoredSession>> Load() const override {
    std::lock_guard<std::mutex> lock(mutex);
    if (fail_load) return Result<std::optional<StoredSession>>::Failure(StorageError::kReadFailed,"fixture failure");
    return Result<std::optional<StoredSession>>::Success(value);
  }
  Result<void> Save(const StoredSession& s) const override {
    std::lock_guard<std::mutex> lock(mutex); value=s; return Result<void>::Success();
  }
  Result<void> Clear() const override {
    std::lock_guard<std::mutex> lock(mutex); ++clears;
    if (fail_clear) return Result<void>::Failure(StorageError::kRemoveFailed,"fixture failure");
    value.reset(); return Result<void>::Success();
  }
};
class Backend final : public AuthBackend {
 public:
  std::atomic<int> calls{0};
  std::atomic<bool> approve{false};
  std::atomic<bool> replace{false};
  bool ignore_cancel = false;
  AuthError error = AuthError::kNone;
  AuthResult AuthenticateQr(const std::atomic<bool>& cancel,const ChallengeCallback& cb) override {
    ++calls; cb("https://s.team/q/synthetic-first");
    bool sent = false;
    const auto deadline = std::chrono::steady_clock::now()+3s;
    while (!approve && (ignore_cancel || !cancel) && std::chrono::steady_clock::now()<deadline) {
      if (replace && !sent) { sent=true; cb("https://s.team/q/synthetic-replaced"); }
      std::this_thread::sleep_for(1ms);
    }
    if (cancel && !ignore_cancel) return {AuthError::kCanceled,{}};
    return {error,error == AuthError::kNone ? std::optional<StoredSession>(Session()) : std::nullopt};
  }
  AuthResult Restore(const StoredSession& s,const std::atomic<bool>&) override { return {error,error == AuthError::kNone ? std::optional<StoredSession>(s) : std::nullopt}; }
};
void Wait(Controller& controller,const std::function<bool()>& predicate) {
  const auto deadline = std::chrono::steady_clock::now()+4s;
  do { controller.Tick(); if (predicate()) return; std::this_thread::sleep_for(1ms); } while (std::chrono::steady_clock::now()<deadline);
  assert(false && "controller event deadline exceeded");
}
void Choose(Simulation& model,const std::string& action) {
  const auto& rows=model.view().rows;
  const auto found=std::find_if(rows.begin(),rows.end(),[&](const ViewRow& row){return row.action==action;});
  assert(found!=rows.end());
  const auto index=static_cast<int>(found-rows.begin());
  for (int i=0; i<80 && model.view().focused_row!=index; ++i)
    assert(model.action(model.view().focused_row<index?"down":"up"));
  assert(model.view().focused_row==index); assert(model.action("cross"));
}
void OpenSettings(Simulation& model) {
  for (int i=0; i<4 && model.state().navigation.route!=Route::Settings; ++i) model.action("r1");
  assert(model.state().navigation.route==Route::Settings);
}
void SignOut(Simulation& model) { OpenSettings(model); Choose(model,"signout-confirm"); Choose(model,"confirm-destructive"); }
void Connect(Simulation& model) { Choose(model,"connect"); }
void ChallengeAndCancel() {
  Simulation model({}, {},ModelMode::Live); Repository repo; Backend backend; backend.ignore_cancel=true;
  Controller controller(model,backend,repo);
  Wait(controller,[&]{return model.state().account.authentication==AuthState::SignedOut;});
  assert(model.state().games.empty() && backend.calls==0);
  Connect(model); Wait(controller,[&]{return !model.state().account.challenge_url.empty();});
  backend.replace=true;
  Wait(controller,[&]{return model.state().account.challenge_url.find("replaced")!=std::string::npos;});
  assert(model.snapshot().find("s.team")==std::string::npos);
  Choose(model,"qr-cancel"); controller.Tick(); backend.approve=true;
  Wait(controller,[&]{return model.state().account.authentication==AuthState::Cancelled;});
  controller.Shutdown();
  assert(!repo.Load().value() && model.state().account.challenge_url.empty());
}
void AuthenticationRestoreAndSignOut() {
  Repository repo;
  {
    Simulation model({}, {},ModelMode::Live); Backend backend; Controller controller(model,backend,repo);
    Wait(controller,[&]{return model.state().account.authentication==AuthState::SignedOut;});
    Connect(model); Wait(controller,[&]{return !model.state().account.challenge_url.empty();});
    backend.approve=true;
    Wait(controller,[&]{return model.state().account.authentication==AuthState::SignedIn;});
    assert(model.state().account.id=="12345" && model.state().account.challenge_url.empty());
    assert(model.state().games.empty() && model.state().jobs.empty());
    assert(!model.state().account.cache_available);
    assert(model.action("account-sync"));
    controller.Tick();
    assert(model.state().account.authentication==AuthState::SignedIn);
    assert(!model.state().library_syncing && !model.state().library_loading && !model.state().account.error.empty());
    assert(model.snapshot().find("fixture-refresh-secret")==std::string::npos);
    assert(model.snapshot().find("Synthetic account")==std::string::npos);
  }
  {
    Simulation model({}, {},ModelMode::Live); Backend backend; Controller controller(model,backend,repo);
    Wait(controller,[&]{return model.state().account.authentication==AuthState::SignedIn;});
    assert(backend.calls==0);
    SignOut(model); controller.Tick();
    Wait(controller,[&]{return model.state().account.authentication==AuthState::SignedOut;});
    assert(!repo.Load().value() && model.state().account.id.empty());
  }
}
void FailedClearDoesNotSwitch() {
  Simulation model({}, {},ModelMode::Live); Repository repo; repo.value=Session(); repo.fail_clear=true;
  Backend backend; Controller controller(model,backend,repo);
  Wait(controller,[&]{return model.state().account.authentication==AuthState::SignedIn;});
  OpenSettings(model); Choose(model,"account"); Choose(model,"account-switch"); Choose(model,"confirm-destructive");
  controller.Tick(); Wait(controller,[&]{return model.state().account.authentication==AuthState::SignedIn && !model.state().account.error.empty();});
  assert(backend.calls==0 && repo.Load().value() && !model.state().account.error.empty());
  assert(model.state().account.id=="12345" && model.state().account.display_name=="Synthetic account");
  assert(model.state().navigation.route==Route::Settings);
  assert(model.view().subtitle.find("Sign-out failed")!=std::string::npos);
  assert(std::none_of(model.view().rows.begin(),model.view().rows.end(),[](const ViewRow& row){return row.action=="qr-retry";}));
  repo.fail_clear=false;
  SignOut(model); controller.Tick();
  Wait(controller,[&]{return model.state().account.authentication==AuthState::SignedOut;});
  assert(backend.calls==0 && !repo.Load().value());
}
void ExitHonorsClearWithoutStartingNewQr() {
  Simulation model({}, {},ModelMode::Live); Repository repo; repo.value=Session(); Backend backend;
  Controller controller(model,backend,repo);
  Wait(controller,[&]{return model.state().account.authentication==AuthState::SignedIn;});
  OpenSettings(model); Choose(model,"account"); Choose(model,"account-switch"); Choose(model,"confirm-destructive");
  controller.Shutdown();
  assert(backend.calls==0 && !repo.Load().value());
}
void StorageFailureAndRevocation() {
  for (const bool corrupt : {false,true}) {
    Simulation model({}, {},ModelMode::Live); Repository repo; repo.value=Session(); repo.fail_load=corrupt;
    Backend backend; backend.error=AuthError::kCredentialsRevoked;
    Controller controller(model,backend,repo);
    Wait(controller,[&]{return model.state().account.authentication==AuthState::Failed;});
    assert(model.state().account.challenge_url.empty() && model.state().account.id.empty());
    std::lock_guard<std::mutex> lock(repo.mutex); assert(corrupt?repo.value.has_value():!repo.value.has_value());
  }
}
}
int main() { ChallengeAndCancel(); AuthenticationRestoreAndSignOut(); FailedClearDoesNotSwitch(); ExitHonorsClearWithoutStartingNewQr(); StorageFailureAndRevocation(); }
