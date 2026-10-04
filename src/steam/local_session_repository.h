#pragma once
#include "steam/session_store.h"

namespace playden {
// Lazy filesystem preparation runs only on the calling service worker. The
// configured root's parent must already exist; failures never enable fixtures.
class LocalSessionRepository final : public SessionRepository {
 public:
  explicit LocalSessionRepository(std::string root);
  Result<std::optional<StoredSession>> Load() const override;
  Result<void> Save(const StoredSession& session) const override;
  Result<void> Clear() const override;
 private:
  Result<void> Prepare() const;
  std::string root_;
  mutable Storage storage_;
  SessionStore store_;
  mutable bool prepared_ = false;
};
}
