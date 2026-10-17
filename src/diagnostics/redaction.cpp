#include "diagnostics/redaction.h"
namespace playden::diagnostics {
const char* AuthMessage(steam::AuthError error) {
  switch (error) {
    case steam::AuthError::kNone: return "";
    case steam::AuthError::kCanceled: return "Sign-in canceled.";
    case steam::AuthError::kExpired: return "The QR code expired. Request a new code.";
    case steam::AuthError::kRejected: return "Steam did not approve sign-in. Try again.";
    case steam::AuthError::kNetwork: return "Steam could not be reached. Check the connection and retry.";
    case steam::AuthError::kProtocol: return "Steam returned an invalid response. Try again.";
    case steam::AuthError::kCredentialsRevoked: return "The saved Steam session is no longer valid. Sign in again.";
    case steam::AuthError::kPersistence: return "Account storage is unavailable. Retry when storage is available.";
    case steam::AuthError::kNoSession: return "Sign in to Steam to connect your library.";
    case steam::AuthError::kInvalidPassword:
      return "Steam did not accept the account name or password. Check them and try again.";
    case steam::AuthError::kRateLimited:
      return "Steam is limiting sign-in attempts. Wait a while before you try again, or use the QR code.";
    case steam::AuthError::kSignInRefused: return "Steam did not allow this sign-in. Try the QR code instead.";
    case steam::AuthError::kSignInTimedOut: return "The sign-in timed out. Try again.";
  }
  return "Steam sign-in failed.";
}
std::string AuthRecord(const steam::SteamEvent& event) {
  const char* kind = "unknown";
  switch (event.kind) {
    case steam::SteamEventKind::kChallenge: kind="challenge"; break;
    case steam::SteamEventKind::kAwaitingApproval: kind="awaiting-approval"; break;
    case steam::SteamEventKind::kNeedsEmailCode: kind="needs-email-code"; break;
    case steam::SteamEventKind::kNeedsAuthenticatorCode: kind="needs-authenticator-code"; break;
    case steam::SteamEventKind::kCodeRejected: kind="code-rejected"; break;
    case steam::SteamEventKind::kAuthenticated: kind="authenticated"; break;
    case steam::SteamEventKind::kOfflineSession: kind="offline-session"; break;
    case steam::SteamEventKind::kSignedOut: kind="signed-out"; break;
    case steam::SteamEventKind::kCanceled: kind="canceled"; break;
    case steam::SteamEventKind::kFailure: kind="failure"; break;
  }
  return std::string("{\"kind\":\"") + kind + "\",\"request\":" + std::to_string(event.request_id) +
      ",\"generation\":" + std::to_string(event.generation) + ",\"error\":\"" + AuthMessage(event.error) + "\"}";
}
}
