#pragma once
#include "library/store.h"

#include <cstdint>

namespace playden::stores {

// One store's account: it restores the saved sign-in at startup and forgets it on sign-out. How a
// player signs in is the store's own (Steam: QR code or account name and password, see SteamAccount).
// Results come back on the store's worker and are read by the controller.
class StoreAccount {
 public:
  virtual ~StoreAccount() = default;
  virtual Store store() const = 0;
  // Starts restoring the saved sign-in; the request it answers, 0 when refused.
  virtual std::uint64_t RestoreSession() = 0;
  virtual void Cancel(std::uint64_t request) = 0;
  // Forgets the saved sign-in.
  virtual void SignOut() = 0;
};

}  // namespace playden::stores
