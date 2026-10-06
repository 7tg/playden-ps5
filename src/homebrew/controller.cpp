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
  TickInstalls();
  TickCloud();
  TickRuntime();
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
  for (auto it = sync_after_session_.begin(); it != sync_after_session_.end();) {
    if (game_roots_.count(*it) && !saves_root_.empty()) {
      requests.push_back({CloudRequestKind::Sync, *it});
      it = sync_after_session_.erase(it);
    } else {
      ++it;
    }
  }
  for (const auto& request : requests) {
    const auto root = game_roots_.find(request.game_id);
    if (root == game_roots_.end() || saves_root_.empty()) continue;  // only installed games sync
    // Never transfer saves while the game's own title may be writing them.
    if (SessionActive(request.game_id)) continue;
    // Busy from now on, before Cloud reports it, so Play cannot slip in.
    cloud_busy_.insert(request.game_id);
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
  const auto settle = [this](GameId id, CloudStatus status) {
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
  for (const auto id : model_.TakeRuntimePrepareRequests()) {
    const auto root = game_roots_.find(id);
    if (root != game_roots_.end()) runtime_->Prepare({id, root->second, game_volumes_[id]});
  }
  auto plays = model_.TakeRuntimePlayRequests();
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
  for (const auto id : plays_ready_) {
    // Never start a game while its saves are moving.
    if (cloud_busy_.count(id)) continue;
    play_queued_.insert(id);
    ++plays_requested_;
    runtime_->Play(id);
  }
  plays_ready_.clear();
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
  if (snapshot.revision == runtime_revision_ && install_revision_ == runtime_install_revision_ &&
      cloud_revision_ == runtime_cloud_revision_ && play_notes_revision_ == runtime_notes_revision_) return;
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
    if (snapshot.status == RuntimeHostStatus::kStarting) {
      game.status = "Checking";
    } else if (snapshot.status != RuntimeHostStatus::kReady) {
      game.status = "Unavailable"; game.detail = snapshot.reason;
    } else if (snapshot.preparing_app == id) {
      game.status = "Preparing"; game.detail = record ? record->step : std::string{};
    } else if (record && record->state == runtime::PreparationState::kReady && !record->recovery_pending &&
               !record->title_current) {
      game.status = "Update available"; game.can_prepare = usable;
      game.detail = record->title_id + " was made from an earlier kit";
    } else if (record && record->state == runtime::PreparationState::kReady && !record->recovery_pending &&
               record->admission != "ok") {
      game.status = "Needs repair"; game.can_prepare = usable;
      game.detail = record->title_id + " would refuse to start: " + record->admission;
    } else if (record && record->state == runtime::PreparationState::kReady && !record->recovery_pending) {
      game.status = "Ready"; game.detail = record->title_id; game.ready = true;
    } else if (record && record->recovery_pending) {
      game.status = "Needs recovery";
      game.detail = record->failure.empty() ? "Resume continues the saved preparation." : record->failure;
      game.can_prepare = usable;
    } else if (record && (record->state == runtime::PreparationState::kFailed ||
                          record->state == runtime::PreparationState::kUnavailable)) {
      game.status = "Failed"; game.detail = record->failure; game.can_prepare = usable;
    } else {
      game.status = "Not prepared";
      game.detail = usable ? "Creates a private bottle and the game's own PS5 title" : snapshot.kit_reason;
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
    game.can_play = game.ready && !busy && snapshot.preparing_app != id && !cloud_busy_.count(id) &&
                    play_queued_.empty() && !sync_before_play_.count(id);
    if (const auto note = play_notes_.find(id); note != play_notes_.end() && !launching) game.play_detail = note->second;
    if (sync_before_play_.count(id)) game.play_status = "Checking saves…";
    if (game.ready && cloud_busy_.count(id) && game.play_detail.empty()) game.play_detail = "Waiting for Cloud saves to finish.";
    games.push_back(std::move(game));
  }
  model_.UpdateRuntime(state, games);
}
void Controller::TickInstalls() {
  if (!installs_) return;
  for (const auto& request : model_.TakeInstallRequests()) {
    switch (request.kind) {
      case InstallRequestKind::Install: installs_->Install(request.game_id); break;
      case InstallRequestKind::Pause: installs_->Pause(request.game_id); break;
      case InstallRequestKind::Resume: installs_->Resume(request.game_id); break;
      case InstallRequestKind::Cancel: installs_->Cancel(request.game_id); break;
      case InstallRequestKind::Uninstall: installs_->Uninstall(request.game_id); break;
      case InstallRequestKind::SetDefaultVolume: installs_->SetDefaultVolume(request.volume_id); break;
    }
  }
  const auto snapshot = installs_->Snapshot();
  game_roots_.clear();
  game_volumes_.clear();
  for (const auto& game : snapshot.installed)
    if (game.available) { game_roots_[game.id] = game.root; game_volumes_[game.id] = game.volume_id; }
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
void Controller::TickAppDetails() {
  if (!app_details_) return;
  const auto& state = model_.state();
  const auto generation = state.account_generation;
  if (generation != requested_generation_) {
    app_details_->Invalidate(generation);
    requested_generation_ = generation;
    requested_detail_.reset();
  }
  // Fetch Steam app info when the player opens a game's details while signed in.
  if (state.account.authentication == AuthState::SignedIn && state.navigation.detail_id &&
      state.navigation.detail_id != requested_detail_) {
    requested_detail_ = state.navigation.detail_id;
    app_details_->Request(*state.navigation.detail_id, generation);
  }
  for (const auto& details : app_details_->Pump()) {
    if (details.account_generation != generation) continue;
    if (details.available)
      model_.UpdateAppDetails(details.id, details.download_bytes, details.install_bytes, details.windows_installable);
  }
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
      if(library_) library_->Activate(event.steam_id,offline);
      break;
    }
    case steam::SteamEventKind::kSignedOut: {
      phase_ = Phase::Idle;
      established_auth_=AuthState::SignedOut;
      if(library_) library_->Reset();
      if(persistence_) persistence_->Reset();
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
      else if (event.error == steam::AuthError::kCanceled) Update(AuthState::Cancelled,{},true);
      else if (event.error == steam::AuthError::kExpired) Update(AuthState::Expired,diagnostics::AuthMessage(event.error),true);
      else Update(AuthState::Failed,diagnostics::AuthMessage(event.error),true);
      break;
    }
  }
}
}
