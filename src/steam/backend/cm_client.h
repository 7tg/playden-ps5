#pragma once

#include "platform/storage.h"
#include "steam/backend/cm_messages.h"
#include "steam/backend/http_client.h"
#include "steam/backend/ws_client.h"
#include "steam/session_store.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
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

 private:
  class Session;
  CmClientHooks hooks_;
  std::string ca_bundle_;
  const Storage* device_storage_ = nullptr;
  std::optional<cm::DeviceIdentity> device_;
  std::unique_ptr<HttpClient> http_;
};

}  // namespace playden::steam
