#include "homebrew/runtime_host.hpp"
#include "platform/storage.h"
#include "runtime/artifact_hash.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <thread>
#include <atomic>
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
      if (route != "/api/v1/games") { ++registry_mutations; registered = true; return R"({"status":0})"; }
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

RuntimeSnapshot WaitFor(const RuntimeHost& host, const std::function<bool(const RuntimeSnapshot&)>& done,
                        int seconds = 10) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  for (;;) {
    auto snapshot = host.Snapshot();
    if (done(snapshot)) return snapshot;
    CHECK(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}
bool Started(const RuntimeSnapshot& s) { return s.status != RuntimeHostStatus::kStarting; }

RuntimePrepareRequest Hl2() { return {220, "/data/homebrew/FAKE77031/games/220", "internal", "Half-Life \"2\""}; }

RuntimeInstall Record() {
  RuntimeInstall record;
  record.install_id = "steam-220"; record.app_id = 220; record.default_profile_id = "playden-steam-220";
  record.runtime_bundle_id = "wine-wow64"; record.recipe_id = "steam-220-hl2-v1"; record.recipe_version = "1";
  record.launch_entry_id = "hl2"; record.launch_path = "hl2.exe";
  record.install_path = "/data/homebrew/FAKE77031/games/220"; record.volume_id = "internal";
  return record;
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
  admitted.capabilities = {"dxvk", "offline_bottle", "per_game_title", "wow64_i386"};
  return admitted;
}
}  // namespace

// Without an admitted kit, the composed services start, report why preparation
// is unavailable and refuse Prepare without creating durable records.
void NoKitRefusesPreparationWithoutDurableEffects() {
  Fixture fixture;
  RuntimeHost host(fixture.Config());
  CHECK(host.Start());
  const auto started = WaitFor(host, Started);
  CHECK(started.status == RuntimeHostStatus::kReady);
  CHECK(!started.kit_available && started.kit_reason == "No verified compatibility kit is installed.");
  CHECK(started.installs.empty());
  struct stat info{};
  CHECK(stat((fixture.root / "data/runtime-state").c_str(), &info) == 0 && (info.st_mode & 0777) == 0700);
  host.Prepare(Hl2());
  const auto refused = WaitFor(host, [](const RuntimeSnapshot& s) { return s.refusals.count(220) != 0; });
  CHECK(refused.refusals.at(220) == "No verified compatibility kit is installed.");
  host.Shutdown();
  CHECK(host.Snapshot().status == RuntimeHostStatus::kStopped);
  CHECK(!fs::exists(fixture.root / "data/runtime-state/installs.json"));
  CHECK(fixture.registry_mutations == 0);
  CHECK(fs::is_empty(fixture.root / "data/homebrew"));
}

// Startup reads durable records only. A journaled record is reported as needing
// recovery; it is not resumed, re-registered or rewritten.
void StartupReportsJournalsWithoutResuming() {
  Fixture fixture;
  auto config = fixture.Config();
  fs::create_directories(config.state_root); CHECK(chmod(config.state_root.c_str(), 0700) == 0);
  {
    playden::Storage storage(config.state_root.string());
    RuntimeInstallRepository repository(storage, "installs.json", [](const std::string&) {
      return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree};
    });
    CHECK(repository.Load().ok());
    const auto record = Record();
    ReserveRequest request;
    request.install_id = record.install_id; request.app_id = record.app_id;
    request.default_profile_id = record.default_profile_id; request.runtime_bundle_id = record.runtime_bundle_id;
    request.recipe_id = record.recipe_id; request.recipe_version = record.recipe_version;
    request.launch_entry_id = record.launch_entry_id; request.launch_path = record.launch_path;
    request.install_path = record.install_path; request.volume_id = record.volume_id;
    request.kit_title_variants = {"FAKE78001"};
    auto reserved = repository.Reserve(request); CHECK(reserved.ok());
    auto journaled = reserved.value();
    journaled.state = PreparationState::kRegistering;
    journaled.journal = PreparationJournal{"11111111-2222-4333-8444-555555555555", "settings-publication", "register"};
    CHECK(repository.Update(journaled).ok());
  }
  const auto before = Read(config.state_root / "installs.json");
  CHECK(!before.empty());
  const auto database_reads_before = fixture.database_reads;
  RuntimeHost host(config);
  CHECK(host.Start());
  const auto started = WaitFor(host, Started);
  CHECK(started.status == RuntimeHostStatus::kReady && started.installs.size() == 1);
  const auto& view = started.installs[0];
  CHECK(view.app_id == 220 && view.title_id == "FAKE78001" && view.state == PreparationState::kRegistering);
  CHECK(view.recovery_pending);
  host.Shutdown();
  CHECK(Read(config.state_root / "installs.json") == before);
  CHECK(fixture.registry_mutations == 0 && fixture.database_reads == database_reads_before);
}

// A public or foreign state root is refused rather than taken over.
void PublicStateRootIsUnavailable() {
  Fixture fixture;
  auto config = fixture.Config();
  fs::create_directories(config.state_root); CHECK(chmod(config.state_root.c_str(), 0755) == 0);
  RuntimeHost host(config);
  CHECK(host.Start());
  const auto started = WaitFor(host, Started);
  CHECK(started.status == RuntimeHostStatus::kUnavailable && !started.reason.empty());
  host.Prepare(Hl2());
  const auto refused = WaitFor(host, [](const RuntimeSnapshot& s) { return s.refusals.count(220) != 0; });
  CHECK(refused.refusals.at(220) == started.reason);
  host.Shutdown();
  struct stat info{};
  CHECK(stat(config.state_root.c_str(), &info) == 0 && (info.st_mode & 0777) == 0755);
}

// An existing download is prepared in place: the host stages its own title,
// copies a private bottle, publishes settings through OWNER, registers once and
// commits Ready. Game files, DLLs and saves are preserved byte for byte.
void PreparesExistingDownloadToReady() {
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
    ++uuids; return std::string("00000000-0000-4000-8000-00000000000") + std::to_string(uuids);
  };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  config.title_icon = [](std::uint32_t app) -> std::optional<std::string> {
    return app == 220 ? std::optional<std::string>("game-icon-png") : std::nullopt;
  };
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/220";
  Write(game / "hl2.exe", "existing-game", 0644);
  Write(game / "bin/steam_api.dll", "unchanged-original-dll", 0644);
  Write(game / "hl2/save/quick.sav", "existing-player-save", 0644);
  CHECK(chmod(game.c_str(), 0777) == 0);  // console downloads are world-writable

  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  host.Prepare(Hl2());
  const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && (s.refusals.count(220) ||
        (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
  }, 60);
  if (done.refusals.count(220)) std::fprintf(stderr, "Prepare refused: %s\n", done.refusals.at(220).c_str());
  CHECK(!done.refusals.count(220));
  CHECK(done.installs.size() == 1);
  const auto& ready = done.installs[0];
  CHECK(ready.state == PreparationState::kReady && ready.title_id == "FAKE78001" && !ready.recovery_pending);
  CHECK(fixture.registry_mutations == 2 && uuids == 1);
  // Game files stay at the recorded download root and are untouched.
  CHECK(Read(game / "hl2.exe") == "existing-game");
  CHECK(Read(game / "bin/steam_api.dll") == "unchanged-original-dll");
  CHECK(Read(game / "hl2/save/quick.sav") == "existing-player-save");
  CHECK(!fs::exists(fixture.root / "data/homebrew/FAKE78001/games"));
  struct stat game_info{};
  CHECK(stat(game.c_str(), &game_info) == 0 && (game_info.st_mode & 07777) == 0755);
  // The staged title is readable by ShadowMount but not writable by others.
  struct stat title_info{};
  for (const auto* relative : {"data/homebrew/FAKE78001", "data/homebrew/FAKE78001/sce_sys"})
    CHECK(stat((fixture.root / relative).c_str(), &title_info) == 0 && (title_info.st_mode & 07777) == 0755);
  // The PS5 title shows the game's own name and icon, not the kit's.
  const auto param = Read(fixture.root / "data/homebrew/FAKE78001/sce_sys/param.json");
  CHECK(param.find(R"("titleName": "Half-Life \"2\"")") != std::string::npos);
  CHECK(param.find("Prospero Win") == std::string::npos && param.find("FAKE78001") != std::string::npos);
  CHECK(Read(fixture.root / "data/homebrew/FAKE78001/sce_sys/icon0.png") == "game-icon-png");
  CHECK(stat((fixture.root / "data/homebrew/FAKE78001/sce_sys/icon0.png").c_str(), &title_info) == 0 &&
        (title_info.st_mode & 07777) == 0644);
  CHECK(done.installs[0].title_current);
  // The descriptor binds the existing root; the bottle is private.
  const auto descriptor = Read(fixture.root / "data/homebrew/FAKE78001/game-launch.json");
  CHECK(descriptor.find(R"("schema_version":2)") != std::string::npos);
  CHECK(descriptor.find(R"("game_root":"/data/homebrew/FAKE77031/games/220")") != std::string::npos);
  struct stat info{};
  CHECK(stat((fixture.root / "data/playden/games/steam-220/library/prefixes/game").c_str(), &info) == 0 &&
        (info.st_mode & 0777) == 0700);
  // The finished publication's request is no longer named by any journal: an
  // explicit cleanup removes it.
  const auto request = fixture.root / "data/runtime-state/requests/settings-request-00000000-0000-4000-8000-000000000001.v1";
  CHECK(fs::is_regular_file(request));
  CHECK(WaitFor(host, [](const RuntimeSnapshot& s) { return s.leftover_requests == 1; }).leftover_requests == 1);
  host.CleanUpRequests();
  WaitFor(host, [](const RuntimeSnapshot& s) { return s.leftover_requests == 0; });
  CHECK(!fs::exists(request));
  // A second request does not redo anything.
  host.Prepare(Hl2());
  const auto again = WaitFor(host, [](const RuntimeSnapshot& s) { return s.refusals.count(220) != 0; });
  CHECK(again.refusals.at(220) == "This game is already prepared.");
  CHECK(fixture.registry_mutations == 2);
  host.Shutdown();
}

// A capture refused after control setup (the console's world-writable game
// folder) leaves a settings-control journal. An explicit Prepare resumes the
// saved request and reaches Ready with one registration.
void ResumesInterruptedCaptureFromSavedRequest(bool tamper_backup = false) {
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
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/220";
  unsigned uuids = 0;
  // Called just before capture: the first attempt sees a world-writable folder.
  config.uuid = [&uuids, game]() -> std::optional<std::string> {
    if (++uuids == 1) CHECK(chmod(game.c_str(), 0777) == 0);
    return std::string("00000000-0000-4000-8000-00000000000") + std::to_string(uuids);
  };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  Write(game / "hl2.exe", "existing-game", 0644);
  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  host.Prepare(Hl2());
  const auto failed = WaitFor(host, [](const RuntimeSnapshot& s) { return !s.preparing_app && s.refusals.count(220); }, 60);
  CHECK(failed.installs.size() == 1 && failed.installs[0].state != PreparationState::kReady);
  CHECK(failed.installs[0].recovery_pending);
  // The saved request the journal names is in use: never leftover, never removed.
  CHECK(failed.leftover_requests == 0);
  host.CleanUpRequests();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(fs::is_regular_file(fixture.root / "data/runtime-state/requests/settings-request-00000000-0000-4000-8000-000000000001.v1"));
  const auto backup = fixture.root / "data/playden/games/steam-220/.playden-control" /
                      "registry-backup-00000000-0000-4000-8000-000000000001.v1";
  CHECK(fs::is_regular_file(backup));
  if (tamper_backup) {  // Same size, different bytes: not this capture's backup.
    auto bytes = Read(backup); bytes[0] = bytes[0] == 'X' ? 'Y' : 'X';
    Write(backup, bytes, 0600);
  }
  const auto before = host.Snapshot().revision;
  host.Prepare(Hl2());
  WaitFor(host, [before](const RuntimeSnapshot& s) { return s.revision > before && !s.refusals.count(220); });
  const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && !s.installs.empty() &&
        (s.installs[0].state == PreparationState::kReady || s.refusals.count(220));
  }, 60);
  if (tamper_backup) {
    CHECK(done.refusals.count(220) && done.refusals.at(220).find("orphan") != std::string::npos);
    CHECK(done.installs[0].state != PreparationState::kReady && fixture.registry_mutations == 0);
    host.Shutdown();
    return;
  }
  if (done.refusals.count(220)) std::fprintf(stderr, "Resume refused: %s\n", done.refusals.at(220).c_str());
  CHECK(done.installs[0].state == PreparationState::kReady && !done.installs[0].recovery_pending);
  CHECK(fixture.registry_mutations == 2 && uuids == 1);
  host.Shutdown();
}

// A respun kit with the same id, runtime and recipe (for example a new wrapper
// build) updates the installed title in place: changed files and owner markers
// only, no new registration, publication or bottle changes. A different kit id
// is refused.
void RefreshesInstalledTitleToRespunKit() {
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
  config.uuid = []() -> std::optional<std::string> { return std::string("00000000-0000-4000-8000-000000000001"); };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/220";
  Write(game / "hl2.exe", "existing-game", 0644);
  {
    const auto kit = SyntheticKit(fixture.root / "kit");
    auto first = config;
    first.admit_kit = [kit](std::string*) { return std::optional<RuntimeKit>(kit); };
    RuntimeHost host(first);
    CHECK(host.Start());
    host.Prepare(Hl2());
    const auto ready = WaitFor(host, [](const RuntimeSnapshot& s) {
      return !s.preparing_app && !s.installs.empty() && s.installs[0].state == PreparationState::kReady;
    }, 60);
    CHECK(ready.installs[0].title_current);
    host.Shutdown();
  }
  const auto bottle_reg = fixture.root / "data/playden/games/steam-220/library/prefixes/game/user.reg";
  const auto registry_before = Read(bottle_reg);
  const auto installs_before = Read(fixture.root / "data/runtime-state/installs.json");
  CHECK(fixture.registry_mutations == 2);
  {  // A different kit id is not a respin; it needs republication.
    const auto other = SyntheticKit(fixture.root / "kit-other", "native", "kit-2", 'b');
    auto second = config;
    second.admit_kit = [other](std::string*) { return std::optional<RuntimeKit>(other); };
    RuntimeHost host(second);
    CHECK(host.Start());
    CHECK(!WaitFor(host, Started).installs[0].title_current);
    host.Prepare(Hl2());
    const auto refused = WaitFor(host, [](const RuntimeSnapshot& s) { return !s.preparing_app && s.refusals.count(220); });
    CHECK(refused.refusals.at(220).find("compatible kit") != std::string::npos);
    host.Shutdown();
  }
  const auto respun = SyntheticKit(fixture.root / "kit-respun", "native-2", "kit-1", 'c');
  auto third = config;
  third.admit_kit = [respun](std::string*) { return std::optional<RuntimeKit>(respun); };
  RuntimeHost host(third);
  CHECK(host.Start());
  CHECK(!WaitFor(host, Started).installs[0].title_current);
  host.Prepare(Hl2());
  const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && !s.installs.empty() && (s.installs[0].title_current || s.refusals.count(220));
  }, 60);
  if (done.refusals.count(220)) std::fprintf(stderr, "Refresh refused: %s\n", done.refusals.at(220).c_str());
  CHECK(!done.refusals.count(220) && done.installs[0].state == PreparationState::kReady);
  CHECK(Read(fixture.root / "data/homebrew/FAKE78001/eboot.bin") == "native-2");
  CHECK(Read(bottle_reg) == registry_before);
  CHECK(Read(fixture.root / "data/runtime-state/installs.json") == installs_before);
  CHECK(fixture.registry_mutations == 2);
  host.Shutdown();

  // A rebuilt runtime (same artifacts, new bytes) upgrades the title in place:
  // the new runtime reaches the title, the record keeps the new hashes, the
  // registration and bottle registry stay.
  const auto rebuilt = SyntheticKit(fixture.root / "kit-rebuilt", "native-2", "kit-1", 'd', "runtime-neutral", "rev-neutral");
  auto fourth = config;
  fourth.admit_kit = [rebuilt](std::string*) { return std::optional<RuntimeKit>(rebuilt); };
  RuntimeHost upgraded(fourth);
  CHECK(upgraded.Start());
  CHECK(!WaitFor(upgraded, Started).installs[0].title_current);
  upgraded.Prepare(Hl2());
  const auto up = WaitFor(upgraded, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && !s.installs.empty() && (s.installs[0].title_current || s.refusals.count(220));
  }, 60);
  if (up.refusals.count(220)) std::fprintf(stderr, "Upgrade refused: %s\n", up.refusals.at(220).c_str());
  CHECK(!up.refusals.count(220) && up.installs[0].state == PreparationState::kReady);
  CHECK(Read(fixture.root / "data/homebrew/FAKE78001/win/wine/lib/core.prx") == "runtime-neutral");
  const auto upgraded_installs = Read(fixture.root / "data/runtime-state/installs.json");
  CHECK(upgraded_installs != installs_before &&
        upgraded_installs.find(*HashArtifactBytes("runtime-neutral")) != std::string::npos);
  CHECK(Read(bottle_reg) == registry_before);
  CHECK(fixture.registry_mutations == 2);
  upgraded.Shutdown();
  // A restart keeps the upgraded title current.
  RuntimeHost again(fourth);
  CHECK(again.Start());
  CHECK(WaitFor(again, Started).installs[0].title_current);
  again.Shutdown();
}

// A game run that leaves the bottle registry wider than admission accepts
// (Wine saving with umask 0) is reported at startup, and Prepare repairs it.
void RepairsBottleRegistryModes() {
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
  config.admit_kit = [kit](std::string*) { return std::optional<RuntimeKit>(kit); };
  config.uuid = []() -> std::optional<std::string> { return std::string("00000000-0000-4000-8000-000000000001"); };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  Write(fixture.root / "data/homebrew/FAKE77031/games/220/hl2.exe", "existing-game", 0644);
  {
    RuntimeHost host(config);
    CHECK(host.Start());
    host.Prepare(Hl2());
    WaitFor(host, [](const RuntimeSnapshot& s) {
      return !s.preparing_app && !s.installs.empty() && s.installs[0].state == PreparationState::kReady;
    }, 60);
    host.Shutdown();
  }
  const auto user_reg = fixture.root / "data/playden/games/steam-220/library/prefixes/game/user.reg";
  CHECK(chmod(user_reg.c_str(), 0666) == 0);
  RuntimeHost host(config);
  CHECK(host.Start());
  const auto started = WaitFor(host, Started);
  CHECK(started.installs[0].admission.find("stage 7") != std::string::npos);
  host.Prepare(Hl2());
  const auto repaired = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && !s.installs.empty() && s.installs[0].admission == "ok";
  }, 60);
  CHECK(!repaired.refusals.count(220));
  struct stat info{};
  CHECK(stat(user_reg.c_str(), &info) == 0 && (info.st_mode & 07777) == 0600);
  CHECK(fixture.registry_mutations == 2);
  host.Shutdown();

  // A title prepared before per-game names keeps the kit's name and icon: it
  // reports an update, and Update gives it the game's own without registering
  // again.
  std::error_code ignored;
  fs::remove_all(fixture.root / "data/playden/games/steam-220/title-metadata", ignored);
  const auto title_param = fixture.root / "data/homebrew/FAKE78001/sce_sys/param.json";
  CHECK(chmod(title_param.c_str(), 0644) == 0);
  Write(title_param, Read(fixture.root / "kit/titles/variant/sce_sys/param.json"), 0644);
  auto named = config;
  named.title_icon = [](std::uint32_t) -> std::optional<std::string> { return std::string("game-icon-png"); };
  RuntimeHost upgraded(named);
  CHECK(upgraded.Start());
  CHECK(!WaitFor(upgraded, Started).installs[0].title_current);
  upgraded.Prepare(Hl2());
  const auto renamed = WaitFor(upgraded, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && !s.installs.empty() && s.installs[0].title_current;
  }, 60);
  CHECK(!renamed.refusals.count(220));
  CHECK(Read(title_param).find("Half-Life") != std::string::npos);
  CHECK(Read(fixture.root / "data/homebrew/FAKE78001/sce_sys/icon0.png") == "game-icon-png");
  CHECK(fixture.registry_mutations == 2);
  // Asking for the store name in titles makes the title out of date; Update
  // renames it, still without registering again.
  upgraded.SetTitleNames({{220, "Half-Life \"2\" (Steam)"}});
  WaitFor(upgraded, [](const RuntimeSnapshot& s) { return !s.installs.empty() && !s.installs[0].title_current; }, 10);
  auto suffixed = Hl2();
  suffixed.title_name = "Half-Life \"2\" (Steam)";
  upgraded.Prepare(suffixed);
  WaitFor(upgraded, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && !s.installs.empty() && s.installs[0].title_current;
  }, 60);
  CHECK(Read(title_param).find(R"(Half-Life \"2\" (Steam))") != std::string::npos);
  CHECK(fixture.registry_mutations == 2);
  upgraded.Shutdown();
}

// Play from Playden: the worker checks the Ready title and prepares a session;
// only Handoff on the application thread starts it, once, with the session
// argument. The wrapper's exit record ends the session and allows the next Play.
void PlaysReadyTitleThroughHandoff() {
  Fixture fixture;
  auto config = fixture.Config();
  config.host_namespace_prefix = fixture.root;
  config.session_data_root = fixture.root / "data";
  config.package_paths.console_data_root = "/data";
  config.package_paths.host_data_root = fixture.root / "data";
  config.package_operations.hash_file = HashArtifactFile;
  config.package_operations.hash_bytes = HashArtifactBytes;
  config.package_operations.free_space_unverifiable = true;
  config.package_operations.validate_bottle = [](const fs::path& bottle) {
    return fs::is_regular_file(bottle / "system.reg") && fs::is_regular_file(bottle / "user.reg");
  };
  const auto kit = SyntheticKit(fixture.root / "kit");
  config.admit_kit = [kit](std::string*) { return std::optional<RuntimeKit>(kit); };
  unsigned uuids = 0;
  config.uuid = [&uuids]() -> std::optional<std::string> {
    ++uuids; return std::string("00000000-0000-4000-8000-00000000000") + std::to_string(uuids);
  };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  std::vector<std::pair<std::string, std::vector<std::string>>> launches;
  config.launch_title = [&launches](const std::string& title, const std::vector<std::string>& arguments) {
    launches.emplace_back(title, arguments);
    return TitleResult{true, 0, "launch"};
  };
  Write(fixture.root / "data/homebrew/FAKE77031/games/220/hl2.exe", "existing-game", 0644);
  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  CHECK(!host.Handoff());  // nothing prepared
  host.Prepare(Hl2());
  WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && !s.installs.empty() && s.installs[0].state == PreparationState::kReady;
  }, 60);

  host.Play(220);
  const auto prepared = WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready; }, 30);
  CHECK(prepared.launch && prepared.launch->app_id == 220 && prepared.launch->title_id == "FAKE78001");
  // Cloud saves under Windows folders use this game's private bottle.
  CHECK(prepared.installs[0].prefix_root == "/data/playden/games/steam-220/library/prefixes/game");
  CHECK(launches.empty());
  // Another thread cannot start the title.
  std::thread other([&host] { CHECK(host.Handoff()); });
  other.join();
  CHECK(launches.empty());
  WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready; }, 30);
  CHECK(host.Handoff());
  CHECK(launches.size() == 1 && launches[0].first == "FAKE78001");
  const std::string session = "00000000-0000-4000-8000-000000000002";
  CHECK(launches[0].second == std::vector<std::string>{"session=" + session});
  CHECK(!host.Handoff());
  CHECK(host.Snapshot().launch->state == LaunchState::kLaunching);
  // A second Play while the session is undecided is refused and stays visible
  // for that game; the pending session is untouched.
  const auto handled = host.Snapshot().plays_handled;
  host.Play(220);
  const auto refused = WaitFor(host, [handled](const RuntimeSnapshot& s) { return s.plays_handled > handled; }, 30);
  CHECK(refused.play_refusals.count(220) && refused.launch && refused.launch->state == LaunchState::kLaunching);
  std::this_thread::sleep_for(std::chrono::milliseconds(2500));  // one more poll
  CHECK(host.Snapshot().play_refusals.count(220) && launches.size() == 1);

  // The game cannot be updated or repaired while its session is undecided.
  host.Prepare(Hl2());
  const auto held = WaitFor(host, [](const RuntimeSnapshot& s) { return s.refusals.count(220) != 0; }, 30);
  CHECK(held.refusals.at(220).find("Close the game") != std::string::npos);
  // The wrapper records the session in the private game data root.
  const auto sessions = fixture.root / "data/playden/games/steam-220/logs/sessions";
  fs::create_directories(sessions);
  CHECK(chmod((fixture.root / "data/playden/games/steam-220/logs").c_str(), 0700) == 0);
  CHECK(chmod(sessions.c_str(), 0700) == 0);
  const auto record = [&](const std::string& id, const std::string& phase) {
    return R"({"schema_version":1,"session_id":")" + id +
           R"(","install_id":"steam-220","title_id":"FAKE78001","origin":"playden","settings_revision":0,)"
           R"("runtime_bundle_id":"wine-wow64","phase":")" + phase + "\"}\n";
  };
  // Many earlier Home sessions do not hide this one.
  for (int i = 0; i < 300; ++i) {
    char id[37];
    std::snprintf(id, sizeof(id), "11111111-1111-4111-8111-%012d", i);
    Write(sessions / (std::string(id) + ".json"), record(id, "exited"), 0600);
  }
  Write(sessions / (session + ".json"), record(session, "running"), 0600);
  WaitFor(host, [](const RuntimeSnapshot& s) { return s.launch && s.launch->state == LaunchState::kRunning; }, 30);
  Write(sessions / (session + ".json"), record(session, "exited"), 0600);
  WaitFor(host, [](const RuntimeSnapshot& s) { return s.launch && s.launch->state == LaunchState::kExited; }, 30);
  // The ended session no longer blocks Play, and an accepted Play clears the
  // earlier refusal.
  host.Play(220);
  WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready; }, 30);
  CHECK(host.Handoff());
  CHECK(launches.size() == 2 && launches[1].second[0] != launches[0].second[0]);
  CHECK(!host.Snapshot().play_refusals.count(220));
  // The system closes Playden when the game starts. If the game is then closed
  // from the system menu, its record still says running; the restarted host
  // ends that session once the title is seen stopped, and Play works again.
  const std::string third = "00000000-0000-4000-8000-000000000003";
  CHECK(launches[1].second[0] == "session=" + third);
  host.Shutdown();
  Write(sessions / (third + ".json"), record(third, "running"), 0600);
  RuntimeHost restarted(config);
  CHECK(restarted.Start());
  const auto ended = WaitFor(restarted, [](const RuntimeSnapshot& s) {
    return s.launch && s.launch->state == LaunchState::kExited;
  }, 30);
  CHECK(ended.launch->code == "title-ended-without-record");
  restarted.Play(220);
  WaitFor(restarted, [](const RuntimeSnapshot& s) { return s.handoff_ready; }, 30);
  CHECK(restarted.Handoff() && launches.size() == 3);
  // Evidence that does not belong to this session fails it, but while the title
  // runs the session stays pending and Play stays refused.
  const std::string fourth = "00000000-0000-4000-8000-000000000004";
  CHECK(launches[2].second[0] == "session=" + fourth);
  fixture.activity->store(static_cast<int>(NativeActivity::kActive));
  // Cloud asks the console through the worker: a running title answers active,
  // and every answer carries a newer serial.
  const auto serial = restarted.Snapshot().activity_serial;
  restarted.CheckActivity(220);
  const auto checked = WaitFor(restarted, [serial](const RuntimeSnapshot& s) { return s.activity_serial > serial; }, 10);
  CHECK(checked.activity.at(220).first == NativeActivity::kActive && checked.activity.at(220).second > serial);
  restarted.CheckActivity(999);  // no prepared title: nothing of it can run
  const auto unprepared = WaitFor(restarted, [](const RuntimeSnapshot& s) { return s.activity.count(999) != 0; }, 10);
  CHECK(unprepared.activity.at(999).first == NativeActivity::kStopped);
  auto foreign = record(fourth, "running");
  foreign.replace(foreign.find("\"playden\""), 9, "\"home\"");
  Write(sessions / (fourth + ".json"), foreign, 0600);
  WaitFor(restarted, [](const RuntimeSnapshot& s) { return s.launch && s.launch->state == LaunchState::kFailed; }, 30);
  std::this_thread::sleep_for(std::chrono::milliseconds(2500));
  const auto handled_before = restarted.Snapshot().plays_handled;
  restarted.Play(220);
  const auto refused_while_alive = WaitFor(restarted, [handled_before](const RuntimeSnapshot& s) {
    return s.plays_handled > handled_before;
  }, 30);
  CHECK(refused_while_alive.play_refusals.count(220) && launches.size() == 3);
  // Once the title is seen stopped, the failed session ends and Play works again.
  fixture.activity->store(static_cast<int>(NativeActivity::kStopped));
  std::this_thread::sleep_for(std::chrono::milliseconds(2500));
  restarted.Play(220);
  WaitFor(restarted, [](const RuntimeSnapshot& s) { return s.handoff_ready; }, 30);
  CHECK(restarted.Handoff() && launches.size() == 4);
  restarted.Shutdown();
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  g_runtime_dir = argv[1];
  NoKitRefusesPreparationWithoutDurableEffects();
  StartupReportsJournalsWithoutResuming();
  PublicStateRootIsUnavailable();
  PreparesExistingDownloadToReady();
  ResumesInterruptedCaptureFromSavedRequest();
  ResumesInterruptedCaptureFromSavedRequest(true);
  RefreshesInstalledTitleToRespunKit();
  RepairsBottleRegistryModes();
  PlaysReadyTitleThroughHandoff();
  std::puts("runtime host tests passed");
  return 0;
}
