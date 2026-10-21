#pragma once
#include "steam/session_store.h"
#include "steam/session_vault.h"
#include <functional>

namespace playden {
// Lazy filesystem preparation runs only on the calling service worker. The
// configured root's parent must already exist; failures never enable fixtures.
// With a vault, "Stay signed in" decides where the session lives: on, in
// session.v1 as before; off, only in the vault, and session.v1 is deleted.
// Without one, the session is always saved.
class LocalSessionRepository final : public SessionRepository {
 public:
  using DiagnosticReport = std::function<void(const std::string&)>;
  // Reports operation/error metadata only; never session bytes or account data.
  explicit LocalSessionRepository(std::string root, DiagnosticReport report = {}, SessionVault* vault = nullptr);
  Result<std::optional<StoredSession>> Load() const override;
  Result<void> Save(const StoredSession& session) const override;
  Result<void> Clear() const override;
  // The player changed "Stay signed in". Off moves a saved session into the vault and deletes
  // session.v1; on saves the session the vault holds. The setting changes even if the disk fails.
  Result<void> SetPersist(bool persist) const;
 private:
  Result<void> Prepare() const;
  Result<std::optional<StoredSession>> LoadSaved() const;
  Result<void> SaveToDisk(const StoredSession& session) const;
  Result<void> ClearSaved() const;
  std::string root_;
  mutable Storage storage_;
  SessionStore store_;
  mutable bool prepared_ = false;
  DiagnosticReport report_;
  SessionVault* vault_;
};
}
