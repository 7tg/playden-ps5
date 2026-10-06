#pragma once
// runtime_host.hpp - Worker-owned composition of the per-game runtime services.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "platform/ps5/native_coordinator.h"
#include "platform/ps5/title_registry.h"
#include "platform/ps5/title_runtime.h"
#include "runtime/game_install.h"
#include "runtime/game_settings.h"
#include "runtime/install_service.h"
#include "runtime/launch_service.h"
#include "runtime/package_installer.h"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <pthread.h>
#include <string>
#include <vector>

namespace playden::homebrew {

// An admitted kit: the verified view and the capabilities its manifest declares.
struct RuntimeKit {
  runtime::VerifiedKitView view;
  std::vector<std::string> capabilities;
};

struct RuntimeHostConfig {
  // Existing trusted parent of state_root (for example /data).
  std::filesystem::path boundary;
  // Private 0700 OWNER state: installs.json and requests/ (settings requests).
  std::filesystem::path state_root;
  std::filesystem::path homebrew_root = "/data/homebrew";
  // Host-test namespace prefix for console paths; empty on the console.
  std::filesystem::path host_namespace_prefix;
  // Empty members use the native SQLite inventory and process activity readers.
  ps5::NativeCoordinatorOperations native;
  ps5::TitleRegistry::Transport registry_transport;
  // Verifies the installed kit on the worker, or returns nothing with a reason.
  std::function<std::optional<RuntimeKit>(std::string* reason)> admit_kit;
  // Package layer paths and base operations (hashing, sync, space, bottle
  // validation). The host adds cancellation.
  runtime::PackageInstallPaths package_paths;
  runtime::PackageInstallerOperations package_operations;
  // Bundled recipes/presets/input presets (<title>/runtime).
  std::string runtime_dir;
  // The player's saved settings for a game; nothing means Playden defaults.
  std::function<std::optional<runtime::GameSettingsRecord>(std::uint32_t app_id)> settings_record;
  // Canonical lower-case UUID for a publication transaction, or nothing when no
  // secure random source is available. Empty uses /dev/urandom.
  std::function<std::optional<std::string>()> uuid;
  std::function<std::int64_t()> unix_time;
  // Diagnostic lines (no credentials): startup, each preparation step and refusals.
  std::function<void(const std::string&)> log;
  // Receives each prepared title's newest wrapper session log (bounded tail) at
  // startup. The wrapper writes it inside the private bottle tree, which only
  // the app user can read.
  std::function<void(const std::string& title_id, const std::string& text)> title_log;
  // The game's PS5 title icon: a 512x512 PNG made from its store artwork, or
  // nothing (the kit's icon stays). Runs on the worker; may use the network.
  std::function<std::optional<std::string>(std::uint32_t app_id)> title_icon;
  // Native start of a prepared title with its arguments ("session=<UUID>").
  // Called only on the application thread, from Handoff. Empty disables Play.
  std::function<ps5::TitleResult(const std::string& title_id, const std::vector<std::string>& arguments)> launch_title;
  // Canonical /data as this process sees it, for the wrapper's session records.
  std::filesystem::path session_data_root = "/data";
  std::function<std::int64_t()> now_ms;
  // How long a started title may take to report its session before Play gives up.
  std::int64_t launch_evidence_wait_ms = 60000;
  // SQLite and the native readers run on this worker. PS5 std::thread stacks are
  // too small for them, so the worker uses an explicit stack size.
  std::size_t stack_bytes = 1024 * 1024;
};

struct RuntimeInstallView {
  std::string install_id;
  std::uint32_t app_id = 0;
  std::string title_id;
  // The game's private bottle (console path); Cloud saves under Windows
  // folders live here.
  std::string prefix_root;
  runtime::PreparationState state = runtime::PreparationState::kDownloaded;
  std::string step;     // current journal step, if any
  std::string failure;
  // A durable journal is present while no preparation runs. Recovery is
  // explicit; startup never resumes it.
  bool recovery_pending = false;
  // False when a Ready title was made from an earlier kit; Prepare updates it.
  bool title_current = true;
  // Publication admission result for Ready titles ("ok" or the refusal); a
  // refusal means the next start would be refused, and Prepare repairs it.
  std::string admission = "ok";
};

// Play: the launch in progress, or how the last one ended.
struct RuntimeLaunchView {
  std::string install_id;
  std::uint32_t app_id = 0;
  std::string title_id;
  runtime::LaunchState state = runtime::LaunchState::kIdle;
  std::string code;
  std::string message;
  std::optional<int> exit_code;
};

enum class RuntimeHostStatus { kStarting, kUnavailable, kReady, kStopped };

struct RuntimeSnapshot {
  std::uint64_t revision = 0;
  RuntimeHostStatus status = RuntimeHostStatus::kStarting;
  std::string reason;  // why the runtime services are unavailable
  bool kit_available = false;
  std::string kit_reason;  // why preparation is unavailable
  std::vector<RuntimeInstallView> installs;
  // Latest refused or failed request per Steam app, for presentation.
  std::map<std::uint32_t, std::string> refusals;
  std::optional<std::uint32_t> preparing_app;  // the game being prepared now
  std::optional<RuntimeLaunchView> launch;
  // Settings requests no install record's journal names: left by an interrupted
  // preparation, a refused request or a finished publication. They grant nothing.
  std::size_t leftover_requests = 0;
  // Answers to CheckActivity: the title's activity per Steam app, with the
  // check's serial so a caller can tell a fresh answer from an older one.
  std::map<std::uint32_t, std::pair<runtime::NativeActivity, std::uint64_t>> activity;
  std::uint64_t activity_serial = 0;
  // Play requests the worker has finished handling (accepted or refused).
  std::uint64_t plays_handled = 0;
  // Latest refused Play per Steam app, for presentation.
  std::map<std::uint32_t, std::string> play_refusals;
  // A checked session waits for Handoff on the application thread.
  bool handoff_ready = false;
};

// A downloaded game to prepare. The game files stay at install_path.
struct RuntimePrepareRequest {
  std::uint32_t app_id = 0;
  std::string install_path;  // console path of the Steam install
  std::string volume_id;
  std::string title_name;    // the game's name, shown for its PS5 title
};

// The worker constructs and owns every runtime collaborator, so durable work and
// native inventory never run on the main thread. The main thread reads snapshots
// and queues requests. Play is split the same way: the worker checks the game
// and prepares the launch session, and only Handoff, on the application thread
// that constructed this object, starts the title.
class RuntimeHost {
 public:
  explicit RuntimeHost(RuntimeHostConfig config);
  ~RuntimeHost();
  RuntimeHost(const RuntimeHost&) = delete;
  RuntimeHost& operator=(const RuntimeHost&) = delete;

  bool Start();
  void Shutdown();
  RuntimeSnapshot Snapshot() const;
  // Queues preparation of a downloaded game, or a retry of a failed one.
  void Prepare(RuntimePrepareRequest request);
  // Asks the console whether a game's PS5 title runs (from any origin, Home
  // included). A game with no prepared title reports stopped.
  void CheckActivity(std::uint32_t app_id);
  // Queues removal of the leftover settings requests (rechecked on the worker).
  void CleanUpRequests();
  // Queues Play of a Ready game.
  void Play(std::uint32_t app_id);
  // Application thread only: starts the title of a prepared launch session.
  // Returns false when no session is waiting.
  bool Handoff();

 private:
  struct Composition;
  static void* Entry(void* self);
  void Run();
  void Publish(const std::function<void(RuntimeSnapshot&)>& change);
  void PublishInstalls(const Composition& composition);
  std::string RunPrepare(Composition& composition, const RuntimePrepareRequest& request);
  runtime::PackageActionResult DrivePublication(Composition& composition, const runtime::RuntimeInstall& record);
  runtime::LaunchPreflight Preflight(Composition& composition, const runtime::RuntimeInstall& record);
  runtime::LaunchEvidence Evidence(Composition& composition, const runtime::RuntimeInstall& record,
                                   const std::string& session_id);
  void RunPlay(Composition& composition, std::uint32_t app_id);
  // Writes the game's own title name (and icon) for its PS5 title.
  runtime::PackageActionResult WriteTitleMetadata(Composition& composition, const runtime::RuntimeInstall& record,
                                                  const std::string& title_name);
  // Polls a pending launch session and ends it once its outcome is final.
  // Returns true while a session is still undecided.
  bool PollLaunch(Composition& composition);
  void PublishLaunch(Composition& composition, const runtime::LaunchSnapshot& launch);
  // Settings request files that no record's journal names; with remove, deletes them.
  std::size_t LeftoverRequests(Composition& composition, bool remove);
  bool Stopping() const;
  void Log(const std::string& line) const;

  RuntimeHostConfig config_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  RuntimeSnapshot snapshot_;
  std::vector<RuntimePrepareRequest> prepare_requests_;
  std::vector<std::uint32_t> play_requests_;
  std::vector<std::uint32_t> activity_requests_;
  bool launch_poll_ = false;
  bool cleanup_requested_ = false;
  // Guards the worker's launch service and preflight cache for Handoff.
  std::mutex launch_mutex_;
  runtime::LaunchService* launch_ = nullptr;
  Composition* launch_composition_ = nullptr;
  pthread_t application_thread_{};
  bool stopping_ = false;
  bool started_ = false;
  void* thread_ = nullptr;  // pthread_t storage, owned by the implementation
};

// Canonical random (version 4) UUID from /dev/urandom, or nothing.
std::optional<std::string> RandomUuid();

}  // namespace playden::homebrew
