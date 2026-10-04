#include "homebrew/controller.hpp"
#include "diagnostics/redaction.h"
#include <chrono>

namespace playden::homebrew {
Controller::Controller(Simulation& model, steam::AuthBackend& backend, SessionRepository& repository,
                       LibraryService* library_service)
    : model_(model), library_service_(library_service) {
  if (model_.state().simulation_mode) return;
  if(library_service_) library_=std::make_unique<app::LibraryController>(model_,*library_service_);
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
    if ((action == ServiceAction::SignOut || action == ServiceAction::SwitchAccount) &&
        phase_!=Phase::ClearLibrary && phase_!=Phase::SignOut) SignOut(false);
  }
  begin_after_clear_=false;
  if(library_service_) {
    library_service_->Shutdown();
    library_->Tick();AdvanceClear();
  }
  service_.reset();
  library_.reset();
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
  if (phase_ == Phase::SignOut || phase_==Phase::ClearLibrary) { begin_after_clear_ = true; return; }
  if(!model_.state().account.id.empty() && established_auth_==AuthState::Offline) { Restore();return; }
  if(!model_.state().account.id.empty() && established_auth_==AuthState::SignedIn) { SignOut(true);return; }
  if(library_) library_->Reset();
  phase_ = Phase::SignIn;
  request_ = service_->BeginQrSignIn();
  Update(AuthState::Waiting,{},true);
}
void Controller::Restore() {
  if(library_) library_->Reset();
  phase_=Phase::Restore;request_=service_->RestoreSession();
  Update(AuthState::Restoring);
}
void Controller::SignOut(bool switch_account) {
  if(phase_==Phase::ClearLibrary || phase_==Phase::SignOut) return;
  begin_after_clear_ = switch_account;
  request_ = 0;
  if(library_) { phase_=Phase::ClearLibrary;library_->Clear(); }
  else { phase_=Phase::SignOut;service_->SignOut(); }
  Update(AuthState::SigningOut);
}
void Controller::AdvanceClear() {
  if(phase_!=Phase::ClearLibrary || !library_) return;
  const auto result=library_->TakeClearResult();
  if(!result) return;
  if(*result==steam::LibraryError::kNone) {
    phase_=Phase::SignOut;service_->SignOut();
  } else {
    phase_=Phase::Idle;begin_after_clear_=false;
    Update(model_.state().account.id.empty()?AuthState::Failed:established_auth_,
           "Sign-out failed. Your saved library remains. Retry sign-out in Settings.");
  }
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
          if(library_) library_->Reset();
          Update(AuthState::Cancelled,{},true);
        }
        break;
      case ServiceAction::SignOut: SignOut(false); break;
      case ServiceAction::SwitchAccount: SignOut(true); break;
      case ServiceAction::RefreshLibrary:
        if (phase_ == Phase::Idle && !model_.state().account.id.empty()) {
          if(library_) {
            if(established_auth_==AuthState::Offline || library_->NeedsRenewal()) Restore();
            else library_->Refresh();
          } else model_.UpdateLibrary({{},false,"Library sync is unavailable. Try again later.",
                                        model_.state().library_offline,model_.state().account.cache_available,{}});
        }
        break;
    }
  }
  for (const auto& event : service_->Pump(16)) Apply(event);
  if(library_) { library_->Tick();AdvanceClear(); }
}
void Controller::Apply(const steam::SteamEvent& event) {
  if (event.generation < generation_) return;
  if (phase_ == Phase::SignOut) {
    if (event.kind != steam::SteamEventKind::kSignedOut && event.kind != steam::SteamEventKind::kFailure) return;
  } else if (phase_ == Phase::Idle || phase_==Phase::ClearLibrary || event.request_id != request_) return;
  generation_ = event.generation;
  switch (event.kind) {
    case steam::SteamEventKind::kChallenge:
      if (phase_ == Phase::SignIn) Update(AuthState::Waiting,{},false,event.challenge_url);
      break;
    case steam::SteamEventKind::kAwaitingApproval:
      break;  // The preceding challenge remains on screen; do not erase it.
    case steam::SteamEventKind::kOfflineSession:
    case steam::SteamEventKind::kAuthenticated: {
      phase_ = Phase::Idle;
      const bool offline=event.kind==steam::SteamEventKind::kOfflineSession;
      established_auth_=offline?AuthState::Offline:AuthState::SignedIn;
      AccountUpdate update;
      update.authentication = established_auth_;
      update.id = std::to_string(event.steam_id); update.display_name = event.account_name;
      update.clear_library = model_.state().account.id!=update.id;
      if(!library_) update.error = "Library sync is unavailable. Try again later.";
      model_.UpdateAccount(std::move(update));
      if(library_) library_->Activate(event.steam_id,offline);
      break;
    }
    case steam::SteamEventKind::kSignedOut: {
      phase_ = Phase::Idle;
      established_auth_=AuthState::SignedOut;
      if(library_) library_->Reset();
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
      if(!clearing) { established_auth_=AuthState::SignedOut;if(library_)library_->Reset(); }
      if (clearing) Update(model_.state().account.id.empty() ? AuthState::Failed : established_auth_,
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
