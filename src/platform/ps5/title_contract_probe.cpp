#include "platform/ps5/title_contract_probe.h"
#include "steam/backend/native_network.h"
#include "platform/ps5/title_registry.h"
extern "C" {
#include "pw_data_mount.h"
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
bool RunTitleContractProbe() {
  FILE* request = std::fopen("/app0/dev/title-contract-request.txt", "r");
  if (!request) return false;
  char action[32]{};
  if (!std::fgets(action, sizeof(action), request)) action[0] = 0;
  for (char& ch : action) if (ch == '\n' || ch == '\r') ch = 0;
  std::fclose(request);
  std::remove("/app0/dev/title-contract-request.txt");
  mkdir("/app0/logs", 0755);
  FILE* log = std::fopen("/app0/logs/title-contract.log", "a");
  if (!log) return true;
  std::fprintf(log, "begin pid=%d\n", getpid()); std::fflush(log);
  if (!steam::internal::InitializeNativeNetwork()) {
    std::fprintf(log, "network unavailable\n"); std::fclose(log); return true;
  }
  PwDataMountResult detail{};
  service_post(log, "/api/v1/version", "{}");
  const auto result = pw_data_mount_request(&detail);
  std::fprintf(log, "data result=%d before=%d helper=%d errno=%d after=%d\n", result,
               detail.data_before, detail.helper_completed, detail.helper_errno, detail.data_after);
  std::fflush(log);
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
