// uninstall_flow.cpp - The order of an uninstall: Cloud saves, then the PS5 title, then the download.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "homebrew/uninstall_flow.hpp"

namespace playden::homebrew {

bool UninstallFlow::Begin(GameId id, bool delete_saves, bool cloud_available) {
  if (entries_.count(id)) return false;
  Entry entry;
  entry.delete_saves = delete_saves;
  entry.step = cloud_available && !without_sync_.erase(id) ? UninstallStep::kSync : UninstallStep::kRemoveTitle;
  entries_[id] = entry;
  return true;
}

std::vector<GameId> UninstallFlow::TakeSyncRequests() {
  std::vector<GameId> ids;
  for (auto& [id, entry] : entries_)
    if (entry.step == UninstallStep::kSync && !entry.sync_issued) {
      entry.sync_issued = true;
      ids.push_back(id);
    }
  return ids;
}

bool UninstallFlow::WaitingForCloud(GameId id) const {
  const auto found = entries_.find(id);
  return found != entries_.end() && found->second.step == UninstallStep::kSync && found->second.sync_issued;
}

void UninstallFlow::SyncDispatched(GameId id) {
  if (const auto found = entries_.find(id); found != entries_.end() && found->second.step == UninstallStep::kSync)
    found->second.sync_dispatched = true;
}

UninstallAction UninstallFlow::Stop(GameId id, std::string reason) {
  entries_.erase(id);
  return {UninstallAction::Kind::kStop, false, std::move(reason)};
}

UninstallAction UninstallFlow::CloudSettled(GameId id, CloudStatus status) {
  if (!WaitingForCloud(id) || !entries_[id].sync_dispatched || status == CloudStatus::Checking ||
      status == CloudStatus::Syncing)
    return {};
  switch (status) {
    case CloudStatus::Synced:
    case CloudStatus::UpToDate:
    case CloudStatus::Unsupported:
      entries_[id].step = UninstallStep::kRemoveTitle;
      return {};
    case CloudStatus::Conflict:
    case CloudStatus::NeedsAccount:
      return Stop(id, "Choose which Cloud saves to keep, then uninstall again.");
    default:
      without_sync_.insert(id);
      return Stop(id, "Cloud saves could not be synced. Uninstall again to go on without syncing.");
  }
}

UninstallAction UninstallFlow::CloudRefused(GameId id) {
  if (!WaitingForCloud(id)) return {};
  return Stop(id, "The game's PS5 title may still be running. Close it, then uninstall again.");
}

void UninstallFlow::CloudSkipped(GameId id) {
  if (const auto found = entries_.find(id); found != entries_.end() && found->second.step == UninstallStep::kSync)
    found->second.step = UninstallStep::kRemoveTitle;
}

UninstallAction UninstallFlow::Advance(GameId id, const UninstallFacts& facts) {
  const auto found = entries_.find(id);
  if (found == entries_.end()) return {};
  auto& entry = found->second;
  if (entry.step == UninstallStep::kRemoveTitle) {
    if (!entry.remove_sent) {
      if (!facts.has_title) {
        entry.step = UninstallStep::kRemoveDownload;
      } else {
        // Saves must not move, and the title must not run, while it is removed.
        if (facts.saves_moving || facts.session_active) return {};
        // The runtime counts removals for all games together, so only one may be in flight to tell whose finished.
        for (const auto& [other_id, other] : entries_)
          if (other_id != id && other.remove_sent) return {};
        entry.remove_sent = true;
        entry.removals_before = facts.removals_handled;
        return {UninstallAction::Kind::kRemoveTitle, entry.delete_saves, {}};
      }
    } else {
      if (facts.removing || facts.removals_handled <= entry.removals_before) return {};
      // A refusal before the removal began changed nothing: there is no removal for Retry to continue.
      // A failure stops the uninstall even when the runtime lists no title: its install list can be
      // empty because it could not be read, while the PS5 title still points at the game's files.
      if (facts.has_title || !facts.removal_failure.empty())
        return Stop(id, "The PS5 title could not be removed: " +
                            (facts.removal_failure.empty() ? std::string("unknown reason.") : facts.removal_failure) +
                            (facts.removal_started ? " The game stays installed; Retry on its PS5 title continues."
                                                   : " The game stays installed."));
      entry.step = UninstallStep::kRemoveDownload;
    }
  }
  if (entry.step == UninstallStep::kRemoveDownload) {
    entries_.erase(found);
    return {UninstallAction::Kind::kRemoveDownload, false, {}};
  }
  return {};
}

std::optional<UninstallStep> UninstallFlow::Step(GameId id) const {
  const auto found = entries_.find(id);
  return found == entries_.end() ? std::nullopt : std::optional<UninstallStep>(found->second.step);
}

std::vector<GameId> UninstallFlow::Active() const {
  std::vector<GameId> ids;
  for (const auto& [id, entry] : entries_) ids.push_back(id);
  return ids;
}
}  // namespace playden::homebrew
