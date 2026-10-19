#include "homebrew/runtime_host.hpp"
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
  admitted.capabilities = {"dxvk", "game-root-unowned", "offline_bottle", "per_game_title", "wow64_i386"};
  return admitted;
}
// A kit before 0.10: game folders must be the app user's and 0700/0755, except on drives
// that record no owners, which it can't start games from.
RuntimeKit WithoutUnownedRoots(RuntimeKit kit) {
  kit.capabilities.erase(std::remove(kit.capabilities.begin(), kit.capabilities.end(), "game-root-unowned"),
                         kit.capabilities.end());
  return kit;
}
// The same kit with one shell for every game: its own param.json names the template title.
RuntimeKit AnyTitleKit(const fs::path& kit) {
  auto admitted = SyntheticKit(kit);
  const auto param = kit / "titles/variant/sce_sys/param.json";
  Write(param,
        "{\n  \"conceptId\": \"78000\",\n  \"contentId\": \"UP9000-FAKE78000_00-PROSPEROWIN00001\",\n"
        "  \"localizedParameters\": {\n    \"en-US\": {\n      \"titleName\": \"Prospero Win\"\n    }\n  },\n"
        "  \"titleId\": \"FAKE78000\"\n}\n", 0644);
  for (auto& file : admitted.view.files)
    if (file.path == "titles/variant/sce_sys/param.json") {
      file.size = fs::file_size(param);
      file.sha256 = *HashArtifactFile(param);
    }
  admitted.view.variants = {{"any-title", "FAKE78000", "titles/variant", true}};
  admitted.capabilities.push_back("title-any-id");
  return admitted;
}
}  // namespace

// One shell for every game: the game gets a new title of its own (the lowest free FAKE78xxx),
// stamped with its own title ID, and its helper's allow-list names it before the title exists.
// Without a way to write that list nothing is prepared.
void AnyTitleKitMakesTheGameItsOwnTitle() {
  for (const int scenario : {0, 1, 2}) {
    const bool writer = scenario != 0;
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
    const auto kit = AnyTitleKit(fixture.root / "kit");
    config.admit_kit = [&kit](std::string*) { return std::optional<RuntimeKit>(kit); };
    unsigned uuids = 0;
    config.uuid = [&uuids]() -> std::optional<std::string> {
      ++uuids; return std::string("00000000-0000-4000-8000-00000000000") + std::to_string(uuids);
    };
    config.unix_time = [] { return std::int64_t{1760000000}; };
    std::vector<std::string> lists;
    if (writer)
      config.write_title_allow_list = [&lists, scenario](const std::string& contents, std::string*) {
        lists.push_back(contents);
        return scenario != 2 || lists.size() != 2;
      };
    Write(fixture.root / "data/homebrew/FAKE77031/games/220/hl2.exe", "existing-game", 0644);
    RuntimeHost host(config);
    CHECK(host.Start());
    CHECK(WaitFor(host, Started).kit_available);
    host.Prepare(Hl2());
    auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
      return !s.preparing_app && (s.refusals.count(220) ||
          (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
    }, 60);
    if (scenario == 2) {
      CHECK(done.refusals.count(220));
      host.Prepare(Hl2());
      done = WaitFor(host, [](const RuntimeSnapshot& s) {
        return !s.preparing_app && !s.installs.empty() && s.installs[0].state == PreparationState::kReady;
      }, 60);
    }
    if (!writer) {
      CHECK(done.refusals.count(220) && done.refusals.at(220) == "This console can't let new PS5 titles start.");
      CHECK(!fs::exists(fixture.root / "data/homebrew/FAKE78001"));
      host.Shutdown();
      continue;
    }
    if (done.refusals.count(220)) std::fprintf(stderr, "Prepare refused: %s\n", done.refusals.at(220).c_str());
    CHECK(!done.refusals.count(220) && done.installs.size() == 1);
    CHECK(done.installs[0].state == PreparationState::kReady && done.installs[0].title_id == "FAKE78001");
    // Startup wrote the (empty) list; the reservation listed the new title before staging.
    CHECK(lists.size() == (scenario == 2 ? 3u : 2u) && lists[0].empty() && lists.back() == "FAKE78001\n");
    const auto param = Read(fixture.root / "data/homebrew/FAKE78001/sce_sys/param.json");
    CHECK(param.find(R"("titleId": "FAKE78001")") != std::string::npos);
    CHECK(param.find(R"("conceptId": "78001")") != std::string::npos);
    CHECK(param.find(R"("contentId": "UP9000-FAKE78001_00-PROSPEROWIN00001")") != std::string::npos);
    CHECK(param.find("FAKE78000") == std::string::npos && param.find(R"("titleName": "Half-Life \"2\"")") != std::string::npos);
    CHECK(Read(fixture.root / "data/homebrew/FAKE78001/eboot.bin") == "native");
    CHECK(done.installs[0].title_current && done.installs[0].admission == "ok");
    host.Shutdown();
    std::atomic<bool> list_writable{false};
    std::atomic<unsigned> retries{0};
    config.write_title_allow_list = [&](const std::string&, std::string*) { ++retries; return list_writable.load(); };
    RuntimeHost restarted(config);
    CHECK(restarted.Start());
    WaitFor(restarted, Started);
    restarted.Prepare(Hl2());
    const auto refused = WaitFor(restarted, [](const RuntimeSnapshot& s) { return s.refusals.count(220); });
    CHECK(retries >= 2);
    CHECK(refused.refusals.at(220) != "This game is already prepared.");
    list_writable = true;
    restarted.Prepare(Hl2());
    WaitFor(restarted, [](const RuntimeSnapshot& s) {
      return !s.preparing_app && s.refusals.count(220) && s.refusals.at(220) == "This game is already prepared.";
    });
    restarted.Shutdown();
  }
}

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

// Root is process-wide on the console: while the worker starts up or works through a batch it holds
// the effective UID, so a root raise elsewhere (an update's cleanup) can't fail its ownership checks.
// Waiting for work, it holds nothing.
void HoldsEffectiveUidWhileWorking() {
  Fixture fixture;
  auto config = fixture.Config();
  std::atomic<int> holds{0};
  std::atomic<bool> admitted_while_held{false};
  config.hold_effective_uid = [&holds]() -> std::shared_ptr<void> {
    ++holds;
    return std::shared_ptr<void>(nullptr, [&holds](void*) { --holds; });
  };
  config.admit_kit = [&](std::string* reason) -> std::optional<RuntimeKit> {
    admitted_while_held = holds > 0;
    *reason = "No verified compatibility kit is installed.";
    return std::nullopt;
  };
  RuntimeHost host(std::move(config));
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).status == RuntimeHostStatus::kReady);
  CHECK(admitted_while_held);
  host.Prepare(Hl2());
  CHECK(WaitFor(host, [](const RuntimeSnapshot& s) { return s.refusals.count(220) != 0; }).refusals.count(220));
  for (int i = 0; i < 200 && holds != 0; ++i) usleep(5000);
  CHECK(holds == 0);  // idle: root may be raised again
  host.Shutdown();
}

// A preparation asked for before the runtime is ready is published as queued until the worker
// takes it, so Play and Recreate bottle wait instead of reporting it lost.
void QueuedPreparationIsPublishedUntilTaken() {
  Fixture fixture;
  RuntimeHost host(fixture.Config());
  host.Prepare(Hl2());
  CHECK(host.Snapshot().queued_prepares.count(220) == 1);
  CHECK(host.Start());
  const auto refused = WaitFor(host, [](const RuntimeSnapshot& s) { return s.refusals.count(220) != 0; });
  CHECK(refused.queued_prepares.empty() && !refused.preparing_app);
  host.Shutdown();
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

// ShadowMountPlus registers a submitted title only once no title runs (Playden
// included): preparation ends as soon as the registration is submitted, with the
// game waiting for the console and no refusal. A later check only asks the console:
// once the title is registered it becomes Ready, never submitted again.
void PreparationEndsAwaitingRegistration() {
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
    ++uuids; return std::string("00000000-0000-4000-8000-00000000000") + std::to_string(uuids);
  };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  Write(fixture.root / "data/homebrew/FAKE77031/games/220/hl2.exe", "existing-game", 0644);
  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  const auto began = std::chrono::steady_clock::now();
  host.Prepare(Hl2());
  const auto waiting = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && !s.installs.empty() && (s.refusals.count(220) || s.installs[0].awaiting_registration);
  }, 60);
  if (waiting.refusals.count(220)) std::fprintf(stderr, "Prepare refused: %s\n", waiting.refusals.at(220).c_str());
  CHECK(!waiting.refusals.count(220) && waiting.installs[0].awaiting_registration);
  CHECK(waiting.installs[0].state != PreparationState::kReady && waiting.installs[0].recovery_pending);
  CHECK(std::chrono::steady_clock::now() - began < std::chrono::seconds(20));  // no polling for the console
  const auto submissions = fixture.registry_mutations;
  CHECK(submissions > 0 && fixture.submitted && !fixture.registered);

  // Asked again while the console still has not registered it: still waiting, nothing resubmitted.
  host.Prepare(Hl2());
  auto again = WaitFor(host, [](const RuntimeSnapshot& s) { return s.preparing_app.has_value(); });
  again = WaitFor(host, [](const RuntimeSnapshot& s) { return !s.preparing_app; }, 30);
  CHECK(!again.refusals.count(220) && again.installs[0].awaiting_registration);
  CHECK(fixture.registry_mutations == submissions);

  // The player left Playden and ShadowMountPlus registered it: the check finds it Ready.
  fixture.registered = true;
  host.Prepare(Hl2());
  const auto ready = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && (s.refusals.count(220) ||
        (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
  }, 30);
  CHECK(!ready.refusals.count(220) && ready.installs[0].state == PreparationState::kReady);
  CHECK(!ready.installs[0].awaiting_registration && !ready.installs[0].recovery_pending);
  CHECK(fixture.registry_mutations == submissions);
  host.Shutdown();
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
  // The game folder stays world-writable (mods over FTP): the kit admits it as it is.
  struct stat game_info{};
  CHECK(stat(game.c_str(), &game_info) == 0 && (game_info.st_mode & 07777) == 0777);
  const auto metadata = Read(fixture.root / "data/playden/games/steam-220/library/profiles/.playden-publication");
  CHECK(metadata.find("schema=3\n") != std::string::npos && metadata.find("game_root.unowned=1\n") != std::string::npos);
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

// A folder made over FTP belongs to root and is 0755, so the app user can't write into it
// (#70): Prepare makes it 0777 as root first. Here 0555 stands in for it.
RuntimeHostConfig WritableFoldersConfig(Fixture& fixture, const RuntimeKit& kit) {
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
  config.admit_kit = [&kit](std::string*) { return std::optional<RuntimeKit>(kit); };
  config.uuid = [] { return std::optional<std::string>("00000000-0000-4000-8000-000000000001"); };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  config.writable_folders = {"/data/homebrew", "/data/playden", "/data/playden-games", "/data/playden/user",
                             "/data/playden/logs"};
  return config;
}

void PrepareFixesRootOwnedHomebrewFolder() {
  if (geteuid() == 0) return;  // root writes anywhere
  Fixture fixture;
  const auto kit = SyntheticKit(fixture.root / "kit");
  auto config = WritableFoldersConfig(fixture, kit);
  std::atomic<int> raises{0};
  config.as_root = [&raises](const std::function<bool()>& work) { ++raises; return work(); };
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/220";
  Write(game / "hl2.exe", "existing-game", 0644);
  const auto homebrew = fixture.root / "data/homebrew";
  CHECK(chmod(homebrew.c_str(), 0555) == 0);

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
  CHECK(done.installs.size() == 1 && done.installs[0].state == PreparationState::kReady);
  CHECK(raises == 1);  // only the folder that failed its probe
  struct stat info{};
  CHECK(stat(homebrew.c_str(), &info) == 0 && (info.st_mode & 07777) == 0777);
  CHECK(fs::is_regular_file(fixture.root / "data/homebrew/FAKE78001/game-launch.json"));
  host.Shutdown();
}

void PrepareNamesUnfixableFolder() {
  if (geteuid() == 0) return;  // root writes anywhere
  Fixture fixture;
  const auto kit = SyntheticKit(fixture.root / "kit");
  auto config = WritableFoldersConfig(fixture, kit);
  std::atomic<int> raises{0};
  config.as_root = [&raises](const std::function<bool()>&) { ++raises; return false; };  // root refused
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/220";
  Write(game / "hl2.exe", "existing-game", 0644);
  const auto homebrew = fixture.root / "data/homebrew";
  CHECK(chmod(homebrew.c_str(), 0555) == 0);

  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  host.Prepare(Hl2());
  const auto done = WaitFor(host, [](const RuntimeSnapshot& s) { return !s.preparing_app && s.refusals.count(220); }, 60);
  CHECK(done.refusals.at(220) == "Playden can't write to " + homebrew.string() +
                                 ". Make the folder writable over FTP (permissions 777), then try again.");
  CHECK(raises == 1);
  CHECK(done.installs.empty());
  struct stat info{};
  CHECK(stat(homebrew.c_str(), &info) == 0 && (info.st_mode & 07777) == 0555);
  CHECK(!fs::exists(fixture.root / "data/homebrew/FAKE78001"));
  CHECK(!fs::exists(fixture.root / "data/playden"));
  CHECK(fixture.registry_mutations == 0);
  host.Shutdown();
  CHECK(chmod(homebrew.c_str(), 0755) == 0);  // so the fixture can clean up
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
  const auto staged = fixture.root / "data/homebrew/FAKE78001";
  unsigned uuids = 0;
  // Called just before capture: the first attempt sees a world-writable PS5 title folder,
  // which the capture's preflight refuses (game folders may be anything).
  config.uuid = [&uuids, staged]() -> std::optional<std::string> {
    if (++uuids == 1) CHECK(chmod(staged.c_str(), 0777) == 0);
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
  CHECK(chmod(staged.c_str(), 0755) == 0);
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
  if (!tamper_backup) {
    // While the console cannot tell whether the title runs, the resume stops before it
    // rebuilds a title folder the player deleted (ShadowMount would put it back on Home).
    CHECK(failed.installs[0].state == PreparationState::kPreparing ||
          failed.installs[0].state == PreparationState::kRegistering);
    const auto title = fixture.root / "data/homebrew/FAKE78001", aside = fixture.root / "title-aside";
    fs::rename(title, aside);
    fixture.activity->store(static_cast<int>(NativeActivity::kUnknown));
    const auto asked = host.Snapshot().revision;
    host.Prepare(Hl2());
    WaitFor(host, [asked](const RuntimeSnapshot& s) {
      return s.revision > asked && !s.preparing_app && s.refusals.count(220) &&
             s.refusals.at(220).find("Restart the PS5") != std::string::npos;
    });
    CHECK(!fs::exists(title) && fixture.registry_mutations == 0);
    fs::rename(aside, title);
    fixture.activity->store(static_cast<int>(NativeActivity::kStopped));
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

// A preparation that failed under one kit and is retried under a rebuilt kit binds
// the new kit's artifacts, so validation compares the title with the kit it came from.
void RetryAfterKitRespinBindsTheNewArtifacts() {
  Fixture fixture;
  auto config = fixture.Config();
  config.host_namespace_prefix = fixture.root;
  config.package_paths.console_data_root = "/data";
  config.package_paths.host_data_root = fixture.root / "data";
  config.package_operations.hash_file = HashArtifactFile;
  config.package_operations.hash_bytes = HashArtifactBytes;
  config.package_operations.validate_bottle = [](const fs::path& bottle) {
    return fs::is_regular_file(bottle / "system.reg") && fs::is_regular_file(bottle / "user.reg");
  };
  config.uuid = []() -> std::optional<std::string> { return std::string("00000000-0000-4000-8000-000000000001"); };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  Write(fixture.root / "data/homebrew/FAKE77031/games/220/hl2.exe", "existing-game", 0644);
  {
    const auto kit = SyntheticKit(fixture.root / "kit");
    auto first = config;
    first.admit_kit = [kit](std::string*) { return std::optional<RuntimeKit>(kit); };
    first.package_operations.free_bytes = [](const fs::path&) { return std::optional<std::uint64_t>(0); };
    RuntimeHost host(first);
    CHECK(host.Start());
    host.Prepare(Hl2());
    const auto failed = WaitFor(host, [](const RuntimeSnapshot& s) {
      return !s.preparing_app && !s.installs.empty() && s.installs[0].state == PreparationState::kFailed;
    }, 60);
    CHECK(failed.installs[0].state == PreparationState::kFailed);
    host.Shutdown();
  }
  const auto rebuilt = SyntheticKit(fixture.root / "kit-rebuilt", "native", "kit-1", 'd', "runtime-neutral", "rev-neutral");
  auto second = config;
  second.admit_kit = [rebuilt](std::string*) { return std::optional<RuntimeKit>(rebuilt); };
  second.package_operations.free_space_unverifiable = true;
  RuntimeHost host(second);
  CHECK(host.Start());
  WaitFor(host, Started);
  host.Prepare(Hl2());
  const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && !s.installs.empty() &&
           (s.installs[0].state == PreparationState::kReady || s.refusals.count(220) ||
            (s.installs[0].state == PreparationState::kFailed &&
             s.installs[0].failure.find("free space") == std::string::npos));
  }, 60);
  if (done.refusals.count(220)) std::fprintf(stderr, "Retry refused: %s\n", done.refusals.at(220).c_str());
  if (done.installs[0].state != PreparationState::kReady)
    std::fprintf(stderr, "Retry state=%d step=%s failure=%s\n", static_cast<int>(done.installs[0].state),
                 done.installs[0].step.c_str(), done.installs[0].failure.c_str());
  CHECK(done.installs[0].state == PreparationState::kReady);
  CHECK(Read(fixture.root / "data/homebrew/FAKE78001/win/wine/lib/core.prx") == "runtime-neutral");
  CHECK(Read(fixture.root / "data/runtime-state/installs.json").find(*HashArtifactBytes("runtime-neutral")) !=
        std::string::npos);
  host.Shutdown();
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

  // A kit update adds media metadata after this bottle was made. The next Play merges it before handoff.
  const auto media = kit.view.root / kit.view.runtime.path / "share/wine/media-build/media-registry.json";
  const auto hive = fixture.root / "data/playden/games/steam-220/library/prefixes/game/system.reg";
  CHECK(Read(hive).find("winegstreamer") == std::string::npos);
  Write(media, R"({"schema":"playden-media-registry/1","values":[
    {"key":"Software\\Classes\\CLSID\\media\\InprocServer32","name":"","data":"\"winegstreamer.dll\""},
    {"key":"Software\\Classes\\Wow6432Node\\CLSID\\media\\InprocServer32","name":"","data":"\"winegstreamer.dll\""}]})", 0644);
  // The bottle predates the media DLLs as well; the same Play copies them in.
  const auto windows = fixture.root / "data/playden/games/steam-220/library/prefixes/game/drive_c/windows";
  for (const auto* arch : {"x86_64-windows", "i386-windows"})
    Write(kit.view.root / kit.view.runtime.path / "lib/wine" / arch / "winegstreamer.dll", std::string("dll ") + arch, 0644);
  for (const auto* folder : {"system32", "syswow64"}) std::filesystem::create_directories(windows / folder);

  host.Play(220);
  const auto prepared = WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready; }, 30);
  const auto upgraded_hive = Read(hive);
  CHECK(upgraded_hive.find("@=\"winegstreamer.dll\"") != std::string::npos &&
        upgraded_hive.find("Wow6432Node") != std::string::npos);
  CHECK(Read(hive.string() + ".playden-backup") == "WINE REGISTRY Version 2\n");
  CHECK(Read(windows / "system32/winegstreamer.dll") == "dll x86_64-windows");
  CHECK(Read(windows / "syswow64/winegstreamer.dll") == "dll i386-windows");
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
  CHECK(Read(hive) == upgraded_hive);
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
  // Another thread (the Cloud worker) can wait for a fresh answer.
  CHECK(restarted.ActivityNow(220, std::chrono::seconds(10)) == NativeActivity::kActive);
  CHECK(restarted.ActivityNow(999, std::chrono::seconds(10)) == NativeActivity::kStopped);
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

// A game Playden ships no recipe for is prepared from Steam's launch options; the
// generated recipe is saved and read back after a restart.
// A kit replaced while Playden runs (an upload with --replace) leaves the admitted paths behind:
// Play, preparation and maintenance say to restart Playden instead of failing on a missing file.
void ReplacedKitAsksForRestart() {
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
  auto kit = SyntheticKit(fixture.root / "kit");
  Write(fixture.root / "kit/manifest.json", "{\"kit\":\"first\"}", 0644);
  kit.manifest_sha256 = *HashArtifactFile(fixture.root / "kit/manifest.json");
  config.admit_kit = [kit](std::string*) { return std::optional<RuntimeKit>(kit); };
  unsigned uuids = 0;
  config.uuid = [&uuids]() -> std::optional<std::string> {
    ++uuids; return std::string("00000000-0000-4000-8000-00000000000") + std::to_string(uuids);
  };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  std::vector<std::string> launches;
  config.launch_title = [&launches](const std::string& title, const std::vector<std::string>&) {
    launches.push_back(title);
    return TitleResult{true, 0, "launch"};
  };
  Write(fixture.root / "data/homebrew/FAKE77031/games/220/hl2.exe", "existing-game", 0644);
  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  host.Prepare(Hl2());
  WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && !s.installs.empty() && s.installs[0].state == PreparationState::kReady;
  }, 60);

  Write(fixture.root / "kit/manifest.json", "{\"kit\":\"second\"}", 0644);
  const auto handled = host.Snapshot().plays_handled;
  host.Play(220);
  const auto refused = WaitFor(host, [handled](const RuntimeSnapshot& s) { return s.plays_handled > handled; }, 30);
  CHECK(refused.play_refusals.count(220) &&
        refused.play_refusals.at(220).find("Quit Playden and start it again") != std::string::npos);
  CHECK(!refused.handoff_ready && launches.empty());
  host.Prepare(Hl2());
  const auto held = WaitFor(host, [](const RuntimeSnapshot& s) { return s.refusals.count(220) != 0; }, 30);
  CHECK(held.refusals.at(220).find("changed while Playden was running") != std::string::npos);
}

void PreparesGameWithoutShippedRecipe() {
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
  config.session_data_root = fixture.root / "data";
  std::vector<std::pair<std::string, std::vector<std::string>>> launches;
  config.launch_title = [&launches](const std::string& title, const std::vector<std::string>& arguments) {
    launches.emplace_back(title, arguments);
    return TitleResult{true, 0, "launch"};
  };
  std::map<std::string, std::string> title_logs;
  config.title_log = [&title_logs](const std::string& title, const std::string& text) { title_logs[title] = text; };
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/4242";
  // A 64-bit PE header is all the generator reads.
  std::string pe(0x80 + 26, '\0');
  pe[0] = 'M'; pe[1] = 'Z'; pe[0x3c] = 0x40; pe[0x40] = 'P'; pe[0x41] = 'E';
  pe[0x44] = 0x64; pe[0x45] = static_cast<char>(0x86); pe[0x40 + 24] = 0x0b; pe[0x40 + 25] = 0x02;
  Write(game / "Bin/Game.exe", pe, 0644);
  // The game's own Steam library (a 64-bit PE that asks for one interface).
  const std::string own_api = pe + std::string("\0SteamUser021\0", 14);
  Write(game / "Bin/steam_api64.dll", own_api, 0644);
  Write(game / ".playden-steam", "depots=4243\ndlc=4250\n", 0644);
  std::map<std::string, std::string> saved_settings;
  config.settings_record = [&saved_settings](std::uint32_t) -> std::optional<GameSettingsRecord> {
    if (saved_settings.empty()) return std::nullopt;
    GameSettingsRecord record;
    record.overrides = saved_settings;
    return record;
  };
  CHECK(chmod(game.c_str(), 0755) == 0);
  RuntimePrepareRequest request{4242, "/data/homebrew/FAKE77031/games/4242", "internal", "Example Game"};

  RuntimeHost host(config);
  host.SetSteamAccount("player", 76561198000000001ull);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  // Without Steam's launch options there is nothing to start.
  request.launch_reason = "Steam details are unavailable right now.";
  host.Prepare(request);
  auto refused = WaitFor(host, [](const RuntimeSnapshot& s) { return s.refusals.count(4242) != 0; });
  CHECK(refused.refusals.at(4242) == "Steam details are unavailable right now.");
  CHECK(refused.installs.empty() && fixture.registry_mutations == 0);

  request.launch_reason.clear();
  request.launches = {{"", "", "bin\\game.exe", "", "-windowed"}};
  host.Prepare(request);
  const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && ((s.refusals.count(4242) && s.refusals.at(4242) != "Steam details are unavailable right now.") ||
           (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
  }, 60);
  if (done.refusals.count(4242)) std::fprintf(stderr, "Prepare refused: %s\n", done.refusals.at(4242).c_str());
  CHECK(done.installs.size() == 1 && done.installs[0].state == PreparationState::kReady);
  CHECK(done.generated_recipes.size() == 1 && done.generated_recipes[0].id == "steam-4242-auto");
  CHECK(done.generated_recipes[0].architecture == "pe64");
  CHECK(done.generated_recipes[0].launch_entries[0].executable == "Bin/Game.exe");
  CHECK(fs::is_regular_file(fixture.root / "data/runtime-state/recipe-steam-4242-auto.json"));
  const auto descriptor = Read(fixture.root / "data/homebrew/FAKE78001/game-launch.json");
  CHECK(descriptor.find("steam-4242-auto") != std::string::npos);
  // Steamworks: the emulator replaced the game's library, which is kept, and steam_settings/ was written.
  const auto emulator = Read(fs::path(g_runtime_dir) / "steamworks/steam_api64.dll");
  CHECK(Read(game / "Bin/steam_api64.dll") == emulator && Read(game / "Bin/steam_api64.dll.orig") == own_api);
  CHECK(Read(game / "Bin/steam_settings/configs.user.ini").find("account_name=player\n") != std::string::npos);
  CHECK(Read(game / "Bin/steam_settings/configs.app.ini").find("4250=dlc4250\n") != std::string::npos);
  CHECK(Read(game / "Bin/steam_settings/steam_interfaces.txt") == "SteamUser021\n");
  // The bottle's owner marker names the generated recipe, so the game's sessions are read.
  const auto marker = Read(fixture.root / "data/playden/games/steam-4242/library/prefixes/game/.playden-template-owner");
  CHECK(marker.find("\nrecipe_id=steam-4242-auto\n") != std::string::npos);
  // The player asks for the game's own Steam files: Play puts them back before the game starts.
  saved_settings = {{"steam_features", "game"}};
  host.Play(4242);
  WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready; }, 30);
  CHECK(host.Handoff() && launches.size() == 1 && launches[0].first == "FAKE78001");
  CHECK(Read(game / "Bin/steam_api64.dll") == own_api);
  const auto session = launches[0].second[0].substr(std::strlen("session="));
  const auto sessions = fixture.root / "data/playden/games/steam-4242/logs/sessions";
  fs::create_directories(sessions);
  CHECK(chmod((fixture.root / "data/playden/games/steam-4242/logs").c_str(), 0700) == 0);
  CHECK(chmod(sessions.c_str(), 0700) == 0);
  const auto record = [&](const std::string& phase) {
    return R"({"schema_version":1,"session_id":")" + session +
           R"(","install_id":"steam-4242","title_id":"FAKE78001","origin":"playden","settings_revision":0,)"
           R"("runtime_bundle_id":"wine-wow64","phase":")" + phase + "\"}\n";
  };
  Write(sessions / (session + ".json"), record("running"), 0600);
  WaitFor(host, [](const RuntimeSnapshot& s) { return s.launch && s.launch->state == LaunchState::kRunning; }, 30);
  Write(sessions / (session + ".json"), record("exited"), 0600);
  const auto ended = WaitFor(host, [](const RuntimeSnapshot& s) {
    return s.launch && s.launch->state != LaunchState::kRunning && s.launch->state != LaunchState::kLaunching;
  }, 30);
  CHECK(ended.launch->state == LaunchState::kExited);
  host.Shutdown();

  // After a restart the saved recipe is found without Steam, and the title log carries the
  // session report followed by the engine's own log from the bottle.
  const auto data = fixture.root / "data/playden/games/steam-4242";
  Write(data / "library/logs/session-0.log", "PW_REPORT/1 stopped\n", 0600);
  Write(data / "library/prefixes/game/drive_c/users/prospero/AppData/LocalLow/Studio/Game/Player.log",
        "NullReferenceException at Boot\n", 0600);
  RuntimeHost restarted(config);
  CHECK(restarted.Start());
  const auto reloaded = WaitFor(restarted, [](const RuntimeSnapshot& s) { return Started(s) && !s.generated_recipes.empty(); });
  CHECK(reloaded.generated_recipes[0].id == "steam-4242-auto");
  CHECK(title_logs.count("FAKE78001"));
  const auto& title_log = title_logs.at("FAKE78001");
  CHECK(title_log.find("== session-0.log\nPW_REPORT/1 stopped") != std::string::npos);
  CHECK(title_log.find("AppData/LocalLow/Studio/Game/Player.log\nNullReferenceException at Boot") != std::string::npos);
  restarted.Prepare({4242, "/data/homebrew/FAKE77031/games/4242", "internal", "Example Game"});
  const auto prepared = WaitFor(restarted, [](const RuntimeSnapshot& s) { return s.refusals.count(4242) != 0; });
  CHECK(prepared.refusals.at(4242) == "This game is already prepared.");
  restarted.Shutdown();

  // Unity 5 writes output_log.txt beside the program instead; the newer log is the one shown.
  Write(game / "Bin/Game_Data/output_log.txt", "Initialize engine version: 5.3.6p5\n", 0600);
  std::filesystem::last_write_time(game / "Bin/Game_Data/output_log.txt",
                                   std::filesystem::file_time_type::clock::now() + std::chrono::seconds(10));
  title_logs.clear();
  RuntimeHost legacy(config);
  CHECK(legacy.Start());
  WaitFor(legacy, [](const RuntimeSnapshot& s) { return Started(s) && !s.generated_recipes.empty(); });
  CHECK(title_logs.count("FAKE78001"));
  CHECK(title_logs.at("FAKE78001").find("== Game_Data/output_log.txt\nInitialize engine version: 5.3.6p5") !=
        std::string::npos);
  legacy.Prepare({4242, "/data/homebrew/FAKE77031/games/4242", "internal", "Example Game"});
  const auto again = WaitFor(legacy, [](const RuntimeSnapshot& s) { return s.refusals.count(4242) != 0; });
  CHECK(again.refusals.at(4242) == "This game is already prepared.");
  legacy.Shutdown();
}

// Settings saved while the game is being prepared, after its profile was written, reach the title
// as soon as it is ready instead of waiting for the next Playden start.
void SettingsSavedDuringPrepareReachReadyTitle() {
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
  auto kit = SyntheticKit(fixture.root / "kit");
  kit.capabilities = {"dxvk", "offline_bottle", "per_game_title", kKitDxvkConfig, "wow64_i386"};
  config.admit_kit = [&kit](std::string*) { return std::optional<RuntimeKit>(kit); };
  unsigned uuids = 0;
  config.uuid = [&uuids]() -> std::optional<std::string> {
    ++uuids; return std::string("00000000-0000-4000-8000-00000000000") + std::to_string(uuids);
  };
  std::int64_t now = 1760000000;
  config.unix_time = [&now] { return now; };
  config.session_data_root = fixture.root / "data";
  std::mutex mutex;
  GameSettingsRecord saved;
  bool applied_logged = false;
  config.settings_record = [&](std::uint32_t) -> std::optional<GameSettingsRecord> {
    std::lock_guard<std::mutex> lock(mutex);
    return saved;
  };
  // The player changes a setting right after the profile is written.
  config.log = [&](const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex);
    if (line.rfind("prepare step app=220 from=publish-profile", 0) == 0) {
      saved.revision = 1;
      saved.overrides = {{"resolution", "1280x720"}};
    }
    if (line == "prepared settings app=220 applied") applied_logged = true;
  };
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/220";
  Write(game / "hl2.exe", "existing-game", 0644);
  CHECK(chmod(game.c_str(), 0755) == 0);
  const auto profile = fixture.root / "data/playden/games/steam-220/library/profiles/playden-steam-220.profile";

  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  host.Prepare(Hl2());
  const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && (s.refusals.count(220) || (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
  }, 60);
  CHECK(!done.refusals.count(220) && done.installs[0].state == PreparationState::kReady);
  CHECK(done.installs[0].settings_revision == 1 && Read(profile).find("desktop = 1280x720") != std::string::npos);
  {
    std::lock_guard<std::mutex> lock(mutex);
    CHECK(applied_logged);
  }
  host.Shutdown();
}

// Settings saved after preparation reach the Ready title through a journaled update, from
// maintenance or from Play, only while the title is stopped; maintenance clears the shader
// cache, backs up the bottle's user folders and repairs the bottle.
void MaintainsReadyTitle() {
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
  auto kit = SyntheticKit(fixture.root / "kit");
  kit.capabilities = {"dxvk", "game-root-unowned", "offline_bottle", "per_game_title", kKitDxvkConfig, "wow64_i386"};
  config.admit_kit = [&kit](std::string*) { return std::optional<RuntimeKit>(kit); };
  unsigned uuids = 0;
  bool loosen_next = false;  // the next armed update's capture sees a world-writable PS5 title folder
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/220";
  const auto staged = fixture.root / "data/homebrew/FAKE78001";
  config.uuid = [&uuids]() -> std::optional<std::string> {
    ++uuids; return std::string("00000000-0000-4000-8000-00000000000") + std::to_string(uuids);
  };
  // Logged once the update is armed and before its capture: the seam for an interruption.
  unsigned full_inspections = 0, ownership_inspections = 0;
  config.log = [&loosen_next, staged, &full_inspections, &ownership_inspections](const std::string& line) {
    if (line.rfind("package inspect ownership ", 0) == 0) ++ownership_inspections;
    else if (line.rfind("package inspect ", 0) == 0) ++full_inspections;
    if (loosen_next && line.rfind("settings update request", 0) == 0 && line.find(" state=0 ") != std::string::npos) {
      loosen_next = false;
      CHECK(chmod(staged.c_str(), 0777) == 0);
    }
  };
  std::int64_t now = 1760000000;
  config.unix_time = [&now] { return now; };
  config.session_data_root = fixture.root / "data";
  std::vector<std::string> launches;
  config.launch_title = [&launches](const std::string& title, const std::vector<std::string>&) {
    launches.push_back(title);
    return TitleResult{true, 0, "launch"};
  };
  GameSettingsRecord saved;
  config.settings_record = [&saved](std::uint32_t) -> std::optional<GameSettingsRecord> { return saved; };
  Write(game / "hl2.exe", "existing-game", 0644);
  CHECK(chmod(game.c_str(), 0755) == 0);
  const auto data = fixture.root / "data/playden/games/steam-220";
  const auto profile = data / "library/profiles/playden-steam-220.profile";

  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  host.Prepare(Hl2());
  auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && (s.refusals.count(220) || (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
  }, 60);
  CHECK(!done.refusals.count(220) && done.installs[0].settings_revision == 0 && done.installs[0].launch_entry_id == "hl2");
  CHECK(Read(profile).find("desktop = 1920x1080") != std::string::npos && !fs::exists(game / "dxvk.conf"));

  const auto maintain = [&host](RuntimeMaintenance action) {
    const auto before = host.Snapshot().maintenance_handled;
    host.Maintain(220, action);
    CHECK(host.Snapshot().maintenance.at(220).running);
    return WaitFor(host, [before](const RuntimeSnapshot& s) { return s.maintenance_handled > before; }, 60);
  };
  // Saved while the game runs: nothing changes until it is stopped.
  saved.revision = 1;
  saved.overrides = {{"resolution", "1280x720"}, {"frame_limit", "30"}};
  fixture.activity->store(static_cast<int>(NativeActivity::kActive));
  auto result = maintain(RuntimeMaintenance::kApplySettings);
  CHECK(!result.maintenance.at(220).ok && result.maintenance.at(220).message == "Changes apply the next time Playden starts the game.");
  CHECK(result.installs[0].settings_revision == 0 && Read(profile).find("desktop = 1920x1080") != std::string::npos);
  fixture.activity->store(static_cast<int>(NativeActivity::kUnknown));
  CHECK(!maintain(RuntimeMaintenance::kClearShaderCache).maintenance.at(220).ok);
  fixture.activity->store(static_cast<int>(NativeActivity::kStopped));
  full_inspections = ownership_inspections = 0;
  result = maintain(RuntimeMaintenance::kApplySettings);
  CHECK(result.maintenance.at(220).ok && result.maintenance.at(220).message == "Settings applied.");
  // The update checks the title's ownership once, before it changes anything, and never
  // walks its files and folders (14 full walks, then 11 ownership checks, before).
  CHECK(full_inspections == 0 && ownership_inspections == 1);
  CHECK(result.installs[0].settings_revision == 1 && !result.installs[0].recovery_pending);
  CHECK(Read(profile).find("desktop = 1280x720") != std::string::npos);
  CHECK(Read(game / "dxvk.conf").find("maxFrameRate = 30") != std::string::npos);
  CHECK(maintain(RuntimeMaintenance::kApplySettings).maintenance.at(220).message == "Settings are up to date.");

  // The shader cache of the launch program goes; nothing else in the folder does.
  Write(game / "hl2.dxvk-cache", "pipelines", 0644);
  CHECK(maintain(RuntimeMaintenance::kClearShaderCache).maintenance.at(220).ok);
  CHECK(!fs::exists(game / "hl2.dxvk-cache") && Read(game / "hl2.exe") == "existing-game");

  // Backups copy the Windows user folders; the newest three are kept.
  // A file with another link (here the bottle's registry linked into the profile) is not copied.
  Write(data / "library/prefixes/game/drive_c/users/prospero/Documents/save.dat", "progress", 0600);
  const auto linked = data / "library/prefixes/game/drive_c/users/prospero/Documents/linked.reg";
  CHECK(link((data / "library/prefixes/game/user.reg").c_str(), linked.c_str()) == 0);
  for (int index = 0; index < 4; ++index, ++now) CHECK(maintain(RuntimeMaintenance::kBackupSaves).maintenance.at(220).ok);
  const auto backups = fixture.root / "data/playden/games/steam-220.saves";
  CHECK(Read(backups / "1760000003/users/prospero/Documents/save.dat") == "progress");
  CHECK(!fs::exists(backups / "1760000003/users/prospero/Documents/linked.reg"));
  CHECK(unlink(linked.c_str()) == 0);
  CHECK(!fs::exists(backups / "1760000000") && fs::exists(backups / "1760000001"));
  result = host.Snapshot();
  CHECK(result.installs[0].save_backups == 3 && result.installs[0].latest_save_backup == 1760000003);

  CHECK(chmod((data / "library/prefixes/game/user.reg").c_str(), 0666) == 0);
  result = maintain(RuntimeMaintenance::kRepair);
  CHECK(result.maintenance.at(220).ok);
  struct stat info{};
  CHECK(stat((data / "library/prefixes/game/user.reg").c_str(), &info) == 0 && (info.st_mode & 0777) == 0600);
  // A kit without bottle-profile-shared keeps the bottle private: its way and its saves.
  for (const auto* folder : {"", "library", "library/prefixes", "library/prefixes/game", "library/prefixes/game/drive_c/users"})
    CHECK(lstat((data / folder).c_str(), &info) == 0 && (info.st_mode & 07777) == 0700);
  CHECK(lstat((data / "library/prefixes/game/drive_c/users/prospero/Documents/save.dat").c_str(), &info) == 0 &&
        (info.st_mode & 07777) == 0600);
  CHECK(lstat(backups.c_str(), &info) == 0 && (info.st_mode & 07777) == 0700);

  // An update interrupted after its arm keeps its journal: Play waits, and Resume (Prepare)
  // finishes the saved request instead of making a new one.
  saved.revision = 2;
  saved.overrides = {{"resolution", "1280x720"}, {"frame_limit", "60"}};
  loosen_next = true;
  result = maintain(RuntimeMaintenance::kApplySettings);
  CHECK(!result.maintenance.at(220).ok && result.installs[0].recovery_pending && result.installs[0].settings_revision == 1);
  const auto plays = host.Snapshot().plays_handled;
  host.Play(220);
  result = WaitFor(host, [plays](const RuntimeSnapshot& s) { return s.plays_handled > plays; }, 30);
  CHECK(result.play_refusals.count(220) && launches.empty());
  CHECK(chmod(staged.c_str(), 0755) == 0);
  host.Prepare(Hl2());
  result = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && !s.installs.empty() && (!s.installs[0].recovery_pending || s.refusals.count(220));
  }, 60);
  if (result.refusals.count(220)) std::fprintf(stderr, "Resume refused: %s\n", result.refusals.at(220).c_str());
  CHECK(!result.refusals.count(220) && !result.installs[0].recovery_pending && result.installs[0].settings_revision == 2);
  CHECK(Read(game / "dxvk.conf").find("maxFrameRate = 60") != std::string::npos);

  // Play publishes newer settings before the game starts, so Home starts the same profile.
  saved.revision = 3;
  saved.overrides = {{"resolution", "2560x1440"}};
  full_inspections = ownership_inspections = 0;
  host.Play(220);
  WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready; }, 30);
  CHECK(host.Handoff() && launches.size() == 1);
  // Play's checks: one full inspection for the launch (two before, plus a separate start
  // check), the apply's one ownership check and a marker-only native query.
  CHECK(full_inspections == 1 && ownership_inspections == 2);
  CHECK(host.Snapshot().installs[0].settings_revision == 3);
  CHECK(Read(profile).find("desktop = 2560x1440") != std::string::npos && !fs::exists(game / "dxvk.conf"));
  host.Shutdown();
}

// #73: files a player adds or changes over FTP (a mod, a world-writable folder) are theirs.
// A kit that admits unowned game folders gets the folder as it is: Play, a settings update
// and Repair all work, and the added files and modes stay.
void ModdedGameFolderStillPlays() {
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
  auto kit = SyntheticKit(fixture.root / "kit");
  kit.capabilities = {"dxvk", "game-root-unowned", "offline_bottle", "per_game_title", kKitDxvkConfig, "wow64_i386"};
  config.admit_kit = [&kit](std::string*) { return std::optional<RuntimeKit>(kit); };
  unsigned uuids = 0;
  config.uuid = [&uuids]() -> std::optional<std::string> {
    ++uuids; return std::string("00000000-0000-4000-8000-00000000000") + std::to_string(uuids);
  };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  config.session_data_root = fixture.root / "data";
  std::vector<std::string> launches;
  config.launch_title = [&launches](const std::string& title, const std::vector<std::string>&) {
    launches.push_back(title);
    return TitleResult{true, 0, "launch"};
  };
  GameSettingsRecord saved;
  config.settings_record = [&saved](std::uint32_t) -> std::optional<GameSettingsRecord> { return saved; };
  config.as_root = [](const std::function<bool()>& work) { return work(); };
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/220";
  Write(game / "hl2.exe", "existing-game", 0644);
  CHECK(chmod(game.c_str(), 0755) == 0);  // prepared by an older Playden, which tightened it
  const auto mode = [](const fs::path& path) {
    struct stat info{};
    CHECK(lstat(path.c_str(), &info) == 0);
    return unsigned(info.st_mode & 07777);
  };

  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  host.Prepare(Hl2());
  auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && (s.refusals.count(220) || (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
  }, 60);
  CHECK(!done.refusals.count(220) && done.installs[0].state == PreparationState::kReady);

  // The mod, as an FTP client leaves it: world-writable folders and files.
  Write(game / "GInput.asi", "mod-plugin", 0666);
  Write(game / "mods/GInput.ini", "mod-settings", 0666);
  CHECK(chmod((game / "mods").c_str(), 0777) == 0);
  CHECK(chmod(game.c_str(), 0777) == 0);
  const auto mod_kept = [&] {
    CHECK(Read(game / "GInput.asi") == "mod-plugin" && Read(game / "mods/GInput.ini") == "mod-settings");
    CHECK(mode(game) == 0777 && mode(game / "mods") == 0777 && mode(game / "GInput.asi") == 0666);
    CHECK(Read(game / "hl2.exe") == "existing-game");
  };

  const auto maintain = [&host](RuntimeMaintenance action) {
    const auto before = host.Snapshot().maintenance_handled;
    host.Maintain(220, action);
    return WaitFor(host, [before](const RuntimeSnapshot& s) { return s.maintenance_handled > before; }, 60);
  };
  // A settings update republishes into the opened-up folder. One the app user can't write
  // (root's 0755 after an FTP upload; 0555 stands in for it) is opened up first.
  if (geteuid() != 0) CHECK(chmod(game.c_str(), 0555) == 0);
  saved.revision = 1;
  saved.overrides = {{"frame_limit", "30"}};
  auto result = maintain(RuntimeMaintenance::kApplySettings);
  if (!result.maintenance.at(220).ok) std::fprintf(stderr, "Apply refused: %s\n", result.maintenance.at(220).message.c_str());
  CHECK(result.maintenance.at(220).ok && result.installs[0].settings_revision == 1);
  CHECK(Read(game / "dxvk.conf").find("maxFrameRate = 30") != std::string::npos);
  mod_kept();
  // Repair passes the start check and keeps the mod.
  result = maintain(RuntimeMaintenance::kRepair);
  if (!result.maintenance.at(220).ok) std::fprintf(stderr, "Repair refused: %s\n", result.maintenance.at(220).message.c_str());
  CHECK(result.maintenance.at(220).ok);
  mod_kept();
  // Prepare again (Update) is not refused for the folder either.
  host.Prepare(Hl2());
  done = WaitFor(host, [](const RuntimeSnapshot& s) { return !s.preparing_app && s.refusals.count(220); });
  CHECK(done.refusals.at(220) == "This game is already prepared.");
  mod_kept();
  // A dxvk.conf hard-linked by hand fails the title's start check; Repair makes it a copy of its own.
  const auto dxvk = Read(game / "dxvk.conf");
  CHECK(link((game / "dxvk.conf").c_str(), (game / "dxvk-copy.conf").c_str()) == 0);
  result = maintain(RuntimeMaintenance::kRepair);
  if (!result.maintenance.at(220).ok) std::fprintf(stderr, "Repair refused: %s\n", result.maintenance.at(220).message.c_str());
  CHECK(result.maintenance.at(220).ok);
  struct stat linked{};
  CHECK(lstat((game / "dxvk.conf").c_str(), &linked) == 0 && linked.st_nlink == 1);
  CHECK(Read(game / "dxvk.conf") == dxvk && Read(game / "dxvk-copy.conf") == dxvk);
  mod_kept();
  // Play publishes newer settings first, then starts the game.
  saved.revision = 2;
  saved.overrides = {{"frame_limit", "60"}};
  host.Play(220);
  WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready; }, 30);
  CHECK(host.Handoff() && launches.size() == 1);
  CHECK(host.Snapshot().installs[0].settings_revision == 2);
  CHECK(Read(game / "dxvk.conf").find("maxFrameRate = 60") != std::string::npos);
  mod_kept();
  host.Shutdown();
}

// A game an older Playden or kit published owned (schema 2), which the player then opened for a
// mod. With a kit that admits unowned folders, each way back republishes it unowned: the
// startup pass (route 0), or with the title busy at startup, Repair (1), a settings update (2),
// Update (3) or Play (4). Owned 0755 folders that were tightened open up again (route 0), but
// never while the publication stays owned (5: the republication is refused). Such a game
// played before, so it still plays, Repairs and Updates under its owned publication (5).
void OwnedPublicationOpenedUpRecovers(int route) {
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
  auto kit = SyntheticKit(fixture.root / "kit");
  kit.capabilities = {"dxvk", "offline_bottle", "per_game_title", kKitDxvkConfig, "wow64_i386"};
  config.admit_kit = [&kit](std::string*) { return std::optional<RuntimeKit>(kit); };
  unsigned uuids = 0;
  config.uuid = [&uuids]() -> std::optional<std::string> {
    char id[37]; std::snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012u", ++uuids);
    return std::string(id);
  };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  config.session_data_root = fixture.root / "data";
  config.launch_title = [](const std::string&, const std::vector<std::string>&) { return TitleResult{true, 0, "launch"}; };
  GameSettingsRecord saved;
  config.settings_record = [&saved](std::uint32_t) -> std::optional<GameSettingsRecord> { return saved; };
  std::vector<std::string> lines;
  std::mutex lines_mutex;
  config.log = [&](const std::string& line) { std::lock_guard<std::mutex> lock(lines_mutex); lines.push_back(line); };
  const auto logged = [&](const std::string& prefix) {
    std::lock_guard<std::mutex> lock(lines_mutex);
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) { return line.rfind(prefix, 0) == 0; });
  };
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/220";
  Write(game / "hl2.exe", "existing-game", 0644);
  CHECK(chmod(game.c_str(), 0777) == 0);
  const auto metadata = fixture.root / "data/playden/games/steam-220/library/profiles/.playden-publication";
  const auto mode = [](const fs::path& path) {
    struct stat info{};
    CHECK(lstat(path.c_str(), &info) == 0);
    return unsigned(info.st_mode & 07777);
  };
  if (route == 5) saved.revision = 1;
  {
    RuntimeHost host(config);
    CHECK(host.Start());
    CHECK(WaitFor(host, Started).kit_available);
    host.Prepare(Hl2());
    const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
      return !s.preparing_app && (s.refusals.count(220) || (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
    }, 60);
    CHECK(!done.refusals.count(220) && done.installs[0].state == PreparationState::kReady);
    host.Shutdown();
  }
  CHECK(Read(metadata).find("schema=2\n") != std::string::npos && mode(game) == 0755);
  Write(game / "GInput.asi", "mod-plugin", 0666);
  if (route == 5) {
    saved.revision = 0;  // older than the title's: the republication is refused
    kit.capabilities = {"dxvk", "game-root-unowned", "offline_bottle", "per_game_title", kKitDxvkConfig, "wow64_i386"};
    RuntimeHost host(config);
    CHECK(host.Start());
    WaitFor(host, Started);
    CHECK(logged("startup unowned app=220 deferred: "));
    CHECK(Read(metadata).find("schema=2\n") != std::string::npos && mode(game) == 0755);
    const auto maintain = [&host](RuntimeMaintenance action) {
      const auto before = host.Snapshot().maintenance_handled;
      host.Maintain(220, action);
      return WaitFor(host, [before](const RuntimeSnapshot& s) { return s.maintenance_handled > before; }, 60).maintenance.at(220);
    };
    const auto repaired = maintain(RuntimeMaintenance::kRepair);
    if (!repaired.ok) std::fprintf(stderr, "route 5 Repair refused: %s\n", repaired.message.c_str());
    CHECK(repaired.ok && repaired.message.find("could not be published for mods") != std::string::npos);
    const auto before = host.Snapshot().prepares_handled;
    host.Prepare(Hl2());
    const auto updated = WaitFor(host, [before](const RuntimeSnapshot& s) { return s.prepares_handled > before; }, 60);
    CHECK(updated.refusals.count(220) && updated.refusals.at(220) == "This game is already prepared.");
    host.Play(220);
    const auto done = WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready || s.play_refusals.count(220); }, 30);
    if (done.play_refusals.count(220)) std::fprintf(stderr, "route 5 Play refused: %s\n", done.play_refusals.at(220).c_str());
    CHECK(done.handoff_ready && host.Handoff());
    CHECK(Read(metadata).find("schema=2\n") != std::string::npos && mode(game) == 0755);
    CHECK(Read(game / "GInput.asi") == "mod-plugin");
    host.Shutdown();
    return;
  }
  if (route != 0) CHECK(chmod(game.c_str(), 0777) == 0);  // opened over FTP
  kit.capabilities = {"dxvk", "game-root-unowned", "offline_bottle", "per_game_title", kKitDxvkConfig, "wow64_i386"};
  if (route != 0) fixture.activity->store(static_cast<int>(NativeActivity::kActive));

  RuntimeHost host(config);
  CHECK(host.Start());
  WaitFor(host, Started);
  if (route == 0) {
    CHECK(logged("startup unowned app=220 republished"));
  } else {
    CHECK(logged("startup unowned app=220 waits: "));
    CHECK(Read(metadata).find("schema=2\n") != std::string::npos);
    fixture.activity->store(static_cast<int>(NativeActivity::kStopped));
  }
  const auto maintain = [&host](RuntimeMaintenance action) {
    const auto before = host.Snapshot().maintenance_handled;
    host.Maintain(220, action);
    return WaitFor(host, [before](const RuntimeSnapshot& s) { return s.maintenance_handled > before; }, 60).maintenance.at(220);
  };
  bool played = false;
  if (route == 1 || route == 2) {
    if (route == 2) { saved.revision = 1; saved.overrides = {{"frame_limit", "30"}}; }
    const auto result = maintain(route == 1 ? RuntimeMaintenance::kRepair : RuntimeMaintenance::kApplySettings);
    if (!result.ok) std::fprintf(stderr, "route %d refused: %s\n", route, result.message.c_str());
    CHECK(result.ok);
  } else if (route == 3) {
    const auto before = host.Snapshot().prepares_handled;
    host.Prepare(Hl2());
    const auto done = WaitFor(host, [before](const RuntimeSnapshot& s) { return s.prepares_handled > before; }, 60);
    if (done.refusals.count(220)) std::fprintf(stderr, "Update refused: %s\n", done.refusals.at(220).c_str());
    CHECK(!done.refusals.count(220));
  } else if (route == 4) {
    host.Play(220);
    const auto done = WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready || s.play_refusals.count(220); }, 30);
    if (done.play_refusals.count(220)) std::fprintf(stderr, "Play refused: %s\n", done.play_refusals.at(220).c_str());
    CHECK(done.handoff_ready && host.Handoff());
    played = true;
  }
  const auto published = Read(metadata);
  CHECK(published.find("schema=3\n") != std::string::npos && published.find("game_root.unowned=1\n") != std::string::npos);
  CHECK(mode(game) == 0777 && Read(game / "GInput.asi") == "mod-plugin" && mode(game / "GInput.asi") == 0666);
  if (!played) {
    host.Play(220);
    const auto done = WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready || s.play_refusals.count(220); }, 30);
    if (done.play_refusals.count(220)) std::fprintf(stderr, "Play refused: %s\n", done.play_refusals.at(220).c_str());
    CHECK(done.handoff_ready && host.Handoff());
  }
  host.Shutdown();
}

// A kit without game-root-unowned (before 0.10) keeps the old rule: an owned, world-writable
// game folder is tightened to 0755 and published owned (metadata schema 2). A folder owned by
// another user is refused; that needs root to set up, so it isn't tested here.
void KitWithoutUnownedRootsKeepsTightening() {
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
  const auto kit = WithoutUnownedRoots(SyntheticKit(fixture.root / "kit"));
  config.admit_kit = [&kit](std::string*) { return std::optional<RuntimeKit>(kit); };
  config.uuid = [] { return std::optional<std::string>("00000000-0000-4000-8000-000000000001"); };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/220";
  Write(game / "hl2.exe", "existing-game", 0644);
  CHECK(chmod(game.c_str(), 0777) == 0);
  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  host.Prepare(Hl2());
  const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && (s.refusals.count(220) ||
        (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
  }, 60);
  if (done.refusals.count(220)) std::fprintf(stderr, "Prepare refused: %s\n", done.refusals.at(220).c_str());
  CHECK(!done.refusals.count(220) && done.installs[0].state == PreparationState::kReady);
  struct stat info{};
  CHECK(stat(game.c_str(), &info) == 0 && (info.st_mode & 07777) == 0755);
  const auto metadata = Read(fixture.root / "data/playden/games/steam-220/library/profiles/.playden-publication");
  CHECK(metadata.find("schema=2\n") != std::string::npos && metadata.find("game_root.unowned") == std::string::npos);
  host.Shutdown();
}

// A kit respin while a settings update waits for recovery (#5): the title is still this
// install's, so Update first finishes the saved update, and the title only reports that
// it needs Update (not current) until a second Update refreshes it.
void KitRespinKeepsWaitingSettingsUpdate() {
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
  unsigned uuids = 0;
  config.uuid = [&uuids]() -> std::optional<std::string> {
    ++uuids; return std::string("00000000-0000-4000-8000-00000000000") + std::to_string(uuids);
  };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/220";
  bool loosen_next = false;  // interrupts the next armed update before its capture
  config.log = [&loosen_next, game](const std::string& line) {
    if (loosen_next && line.rfind("settings update request", 0) == 0 && line.find(" state=0 ") != std::string::npos) {
      loosen_next = false;
      CHECK(chmod(game.c_str(), 0777) == 0);
    }
  };
  GameSettingsRecord saved;
  config.settings_record = [&saved](std::uint32_t) -> std::optional<GameSettingsRecord> { return saved; };
  Write(game / "hl2.exe", "existing-game", 0644);
  CHECK(chmod(game.c_str(), 0755) == 0);
  {
    auto kit = SyntheticKit(fixture.root / "kit");
    kit.capabilities = {"dxvk", "offline_bottle", "per_game_title", kKitDxvkConfig, "wow64_i386"};
    auto first = config;
    first.admit_kit = [kit](std::string*) { return std::optional<RuntimeKit>(kit); };
    RuntimeHost host(first);
    CHECK(host.Start());
    host.Prepare(Hl2());
    const auto ready = WaitFor(host, [](const RuntimeSnapshot& s) {
      return !s.preparing_app && !s.installs.empty() && s.installs[0].state == PreparationState::kReady;
    }, 60);
    CHECK(ready.installs[0].title_current);
    saved.revision = 1;
    saved.overrides = {{"frame_limit", "30"}};
    loosen_next = true;
    const auto before = host.Snapshot().maintenance_handled;
    host.Maintain(220, RuntimeMaintenance::kApplySettings);
    const auto interrupted = WaitFor(host, [before](const RuntimeSnapshot& s) { return s.maintenance_handled > before; }, 60);
    CHECK(!interrupted.maintenance.at(220).ok && interrupted.installs[0].recovery_pending);
    host.Shutdown();
  }
  CHECK(chmod(game.c_str(), 0755) == 0);
  auto respun = SyntheticKit(fixture.root / "kit-respun", "native-2", "kit-1", 'c');
  respun.capabilities = {"dxvk", "offline_bottle", "per_game_title", kKitDxvkConfig, "wow64_i386"};
  config.admit_kit = [respun](std::string*) { return std::optional<RuntimeKit>(respun); };
  RuntimeHost host(config);
  CHECK(host.Start());
  const auto started = WaitFor(host, Started);
  CHECK(!started.installs[0].title_current && started.installs[0].recovery_pending);
  host.Prepare(Hl2());
  auto result = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && !s.installs.empty() && (!s.installs[0].recovery_pending || s.refusals.count(220));
  }, 60);
  if (result.refusals.count(220)) std::fprintf(stderr, "Resume after respin refused: %s\n", result.refusals.at(220).c_str());
  CHECK(!result.refusals.count(220) && result.installs[0].settings_revision == 1);
  CHECK(Read(game / "dxvk.conf").find("maxFrameRate = 30") != std::string::npos);
  CHECK(!result.installs[0].title_current);  // needs Update, not recovery
  host.Prepare(Hl2());
  result = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && !s.installs.empty() && (s.installs[0].title_current || s.refusals.count(220));
  }, 60);
  CHECK(!result.refusals.count(220) && result.installs[0].title_current);
  CHECK(Read(fixture.root / "data/homebrew/FAKE78001/eboot.bin") == "native-2");
  host.Shutdown();
}

// Uninstall removes the game's PS5 title: the title folder leaves ShadowMount's
// scan root, the title is unregistered through the remover, the bottle is kept
// (or deleted on request) and the record goes once the console no longer lists
// the title. Every step is journaled and continues after a failure or restart.
// A game installed again on another drive (its uninstall stopped part way and left the title):
// Prepare removes the old title, keeping its saves, and prepares the game where it is now.
void PreparesGameInstalledAgainElsewhere() {
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
  config.removal_confirm_polls = 3;
  config.removal_poll_interval = std::chrono::milliseconds(1);
  std::vector<std::string> removed_titles;
  bool remover_works = false;  // silent at first: the console never confirms
  config.unregister_title = [&fixture, &removed_titles, &remover_works](const std::string& title, const std::string&) {
    removed_titles.push_back(title);
    if (!remover_works) return TitleRemoverResult{true, std::nullopt, "no answer"};
    fixture.registered = false; fixture.submitted = false;
    return TitleRemoverResult{true, 0, "answered"};
  };
  Write(fixture.root / "data/homebrew/FAKE77031/games/220/hl2.exe", "existing-game", 0644);
  const auto bottle = fixture.root / "data/playden/games/steam-220";
  const auto save = fs::path("library/prefixes/game/drive_c/users/steamuser/AppData/Roaming/Game/save.dat");
  const auto prepare = [](RuntimeHost& host, const RuntimePrepareRequest& request) {
    const auto before = host.Snapshot().prepares_handled;
    host.Prepare(request);
    const auto done = WaitFor(host, [before](const RuntimeSnapshot& s) {
      return !s.preparing_app && s.prepares_handled > before;
    }, 60);
    if (done.refusals.count(220)) std::fprintf(stderr, "Prepare refused: %s\n", done.refusals.at(220).c_str());
    return done;
  };
  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  auto first = prepare(host, Hl2());
  CHECK(!first.refusals.count(220) && first.installs.size() == 1 &&
        first.installs[0].install_path == "/data/homebrew/FAKE77031/games/220");
  Write(bottle / save, "player-save", 0600);
  // The download went away with an uninstall that stopped; the game is on the USB drive now.
  fs::remove_all(fixture.root / "data/homebrew/FAKE77031/games/220");
  auto elsewhere = Hl2();
  elsewhere.install_path = "/mnt/usb0/playden/games/220";
  elsewhere.volume_id = "usb0";
  // Not there yet: nothing is removed.
  auto refused = prepare(host, elsewhere);
  CHECK(refused.refusals.count(220) && refused.refusals.at(220).find("missing") != std::string::npos);
  CHECK(removed_titles.empty() && refused.installs.size() == 1);
  const auto usb = fixture.root / "mnt/usb0/playden/games/220";
  Write(usb / "hl2.exe", "existing-game", 0644);
  for (const auto& folder : {fixture.root / "mnt", fixture.root / "mnt/usb0", fixture.root / "mnt/usb0/playden",
                             fixture.root / "mnt/usb0/playden/games", usb})
    CHECK(chmod(folder.c_str(), 0755) == 0);
  // The console doesn't confirm the old title is gone: the move waits, recorded as a move, so the
  // game page offers to finish it rather than an uninstall.
  const auto stopped = prepare(host, elsewhere);
  CHECK(stopped.refusals.count(220) && stopped.refusals.at(220).find("Play tries again") != std::string::npos);
  CHECK(stopped.installs.size() == 1 && stopped.installs[0].state == PreparationState::kRemoving &&
        stopped.installs[0].moving);
  remover_works = true;
  const auto moved = prepare(host, elsewhere);
  CHECK(!moved.refusals.count(220) && moved.installs.size() == 1 && !moved.installs[0].moving);
  CHECK(moved.installs[0].install_path == "/mnt/usb0/playden/games/220");
  CHECK(removed_titles.size() == 2 && removed_titles[1] == first.installs[0].title_id);
  CHECK(Read(bottle / save) == "player-save" && moved.kept_bottles.empty());
  // The move brought the saves back and says so in the bottle.
  CHECK(fs::exists(bottle / ".playden-saves-restored"));
  // A kept copy whose saves never came back (a preparation stopped before that, so no mark): an
  // uninstall keeping saves leaves it as it is and sets the newer bottle aside beside it.
  fs::remove(bottle / ".playden-saves-restored");
  const auto kept = fixture.root / "data/playden/games/steam-220.kept";
  Write(kept / save, "original-save", 0600);
  const auto before = host.Snapshot().removals_handled;
  host.Remove(220, false);
  const auto removed = WaitFor(host, [before](const RuntimeSnapshot& s) { return s.removals_handled > before; }, 30);
  CHECK(!removed.removal_failures.count(220) && removed.installs.empty());
  CHECK(Read(kept / save) == "original-save" && !fs::exists(bottle));
  bool aside = false;
  for (const auto& entry : fs::directory_iterator(fixture.root / "data/playden/games"))
    aside |= entry.path().filename().string().rfind("steam-220.set-aside-", 0) == 0 &&
             Read(entry.path() / save) == "player-save";
  CHECK(aside);
  host.Shutdown();
}

void RemovesTitleKeepingSaves() {
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
  config.removal_confirm_polls = 3;
  config.removal_poll_interval = std::chrono::milliseconds(1);
  // The remover: silent at first (the console never drops the title), then working.
  auto remover_works = std::make_shared<std::atomic<bool>>(false);
  auto remover_calls = std::make_shared<std::atomic<int>>(0);
  config.unregister_title = [&fixture, remover_works, remover_calls](const std::string& title, const std::string& nonce) {
    ++*remover_calls;
    CHECK(title == "FAKE78001" && nonce.size() == 36);
    if (!*remover_works) return TitleRemoverResult{true, std::nullopt, "no answer"};
    fixture.registered = false; fixture.submitted = false;
    return TitleRemoverResult{true, 0, "answered"};
  };
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/220";
  Write(game / "hl2.exe", "existing-game", 0644);
  const auto bottle = fixture.root / "data/playden/games/steam-220";
  const auto kept = fixture.root / "data/playden/games/steam-220.kept";
  const auto save = fs::path("library/prefixes/game/drive_c/users/steamuser/AppData/Roaming/Game/save.dat");
  const auto prepare = [](RuntimeHost& host) {
    host.Prepare(Hl2());
    const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
      return !s.preparing_app && (s.refusals.count(220) ||
          (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
    }, 60);
    if (done.refusals.count(220)) std::fprintf(stderr, "Prepare refused: %s\n", done.refusals.at(220).c_str());
    CHECK(!done.refusals.count(220) && done.installs.size() == 1);
    return done;
  };
  const auto remove = [](RuntimeHost& host, bool delete_saves) {
    const auto before = host.Snapshot().removals_handled;
    host.Remove(220, delete_saves);
    return WaitFor(host, [before](const RuntimeSnapshot& s) { return s.removals_handled > before; }, 30);
  };
  {
    RuntimeHost host(config);
    CHECK(host.Start());
    CHECK(WaitFor(host, Started).kit_available);
    prepare(host);
    host.SetTitleNames({{220, "Half-Life 2"}});
    Write(bottle / save, "player-save", 0600);
    for (const auto* part : {"library/prefixes/game/drive_c/users", "library/prefixes/game/drive_c/users/steamuser",
                             "library/prefixes/game/drive_c/users/steamuser/AppData",
                             "library/prefixes/game/drive_c/users/steamuser/AppData/Roaming",
                             "library/prefixes/game/drive_c/users/steamuser/AppData/Roaming/Game"})
      CHECK(chmod((bottle / part).c_str(), 0700) == 0);

    // A running title is never removed.
    fixture.activity->store(static_cast<int>(NativeActivity::kActive));
    auto removed = remove(host, false);
    CHECK(removed.removal_failures.at(220) == "Close the game before uninstalling it.");
    CHECK(removed.installs[0].state == PreparationState::kReady && *remover_calls == 0);
    fixture.activity->store(static_cast<int>(NativeActivity::kStopped));

    // The remover is silent: the folder already left the scan root, nothing is
    // recorded as failed by the console, and the record waits at "unregister".
    removed = remove(host, false);
    CHECK(removed.removal_failures.at(220).find("has not confirmed") != std::string::npos);
    CHECK(removed.installs.size() == 1 && removed.installs[0].state == PreparationState::kRemoving);
    CHECK(removed.installs[0].step == "unregister");
    CHECK(!fs::exists(fixture.root / "data/homebrew/FAKE78001"));
    CHECK(fs::is_directory(fixture.root / "data/runtime-state/trash/FAKE78001-00000000-0000-4000-8000-000000000002"));
    CHECK(*remover_calls == 1);
    // Preparation waits for the removal to finish.
    host.Prepare(Hl2());
    const auto refused = WaitFor(host, [](const RuntimeSnapshot& s) { return s.refusals.count(220) != 0; });
    CHECK(refused.refusals.at(220).find("still being removed") != std::string::npos);
    host.Shutdown();
  }
  {
    // After a restart the removal is reported, not resumed, until asked. Asked again,
    // it continues with the first choice (keep saves) even when asked to delete them.
    RuntimeHost host(config);
    CHECK(host.Start());
    const auto started = WaitFor(host, Started);
    CHECK(started.installs.size() == 1 && started.installs[0].state == PreparationState::kRemoving);
    CHECK(started.installs[0].failure.find("has not confirmed") != std::string::npos);
    CHECK(!fs::exists(fixture.root / "data/runtime-state/trash/FAKE78001-00000000-0000-4000-8000-000000000002"));
    // The PS5 title carries the store name; the kept saves are named for the game.
    host.SetTitleNames({{220, "Half-Life 2 (Steam)"}});
    // A read-only games folder stops the bottle step; the next attempt continues there.
    const auto games = fixture.root / "data/playden/games";
    CHECK(chmod(games.c_str(), 0500) == 0);
    *remover_works = true;
    auto removed = remove(host, true);
    CHECK(chmod(games.c_str(), 0700) == 0);
    CHECK(removed.installs.size() == 1 && removed.installs[0].step == "bottle");
    CHECK(removed.removal_failures.at(220).find("could not be kept") != std::string::npos);
    CHECK(!fixture.registered && *remover_calls == 2);
    removed = remove(host, true);
    CHECK(!removed.removal_failures.count(220) && removed.installs.empty());
    CHECK(*remover_calls == 2);  // the console no longer listed the title: never sent again
    CHECK(!fs::exists(bottle) && Read(kept / save) == "player-save");
    CHECK(removed.kept_bottles.size() == 1 && removed.kept_bottles[0].app_id == 220);
    CHECK(removed.kept_bottles[0].name == "Half-Life 2" && removed.kept_bottles[0].kept_at == 1760000000);
    CHECK(removed.kept_bottles[0].bytes > 0);
    CHECK(Read(kept / "playden-kept.json").find("\"install_id\":\"steam-220\"") != std::string::npos);
    CHECK(Read(game / "hl2.exe") == "existing-game");  // the download is the controller's to remove
    // The kept bottle says where it is, and its Windows user folders can be measured (the Saves tab).
    CHECK(removed.kept_bottles[0].data_root.size() > 14 &&
          removed.kept_bottles[0].data_root.compare(removed.kept_bottles[0].data_root.size() - 14, 14, "steam-220.kept") == 0);
    host.MeasureSaves();
    const auto measured = WaitFor(host, [](const RuntimeSnapshot& s) { return s.save_bytes.count(220) > 0; });
    CHECK(measured.save_bytes.at(220) >= std::string("player-save").size());

    // Prepared again, the game gets its saves back and the kept copy goes.
    const auto ready = prepare(host);
    CHECK(ready.kept_bottles.empty() && !fs::exists(kept));
    CHECK(!ready.work_progress);  // the copy's progress ends with the preparation
    CHECK(Read(bottle / save) == "player-save");
    struct stat info{};
    CHECK(stat((bottle / "library/prefixes/game/drive_c/users/steamuser").c_str(), &info) == 0 &&
          (info.st_mode & 0777) == 0700);

    // Deleting saves removes the bottle without following its links.
    const auto outside = fixture.root / "outside";
    Write(outside / "keep.txt", "not the bottle's", 0644);
    fs::create_directories(bottle / "library/prefixes/game/dosdevices");
    fs::create_directory_symlink(outside, bottle / "library/prefixes/game/dosdevices/z:");
    removed = remove(host, true);
    CHECK(!removed.removal_failures.count(220) && removed.installs.empty());
    CHECK(!fs::exists(bottle) && !fs::exists(kept) && removed.kept_bottles.empty());
    CHECK(!removed.work_progress);  // the deletion's progress ends with it
    CHECK(Read(outside / "keep.txt") == "not the bottle's");

    // A kept bottle can be deleted on its own.
    prepare(host);
    removed = remove(host, false);
    CHECK(removed.kept_bottles.size() == 1);
    host.DeleteKeptBottle(220);
    WaitFor(host, [](const RuntimeSnapshot& s) { return s.kept_bottles.empty(); });
    CHECK(!fs::exists(kept));
    host.Shutdown();
  }
}

// A game on a drive that records no file owners (exFAT) keeps its folder as it
// is. Its publication says so (metadata schema 3), and only a kit that can start
// games from such drives prepares it.
void PreparesGameOnUnownedDrive() {
  playden::platform::SetOwnershipProbeForTesting([](const fs::path&) { return std::optional<bool>(false); });
  for (const bool capable : {false, true}) {
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
    auto kit = capable ? SyntheticKit(fixture.root / "kit") : WithoutUnownedRoots(SyntheticKit(fixture.root / "kit"));
    config.admit_kit = [&kit](std::string*) { return std::optional<RuntimeKit>(kit); };
    unsigned uuids = 0;
    config.uuid = [&uuids]() -> std::optional<std::string> {
      ++uuids; return std::string("00000000-0000-4000-8000-00000000000") + std::to_string(uuids);
    };
    config.unix_time = [] { return std::int64_t{1760000000}; };
    const auto game = fixture.root / "data/homebrew/FAKE77031/games/220";
    Write(game / "hl2.exe", "existing-game", 0644);
    CHECK(chmod(game.c_str(), 0777) == 0);  // what exFAT reports for every folder

    RuntimeHost host(config);
    CHECK(host.Start());
    CHECK(WaitFor(host, Started).kit_available);
    host.Prepare(Hl2());
    const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
      return !s.preparing_app && (s.refusals.count(220) ||
          (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
    }, 60);
    struct stat game_info{};
    CHECK(stat(game.c_str(), &game_info) == 0 && (game_info.st_mode & 07777) == 0777);
    if (!capable) {
      CHECK(done.refusals.count(220) && done.refusals.at(220).find("doesn't keep file owners") != std::string::npos);
      CHECK(done.installs.empty());
    } else {
      if (done.refusals.count(220)) std::fprintf(stderr, "Prepare refused: %s\n", done.refusals.at(220).c_str());
      CHECK(!done.refusals.count(220));
      CHECK(done.installs.size() == 1 && done.installs[0].state == PreparationState::kReady);
      const auto metadata = Read(fixture.root / "data/playden/games/steam-220/library/profiles/.playden-publication");
      CHECK(metadata.find("schema=3\n") != std::string::npos);
      CHECK(metadata.find("game_root.unowned=1\n") != std::string::npos);
    }
    host.Shutdown();
  }
  playden::platform::SetOwnershipProbeForTesting(nullptr);
}

// Selaco (issue 52): Steam lists only .bat files, so Playden refuses it, but the program the
// player picks in Game settings prepares and starts; later settings updates keep that program.
void SteamEntryWithoutFolderStartsInInstallRoot() {
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
  config.session_data_root = fixture.root / "data";
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/646570";
  std::string pe(0x80 + 26, '\0');
  pe[0] = 'M'; pe[1] = 'Z'; pe[0x3c] = 0x40; pe[0x40] = 'P'; pe[0x41] = 'E';
  pe[0x44] = 0x64; pe[0x45] = static_cast<char>(0x86); pe[0x40 + 24] = 0x0b; pe[0x40 + 25] = 0x02;
  // Slay the Spire: Steam starts jre/bin/javaw.exe -jar desktop-1.0.jar in the install root.
  Write(game / "jre/bin/javaw.exe", pe, 0644);
  Write(game / "desktop-1.0.jar", "jar", 0644);
  CHECK(chmod(game.c_str(), 0755) == 0 && chmod((game / "jre").c_str(), 0755) == 0 && chmod((game / "jre/bin").c_str(), 0755) == 0);
  GameSettingsRecord saved;
  config.settings_record = [&](std::uint32_t) -> std::optional<GameSettingsRecord> { return saved; };
  RuntimePrepareRequest request{646570, "/data/homebrew/FAKE77031/games/646570", "internal", "Slay the Spire"};
  request.launches = {{"", "Play", "jre\\bin\\javaw.exe", "", "-jar desktop-1.0.jar"}};
  const auto profile = fixture.root / "data/playden/games/steam-646570/library/profiles/playden-steam-646570.profile";
  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  host.Prepare(request);
  const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && !s.installs.empty() && s.installs[0].state == PreparationState::kReady;
  }, 60);
  if (done.refusals.count(646570)) std::fprintf(stderr, "refused: %s\n", done.refusals.at(646570).c_str());
  CHECK(!done.installs.empty() && done.installs[0].launch_in_install_root && done.installs[0].launch_working_directory.empty());
  CHECK(Read(profile).find("working_directory = Z:\\data\\homebrew\\FAKE77031\\games\\646570\n") != std::string::npos);
  // The program's own folder, chosen in Game settings, reaches the title and the record; "." goes back to the game folder.
  fixture.activity->store(static_cast<int>(NativeActivity::kStopped));
  const auto apply = [&](std::uint64_t revision) {
    saved.revision = revision;
    const auto before = host.Snapshot().maintenance_handled;
    host.Maintain(646570, RuntimeMaintenance::kApplySettings);
    return WaitFor(host, [before](const RuntimeSnapshot& s) { return s.maintenance_handled > before; }, 60);
  };
  saved.overrides = {{"program_folder", "jre/bin"}};
  auto applied = apply(1);
  if (!applied.maintenance.at(646570).ok) std::fprintf(stderr, "apply: %s\n", applied.maintenance.at(646570).message.c_str());
  CHECK(applied.maintenance.at(646570).ok && !applied.installs[0].launch_in_install_root &&
        applied.installs[0].launch_working_directory == "jre/bin");
  CHECK(Read(profile).find("working_directory = Z:\\data\\homebrew\\FAKE77031\\games\\646570\\jre\\bin\n") != std::string::npos);
  saved.overrides = {{"program_folder", "."}};
  applied = apply(2);
  CHECK(applied.maintenance.at(646570).ok && applied.installs[0].launch_in_install_root &&
        applied.installs[0].launch_working_directory.empty());
  CHECK(Read(profile).find("working_directory = Z:\\data\\homebrew\\FAKE77031\\games\\646570\n") != std::string::npos);
  host.Shutdown();
}

void PreparesProgramChosenInSettings() {
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
  config.session_data_root = fixture.root / "data";
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/1592280";
  std::string pe(0x80 + 26, '\0');
  pe[0] = 'M'; pe[1] = 'Z'; pe[0x3c] = 0x40; pe[0x40] = 'P'; pe[0x41] = 'E';
  pe[0x44] = 0x64; pe[0x45] = static_cast<char>(0x86); pe[0x40 + 24] = 0x0b; pe[0x40 + 25] = 0x02;
  Write(game / "SELACO.exe", pe, 0644);
  Write(game / "Launch.bat", "start Launcher.exe\n", 0644);
  CHECK(chmod(game.c_str(), 0755) == 0);
  GameSettingsRecord saved;
  bool has_record = false;
  config.settings_record = [&](std::uint32_t) -> std::optional<GameSettingsRecord> {
    if (!has_record) return std::nullopt;
    return saved;
  };
  RuntimePrepareRequest request{1592280, "/data/homebrew/FAKE77031/games/1592280", "internal", "Selaco"};
  request.launches = {{"", "Play", "Launch.bat", "", ""}};
  const auto profile = fixture.root / "data/playden/games/steam-1592280/library/profiles/playden-steam-1592280.profile";

  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  host.Prepare(request);
  const auto refused = WaitFor(host, [](const RuntimeSnapshot& s) { return s.refusals.count(1592280) != 0; });
  CHECK(refused.refusals.at(1592280).find("no Windows program") != std::string::npos && refused.installs.empty());
  // Game settings gets a draft to start from and the folder's programs to pick.
  const auto& setup = refused.game_setups.at(1592280);
  CHECK(setup.draft && setup.draft->launch_entries.empty() && setup.refusal_soft && setup.scanned);
  CHECK(setup.programs.size() == 1 && setup.programs[0].path == "SELACO.exe" && setup.programs[0].architecture == "pe64");

  // The player picks SELACO.exe: Prepare makes the recipe from it and the title starts it.
  has_record = true;
  saved.overrides = {{"program", "SELACO.exe"}, {"architecture", "pe64"}};
  host.Prepare(request);
  const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && !s.installs.empty() && s.installs[0].state == PreparationState::kReady;
  }, 60);
  CHECK(!done.refusals.count(1592280) && !done.game_setups.at(1592280).draft);
  CHECK(done.generated_recipes.size() == 1 && done.generated_recipes[0].launch_entries[0].id == kProgramEntryId);
  CHECK(Read(profile).find("\\games\\1592280\\SELACO.exe\n") != std::string::npos);
  CHECK(Read(profile).find("\narchitecture = pe64\n") != std::string::npos);

  // A later update applies the program's folder too, without preparing the game again; a folder
  // that isn't there is refused and the title keeps what it had.
  fixture.activity->store(static_cast<int>(NativeActivity::kStopped));
  const auto apply = [&](std::uint64_t revision) {
    saved.revision = revision;
    const auto before = host.Snapshot().maintenance_handled;
    host.Maintain(1592280, RuntimeMaintenance::kApplySettings);
    return WaitFor(host, [before](const RuntimeSnapshot& s) { return s.maintenance_handled > before; }, 60);
  };
  saved.overrides["program_arguments"] = "-nostartup";
  saved.overrides["program_folder"] = "Elsewhere";
  auto applied = apply(1);
  CHECK(!applied.maintenance.at(1592280).ok &&
        applied.maintenance.at(1592280).message.find("Elsewhere chosen in Game settings is missing") != std::string::npos);
  CHECK(Read(profile).find("working_directory = Z:\\data\\homebrew\\FAKE77031\\games\\1592280\n") != std::string::npos);
  CHECK(applied.installs[0].launch_working_directory.empty() && applied.installs[0].launch_path == "SELACO.exe" &&
        !applied.installs[0].recovery_pending);
  std::filesystem::create_directory(game / "Elsewhere");
  CHECK(chmod((game / "Elsewhere").c_str(), 0755) == 0);
  applied = apply(2);
  if (!applied.maintenance.at(1592280).ok) std::fprintf(stderr, "apply: %s\n", applied.maintenance.at(1592280).message.c_str());
  CHECK(applied.maintenance.at(1592280).ok);
  CHECK(Read(profile).find("\narguments = -nostartup\n") != std::string::npos);
  CHECK(Read(profile).find("working_directory = Z:\\data\\homebrew\\FAKE77031\\games\\1592280\\Elsewhere\n") !=
        std::string::npos);
  CHECK(applied.installs[0].launch_working_directory == "Elsewhere");
  struct stat elsewhere{};  // an owned 0755 folder on the way to dxvk.conf opens for FTP
  CHECK(stat((game / "Elsewhere").c_str(), &elsewhere) == 0 && (elsewhere.st_mode & 07777) == 0777);
  // Another program from the folder: the profile and the install record both name it.
  Write(game / "Other.exe", pe, 0644);
  saved.overrides = {{"program", "Other.exe"}, {"architecture", "pe64"}};
  applied = apply(3);
  if (!applied.maintenance.at(1592280).ok) std::fprintf(stderr, "switch: %s\n", applied.maintenance.at(1592280).message.c_str());
  CHECK(applied.maintenance.at(1592280).ok);
  CHECK(Read(profile).find("\\games\\1592280\\Other.exe\n") != std::string::npos);
  CHECK(applied.installs[0].launch_path == "Other.exe" && applied.installs[0].launch_entry_id == kProgramEntryId &&
        applied.installs[0].launch_working_directory.empty() && applied.installs[0].settings_revision == 3);
  // A program that isn't there is refused before anything changes.
  saved.overrides = {{"program", "Missing.exe"}, {"architecture", "pe64"}};
  applied = apply(4);
  CHECK(!applied.maintenance.at(1592280).ok && applied.maintenance.at(1592280).message.find("missing") != std::string::npos);
  CHECK(Read(profile).find("\\games\\1592280\\Other.exe\n") != std::string::npos &&
        applied.installs[0].launch_path == "Other.exe" && applied.installs[0].settings_revision == 3);
  host.Shutdown();
}

// An Unreal Engine 3 (UDK) game asks for its EULA through UnSetup.exe, which the PS5 cannot
// start; preparing it marks the EULA accepted in the bottle's system.reg instead, once.
void PreparesUdkGameWithEulaAccepted() {
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
  config.session_data_root = fixture.root / "data";
  std::vector<std::string> lines;
  std::mutex lines_mutex;
  config.log = [&](const std::string& line) {
    std::lock_guard<std::mutex> lock(lines_mutex);
    lines.push_back(line);
  };
  const auto eula_lines = [&] {
    std::lock_guard<std::mutex> lock(lines_mutex);
    std::vector<std::string> found;
    for (const auto& line : lines)
      if (line.rfind("ue3 eula ", 0) == 0) found.push_back(line);
    return found;
  };
  const auto game = fixture.root / "data/homebrew/FAKE77031/games/219890";
  std::string pe(0x80 + 26, '\0');
  pe[0] = 'M'; pe[1] = 'Z'; pe[0x3c] = 0x40; pe[0x40] = 'P'; pe[0x41] = 'E';
  pe[0x44] = 0x4c; pe[0x45] = 0x01; pe[0x40 + 24] = 0x0b; pe[0x40 + 25] = 0x01;
  Write(game / "Binaries/Win32/UDK.exe", pe, 0644);
  Write(game / "Binaries/UnSetup.exe", "MZ", 0644);
  Write(game / "Binaries/InstallInfo.xml",
        "<?xml version=\"1.0\"?>\r\n<InstallInfo>\r\n  <InstallGuidString>dcf140f3-829e-4033-950d-b3be8d76b933"
        "</InstallGuidString>\r\n</InstallInfo>", 0644);
  CHECK(chmod(game.c_str(), 0755) == 0);
  RuntimePrepareRequest request{219890, "/data/homebrew/FAKE77031/games/219890", "internal", "Antichamber"};
  request.launches = {{"", "Play", "Binaries\\Win32\\UDK.exe", "", ""}};

  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  host.Prepare(request);
  const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && (s.refusals.count(219890) ||
        (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
  }, 60);
  if (done.refusals.count(219890)) std::fprintf(stderr, "Prepare refused: %s\n", done.refusals.at(219890).c_str());
  CHECK(!done.refusals.count(219890) && done.installs.size() == 1);
  const auto bottle = fixture.root / "data/playden/games/steam-219890/library/prefixes/game";
  const auto key = std::string("Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\UDK-dcf140f3-829e-4033-950d-b3be8d76b933]");
  const auto system_reg = Read(bottle / "system.reg");
  CHECK(system_reg.find("[Software\\\\" + key) != std::string::npos);
  CHECK(system_reg.find("[Software\\\\Wow6432Node\\\\" + key) != std::string::npos);
  CHECK(Read(bottle / "system.reg.playden-backup") == "WINE REGISTRY Version 2\n");
  auto logged = eula_lines();
  CHECK(!logged.empty() && logged.front() == "ue3 eula app=219890 guid=dcf140f3-829e-4033-950d-b3be8d76b933 written=1");

  // Repair runs the fixups again: the value is already there, so nothing is rewritten.
  fixture.activity->store(static_cast<int>(NativeActivity::kStopped));
  const auto before = host.Snapshot().maintenance_handled;
  host.Maintain(219890, RuntimeMaintenance::kRepair);
  const auto repaired = WaitFor(host, [before](const RuntimeSnapshot& s) { return s.maintenance_handled > before; }, 60);
  if (!repaired.maintenance.at(219890).ok) std::fprintf(stderr, "repair: %s\n", repaired.maintenance.at(219890).message.c_str());
  CHECK(repaired.maintenance.at(219890).ok);
  CHECK(Read(bottle / "system.reg") == system_reg);
  logged = eula_lines();
  CHECK(logged.back() == "ue3 eula app=219890 guid=dcf140f3-829e-4033-950d-b3be8d76b933 written=0");
  host.Shutdown();
}

// Playden ships more recipes than the one the kit carries: a game with its own shipped recipe
// (FF XIII) is prepared by a kit packaged for HL2, with the recipe's bottle values written.
void ShippedRecipePreparesWithAnotherKitsRecipe() {
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
  Write(fixture.root / "data/homebrew/FAKE77031/games/292120/white_data/prog/win/bin/ffxiiiimg.exe", "existing-game", 0644);
  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  host.Prepare({292120, "/data/homebrew/FAKE77031/games/292120", "internal", "Final Fantasy XIII"});
  const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && (s.refusals.count(292120) || (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
  }, 60);
  if (done.refusals.count(292120)) std::fprintf(stderr, "Prepare refused: %s\n", done.refusals.at(292120).c_str());
  CHECK(!done.refusals.count(292120) && done.installs.size() == 1 && done.installs[0].app_id == 292120);
  CHECK(Read(config.state_root / "installs.json").find("\"recipe_id\":\"steam-292120-ffxiii-v1\"") != std::string::npos);
  const auto game = fixture.root / "data/playden/games/steam-292120";
  CHECK(Read(game / "library/profiles/playden-steam-292120.profile").find("ffxiiiimg.exe") != std::string::npos);
  CHECK(Read(game / "library/prefixes/game/user.reg").find("\"VideoMemorySize\"=\"4096\"") != std::string::npos);
  // A restart resolves the record again through the same rule.
  host.Shutdown();
  RuntimeHost restarted(config);
  CHECK(restarted.Start());
  const auto reloaded = WaitFor(restarted, Started);
  CHECK(reloaded.installs.size() == 1 && reloaded.installs[0].state == PreparationState::kReady);
  restarted.Shutdown();
}

// A shipped recipe's requirements are checked against the kit's template, as the kit's packager
// checks the kit's own recipe: without PS5 audio in the template, a recipe that needs it is refused.
void ShippedRecipeRequirementsFollowTheKit(bool template_audio_ps5) {
  Fixture fixture;
  auto config = fixture.Config();
  const auto runtime = fixture.root / "runtime";
  Write(runtime / "presets.json", Read(fs::path(g_runtime_dir) / "presets.json"), 0644);
  Write(runtime / "recipes/recipes.lst", Read(fs::path(g_runtime_dir) / "recipes/recipes.lst"), 0644);
  Write(runtime / "recipes/steam-220-hl2-v1.json", Read(fs::path(g_runtime_dir) / "recipes/steam-220-hl2-v1.json"), 0644);
  auto ffxiii = Read(fs::path(g_runtime_dir) / "recipes/steam-292120-ffxiii-v1.json");
  ffxiii.replace(ffxiii.find("\"locked\""), 0, "\"requirements\": {\"audio\": \"ps5\"},\n  ");
  Write(runtime / "recipes/steam-292120-ffxiii-v1.json", ffxiii, 0644);
  config.runtime_dir = runtime.string();
  config.steamworks_dir = (fs::path(g_runtime_dir) / "steamworks").string();
  config.host_namespace_prefix = fixture.root;
  config.package_paths.console_data_root = "/data";
  config.package_paths.host_data_root = fixture.root / "data";
  config.package_operations.hash_file = HashArtifactFile;
  config.package_operations.hash_bytes = HashArtifactBytes;
  config.package_operations.free_space_unverifiable = true;
  config.package_operations.validate_bottle = [](const fs::path& bottle) {
    return fs::is_regular_file(bottle / "system.reg") && fs::is_regular_file(bottle / "user.reg");
  };
  auto kit = SyntheticKit(fixture.root / "kit");
  kit.view.template_audio_ps5 = template_audio_ps5;
  config.admit_kit = [&kit](std::string*) { return std::optional<RuntimeKit>(kit); };
  unsigned uuids = 0;
  config.uuid = [&uuids]() -> std::optional<std::string> {
    ++uuids; return std::string("00000000-0000-4000-8000-00000000000") + std::to_string(uuids);
  };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  Write(fixture.root / "data/homebrew/FAKE77031/games/292120/white_data/prog/win/bin/ffxiiiimg.exe", "existing-game", 0644);
  RuntimeHost host(config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  host.Prepare({292120, "/data/homebrew/FAKE77031/games/292120", "internal", "Final Fantasy XIII"});
  const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && (s.refusals.count(292120) || (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
  }, 60);
  if (template_audio_ps5) {
    if (done.refusals.count(292120)) std::fprintf(stderr, "Prepare refused: %s\n", done.refusals.at(292120).c_str());
    CHECK(!done.refusals.count(292120) && done.installs.size() == 1);
    // Swapped for a kit without PS5 audio, Play is refused before the bottle or the game's files change.
    host.Shutdown();
    const auto user_reg = fixture.root / "data/playden/games/steam-292120/library/prefixes/game/user.reg";
    auto marked = Read(user_reg);
    marked.replace(marked.find("\"VideoMemorySize\"=\"4096\""), 24, "\"VideoMemorySize\"=\"1234\"");
    Write(user_reg, marked, 0600);
    kit.view.template_audio_ps5 = false;
    RuntimeHost swapped(config);
    CHECK(swapped.Start());
    CHECK(WaitFor(swapped, Started).kit_available);
    swapped.Play(292120);
    const auto played = WaitFor(swapped, [](const RuntimeSnapshot& s) { return s.plays_handled > 0; }, 30);
    if (played.play_refusals.count(292120)) std::fprintf(stderr, "Play refused: %s\n", played.play_refusals.at(292120).c_str());
    CHECK(played.play_refusals.count(292120) &&
          played.play_refusals.at(292120).find("lacks what this game needs (PS5 audio)") != std::string::npos);
    CHECK(Read(user_reg).find("\"VideoMemorySize\"=\"1234\"") != std::string::npos);
    swapped.Shutdown();
    return;
  } else {
    CHECK(done.refusals.count(292120) && done.installs.empty());
    CHECK(done.refusals.at(292120).find("lacks what this game needs (PS5 audio)") != std::string::npos);
  }
  host.Shutdown();
}

// The 0.6 layout moves /data/homebrew/FAKE77031/games to /data/playden-games in one rename at
// startup. The runtime host then rewrites the records and republishes each moved Ready game, so
// its descriptor, profile and dxvk.conf name the new folder and it starts from Home again.
struct GamesMove {
  std::mutex mutex;
  std::optional<std::pair<std::string, std::string>> journal;  // at "games-moved"
  int done = 0;
  std::vector<std::string> lines;
  void Wire(RuntimeHostConfig& config) {
    config.moved_games_root = [this] { std::lock_guard<std::mutex> lock(mutex); return journal; };
    config.finish_games_move = [this](std::string*) {
      std::lock_guard<std::mutex> lock(mutex);
      journal.reset(); ++done;
      return true;
    };
    config.log = [this](const std::string& line) { std::lock_guard<std::mutex> lock(mutex); lines.push_back(line); };
  }
  bool Logged(const std::string& prefix) {
    std::lock_guard<std::mutex> lock(mutex);
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) { return line.rfind(prefix, 0) == 0; });
  }
  int Done() { std::lock_guard<std::mutex> lock(mutex); return done; }
};
constexpr const char* kOldGames = "/data/homebrew/FAKE77031/games";
constexpr const char* kNewGames = "/data/playden-games";

RuntimeHostConfig MoveConfig(Fixture& fixture, const RuntimeKit& kit, unsigned* uuids, GamesMove& move) {
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
  config.admit_kit = [&kit](std::string*) { return std::optional<RuntimeKit>(kit); };
  config.uuid = [uuids]() -> std::optional<std::string> {
    char id[37]; std::snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012u", ++*uuids);
    return std::string(id);
  };
  config.unix_time = [] { return std::int64_t{1760000000}; };
  config.settings_record = [](std::uint32_t) -> std::optional<GameSettingsRecord> {
    GameSettingsRecord saved; saved.revision = 1; saved.overrides = {{"resolution", "1280x720"}};
    return saved;
  };
  config.launch_title = [](const std::string&, const std::vector<std::string>&) { return TitleResult{true, 0, "launch"}; };
  move.Wire(config);
  return config;
}

// HL2 prepared under the old games root, with an install script and a Cloud index; then the
// startup rename moves the tree, and the journal says so.
void PrepareThenMoveGames(Fixture& fixture, const RuntimeHostConfig& config, GamesMove& move) {
  const auto old_games = fixture.root / "data/homebrew/FAKE77031/games";
  Write(old_games / "220/hl2.exe", "existing-game", 0644);
  CHECK(chmod((old_games / "220").c_str(), 0755) == 0);
  Write(old_games / "220/installscript.vdf",
        "\"InstallScript\"\n{\n \"Registry\"\n {\n  \"HKEY_LOCAL_MACHINE\\\\SOFTWARE\\\\Valve\\\\HL2\"\n  {\n"
        "   \"string\"\n   {\n    \"Installed Path\" \"%INSTALLDIR%\\\\\"\n   }\n  }\n }\n}\n", 0644);
  {
    RuntimeHost host(config);
    CHECK(host.Start());
    CHECK(WaitFor(host, Started).kit_available);
    host.Prepare(Hl2());
    const auto ready = WaitFor(host, [](const RuntimeSnapshot& s) {
      return !s.preparing_app && (s.refusals.count(220) || (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
    }, 60);
    if (ready.refusals.count(220)) std::fprintf(stderr, "Prepare refused: %s\n", ready.refusals.at(220).c_str());
    CHECK(!ready.refusals.count(220) && ready.installs[0].settings_revision == 1);
    host.Shutdown();
  }
  const auto system_reg = Read(fixture.root / "data/playden/games/steam-220/library/prefixes/game/system.reg");
  CHECK(system_reg.find("FAKE77031\\\\games\\\\220") != std::string::npos);
  Write(old_games / ".playden-saves/220/cloud-index.txt",
        "version=1\naccount=a\nchange=7\ngame_root=/data/homebrew/FAKE77031/games/220\n"
        "prefix_root=/data/playden/games/steam-220/library/prefixes/game\nrule\tg\t1\tsave\t*\tsave/\n"
        "file\tg\t3\t5\tsave/game_root=/data/homebrew/FAKE77031/games/220\n", 0666);  // Cloud writes it under umask 0
  fs::rename(old_games, fixture.root / "data/playden-games");
  std::lock_guard<std::mutex> lock(move.mutex);
  move.journal = std::make_pair(std::string(kOldGames), std::string(kNewGames));
}

// What the moved game's published files and record name after the move finished.
void CheckRepublishedAtNewRoot(Fixture& fixture) {
  const auto records = Read(fixture.root / "data/runtime-state/installs.json");
  CHECK(records.find(R"("install_path":"/data/playden-games/220")") != std::string::npos);
  CHECK(records.find("moved_from") == std::string::npos && records.find("FAKE77031") == std::string::npos);
  const auto descriptor = Read(fixture.root / "data/homebrew/FAKE78001/game-launch.json");
  CHECK(descriptor.find(R"("game_root":"/data/playden-games/220")") != std::string::npos);
  CHECK(descriptor.find(R"("settings_revision":1)") != std::string::npos);
  const auto profiles = fixture.root / "data/playden/games/steam-220/library/profiles";
  const auto profile = Read(profiles / "playden-steam-220.profile");
  CHECK(profile.find("playden-games") != std::string::npos && profile.find("FAKE77031") == std::string::npos);
  CHECK(profile.find("desktop = 1280x720") != std::string::npos);  // the saved settings are kept
  // The metadata keeps paths in hex.
  const auto hex = [](const std::string& text) {
    std::string out;
    for (const unsigned char c : text) { char pair[3]; std::snprintf(pair, sizeof(pair), "%02x", c); out += pair; }
    return out;
  };
  const auto metadata = Read(profiles / ".playden-publication");
  CHECK(metadata.find("dxvk.path_hex=" + hex("/data/playden-games/220/dxvk.conf") + "\n") != std::string::npos);
  // The install script's path follows the folder (ApplySteamworks runs in the republication).
  const auto system_reg = Read(fixture.root / "data/playden/games/steam-220/library/prefixes/game/system.reg");
  CHECK(system_reg.find("playden-games\\\\220") != std::string::npos && system_reg.find("FAKE77031") == std::string::npos);
}

void MigrationRewritesInstallPathsAndRepublishes() {
  Fixture fixture;
  GamesMove move;
  auto kit = SyntheticKit(fixture.root / "kit");
  kit.capabilities = {"dxvk", "offline_bottle", "per_game_title", kKitDxvkConfig, "wow64_i386"};
  unsigned uuids = 0;
  const auto config = MoveConfig(fixture, kit, &uuids, move);
  PrepareThenMoveGames(fixture, config, move);
  const auto registrations = fixture.registry_mutations;

  RuntimeHost host(config);
  CHECK(host.Start());
  const auto started = WaitFor(host, Started);
  CHECK(started.installs.size() == 1 && started.installs[0].state == PreparationState::kReady);
  CHECK(started.installs[0].admission == "ok" && started.installs[0].settings_revision == 1);
  CHECK(move.Logged("startup moved app=220 republished") && move.Done() == 1);
  CheckRepublishedAtNewRoot(fixture);
  // No new registration: the title stays the one Home lists.
  CHECK(fixture.registry_mutations == registrations);
  // The Cloud index follows the game folder; the bottle (prefix_root) did not move.
  const auto index_path = fixture.root / "data/playden-games/.playden-saves/220/cloud-index.txt";
  const auto index = Read(index_path);
  struct stat index_info{};  // the rewrite keeps the file's mode, whatever the umask
  CHECK(stat(index_path.c_str(), &index_info) == 0 && (index_info.st_mode & 07777) == 0666);
  CHECK(index == "version=1\naccount=a\nchange=7\ngame_root=/data/playden-games/220\n"
                 "prefix_root=/data/playden/games/steam-220/library/prefixes/game\nrule\tg\t1\tsave\t*\tsave/\n"
                 "file\tg\t3\t5\tsave/game_root=/data/homebrew/FAKE77031/games/220\n");
  // Play passes the title's whole start check.
  host.Play(220);
  const auto played = WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready || s.play_refusals.count(220); }, 30);
  if (played.play_refusals.count(220)) std::fprintf(stderr, "Play refused: %s\n", played.play_refusals.at(220).c_str());
  CHECK(played.handoff_ready);
  host.Shutdown();
  // Nothing is left to do on the next start.
  RuntimeHost again(config);
  CHECK(again.Start());
  WaitFor(again, Started);
  CHECK(move.Done() == 1);
  again.Shutdown();
}

// A game an older Playden prepared (owned publication, schema 2) and the player then opened up
// over FTP (#73): with a kit that admits unowned folders, the move republishes it unowned.
void MovedModdedGameRepublishesUnowned() {
  Fixture fixture;
  GamesMove move;
  auto kit = SyntheticKit(fixture.root / "kit");
  kit.capabilities = {"dxvk", "offline_bottle", "per_game_title", kKitDxvkConfig, "wow64_i386"};
  unsigned uuids = 0;
  const auto config = MoveConfig(fixture, kit, &uuids, move);
  PrepareThenMoveGames(fixture, config, move);
  const auto profiles = fixture.root / "data/playden/games/steam-220/library/profiles";
  CHECK(Read(profiles / ".playden-publication").find("schema=2\n") != std::string::npos);
  const auto game = fixture.root / "data/playden-games/220";
  Write(game / "GInput.asi", "mod-plugin", 0666);
  CHECK(chmod(game.c_str(), 0777) == 0);
  kit.capabilities = {"dxvk", "game-root-unowned", "offline_bottle", "per_game_title", kKitDxvkConfig, "wow64_i386"};

  RuntimeHost host(config);
  CHECK(host.Start());
  const auto started = WaitFor(host, Started);
  CHECK(started.installs.size() == 1 && started.installs[0].state == PreparationState::kReady);
  CHECK(started.installs[0].admission == "ok");
  CHECK(move.Logged("startup moved app=220 republished") && move.Done() == 1);
  CheckRepublishedAtNewRoot(fixture);
  const auto metadata = Read(profiles / ".playden-publication");
  CHECK(metadata.find("schema=3\n") != std::string::npos && metadata.find("game_root.unowned=1\n") != std::string::npos);
  struct stat info{};
  CHECK(stat(game.c_str(), &info) == 0 && (info.st_mode & 07777) == 0777 && Read(game / "GInput.asi") == "mod-plugin");
  host.Play(220);
  const auto played = WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready || s.play_refusals.count(220); }, 30);
  if (played.play_refusals.count(220)) std::fprintf(stderr, "Play refused: %s\n", played.play_refusals.at(220).c_str());
  CHECK(played.handoff_ready);
  host.Shutdown();
}

// The startup republication runs before the Steam account is known: the emulator's files keep
// the account Play wrote, and Play rewrites them.
void MovedGameStartupKeepsSteamAccount() {
  Fixture fixture;
  GamesMove move;
  const auto kit = SyntheticKit(fixture.root / "kit");
  unsigned uuids = 0;
  auto config = MoveConfig(fixture, kit, &uuids, move);
  config.settings_record = [](std::uint32_t) -> std::optional<GameSettingsRecord> {
    GameSettingsRecord saved; saved.revision = 1; return saved;
  };
  const auto old_games = fixture.root / "data/homebrew/FAKE77031/games";
  std::string pe(0x80 + 26, '\0');
  pe[0] = 'M'; pe[1] = 'Z'; pe[0x3c] = 0x40; pe[0x40] = 'P'; pe[0x41] = 'E';
  pe[0x44] = 0x64; pe[0x45] = static_cast<char>(0x86); pe[0x40 + 24] = 0x0b; pe[0x40 + 25] = 0x02;
  Write(old_games / "4242/Bin/Game.exe", pe, 0644);
  Write(old_games / "4242/Bin/steam_api64.dll", pe + std::string("\0SteamUser021\0", 14), 0644);
  RuntimePrepareRequest request{4242, std::string(kOldGames) + "/4242", "internal", "Example Game"};
  request.launches = {{"", "", "bin\\game.exe", "", "-windowed"}};
  {
    RuntimeHost host(config);
    host.SetSteamAccount("player", 76561198000000001ull);
    CHECK(host.Start());
    CHECK(WaitFor(host, Started).kit_available);
    host.Prepare(request);
    const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
      return !s.preparing_app && (s.refusals.count(4242) || (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
    }, 60);
    if (done.refusals.count(4242)) std::fprintf(stderr, "Prepare refused: %s\n", done.refusals.at(4242).c_str());
    CHECK(!done.refusals.count(4242) && done.installs[0].state == PreparationState::kReady);
    host.Shutdown();
  }
  const auto user_ini = [&] { return Read(fixture.root / "data/playden-games/4242/Bin/steam_settings/configs.user.ini"); };
  fs::rename(old_games, fixture.root / "data/playden-games");
  CHECK(user_ini().find("account_name=player\n") != std::string::npos);
  {
    std::lock_guard<std::mutex> lock(move.mutex);
    move.journal = std::make_pair(std::string(kOldGames), std::string(kNewGames));
  }
  RuntimeHost host(config);
  CHECK(host.Start());
  WaitFor(host, Started);
  CHECK(move.Logged("startup moved app=4242 republished") && move.Done() == 1);
  CHECK(user_ini().find("account_name=player\n") != std::string::npos);
  CHECK(user_ini().find("account_steamid=76561198000000001\n") != std::string::npos);
  CHECK(move.Logged("steamworks app=4242 mode=emulate waits"));
  host.Shutdown();
}

// A moved game whose publication can't be redone (here its saved settings are older than its
// title's) is not prepared again behind the player's back: Play and Update refuse, name the
// confirmed way out (Recreate bottle), and change nothing.
void StuckMovedGameOffersRecreateBottle() {
  Fixture fixture;
  GamesMove move;
  const auto kit = SyntheticKit(fixture.root / "kit");
  unsigned uuids = 0;
  auto config = MoveConfig(fixture, kit, &uuids, move);
  auto revision = std::make_shared<std::atomic<std::uint64_t>>(1);
  config.settings_record = [revision](std::uint32_t) -> std::optional<GameSettingsRecord> {
    GameSettingsRecord saved; saved.revision = revision->load(); saved.overrides = {{"resolution", "1280x720"}};
    return saved;
  };
  bool unregistered = false;
  config.unregister_title = [&unregistered](const std::string&, const std::string&) {
    unregistered = true;
    return TitleRemoverResult{true, 0, "answered"};
  };
  PrepareThenMoveGames(fixture, config, move);
  const auto save = fixture.root / "data/playden/games/steam-220/library/prefixes/game/drive_c/users/steamuser/save.dat";
  Write(save, "player-save", 0600);
  revision->store(0);

  RuntimeHost host(config);
  CHECK(host.Start());
  WaitFor(host, Started);
  CHECK(move.Logged("startup moved app=220 failed: ") && move.Done() == 0);
  const auto records_before = Read(fixture.root / "data/runtime-state/installs.json");
  CHECK(records_before.find("moved_from") != std::string::npos);
  const auto title = host.Snapshot().installs.at(0).title_id;
  host.Play(220);
  const auto played = WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready || s.play_refusals.count(220); }, 30);
  CHECK(played.play_refusals.count(220) &&
        played.play_refusals.at(220).find("Use Maintenance > Recreate bottle on the game page") != std::string::npos);
  const auto before = host.Snapshot().prepares_handled;
  host.Prepare({220, std::string(kNewGames) + "/220", "internal", "Half-Life \"2\""});
  const auto done = WaitFor(host, [before](const RuntimeSnapshot& s) { return s.prepares_handled > before; }, 60);
  CHECK(done.refusals.count(220) &&
        done.refusals.at(220).find("Use Maintenance > Recreate bottle on the game page") != std::string::npos);
  CHECK(done.installs.size() == 1 && done.installs[0].state == PreparationState::kReady && done.installs[0].title_id == title);
  CHECK(!move.Logged("prepare again app=220") && move.Done() == 0 && !unregistered);
  CHECK(Read(fixture.root / "data/runtime-state/installs.json") == records_before);
  CHECK(Read(save) == "player-save" && !fs::exists(fixture.root / "data/playden/games/steam-220.kept"));
  host.Shutdown();
}

// A moved game whose title was busy at startup is republished by Play first, which says so.
void PlayBeforeRepublishRepublishesFirst() {
  Fixture fixture;
  GamesMove move;
  const auto kit = SyntheticKit(fixture.root / "kit");
  unsigned uuids = 0;
  auto config = MoveConfig(fixture, kit, &uuids, move);
  std::vector<std::string> progress;
  std::shared_ptr<RuntimeHost*> current = std::make_shared<RuntimeHost*>(nullptr);
  const auto log = config.log;
  config.log = [log, current, &progress](const std::string& line) {
    log(line);
    // Settings update requests run inside Play's republication; its progress text is up meanwhile.
    if (line.rfind("settings update request app=220", 0) == 0 && *current) {
      const auto shared = (*current)->Shared();
      if (const auto found = shared->play_progress.find(220); found != shared->play_progress.end())
        progress.push_back(found->second);
    }
  };
  PrepareThenMoveGames(fixture, config, move);
  fixture.activity->store(static_cast<int>(NativeActivity::kActive));
  RuntimeHost host(config);
  *current = &host;
  CHECK(host.Start());
  const auto started = WaitFor(host, Started);
  CHECK(move.Logged("startup moved app=220 waits: Close the game first.") && move.Done() == 0);
  // The mark is durable; the published descriptor still names the old folder.
  CHECK(Read(fixture.root / "data/runtime-state/installs.json").find(R"("moved_from":"/data/homebrew/FAKE77031/games/220")") !=
        std::string::npos);
  CHECK(started.installs[0].state == PreparationState::kReady);
  CHECK(Read(fixture.root / "data/homebrew/FAKE78001/game-launch.json").find(kOldGames) != std::string::npos);
  // While it runs, Play says why it can't update the game.
  host.Play(220);
  const auto refused = WaitFor(host, [](const RuntimeSnapshot& s) { return s.play_refusals.count(220) != 0; });
  CHECK(refused.play_refusals.at(220) == "This game needs an update for Playden 0.6 first: Close the game first.");
  fixture.activity->store(static_cast<int>(NativeActivity::kStopped));
  host.Play(220);
  const auto played = WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready; }, 30);
  CHECK(played.play_progress.empty() && !played.play_refusals.count(220));
  CHECK(progress == std::vector<std::string>{kPlayUpdatingForLayout});
  CHECK(move.Logged("play moved app=220 republished") && move.Done() == 1);
  CheckRepublishedAtNewRoot(fixture);
  CHECK(host.Handoff());
  *current = nullptr;
  host.Shutdown();
}

// USB and M.2 installs, and folders that only share the old root's spelling, keep their paths.
void MigrationSkipsRecordsOutsideOldPrefix() {
  Fixture fixture;
  auto config = fixture.Config();  // no kit: the records are rewritten all the same
  // The real layout journal on state_root (no injected callbacks).
  fs::create_directories(config.state_root); CHECK(chmod(config.state_root.c_str(), 0700) == 0);
  const std::vector<std::pair<std::uint32_t, std::string>> games = {
      {400, "/mnt/usb1/playden-games/400"}, {500, "/mnt/ext1/playden-games/500"},
      {600, "/data/homebrew/FAKE77031/games-old/600"}, {700, "/data/homebrew/FAKE77031/games"},
      {220, "/data/homebrew/FAKE77031/games/220"}};
  {
    playden::Storage storage(config.state_root.string());
    RuntimeInstallRepository repository(storage, "installs.json", [](const std::string&) {
      return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree};
    });
    CHECK(repository.Load().ok());
    int title = 1;
    for (const auto& [app, path] : games) {
      auto record = Record();
      ReserveRequest request;
      request.install_id = "steam-" + std::to_string(app); request.app_id = app;
      request.default_profile_id = record.default_profile_id; request.runtime_bundle_id = record.runtime_bundle_id;
      request.recipe_id = record.recipe_id; request.recipe_version = record.recipe_version;
      request.launch_entry_id = record.launch_entry_id; request.launch_path = record.launch_path;
      request.install_path = path; request.volume_id = record.volume_id;
      char id[10]; std::snprintf(id, sizeof(id), "FAKE78%03d", title++);
      request.kit_title_variants = {id};
      CHECK(repository.Reserve(request).ok());
    }
  }
  {
    // A trailing slash on the old root is ignored.
    const std::string journal = std::string(R"({"step":"games-moved","from":")") + kOldGames + R"(/","to":")" + kNewGames +
                                R"(","postponed":0})";
    CHECK(playden::Storage(config.state_root.string()).WriteDurable(kLayoutJournal, playden::Bytes(journal.begin(), journal.end())).ok());
  }
  RuntimeHost host(config);
  CHECK(host.Start());
  WaitFor(host, Started);
  host.Shutdown();
  playden::Storage storage(config.state_root.string());
  RuntimeInstallRepository repository(storage, "installs.json", {});
  CHECK(repository.Load().ok());
  std::map<std::uint32_t, RuntimeInstall> records;
  for (const auto& record : repository.Snapshot()) records[record.app_id] = record;
  CHECK(records.size() == 5);
  for (const std::uint32_t app : {400u, 500u, 600u}) {
    CHECK(records[app].moved_from.empty());
    CHECK(records[app].install_path == std::find_if(games.begin(), games.end(), [app](const auto& g) { return g.first == app; })->second);
  }
  CHECK(records[700].install_path == kNewGames && records[700].moved_from == "/data/homebrew/FAKE77031/games");
  CHECK(records[220].install_path == "/data/playden-games/220" && records[220].moved_from == "/data/homebrew/FAKE77031/games/220");
  // None was Ready, so nothing waits for a republication; the marks stay for their preparation.
  const auto finished = LoadLayoutJournal(config.state_root);
  CHECK(finished && finished->step == "done");
}

// The mark survives restarts: a start that can't republish (busy title) and one interrupted in
// the middle of the republication both leave the move to the next one, which finishes it.
void RestartDuringRepublishResumes() {
  Fixture fixture;
  GamesMove move;
  const auto kit = SyntheticKit(fixture.root / "kit");
  unsigned uuids = 0;
  auto config = MoveConfig(fixture, kit, &uuids, move);
  PrepareThenMoveGames(fixture, config, move);
  fixture.activity->store(static_cast<int>(NativeActivity::kActive));
  {
    RuntimeHost host(config);
    CHECK(host.Start());
    WaitFor(host, Started);
    CHECK(move.Logged("startup moved app=220 waits:") && move.Done() == 0);
    host.Shutdown();
  }
  CHECK(Read(fixture.root / "data/runtime-state/installs.json").find("moved_from") != std::string::npos);
  // The next start's republication is cut off after its request is saved: the console stops
  // answering whether the title runs.
  fixture.activity->store(static_cast<int>(NativeActivity::kStopped));
  auto interrupted = config;
  const auto log = config.log;
  auto activity = fixture.activity;
  interrupted.log = [log, activity](const std::string& line) {
    log(line);
    if (line.rfind("settings update request app=220", 0) == 0) activity->store(static_cast<int>(NativeActivity::kUnknown));
  };
  {
    RuntimeHost host(interrupted);
    CHECK(host.Start());
    const auto started = WaitFor(host, Started);
    CHECK(move.Logged("startup moved app=220 failed:") && move.Done() == 0);
    CHECK(started.installs[0].recovery_pending);
    host.Shutdown();
  }
  CHECK(Read(fixture.root / "data/runtime-state/installs.json").find("moved_from") != std::string::npos);
  // Restarted: the saved request is recovery, which Update resumes; Play says what is missing.
  fixture.activity->store(static_cast<int>(NativeActivity::kStopped));
  RuntimeHost host(config);
  CHECK(host.Start());
  WaitFor(host, Started);
  CHECK(move.Done() == 0);
  host.Play(220);
  const auto refused = WaitFor(host, [](const RuntimeSnapshot& s) { return s.play_refusals.count(220) != 0; });
  CHECK(refused.play_refusals.at(220).rfind("This game needs an update for Playden 0.6 first: ", 0) == 0);
  host.Prepare({220, "/data/playden-games/220", "internal", "Half-Life \"2\""});
  const auto resumed = WaitFor(host, [](const RuntimeSnapshot& s) {
    return !s.preparing_app && s.prepares_handled > 0 && !s.installs.empty() && !s.installs[0].recovery_pending;
  }, 60);
  if (resumed.refusals.count(220)) std::fprintf(stderr, "Resume refused: %s\n", resumed.refusals.at(220).c_str());
  CHECK(!resumed.refusals.count(220) && resumed.installs[0].state == PreparationState::kReady);
  CHECK(move.Done() == 1);
  CheckRepublishedAtNewRoot(fixture);
  host.Play(220);
  CHECK(WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready; }, 30).handoff_ready);
  CHECK(host.Handoff());
  host.Shutdown();
}

// A moved game whose folder is gone from the new place is not published over: it keeps its mark
// and the move stays unfinished.
void MovedGameMissingAtNewRootKeepsTheMove() {
  Fixture fixture;
  GamesMove move;
  const auto kit = SyntheticKit(fixture.root / "kit");
  unsigned uuids = 0;
  const auto config = MoveConfig(fixture, kit, &uuids, move);
  PrepareThenMoveGames(fixture, config, move);
  fs::remove_all(fixture.root / "data/playden-games/220");
  RuntimeHost host(config);
  CHECK(host.Start());
  WaitFor(host, Started);
  CHECK(move.Logged("startup moved app=220 failed:") && move.Done() == 0);
  CHECK(Read(fixture.root / "data/runtime-state/installs.json").find("moved_from") != std::string::npos);
  CHECK(Read(fixture.root / "data/homebrew/FAKE78001/game-launch.json").find(kOldGames) != std::string::npos);
  host.Shutdown();
}

// bottle-profile-shared: the bottle's way to its user profile is 0711 and the profile is open to FTP.
RuntimeKit SharedProfileKit(const fs::path& kit) {
  auto admitted = SyntheticKit(kit);
  admitted.capabilities.push_back(kKitBottleProfileShared);
  std::sort(admitted.capabilities.begin(), admitted.capabilities.end());
  return admitted;
}
unsigned ModeOf(const fs::path& path) {
  struct stat info{};
  CHECK(lstat(path.c_str(), &info) == 0);
  return static_cast<unsigned>(info.st_mode & 07777);
}
// The six folders on the way from the data root to the profile.
std::vector<fs::path> ProfileWay(const fs::path& data) {
  const auto bottle = data / "library/prefixes/game";
  return {data, data / "library", data / "library/prefixes", bottle, bottle / "drive_c", bottle / "drive_c/users"};
}

// A prepared HL2 title with a kit that the test can swap, Play from Playden and a working remover.
struct ProfileRun {
  Fixture fixture;
  RuntimeKit kit;
  unsigned uuids = 0;
  std::vector<std::vector<std::string>> launches;
  RuntimeHostConfig config;
  const fs::path data = fixture.root / "data/playden/games/steam-220";
  const fs::path profile = data / "library/prefixes/game/drive_c/users/prospero";
  explicit ProfileRun(bool shared) {
    kit = shared ? SharedProfileKit(fixture.root / "kit") : SyntheticKit(fixture.root / "kit");
    config = fixture.Config();
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
    config.admit_kit = [this](std::string*) { return std::optional<RuntimeKit>(kit); };
    config.uuid = [this]() -> std::optional<std::string> {
      ++uuids;
      char id[37];
      std::snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012u", uuids);
      return std::string(id);
    };
    config.unix_time = [] { return std::int64_t{1760000000}; };
    config.launch_title = [this](const std::string&, const std::vector<std::string>& arguments) {
      launches.push_back(arguments);
      return TitleResult{true, 0, "launch"};
    };
    config.removal_confirm_polls = 3;
    config.removal_poll_interval = std::chrono::milliseconds(1);
    config.unregister_title = [this](const std::string&, const std::string&) {
      fixture.registered = false; fixture.submitted = false;
      return TitleRemoverResult{true, 0, "answered"};
    };
    Write(fixture.root / "data/homebrew/FAKE77031/games/220/hl2.exe", "existing-game", 0644);
  }
  RuntimeSnapshot Prepare(RuntimeHost& host) {
    host.Prepare(Hl2());
    const auto done = WaitFor(host, [](const RuntimeSnapshot& s) {
      return !s.preparing_app && (s.refusals.count(220) ||
          (!s.installs.empty() && s.installs[0].state == PreparationState::kReady));
    }, 60);
    if (done.refusals.count(220)) std::fprintf(stderr, "Prepare refused: %s\n", done.refusals.at(220).c_str());
    CHECK(!done.refusals.count(220) && done.installs.size() == 1);
    return done;
  }
  RuntimeSnapshot Maintain(RuntimeHost& host, RuntimeMaintenance action) {
    const auto before = host.Snapshot().maintenance_handled;
    host.Maintain(220, action);
    return WaitFor(host, [before](const RuntimeSnapshot& s) { return s.maintenance_handled > before; }, 60);
  }
  // Play from Playden up to the handoff; returns the session it starts.
  std::string Play(RuntimeHost& host) {
    const auto handled = host.Snapshot().plays_handled;
    host.Play(220);
    const auto played = WaitFor(host, [handled](const RuntimeSnapshot& s) {
      return s.handoff_ready || s.plays_handled > handled;
    }, 30);
    if (!played.handoff_ready && played.play_refusals.count(220))
      std::fprintf(stderr, "Play refused: %s\n", played.play_refusals.at(220).c_str());
    WaitFor(host, [](const RuntimeSnapshot& s) { return s.handoff_ready; }, 30);
    CHECK(host.Handoff());
    return launches.back().at(0).substr(std::string("session=").size());
  }
  // The title's wrapper records the session in the private data root.
  void Session(const std::string& id, const std::string& phase) {
    const auto sessions = data / "logs/sessions";
    fs::create_directories(sessions);
    CHECK(chmod((data / "logs").c_str(), 0700) == 0 && chmod(sessions.c_str(), 0700) == 0);
    Write(sessions / (id + ".json"),
          R"({"schema_version":1,"session_id":")" + id +
              R"(","install_id":"steam-220","title_id":"FAKE78001","origin":"playden","settings_revision":0,)"
              R"("runtime_bundle_id":"wine-wow64","phase":")" + phase + "\"}\n", 0600);
  }
};

// Whether the linked publication verifier (the pinned kit's) admits a 0711 data root. One from
// before bottle-profile-shared refuses it, and with it every start check of a shared bottle:
// those checks wait for the pin that brings the capability.
bool VerifierAdmitsSharedBottle() {
  static const bool admits = [] {
    ProfileRun run(false);
    {
      RuntimeHost host(run.config);
      CHECK(host.Start());
      run.Prepare(host);
      host.Shutdown();
    }
    CHECK(chmod(run.data.c_str(), 0711) == 0);
    FilesystemPackageInstaller installer(run.kit.view, run.config.package_paths, run.config.package_operations);
    auto record = Record();
    record.title_id = "FAKE78001";
    record.data_root = "/data/playden/games/steam-220";
    const bool ok = installer.AdmissionDiagnostic(record) == "ok";
    if (!ok)
      std::fprintf(stderr, "note: the linked publication verifier refuses 0711 bottles; "
                           "bottle-profile-shared start checks are skipped until the kit pin admits them\n");
    return ok;
  }();
  return admits;
}

// A kit with bottle-profile-shared makes the bottle's way 0711 and its profile 0777/0666. Saves
// Wine makes (0600, umask 077) are opened again at Play and when the session ends; Repair keeps
// the modes, backups stay private, and saves kept across an uninstall come back open.
void SharedProfileKitOpensSaves() {
  if (!VerifierAdmitsSharedBottle()) return;
  ProfileRun run(true);
  RuntimeHost host(run.config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  auto ready = run.Prepare(host);
  CHECK(ready.installs[0].admission == "ok");
  for (const auto& folder : ProfileWay(run.data)) CHECK(ModeOf(folder) == 0711);
  for (const auto* folder : {".playden-control", "library/profiles", "library/input"}) CHECK(ModeOf(run.data / folder) == 0700);
  CHECK(ModeOf(run.data / "library/prefixes/game/user.reg") == 0600);
  for (const auto* folder : {"", "AppData", "AppData/Roaming", "Documents", "Saved Games"})
    CHECK(ModeOf(run.profile / folder) == 0777);

  // Between sessions (from Home, or over FTP) Wine left a private save: Play opens it first.
  const auto game = run.profile / "AppData/Roaming/Game";
  Write(game / "save.dat", "first", 0600);
  CHECK(chmod(game.c_str(), 0700) == 0);
  const auto session = run.Play(host);
  CHECK(ModeOf(game) == 0777 && ModeOf(game / "save.dat") == 0666);
  // The game saves while it runs; once its session ends the new save is opened too.
  run.Session(session, "running");
  WaitFor(host, [](const RuntimeSnapshot& s) { return s.launch && s.launch->state == LaunchState::kRunning; }, 30);
  Write(game / "later.dat", "second", 0600);
  run.Session(session, "exited");
  WaitFor(host, [](const RuntimeSnapshot& s) { return s.launch && s.launch->state == LaunchState::kExited; }, 30);
  CHECK(ModeOf(game / "later.dat") == 0666);
  CHECK(ModeOf(run.data / "logs/sessions") == 0700 && ModeOf(run.data / "library/prefixes/game/user.reg") == 0600);

  // Repair keeps the modes and the start check passes.
  auto result = run.Maintain(host, RuntimeMaintenance::kRepair);
  CHECK(result.maintenance.at(220).ok);
  for (const auto& folder : ProfileWay(run.data)) CHECK(ModeOf(folder) == 0711);
  // Backups live in Playden's private backup folder.
  result = run.Maintain(host, RuntimeMaintenance::kBackupSaves);
  CHECK(result.maintenance.at(220).ok);
  const auto backups = run.fixture.root / "data/playden/games/steam-220.saves";
  CHECK(ModeOf(backups) == 0700 && ModeOf(backups / "1760000000/users/prospero/AppData/Roaming/Game/save.dat") == 0600);

  // Uninstalled keeping saves, prepared again: the saves come back open.
  const auto removals = host.Snapshot().removals_handled;
  host.Remove(220, false);
  auto removed = WaitFor(host, [removals](const RuntimeSnapshot& s) { return s.removals_handled > removals; }, 30);
  CHECK(removed.installs.empty() && removed.kept_bottles.size() == 1);
  const auto kept = run.fixture.root / "data/playden/games/steam-220.kept";
  CHECK(ModeOf(kept / "library/prefixes/game/drive_c/users/prospero/AppData/Roaming/Game/save.dat") == 0666);
  ready = run.Prepare(host);
  CHECK(ready.kept_bottles.empty() && Read(game / "save.dat") == "first");
  CHECK(ModeOf(game) == 0777 && ModeOf(game / "save.dat") == 0666 && ModeOf(game / "later.dat") == 0666);
  for (const auto& folder : ProfileWay(run.data)) CHECK(ModeOf(folder) == 0711);
  host.Shutdown();
}

// A bottle made before the kit had bottle-profile-shared (all 0700) is opened when Playden starts
// with the new kit, and again by Repair and by the first Play if it was tightened since.
void ExistingBottleOpensWithSharedKit() {
  if (!VerifierAdmitsSharedBottle()) return;
  ProfileRun run(false);
  {
    RuntimeHost host(run.config);
    CHECK(host.Start());
    run.Prepare(host);
    host.Shutdown();
  }
  for (const auto& folder : ProfileWay(run.data)) CHECK(ModeOf(folder) == 0700);
  Write(run.profile / "Documents/My Games/settings.ini", "[video]\n", 0600);
  run.kit = SharedProfileKit(run.fixture.root / "kit");
  const auto tighten = [&run] {
    for (const auto& folder : ProfileWay(run.data)) CHECK(chmod(folder.c_str(), 0700) == 0);
    CHECK(chmod((run.profile / "Documents/My Games/settings.ini").c_str(), 0600) == 0);
  };
  RuntimeHost host(run.config);
  CHECK(host.Start());
  CHECK(WaitFor(host, Started).kit_available);
  for (const auto& folder : ProfileWay(run.data)) CHECK(ModeOf(folder) == 0711);
  CHECK(ModeOf(run.profile / "Documents/My Games") == 0777 && ModeOf(run.profile / "Documents/My Games/settings.ini") == 0666);
  tighten();
  CHECK(run.Maintain(host, RuntimeMaintenance::kRepair).maintenance.at(220).ok);
  for (const auto& folder : ProfileWay(run.data)) CHECK(ModeOf(folder) == 0711);
  CHECK(ModeOf(run.profile / "Documents/My Games/settings.ini") == 0666);
  tighten();
  run.Play(host);
  for (const auto& folder : ProfileWay(run.data)) CHECK(ModeOf(folder) == 0711);
  CHECK(ModeOf(run.profile / "Documents/My Games/settings.ini") == 0666);
  host.Shutdown();
}

// Without the capability nothing opens: Wine's saves stay as they are after Play and after a
// session. A bottle a capable kit opened (then a kit downgrade) fails the strict start check
// until Repair, or the next Play, puts its way back to 0700; the profile keeps its modes.
void KitWithoutSharedProfileKeepsBottlePrivate() {
  ProfileRun run(false);
  RuntimeHost host(run.config);
  CHECK(host.Start());
  run.Prepare(host);
  const auto save = run.profile / "Saved Games/Game/save.dat";
  Write(save, "save", 0600);
  CHECK(chmod(save.parent_path().c_str(), 0700) == 0);
  const auto session = run.Play(host);
  run.Session(session, "exited");
  WaitFor(host, [](const RuntimeSnapshot& s) { return s.launch && s.launch->state == LaunchState::kExited; }, 30);
  CHECK(ModeOf(save) == 0600 && ModeOf(save.parent_path()) == 0700 && ModeOf(run.profile) == 0700);
  for (const auto& folder : ProfileWay(run.data)) CHECK(ModeOf(folder) == 0700);

  const auto open_up = [&run, &save] {
    for (const auto& folder : ProfileWay(run.data)) CHECK(chmod(folder.c_str(), 0711) == 0);
    CHECK(chmod(run.profile.c_str(), 0777) == 0 && chmod(save.c_str(), 0666) == 0);
  };
  open_up();
  auto result = run.Maintain(host, RuntimeMaintenance::kRepair);
  if (!result.maintenance.at(220).ok) std::fprintf(stderr, "Repair: %s\n", result.maintenance.at(220).message.c_str());
  CHECK(result.maintenance.at(220).ok && result.installs[0].admission == "ok");
  for (const auto& folder : ProfileWay(run.data)) CHECK(ModeOf(folder) == 0700);
  CHECK(ModeOf(run.profile) == 0777 && ModeOf(save) == 0666);
  open_up();
  run.Play(host);
  for (const auto& folder : ProfileWay(run.data)) CHECK(ModeOf(folder) == 0700);
  CHECK(ModeOf(save) == 0666);
  host.Shutdown();
}

int main(int argc, char** argv) {
  CHECK(argc == 2);
  g_runtime_dir = argv[1];
  NoKitRefusesPreparationWithoutDurableEffects();
  QueuedPreparationIsPublishedUntilTaken();
  HoldsEffectiveUidWhileWorking();
  StartupReportsJournalsWithoutResuming();
  PublicStateRootIsUnavailable();
  PreparesExistingDownloadToReady();
  PreparesGameOnUnownedDrive();
  PrepareFixesRootOwnedHomebrewFolder();
  PrepareNamesUnfixableFolder();
  AnyTitleKitMakesTheGameItsOwnTitle();
  PreparationEndsAwaitingRegistration();
  PreparesGameWithoutShippedRecipe();
  PreparesProgramChosenInSettings();
  SteamEntryWithoutFolderStartsInInstallRoot();
  PreparesUdkGameWithEulaAccepted();
  ResumesInterruptedCaptureFromSavedRequest();
  ResumesInterruptedCaptureFromSavedRequest(true);
  RefreshesInstalledTitleToRespunKit();
  RetryAfterKitRespinBindsTheNewArtifacts();
  RepairsBottleRegistryModes();
  PlaysReadyTitleThroughHandoff();
  ReplacedKitAsksForRestart();
  MaintainsReadyTitle();
  ModdedGameFolderStillPlays();
  KitWithoutUnownedRootsKeepsTightening();
  for (int route = 0; route <= 5; ++route) OwnedPublicationOpenedUpRecovers(route);
  SettingsSavedDuringPrepareReachReadyTitle();
  KitRespinKeepsWaitingSettingsUpdate();
  RemovesTitleKeepingSaves();
  PreparesGameInstalledAgainElsewhere();
  SharedProfileKitOpensSaves();
  ExistingBottleOpensWithSharedKit();
  KitWithoutSharedProfileKeepsBottlePrivate();
  ShippedRecipePreparesWithAnotherKitsRecipe();
  ShippedRecipeRequirementsFollowTheKit(false);
  ShippedRecipeRequirementsFollowTheKit(true);
  MigrationRewritesInstallPathsAndRepublishes();
  MovedModdedGameRepublishesUnowned();
  MovedGameStartupKeepsSteamAccount();
  StuckMovedGameOffersRecreateBottle();
  PlayBeforeRepublishRepublishesFirst();
  MigrationSkipsRecordsOutsideOldPrefix();
  RestartDuringRepublishResumes();
  MovedGameMissingAtNewRootKeepsTheMove();
  std::puts("runtime host tests passed");
  return 0;
}
