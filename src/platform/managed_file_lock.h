#pragma once

#include "platform/managed_directory.h"

namespace playden::platform {

enum class LockMode { kReader, kPublisher };

// Cooperative publication lease. The caller creates/syncs the private lock file
// before use and never renames, unlinks or truncates it. Admission checks its
// identity around nonblocking acquisition; this inherits ManagedDirectory's
// stable-namespace assumption, not protection against adversarial replacement.
class ManagedFileLock {
 public:
  static std::optional<ManagedFileLock> Acquire(
      const ManagedDirectory& directory, const std::string& filename, LockMode mode);
  ManagedFileLock(ManagedFileLock&& other) noexcept;
  ManagedFileLock& operator=(ManagedFileLock&& other) noexcept;
  ~ManagedFileLock();
  ManagedFileLock(const ManagedFileLock&) = delete;
  ManagedFileLock& operator=(const ManagedFileLock&) = delete;

  // Borrowed by the locked semantic verifier; callers never close or convert it.
  int fd() const { return descriptor_; }
  bool Validate(const ManagedDirectory& directory, const std::string& filename) const;

 private:
  explicit ManagedFileLock(int descriptor) : descriptor_(descriptor) {}
  int descriptor_ = -1;
};

}  // namespace playden::platform
