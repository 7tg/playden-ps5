#include "steam/install_service.h"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace playden::steam {
std::vector<InstallRequest> ParseInstallRequests(const std::string& text) {
  std::vector<InstallRequest> requests;
  std::string_view remaining(text);
  while (!remaining.empty() && requests.size() < 64) {
    const auto newline = remaining.find('\n');
    auto line = remaining.substr(0, newline);
    remaining = newline == std::string_view::npos ? std::string_view{} : remaining.substr(newline + 1);
    const auto token = [&line]() {
      const auto first = line.find_first_not_of(" \t\r\v\f");
      if (first == std::string_view::npos) { line = {}; return std::string_view{}; }
      line.remove_prefix(first);
      const auto end = line.find_first_of(" \t\r\v\f");
      const auto word = line.substr(0, end);
      line = end == std::string_view::npos ? std::string_view{} : line.substr(end);
      return word;
    };
    auto id = token();
    if (id.empty()) continue;
    InstallRequest request;
    if (id == "uninstall" || id == "cancel") {
      request.action = id == "uninstall" ? InstallRequest::Action::kUninstall : InstallRequest::Action::kCancel;
      id = token();
    }
    if (id.empty() || !token().empty()) continue;
    const auto parsed = std::from_chars(id.data(), id.data() + id.size(), request.app);
    if (parsed.ec != std::errc{} || parsed.ptr != id.data() + id.size() || request.app == 0) continue;
    requests.push_back(request);
  }
  return requests;
}
namespace {
std::int64_t RateNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}

constexpr const char* kRecord = ".playden-install.json";
constexpr const char* kJournal = ".playden-download";
constexpr const char* kRegistry = ".playden-games";
constexpr const char* kDefaultVolume = ".playden-default-volume";
constexpr const char* kVolumeUnavailable = "The game's storage is not available.";
constexpr std::chrono::seconds kSessionRetry{5};

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

// Volume ids are short identifiers written into the registry; anything else is refused.
bool ValidVolumeId(std::string_view id) {
  if (id.empty() || id.size() > 32) return false;
  for (const char value : id)
    if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '-' || value == '_')) return false;
  return true;
}

struct RegistryEntry {
  GameId id = 0;
  std::string volume_id;
  std::uint64_t install_bytes = 0;
  bool queued = false;  // written for a job in Downloads: "<id>\t<volume>\t0"
  bool paused = false;  // "<id>\t<volume>\t0\tpaused": the player paused it, or it failed
};

// One line per game: "<app id>" (legacy, primary volume) or "<app id>\t<volume id>\t<install bytes>",
// followed by "\tpaused" for a job that should wait for Resume after a restart.
std::optional<RegistryEntry> ParseRegistryLine(std::string_view line) {
  RegistryEntry entry;
  const auto first_tab = line.find('\t');
  const auto id_text = line.substr(0, first_tab);
  const auto parsed = std::from_chars(id_text.data(), id_text.data() + id_text.size(), entry.id);
  if (parsed.ec != std::errc{} || parsed.ptr != id_text.data() + id_text.size() || entry.id == 0) return std::nullopt;
  if (first_tab == std::string_view::npos) return entry;
  const auto rest = line.substr(first_tab + 1);
  const auto second_tab = rest.find('\t');
  const auto volume = rest.substr(0, second_tab);
  if (!ValidVolumeId(volume)) return std::nullopt;
  entry.volume_id = std::string(volume);
  if (second_tab != std::string_view::npos) {
    const auto fields = rest.substr(second_tab + 1);
    const auto third_tab = fields.find('\t');
    const auto bytes = fields.substr(0, third_tab);
    std::from_chars(bytes.data(), bytes.data() + bytes.size(), entry.install_bytes);
    entry.queued = entry.install_bytes == 0;
    entry.paused = third_tab != std::string_view::npos && fields.substr(third_tab + 1) == "paused";
  }
  return entry;
}

}  // namespace

InstallService::InstallService(InstallRunner& runner, SessionRepository& sessions, std::string games_root)
    : InstallService(runner, sessions, std::vector<InstallVolume>{{kPrimaryVolumeId, "Console storage", std::move(games_root), true}}) {}

InstallService::InstallService(InstallRunner& runner, SessionRepository& sessions, std::vector<InstallVolume> volumes)
    : runner_(runner), sessions_(sessions), volumes_(std::move(volumes)) {
  if (volumes_.empty()) volumes_.push_back({kPrimaryVolumeId, "Console storage", "games", true});
  volumes_.front().available = true;
  default_volume_id_ = volumes_.front().id;
  Scan();
  worker_ = std::thread([this] { Run(); });
}

InstallService::~InstallService() { Shutdown(); }

const InstallVolume* InstallService::FindVolume(const std::string& id) const {
  for (const auto& volume : volumes_)
    if (volume.id == id) return &volume;
  return nullptr;
}

std::string InstallService::RootOn(const std::string& volume_id, GameId id) const {
  const auto* volume = FindVolume(volume_id);
  return volume ? volume->games_root + "/" + std::to_string(id) : std::string();
}

std::string InstallService::LockedRootFor(GameId id) const {
  for (const auto& job : jobs_)
    if (job.view.id == id) return RootOn(job.view.volume_id, id);
  for (const auto& game : installed_)
    if (game.id == id) return RootOn(game.volume_id, id);
  return RootOn(default_volume_id_, id);
}

std::string InstallService::RootFor(GameId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return LockedRootFor(id);
}

// The primary volume keeps a registry of app IDs Playden manages and the volume of each;
// directory listing is avoided because the console's readdir did not return entries reliably.
void InstallService::Scan() {
  const auto& primary = volumes_.front().games_root;
  std::vector<RegistryEntry> entries;
  if (FILE* registry = std::fopen((primary + "/" + kRegistry).c_str(), "r")) {
    char line[128];
    while (std::fgets(line, sizeof(line), registry) && entries.size() < kMaxJobs * 4) {
      const auto length = std::strcspn(line, "\r\n");
      auto entry = ParseRegistryLine(std::string_view(line, length));
      if (!entry) continue;
      if (entry->volume_id.empty()) entry->volume_id = volumes_.front().id;
      if (std::none_of(entries.begin(), entries.end(), [&](const RegistryEntry& other) { return other.id == entry->id; }))
        entries.push_back(std::move(*entry));
    }
    std::fclose(registry);
  }
  if (FILE* file = std::fopen((primary + "/" + kDefaultVolume).c_str(), "r")) {
    char line[64] = {};
    if (std::fgets(line, sizeof(line), file)) {
      const std::string id(line, std::strcspn(line, "\r\n"));
      if (const auto* volume = FindVolume(id); volume && volume->available) default_volume_id_ = id;
    }
    std::fclose(file);
  }
  for (const auto& entry : entries) {
    const auto* volume = FindVolume(entry.volume_id);
    if (!volume || !volume->available) {
      // Keep games on an absent drive listed so they come back when it does.
      installed_.push_back({entry.id, entry.install_bytes, RootOn(entry.volume_id, entry.id), entry.volume_id, false});
      continue;
    }
    const auto root = RootOn(entry.volume_id, entry.id);
    if (Exists(root + "/" + kRecord)) {
      installed_.push_back({entry.id, RecordedBytes(root + "/" + kRecord), root, entry.volume_id, true});
    } else if ((Exists(root + "/" + kJournal) || entry.queued) && jobs_.size() < kMaxJobs) {
      // A job that failed or waited before writing anything (no journal yet) stays in
      // Downloads too, so a failure while Steam connects doesn't lose the queued game. A job that
      // was queued or downloading when Playden stopped (a game started, or Playden restarted)
      // carries on by itself; one the player paused, or that failed, waits for Resume.
      Job job;
      job.view.id = entry.id;
      if (entry.queued && !entry.paused) {
        job.view.state = InstallJobState::kQueued;
      } else {
        job.view.state = InstallJobState::kPaused;
        job.view.failure = "Interrupted. Resume to continue.";
      }
      job.view.volume_id = entry.volume_id;
      jobs_.push_back(job);
    }
  }
  std::sort(installed_.begin(), installed_.end(),
            [](const InstalledGame& left, const InstalledGame& right) { return left.id < right.id; });
  ++revision_;
}

void InstallService::SaveRegistry() {
  const auto& primary = volumes_.front().games_root;
  mkdir(primary.c_str(), 0777);
  const auto path = primary + "/" + kRegistry;
  const auto temporary = path + ".tmp";
  FILE* registry = std::fopen(temporary.c_str(), "w");
  if (!registry) return;
  for (const auto& game : installed_)
    std::fprintf(registry, "%u\t%s\t%llu\n", game.id, game.volume_id.c_str(),
                 static_cast<unsigned long long>(game.install_bytes));
  for (const auto& job : jobs_) {
    const bool waits = job.view.state == InstallJobState::kPaused || job.view.state == InstallJobState::kFailed;
    std::fprintf(registry, "%u\t%s\t0%s\n", job.view.id, job.view.volume_id.c_str(), waits ? "\tpaused" : "");
  }
  if (std::fclose(registry) == 0) std::rename(temporary.c_str(), path.c_str());
}

void InstallService::SaveDefaultVolume() {
  const auto& primary = volumes_.front().games_root;
  mkdir(primary.c_str(), 0777);
  const auto path = primary + "/" + kDefaultVolume;
  const auto temporary = path + ".tmp";
  FILE* file = std::fopen(temporary.c_str(), "w");
  if (!file) return;
  std::fprintf(file, "%s\n", default_volume_id_.c_str());
  if (std::fclose(file) == 0) std::rename(temporary.c_str(), path.c_str());
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
  job.view.volume_id = default_volume_id_;
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
  job->rate.Reset(); job->view.bytes_per_second.reset(); job->view.eta_seconds.reset();
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

void InstallService::DownloadNow(GameId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = std::find_if(jobs_.begin(), jobs_.end(), [id](const Job& job) { return job.view.id == id; });
  if (found == jobs_.end() || found->view.state == InstallJobState::kRunning || found->cancel_requested) return;
  Job job = *found;
  jobs_.erase(found);
  job.view.state = InstallJobState::kQueued;
  job.view.failure.clear();
  jobs_.push_front(job);
  // The running download stops and goes back in the queue right behind it, so it resumes next.
  if (active_ && *active_ != id) {
    const auto running = std::find_if(jobs_.begin(), jobs_.end(), [this](const Job& job) { return job.view.id == *active_; });
    if (running != jobs_.end() && !running->cancel_requested) {
      Job stopped = *running;
      jobs_.erase(running);
      stopped.view.state = InstallJobState::kQueued;
      stopped.view.failure.clear();
      jobs_.insert(jobs_.begin() + 1, stopped);
      active_cancel_.store(true);
    }
  }
  Publish();
  wake_.notify_all();
}

void InstallService::MoveQueued(GameId id, bool up) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto movable = [this](const Job& job) {
    return job.view.state == InstallJobState::kQueued && !job.cancel_requested && active_ != job.view.id;
  };
  const auto found = std::find_if(jobs_.begin(), jobs_.end(), [id](const Job& job) { return job.view.id == id; });
  if (found == jobs_.end() || !movable(*found)) return;
  auto other = found;
  while (up ? other != jobs_.begin() : other + 1 != jobs_.end()) {
    if (up) --other; else ++other;
    if (!movable(*other)) continue;
    std::iter_swap(found, other);
    Publish();
    wake_.notify_all();
    return;
  }
}

void InstallService::Cancel(GameId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* job = Find(id);
  if (!job) return;
  job->cancel_requested = true;
  if (active_ == id) {
    active_cancel_.store(true);
  } else {
    const auto* volume = FindVolume(job->view.volume_id);
    const auto root = volume && volume->available ? RootOn(job->view.volume_id, id) : std::string();
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [id](const Job& entry) { return entry.view.id == id; }),
                jobs_.end());
    if (!root.empty()) removals_.push_back(root);
  }
  Publish();
  wake_.notify_all();
}

void InstallService::Uninstall(GameId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = std::find_if(installed_.begin(), installed_.end(), [id](const InstalledGame& game) { return game.id == id; });
  if (found == installed_.end() || !found->available) return;  // nothing can be deleted on an absent drive
  const auto root = RootOn(found->volume_id, id);
  installed_.erase(found);
  if (!root.empty()) removals_.push_back(root);
  Publish();
  wake_.notify_all();
}

void InstallService::SetDefaultVolume(const std::string& volume_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto* volume = FindVolume(volume_id);
  if (!volume || !volume->available || volume_id == default_volume_id_) return;
  default_volume_id_ = volume_id;
  SaveDefaultVolume();
  ++revision_;
}

InstallSnapshot InstallService::Snapshot() {
  std::lock_guard<std::mutex> lock(mutex_);
  InstallSnapshot snapshot;
  const auto now = RateNowMs();
  for (auto& job : jobs_) {
    DownloadEstimate estimate;
    if (job.view.state == InstallJobState::kRunning)
      estimate = job.rate.Shown(now, job.view.done_bytes, job.view.total_bytes);
    if (job.view.bytes_per_second != estimate.bytes_per_second || job.view.eta_seconds != estimate.eta_seconds) {
      job.view.bytes_per_second = estimate.bytes_per_second; job.view.eta_seconds = estimate.eta_seconds;
      ++revision_; // Time-only telemetry changes do not write the install registry.
    }
  }
  snapshot.revision = revision_;
  for (const auto& job : jobs_) snapshot.jobs.push_back(job.view);
  snapshot.installed = installed_;
  snapshot.volumes = volumes_;
  snapshot.default_volume_id = default_volume_id_;
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
    std::string next_root;
    std::optional<std::string> removal;
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
          const auto* volume = FindVolume(job.view.volume_id);
          if (!volume || !volume->available) {
            job.view.state = InstallJobState::kFailed;
            job.view.failure = kVolumeUnavailable;
            Publish();
            continue;
          }
          job.view.state = InstallJobState::kRunning;
          job.rate.Reset();
          job.view.bytes_per_second.reset(); job.view.eta_seconds.reset();
          next = job.view.id;
          next_root = RootOn(job.view.volume_id, job.view.id);
          active_ = next;
          active_cancel_.store(false);
          Publish();
          break;
        }
      }
    }
    if (removal) {
      runner_.Remove(*removal);
      std::lock_guard<std::mutex> lock(mutex_);
      Publish();
      continue;
    }
    if (!next) continue;

    InstallRunner::Result result;
    const auto session = sessions_.Load();
    if (!session.ok() || !session.value()) {
      // No saved sign-in yet: right after Playden starts it may still be restoring it. The job
      // stays queued and is tried again shortly, instead of failing without a word in the log.
      std::unique_lock<std::mutex> lock(mutex_);
      active_.reset();
      if (auto* job = Find(*next); job && job->view.state == InstallJobState::kRunning) {
        job->view.state = InstallJobState::kQueued;
        job->view.failure = "Waiting for the Steam sign-in.";
      }
      Publish();
      // Any request (Install, Resume, Shutdown) also ends the wait.
      if (!stopping_) wake_.wait_for(lock, kSessionRetry);
      continue;
    } else {
      result = runner_.Install(*session.value(), *next, next_root, active_cancel_,
                               [this, id = *next](std::uint64_t done, std::uint64_t total) {
                                 std::lock_guard<std::mutex> lock(mutex_);
                                 if (auto* job = Find(id)) {
                                   if (job->view.state == InstallJobState::kRunning) job->rate.Record(done, RateNowMs());
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
        const auto volume_id = job->view.volume_id;
        jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const Job& entry) { return entry.view.id == *next; }),
                    jobs_.end());
        installed_.push_back({*next, result.install_bytes, next_root, volume_id, true});
      } else if (job && job->view.state == InstallJobState::kRunning && result.canceled && stopping_) {
        // Playden is closing: the download stays queued, so the next start carries on with it.
        job->view.state = InstallJobState::kQueued;
      } else if (job && job->view.state == InstallJobState::kRunning) {
        // A Pause or Resume that arrived while the run was stopping already chose the state.
        job->view.state = result.canceled ? InstallJobState::kPaused : InstallJobState::kFailed;
        job->view.failure = result.canceled ? std::string() : result.failure;
      }
      Publish();
    }
    if (remove_files) runner_.Remove(next_root);
  }
}

}  // namespace playden::steam
