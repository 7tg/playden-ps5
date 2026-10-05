#include "steam/backend/cloud_sync.h"

#include "steam/backend/app_info.h"
#include "steam/backend/depot_manifest.h"
#include "steam/backend/gzip.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#if !PLAYDEN_PLATFORM_PS5
#include <sys/time.h>
#endif

#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <set>

namespace playden::steam::cloud {
namespace {

constexpr std::uint32_t kSessionDeadlineMs = 300'000;
constexpr const char* kBaselineFile = "cloud-baseline.txt";
constexpr const char* kClientIdFile = "cloud-client-id.txt";
constexpr const char* kBackupFolder = "backup";
constexpr std::size_t kKeptBackups = 5;

std::string Hex(const std::uint8_t* data, std::size_t size) {
  static const char digits[] = "0123456789abcdef";
  std::string text;
  text.reserve(size * 2);
  for (std::size_t index = 0; index < size; ++index) {
    text.push_back(digits[data[index] >> 4]);
    text.push_back(digits[data[index] & 15]);
  }
  return text;
}

std::optional<std::vector<std::uint8_t>> Unhex(const std::string& text) {
  if (text.size() % 2) return std::nullopt;
  std::vector<std::uint8_t> bytes;
  for (std::size_t index = 0; index < text.size(); index += 2) {
    unsigned value = 0;
    if (std::sscanf(text.c_str() + index, "%2x", &value) != 1) return std::nullopt;
    bytes.push_back(static_cast<std::uint8_t>(value));
  }
  return bytes;
}

std::vector<std::uint8_t> Sha1(const std::vector<std::uint8_t>& data) {
  std::vector<std::uint8_t> digest(20);
  mbedtls_sha1(data.data(), data.size(), digest.data());
  return digest;
}

bool IsDirectory(const std::string& path) {
  struct stat info {};
  return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool MakeDirectories(const std::string& path) {
  for (std::size_t slash = path.find('/', 1); slash != std::string::npos; slash = path.find('/', slash + 1)) {
    const auto prefix = path.substr(0, slash);
    if (!IsDirectory(prefix) && mkdir(prefix.c_str(), 0777) != 0 && !IsDirectory(prefix)) return false;
  }
  return IsDirectory(path) || mkdir(path.c_str(), 0777) == 0 || IsDirectory(path);
}

std::string Parent(const std::string& path) {
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos || slash == 0 ? std::string() : path.substr(0, slash);
}

// Worker threads on the console have small stacks: file buffers live on the heap.
constexpr std::size_t kReadChunk = 64 * 1024;

std::optional<std::vector<std::uint8_t>> ReadFile(const std::string& path, std::uint64_t limit) {
  struct stat info {};
  if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || static_cast<std::uint64_t>(info.st_size) > limit)
    return std::nullopt;
  FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return std::nullopt;
  std::vector<std::uint8_t> data;
  data.reserve(static_cast<std::size_t>(info.st_size));
  std::vector<std::uint8_t> buffer(kReadChunk);
  std::size_t read = 0;
  while ((read = std::fread(buffer.data(), 1, buffer.size(), file)) > 0) {
    if (data.size() + read > limit) {
      std::fclose(file);
      return std::nullopt;
    }
    data.insert(data.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(read));
  }
  const bool failed = std::ferror(file) != 0;
  std::fclose(file);
  if (failed) return std::nullopt;
  return data;
}

bool WriteFileAtomic(const std::string& path, const std::vector<std::uint8_t>& data) {
  if (!MakeDirectories(Parent(path))) return false;
  const auto temporary = path + ".playden-partial";
  FILE* file = std::fopen(temporary.c_str(), "wb");
  if (!file) return false;
  const bool written = data.empty() || std::fwrite(data.data(), 1, data.size(), file) == data.size();
  if (std::fclose(file) != 0 || !written) {
    std::remove(temporary.c_str());
    return false;
  }
  if (std::rename(temporary.c_str(), path.c_str()) != 0) {
    std::remove(temporary.c_str());
    return false;
  }
  return true;
}

void SetModified(const std::string& path, std::uint64_t unix_seconds) {
#if PLAYDEN_PLATFORM_PS5
  (void)path;
  (void)unix_seconds;
#else
  if (unix_seconds == 0) return;
  struct timeval times[2] = {{static_cast<time_t>(unix_seconds), 0}, {static_cast<time_t>(unix_seconds), 0}};
  utimes(path.c_str(), times);
#endif
}

std::string RootFolder(const GameSaveLocation& location, SaveRoot root) {
  return root == SaveRoot::kGame ? location.game_root : location.prefix_root;
}

std::string Absolute(const GameSaveLocation& location, const LocalSavePath& local) {
  return RootFolder(location, local.root) + "/" + local.path;
}

// Size and SHA-1 of a file, hashed in chunks so large saves are never held in memory whole.
std::optional<FileFact> Fingerprint(const std::string& path) {
  struct stat info {};
  if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) return std::nullopt;
  if (static_cast<std::uint64_t>(info.st_size) > kMaxRemoteFileBytes) return std::nullopt;
  FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return std::nullopt;
  mbedtls_sha1_context context;
  mbedtls_sha1_init(&context);
  mbedtls_sha1_starts(&context);
  std::vector<std::uint8_t> buffer(kReadChunk);
  std::uint64_t size = 0;
  std::size_t read = 0;
  while ((read = std::fread(buffer.data(), 1, buffer.size(), file)) > 0) {
    mbedtls_sha1_update(&context, buffer.data(), read);
    size += read;
  }
  const bool failed = std::ferror(file) != 0 || size > kMaxRemoteFileBytes;
  std::fclose(file);
  FileFact fact;
  fact.sha1.resize(20);
  mbedtls_sha1_finish(&context, fact.sha1.data());
  mbedtls_sha1_free(&context);
  if (failed) return std::nullopt;
  fact.present = true;
  fact.size = size;
  fact.time = static_cast<std::uint64_t>(info.st_mtime);
  return fact;
}

// Files below a rule's folder (relative to the save root). Playden's own staging names are skipped.
void Enumerate(const std::string& root, const std::string& relative, bool recursive, int depth,
               std::vector<std::string>* out) {
  if (depth > 16 || out->size() >= kMaxLocalSaveFiles) return;
  const auto folder = relative.empty() ? root : root + "/" + relative;
  DIR* directory = opendir(folder.c_str());
  if (!directory) return;
  while (dirent* entry = readdir(directory)) {
    const std::string name = entry->d_name;
    if (name == "." || name == ".." || name.rfind(".playden", 0) == 0 ||
        name.find(".playden-partial") != std::string::npos)
      continue;
    const auto child = relative.empty() ? name : relative + "/" + name;
    struct stat info {};
    if (stat((root + "/" + child).c_str(), &info) != 0) continue;
    if (S_ISDIR(info.st_mode)) {
      if (recursive) Enumerate(root, child, recursive, depth + 1, out);
    } else if (S_ISREG(info.st_mode)) {
      out->push_back(child);
      if (out->size() >= kMaxLocalSaveFiles) break;
    }
  }
  closedir(directory);
}

struct Baseline {
  bool loaded = false;
  std::string account_key;
  std::uint64_t change_number = 0;
  std::map<std::string, FileFact> files;  // by lowercased remote name
};

std::string Lower(std::string text) {
  for (auto& value : text) value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  return text;
}

Baseline LoadBaseline(const std::string& state_root) {
  Baseline baseline;
  FILE* file = std::fopen((state_root + "/" + kBaselineFile).c_str(), "r");
  if (!file) return baseline;
  std::vector<char> buffer(8192);
  char* line = buffer.data();
  bool header = std::fgets(line, static_cast<int>(buffer.size()), file) && std::strncmp(line, "version=1", 9) == 0;
  while (header && std::fgets(line, static_cast<int>(buffer.size()), file)) {
    std::string text(line, std::strcspn(line, "\r\n"));
    if (text.rfind("account=", 0) == 0) baseline.account_key = text.substr(8);
    else if (text.rfind("change=", 0) == 0) baseline.change_number = std::strtoull(text.c_str() + 7, nullptr, 10);
    else {
      const auto first = text.find('\t');
      const auto second = first == std::string::npos ? std::string::npos : text.find('\t', first + 1);
      if (second == std::string::npos) continue;
      FileFact fact;
      fact.present = true;
      const auto sha = Unhex(text.substr(0, first));
      if (!sha || sha->size() != 20) continue;
      fact.sha1 = *sha;
      fact.size = std::strtoull(text.c_str() + first + 1, nullptr, 10);
      baseline.files[Lower(text.substr(second + 1))] = std::move(fact);
    }
  }
  std::fclose(file);
  baseline.loaded = header && !baseline.account_key.empty();
  return baseline;
}

bool SaveBaseline(const std::string& state_root, const std::string& account_key, std::uint64_t change_number,
                  const std::vector<std::pair<std::string, FileFact>>& files) {
  std::string text = "version=1\naccount=" + account_key + "\nchange=" + std::to_string(change_number) + "\n";
  for (const auto& [name, fact] : files)
    text += Hex(fact.sha1.data(), fact.sha1.size()) + "\t" + std::to_string(fact.size) + "\t" + name + "\n";
  return WriteFileAtomic(state_root + "/" + kBaselineFile, std::vector<std::uint8_t>(text.begin(), text.end()));
}

std::uint64_t ClientId(const std::string& state_root) {
  const auto path = state_root + "/" + kClientIdFile;
  if (const auto data = ReadFile(path, 64)) {
    const std::string text(data->begin(), data->end());
    if (const auto value = std::strtoull(text.c_str(), nullptr, 10); value != 0) return value;
  }
  std::random_device device;
  std::uint64_t value = 0;
  while (value == 0) value = (static_cast<std::uint64_t>(device()) << 32) ^ device();
  const auto text = std::to_string(value) + "\n";
  WriteFileAtomic(path, std::vector<std::uint8_t>(text.begin(), text.end()));
  return value;
}

bool SameContent(const FileFact& left, const FileFact& right) {
  return left.present && right.present && left.size == right.size && left.sha1 == right.sha1;
}

// Signed transfer URLs from Steam: a plain host name (optionally with a port) and an absolute path.
std::optional<std::string> TransferUrl(bool https, const std::string& host, const std::string& path) {
  if (host.empty() || host.size() > 253 || path.empty() || path.front() != '/') return std::nullopt;
  for (const char value : host)
    if (!(std::isalnum(static_cast<unsigned char>(value)) || value == '.' || value == '-' || value == ':')) return std::nullopt;
  for (const char value : path)
    if (static_cast<unsigned char>(value) <= 0x20 || value == '#') return std::nullopt;
  return std::string(https ? "https://" : "http://") + host + path;
}

void PruneBackups(const std::string& state_root) {
  std::vector<std::string> names;
  const auto folder = state_root + "/" + kBackupFolder;
  if (DIR* directory = opendir(folder.c_str())) {
    while (dirent* entry = readdir(directory)) {
      const std::string name = entry->d_name;
      if (name != "." && name != "..") names.push_back(name);
    }
    closedir(directory);
  }
  std::sort(names.begin(), names.end());
  while (names.size() > kKeptBackups) {
    // Backups are only ever Playden's copies; remove the oldest folder tree.
    std::vector<std::string> files;
    Enumerate(folder + "/" + names.front(), "", true, 0, &files);
    for (const auto& file : files) std::remove((folder + "/" + names.front() + "/" + file).c_str());
    std::vector<std::string> stack = {folder + "/" + names.front()};
    // Remove now-empty folders deepest first.
    std::vector<std::string> directories;
    while (!stack.empty()) {
      const auto current = stack.back();
      stack.pop_back();
      directories.push_back(current);
      if (DIR* directory = opendir(current.c_str())) {
        while (dirent* entry = readdir(directory)) {
          const std::string name = entry->d_name;
          if (name != "." && name != ".." && IsDirectory(current + "/" + name)) stack.push_back(current + "/" + name);
        }
        closedir(directory);
      }
    }
    for (auto it = directories.rbegin(); it != directories.rend(); ++it) rmdir(it->c_str());
    names.erase(names.begin());
  }
}

std::string Plural(std::size_t count, const char* one, const char* many) {
  return std::to_string(count) + " " + (count == 1 ? one : many);
}

}  // namespace

std::string AccountKey(std::uint64_t steam_id) {
  const std::string input = "steam:" + std::to_string(steam_id);
  std::uint8_t digest[32] = {};
  mbedtls_sha256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest, 0);
  return Hex(digest, sizeof(digest));
}

CloudSync::CloudSync(CmClient& cm, HttpClient& http) {
  hooks_.session = [&cm](const StoredSession& session, std::uint32_t deadline_ms, const std::atomic<bool>& cancel,
                         const std::function<CmError(const CmClient::ServiceCall&, std::uint64_t)>& work) {
    return cm.WithServiceSession(session, deadline_ms, cancel, work);
  };
  hooks_.app_info = [&cm](const StoredSession& session, std::uint32_t app_id, const std::atomic<bool>& cancel,
                          CmError* error) -> std::optional<CloudSaveInfo> {
    const auto buffers = cm.FetchAppInfo(session, {app_id}, cancel);
    *error = buffers.error;
    if (buffers.error != CmError::kNone) return std::nullopt;
    const auto found = buffers.buffers.find(app_id);
    if (found == buffers.buffers.end()) return std::nullopt;
    if (const auto info = ParseAppInfo(app_id, found->second)) return info->cloud;
    return std::nullopt;
  };
  hooks_.http = [&http](const HttpRequest& request, const std::atomic<bool>& cancel) { return http.Perform(request, cancel); };
}

CloudSync::CloudSync(CloudHooks hooks) : hooks_(std::move(hooks)) {}

#define PLAYDEN_CLOUD_TRACE(step) \
  do { if (hooks_.trace) hooks_.trace(step); } while (false)

CloudPlan CloudSync::Check(const StoredSession& session, const GameSaveLocation& location, const std::atomic<bool>& cancel) {
  CloudPlan plan;
  const auto fail = [&](PlanStatus status, std::string message) {
    plan.status = status;
    plan.message = std::move(message);
    plan.decisions.clear();
    return plan;
  };
  PLAYDEN_CLOUD_TRACE("app-info");
  CmError info_error = CmError::kNone;
  const auto info = hooks_.app_info(session, location.app_id, cancel, &info_error);
  if (info_error != CmError::kNone) return fail(PlanStatus::kFailed, "Steam is not reachable. Try again.");
  if (!info || !info->declared) return fail(PlanStatus::kUnsupported, "This game does not use Steam Cloud.");

  PLAYDEN_CLOUD_TRACE("list");
  std::optional<RemoteFileList> remote;
  std::uint64_t steam_id = 0;
  const auto listed = hooks_.session(session, kSessionDeadlineMs, cancel,
                                     [&](const CmClient::ServiceCall& call, std::uint64_t logged_on) {
    steam_id = logged_on;
    std::vector<std::uint8_t> reply;
    if (const auto error = call(kGetAppFileChangelist, EncodeChangelistRequest(location.app_id, 0), &reply, nullptr);
        error != CmError::kNone)
      return error;
    remote = DecodeChangelistResponse(reply);
    return remote ? CmError::kNone : CmError::kProtocol;
  });
  if (listed == CmError::kRejected) return fail(PlanStatus::kFailed, "Steam refused the Cloud save list.");
  if (listed != CmError::kNone || !remote) return fail(PlanStatus::kFailed, "Steam did not return the Cloud save list. Try again.");

  PLAYDEN_CLOUD_TRACE("mapping");
  const auto mapping = BuildSaveMapping(*info, location.app_id, steam_id);
  if (!mapping.usable()) return fail(PlanStatus::kUnsupported, mapping.unresolved.empty() ? "This game has no Cloud save locations."
                                                                                     : mapping.unresolved.front());
  const SavePaths paths(mapping);
  plan.change_number = remote->change_number;
  plan.account_key = AccountKey(steam_id);
  auto baseline = LoadBaseline(location.state_root);
  const bool trusted = baseline.loaded && baseline.account_key == plan.account_key;
  if (!trusted) baseline.files.clear();
  plan.requires_account_confirmation = !trusted;

  // Every slot either side knows about, keyed by local identity.
  std::map<std::string, Decision> slots;
  std::set<std::string> unmapped_remote;
  for (const auto& file : remote->files) {
    std::string error;
    const auto local = paths.LocalFor(file.name, &error);
    if (!local) {
      if (file.state == RemoteState::kPresent) unmapped_remote.insert(file.name);
      continue;
    }
    auto& slot = slots[local->key()];
    slot.remote_name = file.name;
    slot.local = *local;
    if (file.state == RemoteState::kPresent) {
      slot.remote_fact = {true, file.sha1, file.raw_size, file.time_stamp};
      if (file.reupload_requested) slot.action = Action::kUpload;  // provisional; settled below
    } else if (file.state == RemoteState::kForgotten) {
      slot.action = Action::kUnavailable;
    }
  }
  PLAYDEN_CLOUD_TRACE("scan");
  // Local files under every rule's folder, plus the known names (the console may not list folders).
  std::vector<LocalSavePath> candidates;
  for (const auto& rule : paths.rules()) {
    std::vector<std::string> found;
    const auto root = RootFolder(location, rule.root);
    if (root.empty()) continue;
    Enumerate(root, rule.directory, rule.recursive, 0, &found);
    for (const auto& path : found) candidates.push_back({rule.root, path});
  }
  for (const auto& [key, slot] : slots) candidates.push_back(slot.local);
  PLAYDEN_CLOUD_TRACE("fingerprint");
  std::set<std::string> seen;
  std::uint64_t scanned_bytes = 0;
  for (const auto& candidate : candidates) {
    if (!seen.insert(candidate.key()).second) continue;
    const auto name = paths.RemoteFor(candidate);
    if (!name) continue;
    const auto fact = Fingerprint(Absolute(location, candidate));
    if (!fact) continue;
    scanned_bytes += fact->size;
    if (seen.size() > kMaxLocalSaveFiles || scanned_bytes > kMaxLocalSaveBytes)
      return fail(PlanStatus::kFailed, "This game's local saves are too large to sync.");
    auto& slot = slots[candidate.key()];
    if (slot.remote_name.empty()) slot.remote_name = *name;
    slot.local = candidate;
    slot.local_fact = *fact;
  }

  PLAYDEN_CLOUD_TRACE("plan");
  for (auto& [key, slot] : slots) {
    const bool reupload = slot.action == Action::kUpload;
    if (slot.action == Action::kUnavailable) {
      plan.decisions.push_back(slot);
      continue;
    }
    const auto& local = slot.local_fact;
    const auto& cloud = slot.remote_fact;
    const auto base_it = baseline.files.find(Lower(slot.remote_name));
    const FileFact* base = base_it == baseline.files.end() ? nullptr : &base_it->second;
    if (!local.present && !cloud.present) continue;
    if (SameContent(local, cloud)) slot.action = reupload ? Action::kUpload : Action::kUnchanged;
    else if (cloud.present && !local.present)
      // Missing here but unchanged in the Cloud since the last sync: deleted on purpose? Ask.
      slot.action = base && SameContent(*base, cloud) ? Action::kConflict : Action::kDownload;
    else if (local.present && !cloud.present)
      slot.action = base && SameContent(*base, local) ? Action::kConflict : Action::kUpload;
    else if (base && SameContent(*base, local)) slot.action = Action::kDownload;
    else if (base && SameContent(*base, cloud)) slot.action = Action::kUpload;
    else slot.action = Action::kConflict;
    plan.decisions.push_back(slot);
  }
  for (const auto& decision : plan.decisions) {
    switch (decision.action) {
      case Action::kDownload: ++plan.downloads; break;
      case Action::kUpload: ++plan.uploads; break;
      case Action::kConflict: ++plan.conflicts; break;
      default: break;
    }
    if (decision.local_fact.present) {
      ++plan.local_files;
      plan.local_bytes += decision.local_fact.size;
      plan.local_latest = std::max(plan.local_latest, decision.local_fact.time);
    }
    if (decision.remote_fact.present) {
      ++plan.remote_files;
      plan.remote_bytes += decision.remote_fact.size;
      plan.remote_latest = std::max(plan.remote_latest, decision.remote_fact.time);
    }
  }
  if (plan.conflicts > 0) {
    plan.status = PlanStatus::kConflict;
    plan.message = "Saves here and in Steam Cloud both changed. Choose which to keep.";
  } else if (plan.uploads > 0 && plan.requires_account_confirmation) {
    plan.status = PlanStatus::kNeedsAccount;
    plan.message = "Confirm this Steam account before Playden changes its Cloud saves.";
  } else if (plan.downloads > 0 || plan.uploads > 0) {
    plan.status = PlanStatus::kReady;
    plan.message = plan.downloads > 0 && plan.uploads > 0
                       ? Plural(plan.downloads, "save to download", "saves to download") + ", " +
                             Plural(plan.uploads, "to upload", "to upload")
                   : plan.downloads > 0 ? Plural(plan.downloads, "save to download", "saves to download")
                                        : Plural(plan.uploads, "save to upload", "saves to upload");
  } else {
    plan.status = PlanStatus::kUpToDate;
    plan.message = plan.remote_files == 0 && plan.local_files == 0 ? "No Cloud saves yet" : "Saves are up to date";
  }
  if (!unmapped_remote.empty() && plan.status != PlanStatus::kConflict)
    plan.message += " · " + Plural(unmapped_remote.size(), "Cloud file is", "Cloud files are") + " outside known save folders";
  return plan;
}

CloudOutcome CloudSync::Apply(const StoredSession& session, const GameSaveLocation& location, const CloudPlan& plan,
                              Resolution resolution, bool confirm_account, const std::atomic<bool>& cancel) {
  CloudOutcome outcome;
  const auto fail = [&](std::string message) {
    outcome.ok = false;
    outcome.message = std::move(message);
    return outcome;
  };
  if (plan.status == PlanStatus::kFailed || plan.status == PlanStatus::kUnsupported) return fail(plan.message);

  // Settle conflicts with the player's choice.
  std::vector<Decision> downloads, uploads, local_deletes, remote_deletes;
  for (const auto& decision : plan.decisions) {
    auto action = decision.action;
    if (action == Action::kConflict) {
      if (resolution == Resolution::kAutomatic) return fail("Choose which saves to keep first.");
      if (resolution == Resolution::kUseLocal)
        action = decision.local_fact.present ? Action::kUpload : Action::kDeleteRemote;
      else
        action = decision.remote_fact.present ? Action::kDownload : Action::kDeleteLocal;
    }
    switch (action) {
      case Action::kDownload: downloads.push_back(decision); break;
      case Action::kUpload: uploads.push_back(decision); break;
      case Action::kDeleteLocal: local_deletes.push_back(decision); break;
      case Action::kDeleteRemote: remote_deletes.push_back(decision); break;
      default: break;
    }
  }
  const bool remote_changes = !uploads.empty() || !remote_deletes.empty();
  if (remote_changes && plan.requires_account_confirmation && !confirm_account)
    return fail("Confirm this Steam account before Playden changes its Cloud saves.");

  const auto backup_root = location.state_root + "/" + kBackupFolder + "/" +
                           std::to_string(hooks_.now ? hooks_.now() : std::chrono::duration_cast<std::chrono::seconds>(
                                              std::chrono::system_clock::now().time_since_epoch()).count());
  bool backed_up = false;
  const auto backup = [&](const Decision& decision) {
    const auto source = Absolute(location, decision.local);
    const auto data = ReadFile(source, kMaxRemoteFileBytes);
    if (!data) return true;  // nothing there to keep
    backed_up = true;
    return WriteFileAtomic(backup_root + "/" + (decision.local.root == SaveRoot::kGame ? "game/" : "prefix/") +
                               decision.local.path,
                           *data);
  };

  std::string failure;
  std::optional<RemoteFileList> final_list;
  const auto result = hooks_.session(session, kSessionDeadlineMs, cancel,
                                     [&](const CmClient::ServiceCall& call, std::uint64_t steam_id) {
    if (AccountKey(steam_id) != plan.account_key) {
      failure = "The signed-in Steam account changed. Check again.";
      return CmError::kRejected;
    }
    std::vector<std::uint8_t> reply;
    const auto list = [&]() -> std::optional<RemoteFileList> {
      reply.clear();
      if (call(kGetAppFileChangelist, EncodeChangelistRequest(location.app_id, 0), &reply, nullptr) != CmError::kNone)
        return std::nullopt;
      return DecodeChangelistResponse(reply);
    };
    const auto current = list();
    if (!current) {
      failure = "Steam did not return the Cloud save list. Try again.";
      return CmError::kProtocol;
    }
    if (current->change_number != plan.change_number) {
      failure = "Steam Cloud changed since the check. Check again.";
      return CmError::kRejected;
    }

    for (const auto& decision : downloads) {
      PLAYDEN_CLOUD_TRACE("download");
      reply.clear();
      if (call(kClientFileDownload, EncodeFileDownloadRequest(location.app_id, decision.remote_name), &reply, nullptr) !=
          CmError::kNone) {
        failure = "Steam refused a Cloud save download.";
        return CmError::kRejected;
      }
      const auto ticket = DecodeFileDownloadResponse(reply);
      if (!ticket || ticket->encrypted || ticket->explicit_delete || ticket->sha1 != decision.remote_fact.sha1 ||
          ticket->raw_file_size != decision.remote_fact.size || ticket->file_size > kMaxHttpResponseBytes) {
        failure = ticket && ticket->file_size > kMaxHttpResponseBytes ? "A Cloud save is too large to download yet."
                                                                       : "Steam described a Cloud save Playden cannot use.";
        return CmError::kProtocol;
      }
      const auto url = TransferUrl(ticket->use_https, ticket->host, ticket->path);
      if (!url) {
        failure = "Steam described a Cloud save Playden cannot use.";
        return CmError::kProtocol;
      }
      HttpRequest request;
      request.url = *url;
      request.headers = ticket->headers;
      request.timeout_ms = kMaxHttpTimeoutMs;
      request.max_response_bytes = static_cast<std::size_t>(std::max<std::uint64_t>(ticket->file_size, 1));
      const auto response = hooks_.http(request, cancel);
      if (!response.ok() || response.status_code != 200) {
        failure = "A Cloud save could not be downloaded. Try again.";
        return CmError::kNetwork;
      }
      std::vector<std::uint8_t> data = response.body;
      if (ticket->file_size != ticket->raw_file_size) {
        std::vector<std::uint8_t> unpacked;
        if (!UnzipFirstEntry(response.body, InflateRaw, &unpacked)) {
          failure = "A downloaded Cloud save was damaged.";
          return CmError::kProtocol;
        }
        data = std::move(unpacked);
      }
      if (data.size() != decision.remote_fact.size || Sha1(data) != decision.remote_fact.sha1) {
        failure = "A downloaded Cloud save was damaged.";
        return CmError::kProtocol;
      }
      const auto target = Absolute(location, decision.local);
      if (!backup(decision) || !WriteFileAtomic(target, data)) {
        failure = "A Cloud save could not be written on this device.";
        return CmError::kProtocol;
      }
      SetModified(target, decision.remote_fact.time);
      ++outcome.downloaded;
    }
    for (const auto& decision : local_deletes) {
      if (!backup(decision)) {
        failure = "A local save could not be backed up.";
        return CmError::kProtocol;
      }
      std::remove(Absolute(location, decision.local).c_str());
      ++outcome.removed_local;
    }

    if (!uploads.empty() || !remote_deletes.empty()) {
      // The files must still be what the check saw.
      std::vector<std::vector<std::uint8_t>> contents;
      for (const auto& decision : uploads) {
        auto data = ReadFile(Absolute(location, decision.local), kMaxRemoteFileBytes);
        if (!data || data->size() != decision.local_fact.size || Sha1(*data) != decision.local_fact.sha1) {
          failure = "A local save changed since the check. Check again.";
          return CmError::kRejected;
        }
        contents.push_back(std::move(*data));
      }
      PLAYDEN_CLOUD_TRACE("upload-batch");
      std::vector<std::string> upload_names, delete_names;
      for (const auto& decision : uploads) upload_names.push_back(decision.remote_name);
      for (const auto& decision : remote_deletes) delete_names.push_back(decision.remote_name);
      reply.clear();
      if (call(kBeginAppUploadBatch, EncodeBeginBatchRequest(location.app_id, kMachineName, upload_names, delete_names,
                                                             ClientId(location.state_root), 0),
               &reply, nullptr) != CmError::kNone) {
        failure = "Steam refused the Cloud save upload.";
        return CmError::kRejected;
      }
      const auto batch = DecodeBeginBatchResponse(reply);
      if (!batch) {
        failure = "Steam refused the Cloud save upload.";
        return CmError::kProtocol;
      }
      const auto abort_batch = [&](const std::string& why, CmError error) {
        std::vector<std::uint8_t> ignored;
        call(kCompleteAppUploadBatchBlocking, EncodeCompleteBatchRequest(location.app_id, batch->batch_id, false), &ignored, nullptr);
        failure = why;
        return error;
      };
      if (batch->app_change_number != 0 && batch->app_change_number != plan.change_number + 1)
        return abort_batch("Steam Cloud changed since the check. Check again.", CmError::kRejected);
      for (std::size_t index = 0; index < uploads.size(); ++index) {
        const auto& decision = uploads[index];
        const auto& data = contents[index];
        const auto time_stamp = decision.local_fact.time;
        reply.clear();
        int eresult = 0;
        if (call(kClientBeginFileUpload,
                 EncodeBeginFileUploadRequest(location.app_id, data.size(), decision.local_fact.sha1, time_stamp,
                                              decision.remote_name, batch->batch_id),
                 &reply, &eresult) != CmError::kNone)
          return abort_batch("Steam refused a Cloud save upload (" + std::to_string(eresult) + ").", CmError::kRejected);
        const auto upload = DecodeBeginFileUploadResponse(reply);
        if (!upload || upload->encrypt_file) return abort_batch("Steam asked for an upload Playden cannot make.", CmError::kProtocol);
        bool transferred = true;
        for (const auto& block : upload->blocks) {
          const auto url = TransferUrl(block.use_https, block.host, block.path);
          if (!url || (block.http_method != 3 && block.http_method != 4) ||
              (!block.explicit_body && block.offset + block.length > data.size())) {
            transferred = false;
            break;
          }
          HttpRequest request;
          request.method = block.http_method == 4 ? HttpMethod::kPut : HttpMethod::kPost;
          request.url = *url;
          request.headers = block.headers;
          request.timeout_ms = kMaxHttpTimeoutMs;
          request.body = block.explicit_body ? *block.explicit_body
                                             : std::vector<std::uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(block.offset),
                                                                         data.begin() + static_cast<std::ptrdiff_t>(block.offset + block.length));
          const auto response = hooks_.http(request, cancel);
          if (!response.ok() || response.status_code < 200 || response.status_code >= 300) {
            transferred = false;
            break;
          }
        }
        reply.clear();
        const auto committed = call(kClientCommitFileUpload,
                                    EncodeCommitFileUploadRequest(transferred, location.app_id, decision.local_fact.sha1,
                                                                  decision.remote_name),
                                    &reply, nullptr);
        const auto done = committed == CmError::kNone ? DecodeCommitFileUploadResponse(reply) : std::nullopt;
        if (!transferred || !done || !*done) return abort_batch("A Cloud save could not be uploaded. Try again.", CmError::kNetwork);
        ++outcome.uploaded;
      }
      for (const auto& decision : remote_deletes) {
        reply.clear();
        if (call(kClientDeleteFile, EncodeDeleteFileRequest(location.app_id, decision.remote_name, batch->batch_id), &reply,
                 nullptr) != CmError::kNone)
          return abort_batch("Steam refused to remove a Cloud save.", CmError::kRejected);
        ++outcome.removed_remote;
      }
      reply.clear();
      if (call(kCompleteAppUploadBatchBlocking, EncodeCompleteBatchRequest(location.app_id, batch->batch_id, true), &reply,
               nullptr) != CmError::kNone) {
        failure = "Steam did not finish the Cloud save upload.";
        return CmError::kRejected;
      }
    }
    PLAYDEN_CLOUD_TRACE("verify");
    final_list = list();
    if (!final_list) {
      failure = "Steam did not confirm the Cloud saves. Check again.";
      return CmError::kProtocol;
    }
    return CmError::kNone;
  });
  if (backed_up) PruneBackups(location.state_root);
  if (result != CmError::kNone) return fail(failure.empty() ? "Steam is not reachable. Try again." : failure);

  // The verified common state becomes the next baseline: every Cloud file that maps locally and
  // now matches its local copy.
  std::vector<std::pair<std::string, FileFact>> agreed;
  std::map<std::string, const Decision*> by_name;
  for (const auto& decision : plan.decisions) by_name[Lower(decision.remote_name)] = &decision;
  for (const auto& file : final_list->files) {
    if (file.state != RemoteState::kPresent) continue;
    const auto found = by_name.find(Lower(file.name));
    if (found == by_name.end()) continue;
    const auto local = Fingerprint(Absolute(location, found->second->local));
    const FileFact cloud{true, file.sha1, file.raw_size, file.time_stamp};
    if (local && SameContent(*local, cloud)) agreed.emplace_back(file.name, cloud);
  }
  SaveBaseline(location.state_root, plan.account_key, final_list->change_number, agreed);
  outcome.ok = true;
  const std::size_t moved = outcome.downloaded + outcome.uploaded + outcome.removed_local + outcome.removed_remote;
  outcome.message = moved == 0 ? "Saves are up to date"
                    : outcome.downloaded > 0 && outcome.uploaded > 0
                        ? Plural(outcome.downloaded, "save downloaded", "saves downloaded") + ", " +
                              Plural(outcome.uploaded, "uploaded", "uploaded")
                    : outcome.downloaded > 0 ? Plural(outcome.downloaded, "save downloaded", "saves downloaded")
                    : outcome.uploaded > 0   ? Plural(outcome.uploaded, "save uploaded", "saves uploaded")
                                             : "Saves updated";
  return outcome;
}

}  // namespace playden::steam::cloud
