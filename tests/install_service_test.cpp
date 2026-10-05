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
  std::vector<std::string> install_roots;
  Result Install(const StoredSession&, GameId, const std::string& root, const std::atomic<bool>& cancel,
                 const std::function<void(std::uint64_t, std::uint64_t)>& progress) override {
    ++installs;
    {
      std::lock_guard<std::mutex> lock(mutex);
      install_roots.push_back(root);
    }
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

  // Several volumes: the primary keeps the registry; new installs follow the default volume.
  const auto primary = std::filesystem::temp_directory_path() / ("playden-install-primary-" + std::to_string(::getpid()));
  const auto second = std::filesystem::temp_directory_path() / ("playden-install-second-" + std::to_string(::getpid()));
  std::filesystem::remove_all(primary);
  std::filesystem::remove_all(second);
  std::filesystem::create_directories(primary / "100");
  std::ofstream(primary / "100" / ".playden-install.json") << "{\"install_bytes\":10}";
  std::filesystem::create_directories(second / "600");
  std::ofstream(second / "600" / ".playden-install.json") << "{\"install_bytes\":60}";
  // Legacy line (primary), a game on a drive that is absent now, a game on the second drive, and junk.
  std::ofstream(primary / ".playden-games") << "100\n500\tusb0\t1234\n600\tm2\t0\n700\tBAD VOLUME\t1\n";
  const std::vector<InstallVolume> volumes = {
      {"internal", "Console storage", primary.string(), true},
      {"m2", "M.2 SSD", second.string(), true},
      {"usb0", "USB drive", (second / "absent").string(), false},
  };
  sessions.present = true;
  {
    Runner multi;
    InstallService service(multi, sessions, volumes);
    auto start = service.Snapshot();
    CHECK(start.default_volume_id == "internal");
    CHECK(start.volumes.size() == 3);
    CHECK(start.installed.size() == 3);
    const auto on = [&](GameId id) {
      for (const auto& game : start.installed) if (game.id == id) return &game;
      return static_cast<const InstalledGame*>(nullptr);
    };
    CHECK(on(100) && on(100)->volume_id == "internal" && on(100)->available && on(100)->install_bytes == 10);
    CHECK(on(500) && on(500)->volume_id == "usb0" && !on(500)->available && on(500)->install_bytes == 1234);
    CHECK(on(600) && on(600)->volume_id == "m2" && on(600)->available && on(600)->install_bytes == 60);
    CHECK(!on(700));

    service.SetDefaultVolume("usb0");  // unavailable: ignored
    CHECK(service.Snapshot().default_volume_id == "internal");
    service.SetDefaultVolume("m2");
    CHECK(service.Snapshot().default_volume_id == "m2");
    CHECK(service.RootFor(800) == (second / "800").string());
    CHECK(service.RootFor(100) == (primary / "100").string());

    service.Install(800);
    AwaitAt(__LINE__, service, [](const InstallSnapshot& s) { return Installed(s, 800); });
    {
      std::lock_guard<std::mutex> lock(multi.mutex);
      CHECK(multi.install_roots.size() == 1 && multi.install_roots[0] == (second / "800").string());
    }
    for (const auto& game : service.Snapshot().installed) if (game.id == 800) CHECK(game.volume_id == "m2");

    service.Uninstall(500);  // its drive is absent: nothing to delete, the game stays listed
    CHECK(Installed(service.Snapshot(), 500));
    service.Uninstall(600);
    AwaitAt(__LINE__, service, [&](const InstallSnapshot& s) {
      std::lock_guard<std::mutex> lock(multi.mutex);
      return !Installed(s, 600) && !multi.removed.empty();
    });
    {
      std::lock_guard<std::mutex> lock(multi.mutex);
      CHECK(multi.removed.back() == (second / "600").string());
    }
  }
  {
    // The default volume and every game's volume survive a restart.
    Runner multi;
    InstallService again(multi, sessions, volumes);
    const auto snapshot = again.Snapshot();
    CHECK(snapshot.default_volume_id == "m2");
    CHECK(Installed(snapshot, 800) && Installed(snapshot, 500) && Installed(snapshot, 100) && !Installed(snapshot, 600));
    for (const auto& game : snapshot.installed) {
      if (game.id == 800) CHECK(game.volume_id == "m2" && game.available);
      if (game.id == 500) CHECK(game.volume_id == "usb0" && !game.available && game.install_bytes == 1234);
    }
  }
  {
    // A remembered default whose drive is now absent falls back to the primary volume.
    auto without_m2 = volumes;
    without_m2[1].available = false;
    Runner multi;
    InstallService fallback(multi, sessions, without_m2);
    CHECK(fallback.Snapshot().default_volume_id == "internal");
    for (const auto& game : fallback.Snapshot().installed)
      if (game.id == 800) CHECK(!game.available);
  }
  std::filesystem::remove_all(primary);
  std::filesystem::remove_all(second);
  std::puts("install service tests passed");
}
