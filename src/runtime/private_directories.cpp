#include "runtime/private_directories.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace playden::runtime {
namespace fs = std::filesystem;
namespace {
bool SafeRelative(const fs::path& path) {
  if (path.empty() || path.is_absolute()) return false;
  for (const auto& part : path)
    if (part.empty() || part == "." || part == "..") return false;
  return true;
}
bool Private(const struct stat& info) {
  return S_ISDIR(info.st_mode) && info.st_uid == geteuid() &&
      (info.st_mode & 07777) == 0700;
}
}
bool IsPrivateDirectory(const fs::path& directory) {
  const int fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (fd < 0) return false;
  struct stat info{};
  const bool ok = fstat(fd, &info) == 0 && Private(info);
  close(fd); return ok;
}
bool IsPrivateDirectoryTree(const fs::path& private_root, const fs::path& directory) {
  const auto relative = directory.lexically_relative(private_root);
  if (directory != private_root && !SafeRelative(relative)) return false;
  int fd = open(private_root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (fd < 0) return false;
  struct stat info{};
  bool ok = fstat(fd, &info) == 0 && Private(info);
  if (directory != private_root) {
    for (const auto& part : relative) {
      if (!ok) break;
      const int next = openat(fd, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
      close(fd); fd = next;
      ok = fd >= 0 && fstat(fd, &info) == 0 && Private(info);
    }
  }
  if (fd >= 0) close(fd);
  return ok;
}
bool EnsurePrivateDirectories(const fs::path& boundary, const fs::path& private_root,
                              const fs::path& directory, std::string* error) {
  if (error) error->clear();
  const auto fail = [&](const std::string& message) {
    if (error) *error = message;
    return false;
  };
  const auto relative = directory.lexically_relative(boundary);
  const auto private_relative = private_root.lexically_relative(boundary);
  const auto within = directory.lexically_relative(private_root);
  if (!SafeRelative(relative) || !SafeRelative(private_relative) ||
      (directory != private_root && !SafeRelative(within)))
    return fail("private directory is outside its trusted boundary");
  int fd = open(boundary.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (fd < 0) return fail("cannot open directory boundary: " + std::string(strerror(errno)));
  fs::path current = boundary;
  bool in_private = false;
  for (const auto& part : relative) {
    current /= part;
    if (current == private_root) in_private = true;
    int next = openat(fd, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (next < 0 && errno == ENOENT) {
      if (mkdirat(fd, part.c_str(), 0700) != 0) {
        const int saved = errno; close(fd);
        return fail("cannot create " + current.string() + ": " + strerror(saved));
      }
      next = openat(fd, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    }
    if (next < 0) {
      const int saved = errno; close(fd);
      return fail("cannot open " + current.string() + ": " + strerror(saved));
    }
    struct stat info{};
    if (fstat(next, &info) != 0 || (in_private && !Private(info))) {
      close(next); close(fd);
      return fail("private directory has wrong owner or permissions: " + current.string());
    }
    close(fd); fd = next;
  }
  close(fd); return true;
}
}
