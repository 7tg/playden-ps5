#include "homebrew/controller.hpp"
#include "diagnostics/redaction.h"
#include "steam/install_volumes.h"
#include <chrono>

namespace playden::homebrew {
Controller::Controller(Simulation& model, steam::AuthBackend& backend, SessionRepository& repository,
                       LibraryService* library_service, app::PersistenceService* persistence_service,
                       steam::AppDetailsService* app_details, steam::InstallService* installs,
                       steam::CloudService* cloud, RuntimeHost* runtime)
    : model_(model), app_details_(app_details), installs_(installs), cloud_(cloud), runtime_(runtime),
      library_service_(library_service) {
  if (model_.state().simulation_mode) return;
  if(library_service_) library_=std::make_unique<app::LibraryController>(model_,*library_service_);
  service_ = std::make_unique<steam::SteamService>(backend,repository);
  phase_ = Phase::Restore;
  Update(AuthState::Restoring,{},true);
  if(persistence_service) persistence_=std::make_unique<app::PersistenceController>(model_,*persistence_service);
  request_ = service_->RestoreSession();
}
Controller::~Controller() { Shutdown(); }
void Controller::Shutdown() {
  if (!service_) return;
  // Preserve a confirmed sign-out queued by the last input frame before exit.
  for (const auto action : model_.TakeServiceActions()) {
    if ((action == ServiceAction::SignOut || action == ServiceAction::SwitchAccount) &&
        phase_!=Phase::ClearLibrary && phase_!=Phase::ClearOrganization && phase_!=Phase::SignOut) SignOut(false);
  }
  begin_after_clear_=false;
  if(library_service_) {
    library_service_->Shutdown();
    library_->Tick();AdvanceClear();
  }
  if(persistence_) { persistence_->FlushAndShutdown();AdvanceOrganizationClear(); }
  if(app_details_) app_details_->Shutdown();
  if(installs_) installs_->Shutdown();
  if(cloud_) cloud_->Shutdown();
  if(runtime_) runtime_->Shutdown();
  service_.reset();
  persistence_.reset();
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
  if (phase_ == Phase::SignOut || phase_==Phase::ClearLibrary || phase_==Phase::ClearOrganization) { begin_after_clear_ = true; return; }
  if(!model_.state().account.id.empty() && established_auth_==AuthState::Offline) { Restore();return; }
  if(!model_.state().account.id.empty() && established_auth_==AuthState::SignedIn) { SignOut(true);return; }
  if(library_) library_->Reset();
  if(persistence_) persistence_->Reset();
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
  if(phase_==Phase::ClearLibrary || phase_==Phase::ClearOrganization || phase_==Phase::SignOut) return;
  begin_after_clear_ = switch_account;
  request_ = 0;
  if(library_) { phase_=Phase::ClearLibrary;library_->Clear(); }
  else BeginOrganizationClear();
  Update(AuthState::SigningOut);
}
void Controller::AdvanceClear() {
  if(phase_!=Phase::ClearLibrary || !library_) return;
  const auto result=library_->TakeClearResult();
  if(!result) return;
  if(*result==steam::LibraryError::kNone) {
    BeginOrganizationClear();
  } else {
    phase_=Phase::Idle;begin_after_clear_=false;
    Update(model_.state().account.id.empty()?AuthState::Failed:established_auth_,
           "Sign-out failed. Your saved library remains. Retry sign-out in Settings.");
  }
}
void Controller::BeginOrganizationClear() {
  if(persistence_) {phase_=Phase::ClearOrganization;persistence_->Clear();}
  else {phase_=Phase::SignOut;service_->SignOut();}
}
void Controller::AdvanceOrganizationClear() {
  if(phase_!=Phase::ClearOrganization || !persistence_) return;
  const auto result=persistence_->TakeClearResult();
  if(!result) return;
  if(*result==StorageError::kNone) {phase_=Phase::SignOut;service_->SignOut();}
  else {
    phase_=Phase::Idle;begin_after_clear_=false;
    Update(model_.state().account.id.empty()?AuthState::Failed:established_auth_,
           "Sign-out failed. Your saved organization remains. Retry sign-out in Settings.");
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
          if(persistence_) persistence_->Reset();
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
  if(persistence_) { persistence_->Tick();AdvanceOrganizationClear(); }
  TickAppDetails();
  TickStoreDetails();
  TickInstalls();
  TickCloud();
  TickRuntime();
  TickUninstalls();
}
bool Controller::SessionActive(GameId id) const {
  if (!runtime_) return false;
  const auto snapshot = runtime_->Snapshot();
  // Until the runtime has started, a session pending from before a restart is
  // not known yet: no game's saves move.
  if (snapshot.status == RuntimeHostStatus::kStarting) return true;
  if (play_queued_.count(id)) return true;
  if (!snapshot.launch || snapshot.launch->app_id != id) return false;
  using runtime::LaunchState;
  const auto state = snapshot.launch->state;
  return state == LaunchState::kReadyToHandoff || state == LaunchState::kLaunching || state == LaunchState::kRunning;
}
void Controller::TickCloud() {
  if (!cloud_) return;
  auto requests = model_.TakeCloudRequests();
  // A session that just ended syncs its saves, as Steam does after play.
  for (auto& [id, issued] : sync_before_play_)
    if (!issued) {
      requests.push_back({CloudRequestKind::Sync, id});
      issued = true;
    }
  for (const auto id : uninstalls_.TakeSyncRequests()) requests.push_back({CloudRequestKind::Sync, id});
  for (auto it = sync_after_session_.begin(); it != sync_after_session_.end();) {
    if (game_roots_.count(*it) && !saves_root_.empty()) {
      requests.push_back({CloudRequestKind::Sync, *it});
      it = sync_after_session_.erase(it);
    } else {
      ++it;
    }
  }
  for (const auto& request : requests) cloud_held_.push_back({request, false, 0});
  // Saves move only once the console confirms the game's own title is stopped,
  // whoever started it (Playden or the PS5 Home screen). Unknown counts as running.
  std::vector<CloudRequest> confirmed;
  if (!runtime_) {
    for (const auto& held : cloud_held_) confirmed.push_back(held.request);
    cloud_held_.clear();
  } else {
    const auto runtime = runtime_->Snapshot();
    std::vector<HeldCloudRequest> waiting;
    for (auto& held : cloud_held_) {
      const auto id = held.request.game_id;
      if (runtime.status == RuntimeHostStatus::kStarting) { waiting.push_back(held); continue; }
      if (!held.asked) {
        held.asked = true;
        held.after = runtime.activity_serial;
        runtime_->CheckActivity(id);
        waiting.push_back(held);
        continue;
      }
      const auto answer = runtime.activity.find(id);
      if (answer == runtime.activity.end() || answer->second.second <= held.after) { waiting.push_back(held); continue; }
      if (answer->second.first == runtime::NativeActivity::kStopped && !SessionActive(id)) {
        confirmed.push_back(held.request);
        continue;
      }
      // Refused: the title runs, or its state is unknown.
      if (const auto stopped = uninstalls_.CloudRefused(id); stopped.kind == UninstallAction::Kind::kStop)
        StopUninstall(id, stopped.reason);
      if (sync_before_play_.erase(id)) {
        play_notes_[id] = "The game's PS5 title may still be running. Close it, then press Play.";
        ++play_notes_revision_;
      }
    }
    cloud_held_ = std::move(waiting);
  }
  for (const auto& request : confirmed) {
    const auto root = game_roots_.find(request.game_id);
    if (root == game_roots_.end() || saves_root_.empty()) {
      // A pre-uninstall check that cannot run goes straight on: nothing is there to sync.
      uninstalls_.CloudSkipped(request.game_id);
      // A pre-play check that cannot run must not leave Play waiting.
      if (sync_before_play_.erase(request.game_id)) {
        play_notes_.erase(request.game_id);
        ++play_notes_revision_;
        plays_ready_.push_back(request.game_id);
      }
      continue;
    }
    // Busy from now on, before Cloud reports it, so Play cannot slip in.
    cloud_busy_.insert(request.game_id);
    if (request.kind == CloudRequestKind::Sync) uninstalls_.SyncDispatched(request.game_id);
    steam::CloudLocation location{request.game_id, root->second, saves_root_ + "/" + std::to_string(request.game_id)};
    // A prepared game keeps its Windows-folder saves in its private bottle.
    if (runtime_)
      for (const auto& view : runtime_->Snapshot().installs)
        if (view.app_id == request.game_id && view.state == runtime::PreparationState::kReady)
          location.prefix_root = view.prefix_root;
    switch (request.kind) {
      case CloudRequestKind::Sync: cloud_->Sync(location); break;
      case CloudRequestKind::UseLocal: cloud_->Resolve(location, steam::CloudResolution::kUseLocal, true); break;
      case CloudRequestKind::UseCloud: cloud_->Resolve(location, steam::CloudResolution::kUseCloud, true); break;
      case CloudRequestKind::ConfirmAccount: cloud_->Resolve(location, steam::CloudResolution::kAutomatic, true); break;
    }
  }
  const auto snapshot = cloud_->Snapshot();
  if (snapshot.revision == cloud_revision_) return;
  cloud_revision_ = snapshot.revision;
  // Settle Play requests whose Cloud check has finished. Sync marks a game as
  // checking at once, so a finished state seen here is the check's own result.
  const auto settle_uninstall = [this](GameId id, CloudStatus status) {
    const bool waiting = uninstalls_.WaitingForCloud(id);
    const auto action = uninstalls_.CloudSettled(id, status);
    if (action.kind == UninstallAction::Kind::kStop) StopUninstall(id, action.reason);
    else if (waiting && !uninstalls_.WaitingForCloud(id)) ++play_notes_revision_;
  };
  const auto settle = [this, &settle_uninstall](GameId id, CloudStatus status) {
    settle_uninstall(id, status);
    const auto pending = sync_before_play_.find(id);
    if (pending == sync_before_play_.end() || !pending->second) return;
    if (status == CloudStatus::Checking || status == CloudStatus::Syncing) return;
    sync_before_play_.erase(pending);
    ++play_notes_revision_;
    switch (status) {
      case CloudStatus::Synced:
      case CloudStatus::UpToDate:
      case CloudStatus::Unsupported:
        play_notes_.erase(id);
        plays_ready_.push_back(id);
        break;
      case CloudStatus::Conflict:
      case CloudStatus::NeedsAccount:
        play_notes_[id] = "Choose which Cloud saves to keep, then press Play.";
        break;
      default:
        play_notes_[id] = "Cloud saves could not be checked. Press Play again to start without syncing.";
        play_without_sync_.insert(id);
        break;
    }
  };
  std::vector<LiveCloudGame> games;
  for (const auto& game : snapshot.games) {
    CloudStatus status = CloudStatus::None;
    switch (game.report.state) {
      case steam::CloudState::kIdle: status = CloudStatus::None; break;
      case steam::CloudState::kChecking: status = CloudStatus::Checking; break;
      case steam::CloudState::kSyncing: status = CloudStatus::Syncing; break;
      case steam::CloudState::kSynced: status = CloudStatus::Synced; break;
      case steam::CloudState::kUpToDate: status = CloudStatus::UpToDate; break;
      case steam::CloudState::kConflict: status = CloudStatus::Conflict; break;
      case steam::CloudState::kNeedsAccount: status = CloudStatus::NeedsAccount; break;
      case steam::CloudState::kUnsupported: status = CloudStatus::Unsupported; break;
      case steam::CloudState::kFailed: status = CloudStatus::Failed; break;
    }
    const auto& report = game.report;
    if (status == CloudStatus::Checking || status == CloudStatus::Syncing) cloud_busy_.insert(game.id);
    else cloud_busy_.erase(game.id);
    cloud_status_[game.id] = status;
    settle(game.id, status);
    games.push_back({game.id, status, report.message, report.local_files, report.remote_files, report.local_bytes,
                     report.remote_bytes, static_cast<std::int64_t>(report.local_latest),
                     static_cast<std::int64_t>(report.remote_latest)});
  }
  model_.UpdateCloud(games);
}
// Per-game runtime: queue Prepare with the Steam install's location, queue Play,
// and project the runtime host's state for installed games. The worker checks a
// Play request; this thread, the application thread, then starts the title.
void Controller::TickRuntime() {
  if (!runtime_) return;
  {
    // The name each installed game's PS5 title should carry.
    std::map<std::uint32_t, std::string> names;
    for (const auto& [id, root] : game_roots_)
      for (const auto& game : model_.state().games)
        if (game.id == id && !game.title.empty())
          names[id] = model_.state().title_store_suffix ? game.title + " (Steam)" : game.title;
    runtime_->SetTitleNames(std::move(names));
  }
  for (const auto id : model_.TakeRuntimePrepareRequests()) {
    if (!game_roots_.count(id)) continue;
    // Without Steam's launch options yet, ask Steam first; TickAppDetails submits it.
    if (app_details_ && !launch_options_.count(id) && !launch_options_reason_.count(id) &&
        model_.state().account.authentication == AuthState::SignedIn) {
      prepare_after_details_.insert(id);
      app_details_->Request(id, model_.state().account_generation);
      continue;
    }
    SubmitPrepare(id);
  }
  if (model_.TakeRuntimeCleanupRequest()) runtime_->CleanUpRequests();
  for (const auto id : model_.TakeKeptDeleteRequests()) runtime_->DeleteKeptBottle(id);
  {
    const auto current = runtime_->Snapshot();
    if (current.revision != kept_revision_) {
      kept_revision_ = current.revision;
      std::vector<LiveKeptBottle> kept;
      for (const auto& bottle : current.kept_bottles) kept.push_back({bottle.app_id, bottle.name, bottle.kept_at, bottle.bytes});
      if (kept != kept_projected_) { kept_projected_ = kept; model_.UpdateKeptBottles(std::move(kept)); }
    }
  }
  auto plays = model_.TakeRuntimePlayRequests();
  {
    const auto prepared = runtime_->Snapshot();
    const bool usable = prepared.status == RuntimeHostStatus::kReady && prepared.kit_available;
    const auto record_of = [&prepared](GameId id) -> const RuntimeInstallView* {
      for (const auto& view : prepared.installs) if (view.app_id == id) return &view;
      return nullptr;
    };
    const auto ready = [](const RuntimeInstallView* view) {
      return view && view->state == runtime::PreparationState::kReady && !view->recovery_pending &&
             view->title_current && view->admission == "ok";
    };
    const auto refusal_of = [&prepared](GameId id) {
      const auto found = prepared.refusals.find(id);
      return found == prepared.refusals.end() ? std::string{} : found->second;
    };
    // Play on a game that was never prepared prepares it first.
    std::vector<GameId> direct;
    for (const auto id : plays) {
      if (usable && game_roots_.count(id) && !record_of(id) && !play_after_prepare_.count(id)) {
        play_after_prepare_[id] = {};
        auto_prepare_.erase(id);
        ++play_notes_revision_;
      } else if (!play_after_prepare_.count(id)) {
        direct.push_back(id);
      }
    }
    plays = std::move(direct);
    // Finished downloads are prepared one at a time, never over a game that already has a record.
    if (prepared.status != RuntimeHostStatus::kStarting)
      for (auto it = auto_prepare_.begin(); it != auto_prepare_.end();) {
        if (!usable || !game_roots_.count(*it) || record_of(*it)) { it = auto_prepare_.erase(it); continue; }
        if (prepared.preparing_app) break;
        play_after_prepare_.erase(*it);
        model_.RequestRuntimePrepare(*it);
        auto_prepare_.erase(it);
        break;
      }
    // A title still waiting for the PS5 when Playden started is checked once: the
    // player left Playden since, so ShadowMountPlus has usually registered it. The
    // check only asks the console.
    if (usable && !startup_awaiting_known_) {
      startup_awaiting_known_ = true;
      for (const auto& view : prepared.installs)
        if (view.awaiting_registration) startup_awaiting_.insert(view.app_id);
    }
    for (auto it = startup_awaiting_.begin(); it != startup_awaiting_.end();) {
      if (prepared.preparing_app) break;
      if (!game_roots_.count(*it)) { ++it; continue; }
      const auto* record = record_of(*it);
      if (record && record->awaiting_registration) SubmitPrepare(*it, false);
      it = startup_awaiting_.erase(it);
      break;  // one at a time, after the worker takes it
    }
    // A preparation the player started that ends waiting for the PS5: the game is
    // ready to play once Playden quits to Home, so the model offers that.
    for (auto it = ready_watch_.begin(); it != ready_watch_.end();) {
      const auto id = it->first;
      if (prepared.preparing_app == id) { it->second.seen_preparing = true; ++it; continue; }
      const bool unseen_too_long = !it->second.seen_preparing &&
          std::chrono::steady_clock::now() - it->second.submitted_at > std::chrono::seconds(15);
      if (!it->second.seen_preparing && !unseen_too_long) { ++it; continue; }
      const auto* record = record_of(id);
      // Announced once the model has the game's new state (after the projection below).
      if (it->second.seen_preparing && record && record->awaiting_registration) ready_notices_.push_back(id);
      it = ready_watch_.erase(it);
    }
    for (auto it = play_after_prepare_.begin(); it != play_after_prepare_.end();) {
      const auto id = it->first;
      auto& pending = it->second;
      if (!pending.queued) {
        if (prepared.preparing_app && *prepared.preparing_app != id) { ++it; continue; }
        pending.queued = true;
        pending.refusal = refusal_of(id);
        pending.queued_at = std::chrono::steady_clock::now();
        model_.RequestRuntimePrepare(id);
        ++it;
        continue;
      }
      if (prepared.preparing_app == id) { pending.seen_preparing = true; ++it; continue; }
      if (ready(record_of(id))) {
        plays.push_back(id);
        it = play_after_prepare_.erase(it);
        ++play_notes_revision_;
        continue;
      }
      // Prepare ended without a Ready title (the PS5 title row shows why), or was
      // refused before it was seen starting.
      const bool unseen_too_long = !pending.seen_preparing && !prepared.preparing_app &&
          std::chrono::steady_clock::now() - pending.queued_at > std::chrono::seconds(15);
      if (pending.seen_preparing || refusal_of(id) != pending.refusal || !game_roots_.count(id) || unseen_too_long) {
        it = play_after_prepare_.erase(it);
        ++play_notes_revision_;
        continue;
      }
      ++it;
    }
  }
  for (const auto id : plays) {
    if (sync_before_play_.count(id)) continue;
    // Play checks Cloud saves first, as Steam does before a game starts; the
    // game starts once its saves are in step.
    const bool cloud_known = cloud_ && game_roots_.count(id) && !saves_root_.empty();
    if (cloud_known && !play_without_sync_.erase(id)) {
      sync_before_play_[id] = false;
      play_notes_[id] = "Checking Cloud saves before starting…";
      ++play_notes_revision_;
      continue;
    }
    play_notes_.erase(id);
    ++play_notes_revision_;
    plays_ready_.push_back(id);
  }
  std::vector<GameId> later;
  for (const auto id : plays_ready_) {
    // Never start a game while its saves are moving; it starts once they stop.
    if (cloud_busy_.count(id)) { later.push_back(id); continue; }
    play_queued_.insert(id);
    ++plays_requested_;
    runtime_->Play(id);
  }
  plays_ready_ = std::move(later);
  {
    const auto current = runtime_->Snapshot();
    if (current.handoff_ready && current.launch && !cloud_busy_.count(current.launch->app_id))
      runtime_->Handoff();
  }
  const auto snapshot = runtime_->Snapshot();
  // Queued Plays are settled once the worker has handled every request; their
  // sessions or refusals are then visible.
  if (snapshot.plays_handled >= plays_requested_) play_queued_.clear();
  if (snapshot.launch) {
    const std::pair<GameId, runtime::LaunchState> current{snapshot.launch->app_id, snapshot.launch->state};
    if (current.second == runtime::LaunchState::kExited && last_launch_ != current)
      sync_after_session_.insert(current.first);
    last_launch_ = current;
  }
  const auto announce_ready = [this] {
    for (const auto id : ready_notices_) model_.NotifyReadyToPlay(id);
    ready_notices_.clear();
  };
  if (snapshot.revision == runtime_revision_ && install_revision_ == runtime_install_revision_ &&
      cloud_revision_ == runtime_cloud_revision_ && play_notes_revision_ == runtime_notes_revision_) {
    announce_ready();
    return;
  }
  runtime_notes_revision_ = play_notes_revision_;
  runtime_revision_ = snapshot.revision;
  runtime_install_revision_ = install_revision_;
  runtime_cloud_revision_ = cloud_revision_;
  const bool usable = snapshot.status == RuntimeHostStatus::kReady && snapshot.kit_available;
  const auto state = snapshot.status == RuntimeHostStatus::kStarting ? RuntimeState::Checking
                   : snapshot.status == RuntimeHostStatus::kUnavailable ? RuntimeState::Failed
                   : usable ? RuntimeState::Ready : RuntimeState::Unavailable;
  std::vector<LiveRuntimeGame> games;
  for (const auto& [id, root] : game_roots_) {
    LiveRuntimeGame game; game.game_id = id;
    const RuntimeInstallView* record = nullptr;
    for (const auto& view : snapshot.installs) if (view.app_id == id) record = &view;
    const auto refusal = snapshot.refusals.find(id);
    game.has_title = record != nullptr;
    if (snapshot.status == RuntimeHostStatus::kStarting) {
      game.status = "Checking";
    } else if (snapshot.status != RuntimeHostStatus::kReady) {
      game.status = "Unavailable"; game.detail = snapshot.reason;
    } else if (snapshot.removing_app == id) {
      game.status = "Removing"; game.detail = "Removing the game's PS5 title";
    } else if (record && record->state == runtime::PreparationState::kRemoving) {
      game.status = "Removal incomplete"; game.can_prepare = true;
      game.detail = record->failure.empty() ? "Retry finishes removing the PS5 title, then uninstalls the game."
                                            : record->failure;
    } else if (snapshot.preparing_app == id) {
      game.status = "Preparing"; game.detail = record ? PreparationStepWords(record->step) : std::string{};
    } else if (record && record->state == runtime::PreparationState::kReady && !record->recovery_pending &&
               !record->title_current) {
      game.status = "Update available"; game.can_prepare = usable;
      game.detail = "Update gives " + record->title_id + " the current runtime, name and icon";
    } else if (record && record->state == runtime::PreparationState::kReady && !record->recovery_pending &&
               record->admission != "ok") {
      game.status = "Needs repair"; game.can_prepare = usable;
      game.detail = record->title_id + " would refuse to start: " + record->admission;
    } else if (record && record->state == runtime::PreparationState::kReady && !record->recovery_pending) {
      game.status = "Ready"; game.detail = record->title_id; game.ready = true;
    } else if (record && record->awaiting_registration) {
      // Staged and submitted: ShadowMountPlus registers it as soon as Playden quits,
      // so the game is ready to play from Home. Checking only asks the console.
      game.status = "Waiting for the PS5"; game.awaiting_registration = true;
      game.detail = "Waiting for the PS5, which adds the game to Home when Playden quits";
      game.can_prepare = usable;
    } else if (record && record->recovery_pending &&
               (record->step == "register" || record->step == "await-native" || record->step == "query-native")) {
      // The registration was submitted but not confirmed. Checking only asks the
      // console; an uncertain registration is never submitted again.
      game.status = "Registration pending";
      game.detail = "The console has not confirmed " + record->title_id +
                    " yet. Check again only asks the console; Playden never registers a title twice.";
      game.can_prepare = usable;
    } else if (record && record->recovery_pending) {
      game.status = "Needs recovery";
      game.detail = record->failure.empty() ? "Resume continues the saved preparation." : record->failure;
      game.can_prepare = usable;
    } else if (record && (record->state == runtime::PreparationState::kFailed ||
                          record->state == runtime::PreparationState::kUnavailable)) {
      game.status = "Failed"; game.detail = record->failure; game.can_prepare = usable;
    } else {
      game.status = "Not prepared";
      game.detail = usable ? "Play prepares it: a private bottle and the game's own PS5 title" : snapshot.kit_reason;
      game.can_prepare = usable;
    }
    if (refusal != snapshot.refusals.end() && !game.ready) game.detail = refusal->second;
    if (const auto refused = snapshot.play_refusals.find(id); refused != snapshot.play_refusals.end())
      game.play_detail = "Could not start: " + refused->second;
    const bool launching = snapshot.launch && snapshot.launch->app_id == id;
    if (launching) {
      using runtime::LaunchState;
      switch (snapshot.launch->state) {
        case LaunchState::kWaitingForCloud: game.play_status = "Waiting for Cloud"; break;
        case LaunchState::kReadyToHandoff:
        case LaunchState::kLaunching: game.play_status = "Starting…"; break;
        case LaunchState::kRunning: game.play_status = "Playing"; break;
        case LaunchState::kExited: game.play_detail = "The last session ended."; break;
        case LaunchState::kFailed:
          game.play_detail = snapshot.launch->message.empty() ? "The game could not start."
                                                              : "Could not start: " + snapshot.launch->message;
          break;
        case LaunchState::kIdle: break;
      }
    }
    // One launch at a time: Play waits while any session is undecided.
    const bool busy = snapshot.launch && (snapshot.launch->state == runtime::LaunchState::kWaitingForCloud ||
                                          snapshot.launch->state == runtime::LaunchState::kReadyToHandoff ||
                                          snapshot.launch->state == runtime::LaunchState::kLaunching ||
                                          snapshot.launch->state == runtime::LaunchState::kRunning);
    // Updating or repairing waits while this game is being started or played.
    if (launching && busy) game.can_prepare = false;
    game.can_play = game.ready && !busy && snapshot.preparing_app != id && !cloud_busy_.count(id) &&
                    play_queued_.empty() && !sync_before_play_.count(id);
    if (const auto note = play_notes_.find(id); note != play_notes_.end() && !launching) game.play_detail = note->second;
    if (sync_before_play_.count(id)) game.play_status = "Checking saves…";
    if (play_after_prepare_.count(id)) { game.play_status = "Preparing…"; game.can_prepare = false; }
    if (const auto step = uninstalls_.Step(id)) {
      game.uninstall_status = *step == UninstallStep::kSync ? "Syncing saves…"
                            : *step == UninstallStep::kRemoveTitle ? "Removing PS5 title…" : "Uninstalling…";
      game.can_play = false; game.can_prepare = false;
    }
    if (game.ready && cloud_busy_.count(id) && game.play_detail.empty()) game.play_detail = "Waiting for Cloud saves to finish.";
    if (snapshot.work_progress && (snapshot.preparing_app == id || snapshot.removing_app == id))
      game.progress = snapshot.work_progress;
    games.push_back(std::move(game));
  }
  model_.SetGeneratedRecipes(snapshot.generated_recipes);
  model_.UpdateRuntime(state, games);
  model_.SetRuntimeLeftovers(snapshot.leftover_requests);
  announce_ready();
}
void Controller::BeginUninstall(GameId id, bool delete_saves) {
  // Without the runtime there is no PS5 title to remove: the download goes as before.
  if (!runtime_) { if (installs_) installs_->Uninstall(id); return; }
  if (uninstalls_.Begin(id, delete_saves, cloud_ && game_roots_.count(id) && !saves_root_.empty())) ++play_notes_revision_;
}
void Controller::StopUninstall(GameId id, const std::string& reason) {
  ++play_notes_revision_;
  model_.NotifyUninstallStopped(id, reason);
}
void Controller::TickUninstalls() {
  if (!runtime_) return;
  const auto ids = uninstalls_.Active();
  if (ids.empty()) return;
  const auto snapshot = runtime_->Snapshot();
  if (snapshot.status == RuntimeHostStatus::kStarting) return;
  for (const auto id : ids) {
    UninstallFacts facts;
    facts.has_title = std::any_of(snapshot.installs.begin(), snapshot.installs.end(),
                                  [id](const RuntimeInstallView& view) { return view.app_id == id; });
    facts.removing = snapshot.removing_app == id;
    facts.removals_handled = snapshot.removals_handled;
    if (const auto failure = snapshot.removal_failures.find(id); failure != snapshot.removal_failures.end())
      facts.removal_failure = failure->second;
    facts.saves_moving = cloud_busy_.count(id) != 0;
    facts.session_active = SessionActive(id);
    const auto action = uninstalls_.Advance(id, facts);
    switch (action.kind) {
      case UninstallAction::Kind::kNone: break;
      case UninstallAction::Kind::kRemoveTitle: runtime_->Remove(id, action.delete_saves); ++play_notes_revision_; break;
      // The title is gone, so no title points at the files about to be deleted.
      case UninstallAction::Kind::kRemoveDownload: if (installs_) installs_->Uninstall(id); ++play_notes_revision_; break;
      case UninstallAction::Kind::kStop: StopUninstall(id, action.reason); break;
    }
  }
}
void Controller::TickInstalls() {
  if (!installs_) return;
  for (const auto& request : model_.TakeInstallRequests()) {
    switch (request.kind) {
      case InstallRequestKind::Install: installs_->Install(request.game_id); break;
      case InstallRequestKind::Pause: installs_->Pause(request.game_id); break;
      case InstallRequestKind::Resume: installs_->Resume(request.game_id); break;
      case InstallRequestKind::Cancel: installs_->Cancel(request.game_id); break;
      case InstallRequestKind::Uninstall: BeginUninstall(request.game_id, request.delete_saves); break;
      case InstallRequestKind::SetDefaultVolume: installs_->SetDefaultVolume(request.volume_id); break;
    }
  }
  const auto snapshot = installs_->Snapshot();
  game_roots_.clear();
  game_volumes_.clear();
  for (const auto& game : snapshot.installed)
    if (game.available) { game_roots_[game.id] = game.root; game_volumes_[game.id] = game.volume_id; }
  // A download that finished in this session is prepared next, so the game is ready to play.
  std::set<GameId> downloading;
  for (const auto& job : snapshot.jobs) downloading.insert(job.id);
  for (const auto id : downloading_)
    if (!downloading.count(id) && game_roots_.count(id)) auto_prepare_.insert(id);
  downloading_ = std::move(downloading);
  if (!snapshot.volumes.empty()) saves_root_ = snapshot.volumes.front().games_root + "/.playden-saves";
  // Free space changes outside Playden too, so volumes refresh every few seconds as well.
  const auto now = std::chrono::steady_clock::now();
  const bool account_changed = model_.state().account_generation != install_generation_;
  const bool space_due = now - volume_refreshed_at_ >= std::chrono::seconds(10);
  if (snapshot.revision == install_revision_ && !account_changed && !space_due) return;
  {
    std::vector<LiveVolume> volumes;
    for (const auto& volume : snapshot.volumes) {
      LiveVolume live{volume.id, volume.label, volume.available, std::nullopt, std::nullopt};
      if (volume.available) {
        auto space = steam::QueryVolumeSpace(volume.games_root);
        if (!space) {
          const auto slash = volume.games_root.find_last_of('/');
          if (slash != std::string::npos && slash > 0) space = steam::QueryVolumeSpace(volume.games_root.substr(0, slash));
        }
        if (space) { live.total_bytes = space->total_bytes; live.free_bytes = space->free_bytes; }
      }
      volumes.push_back(std::move(live));
    }
    model_.UpdateVolumes(volumes, snapshot.default_volume_id);
    volume_refreshed_at_ = now;
  }
  if (snapshot.revision == install_revision_ && !account_changed) return;
  install_revision_ = snapshot.revision;
  install_generation_ = model_.state().account_generation;
  std::vector<LiveInstallJob> jobs;
  for (const auto& job : snapshot.jobs) {
    JobState state = JobState::Queued;
    switch (job.state) {
      case steam::InstallJobState::kQueued: state = JobState::Queued; break;
      case steam::InstallJobState::kRunning: state = JobState::Downloading; break;
      case steam::InstallJobState::kPaused: state = JobState::Paused; break;
      case steam::InstallJobState::kFailed: state = JobState::Failed; break;
    }
    jobs.push_back({job.id, state, job.done_bytes, job.total_bytes, job.failure, job.volume_id});
  }
  std::vector<LiveInstalledGame> installed;
  for (const auto& game : snapshot.installed) installed.push_back({game.id, game.install_bytes, game.volume_id, game.available});
  model_.UpdateInstalls(jobs, installed);
}
void Controller::TickStoreDetails() {
  if (!store_details_) return;
  // The store is public: a game's description is asked for when its details open.
  const auto& detail = model_.state().navigation.detail_id;
  if (detail && detail != requested_store_detail_) {
    requested_store_detail_ = detail;
    store_details_->Request(*detail);
  }
  for (auto& details : store_details_->Pump())
    model_.UpdateStoreDetails(details.id, std::move(details.description), std::move(details.genres),
                              std::move(details.controller_support));
}
void Controller::TickAppDetails() {
  if (!app_details_) return;
  const auto& state = model_.state();
  const auto generation = state.account_generation;
  if (generation != requested_generation_) {
    app_details_->Invalidate(generation);
    requested_generation_ = generation;
    requested_detail_.reset();
    launch_options_.clear();
    launch_options_reason_.clear();
    prepare_after_details_.clear();
  }
  // Fetch Steam app info when the player opens a game's details while signed in.
  if (state.account.authentication == AuthState::SignedIn && state.navigation.detail_id &&
      state.navigation.detail_id != requested_detail_) {
    requested_detail_ = state.navigation.detail_id;
    app_details_->Request(*state.navigation.detail_id, generation);
  }
  for (const auto& details : app_details_->Pump()) {
    if (details.account_generation != generation) continue;
    if (details.available) {
      model_.UpdateAppDetails(details.id, details.download_bytes, details.install_bytes, details.windows_installable);
      launch_options_[details.id] = details.launches;
      launch_options_reason_.erase(details.id);
    } else if (!launch_options_.count(details.id)) {
      launch_options_reason_[details.id] = details.reason.empty() ? "Steam details are unavailable right now." : details.reason;
    }
    if (prepare_after_details_.erase(details.id) && runtime_) SubmitPrepare(details.id);
  }
}
void Controller::SubmitPrepare(GameId id, bool announce) {
  const auto root = game_roots_.find(id);
  if (!runtime_ || root == game_roots_.end()) return;
  if (announce) ready_watch_[id] = {false, std::chrono::steady_clock::now()};
  else ready_watch_.erase(id);
  RuntimePrepareRequest request{id, root->second, game_volumes_[id], {}};
  for (const auto& game : model_.state().games) if (game.id == id) request.title_name = game.title;
  if (model_.state().title_store_suffix && !request.title_name.empty()) request.title_name += " (Steam)";
  if (const auto found = launch_options_.find(id); found != launch_options_.end())
    for (const auto& option : found->second)
      request.launches.push_back({"", option.description, option.executable, option.working_directory, option.arguments});
  if (const auto reason = launch_options_reason_.find(id); reason != launch_options_reason_.end())
    request.launch_reason = "Playden could not read how to start this game from Steam: " + reason->second;
  else if (!launch_options_.count(id))
    request.launch_reason = "Sign in to Steam so Playden can read how to start this game.";
  // After a failed answer, ask Steam again so a later Prepare can use the options.
  if (app_details_ && !launch_options_.count(id)) app_details_->Request(id, model_.state().account_generation);
  runtime_->Prepare(std::move(request));
}
void Controller::Apply(const steam::SteamEvent& event) {
  if (event.generation < generation_) return;
  if (phase_ == Phase::SignOut) {
    if (event.kind != steam::SteamEventKind::kSignedOut && event.kind != steam::SteamEventKind::kFailure) return;
  } else if (phase_ == Phase::Idle || phase_==Phase::ClearLibrary || phase_==Phase::ClearOrganization || event.request_id != request_) return;
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
      if(persistence_) persistence_->Activate(event.steam_id);
      if(runtime_) runtime_->SetSteamAccount(event.account_name, event.steam_id);
      if(library_) library_->Activate(event.steam_id,offline);
      break;
    }
    case steam::SteamEventKind::kSignedOut: {
      phase_ = Phase::Idle;
      established_auth_=AuthState::SignedOut;
      if(library_) library_->Reset();
      if(persistence_) persistence_->Reset();
      if(runtime_) runtime_->SetSteamAccount({}, 0);
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
      if(!clearing) { established_auth_=AuthState::SignedOut;if(library_)library_->Reset();if(persistence_)persistence_->Reset(); }
      if (clearing) Update(model_.state().account.id.empty() ? AuthState::Failed : established_auth_,
                           "Sign-out failed. Your saved account remains. Retry sign-out in Settings.");
      else if (restoring && event.error == steam::AuthError::kNoSession) Update(AuthState::SignedOut,{},true);
      else if (restoring && (event.error == steam::AuthError::kRejected || event.error == steam::AuthError::kCredentialsRevoked)) {
        // Steam no longer accepts the saved session: show a new QR code right away
        // instead of a failure the player can only retry.
        Update(AuthState::SignedOut,{},true);
        Begin();
      }
      else if (event.error == steam::AuthError::kCanceled) Update(AuthState::Cancelled,{},true);
      else if (event.error == steam::AuthError::kExpired) Update(AuthState::Expired,diagnostics::AuthMessage(event.error),true);
      else Update(AuthState::Failed,diagnostics::AuthMessage(event.error),true);
      break;
    }
  }
}
}
