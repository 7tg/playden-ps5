#include "steam/install_service.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <unistd.h>

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

class Runner final : public InstallRunner {
 public:
  std::atomic<bool> block{false};
  std::atomic<int> installs{0};
  std::atomic<int> canceled{0};
  std::mutex mutex;
  std::vector<std::string> removed;
  Result Install(const StoredSession&, GameId, const std::string& root, const std::atomic<bool>& cancel,
                 const std::function<void(std::uint64_t, std::uint64_t)>& progress) override {
    ++installs;
    progress(10, 100);
    while (block) {
      if (cancel) { ++canceled; return {false, true, {}, 0}; }
      std::this_thread::sleep_for(1ms);
    }
    std::filesystem::create_directories(root);
    std::ofstream(std::filesystem::path(root) / ".playden-install.json") << "{\"install_bytes\":4096}";
    progress(100, 100);
    return {true, false, {}, 4096};
  }
  bool Remove(const std::string& root) override {
    std::lock_guard<std::mutex> lock(mutex);
    removed.push_back(root);
    std::filesystem::remove_all(root);
    return true;
  }
};

template <typename Ready>
void AwaitAt(int line, InstallService& service, Ready ready) {
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < deadline) {
    if (ready(service.Snapshot())) return;
    std::this_thread::sleep_for(1ms);
  }
  std::fprintf(stderr, "deadline waiting at line %d\n", line);
  std::exit(1);
}

const InstallJobView* JobOf(const InstallSnapshot& snapshot, GameId id) {
  for (const auto& job : snapshot.jobs) if (job.id == id) return &job;
  return nullptr;
}

bool Installed(const InstallSnapshot& snapshot, GameId id) {
  for (const auto& game : snapshot.installed) if (game.id == id) return true;
  return false;
}

}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / ("playden-install-service-" + std::to_string(::getpid()));
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "100");
  std::ofstream(root / "100" / ".playden-install.json") << "{\"version\":1,\"app_id\":100,\"install_bytes\":777}";
  std::filesystem::create_directories(root / "200" / ".playden-download");
  std::filesystem::create_directories(root / "not-a-game");
  std::ofstream(root / ".playden-games") << "100\n200\nnot-a-number\n0\n";

  Sessions sessions;
  Runner runner;
  {
    InstallService service(runner, sessions, root.string());
    const auto start = service.Snapshot();
    CHECK(start.installed.size() == 1 && start.installed[0].id == 100 && start.installed[0].install_bytes == 777);
    CHECK(start.jobs.size() == 1 && start.jobs[0].id == 200 && start.jobs[0].state == InstallJobState::kPaused);

    // Install, pause while running, resume to completion.
    runner.block = true;
    service.Install(300);
    service.Install(300);  // duplicates are ignored
    AwaitAt(__LINE__, service, [](const InstallSnapshot& s) { auto* job = JobOf(s, 300); return job && job->state == InstallJobState::kRunning && job->total_bytes == 100; });
    service.Pause(300);
    AwaitAt(__LINE__, service, [](const InstallSnapshot& s) { auto* job = JobOf(s, 300); return job && job->state == InstallJobState::kPaused; });
    AwaitAt(__LINE__, service, [&](const InstallSnapshot&) { return runner.canceled.load() == 1; });
    runner.block = false;
    service.Resume(300);
    AwaitAt(__LINE__, service, [](const InstallSnapshot& s) { return Installed(s, 300) && !JobOf(s, 300); });
    CHECK(runner.installs == 2);
    service.Install(300);  // already installed
    CHECK(!JobOf(service.Snapshot(), 300));

    // Cancel a running install deletes its files; uninstall removes a finished one.
    runner.block = true;
    service.Install(400);
    AwaitAt(__LINE__, service, [](const InstallSnapshot& s) { auto* job = JobOf(s, 400); return job && job->state == InstallJobState::kRunning; });
    service.Cancel(400);
    AwaitAt(__LINE__, service, [](const InstallSnapshot& s) { return !JobOf(s, 400); });
    runner.block = false;
    service.Uninstall(100);
    AwaitAt(__LINE__, service, [&](const InstallSnapshot& s) {
      std::lock_guard<std::mutex> lock(runner.mutex);
      return !Installed(s, 100) && runner.removed.size() >= 2;
    });
    {
      std::lock_guard<std::mutex> lock(runner.mutex);
      CHECK(std::find(runner.removed.begin(), runner.removed.end(), (root / "400").string()) != runner.removed.end());
      CHECK(std::find(runner.removed.begin(), runner.removed.end(), (root / "100").string()) != runner.removed.end());
    }

    // Without a session the job fails visibly and can be resumed later.
    sessions.present = false;
    service.Install(500);
    AwaitAt(__LINE__, service, [](const InstallSnapshot& s) { auto* job = JobOf(s, 500); return job && job->state == InstallJobState::kFailed && !job->failure.empty(); });
  }
  {
    // The registry survives a restart: the finished 300 and failed 500 are found again.
    std::ifstream registry(root / ".playden-games");
    const std::string text((std::istreambuf_iterator<char>(registry)), std::istreambuf_iterator<char>());
    CHECK(text.find("300") != std::string::npos);
    InstallService again(runner, sessions, root.string());
    CHECK(Installed(again.Snapshot(), 300));
  }
  std::filesystem::remove_all(root);
  std::puts("install service tests passed");
}
