#pragma once
// runtime_host.hpp - Worker-owned composition of the per-game runtime services.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "platform/ps5/native_coordinator.h"
#include "platform/ps5/title_registry.h"
#include "runtime/game_install.h"
#include "runtime/install_service.h"

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

struct RuntimeHostConfig {
  // Existing trusted parent of state_root (for example /data).
  std::filesystem::path boundary;
  // Private 0700 OWNER state: installs.json and settings requests.
  std::filesystem::path state_root;
  std::filesystem::path homebrew_root = "/data/homebrew";
  // Empty members use the native SQLite inventory and process activity readers.
  ps5::NativeCoordinatorOperations native;
  ps5::TitleRegistry::Transport registry_transport;
  // Returns an installer bound to an admitted kit, or null with a reason when
  // no verified kit is installed. Called once, on the worker.
  std::function<std::unique_ptr<runtime::PackageInstaller>(std::string* reason)> make_installer;
  // SQLite and the native readers run on this worker. PS5 std::thread stacks are
  // too small for them, so the worker uses an explicit stack size.
  std::size_t stack_bytes = 1024 * 1024;
};

struct RuntimeInstallView {
  std::string install_id;
  std::uint32_t app_id = 0;
  std::string title_id;
  runtime::PreparationState state = runtime::PreparationState::kDownloaded;
  std::string failure;
  // A durable journal is present. Recovery is explicit; startup never resumes it.
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
  // Latest refused request per Steam app, for presentation.
  std::map<std::uint32_t, std::string> refusals;
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
  // Queues preparation of a downloaded game. Refused while no kit is admitted.
  void Prepare(std::uint32_t app_id);

 private:
  struct Composition;
  static void* Entry(void* self);
  void Run();
  void Publish(const std::function<void(RuntimeSnapshot&)>& change);

  RuntimeHostConfig config_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  RuntimeSnapshot snapshot_;
  std::vector<std::uint32_t> prepare_requests_;
  bool stopping_ = false;
  bool started_ = false;
  void* thread_ = nullptr;  // pthread_t storage, owned by the implementation
};

}  // namespace playden::homebrew
