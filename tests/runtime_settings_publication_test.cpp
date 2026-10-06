#include "runtime/settings_publication.h"
#include "runtime/settings_publication_owner.h"
#include "platform/managed_file_lock.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <csignal>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
#include "settings_publication_fixture.h"
namespace fs = std::filesystem;
PackageActionResult Ok() { return {PackageActionState::kComplete, {}, {}}; }
PackageActionResult Fail() { return {PackageActionState::kFailed, "injected", "injected failure"}; }
std::string Read(const fs::path& path) { std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}}; }
struct Test {
  fs::path root, control; SettingsPublicationOperation operation; PublicationPaths paths; PublicationDependencies deps;
  unsigned blocks = 0, preflights = 0, applies = 0, verifies = 0, reconciles = 0, activities = 0;
  bool ready = false, fail_apply = false, fail_verify = false, fail_reconcile = false, stopped = true;
  Test() {
    char pattern[] = "/tmp/playden-publication-XXXXXX"; CHECK(mkdtemp(pattern)); root = fs::canonical(pattern);
    std::string reason; auto frozen = FreezeSettingsPublicationOperation(Fixture(), &reason); CHECK(frozen); operation = *frozen;
    const auto data = root / "data/playden/games/install-220"; control = data / ".playden-control";
    fs::create_directories(control); CHECK(chmod(control.c_str(), 0700) == 0);
    auto directory = playden::platform::ManagedDirectory::Open(control, playden::platform::DirectoryPolicy::kPrivate); CHECK(directory);
    int fd = directory->CreateExclusiveFile("publication.lock", 0600); CHECK(fd >= 0 && fsync(fd) == 0 && close(fd) == 0 && directory->Sync());
    CHECK(chmod(data.c_str(), 0700) == 0);
    for (const auto& suffix : {"library", "library/prefixes", "library/prefixes/game"}) {
      fs::create_directories(data / suffix); CHECK(chmod((data / suffix).c_str(), 0700) == 0);
    }
    std::ofstream(data / "library/prefixes/game/user.reg") << "WINE REGISTRY Version 2\n";
    CHECK(chmod((data / "library/prefixes/game/user.reg").c_str(), 0644) == 0);
    paths = {data / "library/profiles/playden-steam-220.profile", data / "library/profiles/profiles.lst",
             data / "library/input", root / "data/homebrew/FAKE78001/game-launch.json", data / "library/profiles/.playden-publication"};
    // Title/game staging precedes settings publication; the production writer
    // must never create or restage this directory.
    fs::create_directories(paths.descriptor.parent_path() / "games/220");
    deps.host_namespace_prefix = root;
    deps.activity = [&](const RuntimeInstall&) { ++activities; return stopped ? NativeActivity::kStopped : NativeActivity::kUnknown; };
    deps.block_owner = [&](const auto&) { CheckLease(); ++blocks; ready = false; return Ok(); };
    deps.preflight = [&](const auto& operation, const auto& p, int borrowed) {
      CHECK(borrowed >= 0); CheckLease(); ++preflights;
      return PreflightSettingsPublication(operation, p, borrowed, root);
    };
    deps.apply_files = [&](const auto& o, const auto&, int borrowed) {
      CHECK(borrowed >= 0); CheckLease(); CHECK(!ready && Read(control / "publication-state.v1") == o.pending_receipt_bytes);
      ++applies;
      auto result = ApplyPendingPublicationFiles(o, paths, borrowed, root);
      if (result.state != PackageActionState::kComplete) return result;
      return fail_apply ? Fail() : Ok();
    };
    deps.verify_files = [&](const auto& o, const auto&, int borrowed, bool committed) {
      CHECK(borrowed >= 0); CheckLease(); CHECK(!ready);
      CHECK(Read(control / "publication-state.v1") == (committed ? o.committed_receipt_bytes : o.pending_receipt_bytes));
      ++verifies; return fail_verify ? Fail() : VerifySettingsPublicationFiles(o, paths, borrowed, committed, root);
    };
    deps.reconcile_committed = [&](const auto& o) {
      CheckLease(); CHECK(!ready && Read(control / "publication-state.v1") == o.committed_receipt_bytes);
      ++reconciles; if (fail_reconcile) return Fail(); ready = true; return Ok();
    };
  }
  ~Test() { fs::remove_all(root); }
  void CheckLease() {
    auto directory = playden::platform::ManagedDirectory::Open(control, playden::platform::DirectoryPolicy::kPrivate); CHECK(directory);
    CHECK(!playden::platform::ManagedFileLock::Acquire(*directory, "publication.lock", playden::platform::LockMode::kReader));
  }
  std::string Registry() { return Read(root / "data/playden/games/install-220/library/prefixes/game/user.reg"); }
  void ConfigureRegistry() {
    const auto file = root / "data/playden/games/install-220/library/prefixes/game/user.reg";
    std::ofstream(file) << "WINE REGISTRY Version 2\n\n[Software\\\\Unrelated] 42\n\"Keep\"=\"value\"\n";
    auto source = Fixture();
    source.plan.registry = {{"Software\\Wine\\AppDefaults\\probe.exe", "Version", "\"win7\""}};
    source.registry_unix_time = 1760000000;
    auto backup = CreatePublicationRegistryBackup(control, source.transaction_id, Registry(), 0644);
    CHECK(backup.ok()); source.registry_backup = backup.value(); Refresh(source);
    std::string reason; auto frozen = FreezeSettingsPublicationOperation(source, &reason); CHECK(frozen); operation = *frozen;
  }
  fs::path DxvkPath() { return paths.descriptor.parent_path() / "games/220/dxvk.conf"; }
  void ConfigureDxvk(const std::string& before, const std::string& desired, unsigned mode = 0644) {
    auto source = Fixture(); source.plan.dxvk_config = desired;
    fs::create_directories(DxvkPath().parent_path());
    if (!before.empty()) {
      // Effect-only fixtures explicitly authorize their captured old DXVK file.
      // Production preflight requires prior committed ownership instead.
      deps.preflight = [&](const auto&, const auto&, int borrowed) {
        CHECK(borrowed >= 0); CheckLease(); ++preflights; return Ok();
      };
      std::ofstream(DxvkPath()) << before; CHECK(chmod(DxvkPath().c_str(), mode) == 0);
      source.dxvk_before = {true, mode, before.size(), *HashArtifactBytes(before), before};
    }
    Refresh(source); std::string reason;
    auto frozen = FreezeSettingsPublicationOperation(source, &reason); CHECK(frozen); operation = *frozen;
  }
  PackageActionResult Run() { return PublishSettingsOperation(operation, paths, deps); }
};
struct OwnerPackage final : PackageInstaller {
  PackageStatus status{true, true, true, true, true, true, true, false, 0};
  unsigned mutations = 0;
  PackageStatus Inspect(const RuntimeInstall&) override { return status; }
  PackageActionResult StageTitle(const RuntimeInstall&) override { ++mutations; return Fail(); }
  PackageActionResult CopyTemplate(const RuntimeInstall&) override { ++mutations; return Fail(); }
  PackageActionResult ValidateArtifacts(const RuntimeInstall&) override { ++mutations; return Fail(); }
  PackageActionResult PublishProfile(const RuntimeInstall&) override { ++mutations; return Fail(); }
};
struct OwnerNative final : NativeCoordinator {
  NativeActivity activity = NativeActivity::kStopped;
  NativeStatus status{true, false, true, true, false, false};
  unsigned registrations = 0; bool queue = false, fail = false; InstallRecordStore* records = nullptr;
  NativeActivity Activity(const std::string&) override { return activity; }
  NativeStatus Query(const RuntimeInstall&) override { return status; }
  RegistrationResult Register(const RuntimeInstall& record) override {
    auto stored = records->Find(record.install_id); CHECK(stored && stored->journal && stored->journal->step == "register");
    CHECK(record.state == stored->state && record.settings_revision == stored->settings_revision && record.journal);
    CHECK(record.journal->operation_id == stored->journal->operation_id &&
          record.journal->operation == stored->journal->operation && record.journal->step == stored->journal->step);
    CHECK(stored->state == PreparationState::kRegistering); ++registrations;
    if (fail) return {RegistrationResultState::kFailed, "submit-failed", "injected native submission failure"};
    if (queue) status.scan_pending = true; else status.installed = true;
    return {queue ? RegistrationResultState::kScanQueued : RegistrationResultState::kAccepted, {}, {}};
  }
};
struct RejectReadyStore final : InstallRecordStore {
  InstallRecordStore& store; bool reject = false; std::string reject_control_step;
  explicit RejectReadyStore(InstallRecordStore& s) : store(s) {}
  std::optional<RuntimeInstall> Find(const std::string& id) const override { return store.Find(id); }
  std::vector<RuntimeInstall> Snapshot() const override { return store.Snapshot(); }
  RecordUpdateResult Update(const RuntimeInstall& record) override {
    if (record.journal && record.journal->operation == "settings-control" && record.journal->step == reject_control_step)
      return {false, "control-write", "injected control intent persistence refusal"};
    if (reject && record.state == PreparationState::kReady) return {false, "ready-write", "injected Ready persistence refusal"};
    return store.Update(record);
  }
};
void SeedOwner(RuntimeInstallRepository& repository, const RuntimeInstall& r, bool fixture_control_initialized = true) {
  CHECK(repository.Load().ok()); ReserveRequest request;
  request.install_id = r.install_id; request.app_id = r.app_id; request.default_profile_id = r.default_profile_id;
  request.runtime_bundle_id = r.runtime_bundle_id; request.recipe_id = r.recipe_id; request.recipe_version = r.recipe_version;
  request.settings_revision = r.settings_revision; request.launch_entry_id = r.launch_entry_id; request.launch_path = r.launch_path;
  request.launch_arguments = r.launch_arguments; request.launch_working_directory = r.launch_working_directory;
  request.install_path = r.install_path; request.volume_id = r.volume_id; request.kit_title_variants = {r.title_id};
  CHECK(repository.Reserve(request).ok());
  auto owned = r;
  // Most publication fixtures created their control/lock before seeding OWNER.
  // Record that test-only authorized setup; initializer tests opt out and prove
  // the actual production creation and durable intent themselves.
  if (fixture_control_initialized) {
    owned.state = PreparationState::kPreparing;
    owned.journal = PreparationJournal{Fixture().transaction_id, "settings-control", "control-ready"};
  }
  CHECK(repository.Update(owned).ok());
}
int main() {
  {
    Test t; auto missing = t.deps; missing.verify_files = {};
    CHECK(PublishSettingsOperation(t.operation, t.paths, missing).state == PackageActionState::kFailed && t.blocks == 0);
    t.stopped = false; CHECK(t.Run().state == PackageActionState::kFailed && t.blocks == 0);
    t.stopped = true; auto wrong = t.paths; wrong.profile = t.root / "foreign";
    CHECK(PublishSettingsOperation(t.operation, wrong, t.deps).state == PackageActionState::kFailed && t.blocks == 0);
    t.deps.activity = [&](const auto&) { return ++t.activities == 1 ? NativeActivity::kStopped : NativeActivity::kActive; };
    t.activities = 0; CHECK(t.Run().state == PackageActionState::kFailed && t.blocks == 0 && t.applies == 0);
  }
  {
    Test t; auto directory = playden::platform::ManagedDirectory::Open(t.control, playden::platform::DirectoryPolicy::kPrivate); CHECK(directory);
    auto reader = playden::platform::ManagedFileLock::Acquire(*directory, "publication.lock", playden::platform::LockMode::kReader); CHECK(reader);
    CHECK(t.Run().code == "publication-busy" && t.blocks == 0 && t.applies == 0);
    CHECK(!fs::exists(t.control / "settings-operation.v1") && !fs::exists(t.control / "publication-state.v1"));
    reader.reset(); CHECK(t.Run().state == PackageActionState::kComplete && t.ready);
  }
  // Reconstruct/reload after every coordinator durability boundary. Files are
  // actual native verification runs on fixture files; production mutator
  // durability/interruption tests remain a later gate.
  for (unsigned failure = 0; failure < 7; ++failure) {
    Test t;
    t.deps.after_step = [failure](PublicationStep step) { return static_cast<unsigned>(step) != failure; };
    CHECK(t.Run().state == PackageActionState::kFailed && !t.ready);
    const auto before = t.applies;
    const bool committed = Read(t.control / "publication-state.v1") == t.operation.committed_receipt_bytes;
    t.deps.after_step = {};
    CHECK(t.Run().state == PackageActionState::kComplete && t.ready);
    CHECK(Read(t.control / "publication-state.v1") == t.operation.committed_receipt_bytes);
    CHECK(LoadSettingsPublicationOperation(t.control).state == PublicationRecoveryState::kValid);
    if (committed) CHECK(t.applies == before); // Ready/commit retry never reapplies settings.
  }
  // Abrupt process exit bypasses RAII and leaves only the durable filesystem
  // evidence. A different process must acquire the released kernel lease and
  // reconstruct the operation before completing it.
  for (unsigned failure = 0; failure < 7; ++failure) {
    Test t;
    const auto child = fork(); CHECK(child >= 0);
    if (child == 0) {
      t.deps.after_step = [failure](PublicationStep step) {
        if (static_cast<unsigned>(step) == failure) _exit(77);
        return true;
      };
      t.Run(); _exit(78);
    }
    int status = 0; CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 77);
    const bool committed = Read(t.control / "publication-state.v1") == t.operation.committed_receipt_bytes;
    CHECK(t.Run().state == PackageActionState::kComplete && t.ready);
    CHECK(t.applies == (committed ? 0u : 1u));
    CHECK(Read(t.control / "publication-state.v1") == t.operation.committed_receipt_bytes);
  }
  {
    Test t; t.deps.block_owner = [](const auto&) { return Fail(); };
    CHECK(t.Run().state == PackageActionState::kFailed && t.applies == 0 && !fs::exists(t.control / "settings-operation.v1"));
  }
  {
    Test t;
    fs::create_directories(t.paths.profile.parent_path()); CHECK(chmod(t.paths.profile.parent_path().c_str(), 0700) == 0);
    std::ofstream(t.paths.profile) << t.operation.plan.profile_bytes; CHECK(chmod(t.paths.profile.c_str(), 0600) == 0);
    CHECK(t.Run().state == PackageActionState::kFailed && t.applies == 0 && !t.ready);
    CHECK(Read(t.paths.profile) == t.operation.plan.profile_bytes);
    CHECK(!fs::exists(t.control / "settings-operation.v1") && !fs::exists(t.control / "publication-state.v1"));
  }
  {
    Test t; t.deps.after_step = [&](PublicationStep step) {
      if (step == PublicationStep::kFilesApplied) std::ofstream(t.paths.input_directory / "gamepad.input") << "changed";
      return true;
    };
    CHECK(t.Run().code == "publication-verification" && !t.ready && t.reconciles == 0);
    CHECK(Read(t.control / "publication-state.v1") == t.operation.pending_receipt_bytes);
  }
  {
    Test t; t.deps.after_step = [&](PublicationStep step) {
      if (step == PublicationStep::kOwnerBlocked) {
        fs::rename(t.control / "publication.lock", t.control / "retired-lock");
        std::ofstream(t.control / "publication.lock"); CHECK(chmod((t.control / "publication.lock").c_str(), 0600) == 0);
      }
      return true;
    };
    CHECK(t.Run().state == PackageActionState::kFailed && t.applies == 0 && !fs::exists(t.control / "settings-operation.v1"));
  }
  {
    Test t; t.fail_apply = true; CHECK(t.Run().state == PackageActionState::kFailed && !t.ready);
    CHECK(Read(t.control / "publication-state.v1") == t.operation.pending_receipt_bytes && t.reconciles == 0);
    t.fail_apply = false; CHECK(t.Run().state == PackageActionState::kComplete && t.ready);
  }
  {
    Test t; t.fail_verify = true; CHECK(t.Run().state == PackageActionState::kFailed && !t.ready);
    CHECK(Read(t.control / "publication-state.v1") == t.operation.pending_receipt_bytes && t.reconciles == 0);
  }
  {
    Test t; t.fail_reconcile = true; CHECK(t.Run().state == PackageActionState::kFailed && !t.ready);
    CHECK(Read(t.control / "publication-state.v1") == t.operation.committed_receipt_bytes);
    const auto applies = t.applies; t.fail_reconcile = false; t.fail_verify = true;
    CHECK(t.Run().state == PackageActionState::kFailed && !t.ready && t.applies == applies);
    t.fail_verify = false;
    CHECK(t.Run().state == PackageActionState::kComplete && t.ready && t.applies == applies);
  }
  {
    Test t; t.deps.after_step = [](PublicationStep step) { return step != PublicationStep::kJournalDurable; };
    CHECK(t.Run().state == PackageActionState::kFailed && !t.ready);
    auto source = Fixture(); source.install.launch_arguments = "changed"; std::string reason;
    auto changed = FreezeSettingsPublicationOperation(source, &reason); CHECK(changed); t.operation = *changed; t.deps.after_step = {};
    CHECK(t.Run().code == "publication-drift" && t.applies == 0);
  }
  {
    Test t; // Foreign receipts, including exact desired commits without a journal, grant no retry ownership.
    playden::Storage storage(t.control.string()); auto bytes = t.operation.committed_receipt_bytes;
    CHECK(storage.WriteDurable("publication-state.v1", playden::Bytes(bytes.begin(), bytes.end())).ok());
    CHECK(t.Run().code == "publication-collision" && t.applies == 0 && !t.ready);
    CHECK(Read(t.control / "publication-state.v1") == bytes);
  }
  {
    Test t; std::ofstream(t.control / "settings-operation.v1") << "corrupt";
    CHECK(chmod((t.control / "settings-operation.v1").c_str(), 0600) == 0);
    CHECK(t.Run().code == "publication-journal" && t.applies == 0 && !t.ready);
  }
  {
    Test t; t.ConfigureRegistry();
    const auto original = t.Registry(); std::string intended = original, reason;
    CHECK(ApplyRegistryValues(&intended, t.operation.plan.registry, t.operation.registry_unix_time, &reason));
    CHECK(t.Run().state == PackageActionState::kComplete && t.ready && t.Registry() == intended);
    auto backup = LoadPublicationRegistryBackup(t.control, t.operation.transaction_id, *t.operation.registry_backup);
    CHECK(backup.state == PublicationRecoveryState::kValid && backup.bytes == original);
    auto directory = playden::platform::ManagedDirectory::Open(t.control, playden::platform::DirectoryPolicy::kPrivate); CHECK(directory);
    auto lease = playden::platform::ManagedFileLock::Acquire(*directory, "publication.lock", playden::platform::LockMode::kPublisher); CHECK(lease);
    CHECK(ApplyPendingPublicationRegistry(t.operation, lease->fd(), t.root).code == "publication-registry-ownership");
    CHECK(t.Registry() == intended); // committed state never grants mutation permission
  }
  {
    Test t; t.ConfigureRegistry();
    CHECK(ApplyPendingPublicationRegistry(t.operation, -1, t.root).code == "publication-lock");
    const auto file = t.root / "data/playden/games/install-220/library/prefixes/game/user.reg";
    std::ofstream(file) << "WINE REGISTRY Version 2\n; changed after snapshot\n";
    const auto foreign = t.Registry();
    CHECK(t.Run().code == "publication-preflight-snapshot" && !t.ready && t.Registry() == foreign);
    CHECK(!fs::exists(t.control / "publication-state.v1") && !fs::exists(t.control / "settings-operation.v1"));
  }
  {
    Test t; t.ConfigureRegistry();
    const auto child = fork(); CHECK(child >= 0);
    if (child == 0) {
      t.deps.apply_files = [&](const auto& operation, const auto&, int borrowed) {
        CHECK(ApplyPendingPublicationRegistry(operation, borrowed, t.root).state == PackageActionState::kComplete);
        _exit(77); return Fail();
      };
      t.Run(); _exit(78);
    }
    int status = 0; CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 77);
    CHECK(Read(t.control / "publication-state.v1") == t.operation.pending_receipt_bytes);
    CHECK(!fs::exists(t.paths.profile)); // registry was durably published before later files
    const auto applied = t.Registry(); CHECK(applied.find("1760000000") != std::string::npos);
    CHECK(t.Run().state == PackageActionState::kComplete && t.ready && t.Registry() == applied);
  }
  {
    Test t; t.ConfigureRegistry(); const auto original = t.Registry();
    const auto child = fork(); CHECK(child >= 0);
    if (child == 0) {
      t.deps.after_step = [](PublicationStep step) {
        if (step == PublicationStep::kPendingDurable) {
          CHECK(std::signal(SIGXFSZ, SIG_IGN) != SIG_ERR);
          struct rlimit limit{32, 32}; CHECK(setrlimit(RLIMIT_FSIZE, &limit) == 0);
        }
        return true;
      };
      const auto result = t.Run();
      _exit(result.code == "publication-registry-write" ? 77 : 78);
    }
    int status = 0; CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 77);
    CHECK(t.Registry() == original && Read(t.control / "publication-state.v1") == t.operation.pending_receipt_bytes);
    CHECK(!fs::exists(t.paths.profile));
    CHECK(t.Run().state == PackageActionState::kComplete && t.ready);
    auto backup = LoadPublicationRegistryBackup(t.control, t.operation.transaction_id, *t.operation.registry_backup);
    CHECK(backup.state == PublicationRecoveryState::kValid && backup.bytes == original);
  }
  {
    EffectiveSettings settings;
    for (const auto* id : {"frame_limit", "vsync", "frame_latency", "overlay_detail", "reported_vram"}) settings.values[id].available = false;
    settings.values["frame_limit"].available = true; settings.values["frame_limit"].value = "60";
    const auto rendered = RenderDxvkConfig(settings); CHECK(!rendered.empty());
    Test t; t.ConfigureDxvk("", rendered);
    CHECK(t.Run().state == PackageActionState::kComplete && Read(t.DxvkPath()) == rendered);
    auto foreign = Fixture(); foreign.plan.dxvk_config = "# Generated by Playden from this game's settings. foreign header\n";
    Refresh(foreign); std::string reason; CHECK(!FreezeSettingsPublicationOperation(foreign, &reason));
  }
  const std::string dxvk_old = std::string("# Generated by Playden from this game's settings.") + "\ndxgi.maxFrameRate = 30\n";
  const std::string dxvk_new = std::string("# Generated by Playden from this game's settings.") + "\ndxgi.maxFrameRate = 60\n";
  for (const unsigned mode : {0600u, 0644u}) {
    Test t; t.ConfigureDxvk(dxvk_old, dxvk_new, mode);
    CHECK(t.Run().state == PackageActionState::kComplete && t.ready && Read(t.DxvkPath()) == dxvk_new);
    struct stat info{}; CHECK(stat(t.DxvkPath().c_str(), &info) == 0 && (info.st_mode & 07777) == mode);
  }
  {
    Test t; t.ConfigureDxvk("", dxvk_new);
    CHECK(t.Run().state == PackageActionState::kComplete && t.ready && Read(t.DxvkPath()) == dxvk_new);
  }
  {
    Test t; t.ConfigureDxvk(dxvk_old, "");
    CHECK(t.Run().state == PackageActionState::kComplete && t.ready && !fs::exists(t.DxvkPath()));
  }
  {
    Test t; t.ConfigureDxvk(dxvk_old, dxvk_new);
    std::ofstream(t.DxvkPath()) << "foreign";
    CHECK(t.Run().code == "publication-dxvk-collision" && !t.ready && Read(t.DxvkPath()) == "foreign");
    CHECK(Read(t.control / "publication-state.v1") == t.operation.pending_receipt_bytes);
  }
  for (const bool remove : {false, true}) {
    Test t; t.ConfigureDxvk(dxvk_old, remove ? "" : dxvk_new);
    const auto child = fork(); CHECK(child >= 0);
    if (child == 0) {
      t.deps.apply_files = [&](const auto& operation, const auto&, int borrowed) {
        CHECK(ApplyPendingPublicationDxvk(operation, borrowed, t.root).state == PackageActionState::kComplete);
        _exit(77); return Fail();
      };
      t.Run(); _exit(78);
    }
    int status = 0; CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 77);
    CHECK(Read(t.control / "publication-state.v1") == t.operation.pending_receipt_bytes);
    CHECK(!fs::exists(t.paths.profile));
    CHECK(t.Run().state == PackageActionState::kComplete && t.ready);
    CHECK(remove ? !fs::exists(t.DxvkPath()) : Read(t.DxvkPath()) == dxvk_new);
  }
  {
    Test t; t.ConfigureDxvk(dxvk_old, dxvk_new);
    const auto child = fork(); CHECK(child >= 0);
    if (child == 0) {
      t.deps.apply_files = [&](const auto& operation, const auto&, int borrowed) {
        CHECK(std::signal(SIGXFSZ, SIG_IGN) != SIG_ERR);
        struct rlimit limit{32, 32}; CHECK(setrlimit(RLIMIT_FSIZE, &limit) == 0);
        const auto result = ApplyPendingPublicationDxvk(operation, borrowed, t.root);
        _exit(result.code == "publication-dxvk-write" ? 77 : 78); return Fail();
      };
      t.Run(); _exit(78);
    }
    int status = 0; CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 77);
    CHECK(Read(t.DxvkPath()) == dxvk_old && Read(t.control / "publication-state.v1") == t.operation.pending_receipt_bytes);
    CHECK(!fs::exists(t.paths.profile));
    CHECK(t.Run().state == PackageActionState::kComplete && t.ready && Read(t.DxvkPath()) == dxvk_new);
  }
  for (const unsigned bad : {0u, 1u, 2u}) {
    Test t; t.ConfigureDxvk(dxvk_old, dxvk_new);
    if (bad == 0) CHECK(link(t.DxvkPath().c_str(), (t.root / "other-link").c_str()) == 0);
    if (bad == 1) {
      fs::rename(t.DxvkPath(), t.root / "other-target");
      CHECK(symlink((t.root / "other-target").c_str(), t.DxvkPath().c_str()) == 0);
    }
    if (bad == 2) CHECK(chmod(t.DxvkPath().c_str(), 0444) == 0);
    CHECK(t.Run().code == "publication-dxvk-read" && !t.ready && Read(t.DxvkPath()) == dxvk_old);
  }
  for (unsigned boundary = 0; boundary < 7; ++boundary) {
    Test t;
    const std::vector<fs::path> effects = {t.paths.profile.parent_path(), t.paths.input_directory, t.paths.profile,
      t.paths.input_directory / "gamepad.input", t.paths.catalog, t.paths.descriptor, t.paths.metadata};
    const auto save = t.DxvkPath().parent_path() / "save.dat";
    std::ofstream(save) << "save survives";
    const auto child = fork(); CHECK(child >= 0);
    if (child == 0) {
      t.deps.apply_files = [&](const auto& operation, const auto& paths, int borrowed) {
        return ApplyPendingPublicationFiles(operation, paths, borrowed, t.root, [&](const fs::path& effect) {
          if (effect == effects[boundary]) _exit(77);
          return true;
        });
      };
      t.Run(); _exit(78);
    }
    int status = 0; CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 77);
    CHECK(Read(t.control / "publication-state.v1") == t.operation.pending_receipt_bytes);
    CHECK(t.Run().state == PackageActionState::kComplete && t.ready && Read(save) == "save survives");
  }
  {
    Test t;
    const auto child = fork(); CHECK(child >= 0);
    if (child == 0) {
      t.deps.apply_files = [&](const auto& operation, const auto& paths, int borrowed) {
        return ApplyPendingPublicationFiles(operation, paths, borrowed, t.root, [&](const fs::path& effect) {
          if (effect == paths.input_directory) {
            CHECK(std::signal(SIGXFSZ, SIG_IGN) != SIG_ERR);
            struct rlimit limit{32, 32}; CHECK(setrlimit(RLIMIT_FSIZE, &limit) == 0);
          }
          return true;
        });
      };
      const auto result = t.Run(); _exit(result.code == "publication-file-write" ? 77 : 78);
    }
    int status = 0; CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 77);
    CHECK(!fs::exists(t.paths.profile) && Read(t.control / "publication-state.v1") == t.operation.pending_receipt_bytes);
    CHECK(t.Run().state == PackageActionState::kComplete && t.ready);
  }
  {
    Test t; t.deps.after_step = [&](PublicationStep step) {
      if (step == PublicationStep::kPendingDurable) {
        fs::create_directories(t.paths.catalog.parent_path()); CHECK(chmod(t.paths.catalog.parent_path().c_str(), 0700) == 0);
        std::ofstream(t.paths.catalog) << "foreign\n"; CHECK(chmod(t.paths.catalog.c_str(), 0600) == 0);
      }
      return true;
    };
    CHECK(t.Run().code == "publication-file-collision" && !t.ready && Read(t.paths.catalog) == "foreign\n");
    CHECK(Read(t.control / "publication-state.v1") == t.operation.pending_receipt_bytes);
  }
  for (const bool missing_prior : {false, true}) {
    Test t; CHECK(t.Run().state == PackageActionState::kComplete);
    auto source = Fixture(); source.transaction_id = "00000000-0000-7000-8000-000000000003";
    source.previous_revision = 7; source.install.settings_revision = source.plan.settings_revision = 8;
    source.previous_receipt_bytes = t.operation.committed_receipt_bytes; source.previous_metadata_bytes = t.operation.metadata_bytes;
    source.plan.input_presets[0].second = "[input]\nmode = keyboard\ncross = enter\n";
    Refresh(source); std::string reason; auto frozen = FreezeSettingsPublicationOperation(source, &reason); CHECK(frozen);
    auto directory = playden::platform::ManagedDirectory::Open(t.control, playden::platform::DirectoryPolicy::kPrivate); CHECK(directory);
    auto lease = playden::platform::ManagedFileLock::Acquire(*directory, "publication.lock", playden::platform::LockMode::kPublisher); CHECK(lease);
    CHECK(VerifySettingsPublicationFiles(t.operation, t.paths, lease->fd(), true, t.root).state == PackageActionState::kComplete);
    // Test-only next-operation fixture setup; production journal retirement and
    // OWNER admission are separate, still unfinished boundaries.
    playden::Storage storage(t.control.string()); CHECK(storage.RemoveDurable("settings-operation.v1").ok());
    if (missing_prior) {
      fs::remove(t.paths.profile);
      CHECK(PreflightSettingsPublication(*frozen, t.paths, lease->fd(), t.root).state == PackageActionState::kFailed);
    } else CHECK(PreflightSettingsPublication(*frozen, t.paths, lease->fd(), t.root).state == PackageActionState::kComplete);
    CHECK(CreateSettingsPublicationOperation(t.control, *frozen).ok());
    CHECK(storage.WriteDurable("publication-state.v1", playden::Bytes(frozen->pending_receipt_bytes.begin(), frozen->pending_receipt_bytes.end())).ok());
    if (missing_prior) fs::remove(t.paths.profile);
    const auto result = ApplyPendingPublicationFiles(*frozen, t.paths, lease->fd(), t.root);
    if (missing_prior) CHECK(result.code == "publication-file-collision");
    else {
      CHECK(result.state == PackageActionState::kComplete);
      CHECK(Read(t.paths.input_directory / "gamepad.input") == source.plan.input_presets[0].second);
      CHECK(VerifySettingsPublicationFiles(*frozen, t.paths, lease->fd(), false, t.root).state == PackageActionState::kComplete);
    }
  }
  {
    Test t;
    fs::create_directories(t.paths.input_directory); CHECK(chmod(t.paths.input_directory.c_str(), 0700) == 0);
    CHECK(t.Run().code == "publication-preflight-collision" && !t.ready && t.applies == 0);
    CHECK(!fs::exists(t.control / "settings-operation.v1") && !fs::exists(t.control / "publication-state.v1"));
  }
  {
    Test t; const auto rendered = std::string("# Generated by Playden from this game's settings.\ndxgi.maxFrameRate = 60\n");
    auto source = Fixture(); source.plan.dxvk_config = rendered;
    source.dxvk_before = {true, 0644, rendered.size(), *HashArtifactBytes(rendered), rendered}; Refresh(source);
    std::string reason; auto frozen = FreezeSettingsPublicationOperation(source, &reason); CHECK(frozen); t.operation = *frozen;
    std::ofstream(t.DxvkPath()) << rendered; CHECK(chmod(t.DxvkPath().c_str(), 0644) == 0);
    CHECK(t.Run().code == "publication-preflight-collision" && t.applies == 0 && Read(t.DxvkPath()) == rendered);
    CHECK(!fs::exists(t.control / "publication-state.v1"));
  }
  {
    Test t; t.ConfigureRegistry();
    t.deps.after_step = [&](PublicationStep step) {
      if (step == PublicationStep::kPendingDurable) {
        std::ofstream(t.root / "data/playden/games/install-220/library/prefixes/game/user.reg")
            << "WINE REGISTRY Version 2\n; changed during pending\n";
      }
      return true;
    };
    CHECK(t.Run().code == "publication-registry-collision" && !t.ready);
    CHECK(Read(t.control / "publication-state.v1") == t.operation.pending_receipt_bytes);
    CHECK(t.Registry().find("changed during pending") != std::string::npos);
  }
  {
    Test t; CHECK(t.Run().state == PackageActionState::kComplete);
    auto source = Fixture(); source.transaction_id = "00000000-0000-7000-8000-000000000003";
    source.previous_revision = 7; source.install.settings_revision = source.plan.settings_revision = 8;
    source.previous_receipt_bytes = t.operation.committed_receipt_bytes; source.previous_metadata_bytes = t.operation.metadata_bytes;
    source.plan.input_presets.push_back({"extra.input", "[input]\nmode = keyboard\ncross = space\n"});
    Refresh(source); std::string reason; auto frozen = FreezeSettingsPublicationOperation(source, &reason); CHECK(frozen);
    auto directory = playden::platform::ManagedDirectory::Open(t.control, playden::platform::DirectoryPolicy::kPrivate); CHECK(directory);
    auto lease = playden::platform::ManagedFileLock::Acquire(*directory, "publication.lock", playden::platform::LockMode::kPublisher); CHECK(lease);
    CHECK(PreflightSettingsPublication(*frozen, t.paths, lease->fd(), t.root).state == PackageActionState::kComplete);
    // Identical desired bytes in a new path grant no initial ownership.
    const auto extra = t.paths.input_directory / "extra.input";
    std::ofstream(extra) << source.plan.input_presets.back().second; CHECK(chmod(extra.c_str(), 0600) == 0);
    CHECK(PreflightSettingsPublication(*frozen, t.paths, lease->fd(), t.root).code == "publication-preflight-collision");
    CHECK(Read(extra) == source.plan.input_presets.back().second);
    CHECK(Read(t.control / "publication-state.v1") == t.operation.committed_receipt_bytes);
  }
  for (const bool queued : {false, true}) {
    Test t; const auto owner_root = t.root / "owner"; fs::create_directory(owner_root); CHECK(chmod(owner_root.c_str(), 0700) == 0);
    playden::Storage storage(owner_root.string());
    auto availability = [](const std::string&) { return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree}; };
    RuntimeInstallRepository repository(storage, "installs.json", availability); SeedOwner(repository, t.operation.install);
    RepositoryInstallRecordStore base(repository); RejectReadyStore records(base); OwnerPackage package; OwnerNative native;
    native.records = &records; native.queue = queued; records.reject = !queued;
    SettingsPublicationOwner owner(records, package, native);
    auto prepared = owner.Prepare(t.operation.install, t.operation.plan, t.operation.kit_id, t.operation.transaction_id, 1760000000, t.root);
    if (!prepared.ok()) std::fprintf(stderr, "Prepare: %s\n", prepared.message().c_str());
    CHECK(prepared.ok()); t.operation = prepared.value(); t.deps = owner.Dependencies(t.root);
    CHECK(!fs::exists(t.control / "settings-operation.v1") && !fs::exists(t.paths.profile));
    unsigned applies = 0; auto apply = t.deps.apply_files;
    t.deps.apply_files = [&](const auto& o, const auto& paths, int fd) { ++applies; return apply(o, paths, fd); };
    CHECK(t.Run().state == PackageActionState::kFailed && native.registrations == 1 && applies == 1);
    CHECK(Read(t.control / "publication-state.v1") == t.operation.committed_receipt_bytes);
    RuntimeInstallRepository reload(storage, "installs.json", availability); CHECK(reload.Load().ok());
    auto blocked = reload.Find(t.operation.install.install_id); CHECK(blocked && blocked->state == PreparationState::kRegistering);
    CHECK(blocked->journal && blocked->journal->operation_id == t.operation.transaction_id && blocked->journal->step == "await-native");
    auto premature = Fixture(); premature.transaction_id = "00000000-0000-7000-8000-000000000003";
    premature.previous_revision = 7; premature.install.settings_revision = premature.plan.settings_revision = 8;
    premature.previous_receipt_bytes = t.operation.committed_receipt_bytes; premature.previous_metadata_bytes = t.operation.metadata_bytes;
    Refresh(premature); std::string premature_reason; auto premature_frozen = FreezeSettingsPublicationOperation(premature, &premature_reason); CHECK(premature_frozen);
    CHECK(PublishSettingsOperation(*premature_frozen, t.paths, t.deps).code == "publication-retirement-owner");
    CHECK(applies == 1 && native.registrations == 1);
    CHECK(LoadSettingsPublicationOperation(t.control).operation->transaction_id == t.operation.transaction_id);
    RepositoryInstallRecordStore resumed_base(reload); RejectReadyStore resumed_records(resumed_base);
    native.records = &resumed_records;
    SettingsPublicationOwner resumed_owner(resumed_records, package, native);
    auto recovered_intent = resumed_owner.Prepare(t.operation.install, t.operation.plan, t.operation.kit_id,
        t.operation.transaction_id, 1900000000, t.root);
    CHECK(recovered_intent.ok() && recovered_intent.value().registry_unix_time == 1760000000);
    CHECK(recovered_intent.value().plan_sha256 == t.operation.plan_sha256);
    auto changed_plan = t.operation.plan; changed_plan.profile_bytes += "# changed\n";
    CHECK(!resumed_owner.Prepare(t.operation.install, changed_plan, t.operation.kit_id, t.operation.transaction_id, 1900000000, t.root).ok());
    t.deps = resumed_owner.Dependencies(t.root);
    t.deps.apply_files = [&](const auto& o, const auto& paths, int fd) { ++applies; return apply(o, paths, fd); };
    if (queued) {
      CHECK(t.Run().code == "publication-registration-pending" && native.registrations == 1 && applies == 1);
      native.status.installed = true; native.status.scan_pending = false;
    }
    CHECK(t.Run().state == PackageActionState::kComplete && native.registrations == 1 && applies == 1 && package.mutations == 0);
    CHECK(reload.Load().ok()); auto ready = reload.Find(t.operation.install.install_id);
    CHECK(ready && ready->state == PreparationState::kReady && !ready->journal && !ready->last_failure);
    CHECK(ready->settings_revision == t.operation.plan.settings_revision);
    if (!queued) {
      auto source = Fixture(); source.transaction_id = "00000000-0000-7000-8000-000000000003";
      source.previous_revision = t.operation.plan.settings_revision;
      source.previous_receipt_bytes = t.operation.committed_receipt_bytes; source.previous_metadata_bytes = t.operation.metadata_bytes;
      source.install.settings_revision = source.plan.settings_revision = 8;
      auto next = resumed_owner.Prepare(source.install, source.plan, source.kit_id, source.transaction_id, 1760000100, t.root);
      if (!next.ok()) std::fprintf(stderr, "Prepare next: %s\n", next.message().c_str()); CHECK(next.ok());
      auto frozen = std::optional<SettingsPublicationOperation>(next.value());
      CHECK(frozen->previous_revision == 7 && frozen->previous_receipt_bytes == t.operation.committed_receipt_bytes);
      CHECK(frozen->install.state == PreparationState::kReady && !frozen->install.journal);
      const auto archive = t.control / ("settings-operation-" + t.operation.transaction_id + ".v1");
      t.operation = *frozen; native.status.installed = false;
      // Missing existing registration blocks retirement before any new effects.
      CHECK(t.Run().code == "publication-retirement-owner" && native.registrations == 1 && applies == 1);
      CHECK(!fs::exists(archive)); native.status.installed = true;
      t.deps.after_step = [](PublicationStep step) { return step != PublicationStep::kPreviousArchived; };
      CHECK(t.Run().state == PackageActionState::kFailed && fs::exists(archive) && !fs::exists(t.control / "settings-operation.v1"));
      CHECK(resumed_records.Find(t.operation.install.install_id)->state == PreparationState::kReady);
      auto after_archive = resumed_owner.Prepare(t.operation.install, t.operation.plan, t.operation.kit_id,
          t.operation.transaction_id, 1900000000, t.root);
      CHECK(after_archive.ok() && after_archive.value().plan_sha256 == t.operation.plan_sha256);
      CHECK(after_archive.value().registry_unix_time == 1760000100);
      t.deps.after_step = {};
      native.status.installed = false;
      CHECK(t.Run().code == "publication-native-incoherent" && native.registrations == 1 && applies == 2);
      CHECK(resumed_records.Find(t.operation.install.install_id)->state != PreparationState::kReady);
      native.status.installed = true;
      CHECK(t.Run().state == PackageActionState::kComplete && native.registrations == 1 && applies == 2);
      CHECK(reload.Load().ok()); CHECK(reload.Find(t.operation.install.install_id)->settings_revision == 8);
    }
  }
  for (unsigned fault = 0; fault < 5; ++fault) {
    Test t; const auto owner_root = t.root / "owner"; fs::create_directory(owner_root); CHECK(chmod(owner_root.c_str(), 0700) == 0);
    playden::Storage storage(owner_root.string());
    auto availability = [](const std::string&) { return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree}; };
    RuntimeInstallRepository repository(storage, "installs.json", availability); SeedOwner(repository, t.operation.install);
    RepositoryInstallRecordStore records(repository); OwnerPackage package; OwnerNative native; native.records = &records;
    if (fault == 0) { auto changed = t.operation.install; changed.launch_arguments = "drift"; CHECK(repository.Update(changed).ok()); }
    if (fault == 1) package.status.artifacts_valid = false;
    if (fault == 2) native.activity = NativeActivity::kUnknown;
    if (fault == 3) native.fail = true;
    if (fault == 4) native.status.scan_pending = true;
    SettingsPublicationOwner owner(records, package, native); t.deps = owner.Dependencies(t.root);
    CHECK(t.Run().state == PackageActionState::kFailed && package.mutations == 0);
    CHECK(repository.Find(t.operation.install.install_id)->state != PreparationState::kReady);
    if (fault < 3) {
      CHECK(native.registrations == 0 && !fs::exists(t.control / "settings-operation.v1") && !fs::exists(t.control / "publication-state.v1"));
    } else if (fault == 4) {
      auto blocked = repository.Find(t.operation.install.install_id);
      CHECK(blocked->state == PreparationState::kRegistering && blocked->journal && blocked->journal->step == "await-native");
      native.status.scan_pending = false;
      CHECK(t.Run().code == "publication-registration-pending" && native.registrations == 0);
      native.status.installed = true;
      CHECK(t.Run().state == PackageActionState::kComplete && native.registrations == 0);
    } else {
      CHECK(native.registrations == 1);
      auto blocked = repository.Find(t.operation.install.install_id); CHECK(blocked->journal && blocked->journal->step == "register");
      CHECK(t.Run().code == "publication-registration-pending" && native.registrations == 1);
      native.status.installed = true; native.fail = false;
      CHECK(t.Run().state == PackageActionState::kComplete && native.registrations == 1);
    }
  }
  {
    Test t; const auto owner_root = t.root / "owner"; fs::create_directory(owner_root); CHECK(chmod(owner_root.c_str(), 0700) == 0);
    playden::Storage storage(owner_root.string());
    auto availability = [](const std::string&) { return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree}; };
    RuntimeInstallRepository repository(storage, "installs.json", availability); SeedOwner(repository, t.operation.install);
    RepositoryInstallRecordStore records(repository); OwnerPackage package; OwnerNative native; native.records = &records;
    SettingsPublicationOwner owner(records, package, native);
    auto plan = t.operation.plan;
    plan.registry = {{"Software\\Wine\\AppDefaults\\probe.exe", "Version", "\"win10\""},
                     {"Control Panel\\Desktop", "LogPixels", "dword:000000a8"}};
    plan.dxvk_config = "# Generated by Playden from this game's settings. Changes here are replaced on the next launch.\ndxgi.maxFrameRate = 30\n";
    const auto original = t.Registry();
    const auto producer = fork(); CHECK(producer >= 0);
    if (producer == 0) {
      auto captured = owner.Prepare(t.operation.install, plan, t.operation.kit_id, t.operation.transaction_id, 1760000000, t.root);
      _exit(captured.ok() ? 77 : 78);
    }
    int status = 0; CHECK(waitpid(producer, &status, 0) == producer && WIFEXITED(status) && WEXITSTATUS(status) == 77);
    RuntimeInstallRepository restart(storage, "installs.json", availability); CHECK(restart.Load().ok());
    RepositoryInstallRecordStore restarted_records(restart); native.records = &restarted_records;
    SettingsPublicationOwner restarted_owner(restarted_records, package, native);
    auto prepared = restarted_owner.Prepare(t.operation.install, plan, t.operation.kit_id, t.operation.transaction_id, 1900000000, t.root);
    if (!prepared.ok()) std::fprintf(stderr, "Prepare registry: %s\n", prepared.message().c_str()); CHECK(prepared.ok());
    CHECK(prepared.value().registry_unix_time == 1760000000);
    CHECK(prepared.value().registry_backup && t.Registry() == original && !fs::exists(t.DxvkPath()));
    CHECK(!fs::exists(t.control / "settings-operation.v1") && !fs::exists(t.control / "publication-state.v1"));
    // A complete durable prepared intent authorizes this exact backup's recovery.
    auto intent = LoadPreparedSettingsPublicationOperation(t.control, t.operation.transaction_id);
    CHECK(intent.state == PublicationRecoveryState::kValid && intent.operation);
    auto again = restarted_owner.Prepare(t.operation.install, plan, t.operation.kit_id, t.operation.transaction_id, 1900000000, t.root);
    CHECK(again.ok() && again.value().plan_sha256 == prepared.value().plan_sha256 && again.value().registry_unix_time == 1760000000);
    t.operation = prepared.value(); t.deps = restarted_owner.Dependencies(t.root);
    CHECK(t.Run().state == PackageActionState::kComplete && native.registrations == 1);
    CHECK(t.Registry().find("\"Version\"=\"win10\"") != std::string::npos);
    CHECK(t.Registry().find("\"LogPixels\"=dword:000000a8") != std::string::npos);
    CHECK(Read(t.DxvkPath()) == plan.dxvk_config);
    auto backup = LoadPublicationRegistryBackup(t.control, t.operation.transaction_id, *t.operation.registry_backup);
    CHECK(backup.state == PublicationRecoveryState::kValid && backup.bytes == original);
  }
  // Service recovery discovers the frozen operation through persisted OWNER
  // UUID after producer exit, then resumes queued native registration without
  // regenerating a plan or overwriting the newer registration arm.
  {
    Test t; fs::remove_all(t.control);
    const auto owner_root = t.root / "owner"; fs::create_directory(owner_root); CHECK(chmod(owner_root.c_str(), 0700) == 0);
    playden::Storage storage(owner_root.string());
    auto availability = [](const std::string&) { return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree}; };
    RuntimeInstallRepository repository(storage, "installs.json", availability); SeedOwner(repository, t.operation.install, false);
    RepositoryInstallRecordStore records(repository); OwnerPackage package; OwnerNative native; native.records = &records; native.queue = true;
    SettingsPublicationOwner owner(records, package, native);
    CHECK(owner.Prepare(t.operation.install, t.operation.plan, t.operation.kit_id, t.operation.transaction_id, 1760000000, t.root).ok());
    RuntimeInstallRepository restarted(storage, "installs.json", availability); CHECK(restarted.Load().ok());
    RepositoryInstallRecordStore recovered_records(restarted); native.records = &recovered_records;
    SettingsPublicationOwner resumed_owner(recovered_records, package, native);
    RuntimeInstallService service(recovered_records, package, native, {}, {},
        [&](const RuntimeInstall& record) { return resumed_owner.Resume(record, t.root); });
    CHECK(!service.Step(t.operation.install.install_id).ok());
    auto observed = recovered_records.Find(t.operation.install.install_id);
    CHECK(observed && observed->state == PreparationState::kRegistering && observed->journal);
    CHECK(observed->journal->operation == "settings-publication" && observed->journal->step == "await-native" && !observed->last_failure);
    CHECK(native.registrations == 1 && package.mutations == 0);
    const auto profile = Read(t.paths.profile);
    CHECK(Read(t.control / "publication-state.v1") == LoadSettingsPublicationOperation(t.control).operation->committed_receipt_bytes);
    CHECK(!service.Step(t.operation.install.install_id).ok() && native.registrations == 1 && Read(t.paths.profile) == profile);
    native.status.installed = true; native.status.scan_pending = false;
    CHECK(service.Step(t.operation.install.install_id).ok() && native.registrations == 1);
    observed = recovered_records.Find(t.operation.install.install_id);
    CHECK(observed->state == PreparationState::kReady && !observed->journal && !observed->last_failure);
    // Updates keep the previous OWNER revision while pending, then recovery
    // advances it to the frozen desired revision under the same real protocol.
    const auto previous_revision = t.operation.plan.settings_revision;
    auto desired = t.operation.install; desired.settings_revision = previous_revision + 1;
    auto next_plan = t.operation.plan; next_plan.settings_revision = previous_revision + 1;
    auto next = resumed_owner.Prepare(desired, next_plan, t.operation.kit_id,
        "22222222-2222-4222-8222-222222222222", 1760000010, t.root);
    CHECK(next.ok()); t.operation = next.value(); t.deps = resumed_owner.Dependencies(t.root);
    t.deps.after_step = [](PublicationStep step) { return step != PublicationStep::kPendingDurable; };
    CHECK(t.Run().state == PackageActionState::kFailed);
    observed = recovered_records.Find(t.operation.install.install_id);
    CHECK(observed->settings_revision == previous_revision && observed->journal->operation_id == t.operation.transaction_id);
    CHECK(service.Step(t.operation.install.install_id).ok() && native.registrations == 1);
    CHECK(recovered_records.Find(t.operation.install.install_id)->settings_revision == previous_revision + 1);
  }
  // A managed UUID discovers only its exact frozen journal. Corrupt/missing
  // evidence is refused and retained, without legacy profile effects/register.
  for (unsigned damage = 0; damage < 3; ++damage) {
    Test t; fs::remove_all(t.control);
    const auto owner_root = t.root / "owner"; fs::create_directory(owner_root); CHECK(chmod(owner_root.c_str(), 0700) == 0);
    playden::Storage storage(owner_root.string());
    auto availability = [](const std::string&) { return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree}; };
    RuntimeInstallRepository repository(storage, "installs.json", availability); SeedOwner(repository, t.operation.install, false);
    RepositoryInstallRecordStore records(repository); OwnerPackage package; OwnerNative native;
    SettingsPublicationOwner owner(records, package, native);
    CHECK(owner.InitializeControl(t.operation.install, t.operation.transaction_id, t.root).state == PackageActionState::kComplete);
    if (damage > 0) CHECK(owner.Prepare(t.operation.install, t.operation.plan, t.operation.kit_id, t.operation.transaction_id, 1760000000, t.root).ok());
    if (damage == 1) std::ofstream(t.control / ("settings-intent-" + t.operation.transaction_id + ".v1"), std::ios::binary | std::ios::trunc) << "corrupt";
    if (damage == 2) {
      auto stored = *records.Find(t.operation.install.install_id);
      stored.journal->operation_id = "22222222-2222-4222-8222-222222222222"; CHECK(records.Update(stored).ok);
    }
    const auto before = Read(owner_root / "installs.json");
    RuntimeInstallService service(records, package, native, {}, {}, [&](const RuntimeInstall& record) { return owner.Resume(record, t.root); });
    CHECK(!service.Step(t.operation.install.install_id).ok());
    CHECK(Read(owner_root / "installs.json") == before && package.mutations == 0 && native.registrations == 0);
    CHECK(!fs::exists(t.paths.profile) && !fs::exists(t.control / "publication-state.v1"));
    if (damage == 1) CHECK(Read(t.control / ("settings-intent-" + t.operation.transaction_id + ".v1")) == "corrupt");
  }
  // Initial control setup recovers from real process exit at each durability
  // boundary, with a fresh repository and no manually recreated lock/directory.
  for (unsigned boundary = 0; boundary < 4; ++boundary) {
    Test t; fs::remove_all(t.control);
    const auto owner_root = t.root / "owner"; fs::create_directory(owner_root); CHECK(chmod(owner_root.c_str(), 0700) == 0);
    playden::Storage storage(owner_root.string());
    auto availability = [](const std::string&) { return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree}; };
    RuntimeInstallRepository repository(storage, "installs.json", availability); SeedOwner(repository, t.operation.install, false);
    RepositoryInstallRecordStore records(repository); OwnerPackage package; OwnerNative native; native.records = &records;
    SettingsPublicationOwner owner(records, package, native);
    const auto child = fork(); CHECK(child >= 0);
    if (child == 0) {
      owner.InitializeControl(t.operation.install, t.operation.transaction_id, t.root, [boundary](PublicationControlStep step) {
        if (static_cast<unsigned>(step) == boundary) _exit(77);
        return true;
      });
      _exit(78);
    }
    int status = 0; CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 77);
    RuntimeInstallRepository restarted(storage, "installs.json", availability); CHECK(restarted.Load().ok());
    RepositoryInstallRecordStore resumed_records(restarted); native.records = &resumed_records;
    SettingsPublicationOwner resumed(resumed_records, package, native);
    auto durable = resumed_records.Find(t.operation.install.install_id); CHECK(durable && durable->journal);
    CHECK(durable->state == PreparationState::kPreparing && durable->journal->operation == "settings-control");
    CHECK(durable->journal->operation_id == t.operation.transaction_id);
    CHECK(resumed.InitializeControl(t.operation.install, "22222222-2222-4222-8222-222222222222", t.root).state == PackageActionState::kFailed);
    CHECK(resumed.InitializeControl(t.operation.install, t.operation.transaction_id, t.root).state == PackageActionState::kComplete);
    struct stat original{}, after{}; CHECK(lstat((t.control / "publication.lock").c_str(), &original) == 0);
    CHECK(original.st_uid == geteuid() && (original.st_mode & 07777) == 0600 && original.st_size == 0 && original.st_nlink == 1);
    CHECK(resumed.InitializeControl(t.operation.install, t.operation.transaction_id, t.root).state == PackageActionState::kComplete);
    CHECK(lstat((t.control / "publication.lock").c_str(), &after) == 0 && original.st_ino == after.st_ino);
    auto captured = resumed.Prepare(t.operation.install, t.operation.plan, t.operation.kit_id, t.operation.transaction_id, 1760000000, t.root);
    CHECK(captured.ok()); t.operation = captured.value(); t.deps = resumed.Dependencies(t.root);
    CHECK(t.Run().state == PackageActionState::kComplete && native.registrations == 1 && package.mutations == 0);
  }
  // A preexisting control path, even an empty private directory, cannot be
  // adopted without the durable creation intent. Failed admission changes no OWNER.
  for (unsigned collision = 0; collision < 6; ++collision) {
    Test t; fs::remove_all(t.control);
    const auto owner_root = t.root / "owner"; fs::create_directory(owner_root); CHECK(chmod(owner_root.c_str(), 0700) == 0);
    playden::Storage storage(owner_root.string());
    auto availability = [](const std::string&) { return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree}; };
    RuntimeInstallRepository repository(storage, "installs.json", availability); SeedOwner(repository, t.operation.install, false);
    RepositoryInstallRecordStore records(repository); OwnerPackage package; OwnerNative native;
    SettingsPublicationOwner owner(records, package, native);
    if (collision == 0) { fs::create_directory(t.control); CHECK(chmod(t.control.c_str(), 0700) == 0); }
    if (collision == 1) CHECK(symlink(t.paths.descriptor.parent_path().c_str(), t.control.c_str()) == 0);
    if (collision == 2) native.activity = NativeActivity::kUnknown;
    if (collision == 3) package.status.artifacts_valid = false;
    if (collision == 4) CHECK(chmod(t.control.parent_path().c_str(), 0755) == 0);
    const auto before = Read(owner_root / "installs.json");
    const auto txn = collision == 5 ? "../invalid" : t.operation.transaction_id;
    CHECK(owner.InitializeControl(t.operation.install, txn, t.root).state == PackageActionState::kFailed);
    CHECK(Read(owner_root / "installs.json") == before && native.registrations == 0 && package.mutations == 0);
    if (collision > 1) CHECK(!fs::exists(t.control));
  }
  // No filesystem setup precedes durable OWNER admission; failed completion
  // persistence leaves its original arm and the same lock for explicit retry.
  for (const auto* rejected_step : {"initialize-control", "control-ready"}) {
    Test t; fs::remove_all(t.control);
    const auto owner_root = t.root / "owner"; fs::create_directory(owner_root); CHECK(chmod(owner_root.c_str(), 0700) == 0);
    playden::Storage storage(owner_root.string());
    auto availability = [](const std::string&) { return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree}; };
    RuntimeInstallRepository repository(storage, "installs.json", availability); SeedOwner(repository, t.operation.install, false);
    RepositoryInstallRecordStore base(repository); RejectReadyStore records(base); records.reject_control_step = rejected_step;
    OwnerPackage package; OwnerNative native; SettingsPublicationOwner owner(records, package, native);
    CHECK(owner.InitializeControl(t.operation.install, t.operation.transaction_id, t.root).state == PackageActionState::kFailed);
    const bool armed = std::string(rejected_step) == "control-ready";
    CHECK(fs::exists(t.control / "publication.lock") == armed && fs::exists(t.control) == armed);
    auto observed = records.Find(t.operation.install.install_id); CHECK(observed && observed->journal);
    CHECK(observed->journal->operation == (armed ? "settings-control" : "prepare"));
    records.reject_control_step.clear();
    CHECK(owner.InitializeControl(t.operation.install, t.operation.transaction_id, t.root).state == PackageActionState::kComplete);
  }
  // Matching creation intent does not permit malformed/replaced lock adoption.
  for (unsigned damage = 0; damage < 7; ++damage) {
    Test t; fs::remove_all(t.control);
    const auto owner_root = t.root / "owner"; fs::create_directory(owner_root); CHECK(chmod(owner_root.c_str(), 0700) == 0);
    playden::Storage storage(owner_root.string());
    auto availability = [](const std::string&) { return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree}; };
    RuntimeInstallRepository repository(storage, "installs.json", availability); SeedOwner(repository, t.operation.install, false);
    RepositoryInstallRecordStore records(repository); OwnerPackage package; OwnerNative native;
    SettingsPublicationOwner owner(records, package, native);
    // Stop before lock creation, or after completion for missing-inode cases.
    const auto boundary = damage < 5 ? PublicationControlStep::kDirectorySynced : PublicationControlStep::kOwnerPrepared;
    CHECK(owner.InitializeControl(t.operation.install, t.operation.transaction_id, t.root,
        [boundary](PublicationControlStep step) { return step != boundary; }).state == PackageActionState::kFailed);
    const auto lock_path = t.control / "publication.lock";
    if (damage < 4) { std::ofstream(lock_path) << (damage == 1 ? "foreign" : ""); CHECK(chmod(lock_path.c_str(), 0600) == 0); }
    if (damage == 0) CHECK(chmod(lock_path.c_str(), 0644) == 0);
    if (damage == 2) CHECK(link(lock_path.c_str(), (t.root / "foreign-link").c_str()) == 0);
    if (damage == 3 && geteuid() == 0) CHECK(chown(lock_path.c_str(), 1, -1) == 0);
    if (damage == 3 && geteuid() != 0) CHECK(chmod(lock_path.c_str(), 0400) == 0);
    if (damage == 4) CHECK(symlink((t.root / "absent").c_str(), lock_path.c_str()) == 0);
    if (damage == 5) CHECK(unlink(lock_path.c_str()) == 0);
    if (damage == 6) fs::remove_all(t.control);
    const auto before = Read(owner_root / "installs.json");
    CHECK(owner.InitializeControl(t.operation.install, t.operation.transaction_id, t.root).state == PackageActionState::kFailed);
    CHECK(Read(owner_root / "installs.json") == before && package.mutations == 0 && native.registrations == 0);
    if (damage > 4) CHECK(!fs::exists(lock_path));
  }
  // Activity changes between directory and lock creation stop further effects.
  {
    Test t; fs::remove_all(t.control);
    const auto owner_root = t.root / "owner"; fs::create_directory(owner_root); CHECK(chmod(owner_root.c_str(), 0700) == 0);
    playden::Storage storage(owner_root.string());
    auto availability = [](const std::string&) { return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree}; };
    RuntimeInstallRepository repository(storage, "installs.json", availability); SeedOwner(repository, t.operation.install, false);
    RepositoryInstallRecordStore records(repository); OwnerPackage package; OwnerNative native;
    SettingsPublicationOwner owner(records, package, native);
    CHECK(owner.InitializeControl(t.operation.install, t.operation.transaction_id, t.root, [&](PublicationControlStep step) {
      if (step == PublicationControlStep::kDirectorySynced) native.activity = NativeActivity::kActive;
      return true;
    }).state == PackageActionState::kFailed);
    CHECK(fs::is_directory(t.control) && !fs::exists(t.control / "publication.lock"));
    native.activity = NativeActivity::kStopped;
    CHECK(owner.InitializeControl(t.operation.install, t.operation.transaction_id, t.root).state == PackageActionState::kComplete);
    auto control = playden::platform::ManagedDirectory::Open(t.control, playden::platform::DirectoryPolicy::kPrivate); CHECK(control);
    auto reader = playden::platform::ManagedFileLock::Acquire(*control, "publication.lock", playden::platform::LockMode::kReader); CHECK(reader);
    const auto before = Read(owner_root / "installs.json");
    CHECK(owner.InitializeControl(t.operation.install, t.operation.transaction_id, t.root).state == PackageActionState::kFailed);
    CHECK(!owner.Prepare(t.operation.install, t.operation.plan, t.operation.kit_id, t.operation.transaction_id, 1760000000, t.root).ok());
    CHECK(Read(owner_root / "installs.json") == before && !fs::exists(t.control / "publication-state.v1"));
  }
  // Prepare cannot silently adopt the foreign private control path either.
  {
    Test t;
    const auto owner_root = t.root / "owner"; fs::create_directory(owner_root); CHECK(chmod(owner_root.c_str(), 0700) == 0);
    playden::Storage storage(owner_root.string());
    auto availability = [](const std::string&) { return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree}; };
    RuntimeInstallRepository repository(storage, "installs.json", availability); SeedOwner(repository, t.operation.install, false);
    RepositoryInstallRecordStore records(repository); OwnerPackage package; OwnerNative native;
    SettingsPublicationOwner owner(records, package, native);
    const auto before = Read(owner_root / "installs.json");
    CHECK(!owner.Prepare(t.operation.install, t.operation.plan, t.operation.kit_id, t.operation.transaction_id, 1760000000, t.root).ok());
    CHECK(Read(owner_root / "installs.json") == before && !fs::exists(t.control / ("settings-intent-" + t.operation.transaction_id + ".v1")));
  }
  // Prepare itself initializes a missing control path in a fresh owned bottle.
  {
    Test t; fs::remove_all(t.control);
    const auto owner_root = t.root / "owner"; fs::create_directory(owner_root); CHECK(chmod(owner_root.c_str(), 0700) == 0);
    playden::Storage storage(owner_root.string());
    auto availability = [](const std::string&) { return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree}; };
    RuntimeInstallRepository repository(storage, "installs.json", availability); SeedOwner(repository, t.operation.install, false);
    RepositoryInstallRecordStore records(repository); OwnerPackage package; OwnerNative native; native.records = &records;
    SettingsPublicationOwner owner(records, package, native);
    auto prepared = owner.Prepare(t.operation.install, t.operation.plan, t.operation.kit_id, t.operation.transaction_id, 1760000000, t.root);
    CHECK(prepared.ok() && fs::exists(t.control / "publication.lock") && !fs::exists(t.control / "publication-state.v1"));
    t.operation = prepared.value(); t.deps = owner.Dependencies(t.root);
    CHECK(t.Run().state == PackageActionState::kComplete && native.registrations == 1);
  }
  // Requested settings survive cold restart before capture and after control
  // initialization, without reloading a changed recipe/settings repository.
  for (unsigned boundary = 0; boundary < 2; ++boundary) {
    Test t; fs::remove_all(t.control);
    const auto owner_root = t.root / "owner"; fs::create_directory(owner_root); CHECK(chmod(owner_root.c_str(), 0700) == 0);
    playden::Storage storage(owner_root.string());
    auto availability = [](const std::string&) { return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree}; };
    RuntimeInstallRepository repository(storage, "installs.json", availability);
    auto preparing = t.operation.install; preparing.state = PreparationState::kPreparing;
    SeedOwner(repository, preparing, false);
    RepositoryInstallRecordStore records(repository); OwnerPackage package; OwnerNative native; native.records = &records;
    SettingsPublicationOwner owner(records, package, native);
    SettingsPublicationRequest request;
    request.transaction_id = t.operation.transaction_id; request.kit_id = t.operation.kit_id;
    request.install = t.operation.install; request.plan = t.operation.plan; request.registry_unix_time = 1760000000;
    auto requested = owner.Request(request, owner_root);
    if (requested.state != PackageActionState::kComplete) std::fprintf(stderr, "Request: %s %s\n", requested.code.c_str(), requested.message.c_str());
    CHECK(requested.state == PackageActionState::kComplete);
    CHECK(owner.Request(request, owner_root).state == PackageActionState::kComplete);
    auto changed_request = request; changed_request.plan.profile_bytes += "\n";
    CHECK(owner.Request(changed_request, owner_root).state == PackageActionState::kFailed);
    CHECK(!fs::exists(t.control));
    auto armed = records.Find(request.install.install_id); CHECK(armed && armed->journal && armed->journal->operation == "settings-request");
    if (boundary) CHECK(owner.InitializeControl(*armed, request.transaction_id, t.root).state == PackageActionState::kComplete);
    CHECK(!fs::exists(t.control / ("settings-intent-" + request.transaction_id + ".v1")));
    RuntimeInstallRepository restart(storage, "installs.json", availability); CHECK(restart.Load().ok());
    RepositoryInstallRecordStore restarted_records(restart); native.records = &restarted_records;
    SettingsPublicationOwner resumed(restarted_records, package, native);
    RuntimeInstallService service(restarted_records, package, native, {}, {},
        [&](const RuntimeInstall& record) { return resumed.ResumeRequested(record, owner_root, t.root); });
    CHECK(service.Step(request.install.install_id).ok());
    auto ready = restarted_records.Find(request.install.install_id);
    CHECK(ready && ready->state == PreparationState::kReady && !ready->journal && native.registrations == 1);
    CHECK(Read(t.paths.profile) == request.plan.profile_bytes);
    auto frozen = LoadSettingsPublicationOperation(t.control);
    CHECK(frozen.operation && frozen.operation->registry_unix_time == 1760000000);
    CHECK(resumed.Request(request, owner_root).state == PackageActionState::kFailed);
    // Updating a Ready title keeps the committed revision until publication.
    package.status.profile_published = true;
    package.status.profile_settings_revision = ready->settings_revision;
    auto update = request; update.transaction_id = "22222222-2222-4222-8222-222222222222";
    update.install = *ready; ++update.install.settings_revision;
    update.plan.settings_revision = update.install.settings_revision;
    update.plan.dxvk_config = "# Generated by Playden from this game's settings.\ndxgi.maxFrameRate = 30\n";
    CHECK(resumed.Request(update, owner_root).state == PackageActionState::kComplete);
    auto update_arm = restarted_records.Find(ready->install_id);
    CHECK(update_arm && update_arm->settings_revision == ready->settings_revision && update_arm->journal &&
          update_arm->journal->operation_id == update.transaction_id && update_arm->journal->step == "capture-update");
    CHECK(Read(t.paths.profile) == request.plan.profile_bytes && !fs::exists(t.DxvkPath()));
    if (boundary) {
      auto prepared = resumed.Prepare(update.install, update.plan, update.kit_id, update.transaction_id,
                                     update.registry_unix_time, t.root);
      CHECK(prepared.ok()); // Cold restart after prepared handoff, before publication.
    }
    RuntimeInstallRepository update_restart(storage, "installs.json", availability); CHECK(update_restart.Load().ok());
    RepositoryInstallRecordStore update_records(update_restart); native.records = &update_records;
    SettingsPublicationOwner update_owner(update_records, package, native);
    RuntimeInstallService update_service(update_records, package, native, {}, {},
        [&](const RuntimeInstall& record) { return update_owner.ResumeRequested(record, owner_root, t.root); });
    CHECK(update_service.Step(ready->install_id).ok());
    auto updated = update_records.Find(ready->install_id);
    CHECK(updated && updated->state == PreparationState::kReady && !updated->journal &&
          updated->settings_revision == update.plan.settings_revision && native.registrations == 1);
    CHECK(Read(t.DxvkPath()) == update.plan.dxvk_config);
  }
  {
    Test t; fs::remove_all(t.control);
    const auto owner_root = t.root / "owner"; fs::create_directory(owner_root); CHECK(chmod(owner_root.c_str(), 0700) == 0);
    playden::Storage storage(owner_root.string());
    auto availability = [](const std::string&) { return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree}; };
    RuntimeInstallRepository repository(storage, "installs.json", availability);
    auto preparing = t.operation.install; preparing.state = PreparationState::kPreparing;
    SeedOwner(repository, preparing, false);
    RepositoryInstallRecordStore records(repository); OwnerPackage package; OwnerNative native; native.records = &records;
    SettingsPublicationOwner owner(records, package, native);
    SettingsPublicationRequest request;
    request.transaction_id = t.operation.transaction_id; request.kit_id = t.operation.kit_id;
    request.install = preparing; request.plan = t.operation.plan; request.registry_unix_time = 1760000000;
    const auto before = Read(owner_root / "installs.json");
    native.activity = NativeActivity::kUnknown;
    CHECK(owner.Request(request, owner_root).state == PackageActionState::kFailed);
    CHECK(LoadSettingsPublicationRequest(owner_root, request.transaction_id).state == PublicationRecoveryState::kAbsent);
    native.activity = NativeActivity::kStopped;
    CHECK(CreateSettingsPublicationRequest(owner_root, request).ok()); // Inert pre-arm orphan.
    CHECK(owner.Request(request, owner_root).code == "publication-request-orphan");
    CHECK(owner.ResumeRequested(preparing, owner_root, t.root).state == PackageActionState::kFailed);
    CHECK(Read(owner_root / "installs.json") == before && !fs::exists(t.control));
  }
  // Adopt existing Steam paths without moving downloads, local saves or DLLs.
  for (const auto* download_root : {"/data/homebrew/FAKE77031/games/220", "/mnt/usb0/playden/games/220"}) {
    Test t; fs::remove_all(t.control);
    auto record = t.operation.install; record.install_path = download_root;
    auto plan = t.operation.plan;
    std::string windows = download_root; std::replace(windows.begin(), windows.end(), '/', '\\');
    const std::string old_root = "Z:\\data\\homebrew\\FAKE78001\\games\\220";
    for (std::size_t pos = 0; (pos = plan.profile_bytes.find(old_root, pos)) != std::string::npos; pos += windows.size() + 2)
      plan.profile_bytes.replace(pos, old_root.size(), "Z:" + windows);
    plan.dxvk_config = "# Generated by Playden from this game's settings.\ndxgi.maxFrameRate = 30\n";
    const auto game = t.root / fs::path(download_root).relative_path(); fs::create_directories(game);
    std::ofstream(game / "probe.exe") << "existing-game";
    std::ofstream(game / "steam_api.dll") << "unchanged-original-dll";
    std::ofstream(game / "player-save.dat") << "existing-player-save";
    const auto owner_root = t.root / "owner"; fs::create_directory(owner_root); CHECK(chmod(owner_root.c_str(), 0700) == 0);
    playden::Storage storage(owner_root.string());
    auto availability = [](const std::string&) { return NativeTitleAvailability{TitleAvailability::kFree, TitleAvailability::kFree}; };
    RuntimeInstallRepository repository(storage, "installs.json", availability); SeedOwner(repository, record, false);
    RepositoryInstallRecordStore records(repository); OwnerPackage package; OwnerNative native; native.records = &records;
    SettingsPublicationOwner owner(records, package, native);
    auto prepared = owner.Prepare(record, plan, t.operation.kit_id, t.operation.transaction_id, 1760000000, t.root);
    CHECK(prepared.ok()); t.operation = prepared.value(); t.deps = owner.Dependencies(t.root);
    auto published = t.Run();
    if (published.state != PackageActionState::kComplete) std::fprintf(stderr, "Adopted publication: %s %s\n", published.code.c_str(), published.message.c_str());
    CHECK(published.state == PackageActionState::kComplete && native.registrations == 1);
    auto ready = records.Find(record.install_id); CHECK(ready && ready->state == PreparationState::kReady && ready->install_path == download_root);
    CHECK(Read(game / "dxvk.conf") == plan.dxvk_config && Read(game / "probe.exe") == "existing-game");
    CHECK(Read(game / "steam_api.dll") == "unchanged-original-dll" && Read(game / "player-save.dat") == "existing-player-save");
    CHECK(Read(t.paths.profile) == plan.profile_bytes && !fs::exists(t.paths.descriptor.parent_path() / "games/220/dxvk.conf"));
    CHECK(package.mutations == 0); // No download/title/bottle staging during publication.
  }
  std::puts("settings publication coordinator tests passed");
}
