#include "runtime/package_installer.h"
#include "runtime/private_directories.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/resource.h>
#include <csignal>
#include <unistd.h>
#include <vector>

using namespace playden::runtime;

#define CHECK(condition)                                                                  \
  do {                                                                                    \
    if (!(condition)) {                                                                   \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
      std::exit(1);                                                                       \
    }                                                                                     \
  } while (false)

namespace {

struct TemporaryDirectory {
  TemporaryDirectory() {
    auto pattern = (std::filesystem::temp_directory_path() /
                    "playden-package-installer-XXXXXX").string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    const char* made = ::mkdtemp(writable.data());
    CHECK(made != nullptr);
    // macOS temp_directory_path may use the /var symlink alias. The managed
    // contract rejects every supplied symlink component; use the real fixture.
    path = std::filesystem::canonical(made);
  }
  ~TemporaryDirectory() { std::filesystem::remove_all(path); }
  std::filesystem::path path;
};

void Write(const std::filesystem::path& path, const std::string& bytes,
           unsigned mode = 0644) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << bytes;
  stream.close();
  CHECK(::chmod(path.c_str(), mode) == 0);
}

std::string Read(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// Deterministic test digest. Production supplies the existing cryptographic hash operation.
std::optional<std::string> TestHash(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::nullopt;
  std::uint64_t hash = 1469598103934665603ULL;
  for (char byte; stream.get(byte);) {
    hash ^= static_cast<unsigned char>(byte);
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (int i = 0; i < 4; ++i) out << std::setw(16) << (hash + i);
  return out.str();
}

std::optional<std::string> TestHashBytes(std::string_view bytes) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (int i = 0; i < 4; ++i) out << std::setw(16) << (hash + i);
  return out.str();
}

RuntimeInstall Record() {
  RuntimeInstall record;
  record.install_id = "install-hl2";
  record.title_id = "FAKE78001";
  record.app_id = 220;
  record.data_root = "/data/playden/games/install-hl2";
  record.default_profile_id = "game";
  record.runtime_bundle_id = "wine-wow64";
  record.recipe_id = "steam-220-hl2-v1";
  record.recipe_version = "1";
  record.settings_revision = std::numeric_limits<std::uint64_t>::max();
  record.exit_policy = "exit-title";
  record.launch_entry_id = "0";
  record.launch_path = "hl2.exe";
  record.install_path = "/mnt/games/220";
  record.volume_id = "internal";
  return record;
}

struct Fixture {
  TemporaryDirectory temporary;
  std::filesystem::path kit = temporary.path / "kit";
  std::filesystem::path host_data = temporary.path / "data";
  VerifiedKitView view;
  PackageInstallPaths paths;
  bool canceled = false;
  int cancel_after_checks = -1;
  int cancel_checks = 0;
  std::uint64_t free_bytes = 1ULL << 30;
  int writes = 0;
  int sync_calls = 0;
  int fail_sync_call = -1;
  bool fail_title_post_rename_once = false;
  bool fail_bottle_post_rename_once = false;
  bool publish_public_profile = false;

  Fixture() {
    Write(kit / "titles/variant/eboot.bin", "native", 0755);
    Write(kit / "titles/variant/sce_sys/param.json", "title", 0644);
    Write(kit / "runtime/wine/rev/lib/core.prx", "runtime", 0755);
    Write(kit / "template/bottle/drive_c/windows/system.reg", "registry", 0644);
    Write(kit / "template/bottle/dosdevices/.pw-symlinks",
          "c:\t../drive_c\nz:\t/\n", 0644);
    Write(kit / "recipes/hl2.json", "recipe", 0644);

    view.root = kit;
    view.kit_id = "kit-1";
    view.files_sha256 = std::string(64, 'a');
    view.runtime = {"wine-wow64", "rev", "runtime/wine/rev"};
    view.template_path = "template/bottle";
    view.template_drive_table = "template/bottle/dosdevices/.pw-symlinks";
    view.recipe = {"steam-220-hl2-v1", "1", "recipes/hl2.json",
                   *TestHash(kit / "recipes/hl2.json")};
    view.variants.push_back({"variant", "FAKE78001", "titles/variant"});
    for (const auto& entry : std::vector<std::pair<std::string, unsigned>>{
             {"titles/variant/eboot.bin", 0755},
             {"titles/variant/sce_sys/param.json", 0644},
             {"runtime/wine/rev/lib/core.prx", 0755},
             {"template/bottle/drive_c/windows/system.reg", 0644},
             {"template/bottle/dosdevices/.pw-symlinks", 0644},
             {"recipes/hl2.json", 0644}}) {
      const auto path = kit / entry.first;
      view.files.push_back(
          {entry.first, std::filesystem::file_size(path), *TestHash(path), entry.second});
    }
    view.artifacts = {
        {"runtime", "rev", *TestHash(kit / "runtime/wine/rev/lib/core.prx"),
         ImmutableArtifactLocation::kStagedTitle, "win/wine/lib/core.prx"},
        {"recipe", "steam-220-hl2-v1-1", *TestHash(kit / "recipes/hl2.json"),
         ImmutableArtifactLocation::kKit, "recipes/hl2.json"}};
    paths.console_data_root = "/data";
    paths.host_data_root = host_data;
  }

  ProfilePublication Publication(const RuntimeInstall& record) const {
    ProfilePublication publication;
    publication.install_id = record.install_id;
    publication.title_id = record.title_id;
    publication.data_root = record.data_root;
    publication.profile_id = record.default_profile_id;
    publication.runtime_bundle_id = record.runtime_bundle_id;
    publication.recipe_id = record.recipe_id;
    publication.recipe_version = record.recipe_version;
    publication.settings_revision = record.settings_revision;
    publication.profile_bytes = "[application]\nid = game\nprefix = game\n";
    publication.profile_references_validated = true;
    publication.descriptor_bytes =
        "{\"schema_version\":1,\"install_id\":\"install-hl2\","
        "\"title_id\":\"FAKE78001\",\"app_id\":220,"
        "\"data_root\":\"/data/playden/games/install-hl2\","
        "\"default_profile_id\":\"game\","
        "\"settings_revision\":18446744073709551615,"
        "\"runtime_bundle_id\":\"wine-wow64\","
        "\"recipe_id\":\"steam-220-hl2-v1\",\"recipe_version\":\"1\","
        "\"exit_policy\":\"exit-title\"}";
    return publication;
  }

  FilesystemPackageInstaller Installer(bool inject_sync = true) {
    PackageInstallerOperations operations;
    operations.hash_file = TestHash;
    operations.hash_bytes = TestHashBytes;
    operations.free_bytes = [&](const std::filesystem::path&) {
      return std::optional<std::uint64_t>(free_bytes);
    };
    operations.canceled = [&] {
      ++cancel_checks;
      return canceled ||
             (cancel_after_checks >= 0 && cancel_checks > cancel_after_checks);
    };
    if (inject_sync) {
      operations.sync_path = [&](const std::filesystem::path& path, bool) {
        ++sync_calls;
        if (fail_title_post_rename_once &&
            std::filesystem::exists(host_data / "homebrew/FAKE78001") &&
            path == host_data / "homebrew") {
          fail_title_post_rename_once = false;
          return false;
        }
        if (fail_bottle_post_rename_once &&
            std::filesystem::exists(host_data /
                                    "playden/games/install-hl2/library/prefixes/game") &&
            path == host_data / "playden/games/install-hl2/library/prefixes") {
          fail_bottle_post_rename_once = false;
          return false;
        }
        return fail_sync_call < 0 || sync_calls != fail_sync_call;
      };
    }
    operations.validate_bottle = [](const std::filesystem::path& bottle) {
      return std::filesystem::is_regular_file(bottle / "drive_c/windows/system.reg");
    };
    operations.provide_publication = [&](const RuntimeInstall& record) {
      return std::optional<ProfilePublication>(Publication(record));
    };
    operations.publish_owned = [&](const PublicationPaths& output,
                                   const ProfilePublication& publication,
                                   const std::string& metadata) {
      ++writes;
      Write(output.profile, publication.profile_bytes, publish_public_profile ? 0644 : 0600);
      Write(output.catalog, publication.profile_id + ".profile\n", 0600);
      for (const auto& preset : publication.input_presets)
        Write(output.input_directory / preset.filename, preset.bytes, 0600);
      Write(output.descriptor, publication.descriptor_bytes);
      Write(output.metadata, metadata, 0600);
      return PackageActionResult{PackageActionState::kComplete, {}, {}};
    };
    return FilesystemPackageInstaller(view, paths, std::move(operations));
  }
};

void CompletePublicationAndFreshInspection() {
  Fixture fixture;
  auto record = Record();
  record.artifacts = {{"runtime", "rev", fixture.view.artifacts[0].sha256},
                      {"recipe", "steam-220-hl2-v1-1", fixture.view.artifacts[1].sha256}};
  auto installer = fixture.Installer();
  CHECK(installer.StageTitle(record).state == PackageActionState::kComplete);
  const auto title = fixture.host_data / "homebrew/FAKE78001";
  CHECK(Read(title / "eboot.bin") == "native");
  CHECK(Read(title / "win/wine/lib/core.prx") == "runtime");
  CHECK(installer.CopyTemplate(record).state == PackageActionState::kComplete);
  const auto bottle = fixture.host_data /
                      "playden/games/install-hl2/library/prefixes/game";
  CHECK(Read(bottle / "dosdevices/.pw-symlinks") == "c:\t../drive_c\nz:\t/\n");
  CHECK(!std::filesystem::is_symlink(bottle / "dosdevices/c:"));
  for (const auto& path : {bottle, bottle / "drive_c", bottle / "drive_c/windows", bottle / "dosdevices"}) {
    struct stat info{};
    CHECK(::lstat(path.c_str(), &info) == 0);
    CHECK(info.st_uid == ::geteuid());
    CHECK((info.st_mode & 0777) == 0700);
  }
  struct stat registry{};
  CHECK(::lstat((bottle / "drive_c/windows/system.reg").c_str(), &registry) == 0);
  CHECK(registry.st_uid == ::geteuid());
  CHECK((registry.st_mode & 0777) == 0600);
  CHECK(installer.ValidateArtifacts(record).state == PackageActionState::kComplete);
  CHECK(installer.PublishProfile(record).state == PackageActionState::kComplete);
  const auto status = installer.Inspect(record);
  CHECK(status.known && status.title_staged && status.manifest_matches);
  CHECK(status.template_owned && status.mutable_bottle_present && status.mutable_bottle_owned);
  CHECK(status.artifacts_valid && status.profile_published);
  CHECK(status.profile_settings_revision == std::numeric_limits<std::uint64_t>::max());

  // Inspection is evidence-based rather than cached.
  Write(title / "win/wine/lib/core.prx", "corrupt", 0755);
  CHECK(!installer.Inspect(record).artifacts_valid);
  Write(title / "win/wine/lib/core.prx", "runtime", 0644);
  CHECK(!installer.Inspect(record).manifest_matches);
}

void CancelAndSpaceFailuresAreBounded() {
  Fixture fixture;
  auto record = Record();
  auto installer = fixture.Installer();
  fixture.free_bytes = 1;
  const auto no_space = installer.StageTitle(record);
  CHECK(no_space.state == PackageActionState::kFailed && no_space.code == "space-unavailable");
  CHECK(!std::filesystem::exists(fixture.host_data / "homebrew/FAKE78001"));
  fixture.free_bytes = 1ULL << 30;
  fixture.cancel_after_checks = 2;
  const auto canceled = installer.StageTitle(record);
  CHECK(canceled.state == PackageActionState::kCanceled);
  CHECK(!std::filesystem::exists(fixture.host_data / "homebrew/FAKE78001"));
  CHECK(std::filesystem::exists(fixture.host_data /
                                "playden/staging/title-install-hl2/.playden-stage-owner"));
  CHECK(!std::filesystem::exists(record.install_path));
  fixture.cancel_after_checks = -1;
  fixture.cancel_checks = 0;
  auto restarted = fixture.Installer();
  CHECK(restarted.StageTitle(record).state == PackageActionState::kComplete);
}

void SyncFailureNeverPublishesWatchedTitle() {
  Fixture fixture;
  fixture.fail_sync_call = 5;
  auto installer = fixture.Installer();
  const auto failed = installer.StageTitle(Record());
  CHECK(failed.state == PackageActionState::kFailed && failed.code == "sync-failed");
  CHECK(!std::filesystem::exists(fixture.host_data / "homebrew/FAKE78001"));
  CHECK(std::filesystem::exists(fixture.host_data /
                                "playden/staging/title-install-hl2/.playden-stage-owner"));
  fixture.fail_sync_call = -1;
  fixture.sync_calls = 0;
  auto restarted = fixture.Installer();
  CHECK(restarted.StageTitle(Record()).state == PackageActionState::kComplete);
}

void NativeFsyncFallbackPublishesAtomically() {
  Fixture fixture;
  auto installer = fixture.Installer(false);
  CHECK(installer.StageTitle(Record()).state == PackageActionState::kComplete);
  CHECK(installer.CopyTemplate(Record()).state == PackageActionState::kComplete);
}

void LateSyncFailureIsReestablishedOnRetry() {
  Fixture fixture;
  fixture.fail_title_post_rename_once = true;
  auto installer = fixture.Installer();
  CHECK(installer.StageTitle(Record()).code == "sync-failed");
  CHECK(std::filesystem::is_directory(fixture.host_data / "homebrew/FAKE78001"));
  CHECK(installer.StageTitle(Record()).state == PackageActionState::kComplete);

  fixture.fail_bottle_post_rename_once = true;
  CHECK(installer.CopyTemplate(Record()).code == "sync-failed");
  CHECK(std::filesystem::is_directory(
      fixture.host_data / "playden/games/install-hl2/library/prefixes/game"));
  CHECK(installer.CopyTemplate(Record()).state == PackageActionState::kComplete);
}

void CollisionsTraversalSymlinksAndModesFailClosed() {
  {
    Fixture fixture;
    auto installer = fixture.Installer();
    Write(fixture.host_data / "homebrew/FAKE78001/foreign.txt", "keep");
    CHECK(installer.StageTitle(Record()).code == "title-collision");
    CHECK(Read(fixture.host_data / "homebrew/FAKE78001/foreign.txt") == "keep");
  }
  {
    Fixture fixture;
    fixture.view.files[0].path = "titles/variant/../escape";
    auto installer = fixture.Installer();
    CHECK(!installer.Inspect(Record()).known);
    CHECK(installer.StageTitle(Record()).code == "kit-invalid");
  }
  {
    Fixture fixture;
    std::filesystem::remove(fixture.kit / "titles/variant/eboot.bin");
    std::filesystem::create_symlink("/etc/passwd",
                                    fixture.kit / "titles/variant/eboot.bin");
    auto installer = fixture.Installer();
    CHECK(installer.StageTitle(Record()).code == "kit-source-invalid");
  }
  {
    Fixture fixture;
    ::chmod((fixture.kit / "titles/variant/eboot.bin").c_str(), 0644);
    auto installer = fixture.Installer();
    CHECK(installer.StageTitle(Record()).code == "kit-source-invalid");
  }
  {
    Fixture fixture;
    std::filesystem::create_directories(fixture.host_data / "homebrew");
    std::filesystem::create_directories(fixture.temporary.path / "foreign-title");
    Write(fixture.temporary.path / "foreign-title/keep", "foreign");
    std::filesystem::create_directory_symlink(fixture.temporary.path / "foreign-title",
                                              fixture.host_data / "homebrew/FAKE78001");
    auto installer = fixture.Installer();
    CHECK(installer.StageTitle(Record()).code == "kit-invalid");
    CHECK(Read(fixture.temporary.path / "foreign-title/keep") == "foreign");
  }
}

void RestartDoesNotRecopyOrDestroyMutableFiles() {
  Fixture fixture;
  auto record = Record();
  auto first = fixture.Installer();
  CHECK(first.StageTitle(record).state == PackageActionState::kComplete);
  CHECK(first.CopyTemplate(record).state == PackageActionState::kComplete);
  const auto save = fixture.host_data /
                    "playden/games/install-hl2/library/prefixes/game/drive_c/save.dat";
  Write(save, "player-save");

  auto restarted = fixture.Installer();
  CHECK(restarted.StageTitle(record).state == PackageActionState::kComplete);
  CHECK(restarted.CopyTemplate(record).state == PackageActionState::kComplete);
  CHECK(Read(save) == "player-save");

  std::filesystem::remove(fixture.host_data /
                          "playden/games/install-hl2/library/prefixes/game/.playden-template-owner");
  CHECK(restarted.CopyTemplate(record).code == "bottle-collision");
  CHECK(Read(save) == "player-save");
}

void CorruptPublicationAndInvalidDriveTableAreRejected() {
  {
    Fixture fixture;
    auto record = Record();
    record.artifacts = {{"runtime", "rev", fixture.view.artifacts[0].sha256},
                        {"recipe", "steam-220-hl2-v1-1", fixture.view.artifacts[1].sha256}};
    auto installer = fixture.Installer();
    CHECK(installer.StageTitle(record).state == PackageActionState::kComplete);
    CHECK(installer.CopyTemplate(record).state == PackageActionState::kComplete);
    CHECK(installer.PublishProfile(record).state == PackageActionState::kComplete);
    Write(fixture.host_data / "playden/games/install-hl2/library/profiles/game.profile",
          "changed");
    CHECK(!installer.Inspect(record).profile_published);
  }
  {
    Fixture fixture;
    auto record = Record();
    record.artifacts = {{"runtime", "rev", fixture.view.artifacts[0].sha256},
                        {"recipe", "steam-220-hl2-v1-1", fixture.view.artifacts[1].sha256}};
    auto installer = fixture.Installer();
    CHECK(installer.StageTitle(record).state == PackageActionState::kComplete);
    CHECK(installer.CopyTemplate(record).state == PackageActionState::kComplete);
    Write(fixture.host_data / "homebrew/FAKE78001/game-launch.json", "foreign");
    CHECK(installer.PublishProfile(record).code == "publication-collision");
    CHECK(Read(fixture.host_data / "homebrew/FAKE78001/game-launch.json") == "foreign");
  }
  {
    Fixture fixture;
    Write(fixture.kit / fixture.view.template_drive_table,
          "c:\t../drive_c\nz:\t/\ny:\t../../escape\n");
    fixture.view.files[4].size =
        std::filesystem::file_size(fixture.kit / fixture.view.template_drive_table);
    fixture.view.files[4].sha256 = *TestHash(fixture.kit / fixture.view.template_drive_table);
    auto installer = fixture.Installer();
    CHECK(installer.CopyTemplate(Record()).code == "template-links-invalid");
  }
}

void InputPresetsAreOwnerPublishedAndHashVerified() {
  Fixture fixture;
  auto record = Record();
  record.artifacts = {{"runtime", "rev", fixture.view.artifacts[0].sha256},
                      {"recipe", "steam-220-hl2-v1-1", fixture.view.artifacts[1].sha256}};
  auto publication = fixture.Publication(record);
  publication.input_presets.push_back({"controller.input", "[input]\nmode=xinput\n"});
  publication.required_input_presets.push_back("controller.input");
  PackageInstallerOperations operations;
  operations.hash_file = TestHash;
  operations.hash_bytes = TestHashBytes;
  operations.free_bytes = [](const std::filesystem::path&) {
    return std::optional<std::uint64_t>(1ULL << 30);
  };
  operations.sync_path = [](const std::filesystem::path&, bool) { return true; };
  operations.canceled = [] { return false; };
  operations.validate_bottle = [](const std::filesystem::path& bottle) {
    return std::filesystem::is_regular_file(bottle / "drive_c/windows/system.reg");
  };
  operations.provide_publication = [&](const RuntimeInstall&) {
    return std::optional<ProfilePublication>(publication);
  };
  operations.publish_owned = [](const PublicationPaths& output,
                                const ProfilePublication& value,
                                const std::string& metadata) {
    Write(output.profile, value.profile_bytes, 0600);
    Write(output.catalog, value.profile_id + ".profile\n", 0600);
    for (const auto& preset : value.input_presets)
      Write(output.input_directory / preset.filename, preset.bytes, 0600);
    Write(output.descriptor, value.descriptor_bytes);
    Write(output.metadata, metadata, 0600);
    return PackageActionResult{PackageActionState::kComplete, {}, {}};
  };
  FilesystemPackageInstaller installer(fixture.view, fixture.paths, std::move(operations));
  CHECK(installer.StageTitle(record).state == PackageActionState::kComplete);
  CHECK(installer.CopyTemplate(record).state == PackageActionState::kComplete);
  CHECK(installer.PublishProfile(record).state == PackageActionState::kComplete);
  const auto preset = fixture.host_data /
                      "playden/games/install-hl2/library/input/controller.input";
  CHECK(Read(preset) == "[input]\nmode=xinput\n");
  CHECK(installer.Inspect(record).profile_published);
  Write(preset, "corrupt");
  CHECK(!installer.Inspect(record).profile_published);
}


void MissingReferencedInputAndSymlinkedManagedParentsAreRejected() {
  {
    Fixture fixture;
    auto record = Record();
    record.artifacts = {{"runtime", "rev", fixture.view.artifacts[0].sha256},
                        {"recipe", "steam-220-hl2-v1-1", fixture.view.artifacts[1].sha256}};
    auto publication = fixture.Publication(record);
    publication.required_input_presets.push_back("missing.input");
    PackageInstallerOperations operations;
    operations.hash_file = TestHash;
    operations.hash_bytes = TestHashBytes;
    operations.free_bytes = [](const std::filesystem::path&) {
      return std::optional<std::uint64_t>(1ULL << 30);
    };
    operations.sync_path = [](const std::filesystem::path&, bool) { return true; };
    operations.canceled = [] { return false; };
    operations.validate_bottle = [](const std::filesystem::path& bottle) {
      return std::filesystem::is_regular_file(bottle / "drive_c/windows/system.reg");
    };
    operations.provide_publication = [&](const RuntimeInstall&) {
      return std::optional<ProfilePublication>(publication);
    };
    operations.publish_owned = [](const PublicationPaths&, const ProfilePublication&,
                                  const std::string&) {
      return PackageActionResult{PackageActionState::kComplete, {}, {}};
    };
    FilesystemPackageInstaller installer(fixture.view, fixture.paths, std::move(operations));
    CHECK(installer.StageTitle(record).state == PackageActionState::kComplete);
    CHECK(installer.CopyTemplate(record).state == PackageActionState::kComplete);
    CHECK(installer.PublishProfile(record).code == "publication-invalid");
  }
  {
    Fixture fixture;
    auto record = Record();
    record.artifacts = {{"runtime", "rev", fixture.view.artifacts[0].sha256},
                        {"recipe", "steam-220-hl2-v1-1", fixture.view.artifacts[1].sha256}};
    auto installer = fixture.Installer();
    CHECK(installer.StageTitle(record).state == PackageActionState::kComplete);
    CHECK(installer.CopyTemplate(record).state == PackageActionState::kComplete);
    const auto library = fixture.host_data / "playden/games/install-hl2/library";
    std::filesystem::remove_all(library / "profiles");
    std::filesystem::create_directories(fixture.temporary.path / "outside");
    std::filesystem::create_directory_symlink(fixture.temporary.path / "outside",
                                              library / "profiles");
    CHECK(!installer.Inspect(record).known);
    CHECK(installer.PublishProfile(record).code == "kit-invalid");
    CHECK(std::filesystem::is_empty(fixture.temporary.path / "outside"));
  }
}

}  // namespace

int main() {
  {
    Fixture fixture;
    const auto game = fixture.host_data / "playden/games/install-hl2";
    std::filesystem::create_directories(game);
    CHECK(::chmod(game.c_str(), 0755) == 0);
    Write(game / "existing-save", "keep this save");
    std::string detail;
    CHECK(!EnsurePrivateDirectories(fixture.host_data, game, game / "library/prefixes", &detail));
    CHECK(detail.find("wrong owner or permissions") != std::string::npos);
    CHECK(Read(game / "existing-save") == "keep this save");
    struct stat info{};
    CHECK(::stat(game.c_str(), &info) == 0 && (info.st_mode & 0777) == 0755);
    CHECK(!std::filesystem::exists(game / "library"));
    CHECK(!EnsurePrivateDirectories(fixture.host_data, game, fixture.host_data / "../outside", &detail));
  }
  {
    Fixture fixture;
    const auto game = fixture.host_data / "playden/games/install-hl2";
    std::filesystem::create_directories(game.parent_path());
    std::filesystem::create_directories(fixture.temporary.path / "outside");
    std::filesystem::create_directory_symlink(fixture.temporary.path / "outside", game);
    std::string detail;
    CHECK(!EnsurePrivateDirectories(fixture.host_data, game, game / "library", &detail));
    CHECK(std::filesystem::is_empty(fixture.temporary.path / "outside"));
  }
  for (const auto& relative : {"library", "library/prefixes"}) {
    Fixture fixture;
    auto installer = fixture.Installer();
    const auto record = Record();
    CHECK(installer.CopyTemplate(record).state == PackageActionState::kComplete);
    const auto drifted = fixture.host_data / "playden/games/install-hl2" / relative;
    CHECK(::chmod(drifted.c_str(), 0755) == 0);
    CHECK(!installer.Inspect(record).mutable_bottle_owned);
    CHECK(installer.CopyTemplate(record).code == "bottle-collision");
    CHECK(Read(fixture.host_data / "playden/games/install-hl2/library/prefixes/game/drive_c/windows/system.reg") == "registry");
  }
  {
    Fixture fixture;
    auto installer = fixture.Installer();
    struct rlimit original{}, limited{};
    CHECK(::getrlimit(RLIMIT_FSIZE, &original) == 0);
    limited = original;
    limited.rlim_cur = 8; // Exercise a partial stage marker write, before payload copying.
    const auto previous = std::signal(SIGXFSZ, SIG_IGN);
    CHECK(::setrlimit(RLIMIT_FSIZE, &limited) == 0);
    const auto result = installer.CopyTemplate(Record());
    CHECK(::setrlimit(RLIMIT_FSIZE, &original) == 0);
    std::signal(SIGXFSZ, previous);
    CHECK(result.code == "staging-unavailable");
    CHECK(!std::filesystem::exists(fixture.host_data / "playden/staging/bottle-install-hl2"));
    CHECK(installer.CopyTemplate(Record()).state == PackageActionState::kComplete);
  }
  {
    Fixture fixture;
    auto record = Record();
    record.artifacts = {{"runtime", "rev", fixture.view.artifacts[0].sha256},
                        {"recipe", "steam-220-hl2-v1-1", fixture.view.artifacts[1].sha256}};
    auto installer = fixture.Installer();
    CHECK(installer.StageTitle(record).state == PackageActionState::kComplete);
    CHECK(installer.CopyTemplate(record).state == PackageActionState::kComplete);
    fixture.publish_public_profile = true;
    CHECK(installer.PublishProfile(record).code == "publication-verification");
    CHECK(!installer.Inspect(record).profile_published);
    const auto profiles = fixture.host_data / "playden/games/install-hl2/library/profiles";
    CHECK(IsPrivateDirectory(profiles));
    fixture.publish_public_profile = false;
    CHECK(installer.PublishProfile(record).state == PackageActionState::kComplete);
    CHECK(::chmod(profiles.c_str(), 0755) == 0);
    CHECK(!installer.Inspect(record).profile_published);
    CHECK(installer.PublishProfile(record).code == "private-directory");
  }
  CompletePublicationAndFreshInspection();
  CancelAndSpaceFailuresAreBounded();
  SyncFailureNeverPublishesWatchedTitle();
  NativeFsyncFallbackPublishesAtomically();
  LateSyncFailureIsReestablishedOnRetry();
  CollisionsTraversalSymlinksAndModesFailClosed();
  RestartDoesNotRecopyOrDestroyMutableFiles();
  CorruptPublicationAndInvalidDriveTableAreRejected();
  InputPresetsAreOwnerPublishedAndHashVerified();
  MissingReferencedInputAndSymlinkedManagedParentsAreRejected();
  std::puts("runtime package installer tests passed");
  return 0;
}
