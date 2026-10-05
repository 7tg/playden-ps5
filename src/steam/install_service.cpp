#include "steam/install_service.h"

#include <sys/stat.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>

namespace playden::steam {
namespace {

constexpr const char* kRecord = ".playden-install.json";
constexpr const char* kJournal = ".playden-download";
constexpr const char* kRegistry = ".playden-games";

bool Exists(const std::string& path) {
  struct stat info {};
  return stat(path.c_str(), &info) == 0;
}

std::uint64_t RecordedBytes(const std::string& record_path) {
  FILE* file = std::fopen(record_path.c_str(), "r");
  if (!file) return 0;
  char text[4096] = {};
  const auto read = std::fread(text, 1, sizeof(text) - 1, file);
  std::fclose(file);
  const std::string_view view(text, read);
  const auto key = view.find("\"install_bytes\":");
  if (key == std::string_view::npos) return 0;
  const char* begin = view.data() + key + std::strlen("\"install_bytes\":");
  std::uint64_t value = 0;
  std::from_chars(begin, view.data() + view.size(), value);
  return value;
}

}  // namespace

InstallService::InstallService(InstallRunner& runner, SessionRepository& sessions, std::string games_root)
    : runner_(runner), sessions_(sessions), games_root_(std::move(games_root)) {
  Scan();
  worker_ = std::thread([this] { Run(); });
}

InstallService::~InstallService() { Shutdown(); }

std::string InstallService::RootFor(GameId id) const { return games_root_ + "/" + std::to_string(id); }

// The games root keeps a registry of app IDs Playden manages; directory listing is avoided because
// the console's readdir did not return entries reliably.
void InstallService::Scan() {
  std::vector<GameId> ids;
  if (FILE* registry = std::fopen((games_root_ + "/" + kRegistry).c_str(), "r")) {
    char line[64];
    while (std::fgets(line, sizeof(line), registry) && ids.size() < kMaxJobs * 4) {
      std::uint32_t id = 0;
      const auto length = std::strcspn(line, "\r\n");
      const auto parsed = std::from_chars(line, line + length, id);
      if (parsed.ec == std::errc{} && parsed.ptr == line + length && id != 0 &&
          std::find(ids.begin(), ids.end(), id) == ids.end())
        ids.push_back(id);
    }
    std::fclose(registry);
  }
  for (const auto id : ids) {
    const auto root = RootFor(id);
    if (Exists(root + "/" + kRecord)) {
      installed_.push_back({id, RecordedBytes(root + "/" + kRecord), root});
    } else if (Exists(root + "/" + kJournal) && jobs_.size() < kMaxJobs) {
      Job job;
      job.view.id = id;
      job.view.state = InstallJobState::kPaused;
      job.view.failure = "Interrupted. Resume to continue.";
      jobs_.push_back(job);
    }
  }
  std::sort(installed_.begin(), installed_.end(),
            [](const InstalledGame& left, const InstalledGame& right) { return left.id < right.id; });
  ++revision_;
}

void InstallService::SaveRegistry() {
  std::vector<GameId> ids;
  for (const auto& game : installed_) ids.push_back(game.id);
  for (const auto& job : jobs_) ids.push_back(job.view.id);
  mkdir(games_root_.c_str(), 0777);
  const auto path = games_root_ + "/" + kRegistry;
  const auto temporary = path + ".tmp";
  FILE* registry = std::fopen(temporary.c_str(), "w");
  if (!registry) return;
  for (const auto id : ids) std::fprintf(registry, "%u\n", id);
  if (std::fclose(registry) == 0) std::rename(temporary.c_str(), path.c_str());
}

InstallService::Job* InstallService::Find(GameId id) {
  for (auto& job : jobs_)
    if (job.view.id == id) return &job;
  return nullptr;
}

void InstallService::Publish() {
  ++revision_;
  SaveRegistry();
}

void InstallService::Install(GameId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopping_ || id == 0 || Find(id) || jobs_.size() >= kMaxJobs) return;
  if (std::any_of(installed_.begin(), installed_.end(), [id](const InstalledGame& game) { return game.id == id; })) return;
  Job job;
  job.view.id = id;
  jobs_.push_back(job);
  Publish();
  wake_.notify_all();
}

void InstallService::Pause(GameId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* job = Find(id);
  if (!job) return;
  if (active_ == id) active_cancel_.store(true);
  job->view.state = InstallJobState::kPaused;
  job->view.failure.clear();
  Publish();
}

void InstallService::Resume(GameId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* job = Find(id);
  if (!job || job->view.state == InstallJobState::kRunning) return;
  job->view.state = InstallJobState::kQueued;
  job->view.failure.clear();
  Publish();
  wake_.notify_all();
}

void InstallService::Cancel(GameId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* job = Find(id);
  if (!job) return;
  job->cancel_requested = true;
  if (active_ == id) {
    active_cancel_.store(true);
  } else {
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [id](const Job& entry) { return entry.view.id == id; }),
                jobs_.end());
    removals_.push_back(id);
  }
  Publish();
  wake_.notify_all();
}

void InstallService::Uninstall(GameId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = std::find_if(installed_.begin(), installed_.end(), [id](const InstalledGame& game) { return game.id == id; });
  if (found == installed_.end()) return;
  installed_.erase(found);
  removals_.push_back(id);
  Publish();
  wake_.notify_all();
}

InstallSnapshot InstallService::Snapshot() {
  std::lock_guard<std::mutex> lock(mutex_);
  InstallSnapshot snapshot;
  snapshot.revision = revision_;
  for (const auto& job : jobs_) snapshot.jobs.push_back(job.view);
  snapshot.installed = installed_;
  return snapshot;
}

void InstallService::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ && !worker_.joinable()) return;
    stopping_ = true;
    active_cancel_.store(true);
  }
  wake_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void InstallService::Run() {
  for (;;) {
    std::optional<GameId> next;
    std::optional<GameId> removal;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this] {
        return stopping_ || !removals_.empty() ||
               std::any_of(jobs_.begin(), jobs_.end(), [](const Job& job) { return job.view.state == InstallJobState::kQueued; });
      });
      if (stopping_) return;
      if (!removals_.empty()) {
        removal = removals_.front();
        removals_.pop_front();
      } else {
        for (auto& job : jobs_) {
          if (job.view.state != InstallJobState::kQueued) continue;
          job.view.state = InstallJobState::kRunning;
          next = job.view.id;
          active_ = next;
          active_cancel_.store(false);
          Publish();
          break;
        }
      }
    }
    if (removal) {
      runner_.Remove(RootFor(*removal));
      std::lock_guard<std::mutex> lock(mutex_);
      Publish();
      continue;
    }
    if (!next) continue;

    InstallRunner::Result result;
    const auto session = sessions_.Load();
    if (!session.ok() || !session.value()) {
      result.failure = "Sign in to Steam to install.";
    } else {
      result = runner_.Install(*session.value(), *next, RootFor(*next), active_cancel_,
                               [this, id = *next](std::uint64_t done, std::uint64_t total) {
                                 std::lock_guard<std::mutex> lock(mutex_);
                                 if (auto* job = Find(id)) {
                                   job->view.done_bytes = done;
                                   job->view.total_bytes = total;
                                   ++revision_;  // progress only; the registry is unchanged
                                 }
                               });
    }

    bool remove_files = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      active_.reset();
      auto* job = Find(*next);
      if (job && job->cancel_requested) {
        jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const Job& entry) { return entry.view.id == *next; }),
                    jobs_.end());
        remove_files = true;
      } else if (job && result.ok) {
        jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const Job& entry) { return entry.view.id == *next; }),
                    jobs_.end());
        installed_.push_back({*next, result.install_bytes, RootFor(*next)});
      } else if (job && job->view.state == InstallJobState::kRunning) {
        // A Pause or Resume that arrived while the run was stopping already chose the state.
        job->view.state = result.canceled ? InstallJobState::kPaused : InstallJobState::kFailed;
        job->view.failure = result.canceled ? std::string() : result.failure;
      }
      Publish();
    }
    if (remove_files) runner_.Remove(RootFor(*next));
  }
}

}  // namespace playden::steam
