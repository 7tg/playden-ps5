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

// Folders created above the private root are shared (01777) whatever the umask;
// existing ones keep their mode, and the private tree stays 0700.
void CreatedPublicAncestorsAreShared(const fs::path& boundary) {
  const auto mode = [](const fs::path& path) {
    struct stat info{};
    assert(stat(path.c_str(), &info) == 0);
    return info.st_mode & 017777;
  };
  const auto previous = umask(022);
  std::string error;
  const auto root = boundary / "shared/parent/install";
  assert(playden::runtime::EnsurePrivateDirectories(boundary, root, root / "child", &error));
  assert(mode(boundary / "shared") == 01777 && mode(boundary / "shared/parent") == 01777);
  assert(mode(root) == 0700 && mode(root / "child") == 0700);
  assert(fs::create_directory(boundary / "existing") && chmod((boundary / "existing").c_str(), 0755) == 0);
  const auto kept = boundary / "existing/new/install";
  assert(playden::runtime::EnsurePrivateDirectories(boundary, kept, kept, &error));
  assert(mode(boundary / "existing") == 0755 && mode(boundary / "existing/new") == 01777 && mode(kept) == 0700);
  // Opening again finds the recorded modes unchanged.
  assert(playden::runtime::EnsurePrivateDirectories(boundary, kept, kept / "child", &error));
  umask(previous);
}

// The bottle's way to its user profile (games/<id>, library, prefixes, game) may
// be 0711; the rest of the tree, and anything not asked for, stays 0700.
void TraversalChainAdmits0711(const fs::path& boundary) {
  using playden::runtime::EnsurePrivateDirectories;
  using playden::runtime::IsPrivateDirectoryTree;
  using playden::runtime::PrivateTraversal;
  const auto mode = [](const fs::path& path) {
    struct stat info{};
    assert(stat(path.c_str(), &info) == 0);
    return info.st_mode & 017777;
  };
  const auto previous = umask(077);
  const auto game = boundary / "playden/games/install-a";
  const auto bottle = game / "library/prefixes/game";
  PrivateTraversal traversal{{game, game / "library", game / "library/prefixes", bottle}, 0711};
  std::string error;
  assert(EnsurePrivateDirectories(boundary, game, bottle / "drive_c/users", &error, traversal));
  assert(EnsurePrivateDirectories(boundary, game, game / "library/profiles", &error, traversal));
  assert(EnsurePrivateDirectories(boundary, game, game / ".playden-control", &error, traversal));
  assert(mode(boundary / "playden") == 01777 && mode(boundary / "playden/games") == 01777);
  for (const auto& folder : traversal.folders) assert(mode(folder) == 0711);
  assert(mode(bottle / "drive_c") == 0700 && mode(bottle / "drive_c/users") == 0700);
  assert(mode(game / "library/profiles") == 0700 && mode(game / ".playden-control") == 0700);
  assert(IsPrivateDirectoryTree(game, bottle / "drive_c/users", traversal));
  assert(IsPrivateDirectoryTree(game, game / ".playden-control", traversal));
  assert(playden::runtime::IsPrivateDirectory(bottle, true) && !playden::runtime::IsPrivateDirectory(bottle));
  // Without the traversal, the 0711 chain is refused as before.
  assert(!IsPrivateDirectoryTree(game, bottle));
  assert(!EnsurePrivateDirectories(boundary, game, bottle, &error));
  // 0700 still passes for a traversal folder (a bottle from an older kit).
  assert(chmod((game / "library/prefixes").c_str(), 0700) == 0);
  assert(IsPrivateDirectoryTree(game, bottle, traversal));
  // 0755 is refused on the chain; 0711 is refused off it.
  assert(chmod((game / "library").c_str(), 0755) == 0);
  assert(!IsPrivateDirectoryTree(game, bottle, traversal));
  assert(!EnsurePrivateDirectories(boundary, game, bottle, &error, traversal));
  assert(chmod((game / "library").c_str(), 0711) == 0);
  assert(chmod((game / "library/profiles").c_str(), 0711) == 0);
  assert(!IsPrivateDirectoryTree(game, game / "library/profiles", traversal));
  assert(chmod((game / "library/profiles").c_str(), 0700) == 0);
  assert(chmod((bottle / "drive_c").c_str(), 0711) == 0);
  assert(!IsPrivateDirectoryTree(game, bottle / "drive_c/users", traversal));
  // With created_mode 0700 (kit without the capability), new traversal folders are 0700.
  const auto other = boundary / "playden/games/install-b";
  const auto other_bottle = other / "library/prefixes/game";
  PrivateTraversal closed{{other, other / "library", other / "library/prefixes", other_bottle}};
  assert(EnsurePrivateDirectories(boundary, other, other_bottle, &error, closed));
  assert(mode(other) == 0700 && mode(other_bottle) == 0700);
  assert(IsPrivateDirectoryTree(other, other_bottle) && IsPrivateDirectoryTree(other, other_bottle, closed));
  closed.created_mode = 0755;
  assert(!EnsurePrivateDirectories(boundary, other, other_bottle, &error, closed));
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
  TraversalChainAdmits0711(boundary);
  fs::remove_all(boundary);
  std::cout << "private directory callers pass with native relative operations unavailable\n";
}
