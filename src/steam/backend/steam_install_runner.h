#pragma once

#include "steam/backend/cdn_client.h"
#include "steam/backend/cm_client.h"
#include "steam/backend/depot_installer.h"
#include "steam/install_service.h"

#include <chrono>
#include <mutex>
#include <optional>

namespace playden::steam {

// The Fastest race's winner (a host), kept in memory so a queue of games races once. It belongs to one
// Steam account and lasts kMinutes; another account, an empty host or Clear forgets it.
class FastestServerCache {
 public:
  static constexpr int kMinutes = 45;
  using Now = std::function<std::chrono::steady_clock::time_point()>;
  explicit FastestServerCache(Now now = {}) : now_(std::move(now)) {}
  std::optional<std::string> Get(std::uint64_t account);
  void Remember(std::uint64_t account, const std::string& host);
  void Clear();

 private:
  std::chrono::steady_clock::time_point Time() const { return now_ ? now_() : std::chrono::steady_clock::now(); }
  Now now_;
  std::mutex mutex_;
  std::uint64_t account_ = 0;
  std::string host_;
  std::chrono::steady_clock::time_point at_{};
};

std::string FormatInstallStats(const char* phase, const InstallStats& stats);
std::string FormatInstallServerList(const std::vector<ContentServer>& servers, const char* preference);
std::vector<std::string> FormatInstallServerStats(const std::vector<InstallServerStats>& servers);
using InstallLogFn = std::function<void(std::uint32_t app, const char* step, const char* category, const char* detail)>;
void LogInstallTrace(const InstallLogFn& log, std::uint32_t app, const char* marker);

// Installs a Steam game: PICS app info for the depot selection (Windows, English, public
// branch), then the resumable depot installer.
class SteamInstallRunner final : public SessionInstallRunner {
 public:
  SteamInstallRunner(SessionRepository& sessions, CmClient& cm, CdnClient& cdn, FastestServerCache::Now now = {})
      : SessionInstallRunner(sessions), cm_(cm), installer_(cm, cdn), fastest_(std::move(now)) {}
  Result InstallApp(const StoredSession& session, std::uint32_t id, const std::string& root,
                 const std::atomic<bool>& cancel,
                 const std::function<void(std::uint64_t, std::uint64_t, std::uint64_t)>& progress,
                 const std::function<void(InstallStep)>& step) override;
  std::string Remove(const std::string& root) override;
  std::string DownloadServerName() const override { return server_name_; }
  FastestServerCache& fastest_server() { return fastest_; }
  void SetTuning(std::function<DownloadTuning()> tuning) { tuning_ = std::move(tuning); }
  // Optional sanitized log: the Steam app ID, step, error category and a short detail; never
  // tokens or account identifiers. Called on the install worker.
  using LogFn = InstallLogFn;
  void SetLog(LogFn log) {
    log_ = log;
    installer_.SetStats([this, log](const char* phase, const InstallStats& stats) {
      if (log) {
        if (std::string(phase) == "servers") {
          for (const auto& detail : FormatInstallServerStats(stats.server_stats))
            log(current_app_.load(), "stats", "servers", detail.c_str());
          return;
        }
        const auto detail = FormatInstallStats(phase, stats);
        log(current_app_.load(), "stats", phase, detail.c_str());
      }
    });
    installer_.SetServerList([this, log](const std::vector<ContentServer>& servers, const char* preference) {
      if (log) {
        const auto detail = FormatInstallServerList(servers, preference);
        log(current_app_.load(), "servers", "listed", detail.c_str());
      }
    });
    installer_.SetTrace([this, log](const char* step) { LogInstallTrace(log, current_app_.load(), step); });
    installer_.SetMerge([this, log](const char* result, const std::string& detail) {
      if (log) log(current_app_.load(), "merge", result, detail.c_str());
    });
    installer_.SetRace([this, log](const char* step, const char* result, const std::string& detail) {
      if (log) log(current_app_.load(), step, result, detail.c_str());
    });
  }

 protected:
  void SignedOut() override { fastest_.Clear(); }

 private:
  CmClient& cm_;
  DepotInstaller installer_;
  FastestServerCache fastest_;
  std::string server_name_;  // the install worker's own: set and read during InstallApp's progress
  LogFn log_;
  std::function<DownloadTuning()> tuning_;
  std::atomic<std::uint32_t> current_app_{0};
};

// User-facing text for an install failure category.
const char* InstallFailureText(InstallError error);

}  // namespace playden::steam
