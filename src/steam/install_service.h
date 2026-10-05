#pragma once

#include "library/model.h"
#include "steam/session_store.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace playden::steam {

enum class InstallJobState { kQueued, kRunning, kPaused, kFailed };

struct InstallJobView {
  GameId id = 0;
  InstallJobState state = InstallJobState::kQueued;
  std::uint64_t done_bytes = 0;
  std::uint64_t total_bytes = 0;
  std::string failure;  // short, user-facing
};

struct InstalledGame {
  GameId id = 0;
  std::uint64_t install_bytes = 0;
  std::string root;
};

struct InstallSnapshot {
  std::uint64_t revision = 0;
  std::vector<InstallJobView> jobs;
  std::vector<InstalledGame> installed;
};

// One game's install, run synchronously on the service worker. Implementations report progress
// and honor cancel; they return an empty string on success or a short failure text.
class InstallRunner {
 public:
  virtual ~InstallRunner() = default;
  struct Result {
    bool ok = false;
    bool canceled = false;
    std::string failure;
    std::uint64_t install_bytes = 0;
  };
  virtual Result Install(const StoredSession& session, GameId id, const std::string& root,
                         const std::atomic<bool>& cancel,
                         const std::function<void(std::uint64_t done, std::uint64_t total)>& progress) = 0;
  virtual bool Remove(const std::string& root) = 0;
};

// Install queue for real games under games_root/<app id>. One install runs at a time. Pause keeps
// the partial download for Resume; Cancel and Uninstall delete only Playden-managed folders. At
// start the root is scanned: complete installs are listed and interrupted downloads come back
// paused. The runner and repository outlive the service.
class InstallService {
 public:
  InstallService(InstallRunner& runner, SessionRepository& sessions, std::string games_root);
  ~InstallService();
  InstallService(const InstallService&) = delete;
  InstallService& operator=(const InstallService&) = delete;

  void Install(GameId id);
  void Pause(GameId id);
  void Resume(GameId id);
  void Cancel(GameId id);
  void Uninstall(GameId id);
  InstallSnapshot Snapshot();
  void Shutdown();

  std::string RootFor(GameId id) const;
  static constexpr std::size_t kMaxJobs = 64;

 private:
  struct Job {
    InstallJobView view;
    bool cancel_requested = false;  // delete files when the run stops
  };
  void Run();
  void Scan();
  Job* Find(GameId id);
  void Publish();
  void SaveRegistry();

  InstallRunner& runner_;
  SessionRepository& sessions_;
  std::string games_root_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Job> jobs_;
  std::vector<InstalledGame> installed_;
  std::deque<GameId> removals_;
  std::optional<GameId> active_;
  std::atomic<bool> active_cancel_{false};
  std::uint64_t revision_ = 0;
  bool stopping_ = false;
  std::thread worker_;
};

}  // namespace playden::steam
