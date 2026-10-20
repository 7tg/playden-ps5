#include "runtime/writable_folders.h"

#include <cstdio>
#include <cerrno>
#if PLAYDEN_PLATFORM_PS5
extern int playden_test_gateway_error;
#endif
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace playden::runtime;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

namespace fs = std::filesystem;
namespace {
unsigned Mode(const fs::path& path) {
  struct stat info{};
  CHECK(lstat(path.c_str(), &info) == 0);
  return info.st_mode & 07777;
}
bool Empty(const fs::path& path) { return fs::is_empty(path); }
}  // namespace

int main() {
  char name[] = "/tmp/playden-writable-XXXXXX";
  CHECK(mkdtemp(name));
  const fs::path root = fs::canonical(name);
  const auto open = root / "open", locked = root / "data/playden-games", target = root / "target", link = root / "link";
  fs::create_directories(open);
  fs::create_directories(locked);
  fs::create_directories(target);
  fs::create_directory_symlink(target, link);
  std::vector<std::string> lines;
  const auto log = [&lines](const std::string& line) { lines.push_back(line); };
  unsigned calls = 0;
  const WritableFoldersAsRoot runs = [&calls](const std::function<bool()>& work) { ++calls; return work(); };
  const WritableFoldersAsRoot refuses = [&calls](const std::function<bool()>&) { ++calls; return false; };

  // Writable, missing and linked folders need nothing; the probe leaves no file behind.
  CHECK(EnsureWritableFolders({open, root / "missing", link, {}}, runs, log, root).empty());
  CHECK(calls == 0 && lines.empty() && Empty(open));

  if (geteuid() != 0) {
    // A linked folder is never followed (nor fixed), even when its target can't be written.
    CHECK(chmod(target.c_str(), 0555) == 0);
    CHECK(EnsureWritableFolders({link}, runs, log, root).empty());
    CHECK(calls == 0 && Mode(target) == 0555);
    CHECK(chmod(target.c_str(), 0755) == 0);

    // O_NOFOLLOW on the final folder must also reject linked ancestors.
    fs::create_directories(root / "mnt/usb1");
    fs::create_directory_symlink(target, root / "mnt/usb1/playden");
    fs::create_directories(target / "games");
    CHECK(chmod((target / "games").c_str(), 0555) == 0);
    CHECK(!EnsureWritableFolders({root / "mnt/usb1/playden/games"}, runs, log, root).empty());
    CHECK(Mode(target / "games") == 0555);
    CHECK(chmod((target / "games").c_str(), 0755) == 0);
    lines.clear();
    calls = 0;

    // A link swapped in between the probe and the fix is refused, its target left alone.
    const auto swapped = root / "mnt/usb0/playden/games";
    fs::create_directories(swapped);
    CHECK(chmod(swapped.c_str(), 0555) == 0 && chmod(target.c_str(), 0555) == 0);
    const WritableFoldersAsRoot swaps = [&](const std::function<bool()>& work) {
      ++calls;
      CHECK(chmod(swapped.c_str(), 0755) == 0);
      fs::rename(swapped, root / "swapped-away");
      CHECK(chmod((root / "swapped-away").c_str(), 0555) == 0);
      fs::create_directory_symlink(target, swapped);
      return work();
    };
    CHECK(!EnsureWritableFolders({swapped}, swaps, log, root).empty());
    CHECK(calls == 1 && Mode(target) == 0555 && Mode(root / "swapped-away") == 0555);
    CHECK(lines.size() == 1 && lines[0].find("chmod=") != std::string::npos);
    CHECK(chmod(target.c_str(), 0755) == 0 && chmod((root / "swapped-away").c_str(), 0755) == 0);
    lines.clear();
    calls = 0;

    // No way to raise root: the folder is named.
    CHECK(chmod(locked.c_str(), 0555) == 0);
    auto message = EnsureWritableFolders({open, locked}, {}, log, root);
    CHECK(message.find("can't write to " + locked.string() + ".") != std::string::npos);
    CHECK(Mode(locked) == 0555 && lines.size() == 1 && lines[0].find("fix=unavailable") != std::string::npos);

    // Root refused.
    lines.clear();
    message = EnsureWritableFolders({locked}, refuses, log, root);
    CHECK(calls == 1 && message.find(locked.string()) != std::string::npos && Mode(locked) == 0555);
    CHECK(lines.size() == 1 && lines[0].find("fix=root-refused") != std::string::npos);

    // The fix ran but the folder still can't be written.
    lines.clear();
    calls = 0;
    const WritableFoldersAsRoot undone = [&](const std::function<bool()>& work) {
      ++calls;
      const bool ok = work();
      chmod(locked.c_str(), 0555);
      return ok;
    };
    message = EnsureWritableFolders({locked}, undone, log, root);
    CHECK(calls == 1 && message.find(locked.string()) != std::string::npos);
    CHECK(lines.size() == 1 && lines[0].find("after-fix=") != std::string::npos);

    // Fixed: 0777, logged, then writable without another raise.
    lines.clear();
    calls = 0;
    CHECK(EnsureWritableFolders({open, locked}, runs, log, root).empty());
    CHECK(calls == 1 && Mode(locked) == 0777 && Empty(locked));
    CHECK(lines.size() == 1 && lines[0].find("writable folder fixed") != std::string::npos &&
          lines[0].find("mode=0555 -> 0777") != std::string::npos);
    CHECK(EnsureWritableFolders({locked}, runs, log, root).empty() && calls == 1);
  }

  // A fresh /data and an FTP-created 0755 runtime root both become sticky.
  CHECK(PrepareRuntimeRoot(root / "data/playden", runs, log, root).empty());
  CHECK(Mode(root / "data/playden") == 01777);
  CHECK(chmod((root / "data/playden").c_str(), 0755) == 0);
  CHECK(PrepareRuntimeRoot(root / "data/playden", runs, log, root).empty());
  CHECK(Mode(root / "data/playden") == 01777 && Mode(locked) == 0777);
  // A writable 0777 root is fine as it is: the PS5's /data drops the sticky bit.
  CHECK(chmod((root / "data/playden").c_str(), 0777) == 0);
  lines.clear();
  CHECK(EnsureWritableFolders({root / "data/playden"}, runs, log, root).empty());
  CHECK(Mode(root / "data/playden") == 0777 && lines.empty());
  CHECK(chmod(target.c_str(), 0555) == 0);
  const auto before_calls = calls;
  CHECK(EnsureWritableFolders({target}, runs, log, root).empty());
  CHECK(Mode(target) == 0777 && calls == before_calls);
  CHECK(chmod(target.c_str(), 0755) == 0);
#if PLAYDEN_PLATFORM_PS5
  for (const int error : {EINVAL, ENOSYS}) {
    CHECK(chmod(locked.c_str(), 0555) == 0);
    CHECK(chmod((root / "data/playden").c_str(), 0755) == 0);
    playden_test_gateway_error = error;
    CHECK(!EnsureWritableFolders({locked}, runs, log, root).empty());
    CHECK(Mode(locked) == 0555);
    CHECK(!PrepareRuntimeRoot(root / "data/playden", runs, log, root).empty());
    CHECK(Mode(root / "data/playden") == 0755);
    CHECK(lines.back().find("error=" + std::to_string(error)) != std::string::npos);
    playden_test_gateway_error = 0;
    CHECK(chmod(locked.c_str(), 0777) == 0);
  }
#endif
  std::error_code ignored;
  fs::remove_all(root, ignored);
  std::puts("writable folders tests passed");
  return 0;
}
