#pragma once
// Descriptor walks used by permission repair; never fall back to privileged path operations.
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#if defined(__PROSPERO__) || (defined(PLAYDEN_PLATFORM_PS5) && PLAYDEN_PLATFORM_PS5)
extern "C" int __wrap_openat(int, const char*, int, ...);
extern "C" int __wrap_mkdirat(int, const char*, mode_t);
#endif

namespace playden::platform {
inline int OpenWalkChild(int parent, const char* name, int flags) {
#if defined(__PROSPERO__) || (defined(PLAYDEN_PLATFORM_PS5) && PLAYDEN_PLATFORM_PS5)
  return __wrap_openat(parent, name, flags);
#else
  return ::openat(parent, name, flags);
#endif
}
inline int MakeWalkChild(int parent, const char* name, mode_t mode) {
#if defined(__PROSPERO__) || (defined(PLAYDEN_PLATFORM_PS5) && PLAYDEN_PLATFORM_PS5)
  return __wrap_mkdirat(parent, name, mode);
#else
  return ::mkdirat(parent, name, mode);
#endif
}
// Starts at the filesystem root, opening each component without following links.
inline int OpenDirectoryWalk(const std::filesystem::path& path) {
  if (!path.is_absolute() || path.string().find('\0') != std::string::npos) { errno = EINVAL; return -1; }
  int fd = ::open("/", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  for (const auto& part : path.relative_path()) {
    if (part.empty() || part == "." || part == "..") { ::close(fd); errno = EINVAL; return -1; }
    if (fd < 0) return -1;
    const int child = OpenWalkChild(fd, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    const int error = errno;
    ::close(fd);
    fd = child;
    errno = error;
  }
  return fd;
}
}  // namespace playden::platform
