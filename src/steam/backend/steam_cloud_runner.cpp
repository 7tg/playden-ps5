#include "steam/backend/steam_cloud_runner.h"

namespace playden::steam {
namespace {

CloudReport Report(const cloud::CloudPlan& plan) {
  CloudReport report;
  switch (plan.status) {
    case cloud::PlanStatus::kUpToDate: report.state = CloudState::kUpToDate; break;
    case cloud::PlanStatus::kReady: report.state = CloudState::kSyncing; break;
    case cloud::PlanStatus::kConflict: report.state = CloudState::kConflict; break;
    case cloud::PlanStatus::kNeedsAccount: report.state = CloudState::kNeedsAccount; break;
    case cloud::PlanStatus::kUnsupported: report.state = CloudState::kUnsupported; break;
    case cloud::PlanStatus::kFailed: report.state = CloudState::kFailed; break;
  }
  report.message = plan.message;
  report.downloads = plan.downloads;
  report.uploads = plan.uploads;
  report.conflicts = plan.conflicts;
  report.local_files = plan.local_files;
  report.remote_files = plan.remote_files;
  report.local_bytes = plan.local_bytes;
  report.remote_bytes = plan.remote_bytes;
  report.local_latest = plan.local_latest;
  report.remote_latest = plan.remote_latest;
  return report;
}

const char* StateName(CloudState state) {
  switch (state) {
    case CloudState::kSyncing: return "ready";
    case CloudState::kSynced: return "synced";
    case CloudState::kUpToDate: return "up-to-date";
    case CloudState::kConflict: return "conflict";
    case CloudState::kNeedsAccount: return "needs-account";
    case CloudState::kUnsupported: return "unsupported";
    case CloudState::kFailed: return "failed";
    default: return "other";
  }
}

}  // namespace

SteamCloudRunner::SteamCloudRunner(CmClient& cm, HttpClient& http) : sync_(cm, http) {}

void SteamCloudRunner::SetLog(std::function<void(GameId app, const char* event, const std::string& message)> log) {
  log_ = std::move(log);
  sync_.SetTrace([this](const char* step) {
    if (log_) log_(current_app_, "trace", step);
  });
}

SteamCloudRunner::SteamCloudRunner(cloud::CloudHooks hooks) : sync_(std::move(hooks)) {}

cloud::GameSaveLocation SteamCloudRunner::SaveLocation(const CloudLocation& location) {
  return {location.id, location.game_root,
          location.prefix_root.empty() ? location.state_root + "/prefix" : location.prefix_root,
          location.state_root};
}

CloudReport SteamCloudRunner::Check(const StoredSession& session, const CloudLocation& location,
                                    const std::atomic<bool>& cancel) {
  current_app_ = location.id;
  auto plan = sync_.Check(session, SaveLocation(location), cancel);
  auto report = Report(plan);
  if (log_) log_(location.id, (std::string("check-") + StateName(report.state)).c_str(), report.message);
  plans_[location.id] = {std::move(plan), SaveLocation(location)};
  return report;
}

CloudReport SteamCloudRunner::Apply(const StoredSession& session, const CloudLocation& location, CloudResolution resolution,
                                    bool confirm_account, const std::atomic<bool>& cancel) {
  const auto found = plans_.find(location.id);
  if (found == plans_.end()) {
    CloudReport report;
    report.message = "Check this game's saves again.";
    return report;
  }
  const auto requested = SaveLocation(location);
  const auto& checked = found->second.location;
  if (requested.app_id != checked.app_id || requested.game_root != checked.game_root ||
      requested.prefix_root != checked.prefix_root || requested.state_root != checked.state_root) {
    plans_.erase(found);
    CloudReport report;
    report.message = "This game's save location changed. Check its saves again.";
    return report;
  }
  const auto choice = resolution == CloudResolution::kUseLocal   ? cloud::Resolution::kUseLocal
                      : resolution == CloudResolution::kUseCloud ? cloud::Resolution::kUseCloud
                                                                 : cloud::Resolution::kAutomatic;
  current_app_ = location.id;
  auto report = Report(found->second.plan);
  const auto outcome = sync_.Apply(session, checked, found->second.plan, choice, confirm_account, cancel);
  plans_.erase(found);  // a plan is used once; the next sync checks again
  report.state = outcome.ok ? CloudState::kSynced : CloudState::kFailed;
  report.message = outcome.message;
  if (log_) log_(location.id, outcome.ok ? "apply-ok" : "apply-failed", outcome.message);
  return report;
}

}  // namespace playden::steam
