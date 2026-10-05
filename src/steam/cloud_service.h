#pragma once

#include "library/model.h"
#include "steam/session_store.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace playden::steam {

enum class CloudState {
  kIdle,
  kChecking,
  kSyncing,
  kSynced,        // the last sync finished; message says what moved
  kUpToDate,
  kConflict,      // the player picks local or Cloud saves
  kNeedsAccount,  // Cloud changes wait for the player to confirm this Steam account
  kUnsupported,   // no Steam Cloud, or save locations Playden cannot map
  kFailed,
};

enum class CloudResolution { kAutomatic, kUseLocal, kUseCloud };

// Where one game's saves live: the install, plus a folder that outlives it holding the game's Wine
// prefix (for Windows user folders), the sync baseline and backups.
struct CloudLocation {
  GameId id = 0;
  std::string game_root;
  std::string state_root;
};

// One check or sync, as the player sees it.
struct CloudReport {
  CloudState state = CloudState::kFailed;
  std::string message;  // short, user-facing, no identifiers
  std::size_t downloads = 0, uploads = 0, conflicts = 0;
  std::size_t local_files = 0, remote_files = 0;
  std::uint64_t local_bytes = 0, remote_bytes = 0, local_latest = 0, remote_latest = 0;
};

// Runs on the service worker and may block on the network. Check remembers its plan per game for
// the following Apply, and reports kSyncing when that plan's transfers need no choice (the service
// then applies it at once).
class CloudRunner {
 public:
  virtual ~CloudRunner() = default;
  virtual CloudReport Check(const StoredSession& session, const CloudLocation& location, const std::atomic<bool>& cancel) = 0;
  virtual CloudReport Apply(const StoredSession& session, const CloudLocation& location, CloudResolution resolution,
                            bool confirm_account, const std::atomic<bool>& cancel) = 0;
};

struct CloudGameView {
  GameId id = 0;
  CloudReport report;
};

struct CloudSnapshot {
  std::uint64_t revision = 0;
  std::vector<CloudGameView> games;
};

// Steam Cloud save sync on demand: Sync checks a game and carries out the transfers when they need
// no choice; a conflict or an unconfirmed account waits for Resolve. One operation runs at a time.
// The runner and repository outlive the service.
class CloudService {
 public:
  CloudService(CloudRunner& runner, SessionRepository& sessions);
  ~CloudService();
  CloudService(const CloudService&) = delete;
  CloudService& operator=(const CloudService&) = delete;

  void Sync(const CloudLocation& location);
  void Resolve(const CloudLocation& location, CloudResolution resolution, bool confirm_account);
  CloudSnapshot Snapshot();
  void Shutdown();
  static constexpr std::size_t kMaxPending = 16;

 private:
  struct Request {
    CloudLocation location;
    bool apply = false;
    CloudResolution resolution = CloudResolution::kAutomatic;
    bool confirm_account = false;
  };
  void Run();
  void Set(GameId id, CloudReport report);

  CloudRunner& runner_;
  SessionRepository& sessions_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Request> pending_;
  std::map<GameId, CloudReport> games_;
  std::uint64_t revision_ = 0;
  std::atomic<bool> cancel_{false};
  bool stopping_ = false;
  std::thread worker_;
};

}  // namespace playden::steam
