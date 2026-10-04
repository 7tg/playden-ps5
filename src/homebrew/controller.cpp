#include "homebrew/controller.hpp"
#include "diagnostics/redaction.h"
#include <chrono>

namespace playden::homebrew {
Controller::Controller(Simulation& model, steam::AuthBackend& backend, SessionRepository& repository)
    : model_(model) {
  if (model_.state().simulation_mode) return;
  service_ = std::make_unique<steam::SteamService>(backend,repository);
  phase_ = Phase::Restore;
  Update(AuthState::Restoring,{},true);
  request_ = service_->RestoreSession();
}
Controller::~Controller() { Shutdown(); }
void Controller::Shutdown() {
  if (!service_) return;
  // Preserve a confirmed sign-out queued by the last input frame before exit.
  for (const auto action : model_.TakeServiceActions()) {
    if (action == ServiceAction::SignOut || action == ServiceAction::SwitchAccount)
      service_->SignOut();
  }
  service_.reset();
  phase_ = Phase::Idle;
  Update(AuthState::SignedOut,{},true);
}
void Controller::Update(AuthState state,const std::string& error,bool clear,const std::string& challenge) {
  AccountUpdate update;
  update.authentication = state;
  update.id = clear ? std::string{} : model_.state().account.id;
  update.display_name = clear ? std::string{} : model_.state().account.display_name;
  update.error = error; update.challenge_url = challenge; update.clear_library = clear;
  model_.UpdateAccount(std::move(update));
}
void Controller::Begin() {
  if (phase_ == Phase::SignOut) { begin_after_clear_ = true; return; }
  phase_ = Phase::SignIn;
  request_ = service_->BeginQrSignIn();
  Update(AuthState::Waiting,{},true);
}
void Controller::SignOut(bool switch_account) {
  begin_after_clear_ = switch_account;
  phase_ = Phase::SignOut;
  request_ = 0;
  service_->SignOut();
  Update(AuthState::SigningOut);
}
void Controller::Tick() {
  if (!service_) return;
  model_.SetWallClock(std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
  for (const auto action : model_.TakeServiceActions()) {
    switch (action) {
      case ServiceAction::BeginQr: Begin(); break;
      case ServiceAction::CancelQr:
        if (phase_ == Phase::SignIn || phase_ == Phase::Restore) {
          service_->Cancel(request_);
          Update(AuthState::Cancelled,{},true);
        }
        break;
      case ServiceAction::SignOut: SignOut(false); break;
      case ServiceAction::SwitchAccount: SignOut(true); break;
      case ServiceAction::RefreshLibrary:
        // Ownership retrieval is the next implementation stage. Never pretend
        // that a tick completed a sync or synthesize a successful empty result.
        if (phase_ == Phase::Idle && !model_.state().account.id.empty())
          model_.UpdateLibrary({{},false,"Library sync is unavailable. Try again later.",
                                model_.state().library_offline,model_.state().account.cache_available,{}});
        break;
    }
  }
  for (const auto& event : service_->Pump(16)) Apply(event);
}
void Controller::Apply(const steam::SteamEvent& event) {
  if (event.generation < generation_) return;
  if (phase_ == Phase::SignOut) {
    if (event.kind != steam::SteamEventKind::kSignedOut && event.kind != steam::SteamEventKind::kFailure) return;
  } else if (phase_ == Phase::Idle || event.request_id != request_) return;
  generation_ = event.generation;
  switch (event.kind) {
    case steam::SteamEventKind::kChallenge:
      if (phase_ == Phase::SignIn) Update(AuthState::Waiting,{},false,event.challenge_url);
      break;
    case steam::SteamEventKind::kAwaitingApproval:
      break;  // The preceding challenge remains on screen; do not erase it.
    case steam::SteamEventKind::kAuthenticated: {
      phase_ = Phase::Idle;
      AccountUpdate update;
      update.authentication = AuthState::SignedIn;
      update.id = std::to_string(event.steam_id); update.display_name = event.account_name;
      update.clear_library = true;
      update.error = "Library sync is unavailable. Try again later.";
      model_.UpdateAccount(std::move(update));
      break;
    }
    case steam::SteamEventKind::kSignedOut: {
      phase_ = Phase::Idle;
      const bool begin = begin_after_clear_; begin_after_clear_ = false;
      Update(AuthState::SignedOut,{},true);
      if (begin) Begin();
      break;
    }
    case steam::SteamEventKind::kCanceled:
      phase_ = Phase::Idle; Update(AuthState::Cancelled,{},true); break;
    case steam::SteamEventKind::kFailure: {
      const bool restoring = phase_ == Phase::Restore;
      const bool clearing = phase_ == Phase::SignOut;
      phase_ = Phase::Idle; begin_after_clear_ = false;
      if (clearing) Update(model_.state().account.id.empty() ? AuthState::Failed : AuthState::SignedIn,
                           "Sign-out failed. Your saved account remains. Retry sign-out in Settings.");
      else if (restoring && event.error == steam::AuthError::kNoSession) Update(AuthState::SignedOut,{},true);
      else if (event.error == steam::AuthError::kCanceled) Update(AuthState::Cancelled,{},true);
      else if (event.error == steam::AuthError::kExpired) Update(AuthState::Expired,diagnostics::AuthMessage(event.error),true);
      else Update(AuthState::Failed,diagnostics::AuthMessage(event.error),true);
      break;
    }
  }
}
}
