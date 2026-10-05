#pragma once

#include "platform/storage.h"
#include "steam/backend/cm_messages.h"
#include "steam/backend/http_client.h"
#include "steam/backend/ws_client.h"
#include "steam/auth_backend.h"
#include "steam/session_store.h"

#include <array>
#include <atomic>
#include <chrono>
#include <optional>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <string_view>

namespace playden::steam {

enum class CmError {
  kNone,
  kCanceled,
  kNoSession,
  kNetwork,
  kRejected,  // logon refused; never treated as revoked credentials (the reference ignores it)
  kProtocol,
  kTimeout,
};

const char* CmErrorName(CmError error);

struct AcquisitionResult {
  CmError error = CmError::kProtocol;
  // App ID -> earliest acquisition (unix seconds) among this account's active licenses.
  std::map<std::uint32_t, std::uint32_t> acquired_at;
  // Apps and depots granted by those licenses' packages, for depot selection.
  std::set<std::uint32_t> owned_apps;
  std::set<std::uint32_t> owned_depots;
  bool ok() const { return error == CmError::kNone; }
};

struct TokenRenewal {
  CmError error = CmError::kProtocol;
  std::string access_token;
  std::optional<std::string> refresh_token;  // present only when Steam rotated it
  bool ok() const { return error == CmError::kNone; }
};

inline constexpr std::size_t kMaxAppInfoBatch = 50;

struct AppInfoBuffers {
  CmError error = CmError::kProtocol;
  std::map<std::uint32_t, std::string> buffers;  // app ID -> VDF text (trailing NULs removed)
  bool ok() const { return error == CmError::kNone; }
};

struct DepotRequest {
  std::uint32_t depot_id = 0;
  std::uint64_t manifest_gid = 0;
};
struct DepotGrant {
  std::uint32_t depot_id = 0;
  std::uint64_t manifest_gid = 0;
  std::array<std::uint8_t, 32> key{};
  std::uint64_t manifest_request_code = 0;  // short-lived; fetch the manifest soon after
};
struct DepotAccessResult {
  CmError error = CmError::kProtocol;
  std::vector<DepotGrant> grants;  // one per requested depot, in request order
  std::string detail;              // which step refused, with Steam's result code
  bool ok() const { return error == CmError::kNone; }
};

// The CM-only library facts. Implementations run synchronously on the caller's worker thread.
class AcquisitionSource {
 public:
  virtual ~AcquisitionSource() = default;
  virtual AcquisitionResult FetchAcquisitionDates(const StoredSession& session,
                                                  const std::atomic<bool>& cancel) = 0;
};

// Derives the stable logon device fields from a per-install seed, as the reference
// CMDeviceIdentity: SHA-256 login ID and SHA-1 hashed hardware keys. No hardware ID is read.
cm::DeviceIdentity MakeDeviceIdentity(std::string_view seed);
// Loads the per-install seed from storage, creating it on first use. A failed save still yields a
// seed that is stable for this run.
std::string LoadOrCreateDeviceSeed(const Storage& storage);

struct CmClientHooks {
  std::function<HttpResponse(const HttpRequest&, const std::atomic<bool>&)> directory;
  std::function<std::unique_ptr<MessageTransport>()> transport;
  std::function<std::uint64_t()> unix_now;
  std::uint32_t deadline_ms = 25'000;
};

// One CM session per call: directory, connect (first four endpoints), hello, logon with the
// stored refresh token, the pushed license list, then package PICS in batches of 50. All output is
// error categories and app dates; no endpoint, address, token or account identity is reported.
class CmClient final : public AcquisitionSource {
 public:
  CmClient(std::string ca_bundle, const Storage& device_storage);
  CmClient(CmClientHooks hooks, cm::DeviceIdentity device);
  ~CmClient() override;

  AcquisitionResult FetchAcquisitionDates(const StoredSession& session,
                                          const std::atomic<bool>& cancel) override;

  // Renews the access token through the authenticated CM session
  // (Authentication.GenerateAccessTokenForApp#1), which accepts SteamClient refresh tokens that
  // the Web API refuses. A refused CM logon reports kRejected.
  TokenRenewal RenewAccessToken(const StoredSession& session, const std::atomic<bool>& cancel);

  // PICS app info buffers (text VDF) for up to kMaxAppInfoBatch apps, using app access tokens
  // where Steam grants them. Apps Steam does not describe are absent from the result.
  // Depot decryption keys and public-branch manifest request codes for one app's depots.
  // Any refused depot fails the whole call (kRejected): a partial install is never planned.
  DepotAccessResult FetchDepotAccess(const StoredSession& session, std::uint32_t app_id,
                                     const std::vector<DepotRequest>& depots,
                                     const std::atomic<bool>& cancel);

  AppInfoBuffers FetchAppInfo(const StoredSession& session, const std::vector<std::uint32_t>& app_ids,
                              const std::atomic<bool>& cancel);

  // A unified service call on an open session. When Steam refuses it, the result is kRejected and
  // `eresult` (optional) holds Steam's EResult.
  using ServiceCall = std::function<CmError(const std::string& method, const std::vector<std::uint8_t>& body,
                                            std::vector<std::uint8_t>* reply, int* eresult)>;
  // Opens one logged-on session that stays usable for `deadline_ms` and runs `work` with a service
  // call function and the logged-on SteamID. The session closes when `work` returns; the result is
  // the logon failure or whatever `work` returns.
  CmError WithServiceSession(const StoredSession& session, std::uint32_t deadline_ms, const std::atomic<bool>& cancel,
                             const std::function<CmError(const ServiceCall& call, std::uint64_t steam_id)>& work);

 private:
  class Session;
  // Directory lookup, connect, hello and logon. On success the transport is connected and the
  // session logged on.
  CmError Open(const StoredSession& session, const std::atomic<bool>& cancel,
               std::chrono::steady_clock::time_point deadline,
               std::unique_ptr<MessageTransport>* transport, std::unique_ptr<Session>* opened);
  CmClientHooks hooks_;
  std::string ca_bundle_;
  const Storage* device_storage_ = nullptr;
  std::optional<cm::DeviceIdentity> device_;
  std::unique_ptr<HttpClient> http_;
};

// Adapts CM renewal to the auth worker: a refused CM logon means the credentials are gone; network
// trouble stays a network error so cache-only startup still applies.
AuthResult RenewThroughCm(CmClient& client, const StoredSession& session,
                          const std::atomic<bool>& cancel);

}  // namespace playden::steam
