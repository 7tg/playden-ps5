#include "platform/ps5/title_contract_probe.h"
#include "steam/backend/native_network.h"
#include "platform/ps5/title_registry.h"
#include "platform/ps5/title_activity.h"
#include "platform/ps5/title_database.h"
#include "platform/ps5/native_coordinator.h"
#include "platform/ps5/title_probe_contract.h"
#include "runtime/private_directories.h"
#include "runtime/settings_publication_journal.h"
extern "C" {
int playden_publication_native_probe(FILE*, const char*, const char*);
int playden_ps5_directory_api_ready(void);
int playden_ps5_probe_kernel_directory(const char*);
int sceKernelLoadStartModule(const char*, std::size_t, const void*, unsigned, void*, int*);
int sceKernelDlsym(int, const char*, void**);
int sceSysmoduleLoadModuleInternal(unsigned);
int sceSysmoduleGetModuleHandleInternal(unsigned, int*);
int sceUserServiceInitialize(void*);
int sceUserServiceGetForegroundUser(unsigned*);
}
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <fcntl.h>
#include <dirent.h>

namespace playden::ps5 {
void RunManagedFilesystemProbe(FILE* log);
static size_t receive(char* data, size_t size, size_t count, void* destination) {
  auto& body = *static_cast<std::string*>(destination);
  if (count > 1048576 || size > 1048576 || size * count > 1048576 - body.size()) return 0;
  body.append(data, size * count); return size * count;
}
static std::optional<std::string> service_post(FILE* log, const std::string& route, const std::string& request) {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  CURL* easy = curl_easy_init();
  if (!easy) { std::fprintf(log, "smp curl unavailable\n"); return std::nullopt; }
  std::string body;
  curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
  const std::string url = "http://127.0.0.1:10101" + route;
  curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
  curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(easy, CURLOPT_POSTFIELDS, request.c_str());
  curl_easy_setopt(easy, CURLOPT_PROXY, "");
  curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, 5000L);
  curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, receive);
  curl_easy_setopt(easy, CURLOPT_WRITEDATA, &body);
  const auto rc = curl_easy_perform(easy); long status = 0;
  curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
  std::fprintf(log, "smp route=%s curl=%d http=%ld body=%s\n", route.c_str(), int(rc), status, body.c_str());
  std::fflush(log);
  curl_slist_free_all(headers); curl_easy_cleanup(easy);
  // Preserve the API's error body (e.g. EBUSY) for the registry adapter.
  return rc == CURLE_OK ? std::optional<std::string>(body) : std::nullopt;
}
static bool copy_probe_file(const char* relative, unsigned mode) {
  const std::string src = std::string("/data/homebrew/FAKE77031/dev/contract-title/") + relative;
  const std::string dst = std::string("/data/homebrew/FAKE78000/") + relative;
  FILE* in = std::fopen(src.c_str(), "rb"); if (!in) return false;
  FILE* out = std::fopen(dst.c_str(), "wb");
  if (!out) { std::fclose(in); return false; }
  char buffer[16384]; size_t count; bool ok = true;
  while ((count = std::fread(buffer, 1, sizeof(buffer), in)))
    if (std::fwrite(buffer, 1, count, out) != count) { ok = false; break; }
  ok = ok && !std::ferror(in);
  std::fclose(in); if (std::fclose(out)) ok = false;
  return ok && chmod(dst.c_str(), mode) == 0;
}
static std::optional<std::string> read_probe_metadata(const char* path) {
  const int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if (fd < 0) return std::nullopt;
  struct stat info{};
  if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size < 0 || info.st_size > 65536) {
    close(fd); return std::nullopt;
  }
  std::string content; char buffer[4096];
  for (;;) {
    const auto count = read(fd, buffer, sizeof(buffer));
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 || (count > 0 && content.size() + std::size_t(count) > 65536)) {
      close(fd); return std::nullopt;
    }
    if (!count) break;
    content.append(buffer, std::size_t(count));
  }
  if (close(fd)) return std::nullopt;
  return content;
}
static bool directory_entries(const char* path, const std::string& expected) {
  DIR* directory = opendir(path);
  if (!directory) return false;
  bool found = false, ok = true;
  errno = 0;
  while (const auto* entry = readdir(directory)) {
    const std::string name = entry->d_name;
    if (name == "." || name == "..") continue;
    if (expected.empty() || name != expected || found) { ok = false; break; }
    found = true;
  }
  if (errno) ok = false;
  closedir(directory);
  return ok && (expected.empty() || found);
}
static void private_copy_probe(FILE* log, const std::string& descriptor) {
  const int directory_api = playden_ps5_directory_api_ready();
  std::fprintf(log, "private copy directory_api_ready=%d\n", directory_api); std::fflush(log);
  if (!directory_api) return;
  const char* root = "/data/playden/games/acceptance-hl2-fresh-v16";
  const char* retired = "/data/playden/games/acceptance-hl2-fresh-v16.ftp-staging";
  const auto user = geteuid();
  struct stat info{};
  // Preserve the empty FTP scaffold rather than adopting its ownership. Never
  // retire a real bottle or any directory containing files/saves.
  bool empty = user == 1 && seteuid(0) == 0;
  const int root_stat = lstat(root, &info);
  const bool root_missing = root_stat != 0 && errno == ENOENT;
  const bool root_private = root_stat == 0 && S_ISDIR(info.st_mode) && info.st_uid == user &&
      (info.st_mode & 07777) == 0700;
  const bool resume = root_missing || root_private;
  const std::string base = resume ? retired : root;
  for (const auto& suffix : {"", "/library", "/library/profiles", "/library/prefixes", "/library/input"}) {
    if (!empty) break;
    empty = lstat((base + suffix).c_str(), &info) == 0 && S_ISDIR(info.st_mode) && info.st_uid == 0 &&
        (info.st_mode & 07777) == 0700;
  }
  empty = empty && directory_entries(base.c_str(), "library");
  if (empty) {
    DIR* library = opendir((base + "/library").c_str());
    unsigned mask = 0;
    if (!library) empty = false;
    if (library) {
      errno = 0;
      while (const auto* entry = readdir(library)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        const unsigned bit = name == "profiles" ? 1 : name == "prefixes" ? 2 : name == "input" ? 4 : 0;
        if (!bit || (mask & bit)) { empty = false; break; }
        mask |= bit;
      }
      if (errno || mask != 7) empty = false;
      closedir(library);
    }
  }
  for (const char* suffix : {"/library/profiles", "/library/prefixes", "/library/input"})
    empty = empty && directory_entries((base + suffix).c_str(), "");
  const bool destination_free = lstat(retired, &info) != 0 && errno == ENOENT;
  const bool preserved = empty && (resume || (destination_free && rename(root, retired) == 0));
  const int preserve_error = preserved ? 0 : errno;
  const bool restored = seteuid(user) == 0 && geteuid() == user;
  std::fprintf(log, "private copy scaffold_empty=%d preserved=%d errno=%d uid_restored=%d euid=%d\n",
      empty, preserved, preserve_error, restored, geteuid()); std::fflush(log);
  if (!preserved || !restored) return;
  std::string error;
  const std::string target = std::string(root) + "/library/prefixes/game";
  // Fixed developer fixture: establish app-owned creation/copy separately from
  // the production descriptor-relative API gate. Never use this as a generic
  // production fallback; the directory-contract probe records that openat's
  // relative-dirfd behavior still needs a supported adapter on this firmware.
  bool made = true;
  for (const char* suffix : {"", "/library", "/library/prefixes", "/library/prefixes/game"}) {
    const std::string path = std::string(root) + suffix;
    if (mkdir(path.c_str(), 0700) != 0 && errno != EEXIST) {
      made = false; error = "cannot create " + path + ": " + strerror(errno); break;
    }
    if (!runtime::IsPrivateDirectory(path)) {
      made = false; error = "directory is not private and app-owned: " + path; break;
    }
  }
  std::fprintf(log, "private copy directories=%d error=%s\n", made, error.c_str()); std::fflush(log);
  if (!made) return;
  const std::string proof = std::string(root) + "/library/prefixes/game/bootstrap-copy-proof.json";
  const int fd = open(proof.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  bool copied = fd >= 0;
  std::size_t offset = 0;
  while (copied && offset < descriptor.size()) {
    const auto count = write(fd, descriptor.data() + offset, descriptor.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) { copied = false; break; }
    offset += std::size_t(count);
  }
  copied = copied && fstat(fd, &info) == 0 && info.st_uid == user && (info.st_mode & 07777) == 0600 && fsync(fd) == 0;
  if (fd >= 0 && close(fd)) copied = false;
  const int parent = open(target.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  const bool synced = parent >= 0 && fsync(parent) == 0;
  if (parent >= 0) close(parent);
  const auto readback = read_probe_metadata(proof.c_str());
  std::fprintf(log, "private copy file=%d directory_sync=%d readback=%d uid=%u mode=%o\n", copied, synced,
      readback && *readback == descriptor, unsigned(info.st_uid), unsigned(info.st_mode & 07777));
}
static void bootstrap_registration_probe(FILE* log, bool copy = false, int session = 0) {
  struct stat directory{};
  for (const char* path : {"/data/homebrew", "/data/homebrew/FAKE78001", "/data/homebrew/FAKE78001/sce_sys"}) {
    if (lstat(path, &directory) || !S_ISDIR(directory.st_mode)) {
      std::fprintf(log, "bootstrap registration refused: source directory\n"); return;
    }
  }
  const auto owner = read_probe_metadata("/data/homebrew/FAKE78001/playden-acceptance-owner.json");
  const auto descriptor = read_probe_metadata("/data/homebrew/FAKE78001/game-launch.json");
  const auto param = read_probe_metadata("/data/homebrew/FAKE78001/sce_sys/param.json");
  if (!owner || !descriptor || !param || !ValidateBootstrapProbeSource(*owner, *descriptor, *param)) {
    std::fprintf(log, "bootstrap registration refused: fixture identity\n"); return;
  }
  if (copy) { private_copy_probe(log, *descriptor); return; }
  if (session == 2) {
    const std::string root = "/data/playden/games/acceptance-hl2-fresh-v16";
    const auto prior = read_probe_metadata((root + "/logs/sessions/3771c232-a7b7-47eb-af39-fdb231176185.json").c_str());
    struct stat info{}, retired{};
    const bool app_root = runtime::IsPrivateDirectory(root);
    const bool old_logs = lstat((root + "/logs").c_str(), &info) == 0 &&
        S_ISDIR(info.st_mode) && info.st_uid == geteuid() && (info.st_mode & 07777) == 0755;
    const bool free_destination = lstat((root + "/logs.before-private-session").c_str(), &retired) != 0 && errno == ENOENT;
    const bool prior_failed = prior && prior->find("\"install_id\":\"acceptance-hl2-fresh-v16\"") != std::string::npos &&
        prior->find("\"phase\":\"failed\"") != std::string::npos;
    const bool preserved = app_root && old_logs && prior_failed && free_destination &&
        rename((root + "/logs").c_str(), (root + "/logs.before-private-session").c_str()) == 0;
    std::fprintf(log, "session preparation app_root=%d old_logs=%d prior_failed=%d free_destination=%d preserved=%d errno=%d\n",
        app_root, old_logs, prior_failed, free_destination, preserved, preserved ? 0 : errno);
    return;
  }
  if (session == 1) {
    const std::string directory = "/data/playden/games/acceptance-hl2-fresh-v16/logs/sessions";
    DIR* listing = opendir(directory.c_str());
    std::fprintf(log, "private sessions directory_private=%d readable=%d\n",
        runtime::IsPrivateDirectory("/data/playden/games/acceptance-hl2-fresh-v16") &&
        runtime::IsPrivateDirectory("/data/playden/games/acceptance-hl2-fresh-v16/logs") && runtime::IsPrivateDirectory(directory), listing != nullptr);
    unsigned count = 0;
    if (listing) {
      while (const auto* entry = readdir(listing)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        if (++count > 8) { std::fprintf(log, "session listing limit\n"); break; }
        if (name.size() != 41 || name.substr(36) != ".json" || name.find('/') != std::string::npos) continue;
        const std::string path = directory + "/" + name;
        struct stat info{};
        const auto record = read_probe_metadata(path.c_str());
        const bool private_record = lstat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) &&
            info.st_uid == geteuid() && (info.st_mode & 07777) == 0600;
        std::fprintf(log, "private session readable=%d owned=%d uid=%u mode=%o record=%s\n",
            bool(record), private_record, unsigned(info.st_uid), unsigned(info.st_mode & 07777), record ? record->c_str() : "unavailable");
      }
      closedir(listing);
    }
    return;
  }
  TitleRegistry registry([log](const auto& route, const auto& body) { return service_post(log, route, body); });
  const auto result = registry.Register("FAKE78001");
  std::fprintf(log, "bootstrap registration accepted=%d code=%u stage=%s\n", result.ok, result.code, result.stage.c_str());
  const auto current = registry.Query("FAKE78001");
  std::fprintf(log, "bootstrap registry state=%d path=%s\n", int(current.state), current.path.c_str());
}
static void registration_probe(FILE* log, const char* action) {
  TitleRegistry registry([log](const auto& route, const auto& body) { return service_post(log, route, body); });
  const auto current = registry.Query("FAKE78000");
  std::fprintf(log, "registry action=%s state=%d path=%s\n", action, int(current.state), current.path.c_str());
  if (std::string(action) == "register") {
    struct stat existing{};
    if (current.state != RegistrationState::kAbsent ||
        stat("/user/app/FAKE78000", &existing) == 0 || errno != ENOENT ||
        mkdir("/data/homebrew/FAKE78000", 0755) != 0) {
      std::fprintf(log, "registration refused: unavailable or existing identity/source\n"); return;
    }
    bool staged = mkdir("/data/homebrew/FAKE78000/sce_sys", 0755) == 0 &&
        copy_probe_file("eboot.bin", 0755) && copy_probe_file("sce_sys/param.json", 0644) &&
        copy_probe_file("sce_sys/icon0.png", 0644);
    FILE* ownership = staged ? std::fopen("/data/homebrew/FAKE78000/playden-task1-owner.txt", "w") : nullptr;
    if (ownership) {
      staged = std::fputs("Playden Task 1 disposable FAKE78000\n", ownership) >= 0;
      if (std::fclose(ownership)) staged = false;
    } else staged = false;
    std::fprintf(log, "registration staged=%d\n", staged);
    if (staged) {
      const auto result = registry.Register("FAKE78000");
      std::fprintf(log, "registration accepted=%d code=%u stage=%s\n", result.ok, result.code, result.stage.c_str());
    }
  } else if (std::string(action) == "rescan") {
    char marker[128]{};
    FILE* owner = std::fopen("/data/homebrew/FAKE78000/playden-task1-owner.txt", "r");
    const bool owned = owner && std::fgets(marker, sizeof(marker), owner) &&
        std::string(marker) == "Playden Task 1 disposable FAKE78000\n";
    if (owner) std::fclose(owner);
    if (!owned) { std::fprintf(log, "rescan refused: ownership\n"); return; }
    const auto result = registry.Register("FAKE78000");
    std::fprintf(log, "rescan accepted=%d code=%u stage=%s\n", result.ok, result.code, result.stage.c_str());
  } else if (std::string(action) == "remove") {
    char marker[128]{};
    FILE* owner = std::fopen("/data/homebrew/FAKE78000/playden-task1-owner.txt", "r");
    const bool owned = owner && std::fgets(marker, sizeof(marker), owner) &&
        std::string(marker) == "Playden Task 1 disposable FAKE78000\n";
    if (owner) std::fclose(owner);
    struct stat existing{};
    if (!owned || current.path != "/data/homebrew/FAKE78000" ||
        stat("/data/playden/task1-retired-FAKE78000", &existing) == 0 || errno != ENOENT) {
      std::fprintf(log, "removal refused: ownership or retirement destination\n"); return;
    }
    const auto removed = registry.RemoveManualSource("FAKE78000");
    if (!removed.ok) { std::fprintf(log, "manual removal failed=%u\n", removed.code); return; }
    if (rename("/data/homebrew/FAKE78000", "/data/playden/task1-retired-FAKE78000")) {
      std::fprintf(log, "retirement failed=%d\n", errno); return;
    }
    const auto result = registry.Unregister("FAKE78000");
    std::fprintf(log, "unregister accepted=%d code=%u stage=%s\n", result.ok, result.code, result.stage.c_str());
  }
}
bool RunTitleContractProbe(const NativeAccessResult& access) {
  if (!access.runtime_ready || !access.granted || access.paths.title_root.empty()) return false;
  const auto request_path = access.paths.title_root + "/dev/title-contract-request.txt";
  FILE* request = std::fopen(request_path.c_str(), "r");
  if (!request) return false;
  char action[128]{};
  const auto action_size = std::fread(action, 1, sizeof(action), request);
  bool request_valid = !std::ferror(request) && action_size > 0 && action_size < sizeof(action);
  for (std::size_t i = 0; i < action_size; ++i)
    if (!action[i]) request_valid = false;
  auto trimmed_size = action_size;
  while (trimmed_size && (action[trimmed_size - 1] == '\n' || action[trimmed_size - 1] == '\r')) --trimmed_size;
  if (trimmed_size < sizeof(action)) action[trimmed_size] = 0;
  for (std::size_t i = 0; i < trimmed_size; ++i)
    if (action[i] == '\n' || action[i] == '\r') request_valid = false;
  std::fclose(request);
  std::remove(request_path.c_str());
  mkdir(access.paths.log_root.c_str(), 0755);
  FILE* log = std::fopen((access.paths.log_root + "/title-contract.log").c_str(), "a");
  if (!log) return true;
  std::fprintf(log, "begin pid=%d\n", getpid()); std::fflush(log);
  if (!request_valid) {
    std::fprintf(log, "request rejected\nend\n"); std::fclose(log); return true;
  }
  if (!steam::internal::InitializeNativeNetwork()) {
    std::fprintf(log, "network unavailable\n"); std::fclose(log); return true;
  }
  service_post(log, "/api/v1/version", "{}");
  const int result = access.granted ? 0 : -1;
  std::fprintf(log, "bootstrap grant=%d runtime_ready=%d euid=%d\n", access.granted, access.runtime_ready, geteuid());
  std::fflush(log);
  if (std::string(action) == "title-source") {
    TitleRegistry registry([log](const std::string& route, const std::string& body) { return service_post(log, route, body); });
    for (const char* title : {"FAKE77031", "FAKE78001", "FAKE78998"}) {
      const auto source = ReadTitleSource("/data/homebrew", title);
      std::fprintf(log, "title_source title=%s availability=%d free=%d occupied=%d unknown=%d directory=%d private=%d\n",
          title, static_cast<int>(source.availability), static_cast<int>(runtime::TitleAvailability::kFree),
          static_cast<int>(runtime::TitleAvailability::kOccupied), static_cast<int>(runtime::TitleAvailability::kUnknown),
          source.directory, source.owned_directory);
      const auto cached = registry.Query(title);
      std::fprintf(log, "title_source_cache title=%s state=%d path=%s\n", title, static_cast<int>(cached.state), cached.path.c_str());
    }
    std::fprintf(log, "end\n"); std::fclose(log); return true;
  }
  if (std::string(action) == "publication-request") {
    const auto root = std::filesystem::path("/data/playden") / ("request-probe-" + std::to_string(getpid()));
    struct stat info{}; std::string reason;
    bool ok = lstat(root.c_str(), &info) != 0 && errno == ENOENT &&
        runtime::EnsurePrivateDirectories("/data", root, root, &reason);
    runtime::SettingsPublicationRequest request;
    char transaction[37]{};
    std::snprintf(transaction, sizeof(transaction), "00000000-0000-7000-8000-%012u", unsigned(getpid()));
    request.transaction_id = transaction; request.kit_id = "request-probe"; request.registry_unix_time = 1760000000;
    auto& r = request.install;
    r.install_id = "request-probe"; r.title_id = "FAKE78001"; r.app_id = 220;
    r.data_root = "/data/playden/games/request-probe"; r.default_profile_id = "playden-steam-220";
    r.runtime_bundle_id = "wine-wow64"; r.recipe_id = "steam-220-hl2-v1"; r.recipe_version = "1";
    r.settings_revision = 7; r.launch_entry_id = "main"; r.launch_path = "probe.exe";
    r.install_path = "/data/homebrew/FAKE78001/games/220"; r.volume_id = "internal";
    auto& p = request.plan;
    p.app_id = r.app_id; p.recipe_id = r.recipe_id; p.recipe_version = r.recipe_version;
    p.profile_id = r.default_profile_id; p.settings_revision = r.settings_revision;
    p.launch_entry_id = "main"; p.launch_entry = {"main", "Play", "probe.exe", ""};
    p.profile_bytes = "[application]\nid = playden-steam-220\nname = Probe\n"
        "executable = Z:\\data\\homebrew\\FAKE78001\\games\\220\\probe.exe\n"
        "working_directory = Z:\\data\\homebrew\\FAKE78001\\games\\220\n"
        "arguments = -steam\nprefix = game\nruntime = wine-wow64\narchitecture = pe64\ngraphics = dxvk\n";
    auto bytes = runtime::EncodeSettingsPublicationRequest(request, &reason);
    ok = ok && bytes.has_value() && runtime::CreateSettingsPublicationRequest(root, request).ok();
    auto loaded = runtime::LoadSettingsPublicationRequest(root, request.transaction_id);
    auto observed = loaded.request ? runtime::EncodeSettingsPublicationRequest(*loaded.request, &reason) : std::nullopt;
    ok = ok && loaded.state == runtime::PublicationRecoveryState::kValid && bytes == observed &&
        !runtime::CreateSettingsPublicationRequest(root, request).ok();
    std::fprintf(log, "publication_request root=%s euid=%u passed=%d state=%d bytes=%zu detail=%s\nend\n",
        root.c_str(), unsigned(geteuid()), ok, int(loaded.state), bytes ? bytes->size() : 0, reason.c_str());
    std::fclose(log); return true;
  }
  if (std::string(action) == "title-inventory") {
    const char* path = "/system_data/priv/mms/app.db";
    const int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    const int open_error = fd < 0 ? errno : 0;
    struct stat info{};
    const int stat_result = fd >= 0 ? fstat(fd, &info) : -1;
    char header[16]{};
    ssize_t count = -1;
    if (fd >= 0 && stat_result == 0 && S_ISREG(info.st_mode)) {
      do { count = read(fd, header, sizeof(header)); } while (count < 0 && errno == EINTR);
    }
    const bool sqlite = count == sizeof(header) && std::memcmp(header, "SQLite format 3\0", sizeof(header)) == 0;
    const int close_result = fd >= 0 ? close(fd) : -1;
    std::fprintf(log, "title_inventory_file path=%s open=%d open_errno=%d stat=%d regular=%d uid=%u mode=%o size=%lld header_bytes=%lld sqlite_header=%d close=%d\n",
        path, fd >= 0, open_error, stat_result, S_ISREG(info.st_mode), static_cast<unsigned>(info.st_uid),
        static_cast<unsigned>(info.st_mode & 07777), static_cast<long long>(info.st_size),
        static_cast<long long>(count), sqlite, close_result);
    for (const char* title : {"FAKE77031", "FAKE78001", "FAKE78998"}) {
      const auto observed = ReadNativeTitleDatabase(title);
      std::fprintf(log, "title_inventory_query title=%s state=%d free=%d occupied=%d unknown=%d identity=%d content=%s metadata=%s\n",
          title, static_cast<int>(observed.inventory), static_cast<int>(runtime::TitleAvailability::kFree),
          static_cast<int>(runtime::TitleAvailability::kOccupied), static_cast<int>(runtime::TitleAvailability::kUnknown),
          observed.identity.has_value(), observed.identity ? observed.identity->content_id.c_str() : "",
          observed.identity ? observed.identity->metadata_path.c_str() : "");
    }
    std::fprintf(log, "end\n");
    std::fclose(log); return true;
  }
  if (std::string(action) == "title-activity") {
    const auto self_title = std::filesystem::path(access.paths.title_root).filename().string();
    const TitleActivityTrace trace = [log](const std::string& event) { std::fprintf(log, "title_activity_trace %s\n", event.c_str()); };
    const auto self = ReadTitleActivity(self_title, trace);
    const auto wine = ReadTitleActivity("FAKE78001", trace);
    std::fprintf(log, "title_activity self_title=%s self=%d wine=%d unknown=%d stopped=%d active=%d\nend\n",
        self_title.c_str(), static_cast<int>(self), static_cast<int>(wine),
        static_cast<int>(runtime::NativeActivity::kUnknown), static_cast<int>(runtime::NativeActivity::kStopped),
        static_cast<int>(runtime::NativeActivity::kActive));
    std::fclose(log); return true;
  }
  if (std::string(action).starts_with("publication ")) {
    char run[17]{}, operation[32]{}, extra{};
    const bool valid = std::sscanf(action, "publication %16s %31s %c", run, operation, &extra) == 2;
    const int rc = valid ? playden_publication_native_probe(log, run, operation) : -1;
    std::fprintf(log, "publication bridge rc=%d\nend\n", rc);
    std::fclose(log); return true;
  }
  if (std::string(action) == "managed-filesystem") {
    RunManagedFilesystemProbe(log);
    std::fprintf(log, "end\n"); std::fclose(log); return true;
  }
  if (std::string(action) == "directory-contract") {
    for (const bool kernel_fd : {false, true}) {
      const int root = kernel_fd ? playden_ps5_probe_kernel_directory("/data") : open("/data", O_RDONLY);
      struct stat info{};
      const int inspected = root >= 0 ? fstat(root, &info) : -1;
      std::fprintf(log, "directory contract kernel_fd=%d root_fd=%d stat=%d mode=%o\n", kernel_fd, root, inspected, unsigned(info.st_mode));
      for (const int flags : {O_RDONLY, O_RDONLY | O_NOFOLLOW, O_RDONLY | O_DIRECTORY, O_RDONLY | O_NOFOLLOW | O_DIRECTORY}) {
        for (const bool absolute : {false, true}) {
          errno = 0;
          const int fd = openat(absolute ? AT_FDCWD : root, absolute ? "/data/playden" : "playden", flags);
          const int error = fd < 0 ? errno : 0;
          const int checked = fd >= 0 ? fstat(fd, &info) : -1;
          std::fprintf(log, "directory contract kernel_fd=%d absolute=%d flags=%x fd=%d errno=%d stat=%d mode=%o\n", kernel_fd, absolute, flags, fd, error, checked, unsigned(info.st_mode));
          if (fd >= 0) close(fd);
        }
      }
      if (root >= 0) close(root);
    }
    // Kernel-owned descriptor aliases, if mounted, could preserve FD-relative
    // semantics without resolving a remembered userspace pathname. Read only.
    const int alias_root = playden_ps5_probe_kernel_directory("/data");
    const int target = open("/data/playden", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    struct stat expected{};
    const bool expected_ok = target >= 0 && fstat(target, &expected) == 0;
    for (const auto& prefix : {std::string("/dev/fd/"), std::string("/proc/self/fd/"),
                             std::string("/proc/curproc/fd/"), "/proc/" + std::to_string(getpid()) + "/fd/"}) {
      const std::string path = prefix + std::to_string(alias_root) + "/playden";
      errno = 0;
      const int fd = alias_root >= 0 ? open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW) : -1;
      const int error = fd < 0 ? errno : 0;
      struct stat actual{};
      const bool same = expected_ok && fd >= 0 && fstat(fd, &actual) == 0 &&
          S_ISDIR(actual.st_mode) && actual.st_dev == expected.st_dev && actual.st_ino == expected.st_ino;
      std::fprintf(log, "directory alias path=%s fd=%d errno=%d same_object=%d device=%llu inode=%llu\n",
          path.c_str(), fd, error, same, (unsigned long long)actual.st_dev, (unsigned long long)actual.st_ino);
      if (fd >= 0) close(fd);
    }
    if (target >= 0) close(target);
    if (alias_root >= 0) close(alias_root);
    std::fprintf(log, "end\n"); std::fclose(log); return true;
  }
  if (std::string(action) == "ownership-api") {
    for (const int module : {1, 0x2001}) {
      for (const char* name : {"chown", "_chown", "fchown", "__wrap_fchown", "sceKernelChown", "sceKernelFchown", "getpid"}) {
        void* function = nullptr;
        const int rc = sceKernelDlsym(module, name, &function);
        std::fprintf(log, "ownership api module=%x name=%s result=%08x pointer=%p\n", module, name, unsigned(rc), function);

      }
    }
    std::fprintf(log, "end\n"); std::fclose(log); return true;
  }
  if (std::string(action) == "register-bootstrap" || std::string(action) == "private-copy" || std::string(action) == "private-session" || std::string(action) == "prepare-private-session") {
    bootstrap_registration_probe(log, std::string(action) == "private-copy", std::string(action) == "prepare-private-session" ? 2 : std::string(action) == "private-session" ? 1 : 0);
    std::fprintf(log, "end\n"); std::fclose(log); return true;
  }
  if (result == 0) {
    // A new folder outside Playden's title proves the granted view can provision state.
    const int made = mkdir("/data/playden", 0755);
    const int make_error = made ? errno : 0;
    FILE* marker = std::fopen("/data/playden/task1-write-proof.txt", "w");
    bool wrote = false;
    if (marker) { wrote = std::fputs("Playden native Task 1 provisioning proof\n", marker) >= 0;
      if (std::fclose(marker)) wrote = false; }
    std::fprintf(log, "external_write mkdir=%d errno=%d wrote=%d\n", made, make_error, wrote);
    const int loaded = sceSysmoduleLoadModuleInternal(0x80000010);
    int service = -1;
    const int handled = sceSysmoduleGetModuleHandleInternal(0x80000010, &service);
    void* init = nullptr; void* launch = nullptr;
    void* public_launch = nullptr; void* hide = nullptr;
    const int public_resolve = sceKernelDlsym(service, "sceSystemServiceLaunchApp", &public_launch);
    const int hide_resolve = sceKernelDlsym(service, "sceSystemServiceHideSplashScreen", &hide);
    std::fprintf(log, "public systemservice launch=%08x:%p hide=%08x:%p\n", unsigned(public_resolve), public_launch, unsigned(hide_resolve), hide);
    if (std::string(action) == "handoff" && public_launch) {
      const int initialized = sceUserServiceInitialize(nullptr);
      std::fprintf(log, "handoff user_initialize=%08x\n", unsigned(initialized)); std::fflush(log);
      TitleBackend backend({nullptr, sceUserServiceGetForegroundUser, nullptr,
          reinterpret_cast<decltype(TitleApi::public_launch)>(public_launch)});
      std::fprintf(log, "elevated handoff submit title=PPSA99995 profile=playden-steam-220 check_flag=1\n"); std::fflush(log);
      const auto launch_result = backend.Launch("PPSA99995", {"profile=playden-steam-220"});
      std::fprintf(log, "elevated handoff accepted=%d code=%08x stage=%s\n",
          launch_result.ok, launch_result.code, launch_result.stage.c_str()); std::fflush(log);
    }
    const int resolved_init = sceKernelDlsym(service, "sceLncUtilInitialize", &init);
    const int resolved_launch = sceKernelDlsym(service, "sceLncUtilLaunchApp", &launch);
    std::fprintf(log, "elevated lnc load=%08x handle_result=%08x handle=%d init=%08x:%p launch=%08x:%p\n",
                 unsigned(loaded), unsigned(handled), service, unsigned(resolved_init), init, unsigned(resolved_launch), launch);
    // Determine whether the native application may access AppInstUtil directly.
    const int handle = sceKernelLoadStartModule("/system/common/lib/libSceAppInstUtil.sprx", 0,
                                                nullptr, 0, nullptr, nullptr);
    std::fprintf(log, "appinst load=0x%08x\n", static_cast<unsigned>(handle));
    for (const unsigned id : {0x80000014u, 0x80000068u}) {
      const int loaded = sceSysmoduleLoadModuleInternal(id);
      int module = -1;
      const int found = sceSysmoduleGetModuleHandleInternal(id, &module);
      void* init = nullptr; void* install = nullptr; void* uninstall = nullptr;
      const int a = module >= 0 ? sceKernelDlsym(module, "sceAppInstUtilInitialize", &init) : -1;
      const int b = module >= 0 ? sceKernelDlsym(module, "Wudg3Xe3heE", &install) : -1;
      const int c = module >= 0 ? sceKernelDlsym(module, "sceAppInstUtilAppUnInstall", &uninstall) : -1;
      std::fprintf(log, "appinst internal id=%08x load=%08x found=%08x handle=%d init=%08x:%p install=%08x:%p uninstall=%08x:%p\n",
                   id, unsigned(loaded), unsigned(found), module, unsigned(a), init, unsigned(b), install, unsigned(c), uninstall);
      std::fflush(log);
    }
    if (handle >= 0) {
      void* initialize = nullptr;
      void* install = nullptr;
      void* uninstall = nullptr;
      const int a = sceKernelDlsym(handle, "sceAppInstUtilInitialize", &initialize);
      const int b = sceKernelDlsym(handle, "Wudg3Xe3heE", &install);
      const int c = sceKernelDlsym(handle, "sceAppInstUtilAppUnInstall", &uninstall);
      std::fprintf(log, "appinst resolve init=%d:%p install=%d:%p uninstall=%d:%p\n",
                   a, initialize, b, install, c, uninstall);
      if (initialize) std::fprintf(log, "appinst initialize=0x%08x\n",
          static_cast<unsigned>(reinterpret_cast<int(*)()>(initialize)()));
    }
  }
  if (result == 0) registration_probe(log, action);
  std::fprintf(log, "end\n"); std::fclose(log);
  return true;
}
}
