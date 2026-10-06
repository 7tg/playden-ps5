#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace playden::platform {

enum class DirectoryBackend { kDescriptorRelative, kAbsolutePath };
enum class DirectoryPolicy { kApprovedTree, kPrivate };

// Operations assume a stable managed namespace and coordinated writers. The
// absolute-path backend checks components and observed identities, but is NOT a
// race-proof boundary against concurrent ancestor replacement. A failed
// post-operation check can mean effects occurred. Never retry cleanup through
// that context without reconciling its identity. No process CWD is changed.
//
// kPrivate requires current-EUID ownership and mode 0700 from the selected root
// downward; ancestors above that root may be public. kApprovedTree accepts
// existing directory ownership/modes, then refuses identity/owner/mode drift.
// Both reject symlinks in every path component. Caller owns root authorization,
// stopped-title checks, journals, file-content validation and collision policy.
class ManagedDirectory {
 public:
  static DirectoryBackend DefaultBackend();
  static std::optional<ManagedDirectory> Open(
      const std::filesystem::path& absolute_path, DirectoryPolicy policy,
      DirectoryBackend backend = DefaultBackend());

  ManagedDirectory(ManagedDirectory&& other) noexcept;
  ManagedDirectory& operator=(ManagedDirectory&& other) noexcept;
  ~ManagedDirectory();
  ManagedDirectory(const ManagedDirectory&) = delete;
  ManagedDirectory& operator=(const ManagedDirectory&) = delete;

  std::optional<ManagedDirectory> OpenChild(const std::string& name,
                                           DirectoryPolicy policy,
                                           bool create = false) const;
  // Open/OpenChild never sync implicitly. After creation the caller must sync
  // the new directory and its parent before recording durable completion.
  bool Validate() const;
  // Returned file descriptors belong to the caller. Reads are nonblocking so
  // a FIFO cannot hang admission. expected_mode compares all permission bits.
  int OpenRegularFile(const std::string& name, bool require_owner = false,
                      std::optional<mode_t> expected_mode = std::nullopt) const;
  // Sets/verifies mode and ownership on the new file before returning it. No
  // payload has been written. Failure can leave an empty file for reconciliation.
  int CreateExclusiveFile(const std::string& name, mode_t mode) const;
  // Missing is false/ENOENT; only ordinary files/directories are admitted.
  bool InspectChild(const std::string& name, struct stat* result) const;
  // Caller admits source/destination content and chooses replacement. Existing
  // directories are never replaced. Source and existing destination must be
  // owned by the captured EUID. The caller must sync both parents afterward.
  // replace_existing=false is a checked absence policy, not an atomic
  // no-replace primitive against concurrent writers.
  bool RenameChild(const std::string& from, const ManagedDirectory& destination,
                   const std::string& to, bool replace_existing = true) const;
  bool RemoveRegularFile(const std::string& name) const;
  bool RemoveEmptyDirectory(const std::string& name) const;
  bool Sync() const;

  int fd() const { return fd_; }  // Borrowed, for fsync/fstat only; do not close.
  const std::filesystem::path& path() const { return path_; }
  const std::filesystem::path& root() const { return root_; }
  DirectoryPolicy policy() const { return policy_; }
  DirectoryBackend backend() const { return backend_; }

 private:
  struct Observation {
    std::filesystem::path path;
    struct stat identity;
    bool private_directory = false;
  };
  ManagedDirectory() = default;
  bool ValidName(const std::string& name) const;
  bool Inspect(const std::string& name, struct stat* result) const;
  int OpenFile(const std::string& name, int flags, mode_t mode) const;
  bool Remove(const std::string& name, bool directory) const;
  int fd_ = -1;
  std::filesystem::path root_, path_;
  DirectoryPolicy policy_ = DirectoryPolicy::kApprovedTree;
  DirectoryBackend backend_ = DirectoryBackend::kAbsolutePath;
  uid_t uid_ = 0;
  std::vector<Observation> observations_;
};

}  // namespace playden::platform
