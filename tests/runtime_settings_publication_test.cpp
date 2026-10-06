#include "runtime/settings_publication.h"
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
    deps.host_namespace_prefix = root;
    deps.activity = [&](const RuntimeInstall&) { ++activities; return stopped ? NativeActivity::kStopped : NativeActivity::kUnknown; };
    deps.block_owner = [&](const auto&) { CheckLease(); ++blocks; ready = false; return Ok(); };
    deps.preflight = [&](const auto&, const auto& p, int borrowed) {
      CHECK(borrowed >= 0); CheckLease(); ++preflights;
      for (const auto& file : {p.profile, p.catalog, p.descriptor, p.metadata}) if (fs::exists(file)) return Fail();
      if (fs::exists(p.input_directory) && !fs::is_empty(p.input_directory)) return Fail();
      return Ok();
    };
    deps.apply_files = [&](const auto& o, const auto&, int borrowed) {
      CHECK(borrowed >= 0); CheckLease(); CHECK(!ready && Read(control / "publication-state.v1") == o.pending_receipt_bytes);
      ++applies;
      auto registry = ApplyPendingPublicationRegistry(o, borrowed, root);
      if (registry.state != PackageActionState::kComplete) return registry;
      for (const auto& directory : {paths.profile.parent_path(), paths.input_directory}) {
        fs::create_directories(directory); CHECK(chmod(directory.c_str(), 0700) == 0);
      }
      fs::create_directories(paths.descriptor.parent_path() / "games/220");
      auto write = [&](const auto& file, const auto& bytes) {
        std::ofstream(file, std::ios::binary).write(bytes.data(), bytes.size()); CHECK(chmod(file.c_str(), 0600) == 0);
      };
      write(paths.profile, o.plan.profile_bytes); write(paths.catalog, o.catalog_bytes);
      write(paths.descriptor, o.descriptor_bytes); write(paths.metadata, o.metadata_bytes);
      for (const auto& input : o.plan.input_presets) write(paths.input_directory / input.first, input.second);
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
  PackageActionResult Run() { return PublishSettingsOperation(operation, paths, deps); }
};
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
    CHECK(t.Run().code == "publication-registry-collision" && !t.ready && t.Registry() == foreign);
    CHECK(Read(t.control / "publication-state.v1") == t.operation.pending_receipt_bytes);
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
  std::puts("settings publication coordinator tests passed");
}
