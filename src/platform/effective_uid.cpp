// The shared credential lock is also used by storage outside the runtime worker.
#include "platform/ps5/title_allow_list.h"
#include <mutex>
#include <unistd.h>

namespace playden::ps5 {

namespace {
std::recursive_mutex& RootLock() {
  static std::recursive_mutex lock;
  return lock;
}
}  // namespace

bool WithRoot(const std::function<bool()>& work) {
  std::lock_guard<std::recursive_mutex> lock(RootLock());
  const uid_t user = ::geteuid();
  if (user == 0) return work();
  if (::seteuid(0) != 0) return false;
  const bool ok = work();
  return ::seteuid(user) == 0 && ok;
}

std::shared_ptr<void> HoldEffectiveUid() {
  return std::make_shared<std::unique_lock<std::recursive_mutex>>(RootLock());
}

std::shared_ptr<void> TryHoldEffectiveUid() {
  auto lock = std::make_shared<std::unique_lock<std::recursive_mutex>>(RootLock(), std::try_to_lock);
  if (!lock->owns_lock()) return nullptr;
  return lock;
}

}  // namespace playden::ps5
