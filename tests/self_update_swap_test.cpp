#include "update/app_swap.h"
#include "update/file_ops.h"
#include "zip_fixture.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace playden::update;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #value); std::exit(1); } } while(false)

namespace {
std::string Read(const std::string& path) {
  std::string text;
  return ReadSmallFile(path, &text) ? text : "<missing>";
}
void Put(const std::string& path, const std::string& text) {
  CHECK(MakeParents(path));
  CHECK(zip_fixture::Write(path, text));
}
// An installed app (version tag v) with user data, and a staged app (version tag w).
void Layout(const std::string& root, const std::string& v, const std::string& w) {
  Put(root + "/installed/eboot.bin", v);
  Put(root + "/installed/sce_sys/param.json", v);
  Put(root + "/installed/assets/a.txt", v);
  Put(root + "/installed/only-old.txt", v);
  Put(root + "/installed/logs/launch.log", "log");
  Put(root + "/installed/playden-data/session", "session");
  Put(root + "/installed/games/1/x", "game");
  Put(root + "/staged/eboot.bin", w);
  Put(root + "/staged/sce_sys/param.json", w);
  Put(root + "/staged/assets/a.txt", w);
  Put(root + "/staged/only-new.txt", w);
  Put(root + "/staged/logs/empty", "");
}
}  // namespace

int main() {
  // A swap moves the release's entries, keeps user folders and backs up what it replaced.
  {
    const std::string root = zip_fixture::TempFolder("swap");
    Layout(root, "old", "new");
    std::vector<std::string> lines;
    const auto result = SwapApp(root + "/installed", root + "/staged", root + "/backup",
                                [&](const std::string& line) { lines.push_back(line); });
    CHECK(result.ok && result.moved == 4);
    CHECK(Read(root + "/installed/eboot.bin") == "new" && Read(root + "/installed/assets/a.txt") == "new");
    CHECK(Read(root + "/installed/only-new.txt") == "new" && Read(root + "/installed/only-old.txt") == "old");
    CHECK(Read(root + "/installed/logs/launch.log") == "log" && !Exists(root + "/installed/logs/empty"));
    CHECK(Read(root + "/installed/playden-data/session") == "session" && Read(root + "/installed/games/1/x") == "game");
    CHECK(Read(root + "/backup/eboot.bin") == "old" && Read(root + "/backup/sce_sys/param.json") == "old");
    CHECK(!Exists(root + "/backup/logs") && !Exists(root + "/backup/only-old.txt"));
    // Going back puts the old entries in place again.
    const auto back = RestoreApp(root + "/installed", root + "/backup", root + "/displaced", {});
    CHECK(back.ok && Read(root + "/installed/eboot.bin") == "old" && Read(root + "/displaced/eboot.bin") == "new");
    CHECK(Read(root + "/installed/only-new.txt") == "new");  // not part of the backup, so it stays
    RemoveTree(root);
  }
  // A rename that fails halfway is undone: the installed app is unchanged.
  {
    const std::string root = zip_fixture::TempFolder("swap-undo");
    Layout(root, "old", "new");
    Put(root + "/backup-file", "x");
    const auto refused = SwapApp(root + "/installed", root + "/staged", root + "/backup-file", {});
    CHECK(!refused.ok && refused.error == "backup folder already exists");
    // Entries can't leave a read-only staged folder: the installed entries already moved aside go back.
    CHECK(chmod((root + "/staged").c_str(), 0555) == 0);
    const auto failed = SwapApp(root + "/installed", root + "/staged", root + "/backup", {});
    CHECK(chmod((root + "/staged").c_str(), 0755) == 0);
    if (geteuid() != 0) {  // root ignores the folder's mode
      CHECK(!failed.ok);
      CHECK(Read(root + "/installed/eboot.bin") == "old" && Read(root + "/installed/assets/a.txt") == "old");
      CHECK(Read(root + "/staged/eboot.bin") == "new" && ListFolder(root + "/backup").empty());
    }
    RemoveTree(root);
  }
  // An incomplete staged app is refused before anything moves.
  {
    const std::string root = zip_fixture::TempFolder("swap-incomplete");
    Put(root + "/installed/eboot.bin", "old");
    Put(root + "/staged/assets/a", "new");
    CHECK(!SwapApp(root + "/installed", root + "/staged", root + "/backup", {}).ok);
    CHECK(!Exists(root + "/backup"));
    RemoveTree(root);
  }
  // Home-screen copies are refreshed where they exist.
  {
    const std::string root = zip_fixture::TempFolder("refresh");
    Put(root + "/sce_sys/param.json", "new-param");
    Put(root + "/sce_sys/icon0.png", "new-icon");
    Put(root + "/appmeta/param.json", "old");
    Put(root + "/app/sce_sys/param.json", "old");
    Put(root + "/app/icon0.png", "old");
    CHECK(RefreshRegisteredCopies(root + "/sce_sys", root + "/appmeta", root + "/app", {}));
    CHECK(Read(root + "/appmeta/param.json") == "new-param" && Read(root + "/appmeta/icon0.png") == "new-icon");
    CHECK(Read(root + "/app/sce_sys/param.json") == "new-param" && Read(root + "/app/icon0.png") == "new-icon");
    CHECK(RefreshRegisteredCopies(root + "/sce_sys", root + "/missing", root + "/missing2", {}));
    RemoveTree(root);
  }
  std::puts("ok");
  return 0;
}
