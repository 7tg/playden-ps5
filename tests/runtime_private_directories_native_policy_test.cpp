#include "runtime/private_directories.h"
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

// Reproduce the native contract: descriptor-relative openat is unavailable.
extern "C" int openat(int, const char*, int, ...) { errno = EINVAL; return -1; }
extern "C" int mkdirat(int, const char*, mode_t) { errno = EINVAL; return -1; }

namespace fs = std::filesystem;

// Folders created above the private root are shared (0777) whatever the umask;
// existing ones keep their mode, and the private tree stays 0700.
void CreatedPublicAncestorsAreShared(const fs::path& boundary) {
  const auto mode = [](const fs::path& path) {
    struct stat info{};
    assert(stat(path.c_str(), &info) == 0);
    return info.st_mode & 07777;
  };
  const auto previous = umask(022);
  std::string error;
  const auto root = boundary / "shared/parent/install";
  assert(playden::runtime::EnsurePrivateDirectories(boundary, root, root / "child", &error));
  assert(mode(boundary / "shared") == 0777 && mode(boundary / "shared/parent") == 0777);
  assert(mode(root) == 0700 && mode(root / "child") == 0700);
  assert(fs::create_directory(boundary / "existing") && chmod((boundary / "existing").c_str(), 0755) == 0);
  const auto kept = boundary / "existing/new/install";
  assert(playden::runtime::EnsurePrivateDirectories(boundary, kept, kept, &error));
  assert(mode(boundary / "existing") == 0755 && mode(boundary / "existing/new") == 0777 && mode(kept) == 0700);
  // Opening again finds the recorded modes unchanged.
  assert(playden::runtime::EnsurePrivateDirectories(boundary, kept, kept / "child", &error));
  umask(previous);
}

int main() {
  char scratch[] = "/tmp/playden-private-policy-XXXXXX";
  const char* made = mkdtemp(scratch);
  assert(made);
  const auto boundary = fs::canonical(made);
  const auto root = boundary / "install";
  const auto bottle = root / "library/prefixes/game";
  std::string error;
  assert(playden::runtime::EnsurePrivateDirectories(boundary, root, bottle, &error));
  assert(playden::runtime::IsPrivateDirectoryTree(root, bottle));
  assert(playden::runtime::IsPrivateDirectory(bottle));
  assert(chmod((root / "library").c_str(), 0755) == 0);
  assert(!playden::runtime::IsPrivateDirectoryTree(root, bottle));
  assert(!playden::runtime::EnsurePrivateDirectories(boundary, root, bottle, &error));
  assert(chmod((root / "library").c_str(), 0700) == 0);
  fs::create_directory_symlink(boundary, root / "escape");
  assert(!playden::runtime::EnsurePrivateDirectories(boundary, root, root / "escape/child", &error));
  assert(!fs::exists(boundary / "child"));
  assert(!playden::runtime::EnsurePrivateDirectories(boundary, root, root / "../outside", &error));
  CreatedPublicAncestorsAreShared(boundary);
  fs::remove_all(boundary);
  std::cout << "private directory callers pass with native relative operations unavailable\n";
}
