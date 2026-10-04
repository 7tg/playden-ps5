#pragma once

#include "steam/auth_backend.h"
#include "steam/backend/http_client.h"
#include <functional>
#include <memory>

namespace playden::steam {

// Deterministic protocol tests inject transport and clocks. Production uses
// verified HttpClient, steady_clock and cancel-aware waits. These callbacks must
// be bounded and run only on the owning service worker.
struct AuthClientHooks {
  std::function<HttpResponse(const HttpRequest&, const std::atomic<bool>&)> perform;
  std::function<std::uint64_t()> monotonic_ms;
  std::function<std::uint64_t()> unix_seconds;
  std::function<bool(std::uint32_t, const std::atomic<bool>&)> wait;
};

class AuthClient final : public AuthBackend {
 public:
  // No network initialization occurs until a service-worker call.
  explicit AuthClient(std::string ca_bundle);
  explicit AuthClient(AuthClientHooks hooks);
  AuthResult AuthenticateQr(const std::atomic<bool>& cancel,
                            const ChallengeCallback& challenge) override;
  AuthResult Restore(const StoredSession& session,
                     const std::atomic<bool>& cancel) override;
 private:
  AuthError Post(const char* method, const std::vector<std::uint8_t>& message,
                 const std::atomic<bool>& cancel, std::uint32_t timeout_ms,
                 bool restoring, std::vector<std::uint8_t>* body);
  AuthResult Renew(const StoredSession& session, const std::atomic<bool>& cancel,
                   std::uint32_t timeout_ms, bool restoring);
  AuthClientHooks hooks_;
  std::string ca_bundle_;
  std::unique_ptr<HttpClient> http_;
};

}  // namespace playden::steam
