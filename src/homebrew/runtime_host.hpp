#pragma once
// runtime_host.hpp - Worker-owned composition of the per-game runtime services.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "platform/ps5/native_coordinator.h"
#include "platform/ps5/title_registry.h"
#include "runtime/game_install.h"
#include "runtime/game_settings.h"
#include "runtime/install_service.h"
#include "runtime/package_installer.h"

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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
  // SQLite and the native readers run on this worker. PS5 std::thread stacks are
  // too small for them, so the worker uses an explicit stack size.
  std::size_t stack_bytes = 1024 * 1024;
};

struct RuntimeInstallView {
  std::string install_id;
  std::uint32_t app_id = 0;
  std::string title_id;
  runtime::PreparationState state = runtime::PreparationState::kDownloaded;
  std::string step;     // current journal step, if any
  std::string failure;
  // A durable journal is present while no preparation runs. Recovery is
  // explicit; startup never resumes it.
  bool recovery_pending = false;
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
};

// A downloaded game to prepare. The game files stay at install_path.
struct RuntimePrepareRequest {
  std::uint32_t app_id = 0;
  std::string install_path;  // console path of the Steam install
  std::string volume_id;
};

// The worker constructs and owns every runtime collaborator, so durable work and
// native inventory never run on the main thread. The main thread reads snapshots
// and queues requests. Native launch handoff is not part of this object.
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

 private:
  struct Composition;
  static void* Entry(void* self);
  void Run();
  void Publish(const std::function<void(RuntimeSnapshot&)>& change);
  void PublishInstalls(const Composition& composition);
  std::string RunPrepare(Composition& composition, const RuntimePrepareRequest& request);
  runtime::PackageActionResult DrivePublication(Composition& composition, const runtime::RuntimeInstall& record);
  bool Stopping() const;

  RuntimeHostConfig config_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  RuntimeSnapshot snapshot_;
  std::vector<RuntimePrepareRequest> prepare_requests_;
  bool stopping_ = false;
  bool started_ = false;
  void* thread_ = nullptr;  // pthread_t storage, owned by the implementation
};

// Canonical random (version 4) UUID from /dev/urandom, or nothing.
std::optional<std::string> RandomUuid();

}  // namespace playden::homebrew
