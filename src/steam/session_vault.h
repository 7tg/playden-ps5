#pragma once
#include "steam/secret.h"
#include "steam/session_store.h"
#include <mutex>
#include <optional>

namespace playden {
// "Stay signed in", shared by every LocalSessionRepository of a run. Off, the Steam session lives
// here, in memory, and never on disk. The repositories read, write and delete session.v1 under the
// vault's lock, so a change of the setting can't race a sign-in that is being saved.
class SessionVault {
 public:
  explicit SessionVault(bool persist = true) : persist_(persist) {}
  ~SessionVault() { Drop(); }
  SessionVault(const SessionVault&) = delete;
  SessionVault& operator=(const SessionVault&) = delete;
  bool persist() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return persist_;
  }

 private:
  friend class LocalSessionRepository;
  // The caller holds mutex_.
  void Keep(const StoredSession& session) {
    Drop();
    session_ = session;
  }
  void Drop() {
    if (!session_) return;
    steam::ZeroizeSecret(session_->refresh_token);
    steam::ZeroizeSecret(session_->access_token);
    session_.reset();
  }
  mutable std::mutex mutex_;
  bool persist_;
  std::optional<StoredSession> session_;
};
}
