// "Copy into Playden" and "Move into Playden" (import-games Tasks 4.1-4.4): the copy plan of an imported game's folder,
// and the install service's Local runner copying it into a managed install, with the Local
// source's request, the ImportStore states, resume, cancel, uninstall and the restart reconcile.
// Built twice: with the host's descriptor walk, and with the console's walk by path.
#include "installs/install_service.h"
#include "platform/storage.h"
#include "stores/local/copy_plan.h"
#include "stores/local/copy_runner.h"
#include "stores/local/import_store.h"
#include "stores/local/library_source.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace playden;
using namespace playden::stores::local;
using installs::InstallJobState;
using installs::InstallService;
using installs::InstallVolume;
namespace fs = std::filesystem;

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

namespace {

void Write(const fs::path& path, const std::string& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes;
}
std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void WaitFor(const std::function<bool()>& done, const char* what) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (!done()) {
    if (std::chrono::steady_clock::now() > until) {
      std::fprintf(stderr, "timed out waiting for %s\n", what);
      std::exit(1);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

// Every regular file under `root`, relative, with its bytes (links and the folders left out).
std::vector<std::pair<std::string, std::string>> Files(const fs::path& root) {
  std::vector<std::pair<std::string, std::string>> files;
  for (auto it = fs::recursive_directory_iterator(root); it != fs::recursive_directory_iterator(); ++it)
    if (it->is_regular_file() && !it->is_symlink()) files.emplace_back(fs::relative(it->path(), root).string(), Read(it->path()));
  std::sort(files.begin(), files.end());
  return files;
}

// A game with nested assets: deeper than the scanner looks, hidden files, an empty file and
// folder, a name JSON must escape, and files bigger than the copy buffer.
void MakeGame(const fs::path& game) {
  Write(game / "Bin/Rally.exe", std::string(70000, 'x'));
  std::string big(3 * 1024 * 1024 + 17, '\0');
  for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>(i * 31 + 7);
  Write(game / "data/big.pak", big);
  Write(game / "data/a/b/c/d/e/f/g/h/i/j/deep.pak", "deep");
  Write(game / ".hidden/settings.ini", "[video]\nfullscreen=1\n");
  Write(game / "Caf\xc3\xa9 \"quoted\" name.txt", "unicode");
  Write(game / "empty.txt", "");
  fs::create_directories(game / "saves");
}

constexpr const char* kId = "1b4e28ba-2fa1-41d2-883f-0016d3cca427";
constexpr std::uint32_t kNumber = 0xF0000001u;

struct Fixture {
  fs::path root;
  fs::path source;  // the game on the drive
  fs::path games;   // the internal volume's games folder
  std::unique_ptr<Storage> storage;
  std::unique_ptr<ImportStore> store;
  LocalCopyOptions options;
  std::unique_ptr<LocalCopyRunner> runner;
  std::unique_ptr<InstallService> service;
  std::unique_ptr<LocalLibrarySource> local;
  std::vector<std::string> log;
  std::mutex log_mutex;
  int next_id = 0;

  Fixture() {
    char name[] = "/tmp/playden-local-copy-XXXXXX";
    CHECK(mkdtemp(name));
    root = fs::canonical(name);  // /tmp is a link on macOS; the walk never follows one
    source = root / "mnt/usb0/playden/import/Rally";
    games = root / "data/playden-games";
    fs::create_directories(games);
    fs::create_directories(root / "state");
    CHECK(::chmod((root / "state").c_str(), 0700) == 0);  // private storage
    MakeGame(source);
    options.log = [this](const std::string& line) {
      std::lock_guard<std::mutex> lock(log_mutex);
      log.push_back(line);
    };
  }
  ~Fixture() {
    Stop();
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
  // Playden starting: imports.json, the runner, the install service and the Local source.
  void Start(bool with_local = true) {
    storage = std::make_unique<Storage>((root / "state").string());
    // kId first, then others (the store asks again for an ID in use).
    store = std::make_unique<ImportStore>(*storage, "imports.json", [this]() -> std::optional<std::string> {
      static const char* const kIds[] = {kId, "6fa459ea-ee8a-4ca4-894e-db77e160355e", "16fd2706-8baf-433b-82eb-8c7fada847da"};
      if (next_id >= 3) return std::nullopt;
      return std::string(kIds[next_id++]);
    }, [] { return std::uint64_t{1760000000}; });
    runner = std::make_unique<LocalCopyRunner>(*store, options);
    service = std::make_unique<InstallService>(InstallService::Runners{{Store::kLocal, runner.get()}},
                                               std::vector<InstallVolume>{{"internal", "Console storage", games.string(), true}});
    service->SetRetryDelays(std::chrono::milliseconds(10), std::chrono::milliseconds(20));
    if (!with_local) {
      CHECK(store->Load().ok());
      return;
    }
    LocalLibraryConfig config;
    config.volumes = {{"internal", "/data", "playden-import", false}, {"usb0", "/mnt/usb0", "playden/import", false}};
    config.host_prefix = root.string();
    config.present = [](const ImportVolume&) { return true; };
    config.log = options.log;
    config.copy_into_playden = [this](const ImportEntry& entry, const std::string& from, const std::string& volume,
                                      std::uint32_t number) { return runner->Enqueue(*service, entry, from, volume, number); };
    config.move_into_playden = [this](const ImportEntry& entry, const std::string& from, std::uint32_t number) {
      return runner->Move(*service, entry, from, number);
    };
    local = std::make_unique<LocalLibrarySource>(*store, std::move(config));
    runner->SetChanged([source = local.get()] { source->Reload(); });
    WaitFor([&] { return local->Snapshot()->loaded || !local->Snapshot()->load_error.empty(); }, "imports loaded");
    if (!local->Snapshot()->loaded) std::fprintf(stderr, "load: %s\n", local->Snapshot()->load_error.c_str());
    CHECK(local->Snapshot()->loaded);
    service->RetryWaiting(Store::kLocal);
  }
  // Playden closing (or stopping at once: the install service first, as the controller does).
  void Stop() {
    if (service) service->Shutdown();
    if (runner) runner->SetChanged({});
    local.reset();
    service.reset();
    runner.reset();
    store.reset();
    storage.reset();
  }
  fs::path Dest() const { return games / ("local-" + std::string(kId)); }
  GameId Game() const { return GameId::Local(kId); }
  ImportEntry Add() {
    const auto added = store->Add("usb0", "playden/import/Rally", "Rally", "Bin/Rally.exe");
    CHECK(added.ok());
    if (local) local->Reload();
    return added.value();
  }
  // A game copied over FTP into the drop folder (same file system as the games folder).
  fs::path Drop() const { return root / "data/playden-import/Rally"; }
  ImportEntry AddDrop() {
    fs::create_directories(Drop().parent_path());
    fs::rename(source, Drop());
    const auto added = store->Add("internal", "playden-import/Rally", "Rally", "Bin/Rally.exe");
    CHECK(added.ok());
    if (local) local->Reload();
    return added.value();
  }
  ImportOutcome Outcome(std::uint64_t request) {
    ImportOutcome found;
    WaitFor([&] {
      const auto snapshot = local->Snapshot();
      for (const auto& outcome : snapshot->outcomes)
        if (outcome.request == request) { found = outcome; return true; }
      return false;
    }, "an outcome");
    return found;
  }
  std::optional<installs::InstallJobView> Job() {
    const auto snapshot = service->Snapshot();
    for (const auto& job : snapshot.jobs)
      if (job.id == Game()) return job;
    return std::nullopt;
  }
  bool Installed() {
    const auto snapshot = service->Snapshot();
    return std::any_of(snapshot.installed.begin(), snapshot.installed.end(),
                       [&](const installs::InstalledGame& game) { return game.id == Game() && game.available; });
  }
  ImportState State() {
    const auto entry = store->Find(kId);
    CHECK(entry.has_value());
    return entry->state;
  }
  bool Logged(const std::string& part) {
    std::lock_guard<std::mutex> lock(log_mutex);
    return std::any_of(log.begin(), log.end(), [&](const std::string& line) { return Has(line, part); });
  }
};

void PlanCoversTheTree() {
  Fixture fixture;
  const auto build = BuildCopyPlan(fixture.source.string());
  CHECK(build.ok);
  const auto& plan = build.plan;
  CHECK(plan.source == fixture.source.string());
  std::set<std::string> files, dirs(plan.dirs.begin(), plan.dirs.end());
  std::uint64_t total = 0;
  for (const auto& file : plan.files) {
    files.insert(file.path);
    total += file.bytes;
  }
  CHECK(files.size() == 6 && plan.files.size() == 6);
  CHECK(files.count("Bin/Rally.exe") && files.count("data/a/b/c/d/e/f/g/h/i/j/deep.pak") &&
        files.count(".hidden/settings.ini") && files.count("empty.txt") && files.count("Caf\xc3\xa9 \"quoted\" name.txt"));
  CHECK(dirs.count("saves") && dirs.count("data/a/b/c/d/e/f/g/h/i/j") && dirs.count(".hidden"));
  CHECK(plan.total == total && total == 70000 + 3 * 1024 * 1024 + 17 + 4 + 21 + 7);
  // Parents come before their children.
  for (std::size_t i = 0; i < plan.dirs.size(); ++i) {
    const auto slash = plan.dirs[i].find_last_of('/');
    if (slash == std::string::npos) continue;
    const auto parent = std::find(plan.dirs.begin(), plan.dirs.end(), plan.dirs[i].substr(0, slash));
    CHECK(parent != plan.dirs.end() && parent < plan.dirs.begin() + static_cast<long>(i));
  }

  // Saved before the job is queued, and read back the same; the file list names every path.
  auto saved = plan;
  saved.program = "Bin/Rally.exe";
  saved.runtime_number = kNumber;
  const auto dest = fixture.Dest().string();
  CHECK(SaveCopyPlan(saved, dest).empty());
  CHECK(fs::is_regular_file(fixture.Dest() / ".playden-copy/plan.json"));
  const auto text = Read(fixture.Dest() / ".playden-copy/plan.json");
  CHECK(text.rfind("{\"version\":1,\"source\":\"", 0) == 0 && Has(text, "\"runtime_number\":4026531841"));
  const auto loaded = LoadCopyPlan(dest);
  CHECK(loaded && loaded->source == saved.source && loaded->program == saved.program &&
        loaded->runtime_number == kNumber && loaded->total == saved.total && loaded->dirs == saved.dirs &&
        loaded->files.size() == saved.files.size());
  for (std::size_t i = 0; i < saved.files.size(); ++i)
    CHECK(loaded->files[i].path == saved.files[i].path && loaded->files[i].bytes == saved.files[i].bytes &&
          loaded->files[i].mtime == saved.files[i].mtime && loaded->files[i].mtime_nsec == saved.files[i].mtime_nsec);
  const auto list = Read(fixture.Dest() / ".playden-files");
  CHECK(Has(list, "f\tBin/Rally.exe\n") && Has(list, "d\tdata/a/b/c/d/e/f/g/h/i/j\n"));
  // A cut or edited plan doesn't load.
  Write(fixture.Dest() / ".playden-copy/plan.json", text.substr(0, text.size() - 4));
  CHECK(!LoadCopyPlan(dest));
  std::string edited = text;
  edited.replace(edited.find("\"bytes\":"), 9, "\"bytes\":9");
  Write(fixture.Dest() / ".playden-copy/plan.json", edited);
  CHECK(!LoadCopyPlan(dest));
  // RemoveManagedInstall knows the copy journal folder: a partial copy is Playden's own.
  CHECK(fs::exists(fixture.Dest()));

  // Limits: files and bytes.
  const auto few = BuildCopyPlan(fixture.source.string(), 3);
  CHECK(!few.ok && Has(few.failure, "more than 3 files"));
  const auto small = BuildCopyPlan(fixture.source.string(), kMaxCopyFiles, 1024);
  CHECK(!small.ok && Has(small.failure, "larger than"));
  const auto missing = BuildCopyPlan((fixture.root / "mnt/usb0/playden/import/None").string());
  CHECK(!missing.ok && Has(missing.failure, "not there"));
}

void LinksAreRefused() {
  Fixture fixture;
  // A link to a file inside, one to a folder inside, and one leaving: each fails, naming itself.
  fs::create_symlink("../Bin/Rally.exe", fixture.source / "data/game.lnk");
  auto build = BuildCopyPlan(fixture.source.string());
  CHECK(!build.ok && Has(build.failure, "\"data/game.lnk\"") && Has(build.failure, "link"));
  fs::remove(fixture.source / "data/game.lnk");
  fs::create_directory_symlink("a/b", fixture.source / "data/shortcut");
  build = BuildCopyPlan(fixture.source.string());
  CHECK(!build.ok && Has(build.failure, "\"data/shortcut\""));
  fs::remove(fixture.source / "data/shortcut");
  fs::create_directory_symlink("/etc", fixture.source / "escape");
  build = BuildCopyPlan(fixture.source.string());
  CHECK(!build.ok && Has(build.failure, "\"escape\""));
  fs::remove(fixture.source / "escape");
  CHECK(BuildCopyPlan(fixture.source.string()).ok);
  // The folder itself a link.
  fs::create_directory_symlink(fixture.source, fixture.root / "mnt/usb0/playden/import/Alias");
  CHECK(!BuildCopyPlan((fixture.root / "mnt/usb0/playden/import/Alias").string()).ok);

  // Through the Local source: refused, nothing queued, nothing left in Playden's games folder.
  fs::create_symlink("Bin/Rally.exe", fixture.source / "Rally.lnk");
  fixture.Start();
  fixture.Add();
  const auto outcome = fixture.Outcome(fixture.local->CopyIntoPlayden(kId));
  CHECK(!outcome.ok && Has(outcome.message, "\"Rally.lnk\""));
  CHECK(fixture.State() == ImportState::kInPlace && !fixture.Job() && !fs::exists(fixture.Dest()));
  // The move lists the files first too: refused before the rename.
  fs::remove(fixture.source / "Rally.lnk");
  fixture.AddDrop();
  const auto drop_id = fixture.store->FindByFolder("internal", "playden-import/Rally")->id;
  fs::create_symlink("Bin/Rally.exe", fixture.Drop() / "Rally.lnk");
  const auto moved = fixture.Outcome(fixture.local->MoveIntoPlayden(drop_id));
  CHECK(!moved.ok && Has(moved.message, "\"Rally.lnk\""));
  CHECK(fixture.store->Find(drop_id)->state == ImportState::kInPlace && fs::exists(fixture.Drop() / "Bin/Rally.exe"));
  CHECK(!fs::exists(fixture.games / ("local-" + drop_id)));
}

// A game set up to play from its drive folder: its PS5 title follows the verified copy before the
// install record is written; while the title runs the job waits, the drive folder still active.
void CopyRelocatesItsTitle() {
  Fixture fixture;
  std::mutex mutex;
  std::vector<LocalRelocation> asked;
  std::atomic<bool> running{true};
  fixture.options.relocate = [&](const LocalRelocation& relocation) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      asked.push_back(relocation);
    }
    LocalRelocationResult result;
    // The copy is complete and verified, but no install record yet.
    CHECK(fs::is_regular_file(fs::path(relocation.root) / "data/big.pak"));
    CHECK(!fs::exists(fs::path(relocation.root) / ".playden-install.json"));
    if (running) {
      result.outcome = LocalRelocationResult::Outcome::kBusy;
      result.message = "Close the game first.";
    }
    return result;
  };
  fixture.Start();
  fixture.Add();
  CHECK(fixture.Outcome(fixture.local->CopyIntoPlayden(kId, "", kNumber)).ok);
  WaitFor([&] { std::lock_guard<std::mutex> lock(mutex); return asked.size() >= 2; }, "the relocation asked again");
  WaitFor([&] {
    const auto job = fixture.Job();
    CHECK(job && job->state != InstallJobState::kFailed);
    return Has(job->failure, "Close the game first.");
  }, "the job waiting for the game to close");
  CHECK(!fixture.Installed() && fixture.State() == ImportState::kCopying);
  CHECK(fs::exists(fixture.Dest() / ".playden-copy/plan.json"));
  {
    std::lock_guard<std::mutex> lock(mutex);
    CHECK(asked[0].game == fixture.Game() && asked[0].runtime_number == kNumber &&
          asked[0].root == fixture.Dest().string() && !asked[0].move_files);
  }
  running = false;
  WaitFor([&] { return fixture.Installed() && fixture.State() == ImportState::kCopied; }, "the relocated copy");
  CHECK(fs::exists(fixture.Dest() / ".playden-install.json") && !fs::exists(fixture.Dest() / ".playden-copy"));
  CHECK(Has(Read(fixture.Dest() / ".playden-install.json"), "\"app_id\":4026531841"));
  CHECK(fs::exists(fixture.source / "Bin/Rally.exe"));  // the drive folder is never touched

  // A relocation that fails (the record could not be written) fails the job; Resume tries again.
  Fixture failing;
  failing.options.relocate = [](const LocalRelocation&) {
    LocalRelocationResult result;
    result.outcome = LocalRelocationResult::Outcome::kFailed;
    result.message = "The game's record could not be saved. Try again.";
    return result;
  };
  failing.Start();
  failing.Add();
  CHECK(failing.Outcome(failing.local->CopyIntoPlayden(kId, "", kNumber)).ok);
  WaitFor([&] { const auto job = failing.Job(); return job && job->state == InstallJobState::kFailed; }, "the failed relocation");
  CHECK(Has(failing.Job()->failure, "record could not be saved") && failing.State() == ImportState::kCopying);
  CHECK(!fs::exists(failing.Dest() / ".playden-install.json") && fs::exists(failing.Dest() / ".playden-copy/plan.json"));
}

// "Move into Playden": one rename (as root) of the drop folder into the games folder, under the
// relocation; the moved folder is a managed install that uninstalls like a copy.
void MovesDropFolderGame() {
  Fixture fixture;
  std::atomic<unsigned> raises{0};
  fixture.options.as_root = [&raises](const std::function<bool()>& work) { ++raises; return work(); };
  std::atomic<unsigned> relocations{0};
  fixture.options.relocate = [&](const LocalRelocation& relocation) {
    ++relocations;
    CHECK(relocation.game == fixture.Game() && relocation.runtime_number == kNumber && relocation.volume_id == "internal" &&
          relocation.root == fixture.Dest().string() && relocation.move_files);
    LocalRelocationResult result;
    result.message = relocation.move_files();
    if (!result.message.empty()) result.outcome = LocalRelocationResult::Outcome::kFailed;
    // Renamed, both parents synced, before the record follows.
    CHECK(fs::exists(fixture.Dest() / "Bin/Rally.exe") && !fs::exists(fixture.Drop()));
    return result;
  };
  fixture.Start();
  const auto before = Files(fixture.source);
  fixture.AddDrop();
  WaitFor([&] { return fixture.local->Locations().size() == 1; }, "the in-place location");
  const auto moved = fixture.Outcome(fixture.local->MoveIntoPlayden(kId, kNumber));
  if (!moved.ok) std::fprintf(stderr, "move: %s\n", moved.message.c_str());
  CHECK(moved.ok && relocations == 1 && raises == 1);
  CHECK(fixture.State() == ImportState::kCopied && fixture.Installed() && !fixture.Job());
  CHECK(!fs::exists(fixture.Drop()) && fs::exists(fixture.games / ".." / "playden-import"));
  auto after = Files(fixture.Dest());
  after.erase(std::remove_if(after.begin(), after.end(), [](const auto& file) {
    return file.first == ".playden-install.json" || file.first == ".playden-files";
  }), after.end());
  CHECK(after == before);
  CHECK(Has(Read(fixture.Dest() / ".playden-install.json"), "\"app_id\":4026531841"));
  CHECK(Has(Read(fixture.Dest() / ".playden-files"), "f\tdata/a/b/c/d/e/f/g/h/i/j/deep.pak\n"));
  CHECK(!fs::exists(fixture.Dest() / ".playden-copy"));
  WaitFor([&] { return fixture.local->Locations().empty(); }, "the in-place location gone");
  // Asked again: already in Playden.
  CHECK(!fixture.Outcome(fixture.local->MoveIntoPlayden(kId, kNumber)).ok);
  // Listed after a restart (the service's registry), and uninstalled like a copy.
  fixture.Stop();
  fixture.Start();
  CHECK(fixture.Installed() && fixture.State() == ImportState::kCopied);
  fixture.service->Uninstall(fixture.Game());
  WaitFor([&] { return !fs::exists(fixture.Dest()); }, "the moved game uninstalled");
  WaitFor([&] { return fixture.store->Removed().size() == 1; }, "removed from the library");

  // A game on a drive is copied, not moved.
  Fixture drive;
  drive.Start();
  drive.Add();
  const auto refused = drive.Outcome(drive.local->MoveIntoPlayden(kId, kNumber));
  CHECK(!refused.ok && Has(refused.message, "drop folder") && drive.State() == ImportState::kInPlace);
  CHECK(fs::exists(drive.source / "Bin/Rally.exe") && !fs::exists(drive.Dest()));
}

// While the game's title runs the relocation refuses before the rename: it stays in the drop folder.
void MoveRefusedWhileTheTitleRuns() {
  Fixture fixture;
  std::atomic<bool> ran{false};
  fixture.options.relocate = [&](const LocalRelocation&) {
    LocalRelocationResult result;
    result.outcome = LocalRelocationResult::Outcome::kBusy;
    result.message = "Close the game first.";
    ran = true;
    return result;
  };
  fixture.Start();
  fixture.AddDrop();
  const auto moved = fixture.Outcome(fixture.local->MoveIntoPlayden(kId, kNumber));
  CHECK(ran && !moved.ok && moved.message == "Close the game first.");
  CHECK(fixture.State() == ImportState::kInPlace && fixture.store->Find(kId)->failure.empty());
  CHECK(fs::exists(fixture.Drop() / "Bin/Rally.exe") && !fs::exists(fixture.Dest()) && !fixture.Installed());
  // A folder in the way refuses the move before anything changes.
  fixture.options.relocate = {};
  fs::create_directories(fixture.Dest());
  Write(fixture.Dest() / "mine.txt", "player's");
  fixture.Stop();
  fixture.Start();
  const auto blocked = fixture.Outcome(fixture.local->MoveIntoPlayden(kId, kNumber));
  CHECK(!blocked.ok && Has(blocked.message, "in the way") && fixture.State() == ImportState::kInPlace);
  CHECK(Read(fixture.Dest() / "mine.txt") == "player's" && fs::exists(fixture.Drop() / "Bin/Rally.exe"));
}

// Restarts during a move: after the rename (the title's record not written), and after the install
// record (imports.json not updated). Each start finishes it; none puts it back "in place".
void MoveFinishesAfterRestart() {
  Fixture fixture;
  // The rename runs, then Playden stops before the title's record follows.
  fixture.options.relocate = [](const LocalRelocation& relocation) {
    LocalRelocationResult result;
    CHECK(relocation.move_files().empty());
    result.outcome = LocalRelocationResult::Outcome::kFailed;
    result.message = "stopped";
    return result;
  };
  fixture.Start();
  fixture.AddDrop();
  const auto first = fixture.Outcome(fixture.local->MoveIntoPlayden(kId, kNumber));
  CHECK(!first.ok && fixture.State() == ImportState::kCopying);
  CHECK(!fs::exists(fixture.Drop()) && fs::exists(fixture.Dest() / "Bin/Rally.exe") && !fixture.Installed());
  CHECK(!fs::exists(fixture.Dest() / ".playden-install.json"));
  fixture.Stop();
  // The next start: the relocation is asked again (the rename found done), then the markers.
  std::atomic<unsigned> asked{0};
  fixture.options.relocate = [&asked](const LocalRelocation& relocation) {
    ++asked;
    LocalRelocationResult result;
    result.message = relocation.move_files();
    if (!result.message.empty()) result.outcome = LocalRelocationResult::Outcome::kFailed;
    return result;
  };
  // ...and stops again after the install record, before imports.json says copied.
  fixture.options.before_finish = [](const GameId&, const std::string&) { return false; };
  fixture.Start();
  auto* service = fixture.service.get();
  std::vector<GameId> numbered;
  fixture.local->Reconcile([service] { return service->Snapshot(); }, [service](const GameId& id) { service->Cancel(id); },
                           [&numbered](const GameId& id) { numbered.push_back(id); return kNumber; });
  WaitFor([&] { return fixture.Installed(); }, "the move finished as a managed install");
  WaitFor([&] { return fixture.Logged("import move reconciled id=" + std::string(kId)); }, "the move reconciled");
  CHECK(asked == 1 && numbered.size() == 1 && numbered[0] == fixture.Game());
  CHECK(fixture.State() == ImportState::kCopying && fs::exists(fixture.Dest() / ".playden-install.json"));
  CHECK(Has(Read(fixture.Dest() / ".playden-install.json"), "\"app_id\":4026531841"));
  CHECK(!Has(Read(fixture.Dest() / ".playden-files"), ".playden-install.json"));
  fixture.Stop();
  fixture.options.before_finish = {};
  fixture.Start();
  service = fixture.service.get();
  fixture.local->Reconcile([service] { return service->Snapshot(); }, [service](const GameId& id) { service->Cancel(id); });
  WaitFor([&] { return fixture.State() == ImportState::kCopied; }, "copied after the restart");
  CHECK(fixture.Installed() && asked == 1);

  // A move cut off before its rename goes back in place.
  Fixture early;
  early.Start();
  early.AddDrop();
  CHECK(early.store->QueueCopy(kId).ok() && early.store->StartCopy(kId).ok());
  auto* early_service = early.service.get();
  early.local->Reconcile([early_service] { return early_service->Snapshot(); },
                         [early_service](const GameId& id) { early_service->Cancel(id); });
  WaitFor([&] { return early.State() == ImportState::kInPlace; }, "back in place");
  CHECK(fs::exists(early.Drop() / "Bin/Rally.exe") && !fs::exists(early.Dest()));
}

void CopiesIntoPlayden() {
  Fixture fixture;
  const auto before = Files(fixture.source);
  fixture.Start();
  fixture.Add();
  // Playing in place until it's copied: an external location.
  WaitFor([&] { return fixture.local->Locations().size() == 1; }, "the in-place location");
  CHECK(!fixture.local->Locations()[0].managed());
  std::vector<std::uint64_t> seen;
  const auto request = fixture.local->CopyIntoPlayden(kId, "", kNumber);
  const auto outcome = fixture.Outcome(request);
  CHECK(outcome.ok);
  WaitFor([&] {
    if (const auto job = fixture.Job(); job && job->total_bytes) seen.push_back(job->done_bytes);
    return fixture.Installed();
  }, "the copy");
  WaitFor([&] { return fixture.State() == ImportState::kCopied; }, "copied");
  // The copy is the folder, byte for byte; the source is untouched.
  auto copied = Files(fixture.Dest());
  copied.erase(std::remove_if(copied.begin(), copied.end(), [](const auto& file) {
    return file.first == ".playden-install.json" || file.first == ".playden-files";
  }), copied.end());
  CHECK(copied == before);
  CHECK(Files(fixture.source) == before);
  CHECK(fs::is_directory(fixture.Dest() / "saves"));
  CHECK(!fs::exists(fixture.Dest() / ".playden-copy"));
  for (const auto& file : Files(fixture.Dest())) CHECK(!Has(file.first, ".part"));
  // Recorded as the depot installer records another store's game, with the runtime number.
  const auto record = Read(fixture.Dest() / ".playden-install.json");
  CHECK(record.rfind("{\"version\":1,\"app_id\":4026531841,\"store\":\"local\",\"game\":\"" + std::string(kId) +
                         "\",\"install_bytes\":" + std::to_string(70000 + 3 * 1024 * 1024 + 17 + 4 + 21 + 7) + ",",
                     0) == 0);
  const auto snapshot = fixture.service->Snapshot();
  CHECK(snapshot.installed.size() == 1 && snapshot.installed[0].install_bytes == 70000 + 3 * 1024 * 1024 + 17 + 4 + 21 + 7);
  CHECK(snapshot.installed[0].root == fixture.Dest().string() && snapshot.jobs.empty());
  // Progress went up in bytes.
  CHECK(std::is_sorted(seen.begin(), seen.end()));
  // The location is managed now: the Local source no longer offers the drive folder.
  WaitFor([&] { return fixture.local->Locations().empty(); }, "the external location gone");
  const auto projected = installs::ProjectInstalledLocations(snapshot.installed, fixture.local->Locations());
  CHECK(projected.size() == 1 && projected[0].managed() && projected[0].root == fixture.Dest().string());
  // A copied game isn't copied again.
  CHECK(!fixture.Outcome(fixture.local->CopyIntoPlayden(kId)).ok);

  // Uninstall: the files go, and the entry leaves the library for the removed list.
  fixture.service->Uninstall(fixture.Game());
  WaitFor([&] { return !fs::exists(fixture.Dest()); }, "the files removed");
  WaitFor([&] { return fixture.local->Snapshot()->games.empty(); }, "the game out of the library");
  CHECK(!fixture.store->Find(kId) && fixture.store->Removed().size() == 1 && fixture.store->Removed()[0].id == kId);
  CHECK(Files(fixture.source) == before);
}

void ResumesAfterPauseAndRestart() {
  Fixture fixture;
  std::atomic<int> copied{0};
  std::atomic<bool> pause{true};
  fixture.options.buffer_bytes = 4096;
  fixture.options.after_file = [&](const std::string&) {
    ++copied;
    if (pause.exchange(false)) fixture.service->Pause(GameId::Local(kId));
  };
  fixture.Start();
  fixture.Add();
  CHECK(fixture.Outcome(fixture.local->CopyIntoPlayden(kId)).ok);
  WaitFor([&] { const auto job = fixture.Job(); return job && job->state == InstallJobState::kPaused; }, "paused");
  CHECK(fixture.State() == ImportState::kCopying);
  const int first = copied.load();
  CHECK(first >= 1 && first < 6);
  CHECK(fs::exists(fixture.Dest() / ".playden-copy/plan.json"));
  // Playden restarts: the paused copy comes back paused, and Resume copies only what is left.
  fixture.Stop();
  fixture.Start();
  WaitFor([&] { const auto job = fixture.Job(); return job && job->state == InstallJobState::kPaused; }, "restored paused");
  CHECK(fixture.State() == ImportState::kCopying);
  fixture.service->Resume(fixture.Game());
  WaitFor([&] { return fixture.Installed(); }, "the resumed copy");
  WaitFor([&] { return fixture.State() == ImportState::kCopied; }, "copied");
  CHECK(copied.load() == 6);  // each file once
  CHECK(Read(fixture.Dest() / "data/big.pak") == Read(fixture.source / "data/big.pak"));
}

void CancelLeavesNothing() {
  Fixture fixture;
  std::atomic<bool> cancel{true};
  fixture.options.after_file = [&](const std::string&) {
    if (cancel.exchange(false)) fixture.service->Cancel(GameId::Local(kId));
  };
  fixture.Start();
  fixture.Add();
  CHECK(fixture.Outcome(fixture.local->CopyIntoPlayden(kId)).ok);
  WaitFor([&] { return !fixture.Job() && !fs::exists(fixture.Dest()); }, "the cancelled copy removed");
  WaitFor([&] { return fixture.State() == ImportState::kInPlace; }, "back in place");
  CHECK(fixture.store->Find(kId)->failure.empty());
  CHECK(!fixture.Installed());
  // Copied again after the cancel: from the start, to the end.
  CHECK(fixture.Outcome(fixture.local->CopyIntoPlayden(kId)).ok);
  WaitFor([&] { return fixture.Installed() && fixture.State() == ImportState::kCopied; }, "the second copy");
}

void ChangedSourceFails() {
  Fixture fixture;
  std::atomic<bool> grow{true};
  std::string first;
  fixture.options.after_file = [&](const std::string& path) {
    if (!grow.exchange(false)) return;
    first = path;
    // Every other file grows (one is still to be copied).
    for (auto it = fs::recursive_directory_iterator(fixture.source); it != fs::recursive_directory_iterator(); ++it)
      if (it->is_regular_file() && fs::relative(it->path(), fixture.source).string() != path)
        std::ofstream(it->path(), std::ios::binary | std::ios::app) << "more";
  };
  fixture.Start();
  fixture.Add();
  CHECK(fixture.Outcome(fixture.local->CopyIntoPlayden(kId)).ok);
  WaitFor([&] { const auto job = fixture.Job(); return job && job->state == InstallJobState::kFailed; }, "the failure");
  CHECK(fixture.Job()->failure == kCopyChangedFailure);
  // The partial copy is gone, and the game plays in place again with the failure on its entry.
  CHECK(!fs::exists(fixture.Dest()));
  CHECK(fixture.State() == ImportState::kInPlace && fixture.store->Find(kId)->failure == kCopyChangedFailure);
  // Resume can't carry on; cancelling clears Downloads.
  fixture.service->Resume(fixture.Game());
  WaitFor([&] { const auto job = fixture.Job(); return job && job->state == InstallJobState::kFailed; }, "failed again");
  fixture.service->Cancel(fixture.Game());
  WaitFor([&] { return !fixture.Job(); }, "the job gone");
  CHECK(fixture.State() == ImportState::kInPlace && !fs::exists(fixture.Dest()));

  // A file removed after the plan: the same failure.
  std::atomic<bool> remove{true};
  fixture.Stop();
  fixture.options.after_file = [&](const std::string& path) {
    if (!remove.exchange(false)) return;
    for (auto it = fs::recursive_directory_iterator(fixture.source); it != fs::recursive_directory_iterator(); ++it)
      if (it->is_regular_file() && fs::relative(it->path(), fixture.source).string() != path) {
        fs::remove(it->path());
        break;
      }
  };
  fixture.Start();
  CHECK(fixture.Outcome(fixture.local->CopyIntoPlayden(kId)).ok);
  WaitFor([&] { const auto job = fixture.Job(); return job && job->state == InstallJobState::kFailed; }, "the second failure");
  CHECK(fixture.Job()->failure == kCopyChangedFailure && !fs::exists(fixture.Dest()));
}

void ProgramMissingIsRefused() {
  Fixture fixture;
  fixture.Start(false);
  // The entry's program isn't in the folder: nothing is queued or written.
  const auto added = fixture.store->Add("usb0", "playden/import/Rally", "Rally", "Bin/Missing.exe");
  CHECK(added.ok());
  const auto failure = fixture.runner->Enqueue(*fixture.service, added.value(), fixture.source.string(), "", kNumber);
  CHECK(Has(failure, "program"));
  CHECK(fixture.State() == ImportState::kInPlace && !fixture.Job() && !fs::exists(fixture.Dest()));

  // The program in the plan but not in the copy (a plan that doesn't list it): verify refuses it.
  CHECK(fixture.store->SetProgram(kId, "Bin/Rally.exe").ok());
  auto plan = BuildCopyPlan(fixture.source.string());
  CHECK(plan.ok);
  plan.plan.program = "Bin/Rally.exe";
  plan.plan.files.erase(std::remove_if(plan.plan.files.begin(), plan.plan.files.end(), [](const CopyFile& file) {
    return file.path == "Bin/Rally.exe";
  }), plan.plan.files.end());
  plan.plan.total -= 70000;
  CHECK(SaveCopyPlan(plan.plan, fixture.Dest().string()).empty());
  CHECK(fixture.store->QueueCopy(kId).ok());
  fixture.service->Install(fixture.Game(), plan.plan.total);
  WaitFor([&] { const auto job = fixture.Job(); return job && job->state == InstallJobState::kFailed; }, "the verify failure");
  CHECK(Has(fixture.Job()->failure, "program is missing"));
  CHECK(fixture.State() == ImportState::kInPlace && !fs::exists(fixture.Dest()));
}

void CrashBetweenCommitSteps() {
  Fixture fixture;
  // Playden stops after the install record, before imports.json says copied.
  fixture.options.before_finish = [](const GameId&, const std::string&) { return false; };
  fixture.Start();
  fixture.Add();
  CHECK(fixture.Outcome(fixture.local->CopyIntoPlayden(kId)).ok);
  WaitFor([&] { return fixture.Installed(); }, "the managed install");
  CHECK(fixture.State() == ImportState::kCopying);
  fixture.Stop();
  // The next start lists the managed install; the reconcile commits the entry.
  fixture.options.before_finish = {};
  fixture.Start();
  CHECK(fixture.Installed() && fixture.State() == ImportState::kCopying);
  auto* service = fixture.service.get();
  fixture.local->Reconcile([service] { return service->Snapshot(); }, [service](const GameId& id) { service->Cancel(id); });
  WaitFor([&] { return fixture.State() == ImportState::kCopied; }, "reconciled to copied");
  CHECK(fixture.Logged("import copy reconciled id=" + std::string(kId) + " copied"));
  WaitFor([&] { return fixture.local->Locations().empty(); }, "the external location gone");
  CHECK(fs::exists(fixture.Dest() / "Bin/Rally.exe"));
}

void ReconcileWithoutJob() {
  Fixture fixture;
  fixture.Start();
  fixture.Add();
  // imports.json says copying, but the service has neither a job nor an install (its registry lost).
  CHECK(fixture.store->QueueCopy(kId).ok() && fixture.store->StartCopy(kId).ok());
  auto* service = fixture.service.get();
  fixture.local->Reconcile([service] { return service->Snapshot(); }, [service](const GameId& id) { service->Cancel(id); });
  WaitFor([&] { return fixture.State() == ImportState::kInPlace; }, "back in place");
  CHECK(fixture.store->Find(kId)->failure == kCopyStoppedFailure);
  // A copy job whose game is back in place leaves Downloads.
  fixture.service->Install(fixture.Game(), 1);
  WaitFor([&] { const auto job = fixture.Job(); return job && job->state == InstallJobState::kFailed; }, "the stale job");
  fixture.local->Reconcile([service] { return service->Snapshot(); }, [service](const GameId& id) { service->Cancel(id); });
  WaitFor([&] { return !fixture.Job(); }, "the stale job cancelled");
}

void FreeSpaceRefused() {
  Fixture fixture;
  // At the start (and on resume): the runner checks what is left against the drive.
  std::atomic<std::uint64_t> free_bytes{1024};
  fixture.options.space = [&](const std::string&) {
    return std::optional<steam::VolumeSpace>(steam::VolumeSpace{1ull << 40, free_bytes.load()});
  };
  fixture.Start();
  fixture.Add();
  CHECK(fixture.Outcome(fixture.local->CopyIntoPlayden(kId)).ok);
  WaitFor([&] { const auto job = fixture.Job(); return job && job->state == InstallJobState::kFailed; }, "no space");
  CHECK(Has(fixture.Job()->failure, "Not enough free space"));
  // Not a lost copy: the plan stays and Resume carries on once there's room.
  CHECK(fixture.State() == ImportState::kCopying && fs::exists(fixture.Dest() / ".playden-copy/plan.json"));
  free_bytes = 1ull << 40;
  fixture.service->Resume(fixture.Game());
  WaitFor([&] { return fixture.Installed() && fixture.State() == ImportState::kCopied; }, "the copy with room");
  fixture.service->Uninstall(fixture.Game());
  WaitFor([&] { return !fs::exists(fixture.Dest()); }, "uninstalled");
  WaitFor([&] { return fixture.store->Removed().size() == 1; }, "removed");

  // At the enqueue: the install service's check of the plan's total. A sparse file larger than
  // the drive's free space (when that is under the plan limit) is refused before anything copies.
  const auto space = steam::QueryVolumeSpace(fixture.games.string());
  if (!space || space->free_bytes + (1ull << 30) >= kMaxCopyBytes) {
    std::fprintf(stderr, "skipped: enqueue free-space refusal (free space unknown or above 1 TiB)\n");
    return;
  }
  const auto huge = fixture.root / "mnt/usb0/playden/import/Huge";
  Write(huge / "Huge.exe", "x");
  {
    const int fd = ::open((huge / "huge.pak").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    CHECK(fd >= 0 && ::ftruncate(fd, static_cast<off_t>(space->free_bytes + (1ull << 30))) == 0);
    ::close(fd);
  }
  const auto added = fixture.store->Add("usb0", "playden/import/Huge", "Huge", "Huge.exe");
  CHECK(added.ok());
  const auto failure = fixture.runner->Enqueue(*fixture.service, added.value(), huge.string(), "", 0);
  CHECK(Has(failure, "Not enough free space"));
  CHECK(fixture.store->Find(added.value().id)->state == ImportState::kInPlace);
  CHECK(fixture.store->Find(added.value().id)->failure == failure);
  const auto dest = fixture.games / ("local-" + added.value().id);
  WaitFor([&] {
    const auto snapshot = fixture.service->Snapshot();
    return snapshot.jobs.empty() && !fs::exists(dest);
  }, "the refused copy gone");
}

}  // namespace

int main() {
  PlanCoversTheTree();
  LinksAreRefused();
  CopiesIntoPlayden();
  ResumesAfterPauseAndRestart();
  CancelLeavesNothing();
  ChangedSourceFails();
  ProgramMissingIsRefused();
  CrashBetweenCommitSteps();
  ReconcileWithoutJob();
  FreeSpaceRefused();
  CopyRelocatesItsTitle();
  MovesDropFolderGame();
  MoveRefusedWhileTheTitleRuns();
  MoveFinishesAfterRestart();
  std::puts("local copy tests passed");
  return 0;
}
