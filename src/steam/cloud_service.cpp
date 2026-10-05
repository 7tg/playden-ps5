#include "steam/cloud_service.h"

#include <algorithm>

namespace playden::steam {

CloudService::CloudService(CloudRunner& runner, SessionRepository& sessions) : runner_(runner), sessions_(sessions) {
  worker_ = std::thread([this] { Run(); });
}

CloudService::~CloudService() { Shutdown(); }

void CloudService::Set(GameId id, CloudReport report) {
  games_[id] = std::move(report);
  ++revision_;
}

void CloudService::Sync(const CloudLocation& location) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_ || location.id == 0 || pending_.size() >= kMaxPending) return;
  const auto busy = games_.find(location.id);
  if (busy != games_.end() && (busy->second.state == CloudState::kChecking || busy->second.state == CloudState::kSyncing))
    return;
  CloudReport report;
  report.state = CloudState::kChecking;
  report.message = "Checking Steam Cloud…";
  Set(location.id, std::move(report));
  pending_.push_back({location, false, CloudResolution::kAutomatic, false});
  wake_.notify_all();
}

void CloudService::Resolve(const CloudLocation& location, CloudResolution resolution, bool confirm_account) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_ || location.id == 0 || pending_.size() >= kMaxPending) return;
  const auto current = games_.find(location.id);
  if (current == games_.end() ||
      (current->second.state != CloudState::kConflict && current->second.state != CloudState::kNeedsAccount))
    return;
  auto report = current->second;
  report.state = CloudState::kSyncing;
  report.message = "Syncing saves…";
  Set(location.id, std::move(report));
  pending_.push_back({location, true, resolution, confirm_account});
  wake_.notify_all();
}

CloudSnapshot CloudService::Snapshot() {
  std::lock_guard<std::mutex> lock(mutex_);
  CloudSnapshot snapshot;
  snapshot.revision = revision_;
  for (const auto& [id, report] : games_) snapshot.games.push_back({id, report});
  return snapshot;
}

void CloudService::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ && !worker_.joinable()) return;
    stopping_ = true;
    cancel_.store(true);
  }
  wake_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void CloudService::Run() {
  for (;;) {
    Request request;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
      if (stopping_) return;
      request = pending_.front();
      pending_.pop_front();
    }
    CloudReport report;
    const auto session = sessions_.Load();
    if (!session.ok() || !session.value()) {
      report.message = "Sign in to Steam to sync saves.";
    } else if (!request.apply) {
      report = runner_.Check(*session.value(), request.location, cancel_);
      if (report.state == CloudState::kSyncing) {
        // Transfers that need no choice run straight away.
        {
          std::lock_guard<std::mutex> lock(mutex_);
          Set(request.location.id, report);
        }
        report = runner_.Apply(*session.value(), request.location, CloudResolution::kAutomatic, false, cancel_);
      }
    } else {
      report = runner_.Apply(*session.value(), request.location, request.resolution, request.confirm_account, cancel_);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    Set(request.location.id, std::move(report));
  }
}

}  // namespace playden::steam
