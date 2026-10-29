#pragma once

#include "library/model.h"
#include "steam/session_store.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace playden::steam {

// One Windows launch option from Steam's app info, as Steam wrote it.
struct LaunchOption {
  std::string executable;
  std::string arguments;
  std::string working_directory;
  std::string description;
};

// A version (Steam branch) an install can choose, with what installing it takes.
struct AppVersion {
  std::string name;  // the branch as Steam names it; "public" for the default
  std::string description;
  std::int64_t time_updated = 0;  // unix seconds; 0 = unknown
  std::uint64_t build_id = 0;
  std::uint64_t download_bytes = 0;  // 0 = unknown
  std::uint64_t install_bytes = 0;
  // The sizes for each content language the version carries, when an install picks another one.
  struct LanguageSize {
    std::string language;
    std::uint64_t download_bytes = 0;
    std::uint64_t install_bytes = 0;
  };
  std::vector<LanguageSize> languages;
};

// What a game's Steam app info says about installing it on this console.
struct AppDetails {
  GameId id;
  std::uint64_t account_generation = 0;
  bool available = false;  // false: Steam had no usable app info (see reason)
  std::optional<std::uint64_t> download_bytes;
  std::optional<std::uint64_t> install_bytes;
  bool windows_installable = false;  // depots and a launch entry exist for Windows/English
  std::string reason;                // short, user-facing, no identifiers
  std::vector<LaunchOption> launches;  // Windows launch options, default first
  // No option was for the public branch, so `launches` are every branch's: Steam doesn't say
  // which program starts the game.
  bool launches_any_branch = false;
  // The content languages the game's Windows depots carry, as Steam names them ("german"), in
  // listing order; empty when nothing is localised. An install can pick one of them.
  std::vector<std::string> languages;
  // Owned DLC with Windows content an install includes. Counted from the depots the game itself lists
  // for its DLC (dlcappid): depots only a DLC app lists, and DLC an install claims for free, are not counted.
  std::uint32_t dlc_count = 0;
  // The versions an install can choose: the public branch first, then branches with no password
  // that carry the game's own content, the most recently updated first. One entry: nothing to choose.
  std::vector<AppVersion> versions;
  // An installed game: what Steam has now against its install record (selected with the record's branch
  // and language). `newer`: a depot has another manifest, is new, or left the selection; `new_dlc`: DLC
  // the account owns now that the install doesn't hold. `download_bytes` is Steam's estimate for the
  // changed and new depots (the update downloads only changed files). All false when the licenses or the
  // record's depots are unknown.
  struct Update {
    bool newer = false;
    bool new_dlc = false;
    std::uint64_t download_bytes = 0;
    bool available() const { return newer || new_dlc; }
  };
  Update update;
};

// Runs on the service worker; may block on the network.
class AppDetailsSource {
 public:
  virtual ~AppDetailsSource() = default;
  virtual std::vector<AppDetails> Fetch(const StoredSession& session, const std::vector<GameId>& ids,
                                        const std::atomic<bool>& cancel) = 0;
};

// Fetches app details for games the player opens. Requests are deduplicated and cached for the
// current account generation; Invalidate drops everything for a new account. The source and the
// repository outlive the service.
class AppDetailsService {
 public:
  AppDetailsService(AppDetailsSource& source, SessionRepository& sessions);
  ~AppDetailsService();
  AppDetailsService(const AppDetailsService&) = delete;
  AppDetailsService& operator=(const AppDetailsService&) = delete;

  void Request(GameId id, std::uint64_t account_generation);
  // Asks again for a game already answered in this generation (an installed game whose content changed).
  void Refresh(GameId id, std::uint64_t account_generation);
  void Invalidate(std::uint64_t account_generation);
  std::vector<AppDetails> Pump(std::size_t maximum = 8);
  void Shutdown();

  static constexpr std::size_t kMaxPending = 64;
  static constexpr std::size_t kBatch = 8;

 private:
  void Run();

  AppDetailsSource& source_;
  SessionRepository& sessions_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::uint64_t generation_ = 0;
  std::deque<GameId> pending_;
  std::set<GameId> requested_;  // queued, in flight or answered for this generation
  std::deque<AppDetails> results_;
  std::atomic<bool> cancel_{false};
  bool stopping_ = false;
  std::thread worker_;
};

}  // namespace playden::steam
