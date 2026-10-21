#pragma once

#include "installs/install_service.h"
#include "steam/session_store.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace playden::steam {

// The install service moved to src/installs/; these names keep Steam code compiling.
using installs::CompletedDownload;
using installs::CompletedDownloads;
using installs::InstallJobState;
using installs::InstallJobView;
using installs::InstalledGame;
using installs::InstallRequest;
using installs::InstallRunner;
using installs::InstallService;
using installs::InstallSnapshot;
using installs::InstallStep;
using installs::InstallVolume;
using installs::kCompletedDays;
using installs::kMaxCompleted;
using installs::ParseInstallRequests;

// Steam's install runner: it reads the saved Steam session itself and installs by app ID. Without
// a session (Playden may still be restoring it, or nobody signed in) the job waits for the sign-in.
class SessionInstallRunner : public InstallRunner {
 public:
  static constexpr const char* kWaitingForSignIn = "Waiting for the Steam sign-in.";
  static constexpr const char* kWaitingForSteam = "Waiting to reach Steam. It tries again by itself.";

  explicit SessionInstallRunner(SessionRepository& sessions) : sessions_(sessions) {}
  Result Install(const GameId& id, const std::string& root, const std::atomic<bool>& cancel,
                 const std::function<void(std::uint64_t, std::uint64_t, std::uint64_t)>& progress,
                 const std::function<void(InstallStep)>& step) final {
    Result result;
    const auto app = id.steam_app();
    if (!app) {
      result.failure = "This game can't be downloaded.";
      return result;
    }
    const auto session = sessions_.Load();
    if (!session.ok() || !session.value()) {
      result.retry_later = true;
      result.waiting = kWaitingForSignIn;
      return result;
    }
    result = InstallApp(*session.value(), *app, root, cancel, progress, step);
    if (result.retry_later && result.waiting.empty()) result.waiting = kWaitingForSteam;
    return result;
  }

 protected:
  virtual Result InstallApp(const StoredSession& session, std::uint32_t app, const std::string& root,
                            const std::atomic<bool>& cancel,
                            const std::function<void(std::uint64_t, std::uint64_t, std::uint64_t)>& progress,
                            const std::function<void(InstallStep)>& step) = 0;

 private:
  SessionRepository& sessions_;
};

}  // namespace playden::steam
