#include "platform/ps5/title_contract_probe.h"
#include "steam/backend/native_network.h"
#include "platform/ps5/title_registry.h"
#include "platform/ps5/title_probe_contract.h"
#include "runtime/private_directories.h"
extern "C" {
int playden_ps5_directory_api_ready(void);
int sceKernelLoadStartModule(const char*, std::size_t, const void*, unsigned, void*, int*);
int sceKernelDlsym(int, const char*, void**);
int sceSysmoduleLoadModuleInternal(unsigned);
int sceSysmoduleGetModuleHandleInternal(unsigned, int*);
int sceUserServiceInitialize(void*);
int sceUserServiceGetForegroundUser(unsigned*);
}
#include <cstdio>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <fcntl.h>
#include <dirent.h>

namespace playden::ps5 {
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
static void bootstrap_registration_probe(FILE* log, bool copy = false, bool session = false) {
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
  if (session) {
    const char* path = "/data/playden/games/acceptance-hl2-fresh-v16/logs/sessions/3771c232-a7b7-47eb-af39-fdb231176185.json";
    struct stat info{};
    const auto record = read_probe_metadata(path);
    const bool private_record = lstat(path, &info) == 0 && S_ISREG(info.st_mode) &&
        info.st_uid == geteuid() && (info.st_mode & 07777) == 0600;
    std::fprintf(log, "private session readable=%d owned=%d uid=%u mode=%o record=%s\n",
        bool(record), private_record, unsigned(info.st_uid), unsigned(info.st_mode & 07777), record ? record->c_str() : "unavailable");
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
  char action[32]{};
  if (!std::fgets(action, sizeof(action), request)) action[0] = 0;
  for (char& ch : action) if (ch == '\n' || ch == '\r') ch = 0;
  std::fclose(request);
  std::remove(request_path.c_str());
  mkdir(access.paths.log_root.c_str(), 0755);
  FILE* log = std::fopen((access.paths.log_root + "/title-contract.log").c_str(), "a");
  if (!log) return true;
  std::fprintf(log, "begin pid=%d\n", getpid()); std::fflush(log);
  if (!steam::internal::InitializeNativeNetwork()) {
    std::fprintf(log, "network unavailable\n"); std::fclose(log); return true;
  }
  service_post(log, "/api/v1/version", "{}");
  const int result = access.granted ? 0 : -1;
  std::fprintf(log, "bootstrap grant=%d runtime_ready=%d euid=%d\n", access.granted, access.runtime_ready, geteuid());
  std::fflush(log);
  if (std::string(action) == "directory-contract") {
    const int root = open("/data", O_RDONLY);
    struct stat info{};
    const int inspected = root >= 0 ? fstat(root, &info) : -1;
    std::fprintf(log, "directory contract root_fd=%d stat=%d mode=%o\n", root, inspected, unsigned(info.st_mode));
    for (const int flags : {O_RDONLY, O_RDONLY | O_NOFOLLOW, O_RDONLY | O_DIRECTORY, O_RDONLY | O_NOFOLLOW | O_DIRECTORY}) {
      for (const bool absolute : {false, true}) {
        errno = 0;
        const int fd = openat(absolute ? AT_FDCWD : root, absolute ? "/data/playden" : "playden", flags);
        const int error = fd < 0 ? errno : 0;
        const int checked = fd >= 0 ? fstat(fd, &info) : -1;
        std::fprintf(log, "directory contract absolute=%d flags=%x fd=%d errno=%d stat=%d mode=%o\n", absolute, flags, fd, error, checked, unsigned(info.st_mode));
        if (fd >= 0) close(fd);
      }
    }
    if (root >= 0) close(root);
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
  if (std::string(action) == "register-bootstrap" || std::string(action) == "private-copy" || std::string(action) == "private-session") {
    bootstrap_registration_probe(log, std::string(action) == "private-copy", std::string(action) == "private-session");
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
