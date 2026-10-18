#pragma once

#include "steam/auth_backend.h"
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace playden::steam {

using RequestId = std::uint64_t;
enum class SteamEventKind {
  kChallenge,
  kAwaitingApproval,  // QR: the code is shown; password: approve in the Steam app (code_allowed: or type a code)
  kNeedsEmailCode,
  kNeedsAuthenticatorCode,
  kCodeRejected,      // the typed code was wrong; the same sign-in waits for another (code_is_email says which)
  kAuthenticated,
  kOfflineSession,
  kSignedOut,
  kCanceled,
  kFailure
};
struct SteamEvent {
  SteamEventKind kind = SteamEventKind::kFailure;
  RequestId request_id = 0;
  std::uint64_t generation = 0;
  AuthError error = AuthError::kNone;
  std::string challenge_url;
  std::uint64_t steam_id = 0;
  std::string account_name;
  // Password sign-in's Steam Guard prompts.
  std::string email_domain;
  bool code_allowed = false;
  bool code_is_email = false;
};
// The event's name in auth.log ("awaiting-approval", "authenticated", ...).
const char* EventName(SteamEventKind kind);

// Commands enqueue work and invalidate older results without doing storage or
// network I/O on the caller. Backend and repository must outlive this service.
// SignedOut is emitted only after Clear succeeds; failure is explicit.
class SteamService {
 public:
  // log: optional one-line report of each sign-in step and result (auth.log). It names the step,
  // the event and the error only: never tokens, account names or email addresses.
  SteamService(AuthBackend& backend, SessionRepository& repository,
               std::function<void(const std::string&)> log = {});
  ~SteamService();
  SteamService(const SteamService&) = delete;
  SteamService& operator=(const SteamService&) = delete;
  RequestId BeginQrSignIn();
  // The password is moved in and overwritten once the worker has encrypted it, or when the sign-in is
  // replaced or canceled first.
  RequestId BeginCredentialSignIn(std::string account_name, std::string password);
  // A Steam Guard code for the current password sign-in; ignored when none is running.
  void SubmitGuardCode(std::string code);
  RequestId RestoreSession();
  void Cancel(RequestId request);
  void SignOut();
  std::vector<SteamEvent> Pump(std::size_t max_events);

 private:
  enum class Operation { kBegin, kRestore, kClear, kCredentials };
  struct Job {
    Operation operation;
    RequestId request;
    std::uint64_t generation;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::shared_ptr<Credentials> credentials;  // kCredentials; wiped when the last copy goes
  };
  RequestId Submit(Operation operation, std::shared_ptr<Credentials> credentials = nullptr);
  bool Current(const Job& job) const;
  void Emit(const Job& job, SteamEvent event);
  void Run();
  void Execute(const Job& job);
  void Log(const Job& job, const std::string& line) const;
  AuthBackend& backend_;
  SessionRepository& repository_;
  std::function<void(const std::string&)> log_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<Job> pending_;
  std::shared_ptr<std::atomic<bool>> cancel_;
  std::deque<SteamEvent> events_;
  std::optional<std::string> guard_code_;  // for the current request only
  RequestId request_ = 0;
  std::uint64_t generation_ = 0;
  bool stopping_ = false;
  bool active_ = false;
  bool restore_blocked_ = false;
  // Worker-owned: retry deletion of credentials already rejected by Steam.
  bool revocation_cleanup_pending_ = false;
  std::thread worker_;
};

}  // namespace playden::steam
