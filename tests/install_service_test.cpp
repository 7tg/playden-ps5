#include "steam/install_service.h"
#include "steam/install_volumes.h"

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
  std::atomic<bool> present{true};
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
  const auto requests = ParseInstallRequests("uninstall 439490\n439490\ncancel 108600\n108600\n");
  CHECK(requests.size() == 4);
  CHECK(requests[0].action == InstallRequest::Action::kUninstall && requests[0].app == 439490);
  CHECK(requests[1].action == InstallRequest::Action::kInstall && requests[1].app == 439490);
  CHECK(requests[2].action == InstallRequest::Action::kCancel && requests[2].app == 108600);
  CHECK(requests[3].action == InstallRequest::Action::kInstall && requests[3].app == 108600);
  CHECK(ParseInstallRequests("0\n-1\n4294967296\n12x\nuninstall 12 extra\nunknown 12\n").empty());
  CHECK(ParseInstallRequests("  cancel 42  \r\n")[0].app == 42);
  std::string many_requests;
  for (int app = 1; app <= 70; ++app) many_requests += std::to_string(app) + "\n";
  CHECK(ParseInstallRequests(many_requests).size() == 64);
  {
    // Installed records must survive removal of the title's /app0 view.
    const auto stable = std::filesystem::temp_directory_path() /
        ("playden-stable-title-" + std::to_string(::getpid()));
    std::filesystem::remove_all(stable);
    std::filesystem::create_directories(stable / "games/220");
    std::ofstream(stable / "games/.playden-games") << "220\n";
    std::ofstream(stable / "games/220/.playden-install.json") << "{\"install_bytes\":8192}";
    auto candidates = ConsoleVolumeCandidates(stable.string());
    candidates.resize(1);
    Runner runner;
    Sessions sessions;
    InstallService service(runner, sessions, DiscoverInstallVolumes(candidates));
    const auto installed = service.Snapshot().installed;
    CHECK(installed.size() == 1 && installed[0].id == 220 && installed[0].available);
    CHECK(installed[0].root == (stable / "games/220").string() && installed[0].install_bytes == 8192);
    std::filesystem::remove_all(stable);
  }

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
    // Queue order survives progress snapshots and restart without interrupting the active download.
    const auto reorder_root = root / "reorder";
    Runner reorder_runner;
    reorder_runner.block = true;
    {
      InstallService service(reorder_runner, sessions, reorder_root.string());
      service.Install(10);
      AwaitAt(__LINE__, service, [](const InstallSnapshot& s) {
        return JobOf(s, 10) && JobOf(s, 10)->state == InstallJobState::kRunning;
      });
      service.Install(20);
      service.Install(30);
      service.Install(40);
      service.Pause(30);
      service.MoveQueued(20, false);
      auto snapshot = service.Snapshot();
      CHECK(snapshot.jobs[0].id == 10 && snapshot.jobs[1].id == 40 && snapshot.jobs[3].id == 20);
      CHECK(JobOf(snapshot, 10)->state == InstallJobState::kRunning && reorder_runner.canceled == 0);
      service.MoveQueued(20, true);
      CHECK(service.Snapshot().jobs[1].id == 20);
      service.MoveQueued(20, true);  // first queued: cannot displace the running download
      service.MoveQueued(10, false);  // running downloads cannot be moved
      service.MoveQueued(30, false);  // paused downloads cannot be moved
      service.MoveQueued(999, false);
      CHECK(service.Snapshot().jobs[0].id == 10 && service.Snapshot().jobs[1].id == 20);
      service.MoveQueued(20, false);
      service.MoveQueued(20, false);  // last queued: stays put
      CHECK(service.Snapshot().jobs[3].id == 20);
    }
    {
      InstallService restored(reorder_runner, sessions, reorder_root.string());
      const auto snapshot = restored.Snapshot();
      CHECK(snapshot.jobs.size() == 4);
      CHECK(snapshot.jobs[0].id == 10 && snapshot.jobs[1].id == 40 &&
            snapshot.jobs[2].id == 30 && snapshot.jobs[3].id == 20);
      CHECK(JobOf(snapshot, 30)->state == InstallJobState::kPaused);
    }
    std::filesystem::remove_all(reorder_root);
  }
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

    // Download now: the chosen download goes first and starts; the running one waits right behind it.
    runner.block = true;
    service.Install(610);
    AwaitAt(__LINE__, service, [](const InstallSnapshot& s) { auto* job = JobOf(s, 610); return job && job->state == InstallJobState::kRunning; });
    service.Install(620);
    service.Install(630);
    service.DownloadNow(630);
    AwaitAt(__LINE__, service, [](const InstallSnapshot& s) {
      auto* now = JobOf(s, 630); auto* before = JobOf(s, 610);
      return now && now->state == InstallJobState::kRunning && before && before->state == InstallJobState::kQueued;
    });
    {
      const auto snapshot = service.Snapshot();
      const auto at = [&](GameId id) {
        return std::find_if(snapshot.jobs.begin(), snapshot.jobs.end(), [id](const InstallJobView& job) { return job.id == id; }) -
               snapshot.jobs.begin();
      };
      CHECK(at(630) == 0 && at(610) == 1 && at(620) > 1);
      CHECK(JobOf(snapshot, 620)->state == InstallJobState::kQueued);
    }
    // When it finishes, the one it interrupted carries on by itself.
    service.Cancel(630);
    AwaitAt(__LINE__, service, [](const InstallSnapshot& s) {
      auto* resumed = JobOf(s, 610);
      return !JobOf(s, 630) && resumed && resumed->state == InstallJobState::kRunning;
    });
    service.DownloadNow(610);  // already running: nothing changes
    CHECK(JobOf(service.Snapshot(), 610)->state == InstallJobState::kRunning);
    service.Cancel(620);
    service.Cancel(610);
    runner.block = false;
    AwaitAt(__LINE__, service, [&](const InstallSnapshot& s) {
      std::lock_guard<std::mutex> lock(runner.mutex);
      return !JobOf(s, 610) && !JobOf(s, 620) && !JobOf(s, 630) && runner.removed.size() >= 3;
    });
    {
      std::lock_guard<std::mutex> lock(runner.mutex);
      runner.removed.clear();
    }

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

    // Without a session (Playden may still be restoring it) the job waits, queued, and says why.
    sessions.present = false;
    service.Install(510);
    AwaitAt(__LINE__, service, [](const InstallSnapshot& s) {
      auto* job = JobOf(s, 510);
      return job && job->state == InstallJobState::kQueued && job->failure == "Waiting for the Steam sign-in.";
    });
    // Once the sign-in is there it carries on by itself.
    sessions.present = true;
    service.Resume(510);
    AwaitAt(__LINE__, service, [](const InstallSnapshot& s) { return Installed(s, 510) && !JobOf(s, 510); });
    // A waiting job the player pauses comes back paused after a restart.
    sessions.present = false;
    service.Install(500);
    AwaitAt(__LINE__, service, [](const InstallSnapshot& s) { auto* job = JobOf(s, 500); return job && !job->failure.empty(); });
    service.Pause(500);
    AwaitAt(__LINE__, service, [](const InstallSnapshot& s) { auto* job = JobOf(s, 500); return job && job->state == InstallJobState::kPaused; });
  }
  {
    // The registry survives a restart: the finished 300 and failed 500 are found again.
    std::ifstream registry(root / ".playden-games");
    const std::string text((std::istreambuf_iterator<char>(registry)), std::istreambuf_iterator<char>());
    CHECK(text.find("300") != std::string::npos);
    InstallService again(runner, sessions, root.string());
    CHECK(Installed(again.Snapshot(), 300));
    // 500 was paused before it wrote anything; it comes back paused instead of disappearing.
    const auto restored = again.Snapshot();
    auto* failed = JobOf(restored, 500);
    CHECK(failed && failed->state == InstallJobState::kPaused && !failed->failure.empty());
  }
  std::filesystem::remove_all(root);

  {
    // Downloads that were running or queued when Playden stopped carry on after the restart (a
    // game started, or Playden was restarted); one the player paused waits for Resume.
    const auto resume_root = std::filesystem::temp_directory_path() / ("playden-install-resume-" + std::to_string(::getpid()));
    std::filesystem::remove_all(resume_root);
    Sessions resume_sessions;
    Runner resume_runner;
    {
      InstallService service(resume_runner, resume_sessions, resume_root.string());
      resume_runner.block = true;
      service.Install(600);
      AwaitAt(__LINE__, service, [](const InstallSnapshot& s) { auto* job = JobOf(s, 600); return job && job->state == InstallJobState::kRunning; });
      service.Install(700);
      service.Install(800);
      service.Pause(800);
    }
    std::ifstream registry(resume_root / ".playden-games");
    const std::string text((std::istreambuf_iterator<char>(registry)), std::istreambuf_iterator<char>());
    auto line_of = [&](const std::string& id) {
      const auto at = text.find(id + "\t");
      return at == std::string::npos ? std::string() : text.substr(at, text.find('\n', at) - at);
    };
    CHECK(!line_of("600").empty() && line_of("600").find("paused") == std::string::npos);
    CHECK(!line_of("700").empty() && line_of("700").find("paused") == std::string::npos);
    CHECK(line_of("800").find("\tpaused") != std::string::npos);
    resume_runner.block = false;
    InstallService again(resume_runner, resume_sessions, resume_root.string());
    AwaitAt(__LINE__, again, [](const InstallSnapshot& s) { return Installed(s, 600) && Installed(s, 700); });
    const auto restored = again.Snapshot();
    auto* paused = JobOf(restored, 800);
    CHECK(paused && paused->state == InstallJobState::kPaused);
    std::filesystem::remove_all(resume_root);
  }

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
