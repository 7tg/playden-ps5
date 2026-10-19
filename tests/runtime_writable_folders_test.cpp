#include "runtime/writable_folders.h"

#include <cstdio>
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
  const auto open = root / "open", locked = root / "locked", target = root / "target", link = root / "link";
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
  CHECK(EnsureWritableFolders({open, root / "missing", link, {}}, runs, log).empty());
  CHECK(calls == 0 && lines.empty() && Empty(open));

  if (geteuid() != 0) {
    // A linked folder is never followed (nor fixed), even when its target can't be written.
    CHECK(chmod(target.c_str(), 0555) == 0);
    CHECK(EnsureWritableFolders({link}, runs, log).empty());
    CHECK(calls == 0 && Mode(target) == 0555);
    CHECK(chmod(target.c_str(), 0755) == 0);

    // A link swapped in between the probe and the fix is refused, its target left alone.
    const auto swapped = root / "swapped";
    fs::create_directories(swapped);
    CHECK(chmod(swapped.c_str(), 0555) == 0 && chmod(target.c_str(), 0555) == 0);
    const WritableFoldersAsRoot swaps = [&](const std::function<bool()>& work) {
      ++calls;
      fs::rename(swapped, root / "swapped-away");
      fs::create_directory_symlink(target, swapped);
      return work();
    };
    CHECK(!EnsureWritableFolders({swapped}, swaps, log).empty());
    CHECK(calls == 1 && Mode(target) == 0555 && Mode(root / "swapped-away") == 0555);
    CHECK(lines.size() == 1 && lines[0].find("chmod=") != std::string::npos);
    CHECK(chmod(target.c_str(), 0755) == 0 && chmod((root / "swapped-away").c_str(), 0755) == 0);
    lines.clear();
    calls = 0;

    // No way to raise root: the folder is named.
    CHECK(chmod(locked.c_str(), 0555) == 0);
    auto message = EnsureWritableFolders({open, locked}, {}, log);
    CHECK(message.find("can't write to " + locked.string() + ".") != std::string::npos);
    CHECK(Mode(locked) == 0555 && lines.size() == 1 && lines[0].find("fix=unavailable") != std::string::npos);

    // Root refused.
    lines.clear();
    message = EnsureWritableFolders({locked}, refuses, log);
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
    message = EnsureWritableFolders({locked}, undone, log);
    CHECK(calls == 1 && message.find(locked.string()) != std::string::npos);
    CHECK(lines.size() == 1 && lines[0].find("after-fix=") != std::string::npos);

    // Fixed: 0777, logged, then writable without another raise.
    lines.clear();
    calls = 0;
    CHECK(EnsureWritableFolders({open, locked}, runs, log).empty());
    CHECK(calls == 1 && Mode(locked) == 0777 && Empty(locked));
    CHECK(lines.size() == 1 && lines[0].find("writable folder fixed") != std::string::npos &&
          lines[0].find("mode=0555 -> 0777") != std::string::npos);
    CHECK(EnsureWritableFolders({locked}, runs, log).empty() && calls == 1);
  }

  std::error_code ignored;
  fs::remove_all(root, ignored);
  std::puts("writable folders tests passed");
  return 0;
}
