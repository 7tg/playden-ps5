#include "steam/cloud_service.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

using namespace playden;
using namespace playden::steam;
using namespace std::chrono_literals;

#define CHECK(condition)                                                                  \
  do {                                                                                    \
    if (!(condition)) {                                                                   \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
      std::exit(1);                                                                       \
    }                                                                                     \
  } while (false)

namespace {

class Sessions final : public SessionRepository {
 public:
  bool present = true;
  Result<std::optional<StoredSession>> Load() const override {
    if (!present) return Result<std::optional<StoredSession>>::Success(std::nullopt);
    return Result<std::optional<StoredSession>>::Success(StoredSession{123, "synthetic", "refresh", "access", {}});
  }
  Result<void> Save(const StoredSession&) const override { return Result<void>::Success(); }
  Result<void> Clear() const override { return Result<void>::Success(); }
};

class Runner final : public CloudRunner {
 public:
  std::mutex mutex;
  CloudState next_check = CloudState::kSyncing;
  int checks = 0, applies = 0;
  CloudResolution last_resolution = CloudResolution::kAutomatic;
  bool last_confirm = false;
  std::string last_root;
  std::string last_prefix;
  CloudReport Check(const StoredSession&, const CloudLocation& location, const std::atomic<bool>&) override {
    std::lock_guard<std::mutex> lock(mutex);
    ++checks;
    last_root = location.game_root;
    last_prefix = location.prefix_root;
    CloudReport report;
    report.state = next_check;
    report.message = "checked";
    report.downloads = 2;
    return report;
  }
  CloudReport Apply(const StoredSession&, const CloudLocation&, CloudResolution resolution, bool confirm,
                    const std::atomic<bool>&) override {
    std::lock_guard<std::mutex> lock(mutex);
    ++applies;
    last_resolution = resolution;
    last_confirm = confirm;
    CloudReport report;
    report.state = CloudState::kSynced;
    report.message = "2 saves downloaded";
    return report;
  }
};

template <typename Ready>
void AwaitAt(int line, CloudService& service, Ready ready) {
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (ready(service.Snapshot())) return;
    std::this_thread::sleep_for(1ms);
  }
  std::fprintf(stderr, "deadline waiting at line %d\n", line);
  std::exit(1);
}

const CloudReport* Of(const CloudSnapshot& snapshot, GameId id) {
  for (const auto& game : snapshot.games) if (game.id == id) return &game.report;
  return nullptr;
}

}  // namespace

int main() {
  Sessions sessions;
  Runner runner;
  CloudService service(runner, sessions);
  const CloudLocation game{42, "/games/42", "/games/.playden-saves/42",
                           "/data/playden/games/install-42/library/prefixes/game"};

  // Transfers that need no choice run straight after the check.
  service.Sync(game);
  CHECK(Of(service.Snapshot(), 42) && (Of(service.Snapshot(), 42)->state == CloudState::kChecking ||
                                        Of(service.Snapshot(), 42)->state == CloudState::kSyncing ||
                                        Of(service.Snapshot(), 42)->state == CloudState::kSynced));
  AwaitAt(__LINE__, service, [](const CloudSnapshot& s) { auto* r = Of(s, 42); return r && r->state == CloudState::kSynced; });
  {
    std::lock_guard<std::mutex> lock(runner.mutex);
    CHECK(runner.checks == 1 && runner.applies == 1 && runner.last_resolution == CloudResolution::kAutomatic);
    CHECK(runner.last_root == "/games/42");
    CHECK(runner.last_prefix == game.prefix_root);
  }

  // A conflict waits for the player, then applies their choice.
  runner.next_check = CloudState::kConflict;
  service.Sync(game);
  AwaitAt(__LINE__, service, [](const CloudSnapshot& s) { auto* r = Of(s, 42); return r && r->state == CloudState::kConflict; });
  {
    std::lock_guard<std::mutex> lock(runner.mutex);
    CHECK(runner.applies == 1);
  }
  service.Resolve(game, CloudResolution::kUseCloud, true);
  AwaitAt(__LINE__, service, [](const CloudSnapshot& s) { auto* r = Of(s, 42); return r && r->state == CloudState::kSynced; });
  {
    std::lock_guard<std::mutex> lock(runner.mutex);
    CHECK(runner.applies == 2 && runner.last_resolution == CloudResolution::kUseCloud && runner.last_confirm);
  }

  // Resolve does nothing unless a choice is pending.
  service.Resolve(game, CloudResolution::kUseLocal, false);
  std::this_thread::sleep_for(20ms);
  {
    std::lock_guard<std::mutex> lock(runner.mutex);
    CHECK(runner.applies == 2);
  }

  // Up to date: no transfer.
  runner.next_check = CloudState::kUpToDate;
  service.Sync(game);
  AwaitAt(__LINE__, service, [](const CloudSnapshot& s) { auto* r = Of(s, 42); return r && r->state == CloudState::kUpToDate; });

  // Without a session the sync fails visibly.
  sessions.present = false;
  service.Sync(game);
  AwaitAt(__LINE__, service, [](const CloudSnapshot& s) {
    auto* r = Of(s, 42);
    return r && r->state == CloudState::kFailed && r->message == "Sign in to Steam to sync saves.";
  });
  service.Shutdown();
  std::puts("cloud service tests passed");
}
