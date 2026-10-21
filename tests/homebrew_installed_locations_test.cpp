// An imported game played in place reaches the controller as an external location (import-games
// Task 2.5) and never reaches Steam Cloud (Task 2.6), against a real RuntimeHost.
#include "homebrew/controller.hpp"
#include "homebrew/runtime_host.hpp"
#include "fake_library_source.h"
#include "platform/managed_directory.h"
#include "platform/storage.h"
#include "runtime/layout_migration.h"
#include "runtime/artifact_hash.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <unistd.h>

using namespace playden::homebrew;
using namespace playden::runtime;
using namespace playden::ps5;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

namespace fs = std::filesystem;
namespace {
std::string g_runtime_dir;  // the repository's bundled runtime data (recipes, presets, input)

void Write(const fs::path& path, const std::string& bytes, unsigned mode) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes;
  CHECK(chmod(path.c_str(), mode) == 0);
}
std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

struct Fixture {
  fs::path root;
  unsigned database_reads = 0, registry_mutations = 0;
  bool registered = false;
  // ShadowMountPlus postpones scans while a title runs: with registers false a
  // submission is accepted and listed, but not installed until registered is set.
  bool registers = true, submitted = false;
  std::shared_ptr<std::atomic<int>> activity = std::make_shared<std::atomic<int>>(static_cast<int>(NativeActivity::kStopped));
  Fixture() {
    char name[] = "/tmp/playden-runtime-host-XXXXXX"; CHECK(mkdtemp(name));
    root = fs::canonical(name);
    fs::create_directories(root / "data/homebrew");
  }
  ~Fixture() { std::error_code ignored; fs::remove_all(root, ignored); }
  RuntimeHostConfig Config() {
    RuntimeHostConfig config;
    config.boundary = root / "data";
    config.state_root = root / "data/runtime-state";
    config.homebrew_root = root / "data/homebrew";
    config.native.database = [this](const std::string& title) {
      ++database_reads;
      if (!registered) return TitleDatabaseObservation{TitleAvailability::kFree, {}};
      return TitleDatabaseObservation{TitleAvailability::kOccupied,
          InstalledTitleIdentity{title, "UP9000-FAKE78001_00-PROSPEROWIN00001", "/user/app/FAKE78001/sce_sys"}};
    };
    config.native.activity = [activity = activity](const std::string&) { return static_cast<NativeActivity>(activity->load()); };
    // ShadowMount loopback API: registration makes the title appear installed.
    config.registry_transport = [this](const std::string& route, const std::string&) -> std::optional<std::string> {
      if (route != "/api/v1/games") { ++registry_mutations; submitted = true; registered = registers; return R"({"status":0})"; }
      if (!registered && submitted)
        return R"({"status":0,"games":[{"title_id":"FAKE78001","installed":false,"managed":true,)"
               R"("source_available":true,"source_type":"folder","path":"/data/homebrew/FAKE78001"}]})";
      if (!registered) return R"({"status":0,"games":[]})";
      return R"({"status":0,"games":[{"title_id":"FAKE78001","installed":true,"managed":true,)"
             R"("source_available":true,"source_type":"folder","path":"/data/homebrew/FAKE78001"}]})";
    };
    config.admit_kit = [](std::string* reason) -> std::optional<RuntimeKit> {
      *reason = "No verified compatibility kit is installed.";
      return std::nullopt;
    };
    config.runtime_dir = g_runtime_dir;
    return config;
  }
};

// Generous: a loaded build machine (parallel ctest, other builds) can stretch a step past 10 s.
RuntimeSnapshot WaitFor(const RuntimeHost& host, const std::function<bool(const RuntimeSnapshot&)>& done,
                        int seconds = 30) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  for (;;) {
    auto snapshot = host.Snapshot();
    if (done(snapshot)) return snapshot;
    CHECK(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}
bool Started(const RuntimeSnapshot& s) { return s.status != RuntimeHostStatus::kStarting; }
std::uint32_t NumberOf(const RuntimeSnapshot& s, const playden::GameId& game) {
  for (const auto& [number, id] : s.runtime_games)
    if (id == game) return number;
  return 0;
}

// A synthetic admitted kit with the production file layout, real SHA-256 and
// the repository's HL2 recipe. It is not a runnable Wine runtime.
RuntimeKit SyntheticKit(const fs::path& kit, const std::string& eboot = "native",
                        const std::string& kit_id = "kit-1", char files_digest = 'a',
                        const std::string& runtime = "runtime", const std::string& revision = "rev") {
  Write(kit / "titles/variant/eboot.bin", eboot, 0755);
  Write(kit / "titles/variant/sce_sys/param.json",
        "{\n  \"contentId\": \"UP9000-FAKE78001_00-PROSPEROWIN00001\",\n  \"localizedParameters\": {\n"
        "    \"en-US\": {\n      \"titleName\": \"Prospero Win\"\n    }\n  },\n  \"titleId\": \"FAKE78001\"\n}\n", 0644);
  Write(kit / "titles/variant/sce_sys/icon0.png", "kit-icon", 0644);
  Write(kit / ("runtime/wine/" + revision + "/lib/core.prx"), runtime, 0755);
  Write(kit / "template/bottle/system.reg", "WINE REGISTRY Version 2\n", 0644);
  Write(kit / "template/bottle/user.reg", "WINE REGISTRY Version 2\n", 0644);
  Write(kit / "template/bottle/drive_c/windows/win.ini", "", 0644);
  Write(kit / "template/bottle/dosdevices/.pw-symlinks", "c:\t../drive_c\nz:\t/\n", 0644);
  Write(kit / "recipes/hl2.json", Read(fs::path(g_runtime_dir) / "recipes/steam-220-hl2-v1.json"), 0644);
  RuntimeKit admitted;
  auto& view = admitted.view;
  view.root = kit; view.kit_id = kit_id; view.files_sha256 = std::string(64, files_digest);
  view.runtime = {"wine-wow64", revision, "runtime/wine/" + revision};
  view.template_path = "template/bottle";
  view.template_drive_table = "template/bottle/dosdevices/.pw-symlinks";
  view.recipe = {"steam-220-hl2-v1", "1", "recipes/hl2.json", *HashArtifactFile(kit / "recipes/hl2.json")};
  view.variants.push_back({"variant", "FAKE78001", "titles/variant"});
  for (const auto& [path, mode] : std::vector<std::pair<std::string, unsigned>>{
           {"titles/variant/eboot.bin", 0755}, {"titles/variant/sce_sys/param.json", 0644},
           {"titles/variant/sce_sys/icon0.png", 0644},
           {"runtime/wine/" + revision + "/lib/core.prx", 0755}, {"template/bottle/system.reg", 0644},
           {"template/bottle/user.reg", 0644}, {"template/bottle/drive_c/windows/win.ini", 0644},
           {"template/bottle/dosdevices/.pw-symlinks", 0644}, {"recipes/hl2.json", 0644}})
    view.files.push_back({path, fs::file_size(kit / path), *HashArtifactFile(kit / path), mode});
  view.artifacts = {
      {"runtime", revision, *HashArtifactFile(kit / ("runtime/wine/" + revision + "/lib/core.prx")),
       ImmutableArtifactLocation::kStagedTitle, "win/wine/lib/core.prx"},
      {"recipe", "steam-220-hl2-v1-1", *HashArtifactFile(kit / "recipes/hl2.json"),
       ImmutableArtifactLocation::kKit, "recipes/hl2.json"}};
  admitted.capabilities = {"dxvk", "game-root-unowned", "offline_bottle", "per_game_title", "wow64_i386"};
  return admitted;
}
}  // namespace

namespace {
// Steam for the controller: a saved session restores (signed in) or there is none (signed out).
class MemorySessions final : public playden::SessionRepository {
 public:
  std::optional<playden::StoredSession> value;
  playden::Result<std::optional<playden::StoredSession>> Load() const override {
    return playden::Result<std::optional<playden::StoredSession>>::Success(value);
  }
  playden::Result<void> Save(const playden::StoredSession&) const override { return playden::Result<void>::Success(); }
  playden::Result<void> Clear() const override { return playden::Result<void>::Success(); }
};
class RestoringBackend final : public playden::steam::AuthBackend {
 public:
  playden::steam::AuthResult AuthenticateQr(const std::atomic<bool>&, const playden::steam::ChallengeCallback&) override {
    return {playden::steam::AuthError::kCanceled, std::nullopt};
  }
  playden::steam::AuthResult Restore(const playden::StoredSession& session, const std::atomic<bool>&) override {
    return {playden::steam::AuthError::kNone, session};
  }
};
class NoDownloads final : public playden::steam::SessionInstallRunner {
 public:
  using SessionInstallRunner::SessionInstallRunner;
  Result InstallApp(const playden::StoredSession&, std::uint32_t, const std::string&, const std::atomic<bool>&,
                    const std::function<void(std::uint64_t, std::uint64_t, std::uint64_t)>&,
                    const std::function<void(playden::steam::InstallStep)>&) override {
    return {false, false, "no downloads here", 0};
  }
  bool Remove(const std::string&) override { return true; }
};
// Records every game the Cloud is asked about.
class RecordingCloud final : public playden::steam::CloudRunner {
 public:
  std::mutex mutex;
  std::vector<playden::GameId> seen;
  playden::steam::CloudReport Check(const playden::StoredSession&, const playden::steam::CloudLocation& location,
                                    const std::atomic<bool>&) override {
    Record({location});
    return {playden::steam::CloudState::kUpToDate};
  }
  playden::steam::CloudReport Apply(const playden::StoredSession&, const playden::steam::CloudLocation& location,
                                    playden::steam::CloudResolution, bool, const std::atomic<bool>&) override {
    Record({location});
    return {playden::steam::CloudState::kUpToDate};
  }
  std::vector<playden::steam::CloudDrift> QuickCheck(const playden::StoredSession&,
                                                     const std::vector<playden::steam::CloudLocation>& locations,
                                                     const std::atomic<bool>&) override {
    Record(locations);
    return std::vector<playden::steam::CloudDrift>(locations.size(), playden::steam::CloudDrift::kUnchanged);
  }
  bool Saw(const playden::GameId& id) {
    std::lock_guard<std::mutex> lock(mutex);
    return std::find(seen.begin(), seen.end(), id) != seen.end();
  }

 private:
  void Record(const std::vector<playden::steam::CloudLocation>& locations) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& location : locations) seen.push_back(location.id);
  }
};
}  // namespace

// An imported game played in place is not Playden's install: it reaches the controller as an
// external location. Prepared before a restart and still waiting for the PS5 to register it, it is
// checked again at the next start, like a download (it stayed waiting without the location). It is
// never listed among Playden's installs, and with Steam signed in or out its saves never reach Steam
// Cloud, while the Steam game next to it is checked.
void ExternalGameRegistersAfterRestartWithoutCloud() {
  using playden::GameId;
  using playden::installs::InstalledLocation;
  namespace hb = playden::homebrew;
  const auto local = GameId::Local("0f8fad5b-d9cb-469f-a165-70867728950e");
  Fixture fixture;
  fixture.registers = false;
  auto config = fixture.Config();
  config.host_namespace_prefix = fixture.root;
  config.package_paths.console_data_root = "/data";
  config.package_paths.host_data_root = fixture.root / "data";
  config.package_operations.hash_file = HashArtifactFile;
  config.package_operations.hash_bytes = HashArtifactBytes;
  config.package_operations.free_space_unverifiable = true;
  config.package_operations.validate_bottle = [](const fs::path& bottle) {
    return fs::is_regular_file(bottle / "system.reg") && fs::is_regular_file(bottle / "user.reg");
  };
  const auto kit = SyntheticKit(fixture.root / "kit");
  config.admit_kit = [&kit](std::string*) { return std::optional<RuntimeKit>(kit); };
  unsigned uuids = 0;
  config.uuid = [&uuids]() -> std::optional<std::string> {
    ++uuids; return std::string("00000000-0000-4000-8000-0000000000") + (uuids < 10 ? "0" : "") + std::to_string(uuids);
  };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  std::string pe(0x80 + 26, '\0');
  pe[0] = 'M'; pe[1] = 'Z'; pe[0x3c] = 0x40; pe[0x40] = 'P'; pe[0x41] = 'E';
  pe[0x44] = 0x64; pe[0x45] = static_cast<char>(0x86); pe[0x40 + 24] = 0x0b; pe[0x40 + 25] = 0x02;
  const std::string console_root = "/data/playden-import/Imported";
  Write(fixture.root / "data/playden-import/Imported/Bin/Game.exe", pe, 0644);
  CHECK(chmod((fixture.root / "data/playden-import/Imported").c_str(), 0755) == 0);
  // Playden's own Steam download, for the Cloud check to have a game.
  const auto steam_games = fixture.root / "steam-games";
  Write(steam_games / ".playden-games", "220\tinternal\t4096\n", 0644);
  Write(steam_games / "220/.playden-install.json", "{\"install_bytes\":4096}", 0644);

  std::uint32_t number = 0;
  {
    // Prepared earlier: the title waits for the PS5, which registers it once Playden quits.
    RuntimeHost host(config);
    CHECK(host.Start());
    CHECK(WaitFor(host, Started).kit_available);
    number = *host.RuntimeNumberFor(local);
    RuntimePrepareRequest request{number, console_root, "usb0", "Imported"};
    request.launches = {{"", "", "Bin/Game.exe", "", ""}};
    request.game = local;
    host.Prepare(request);
    const auto waiting = WaitFor(host, [number](const RuntimeSnapshot& s) {
      return !s.preparing_app && (s.refusals.count(number) || (!s.installs.empty() && s.installs[0].awaiting_registration));
    }, 60);
    if (waiting.refusals.count(number)) std::fprintf(stderr, "Prepare refused: %s\n", waiting.refusals.at(number).c_str());
    CHECK(!waiting.refusals.count(number) && waiting.installs[0].awaiting_registration);
    host.Shutdown();
  }
  fixture.registered = true;  // Playden quit, and ShadowMountPlus registered the title

  const auto run = [&](bool signed_in, bool with_location, const std::function<bool(const RuntimeSnapshot&, hb::Simulation&)>& done,
                       std::chrono::milliseconds at_least) {
    hb::Simulation model({}, {}, hb::ModelMode::Live);
    MemorySessions sessions;
    if (signed_in) sessions.value = playden::StoredSession{12345, "Synthetic account", "refresh", "access", {}};
    RestoringBackend backend;
    playden::stores::Registry registry;
    registry.Add(std::make_unique<playden::stores::SteamAccount>(backend, sessions));
    auto imported = std::make_unique<playden::test::FakeLibrarySource>();
    imported->games = {playden::test::ImportedGame("0f8fad5b-d9cb-469f-a165-70867728950e", "Imported")};
    registry.Add(std::move(imported));
    NoDownloads downloads(sessions);
    playden::steam::InstallService installs(downloads, steam_games.string());
    RecordingCloud cloud_runner;
    playden::steam::CloudService cloud(cloud_runner, sessions);
    RuntimeHost host(config);
    CHECK(host.Start());
    hb::Controller controller(model, registry, nullptr, &installs, &cloud, &host);
    if (with_location)
      controller.SetExternalLocations([&] {
        return std::vector<InstalledLocation>{{local, console_root, "usb0", true, InstalledLocation::Ownership::kExternal}};
      });
    // Automatic Cloud sync on, so every automatic path would run.
    for (int i = 0; i < 6 && model.state().navigation.route != hb::Route::Settings; ++i) model.action("r1");
    CHECK(model.action("focus:settings-cloud-saves") && model.action("cross") && model.state().cloud_saves_auto);
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::seconds(60);
    for (;;) {
      controller.Tick();
      const auto snapshot = host.Snapshot();
      if (done(snapshot, model) && std::chrono::steady_clock::now() - started >= at_least) break;
      CHECK(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // Only Playden's own installs are listed; the imported game has no Cloud saves anywhere.
    const auto& state = model.state();
    CHECK(std::none_of(state.installations.begin(), state.installations.end(),
                       [&](const hb::Installation& item) { return item.game_id == local; }));
    CHECK(std::none_of(state.jobs.begin(), state.jobs.end(), [&](const hb::Job& job) { return job.game_id == local; }));
    CHECK(!state.cloud.count(local) && !cloud_runner.Saw(local));
    for (const auto& game : cloud.Snapshot().games) CHECK(game.id != local);
    CHECK(installs.Snapshot().jobs.empty());
    const bool steam_checked = cloud_runner.Saw(GameId::Steam(220));
    controller.Shutdown();
    return steam_checked;
  };
  const auto ready = [&](const RuntimeSnapshot& s) {
    return s.status == RuntimeHostStatus::kReady && !s.installs.empty() && s.installs[0].game == local &&
           s.installs[0].state == PreparationState::kReady && !s.installs[0].awaiting_registration;
  };
  // Without its location the controller doesn't know the game: it stays waiting.
  run(true, false, [](const RuntimeSnapshot& s, hb::Simulation& model) {
    return s.status == RuntimeHostStatus::kReady && model.state().runtime == hb::RuntimeState::Ready;
  }, std::chrono::milliseconds(500));
  {
    RuntimeHost host(config);
    CHECK(host.Start());
    const auto started = WaitFor(host, Started);
    CHECK(started.installs.size() == 1 && started.installs[0].awaiting_registration);
    host.Shutdown();
  }
  // With it, the startup check finds the title registered: the game is ready to play. Signed in,
  // the startup Cloud check covers the Steam game only.
  CHECK(run(true, true, [&](const RuntimeSnapshot& s, hb::Simulation& model) {
    const auto game = model.state().runtime_games.find(local);
    return ready(s) && game != model.state().runtime_games.end() && game->second.ready;
  }, std::chrono::milliseconds(0)));
  // Signed out, a preparation the player asks for ends without Cloud too.
  bool asked = false;
  run(false, true, [&](const RuntimeSnapshot& s, hb::Simulation& model) {
    if (!asked && ready(s) && model.state().runtime_games.count(local)) { model.RequestRuntimePrepare(local); asked = true; }
    return asked && ready(s) && s.prepares_handled > 0 && !s.preparing_app;
  }, std::chrono::milliseconds(300));
}

// Preparing a game from another store asks Steam nothing: without a program it is refused with a
// pointer to Game settings, never "Sign in to Steam", and its PS5 title carries no store's name even
// with "Store name in PS5 titles" on.
void ImportedGamePreparesWithoutSteam() {
  using playden::GameId;
  using playden::installs::InstalledLocation;
  namespace hb = playden::homebrew;
  const auto local = GameId::Local("7c9e6679-7425-40de-944b-e07fc1f90ae7");
  Fixture fixture;
  auto config = fixture.Config();
  config.host_namespace_prefix = fixture.root;
  config.package_paths.console_data_root = "/data";
  config.package_paths.host_data_root = fixture.root / "data";
  config.package_operations.hash_file = HashArtifactFile;
  config.package_operations.hash_bytes = HashArtifactBytes;
  config.package_operations.free_space_unverifiable = true;
  config.package_operations.validate_bottle = [](const fs::path& bottle) {
    return fs::is_regular_file(bottle / "system.reg") && fs::is_regular_file(bottle / "user.reg");
  };
  const auto kit = SyntheticKit(fixture.root / "kit");
  config.admit_kit = [&kit](std::string*) { return std::optional<RuntimeKit>(kit); };
  unsigned uuids = 0;
  config.uuid = [&uuids]() -> std::optional<std::string> {
    ++uuids; return std::string("00000000-0000-4000-8000-0000000000") + (uuids < 10 ? "0" : "") + std::to_string(uuids);
  };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  std::atomic<bool> program_chosen{false};
  config.settings_record = [&](const GameId& game) -> std::optional<GameSettingsRecord> {
    CHECK(game == local);
    if (!program_chosen) return std::nullopt;
    GameSettingsRecord record;
    record.overrides["program"] = "Bin/Game.exe";
    return record;
  };
  std::mutex log_mutex;
  std::vector<std::string> lines;
  config.log = [&](const std::string& line) { std::lock_guard<std::mutex> lock(log_mutex); lines.push_back(line); };
  std::string pe(0x80 + 26, '\0');
  pe[0] = 'M'; pe[1] = 'Z'; pe[0x3c] = 0x40; pe[0x40] = 'P'; pe[0x41] = 'E';
  pe[0x44] = 0x64; pe[0x45] = static_cast<char>(0x86); pe[0x40 + 24] = 0x0b; pe[0x40 + 25] = 0x02;
  const std::string console_root = "/data/playden-import/Second";
  Write(fixture.root / "data/playden-import/Second/Bin/Game.exe", pe, 0644);
  CHECK(chmod((fixture.root / "data/playden-import/Second").c_str(), 0755) == 0);

  hb::Simulation model({}, {}, hb::ModelMode::Live);
  MemorySessions sessions;  // signed out of Steam
  RestoringBackend backend;
  playden::stores::Registry registry;
  registry.Add(std::make_unique<playden::stores::SteamAccount>(backend, sessions));
  auto imported = std::make_unique<playden::test::FakeLibrarySource>();
  registry.Add(std::make_unique<playden::test::FakeLibrarySource>(playden::Store::kSteam));  // signed out: empty
  imported->games = {playden::test::ImportedGame("7c9e6679-7425-40de-944b-e07fc1f90ae7", "Second import")};
  registry.Add(std::move(imported));
  RuntimeHost host(config);
  CHECK(host.Start());
  hb::Controller controller(model, registry, nullptr, nullptr, nullptr, &host);
  controller.SetExternalLocations([&] {
    return std::vector<InstalledLocation>{{local, console_root, "usb0", true, InstalledLocation::Ownership::kExternal}};
  });
  const auto tick_until = [&](const std::function<bool(const RuntimeSnapshot&)>& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    for (;;) {
      controller.Tick();
      const auto snapshot = host.Snapshot();
      if (done(snapshot)) return snapshot;
      if (std::chrono::steady_clock::now() >= deadline)
        std::fprintf(stderr, "auth=%d loading=%d status=%d runtime=%d games=%zu handled=%llu refusals=%zu\n", (int)model.state().account().authentication, (int)model.state().library_loading, (int)snapshot.status,
                     (int)model.state().runtime, model.state().games.size(), (unsigned long long)snapshot.prepares_handled,
                     snapshot.refusals.size());
      CHECK(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  };
  tick_until([&](const RuntimeSnapshot& s) {
    return s.status == RuntimeHostStatus::kReady && model.state().runtime == hb::RuntimeState::Ready &&
           std::any_of(model.state().games.begin(), model.state().games.end(), [&](const hb::Game& game) { return game.id == local; });
  });
  for (int i = 0; i < 6 && model.state().navigation.route != hb::Route::Settings; ++i) model.action("r1");
  CHECK(model.action("focus:settings-title-store") && model.action("cross") && model.state().title_store_suffix);
  model.RequestRuntimePrepare(local);
  auto refused = tick_until([](const RuntimeSnapshot& s) { return s.prepares_handled > 0 && !s.preparing_app; });
  const auto number = NumberOf(refused, local);
  CHECK(number && refused.refusals.count(number));
  CHECK(refused.refusals.at(number) == "Choose the program that starts this game in Game settings.");
  // With a program chosen it is prepared, and its title is named without a store.
  program_chosen = true;
  model.RequestRuntimePrepare(local);
  tick_until([&](const RuntimeSnapshot& s) {
    return s.prepares_handled > 1 && !s.preparing_app && !s.installs.empty() && s.installs[0].game == local;
  });
  {
    std::lock_guard<std::mutex> lock(log_mutex);
    const auto named = "title metadata app=" + std::to_string(number) + " name=";
    const auto line = std::find_if(lines.begin(), lines.end(), [&](const std::string& l) { return l.rfind(named, 0) == 0; });
    CHECK(line != lines.end() && line->rfind(named + "Second import icon=", 0) == 0);
  }
  controller.Shutdown();
  host.Shutdown();
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  g_runtime_dir = argv[1];
  ExternalGameRegistersAfterRestartWithoutCloud();
  ImportedGamePreparesWithoutSteam();
  std::puts("installed locations tests passed");
  return 0;
}
