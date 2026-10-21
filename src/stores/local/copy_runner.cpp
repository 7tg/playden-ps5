#include "stores/local/copy_runner.h"

#include "platform/directory_walk.h"
#include "steam/backend/depot_installer.h"
#include "stores/local/scanner.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace playden::stores::local {
namespace {

constexpr const char* kRecord = ".playden-install.json";  // DepotInstaller::kInstallRecord
constexpr int kOpenFolder = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
constexpr int kOpenFile = O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;

bool Exists(const std::string& path) {
  struct stat info {};
  return ::stat(path.c_str(), &info) == 0;
}

std::optional<std::uint64_t> FileSize(const std::string& path) {
  struct stat info {};
  if (::stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) return std::nullopt;
  return info.st_size > 0 ? static_cast<std::uint64_t>(info.st_size) : 0;
}

std::vector<std::string> Split(const std::string& path) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  for (;;) {
    const auto slash = path.find('/', start);
    parts.push_back(path.substr(start, slash == std::string::npos ? std::string::npos : slash - start));
    if (slash == std::string::npos) return parts;
    start = slash + 1;
  }
}

bool SameTime(const struct stat& info, const CopyFile& file) {
#if defined(__APPLE__)
  const auto seconds = static_cast<std::int64_t>(info.st_mtimespec.tv_sec);
  const auto nsec = static_cast<std::int64_t>(info.st_mtimespec.tv_nsec);
#else
  const auto seconds = static_cast<std::int64_t>(info.st_mtim.tv_sec);
  const auto nsec = static_cast<std::int64_t>(info.st_mtim.tv_nsec);
#endif
  return (seconds < 0 ? 0 : seconds) == file.mtime && nsec == file.mtime_nsec;
}

bool AsPlanned(const struct stat& info, const CopyFile& file) {
  return S_ISREG(info.st_mode) && info.st_size >= 0 && static_cast<std::uint64_t>(info.st_size) == file.bytes &&
         SameTime(info, file);
}

// Opens the plan's files from the game folder, one walk step at a time (platform/directory_walk.h):
// the folders of the current file stay open, so files in the same folder open in one step.
class SourceWalk {
 public:
  explicit SourceWalk(std::string source) : source_(std::move(source)) {}
  ~SourceWalk() {
    for (auto& level : chain_) ::close(level.fd);
    if (root_.fd >= 0) ::close(root_.fd);
  }
  SourceWalk(const SourceWalk&) = delete;
  SourceWalk& operator=(const SourceWalk&) = delete;

  // A descriptor for the file, or -1 with errno.
  int OpenFile(const std::string& relative) {
    if (root_.fd < 0) {
      root_.fd = platform::OpenDirectoryWalk(source_);
      if (root_.fd < 0) return -1;
    }
    const auto parts = Split(relative);
    std::size_t common = 0;
    while (common < chain_.size() && common + 1 < parts.size() && chain_[common].name == parts[common]) ++common;
    while (chain_.size() > common) {
      ::close(chain_.back().fd);
      chain_.pop_back();
    }
    for (std::size_t index = common; index + 1 < parts.size(); ++index) {
      Level& parent = chain_.empty() ? root_ : chain_.back();
      const int fd = platform::OpenWalkChild(parent.fd, parts[index].c_str(), kOpenFolder, Listed(parent, parts[index]));
      if (fd < 0) return -1;
      chain_.push_back(Level{parts[index], fd, {}, false});
    }
    Level& folder = chain_.empty() ? root_ : chain_.back();
    return platform::OpenWalkChild(folder.fd, parts.back().c_str(), kOpenFile, Listed(folder, parts.back()));
  }

 private:
  struct Level {
    std::string name;
    int fd = -1;
    std::unordered_map<std::string, std::uint64_t> listed;
    bool listed_read = false;
  };
  // A walk by path checks each step against its folder's listing; the listing is read once per
  // folder instead of once per file (0: the walk reads it itself).
  std::uint64_t Listed(Level& level, const std::string& name) {
#if defined(PLAYDEN_WALK_BY_PATH)
    if (!level.listed_read) {
      level.listed_read = true;
      const int listing_fd = ::dup(level.fd);
      DIR* listing = listing_fd >= 0 ? ::fdopendir(listing_fd) : nullptr;
      if (!listing && listing_fd >= 0) ::close(listing_fd);
      if (listing) {
        while (const auto* entry = ::readdir(listing)) level.listed[entry->d_name] = entry->d_fileno;
        ::closedir(listing);
      }
    }
    const auto found = level.listed.find(name);
    return found == level.listed.end() ? 0 : found->second;
#else
    (void)level;
    (void)name;
    return 0;
#endif
  }

  std::string source_;
  Level root_;
  std::vector<Level> chain_;
};

enum class CopyStatus { kOk, kChanged, kCanceled, kReadFailed, kWriteFailed, kNoSpace };

bool WriteAll(int fd, const char* data, std::size_t size, int* error) {
  while (size > 0) {
    const auto written = ::write(fd, data, size);
    if (written < 0) {
      if (errno == EINTR) continue;
      *error = errno;
      return false;
    }
    data += written;
    size -= static_cast<std::size_t>(written);
  }
  return true;
}

CopyStatus CopyOne(SourceWalk& walk, const CopyFile& file, const std::string& destination, std::vector<char>& buffer,
                   const std::atomic<bool>& cancel, const std::function<void(std::uint64_t)>& copied_bytes) {
  const int in = walk.OpenFile(file.path);
  if (in < 0) {
    const int error = errno;
    // Gone, a link or a folder in its place, or another file under its name.
    if (error == ENOENT || error == ENOTDIR || error == ELOOP || error == EMLINK || error == ESTALE || error == EISDIR)
      return CopyStatus::kChanged;
    return CopyStatus::kReadFailed;
  }
  struct stat before {};
  if (::fstat(in, &before) != 0) { ::close(in); return CopyStatus::kReadFailed; }
  if (!AsPlanned(before, file)) { ::close(in); return CopyStatus::kChanged; }
  const auto part = destination + ".part";
  const int out = ::open(part.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
  if (out < 0) {
    const int error = errno;
    ::close(in);
    return error == ENOSPC ? CopyStatus::kNoSpace : CopyStatus::kWriteFailed;
  }
  const auto stop = [&](CopyStatus status) {
    ::close(in);
    ::close(out);
    std::remove(part.c_str());
    return status;
  };
  std::uint64_t copied = 0;
  for (;;) {
    if (cancel.load()) return stop(CopyStatus::kCanceled);
    const auto count = ::read(in, buffer.data(), buffer.size());
    if (count < 0) {
      if (errno == EINTR) continue;
      return stop(CopyStatus::kReadFailed);
    }
    if (count == 0) break;
    if (copied + static_cast<std::uint64_t>(count) > file.bytes) return stop(CopyStatus::kChanged);  // it grew
    int error = 0;
    if (!WriteAll(out, buffer.data(), static_cast<std::size_t>(count), &error))
      return stop(error == ENOSPC ? CopyStatus::kNoSpace : CopyStatus::kWriteFailed);
    copied += static_cast<std::uint64_t>(count);
    copied_bytes(static_cast<std::uint64_t>(count));
  }
  struct stat after {};
  if (copied != file.bytes || ::fstat(in, &after) != 0 || !AsPlanned(after, file)) return stop(CopyStatus::kChanged);
  ::close(in);
  if (::fsync(out) != 0) {
    const int error = errno;
    ::close(out);
    std::remove(part.c_str());
    return error == ENOSPC ? CopyStatus::kNoSpace : CopyStatus::kWriteFailed;
  }
  if (::close(out) != 0 || std::rename(part.c_str(), destination.c_str()) != 0) {
    std::remove(part.c_str());
    return CopyStatus::kWriteFailed;
  }
  return CopyStatus::kOk;
}

bool MakeFolder(const std::string& path) {
  if (::mkdir(path.c_str(), 0777) == 0) return true;
  struct stat info {};
  return errno == EEXIST && ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

// The install record, as the depot installer writes it for another store's game.
bool WriteRecord(const std::string& root, const GameId& game, const CopyPlan& plan) {
  char text[512];
  const int length = std::snprintf(text, sizeof(text),
                                   "{\"version\":1,\"app_id\":%u,\"store\":\"%s\",\"game\":\"%s\",\"install_bytes\":%llu,"
                                   "\"files\":%llu,\"depots\":[]}\n",
                                   plan.runtime_number, std::string(Traits(game.store).key).c_str(), game.value.c_str(),
                                   static_cast<unsigned long long>(plan.total),
                                   static_cast<unsigned long long>(plan.files.size()));
  if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(text)) return false;
  const auto path = root + "/" + kRecord;
  const auto temporary = path + ".tmp";
  FILE* file = std::fopen(temporary.c_str(), "w");
  if (!file) return false;
  bool ok = std::fwrite(text, 1, static_cast<std::size_t>(length), file) == static_cast<std::size_t>(length) &&
            std::fflush(file) == 0 && ::fsync(::fileno(file)) == 0;
  ok = std::fclose(file) == 0 && ok;
  ok = ok && std::rename(temporary.c_str(), path.c_str()) == 0;
  if (!ok) std::remove(temporary.c_str());
  return ok;
}

// Partial files the plan's list doesn't name, so RemoveManagedInstall can remove exactly its paths.
void RemoveParts(const std::string& root, const CopyPlan* plan) {
  if (!plan) return;
  for (const auto& file : plan->files) std::remove((root + "/" + file.path + ".part").c_str());
}

bool IsFolder(const std::string& path) {
  struct stat info {};
  return ::lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool Missing(const std::string& path) {
  struct stat info {};
  return ::lstat(path.c_str(), &info) != 0 && errno == ENOENT;
}

std::string Parent(const std::string& path) {
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos || slash == 0 ? std::string("/") : path.substr(0, slash);
}

bool SyncFolder(const std::string& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) return false;
  const bool ok = ::fsync(fd) == 0;
  ::close(fd);
  return ok;
}

// Playden's own files in a moved game's folder, left out of its file list.
bool PlaydenFile(const std::string& path) {
  return path == kRecord || path == std::string(kRecord) + ".tmp" || path == ".playden-files" ||
         path == ".playden-files.tmp";
}

std::string Basename(const std::string& path) {
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

LocalCopyRunner::LocalCopyRunner(ImportStore& store, LocalCopyOptions options)
    : store_(store), options_(std::move(options)) {
  if (options_.buffer_bytes == 0) options_.buffer_bytes = 1024 * 1024;
}

void LocalCopyRunner::SetChanged(std::function<void()> changed) {
  std::lock_guard<std::mutex> lock(changed_mutex_);
  changed_ = std::move(changed);
}

void LocalCopyRunner::Changed() {
  std::function<void()> changed;
  {
    std::lock_guard<std::mutex> lock(changed_mutex_);
    changed = changed_;
  }
  if (changed) changed();
}

void LocalCopyRunner::Log(const std::string& line) const {
  if (options_.log) options_.log(line);
}

std::string LocalCopyRunner::Enqueue(installs::InstallService& service, const ImportEntry& entry,
                                     const std::string& source, const std::string& volume_id,
                                     std::uint32_t runtime_number) {
  const auto game = GameId::Local(entry.id);
  if (entry.state == ImportState::kCopied) return "The game is already in Playden.";
  if (entry.state != ImportState::kInPlace) return "The game is already being copied.";
  auto snapshot = service.Snapshot();
  if (std::any_of(snapshot.installed.begin(), snapshot.installed.end(),
                  [&](const installs::InstalledGame& installed) { return installed.id == game; }))
    return "The game is already in Playden.";
  if (std::any_of(snapshot.jobs.begin(), snapshot.jobs.end(), [&](const installs::InstallJobView& job) { return job.id == game; }))
    return "The game is already being copied.";
  const auto target = volume_id.empty() ? snapshot.default_volume_id : volume_id;
  const auto volume = std::find_if(snapshot.volumes.begin(), snapshot.volumes.end(),
                                   [&](const installs::InstallVolume& known) { return known.id == target; });
  if (volume == snapshot.volumes.end() || !volume->available) return "The storage is disconnected.";

  auto build = BuildCopyPlan(source);
  if (!build.ok) {
    Log("import copy plan failed id=" + entry.id + " errno=" + std::to_string(build.error) + ": " + build.failure);
    return build.failure;
  }
  auto& plan = build.plan;
  plan.program = entry.program;
  plan.runtime_number = runtime_number;
  if (!PlanHasFile(plan, plan.program)) return "The program is not in the game's folder.";

  const auto root = volume->games_root + "/" + ToKey(game);
  MakeFolder(volume->games_root);
  // A folder left by an earlier copy is Playden's own and goes; anything else stays and refuses.
  if (!steam::RemoveManagedInstall(root) || Exists(root)) {
    Log("import copy refused id=" + entry.id + ": " + root + " is in the way");
    return "A folder in Playden's games folder is in the way of the copy.";
  }
  if (const auto saved = SaveCopyPlan(plan, root); !saved.empty()) {
    steam::RemoveManagedInstall(root);
    return saved;
  }
  const auto queued = store_.QueueCopy(entry.id);
  if (!queued.ok()) {
    steam::RemoveManagedInstall(root);
    return queued.error() == ImportError::kWrongState ? "The game is already being copied."
                                                      : "Playden's list of imported games could not be saved.";
  }
  service.Install(game, plan.total, target);
  snapshot = service.Snapshot();
  const auto job = std::find_if(snapshot.jobs.begin(), snapshot.jobs.end(),
                                [&](const installs::InstallJobView& view) { return view.id == game; });
  if (job == snapshot.jobs.end() || job->state == installs::InstallJobState::kFailed) {
    // Refused at once (free space, the drive, a full Downloads): nothing stays behind.
    const std::string failure = job == snapshot.jobs.end() ? "Downloads is full. Try again later." : job->failure;
    store_.CancelCopy(entry.id, failure);
    if (job != snapshot.jobs.end()) service.Cancel(game);  // its folder goes through Remove
    else steam::RemoveManagedInstall(root);
    Log("import copy refused id=" + entry.id + ": " + failure);
    return failure;
  }
  Log("import copy queued id=" + entry.id + " volume=" + target + " files=" + std::to_string(plan.files.size()) +
      " bytes=" + std::to_string(plan.total));
  return {};
}

installs::InstallRunner::Result LocalCopyRunner::Abandon(const ImportEntry& entry, const std::string& root,
                                                         const CopyPlan* plan, const std::string& failure) {
  RemoveParts(root, plan);
  steam::RemoveManagedInstall(root);
  store_.CancelCopy(entry.id, failure);
  Log("import copy failed id=" + entry.id + ": " + failure);
  Changed();
  Result result;
  result.failure = failure;
  return result;
}

installs::InstallRunner::Result LocalCopyRunner::Install(
    const GameId& id, const std::string& root, const std::atomic<bool>& cancel,
    const std::function<void(std::uint64_t, std::uint64_t, std::uint64_t)>& progress,
    const std::function<void(installs::InstallStep)>& step) {
  Result result;
  if (id.store != Store::kLocal) {
    result.failure = "This game can't be copied.";
    return result;
  }
  if (!store_.loaded()) {
    // Playden is starting: the Local source reads imports.json first.
    result.retry_later = true;
    result.waiting = "Waiting for the list of imported games.";
    return result;
  }
  auto entry = store_.Find(id.value);
  if (!entry) {
    result.failure = "The game is no longer in the library.";
    return result;
  }
  if (entry->state == ImportState::kCopied) {
    result.failure = "The game is already in Playden.";
    return result;
  }
  if (entry->state == ImportState::kInPlace) {
    result.failure = entry->failure.empty() ? kCopyStoppedFailure : entry->failure;
    return result;
  }
  if (entry->state == ImportState::kCopyQueued) {
    const auto started = store_.StartCopy(entry->id);
    if (!started.ok()) {
      result.failure = "Playden's list of imported games could not be saved.";
      return result;
    }
    Changed();
  }
  const auto plan = LoadCopyPlan(root);
  if (!plan) return Abandon(*entry, root, nullptr, kCopyStoppedFailure);

  // What a resume already has: files at their final name and planned size.
  std::vector<char> done(plan->files.size(), 0);
  std::uint64_t done_bytes = 0;
  for (std::size_t index = 0; index < plan->files.size(); ++index) {
    const auto size = FileSize(root + "/" + plan->files[index].path);
    if (size && *size == plan->files[index].bytes) {
      done[index] = 1;
      done_bytes += *size;
    }
  }
  if (progress) progress(done_bytes, plan->total, done_bytes);

  // Free space for what is left, as when it was queued (the console storage reserve left out).
  const auto space = options_.space ? options_.space(root) : steam::QueryVolumeSpace(root);
  const auto remaining = plan->total - done_bytes;
  if (space) {
    if (space->free_bytes < steam::kInstallSpaceMargin || remaining > space->free_bytes - steam::kInstallSpaceMargin) {
      result.failure = "Not enough free space. Make room, then Resume.";
      return result;
    }
  } else if (options_.space || steam::VolumeSpaceSupported()) {
    result.failure = "Free space is unavailable. Check the storage and try again.";
    return result;
  }

  for (const auto& dir : plan->dirs)
    if (!MakeFolder(root + "/" + dir)) {
      result.failure = "Could not write the game files.";
      return result;
    }
  Log("import copy started id=" + entry->id + " files=" + std::to_string(plan->files.size()) +
      " bytes=" + std::to_string(plan->total) + " resumed_bytes=" + std::to_string(done_bytes));

  if (step) step(installs::InstallStep::kDownloading);
  std::vector<char> buffer(options_.buffer_bytes);
  SourceWalk walk(plan->source);
  std::uint64_t copied = done_bytes;
  const auto on_bytes = [&](std::uint64_t count) {
    copied += count;
    if (progress) progress(copied, plan->total, copied);
  };
  for (std::size_t index = 0; index < plan->files.size(); ++index) {
    if (done[index]) continue;
    if (cancel.load()) {
      result.canceled = true;
      return result;
    }
    const auto& file = plan->files[index];
    const auto before = copied;
    const auto status = CopyOne(walk, file, root + "/" + file.path, buffer, cancel, on_bytes);
    if (status != CopyStatus::kOk && copied != before) {
      copied = before;  // the file's bytes went with its .part
      if (progress) progress(copied, plan->total, copied);
    }
    switch (status) {
      case CopyStatus::kOk: break;
      case CopyStatus::kCanceled: result.canceled = true; return result;
      case CopyStatus::kChanged:
        Log("import copy source changed id=" + entry->id + " path=" + file.path);
        return Abandon(*entry, root, &*plan, kCopyChangedFailure);
      case CopyStatus::kReadFailed:
        Log("import copy read failed id=" + entry->id + " path=" + file.path + " errno=" + std::to_string(errno));
        result.failure = "Could not read the game's files. Check the drive, then Resume.";
        return result;
      case CopyStatus::kNoSpace: result.failure = "Not enough free space. Make room, then Resume."; return result;
      case CopyStatus::kWriteFailed:
        Log("import copy write failed id=" + entry->id + " path=" + file.path + " errno=" + std::to_string(errno));
        result.failure = "Could not write the game files.";
        return result;
    }
    if (options_.after_file) options_.after_file(file.path);
  }

  // Verify: every planned file at its size, and the program there.
  for (const auto& file : plan->files) {
    const auto size = FileSize(root + "/" + file.path);
    if (!size || *size != file.bytes) {
      Log("import copy verify failed id=" + entry->id + " path=" + file.path);
      return Abandon(*entry, root, &*plan, "The copy could not be verified. Copy it again.");
    }
  }
  if (!PlanHasFile(*plan, plan->program) || CheckProgram(root, plan->program) != ProgramCheck::kOk)
    return Abandon(*entry, root, &*plan, "The game's program is missing from the copy. Copy it again.");

  // A game set up to play from its drive folder: its PS5 title follows the copy first. Until that
  // commits the drive folder stays the active one; a resume after a restart finds the files copied
  // and comes back here (a relocation already written only finishes its republish).
  {
    LocalRelocation relocation;
    relocation.game = id;
    relocation.runtime_number = plan->runtime_number;
    relocation.root = root;  // its volume: the job's (the relocate hook asks the service)
    const auto relocated = Relocate(relocation);
    if (relocated.outcome == LocalRelocationResult::Outcome::kBusy) {
      Log("import copy waits id=" + entry->id + ": " + relocated.message);
      result.retry_later = true;
      result.waiting = relocated.message.empty() ? "Waiting for the game to close." : relocated.message;
      return result;
    }
    if (relocated.outcome == LocalRelocationResult::Outcome::kFailed) {
      Log("import copy relocation failed id=" + entry->id + ": " + relocated.message);
      result.failure = relocated.message.empty() ? "The game's PS5 title could not follow the copy." : relocated.message;
      return result;
    }
  }

  // Commit: the record makes it a complete managed install, then the plan goes, then imports.json.
  if (!WriteRecord(root, id, *plan)) {
    result.failure = "Could not write the game files.";
    return result;
  }
  const auto journal = root + "/" + kCopyJournalDir;
  std::remove(CopyPlanPath(root).c_str());
  ::rmdir(journal.c_str());
  result.ok = true;
  result.install_bytes = plan->total;
  if (options_.before_finish && !options_.before_finish(id, root)) {
    Log("import copy complete id=" + entry->id + "; entry left copying");
    return result;
  }
  const auto finished = store_.FinishCopy(entry->id);
  // A failed write is finished by the reconcile at the next start.
  Log("import copy complete id=" + entry->id + (finished.ok() ? "" : " (imports.json not saved: " + finished.message() + ")"));
  Changed();
  return result;
}

LocalRelocationResult LocalCopyRunner::Relocate(const LocalRelocation& relocation) {
  if (options_.relocate) return options_.relocate(relocation);
  LocalRelocationResult result;
  if (relocation.move_files) {
    result.message = relocation.move_files();
    if (!result.message.empty()) result.outcome = LocalRelocationResult::Outcome::kFailed;
  }
  return result;
}

std::string LocalCopyRunner::Move(installs::InstallService& service, const ImportEntry& entry, const std::string& source,
                                  std::uint32_t runtime_number) {
  const auto game = GameId::Local(entry.id);
  if (entry.state == ImportState::kCopied) return "The game is already in Playden.";
  // copying without a copy job: a move a restart (or a busy title) interrupted.
  const bool resuming = entry.state == ImportState::kCopying;
  if (!resuming && entry.state != ImportState::kInPlace) return "The game is already being copied.";
  if (entry.volume != ImportStore::kDropFolderVolume) return "Only games in the drop folder can be moved. Copy it instead.";
  auto snapshot = service.Snapshot();
  if (std::any_of(snapshot.jobs.begin(), snapshot.jobs.end(), [&](const installs::InstallJobView& job) { return job.id == game; }))
    return "The game is already being copied.";
  const bool listed = std::any_of(snapshot.installed.begin(), snapshot.installed.end(),
                                  [&](const installs::InstalledGame& installed) { return installed.id == game; });
  if (listed && !resuming) return "The game is already in Playden.";
  const auto volume = std::find_if(snapshot.volumes.begin(), snapshot.volumes.end(), [](const installs::InstallVolume& known) {
    return known.id == installs::InstallService::kPrimaryVolumeId;
  });
  if (volume == snapshot.volumes.end() || !volume->available) return "The console storage is unavailable.";
  const auto root = volume->games_root + "/" + ToKey(game);
  const auto back_in_place = [&](const std::string& failure, const std::string& why) {
    store_.CancelCopy(entry.id, failure);
    Log("import move stopped id=" + entry.id + ": " + why);
    Changed();
    return why;
  };

  CopyPlan plan;
  if (!resuming) {
    if (!Missing(root)) {
      Log("import move refused id=" + entry.id + ": " + root + " is in the way");
      return "A folder in Playden's games folder is in the way of the move.";
    }
    // The files are listed first: a link refuses the move as it would a copy, and the list goes
    // into the moved folder for its uninstall.
    auto build = BuildCopyPlan(source);
    if (!build.ok) {
      Log("import move plan failed id=" + entry.id + " errno=" + std::to_string(build.error) + ": " + build.failure);
      return build.failure;
    }
    plan = std::move(build.plan);
    if (!PlanHasFile(plan, entry.program)) return "The program is not in the game's folder.";
    MakeFolder(volume->games_root);
    // copying from here on: a restart finishes the move from what the folders say.
    const auto queued = store_.QueueCopy(entry.id);
    const auto started = queued.ok() ? store_.StartCopy(entry.id) : queued;
    if (!started.ok()) {
      if (queued.ok()) store_.CancelCopy(entry.id);
      return "Playden's list of imported games could not be saved.";
    }
    Changed();
  } else {
    const bool source_there = IsFolder(source);
    const bool root_there = IsFolder(root);
    if (!root_there && source_there) return back_in_place({}, kMoveStoppedFailure);  // the rename never ran
    if (!root_there) return back_in_place("The game's folder is missing.", "The game's folder is missing.");
    if (source_there) return back_in_place({}, "A folder in Playden's games folder is in the way of the move.");
    // Renamed already: the files are listed where they are now.
    auto build = BuildCopyPlan(root);
    if (!build.ok) {
      Log("import move plan failed id=" + entry.id + " errno=" + std::to_string(build.error) + ": " + build.failure);
      return build.failure;
    }
    plan = std::move(build.plan);
    plan.files.erase(std::remove_if(plan.files.begin(), plan.files.end(), [](const CopyFile& file) { return PlaydenFile(file.path); }),
                     plan.files.end());
    plan.total = 0;
    for (const auto& file : plan.files) plan.total += file.bytes;
  }
  plan.source = source;
  plan.program = entry.program;
  plan.runtime_number = runtime_number;

  // The rename, under the title's publication lease once it is stopped (RuntimeHost::Relocate).
  // Both parents are synced before the title's record names the new folder.
  LocalRelocation relocation;
  relocation.game = game;
  relocation.runtime_number = runtime_number;
  relocation.root = root;
  relocation.volume_id = volume->id;
  relocation.move_files = [this, source, root]() -> std::string {
    if (Missing(source) && IsFolder(root)) return {};  // done before a restart
    int error = 0;
    const auto rename = [&] {
      if (std::rename(source.c_str(), root.c_str()) == 0) return true;
      error = errno;
      return false;
    };
    const bool renamed = options_.as_root ? options_.as_root(rename) : rename();
    if (!renamed) {
      Log("import move rename failed from=" + source + " to=" + root + " errno=" + std::to_string(error));
      return "The game could not be moved into Playden (error " + std::to_string(error) + ").";
    }
    if (!SyncFolder(Parent(source)) || !SyncFolder(Parent(root))) {
      Log("import move sync failed from=" + source + " to=" + root + " errno=" + std::to_string(errno));
      return "The move could not be saved on the storage. Move it again.";
    }
    Log("import move renamed from=" + source + " to=" + root);
    return {};
  };
  const auto relocated = Relocate(relocation);
  if (relocated.outcome != LocalRelocationResult::Outcome::kDone) {
    const auto message = relocated.message.empty() ? std::string("The game could not be moved into Playden.") : relocated.message;
    // Not renamed: it plays from the drop folder as before. Renamed but not recorded: copying, so
    // Move again (or the next start) finishes it.
    if (IsFolder(source) && Missing(root)) return back_in_place({}, message);
    Log("import move interrupted id=" + entry.id + ": " + message);
    return message;
  }

  // Managed from here: the file list, then the record, then the install service lists it.
  if (!SaveFileList(plan, root).empty() || !WriteRecord(root, game, plan)) {
    Log("import move markers failed id=" + entry.id);
    return "Could not write the game files. Move it again.";
  }
  if (!service.Adopt(game, volume->id, plan.total)) {
    Log("import move not listed id=" + entry.id);
    return "Playden could not list the moved game. Move it again.";
  }
  if (options_.before_finish && !options_.before_finish(game, root)) {
    Log("import move complete id=" + entry.id + "; entry left copying");
    return {};
  }
  const auto finished = store_.FinishCopy(entry.id);
  Log("import move complete id=" + entry.id + " files=" + std::to_string(plan.files.size()) +
      (finished.ok() ? "" : " (imports.json not saved: " + finished.message() + ")"));
  Changed();
  return {};
}

bool LocalCopyRunner::Remove(const std::string& root) {
  const auto plan = LoadCopyPlan(root);
  RemoveParts(root, plan ? &*plan : nullptr);
  const bool removed = steam::RemoveManagedInstall(root);
  const auto game = ParseKey(Basename(root));
  if (!game || game->store != Store::kLocal || !store_.loaded()) return removed;
  const auto entry = store_.Find(game->value);
  if (!entry) return removed;
  if (entry->state == ImportState::kCopyQueued || entry->state == ImportState::kCopying) {
    store_.CancelCopy(entry->id);
    Log("import copy cancelled id=" + entry->id + (removed ? "" : " (folder not removed)"));
    Changed();
  } else if (entry->state == ImportState::kCopied) {
    // Uninstalled: the game leaves the library (its bottle stays, like any kept data). The service
    // has already forgotten the install, so a folder that didn't go is left as it is.
    store_.Uninstalled(entry->id);
    Log("import copy uninstalled id=" + entry->id + (removed ? "" : " (folder not removed)"));
    Changed();
  }
  return removed;
}

}  // namespace playden::stores::local
