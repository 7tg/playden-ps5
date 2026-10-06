#include "runtime/settings_publication.h"
#include "runtime/settings_publication_owner.h"
#include "platform/managed_file_lock.h"
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
    CHECK(stored->state == PreparationState::kRegistering); ++registrations;
    if (fail) return {RegistrationResultState::kFailed, "submit-failed", "injected native submission failure"};
    if (queue) status.scan_pending = true; else status.installed = true;
    return {queue ? RegistrationResultState::kScanQueued : RegistrationResultState::kAccepted, {}, {}};
  }
};
struct RejectReadyStore final : InstallRecordStore {
  InstallRecordStore& store; bool reject = false;
  explicit RejectReadyStore(InstallRecordStore& s) : store(s) {}
  std::optional<RuntimeInstall> Find(const std::string& id) const override { return store.Find(id); }
  std::vector<RuntimeInstall> Snapshot() const override { return store.Snapshot(); }
  RecordUpdateResult Update(const RuntimeInstall& record) override {
    if (reject && record.state == PreparationState::kReady) return {false, "ready-write", "injected Ready persistence refusal"};
    return store.Update(record);
  }
};
void SeedOwner(RuntimeInstallRepository& repository, const RuntimeInstall& r) {
  CHECK(repository.Load().ok()); ReserveRequest request;
  request.install_id = r.install_id; request.app_id = r.app_id; request.default_profile_id = r.default_profile_id;
  request.runtime_bundle_id = r.runtime_bundle_id; request.recipe_id = r.recipe_id; request.recipe_version = r.recipe_version;
  request.settings_revision = r.settings_revision; request.launch_entry_id = r.launch_entry_id; request.launch_path = r.launch_path;
  request.launch_arguments = r.launch_arguments; request.launch_working_directory = r.launch_working_directory;
  request.install_path = r.install_path; request.volume_id = r.volume_id; request.kit_title_variants = {r.title_id};
  CHECK(repository.Reserve(request).ok()); CHECK(repository.Update(r).ok());
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
    SettingsPublicationOwner owner(records, package, native); t.deps = owner.Dependencies(t.root);
    unsigned applies = 0; auto apply = t.deps.apply_files;
    t.deps.apply_files = [&](const auto& o, const auto& paths, int fd) { ++applies; return apply(o, paths, fd); };
    CHECK(t.Run().state == PackageActionState::kFailed && native.registrations == 1 && applies == 1);
    CHECK(Read(t.control / "publication-state.v1") == t.operation.committed_receipt_bytes);
    RuntimeInstallRepository reload(storage, "installs.json", availability); CHECK(reload.Load().ok());
    auto blocked = reload.Find(t.operation.install.install_id); CHECK(blocked && blocked->state == PreparationState::kRegistering);
    CHECK(blocked->journal && blocked->journal->operation_id == t.operation.transaction_id && blocked->journal->step == "await-native");
    RepositoryInstallRecordStore resumed_base(reload); RejectReadyStore resumed_records(resumed_base);
    native.records = &resumed_records;
    SettingsPublicationOwner resumed_owner(resumed_records, package, native); t.deps = resumed_owner.Dependencies(t.root);
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
      source.install.settings_revision = source.plan.settings_revision = 8; Refresh(source);
      std::string reason; auto frozen = FreezeSettingsPublicationOperation(source, &reason); CHECK(frozen);
      // Test-only next-operation setup; production retirement remains pending.
      playden::Storage control(t.control.string()); CHECK(control.RemoveDurable("settings-operation.v1").ok());
      t.operation = *frozen; native.status.installed = false;
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
  std::puts("settings publication coordinator tests passed");
}
