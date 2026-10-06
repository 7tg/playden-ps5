#include "homebrew/runtime_host.hpp"
#include "platform/storage.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace playden::homebrew;
using namespace playden::runtime;
using namespace playden::ps5;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

namespace fs = std::filesystem;
namespace {
struct Fixture {
  fs::path root;
  unsigned database_reads = 0, registry_posts = 0, registry_mutations = 0;
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
    config.native.database = [this](const std::string&) { ++database_reads; return TitleDatabaseObservation{TitleAvailability::kFree, {}}; };
    config.native.activity = [](const std::string&) { return NativeActivity::kStopped; };
    config.registry_transport = [this](const std::string& route, const std::string&) -> std::optional<std::string> {
      ++registry_posts;
      if (route != "/api/v1/games") ++registry_mutations;
      return R"({"status":0,"games":[]})";
    };
    config.make_installer = [](std::string* reason) -> std::unique_ptr<PackageInstaller> {
      *reason = "No verified compatibility kit is installed.";
      return nullptr;
    };
    return config;
  }
};

RuntimeSnapshot WaitFor(const RuntimeHost& host, const std::function<bool(const RuntimeSnapshot&)>& done) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  for (;;) {
    auto snapshot = host.Snapshot();
    if (done(snapshot)) return snapshot;
    CHECK(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}
bool Started(const RuntimeSnapshot& s) { return s.status != RuntimeHostStatus::kStarting; }

std::string Read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

RuntimeInstall Record() {
  RuntimeInstall record;
  record.install_id = "install-220"; record.app_id = 220; record.default_profile_id = "playden-steam-220";
  record.runtime_bundle_id = "wine-wow64"; record.recipe_id = "steam-220-hl2-v1"; record.recipe_version = "1";
  record.launch_entry_id = "main"; record.launch_path = "hl2.exe";
  record.install_path = "/data/homebrew/FAKE77031/games/220"; record.volume_id = "internal";
  return record;
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
  host.Prepare(220);
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
  host.Prepare(220);
  const auto refused = WaitFor(host, [](const RuntimeSnapshot& s) { return s.refusals.count(220) != 0; });
  CHECK(refused.refusals.at(220) == started.reason);
  host.Shutdown();
  struct stat info{};
  CHECK(stat(config.state_root.c_str(), &info) == 0 && (info.st_mode & 0777) == 0755);
}

int main() {
  NoKitRefusesPreparationWithoutDurableEffects();
  StartupReportsJournalsWithoutResuming();
  PublicStateRootIsUnavailable();
  std::puts("runtime host tests passed");
  return 0;
}
