#include "platform/ps5/native_access.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

using playden::ps5::NativeAccessBootstrap;
using playden::ps5::NativeAccessOps;
using playden::ps5::NativeAccessOptions;
using playden::ps5::NativePathAccess;
using playden::ps5::NativePathKind;

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

namespace {

constexpr const char* kAppRoot = "/app0";
constexpr const char* kStableRoot = "/data/homebrew/FAKE77031";
constexpr const char* kRuntimeRoot = "/data/playden";

struct FakeNative {
  int user_result = 0;
  int graphics_result = 0;
  int grant_result = 0;
  bool expose_stable_after_grant = true;
  bool expose_runtime_after_grant = true;
  bool invalidate_app_after_grant = true;
  std::string stable_identity_after_grant = "playden-package-v1";
  std::map<std::string, NativePathKind> paths;
  std::map<std::string, std::string> identities;
  std::set<std::string> inaccessible;
  std::set<std::string> read_only;
  std::vector<std::string> calls;
  int grants = 0;
};

void AddTree(FakeNative& fake, const std::string& root, bool state = true) {
  fake.paths[root] = NativePathKind::kDirectory;
  fake.paths[root + "/assets"] = NativePathKind::kDirectory;
  fake.paths[root + "/trust/cacert.pem"] = NativePathKind::kFile;
  if (state) {
    fake.paths[root + "/playden-data"] = NativePathKind::kDirectory;
    fake.paths[root + "/logs"] = NativePathKind::kDirectory;
  }
}

void RemoveTree(FakeNative& fake, const std::string& root) {
  for (auto it = fake.paths.begin(); it != fake.paths.end();) {
    if (it->first == root || it->first.rfind(root + "/", 0) == 0)
      it = fake.paths.erase(it);
    else
      ++it;
  }
  fake.identities.erase(root);
}

int InitializeUser(void* context) {
  auto& fake = *static_cast<FakeNative*>(context);
  fake.calls.push_back("user");
  return fake.user_result;
}

int PrepareGraphics(void* context) {
  auto& fake = *static_cast<FakeNative*>(context);
  fake.calls.push_back("graphics");
  return fake.graphics_result;
}

int RequestGrant(void* context) {
  auto& fake = *static_cast<FakeNative*>(context);
  fake.calls.push_back("grant");
  ++fake.grants;
  if (fake.grant_result != 0) return fake.grant_result;
  if (fake.invalidate_app_after_grant) RemoveTree(fake, kAppRoot);
  if (fake.expose_stable_after_grant) {
    AddTree(fake, kStableRoot);
    fake.identities[kStableRoot] = fake.stable_identity_after_grant;
  }
  if (fake.expose_runtime_after_grant)
    fake.paths[kRuntimeRoot] = NativePathKind::kDirectory;
  return 0;
}

NativePathKind PathKind(void* context, const char* path) {
  auto& fake = *static_cast<FakeNative*>(context);
  fake.calls.push_back(std::string("path:") + path);
  const auto found = fake.paths.find(path);
  return found == fake.paths.end() ? NativePathKind::kMissing : found->second;
}

bool PathAccessible(void* context, const char* path, NativePathAccess access) {
  auto& fake = *static_cast<FakeNative*>(context);
  fake.calls.push_back(std::string(access == NativePathAccess::kReadWrite
                                       ? "write:"
                                       : "read:") + path);
  return fake.paths.find(path) != fake.paths.end() &&
         fake.inaccessible.find(path) == fake.inaccessible.end() &&
         (access != NativePathAccess::kReadWrite ||
          fake.read_only.find(path) == fake.read_only.end());
}

bool ReadIdentity(void* context, const char* path, std::string* identity) {
  auto& fake = *static_cast<FakeNative*>(context);
  fake.calls.push_back(std::string("identity:") + path);
  const auto found = fake.identities.find(path);
  if (found == fake.identities.end()) return false;
  *identity = found->second;
  return true;
}

NativeAccessOps Ops(FakeNative& fake) {
  return {&fake, InitializeUser, PrepareGraphics, RequestGrant, PathKind,
          PathAccessible, ReadIdentity};
}

NativeAccessOptions Options(bool known_fresh = false) {
  NativeAccessOptions options;
  options.title_id = "FAKE77031";
  options.sandbox_root = kAppRoot;
  options.stable_homebrew_root = "/data/homebrew";
  options.managed_runtime_root = kRuntimeRoot;
  options.managed_runtime_parent = "/data";
  options.known_fresh_state = known_fresh;
  return options;
}

FakeNative OrdinaryLauncher() {
  FakeNative fake;
  AddTree(fake, kAppRoot);
  fake.identities[kAppRoot] = "playden-package-v1";
  return fake;
}

void PathsRemainValidAfterGrant() {
  auto fake = OrdinaryLauncher();
  NativeAccessBootstrap bootstrap(Ops(fake), Options());

  const auto result = bootstrap.Prepare();

  CHECK(result.launcher_ready && result.runtime_ready && result.granted);
  CHECK(!result.already_accessible && result.error.empty());
  CHECK(result.paths.title_root == kStableRoot);
  CHECK(result.paths.assets_root == std::string(kStableRoot) + "/assets");
  CHECK(result.paths.trust_store == std::string(kStableRoot) + "/trust/cacert.pem");
  CHECK(result.paths.steam_state_root == std::string(kStableRoot) + "/playden-data");
  CHECK(result.paths.log_root == std::string(kStableRoot) + "/logs");
  CHECK(result.paths.runtime_data_root == kRuntimeRoot);
  CHECK(fake.paths.find(kAppRoot) == fake.paths.end());
  CHECK(fake.calls[0] == "user" && fake.calls[1] == "graphics");
  const auto grant = std::find(fake.calls.begin(), fake.calls.end(), "grant");
  const auto original_identity = std::find(fake.calls.begin(), fake.calls.end(), "identity:/app0");
  const auto stable_identity = std::find(fake.calls.begin(), fake.calls.end(),
                                         "identity:/data/homebrew/FAKE77031");
  CHECK(original_identity < grant && grant < stable_identity);
}

// The bootstrap keeps the package paths; the startup move fills the split roots.
void SplitRootsFallBackToPackagePaths() {
  auto fake = OrdinaryLauncher();
  NativeAccessBootstrap bootstrap(Ops(fake), Options());
  auto result = bootstrap.Prepare();
  CHECK(result.runtime_ready && result.notice.empty());
  CHECK(result.paths.user_root.empty() && result.paths.user_log_root.empty() && result.paths.games_root.empty());
  CHECK(playden::ps5::EffectiveUserRoot(result.paths) == std::string(kStableRoot) + "/playden-data");
  CHECK(playden::ps5::EffectiveLogRoot(result.paths) == std::string(kStableRoot) + "/logs");
  CHECK(playden::ps5::EffectiveGamesRoot(result.paths) == std::string(kStableRoot) + "/games");
  result.paths.user_root = "/data/playden/user";
  result.paths.user_log_root = "/data/playden/logs";
  result.paths.games_root = "/data/playden-games";
  CHECK(playden::ps5::EffectiveUserRoot(result.paths) == "/data/playden/user");
  CHECK(playden::ps5::EffectiveLogRoot(result.paths) == "/data/playden/logs");
  CHECK(playden::ps5::EffectiveGamesRoot(result.paths) == "/data/playden-games");
  // The stub folders are still what the bootstrap validates.
  CHECK(result.paths.steam_state_root == std::string(kStableRoot) + "/playden-data");
  CHECK(result.paths.log_root == std::string(kStableRoot) + "/logs");

  auto denied = OrdinaryLauncher();
  denied.grant_result = 1;
  NativeAccessBootstrap fallback(Ops(denied), Options());
  const auto launcher = fallback.Prepare();
  CHECK(launcher.launcher_ready && !launcher.runtime_ready);
  CHECK(launcher.paths.user_root.empty() && launcher.paths.games_root.empty());
  CHECK(playden::ps5::EffectiveGamesRoot(launcher.paths) == launcher.paths.title_root + "/games");
}

void DeferredUserServiceRunsAfterGrantAndBeforeReturning() {
  auto fake = OrdinaryLauncher();
  auto options = Options();
  options.user_service_after_grant = true;
  NativeAccessBootstrap bootstrap(Ops(fake), options);
  CHECK(bootstrap.Prepare().runtime_ready);
  const auto grant = std::find(fake.calls.begin(), fake.calls.end(), "grant");
  const auto user = std::find(fake.calls.begin(), fake.calls.end(), "user");
  CHECK(grant != fake.calls.end() && user != fake.calls.end() && grant < user);
  const auto calls = fake.calls;
  CHECK(bootstrap.Prepare().runtime_ready && fake.calls == calls);

  auto failed = OrdinaryLauncher();
  failed.grant_result = 13;
  NativeAccessBootstrap failed_bootstrap(Ops(failed), options);
  CHECK(failed_bootstrap.Prepare().launcher_ready);
  CHECK(std::find(failed.calls.begin(), failed.calls.end(), "grant") <
        std::find(failed.calls.begin(), failed.calls.end(), "user"));

  auto user_failed = OrdinaryLauncher();
  user_failed.user_result = 12;
  NativeAccessBootstrap user_failed_bootstrap(Ops(user_failed), options);
  const auto result = user_failed_bootstrap.Prepare();
  CHECK(result.launcher_ready && result.granted && !result.runtime_ready);
  CHECK(result.paths.title_root == kStableRoot);
  CHECK(result.error.find("user service") != std::string::npos);
  user_failed.user_result = 0;
  CHECK(user_failed_bootstrap.Prepare().runtime_ready);
  CHECK(user_failed.grants == 1);
}

void GrantFailureDisablesRuntimeOnly() {
  auto fake = OrdinaryLauncher();
  fake.grant_result = 71;
  NativeAccessBootstrap bootstrap(Ops(fake), Options());

  const auto result = bootstrap.Prepare();

  CHECK(result.launcher_ready && !result.runtime_ready && !result.granted);
  CHECK(result.paths.title_root == kAppRoot);
  CHECK(result.error.find("grant") != std::string::npos);
  CHECK(fake.grants == 1);
}

void UserAndGraphicsReadinessPrecedeGrant() {
  auto fake = OrdinaryLauncher();
  NativeAccessBootstrap bootstrap(Ops(fake), Options());
  CHECK(bootstrap.Prepare().runtime_ready);
  CHECK(fake.calls[0] == "user" && fake.calls[1] == "graphics");

  auto failed = OrdinaryLauncher();
  failed.user_result = 12;
  NativeAccessBootstrap failed_bootstrap(Ops(failed), Options());
  const auto result = failed_bootstrap.Prepare();
  CHECK(result.launcher_ready && !result.runtime_ready);
  CHECK(result.error.find("user service") != std::string::npos);
  CHECK(std::find(failed.calls.begin(), failed.calls.end(), "graphics") == failed.calls.end());
  CHECK(failed.grants == 0);
}

void AlreadyAccessibleSourceSkipsGrant() {
  auto fake = OrdinaryLauncher();
  AddTree(fake, kStableRoot);
  fake.identities[kStableRoot] = fake.identities[kAppRoot];
  fake.paths[kRuntimeRoot] = NativePathKind::kDirectory;
  RemoveTree(fake, kAppRoot);
  auto options = Options();
  options.source_identity = "playden-package-v1";
  NativeAccessBootstrap bootstrap(Ops(fake), options);

  const auto result = bootstrap.Prepare();

  CHECK(result.runtime_ready && result.granted && result.already_accessible);
  CHECK(result.paths.title_root == kStableRoot && fake.grants == 0);
}

void RequiredGrantRunsEvenWhenSourceIsVisible() {
  auto fake = OrdinaryLauncher();
  AddTree(fake, kStableRoot);
  fake.identities[kStableRoot] = fake.identities[kAppRoot];
  fake.paths[kRuntimeRoot] = NativePathKind::kDirectory;
  auto options = Options();
  options.require_grant = true;
  NativeAccessBootstrap bootstrap(Ops(fake), options);

  const auto result = bootstrap.Prepare();

  CHECK(result.runtime_ready && result.granted && !result.already_accessible);
  CHECK(result.paths.title_root == kStableRoot && fake.grants == 1);
}

void InvalidOrMissingStableSourceNeverFallsBackAfterGrant() {
  auto wrong = OrdinaryLauncher();
  wrong.stable_identity_after_grant = "foreign-package";
  NativeAccessBootstrap wrong_bootstrap(Ops(wrong), Options());
  const auto mismatched = wrong_bootstrap.Prepare();
  CHECK(!mismatched.launcher_ready && !mismatched.runtime_ready && mismatched.granted);
  CHECK(mismatched.paths.title_root.empty());
  CHECK(mismatched.error.find("identity") != std::string::npos);

  auto missing = OrdinaryLauncher();
  missing.expose_stable_after_grant = false;
  NativeAccessBootstrap missing_bootstrap(Ops(missing), Options());
  const auto absent = missing_bootstrap.Prepare();
  CHECK(!absent.launcher_ready && !absent.runtime_ready);
  CHECK(absent.paths.title_root.empty());
  CHECK(absent.error.find("stable") != std::string::npos);
}

void RetryIsIdempotentAndDoesNotResetStorage() {
  auto fake = OrdinaryLauncher();
  fake.grant_result = 5;
  NativeAccessBootstrap bootstrap(Ops(fake), Options());
  CHECK(!bootstrap.Prepare().runtime_ready);
  fake.grant_result = 0;
  CHECK(bootstrap.Prepare().runtime_ready);
  CHECK(fake.grants == 2);
  CHECK(std::count(fake.calls.begin(), fake.calls.end(), "user") == 1);
  CHECK(std::count(fake.calls.begin(), fake.calls.end(), "graphics") == 1);
  const auto calls_after_success = fake.calls;
  CHECK(bootstrap.Prepare().runtime_ready);
  CHECK(fake.calls == calls_after_success && fake.grants == 2);
  CHECK(fake.paths[std::string(kStableRoot) + "/playden-data"] ==
        NativePathKind::kDirectory);
}

// A hand copy can leave the empty stubs out: the grant still runs, and startup makes them afterwards.
// Only the fallback without the grant, which would write into them, needs them or an explicit fresh install.
void MissingStateRequiresAnExplicitFreshInstall() {
  auto existing = OrdinaryLauncher();
  existing.paths.erase(std::string(kAppRoot) + "/playden-data");
  existing.paths.erase(std::string(kAppRoot) + "/logs");
  NativeAccessBootstrap existing_bootstrap(Ops(existing), Options());
  const auto granted = existing_bootstrap.Prepare();
  CHECK(granted.launcher_ready && granted.runtime_ready && granted.error.empty());
  CHECK(existing.grants == 1);

  auto stable_missing = OrdinaryLauncher();
  stable_missing.paths.erase(std::string(kAppRoot) + "/logs");
  stable_missing.expose_stable_after_grant = false;
  AddTree(stable_missing, kStableRoot, false);
  stable_missing.identities[kStableRoot] = stable_missing.identities[kAppRoot];
  NativeAccessBootstrap stable_missing_bootstrap(Ops(stable_missing), Options());
  const auto stubless = stable_missing_bootstrap.Prepare();
  CHECK(stubless.runtime_ready && stubless.paths.log_root == std::string(kStableRoot) + "/logs");
  CHECK(stable_missing.grants == 1);

  auto denied = OrdinaryLauncher();
  denied.paths.erase(std::string(kAppRoot) + "/playden-data");
  denied.grant_result = 2;
  NativeAccessBootstrap denied_bootstrap(Ops(denied), Options());
  const auto rejected = denied_bootstrap.Prepare();
  CHECK(!rejected.launcher_ready && !rejected.runtime_ready);
  CHECK(rejected.error.find("state") != std::string::npos);

  auto fresh = OrdinaryLauncher();
  fresh.paths.erase(std::string(kAppRoot) + "/playden-data");
  fresh.paths.erase(std::string(kAppRoot) + "/logs");
  fresh.grant_result = 2;
  NativeAccessBootstrap fresh_bootstrap(Ops(fresh), Options(true));
  const auto accepted = fresh_bootstrap.Prepare();
  CHECK(accepted.launcher_ready && !accepted.runtime_ready);
  CHECK(accepted.paths.steam_state_root == std::string(kAppRoot) + "/playden-data");

  auto granted_fresh = OrdinaryLauncher();
  AddTree(granted_fresh, kStableRoot, false);
  granted_fresh.identities[kStableRoot] = granted_fresh.identities[kAppRoot];
  NativeAccessBootstrap granted_fresh_bootstrap(Ops(granted_fresh), Options(true));
  const auto stable = granted_fresh_bootstrap.Prepare();
  CHECK(stable.launcher_ready && stable.runtime_ready && !stable.already_accessible);
  CHECK(granted_fresh.grants == 1);
  CHECK(stable.paths.steam_state_root ==
        std::string(kStableRoot) + "/playden-data");
}

void InaccessibleExistingStateIsRejectedBeforeGrant() {
  auto fake = OrdinaryLauncher();
  fake.inaccessible.insert(std::string(kAppRoot) + "/playden-data");
  NativeAccessBootstrap bootstrap(Ops(fake), Options());

  const auto result = bootstrap.Prepare();

  CHECK(!result.launcher_ready && !result.runtime_ready);
  CHECK(result.error.find("state") != std::string::npos);
  CHECK(fake.grants == 0);
}

void PartialStableVisibilityStillRequestsGrant() {
  auto fake = OrdinaryLauncher();
  fake.paths[kStableRoot] = NativePathKind::kDirectory;
  fake.identities[kStableRoot] = fake.identities[kAppRoot];
  NativeAccessBootstrap bootstrap(Ops(fake), Options());

  const auto result = bootstrap.Prepare();

  CHECK(result.runtime_ready && result.launcher_ready);
  CHECK(!result.already_accessible && fake.grants == 1);
  CHECK(result.paths.title_root == kStableRoot);
}

// Only a launcher without the grant creates its state in the app folder.
void FreshStateRequiresWritableParentWithoutGrant() {
  auto fake = OrdinaryLauncher();
  AddTree(fake, kStableRoot, false);
  fake.identities[kStableRoot] = fake.identities[kAppRoot];
  fake.paths[kRuntimeRoot] = NativePathKind::kDirectory;
  fake.read_only.insert(kStableRoot);
  fake.expose_stable_after_grant = false;
  NativeAccessBootstrap bootstrap(Ops(fake), Options(true));
  CHECK(bootstrap.Prepare().runtime_ready);

  auto launcher = OrdinaryLauncher();
  launcher.paths.erase(std::string(kAppRoot) + "/playden-data");
  launcher.paths.erase(std::string(kAppRoot) + "/logs");
  launcher.read_only.insert(kAppRoot);
  launcher.grant_result = 13;
  NativeAccessBootstrap fallback(Ops(launcher), Options(true));
  const auto refused = fallback.Prepare();
  CHECK(!refused.launcher_ready && !refused.runtime_ready && launcher.grants == 1);
  CHECK(refused.error.find("cannot be created") != std::string::npos);

  auto populated = OrdinaryLauncher();
  AddTree(populated, kStableRoot);
  populated.identities[kStableRoot] = populated.identities[kAppRoot];
  populated.paths[kRuntimeRoot] = NativePathKind::kDirectory;
  populated.read_only.insert(kStableRoot);
  NativeAccessBootstrap complete(Ops(populated), Options());
  CHECK(complete.Prepare().runtime_ready);
  CHECK(populated.grants == 0);
}

// Stubs copied over FTP are root's and 0755: a granted start keeps nothing there.
void RootOwnedStubsAllowGrantedStart() {
  auto fake = OrdinaryLauncher();
  for (const auto* root : {kAppRoot, kStableRoot}) {
    fake.read_only.insert(std::string(root) + "/playden-data");
    fake.read_only.insert(std::string(root) + "/logs");
  }
  auto options = Options();
  options.require_grant = true;
  NativeAccessBootstrap bootstrap(Ops(fake), options);

  const auto result = bootstrap.Prepare();

  CHECK(result.launcher_ready && result.runtime_ready && result.granted);
  CHECK(result.paths.title_root == kStableRoot && fake.grants == 1);
  CHECK(result.paths.runtime_data_root == kRuntimeRoot);
  CHECK(result.error.empty());
}

// Without the grant the launcher keeps its state in the stubs: they must be writable.
void NoGrantFallbackRequiresWritableStubs() {
  auto fake = OrdinaryLauncher();
  fake.read_only.insert(std::string(kAppRoot) + "/playden-data");
  fake.read_only.insert(std::string(kAppRoot) + "/logs");
  fake.grant_result = 13;
  auto options = Options();
  options.require_grant = true;
  NativeAccessBootstrap bootstrap(Ops(fake), options);

  const auto result = bootstrap.Prepare();

  CHECK(!result.launcher_ready && !result.runtime_ready && fake.grants == 1);
  CHECK(result.error.find("grant") != std::string::npos);
  CHECK(result.error.find("inaccessible") != std::string::npos);

  // A stable title that matches but whose stubs are read-only is no launcher either.
  auto stable = OrdinaryLauncher();
  AddTree(stable, kStableRoot);
  stable.identities[kStableRoot] = stable.identities[kAppRoot];
  stable.read_only.insert(std::string(kStableRoot) + "/playden-data");
  stable.grant_result = 13;
  NativeAccessBootstrap stable_bootstrap(Ops(stable), options);
  const auto sandbox = stable_bootstrap.Prepare();
  CHECK(sandbox.launcher_ready && !sandbox.runtime_ready);
  CHECK(sandbox.paths.title_root == kAppRoot);
}

void StableTitleWithoutManagedAccessIsNotRuntimeReady() {
  auto fake = OrdinaryLauncher();
  AddTree(fake, kStableRoot);
  fake.identities[kStableRoot] = fake.identities[kAppRoot];
  fake.expose_runtime_after_grant = false;
  fake.grant_result = 13;
  NativeAccessBootstrap bootstrap(Ops(fake), Options());

  const auto result = bootstrap.Prepare();

  CHECK(result.launcher_ready && !result.runtime_ready && !result.granted);
  CHECK(result.paths.title_root == kStableRoot);
  CHECK(result.error.find("grant") != std::string::npos);
  CHECK(fake.grants == 1);
}

void PostGrantStableTitleSupportsLauncherWithoutManagedAccess() {
  auto fake = OrdinaryLauncher();
  fake.expose_runtime_after_grant = false;
  NativeAccessBootstrap bootstrap(Ops(fake), Options());

  const auto result = bootstrap.Prepare();

  // The grant ran: the screen blames the data, not the grant.
  CHECK(result.launcher_ready && !result.runtime_ready && result.granted);
  CHECK(result.paths.title_root == kStableRoot);
  CHECK(result.error.find("runtime data") != std::string::npos);
  CHECK(fake.paths.find(kAppRoot) == fake.paths.end());
  CHECK(fake.grants == 1);

  // A runtime root that is there but not writable.
  auto read_only = OrdinaryLauncher();
  read_only.read_only.insert(kRuntimeRoot);
  NativeAccessBootstrap read_only_bootstrap(Ops(read_only), Options());
  const auto unwritable = read_only_bootstrap.Prepare();
  CHECK(unwritable.launcher_ready && !unwritable.runtime_ready && unwritable.granted);
  CHECK(unwritable.error.find("inaccessible") != std::string::npos);
  CHECK(read_only.grants == 1);
}

void WritableManagedParentAllowsFirstRuntimeDirectoryCreation() {
  auto fake = OrdinaryLauncher();
  AddTree(fake, kStableRoot);
  fake.identities[kStableRoot] = fake.identities[kAppRoot];
  fake.paths["/data"] = NativePathKind::kDirectory;
  NativeAccessBootstrap bootstrap(Ops(fake), Options());

  const auto result = bootstrap.Prepare();

  CHECK(result.launcher_ready && result.runtime_ready &&
        result.already_accessible);
  CHECK(result.paths.runtime_data_root == kRuntimeRoot);
  CHECK(fake.paths.find(kRuntimeRoot) == fake.paths.end());
  CHECK(fake.grants == 0);
}

}  // namespace

void RuntimeRepairRunsBetweenGrantAndWriteCheck() {
  for (bool missing : {false, true}) {
    FakeNative fake;
    AddTree(fake, kAppRoot);
    fake.identities[kAppRoot] = "playden-package-v1";
    fake.expose_runtime_after_grant = !missing;
    fake.read_only.insert(kRuntimeRoot);
    auto ops = Ops(fake);
    ops.prepare_runtime_root = [](void* context, const char* root, std::string*) {
      auto& state = *static_cast<FakeNative*>(context);
      CHECK(state.grants == 1);
      CHECK(std::find(state.calls.begin(), state.calls.end(), "write:/data/playden") == state.calls.end());
      state.paths[root] = NativePathKind::kDirectory;
      state.read_only.erase(root);
      state.calls.push_back("repair");
      return true;
    };
    NativeAccessOptions options;
    options.title_id = "FAKE77031";
    options.require_grant = true;
    options.user_service_after_grant = true;
    NativeAccessBootstrap bootstrap(ops, options);
    CHECK(bootstrap.Prepare().runtime_ready);
    CHECK(std::find(fake.calls.begin(), fake.calls.end(), "repair") <
          std::find(fake.calls.begin(), fake.calls.end(), "user"));
  }
}

int main() {
  RuntimeRepairRunsBetweenGrantAndWriteCheck();
  DeferredUserServiceRunsAfterGrantAndBeforeReturning();
  PathsRemainValidAfterGrant();
  GrantFailureDisablesRuntimeOnly();
  UserAndGraphicsReadinessPrecedeGrant();
  AlreadyAccessibleSourceSkipsGrant();
  RequiredGrantRunsEvenWhenSourceIsVisible();
  InvalidOrMissingStableSourceNeverFallsBackAfterGrant();
  RetryIsIdempotentAndDoesNotResetStorage();
  MissingStateRequiresAnExplicitFreshInstall();
  InaccessibleExistingStateIsRejectedBeforeGrant();
  PartialStableVisibilityStillRequestsGrant();
  StableTitleWithoutManagedAccessIsNotRuntimeReady();
  FreshStateRequiresWritableParentWithoutGrant();
  RootOwnedStubsAllowGrantedStart();
  NoGrantFallbackRequiresWritableStubs();
  PostGrantStableTitleSupportsLauncherWithoutManagedAccess();
  WritableManagedParentAllowsFirstRuntimeDirectoryCreation();
  SplitRootsFallBackToPackagePaths();
  std::puts("native_access_test passed");
}
