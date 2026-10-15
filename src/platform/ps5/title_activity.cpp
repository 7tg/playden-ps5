#include "platform/ps5/title_activity.h"
#include "platform/ps5/title_runtime.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <set>
#if defined(__PROSPERO__)
#include <sys/sysctl.h>
#include <sys/user.h>
#include <unistd.h>
#include <cstddef>
extern "C" int sceKernelDlsym(int, const char*, void**);
#endif
namespace playden::ps5 {
namespace {
constexpr std::size_t kMaxBytes = 4 * 1024 * 1024, kMaxProcesses = 4096;
bool InventoryValid(const std::vector<std::int32_t>& pids) {
  if (pids.empty() || pids.size() > kMaxProcesses) return false;
  std::set<std::int32_t> unique;
  bool user = false;
  for (auto pid : pids) { if (pid < 0 || !unique.insert(pid).second) return false; user |= pid > 0; }
  return user;
}
}
std::optional<std::vector<std::int32_t>> DecodeTitleActivityProcessIds(const std::vector<std::uint8_t>& bytes) {
  if (bytes.empty() || bytes.size() > kMaxBytes) return std::nullopt;
  std::vector<std::int32_t> pids; std::size_t offset = 0; std::uint32_t stride = 0;
  while (offset < bytes.size()) {
    if (bytes.size() - offset < 76) return std::nullopt;
    std::uint32_t length = 0; std::int32_t pid = -1;
    std::memcpy(&length, bytes.data() + offset, 4); std::memcpy(&pid, bytes.data() + offset + 72, 4);
    if (length < 76 || length > 8192 || length > bytes.size() - offset || (stride && stride != length)) return std::nullopt;
    stride = length; pids.push_back(pid); if (pids.size() > kMaxProcesses) return std::nullopt;
    offset += length;
  }
  return InventoryValid(pids) ? std::optional<std::vector<std::int32_t>>(std::move(pids)) : std::nullopt;
}
runtime::NativeActivity QueryTitleActivity(const std::string& title, const TitleActivityApi& api, std::string* reason) {
  using runtime::NativeActivity;
  const auto unknown = [reason](std::string why) {
    if (reason) *reason = std::move(why);
    return NativeActivity::kUnknown;
  };
  if (!ValidTitleId(title) || !api.processes || !api.identity) return unknown("no title id or process reader");
  auto first = api.processes(); if (!first || !InventoryValid(*first)) return unknown("process list unreadable");
  std::sort(first->begin(), first->end());
  std::vector<ProcessTitleIdentity> identities(first->size());
  // The first process whose identity cannot be trusted, and how many there are.
  std::string untrusted; unsigned untrusted_count = 0;
  auto valid_identity = [&](std::int32_t pid, const std::optional<ProcessTitleIdentity>& value) {
    // FW13.60 PID 1 has app_id zero and a valid system title. A successful
    // title identity is authoritative regardless of app_id; only an untitled
    // identity requires app_id zero. Failed calls never become non-apps.
    if (value && ((value->app_id == 0 && value->title_id.empty()) || ValidTitleId(value->title_id))) return true;
    if (!untrusted_count++)
      untrusted = "pid=" + std::to_string(pid) + (value ? " app_id=" + std::to_string(value->app_id) +
                                                          " title=\"" + value->title_id + "\" is not a title"
                                                        : std::string(" identity unavailable"));
    return false;
  };
  const auto untrusted_reason = [&] {
    return untrusted + (untrusted_count > 1 ? " (+" + std::to_string(untrusted_count - 1) + " more)" : std::string());
  };
  for (std::size_t i = 0; i < first->size(); ++i) {
    const auto pid = (*first)[i]; if (pid == 0) continue; // Kernel cannot be a user title.
    auto identity = api.identity(pid);
    if (!valid_identity(pid, identity)) continue;
    if (identity->title_id == title) return NativeActivity::kActive;
    identities[i] = *identity;
  }
  if (untrusted_count) return unknown(untrusted_reason());
  auto second = api.processes(); if (!second || !InventoryValid(*second)) return unknown("process list unreadable");
  std::sort(second->begin(), second->end()); if (*first != *second) return unknown("process list changed");
  std::string changed;
  for (std::size_t i = 0; i < second->size(); ++i) {
    const auto pid = (*second)[i]; if (pid == 0) continue;
    auto identity = api.identity(pid);
    if (!valid_identity(pid, identity)) continue;
    if (identity->title_id == title) return NativeActivity::kActive;
    if (changed.empty() && (identity->app_id != identities[i].app_id || identity->title_id != identities[i].title_id))
      changed = "pid=" + std::to_string(pid) + " identity changed";
  }
  if (untrusted_count) return unknown(untrusted_reason());
  if (!changed.empty()) return unknown(changed);
  auto final = api.processes(); if (!final || !InventoryValid(*final)) return unknown("process list unreadable");
  std::sort(final->begin(), final->end());
  return *first == *final ? NativeActivity::kStopped : unknown("process list changed");
}
runtime::NativeActivity ReadTitleActivity(const std::string& title, std::string* reason) {
  if (!ValidTitleId(title)) {
    if (reason) *reason = "no title id";
    return runtime::NativeActivity::kUnknown;
  }
#if defined(__PROSPERO__)
  // Same 96-byte AppInfo layout used by the title-scoped elevation helper.
  struct AppInfo { std::uint32_t app_id; std::uint64_t reserved; char title_id[14]; char padding[0x3c]; };
  static_assert(sizeof(AppInfo) == 96 && offsetof(AppInfo, title_id) == 16);
  static_assert(offsetof(kinfo_proc, ki_pid) == 72 && offsetof(kinfo_proc, ki_structsize) == 0);
  // The first kernel call that failed, for an unknown result's reason.
  std::string failed;
  const auto unknown = [&](const std::string& why) {
    if (reason) *reason = failed.empty() ? why : why + "; " + failed;
    return runtime::NativeActivity::kUnknown;
  };
  using GetInfo = int (*)(int, AppInfo*); GetInfo get_info = nullptr;
  if (sceKernelDlsym(0x2001, "sceKernelGetAppInfo", reinterpret_cast<void**>(&get_info)) != 0 || !get_info)
    return unknown("sceKernelGetAppInfo is not available");
  auto identity = [get_info, &failed](std::int32_t pid) -> std::optional<ProcessTitleIdentity> {
    AppInfo value{};
    const auto rc = get_info(pid, &value);
    const auto* end = static_cast<const char*>(std::memchr(value.title_id, 0, sizeof(value.title_id)));
    if (rc != 0 || !end) {
      if (failed.empty()) {
        char code[16]; std::snprintf(code, sizeof(code), "0x%08x", static_cast<unsigned>(rc));
        failed = "sceKernelGetAppInfo pid=" + std::to_string(pid) + " rc=" + code + (end ? "" : " title unterminated");
      }
      return std::nullopt;
    }
    return ProcessTitleIdentity{value.app_id, std::string(value.title_id, static_cast<std::size_t>(end - value.title_id))};
  };
  const auto self_pid = ::getpid(); auto self = self_pid > 0 ? identity(self_pid) : std::nullopt;
  if (!self || !self->app_id || !ValidTitleId(self->title_id)) return unknown("Playden's own identity unavailable");
  TitleActivityApi api; api.identity = identity;
  api.processes = [self_pid, &failed]() -> std::optional<std::vector<std::int32_t>> {
    const auto fail = [&failed](const std::string& why) {
      if (failed.empty()) failed = why;
      return std::nullopt;
    };
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0}; std::size_t length = 0;
    if (::sysctl(mib, 4, nullptr, &length, nullptr, 0) != 0)
      return fail("sysctl size errno=" + std::to_string(errno));
    if (!length || length > kMaxBytes) return fail("sysctl size bytes=" + std::to_string(length));
    std::vector<std::uint8_t> bytes(length);
    if (::sysctl(mib, 4, bytes.data(), &length, nullptr, 0) != 0)
      return fail("sysctl errno=" + std::to_string(errno));
    if (length > bytes.size() || !length) return fail("sysctl bytes=" + std::to_string(length));
    bytes.resize(length); auto pids = DecodeTitleActivityProcessIds(bytes);
    if (!pids) return fail("process records malformed bytes=" + std::to_string(length));
    // Qualify the snapshot's PID field against this independently known PID.
    if (std::find(pids->begin(), pids->end(), self_pid) == pids->end())
      return fail("process list lacks Playden's pid=" + std::to_string(self_pid));
    return pids;
  };
  std::string query_reason;
  const auto activity = QueryTitleActivity(title, api, &query_reason);
  return activity == runtime::NativeActivity::kUnknown ? unknown(query_reason) : activity;
#else
  if (reason) *reason = "not a PS5 build";
  return runtime::NativeActivity::kUnknown;
#endif
}
}  // namespace playden::ps5
