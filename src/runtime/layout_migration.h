#pragma once
// layout_migration.h - The 0.6.0 one-time move of user data, logs and games out of the app folder.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>

namespace playden::runtime {

// Old places are inside the app folder (app_root); new ones are outside it.
struct LayoutPaths {
  std::filesystem::path app_root;    // /data/homebrew/<title>: holds playden-data, logs and games
  std::filesystem::path user_root;   // /data/playden/user
  std::filesystem::path log_root;    // /data/playden/logs
  std::filesystem::path games_root;  // /data/playden-games
  std::filesystem::path state_root;  // /data/playden/runtime-state (private, holds the journal)
  std::filesystem::path boundary;    // trusted parent the runtime creates state_root under (/data)
};

struct LayoutOps {
  // rename(2); returns the errno as an error_code. Empty: the real rename.
  std::function<std::error_code(const std::filesystem::path&, const std::filesystem::path&)> rename;
  // Runs work with root's effective UID. Empty: runs it directly (tests).
  std::function<bool(const std::function<bool()>&)> as_root;
  std::function<void(const std::string&)> log;
  // Whether a title (of a pending launch session) runs or is suspended; nothing: unknown.
  // Empty: unknown (the games move waits for the session to end).
  std::function<std::optional<bool>(const std::string& title_id)> title_running;
};

inline constexpr const char* kLayoutJournal = "layout-0.6.json";
// Pending-journal postponements of the games step before the user is told.
inline constexpr int kLayoutPostponeNoticeAfter = 3;

// Steps, in order: "user", "logs", "games", "games-moved", "done". A step is
// written before its action, so an interrupted move resumes there. At
// "games-moved", from/to are the old and new games prefixes; the runtime host
// rewrites its records, republishes, then calls MarkLayoutDone. RevertLayout
// writes "revert-games", "revert-logs", "revert-user", then removes the journal.
struct LayoutJournal {
  std::string step;
  std::string from;
  std::string to;
  int postponed = 0;  // starts the games step waited for a pending install, publication or launch
};

// Nothing when there is no journal (or it can't be read; `error` says which).
std::optional<LayoutJournal> LoadLayoutJournal(const std::filesystem::path& state_root,
                                               std::string* error = nullptr);
bool MarkLayoutDone(const std::filesystem::path& state_root, std::string* error = nullptr);

struct LayoutResult {
  bool user_moved = false;   // user data lives at paths.user_root
  bool logs_moved = false;   // logs live at paths.log_root
  bool games_moved = false;  // games live at paths.games_root
  // Effective roots for this start: the new place when that step has completed, else the old one.
  std::filesystem::path user_root;
  std::filesystem::path log_root;
  std::filesystem::path games_root;
  std::string notice;  // one line for the user, when the move is held back
};

// Single-threaded, before any service starts, after the root grant. Never copies,
// never merges into a non-empty target, never replaces an existing entry.
LayoutResult MigrateLayout(const LayoutPaths& paths, const LayoutOps& ops);

struct RevertResult {
  bool complete = false;  // everything is back in the app folder and the journal is gone
  std::string error;
};

// The reverse move, for a restore below 0.6.0. Entries that would replace an
// existing one stay in the new place, and the journal is kept so it can be retried.
RevertResult RevertLayout(const LayoutPaths& paths, const LayoutOps& ops);

}  // namespace playden::runtime
