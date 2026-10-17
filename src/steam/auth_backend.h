#pragma once

#include "steam/secret.h"
#include "steam/session_store.h"
#include <atomic>
#include <functional>
#include <optional>
#include <string>

namespace playden::steam {

enum class AuthError {
  kNone, kCanceled, kExpired, kRejected, kNetwork, kProtocol,
  kCredentialsRevoked, kPersistence, kNoSession,
  // Account name and password sign-in only.
  kInvalidPassword,  // EResult 5: Steam does not say whether the name or the password was wrong
  kRateLimited,      // EResult 84: too many attempts
  kSignInRefused,    // any other refusal; the QR code may still work
  kSignInTimedOut
};

struct AuthResult {
  AuthError error = AuthError::kNone;
  std::optional<StoredSession> session;
};

// These callbacks run on the service worker. Challenge URLs are private auth
// presentation data; neither they nor credentials belong in diagnostics.
using ChallengeCallback = std::function<void(const std::string&)>;

// Steam Guard follow-ups of an account name and password sign-in.
enum class GuardPromptKind {
  kApproval,           // approve in the Steam Mobile app (or, with email_domain, from Steam's email)
  kEmailCode,          // type the code Steam emailed to …@email_domain
  kAuthenticatorCode,  // type the code from the Steam Mobile app
};
struct GuardPrompt {
  GuardPromptKind kind = GuardPromptKind::kApproval;
  std::string email_domain;
  bool code_allowed = false;   // kApproval: a typed code works too (code_kind says which)
  GuardPromptKind code_kind = GuardPromptKind::kAuthenticatorCode;
  bool code_rejected = false;  // the last code was wrong; the same sign-in waits for another
};
using GuardPromptCallback = std::function<void(const GuardPrompt&)>;
// Returns the code the player submitted since the last call, if any. Called on the service worker.
using GuardCodeSource = std::function<std::optional<std::string>()>;

// The password is cleared (overwritten) as soon as it is encrypted, and on every failure.
struct Credentials {
  std::string account_name;
  std::string password;
};

class AuthBackend {
 public:
  virtual ~AuthBackend() = default;
  virtual AuthResult AuthenticateQr(const std::atomic<bool>& cancel,
                                    const ChallengeCallback& challenge) = 0;
  virtual AuthResult Restore(const StoredSession& session,
                             const std::atomic<bool>& cancel) = 0;
  // Backends without password sign-in refuse it.
  virtual AuthResult AuthenticateCredentials(Credentials& credentials, const std::atomic<bool>& cancel,
                                             const GuardPromptCallback& prompt, const GuardCodeSource& code);
};

inline AuthResult AuthBackend::AuthenticateCredentials(Credentials& credentials, const std::atomic<bool>&,
                                                       const GuardPromptCallback&, const GuardCodeSource&) {
  ZeroizeSecret(credentials.password);
  return {AuthError::kSignInRefused, std::nullopt};
}

}  // namespace playden::steam
