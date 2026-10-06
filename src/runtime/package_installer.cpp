#include "runtime/package_installer.h"
#include "runtime/private_directories.h"
#include "platform/managed_directory.h"
#include "pw_title_publication_native.h"
#include "pw_title_publication.h"

#include "steam/backend/json.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

#include <sys/stat.h>
#include <sys/file.h>

#if defined(__PROSPERO__) || (defined(PLAYDEN_PLATFORM_PS5) && PLAYDEN_PLATFORM_PS5)
extern "C" int playden_ps5_file_lock(int descriptor, int operation);
#endif
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

namespace playden::runtime {
namespace fs = std::filesystem;

struct FilesystemPackageInstaller::Mapping {
  const KitFile* file = nullptr;
  std::string source_relative;
  std::string destination_relative;
};

struct FilesystemPackageInstaller::ResolvedPaths {
  fs::path title;
  fs::path game;
  fs::path library;
  fs::path bottle;
  fs::path title_staging;
  fs::path bottle_staging;
  PublicationPaths publication;
};

namespace {

constexpr std::uint64_t kCopyOverheadFloor = 1024 * 1024;
constexpr std::size_t kMaxPublicationBytes = 8192;
constexpr char kTitleOwner[] = ".playden-title-owner";
constexpr char kTemplateOwner[] = ".playden-template-owner";
constexpr char kStageOwner[] = ".playden-stage-owner";

PackageActionResult Complete() {
  return {PackageActionState::kComplete, {}, {}};
}

PackageActionResult Failed(std::string code, std::string message) {
  return {PackageActionState::kFailed, std::move(code), std::move(message)};
}

PackageActionResult Canceled() {
  return {PackageActionState::kCanceled, "canceled", "package installation was canceled"};
}

bool IsHexDigest(const std::string& value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char byte) {
           return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
         });
}

bool IsManagedTitle(const std::string& value) {
  if (value.size() != 9 || value.compare(0, 4, "FAKE") != 0) return false;
  if (!std::all_of(value.begin() + 4, value.end(), [](unsigned char byte) {
        return byte >= '0' && byte <= '9';
      }))
    return false;
  return value.compare("FAKE78000") >= 0 && value.compare("FAKE78999") <= 0;
}

bool IsSafeRelative(const std::string& value) {
  if (value.empty() || value.find('\\') != std::string::npos) return false;
  const fs::path path(value);
  if (path.is_absolute() || path.root_name() != fs::path{}) return false;
  for (const auto& part : path)
    if (part.empty() || part == "." || part == "..") return false;
  return path.generic_string() == value;
}

bool HasPrefix(const std::string& path, const std::string& prefix,
               std::string* suffix = nullptr) {
  if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0 ||
      path[prefix.size()] != '/')
    return false;
  if (suffix) *suffix = path.substr(prefix.size() + 1);
  return true;
}

bool IsRegularNoSymlink(const fs::path& path) {
  std::error_code error;
  const auto status = fs::symlink_status(path, error);
  return !error && fs::is_regular_file(status) && !fs::is_symlink(status);
}

bool ExistsNoThrow(const fs::path& path, bool* known = nullptr) {
  std::error_code error;
  const auto status = fs::symlink_status(path, error);
  if (error) {
    if (known) *known = error == std::errc::no_such_file_or_directory;
    return false;
  }
  if (known) *known = true;
  return status.type() != fs::file_type::not_found;
}

bool IsDirectoryNoSymlink(const fs::path& path) {
  std::error_code error;
  const auto status = fs::symlink_status(path, error);
  return !error && fs::is_directory(status) && !fs::is_symlink(status);
}

bool NativeSync(const fs::path& path, bool directory) {
  int flags = O_RDONLY;
#ifdef O_DIRECTORY
  if (directory) flags |= O_DIRECTORY;
#else
  (void)directory;
#endif
  const int descriptor = ::open(path.c_str(), flags);
  if (descriptor < 0) return false;
  const bool ok = ::fsync(descriptor) == 0;
  const bool closed = ::close(descriptor) == 0;
  return ok && closed;
}

bool PathComponentsHaveNoSymlink(const fs::path& root, const fs::path& relative,
                                 bool include_leaf) {
  fs::path current = root;
  std::size_t index = 0;
  const std::size_t count = static_cast<std::size_t>(std::distance(relative.begin(),
                                                                   relative.end()));
  for (const auto& part : relative) {
    current /= part;
    ++index;
    if (!include_leaf && index == count) break;
    std::error_code error;
    const auto status = fs::symlink_status(current, error);
    if (!error && fs::is_symlink(status)) return false;
    if (error && error != std::errc::no_such_file_or_directory) return false;
  }
  return true;
}

unsigned FileMode(const fs::path& path, bool* ok) {
  struct stat status {};
  if (::lstat(path.c_str(), &status) != 0 || !S_ISREG(status.st_mode)) {
    *ok = false;
    return 0;
  }
  *ok = true;
  return static_cast<unsigned>(status.st_mode & 07777);
}

std::optional<std::string> ReadBounded(const fs::path& path, std::size_t limit) {
  if (!IsRegularNoSymlink(path)) return std::nullopt;
  std::error_code error;
  const auto size = fs::file_size(path, error);
  if (error || size > limit) return std::nullopt;
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return std::nullopt;
  std::string bytes(static_cast<std::size_t>(size), '\0');
  stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!stream && !bytes.empty()) return std::nullopt;
  return bytes;
}

bool WriteFile(const fs::path& path, const std::string& bytes, unsigned mode) {
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, mode);
  if (fd < 0) return false;
  struct stat info{};
  bool ok = ::fchmod(fd, mode) == 0 && ::fstat(fd, &info) == 0 &&
            S_ISREG(info.st_mode) && info.st_uid == ::geteuid() &&
            (info.st_mode & 07777) == mode;
  std::size_t offset = 0;
  while (ok && offset < bytes.size()) {
    const auto written = ::write(fd, bytes.data() + offset, bytes.size() - offset);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) { ok = false; break; }
    offset += static_cast<std::size_t>(written);
  }
  if (::close(fd) != 0) ok = false;
  return ok;
}

// Only the just-created directory and its initial marker may be removed here.
// An interrupted initialization must not strand an unidentifiable staging tree.
void CleanInitialStage(const fs::path& staging, const struct stat& created) {
  using platform::DirectoryPolicy;
  using platform::ManagedDirectory;
  auto directory = ManagedDirectory::Open(staging, DirectoryPolicy::kApprovedTree);
  auto parent = ManagedDirectory::Open(staging.parent_path(), DirectoryPolicy::kApprovedTree);
  if (!directory || !parent) return;
  struct stat current{};
  if (::fstat(directory->fd(), &current) != 0 || current.st_dev != created.st_dev ||
      current.st_ino != created.st_ino || current.st_uid != ::geteuid()) return;
  const int listing_fd = ::dup(directory->fd());
  if (listing_fd < 0) return;
  DIR* listing = ::fdopendir(listing_fd);
  if (!listing) { ::close(listing_fd); return; }
  bool safe = true;
  errno = 0;
  while (const auto* entry = ::readdir(listing)) {
    const std::string name = entry->d_name;
    if (name != "." && name != ".." && name != kStageOwner) safe = false;
  }
  if (errno != 0) safe = false;
  ::closedir(listing);
  if (!safe || !directory->Validate()) return;
  struct stat marker{};
  if (directory->InspectChild(kStageOwner, &marker)) {
    if (!S_ISREG(marker.st_mode) || marker.st_uid != ::geteuid() ||
        (marker.st_mode & 07777) != 0600 || !directory->RemoveRegularFile(kStageOwner) ||
        !directory->Sync()) return;
  } else if (errno != ENOENT) return;
  if (!directory->Validate() || !parent->InspectChild(staging.filename().string(), &current) ||
      current.st_dev != created.st_dev || current.st_ino != created.st_ino) return;
  if (parent->RemoveEmptyDirectory(staging.filename().string())) parent->Sync();
}

std::map<std::string, std::string> ParseMarker(const std::string& bytes) {
  std::map<std::string, std::string> values;
  std::size_t start = 0;
  while (start < bytes.size()) {
    const auto end = bytes.find('\n', start);
    if (end == std::string::npos || end == start) return {};
    const auto equal = bytes.find('=', start);
    if (equal == std::string::npos || equal >= end || equal == start) return {};
    const auto key = bytes.substr(start, equal - start);
    const auto value = bytes.substr(equal + 1, end - equal - 1);
    if (!values.emplace(key, value).second) return {};
    start = end + 1;
  }
  return values;
}

std::string OwnerMarker(const RuntimeInstall& record, const VerifiedKitView& kit,
                        const char* kind) {
  std::string variant_id;
  for (const auto& variant : kit.variants)
    if (variant.title_id == record.title_id) variant_id = variant.id;
  std::ostringstream output;
  output << "schema=1\nkind=" << kind << "\ninstall_id=" << record.install_id
         << "\ntitle_id=" << record.title_id << "\nkit_id=" << kit.kit_id
         << "\nfiles_sha256=" << kit.files_sha256
         << "\nruntime_bundle_id=" << kit.runtime.bundle_id
         << "\nruntime_revision=" << kit.runtime.artifact_revision
         << "\nruntime_path=" << kit.runtime.path << "\nvariant_id=" << variant_id
         << "\ntemplate_path=" << kit.template_path
         << "\nrecipe_id=" << kit.recipe.id << "\nrecipe_version=" << kit.recipe.version
         << "\n";
  return output.str();
}

std::string StageMarker(const RuntimeInstall& record, const VerifiedKitView& kit,
                        const std::string& kind) {
  return OwnerMarker(record, kit, kind.c_str());
}

bool MarkerEquals(const fs::path& path, const std::string& expected) {
  const auto bytes = ReadBounded(path, 4096);
  return bytes && *bytes == expected;
}

const playden::steam::json::Value* Required(
    const playden::steam::json::Value& value, const char* name,
    playden::steam::json::Value::Type type) {
  const auto* member = value.Find(name);
  return member && member->type() == type ? member : nullptr;
}

bool DescriptorMatches(const RuntimeInstall& record, std::string_view bytes) {
  if (bytes.empty() || bytes.size() > kMaxPublicationBytes) return false;
  auto parsed = playden::steam::json::Parse(
      bytes, {kMaxPublicationBytes, 4, 32, kMaxPublicationBytes});
  if (!parsed || !parsed->is_object() || parsed->object().size() != 11) return false;
  using Type = playden::steam::json::Value::Type;
  const auto* schema = Required(*parsed, "schema_version", Type::kNumber);
  const auto* install = Required(*parsed, "install_id", Type::kString);
  const auto* title = Required(*parsed, "title_id", Type::kString);
  const auto* app = Required(*parsed, "app_id", Type::kNumber);
  const auto* root = Required(*parsed, "data_root", Type::kString);
  const auto* profile = Required(*parsed, "default_profile_id", Type::kString);
  const auto* revision = Required(*parsed, "settings_revision", Type::kNumber);
  const auto* runtime = Required(*parsed, "runtime_bundle_id", Type::kString);
  const auto* recipe = Required(*parsed, "recipe_id", Type::kString);
  const auto* recipe_version = Required(*parsed, "recipe_version", Type::kString);
  const auto* exit_policy = Required(*parsed, "exit_policy", Type::kString);
  return schema && schema->AsUint64() == 1 && install && install->text() == record.install_id &&
         title && title->text() == record.title_id && app && app->AsUint64() == record.app_id &&
         root && root->text() == record.data_root && profile &&
         profile->text() == record.default_profile_id && revision &&
         revision->AsUint64() == record.settings_revision && runtime &&
         runtime->text() == record.runtime_bundle_id && recipe &&
         recipe->text() == record.recipe_id && recipe_version &&
         recipe_version->text() == record.recipe_version && exit_policy &&
         exit_policy->text() == record.exit_policy;
}

bool PublicationIdentityMatches(const RuntimeInstall& record,
                                const ProfilePublication& publication) {
  if (!(publication.install_id == record.install_id &&
         publication.title_id == record.title_id &&
         publication.data_root == record.data_root &&
         publication.profile_id == record.default_profile_id &&
         publication.runtime_bundle_id == record.runtime_bundle_id &&
         publication.recipe_id == record.recipe_id &&
         publication.recipe_version == record.recipe_version &&
         publication.settings_revision == record.settings_revision &&
         !publication.profile_bytes.empty() &&
         publication.profile_bytes.size() <= kMaxPublicationBytes &&
         publication.profile_references_validated &&
         DescriptorMatches(record, publication.descriptor_bytes)))
    return false;
  std::set<std::string> names;
  for (const auto& preset : publication.input_presets) {
    if (!IsSafeRelative(preset.filename) ||
        fs::path(preset.filename).parent_path() != fs::path{} ||
        fs::path(preset.filename).extension() != ".input" || preset.bytes.empty() ||
        preset.bytes.size() > kMaxPublicationBytes || !names.insert(preset.filename).second)
      return false;
  }
  std::set<std::string> required;
  for (const auto& name : publication.required_input_presets)
    if (!IsSafeRelative(name) || fs::path(name).parent_path() != fs::path{} ||
        fs::path(name).extension() != ".input" || !required.insert(name).second)
      return false;
  return names == required;
}

}  // namespace

FilesystemPackageInstaller::FilesystemPackageInstaller(
    VerifiedKitView kit, PackageInstallPaths paths,
    PackageInstallerOperations operations)
    : kit_(std::move(kit)), paths_(std::move(paths)),
      operations_(std::move(operations)) {}

const KitTitleVariant* FilesystemPackageInstaller::Variant(
    const RuntimeInstall& record) const {
  const auto found = std::find_if(kit_.variants.begin(), kit_.variants.end(),
                                  [&](const KitTitleVariant& variant) {
                                    return variant.title_id == record.title_id;
                                  });
  return found == kit_.variants.end() ? nullptr : &*found;
}

bool FilesystemPackageInstaller::ValidView() const {
  if (paths_.console_data_root != "/data" || paths_.host_data_root.empty() ||
      kit_.root.empty() || kit_.kit_id.empty() || !IsHexDigest(kit_.files_sha256) ||
      kit_.runtime.bundle_id.empty() || kit_.runtime.artifact_revision.empty() ||
      !IsSafeRelative(kit_.runtime.path) || !IsSafeRelative(kit_.template_path) ||
      !IsSafeRelative(kit_.template_drive_table) || !IsSafeRelative(kit_.recipe.path) ||
      kit_.recipe.id.empty() || kit_.recipe.version.empty() ||
      !IsHexDigest(kit_.recipe.sha256) || kit_.files.empty() || kit_.variants.empty() ||
      !operations_.hash_file || !operations_.hash_bytes || !operations_.free_bytes ||
      !operations_.validate_bottle || !paths_.host_data_root.is_absolute() ||
      paths_.host_data_root != paths_.host_data_root.lexically_normal() || paths_.host_data_root.filename() != "data")
    return false;
  std::error_code root_error;
  const auto root_status = fs::symlink_status(kit_.root, root_error);
  if (root_error || !fs::is_directory(root_status) || fs::is_symlink(root_status)) return false;
  std::set<std::string> files;
  for (const auto& file : kit_.files) {
    if (!IsSafeRelative(file.path) || !IsHexDigest(file.sha256) || file.mode > 07777 ||
        !files.insert(file.path).second)
      return false;
  }
  std::set<std::string> variant_ids;
  std::set<std::string> title_ids;
  for (const auto& variant : kit_.variants)
    if (variant.id.empty() || !IsManagedTitle(variant.title_id) ||
        !IsSafeRelative(variant.path) || !variant_ids.insert(variant.id).second ||
        !title_ids.insert(variant.title_id).second)
      return false;
  for (const auto& artifact : kit_.artifacts)
    if (artifact.artifact_id.empty() || artifact.identity.empty() ||
        !IsHexDigest(artifact.sha256) || !IsSafeRelative(artifact.path))
      return false;
  return true;
}

std::optional<FilesystemPackageInstaller::ResolvedPaths>
FilesystemPackageInstaller::Resolve(const RuntimeInstall& record) const {
  if (!ValidView() || !Variant(record) ||
      record.data_root != "/data/playden/games/" + record.install_id ||
      record.install_id.empty() || record.install_id.find('/') != std::string::npos ||
      !IsManagedTitle(record.title_id) || record.default_profile_id.empty() ||
      record.default_profile_id.find('/') != std::string::npos ||
      record.runtime_bundle_id != kit_.runtime.bundle_id ||
      record.recipe_id != kit_.recipe.id || record.recipe_version != kit_.recipe.version)
    return std::nullopt;
  const fs::path game = paths_.host_data_root / "playden/games" / record.install_id;
  const fs::path title = paths_.host_data_root / "homebrew" / record.title_id;
  const fs::path library = game / "library";
  ResolvedPaths resolved;
  resolved.title = title;
  resolved.game = game;
  resolved.library = library;
  resolved.bottle = library / "prefixes/game";
  resolved.title_staging = paths_.host_data_root / "playden/staging" /
                           ("title-" + record.install_id);
  resolved.bottle_staging = paths_.host_data_root / "playden/staging" /
                            ("bottle-" + record.install_id);
  resolved.publication.profile = library / "profiles" /
                                 (record.default_profile_id + ".profile");
  resolved.publication.catalog = library / "profiles/profiles.lst";
  resolved.publication.input_directory = library / "input";
  resolved.publication.descriptor = title / "game-launch.json";
  resolved.publication.metadata = library / "profiles/.playden-publication";
  std::error_code root_error;
  const auto root_status = fs::symlink_status(paths_.host_data_root, root_error);
  if (root_error && root_error != std::errc::no_such_file_or_directory) return std::nullopt;
  if (!root_error && root_status.type() != fs::file_type::not_found &&
      (!fs::is_directory(root_status) || fs::is_symlink(root_status)))
    return std::nullopt;
  const auto safe_target = [&](const fs::path& target) {
    const auto relative = target.lexically_relative(paths_.host_data_root);
    return !relative.empty() && PathComponentsHaveNoSymlink(
                                    paths_.host_data_root, relative, true);
  };
  if (!safe_target(resolved.title) || !safe_target(resolved.publication.descriptor) ||
      !safe_target(resolved.game) || !safe_target(resolved.library) ||
      !safe_target(resolved.bottle) ||
      !safe_target(resolved.publication.profile.parent_path()) ||
      !safe_target(resolved.publication.profile) ||
      !safe_target(resolved.publication.catalog) ||
      !safe_target(resolved.publication.input_directory) ||
      !safe_target(resolved.publication.metadata) ||
      !safe_target(resolved.title_staging) || !safe_target(resolved.bottle_staging))
    return std::nullopt;
  return resolved;
}

std::vector<FilesystemPackageInstaller::Mapping>
FilesystemPackageInstaller::TitleMappings(const RuntimeInstall& record) const {
  std::vector<Mapping> mappings;
  const auto* variant = Variant(record);
  if (!variant) return mappings;
  std::set<std::string> destinations;
  for (const auto& file : kit_.files) {
    std::string relative;
    std::string destination;
    if (HasPrefix(file.path, variant->path, &relative)) {
      destination = relative;
    } else if (HasPrefix(file.path, kit_.runtime.path, &relative)) {
      destination = "win/wine/" + relative;
    } else {
      continue;
    }
    if (!IsSafeRelative(relative) || !IsSafeRelative(destination) ||
        !destinations.insert(destination).second)
      return {};
    mappings.push_back({&file, file.path, destination});
  }
  return mappings;
}

std::vector<FilesystemPackageInstaller::Mapping>
FilesystemPackageInstaller::TemplateMappings() const {
  std::vector<Mapping> mappings;
  for (const auto& file : kit_.files) {
    std::string relative;
    if (HasPrefix(file.path, kit_.template_path, &relative) && IsSafeRelative(relative))
      mappings.push_back({&file, file.path, relative});
  }
  return mappings;
}

bool FilesystemPackageInstaller::TitleMatches(
    const RuntimeInstall& record, const ResolvedPaths& paths) const {
  if (!IsDirectoryNoSymlink(paths.title) ||
      !MarkerEquals(paths.title / kTitleOwner, OwnerMarker(record, kit_, "title")))
    return false;
  const auto mappings = TitleMappings(record);
  if (mappings.empty()) return false;
  for (const auto& mapping : mappings) {
    const auto path = paths.title / mapping.destination_relative;
    bool mode_ok = false;
    std::error_code size_error;
    const auto size = fs::file_size(path, size_error);
    if (!PathComponentsHaveNoSymlink(paths.title, mapping.destination_relative, true) ||
        !IsRegularNoSymlink(path) || size_error || size != mapping.file->size ||
        FileMode(path, &mode_ok) != mapping.file->mode || !mode_ok)
      return false;
    const auto digest = operations_.hash_file(path);
    if (!digest || *digest != mapping.file->sha256) return false;
  }
  return true;
}

bool FilesystemPackageInstaller::BottleOwned(
    const RuntimeInstall& record, const ResolvedPaths& paths) const {
  return IsPrivateDirectoryTree(paths.game, paths.bottle) &&
         MarkerEquals(paths.bottle / kTemplateOwner,
                      OwnerMarker(record, kit_, "template")) &&
         operations_.validate_bottle(paths.bottle);
}

bool FilesystemPackageInstaller::ArtifactsMatch(
    const RuntimeInstall& record, const ResolvedPaths& paths) const {
  if (record.artifacts.size() != kit_.artifacts.size()) return false;
  std::set<std::string> matched;
  for (const auto& artifact : record.artifacts) {
    const auto found = std::find_if(
        kit_.artifacts.begin(), kit_.artifacts.end(), [&](const KitArtifact& expected) {
          return expected.artifact_id == artifact.artifact_id &&
                 expected.identity == artifact.identity && expected.sha256 == artifact.sha256;
        });
    if (found == kit_.artifacts.end() || !matched.insert(found->artifact_id).second)
      return false;
    const fs::path path = found->location == ImmutableArtifactLocation::kKit
                              ? kit_.root / found->path
                              : paths.title / found->path;
    const fs::path root = found->location == ImmutableArtifactLocation::kKit
                              ? kit_.root
                              : paths.title;
    if (!PathComponentsHaveNoSymlink(root, found->path, true) ||
        !IsRegularNoSymlink(path))
      return false;
    const auto hash = operations_.hash_file(path);
    if (!hash || *hash != found->sha256) return false;
  }
  return matched.size() == kit_.artifacts.size();
}

bool FilesystemPackageInstaller::PublicationMatches(
    const RuntimeInstall& record, const ResolvedPaths& paths,
    std::uint64_t* revision) const {
  const auto prefix = paths_.host_data_root == fs::path("/data") ? std::string{} : paths_.host_data_root.parent_path().string();
  PwTitlePublicationPlatform platform{};
  platform.host_root = prefix.empty() ? nullptr : prefix.c_str(); platform.owner_uid = ::geteuid();
  platform.try_lock_shared = [](void*, int descriptor) {
#if defined(__PROSPERO__) || (defined(PLAYDEN_PLATFORM_PS5) && PLAYDEN_PLATFORM_PS5)
    return playden_ps5_file_lock(descriptor, LOCK_SH | LOCK_NB);
#else
    return ::flock(descriptor, LOCK_SH | LOCK_NB);
#endif
  };
  const auto canonical = "/data/homebrew/" + record.title_id + "/game-launch.json";
  PwTitlePublicationDescriptor descriptor{}; char error[256]{};
  if (pw_title_publication_read_descriptor(canonical.c_str(), record.title_id.c_str(), &platform,
      &descriptor, error, sizeof(error)) != PW_OK) return false;
  const auto& launch = descriptor.launch;
  if (launch.install_id != record.install_id || launch.title_id != record.title_id || launch.app_id != record.app_id ||
      launch.data_root != record.data_root || launch.default_profile_id != record.default_profile_id ||
      launch.runtime_bundle_id != record.runtime_bundle_id || launch.recipe_id != record.recipe_id ||
      launch.recipe_version != record.recipe_version || launch.settings_revision != record.settings_revision ||
      launch.exit_policy != record.exit_policy) return false;
  // Same canonical file semantics and qualified shared lease as Wine admission.
  // Inspector retains the lease through the independent current-kit binding.
  PwTitlePublicationLease lease{}; pw_title_publication_lease_init(&lease);
  if (pw_title_publication_open(&descriptor, &platform, &lease, error, sizeof(error)) != PW_OK) return false;
  struct LeaseScope {
    PwTitlePublicationLease* lease;
    ~LeaseScope() { pw_title_publication_close(lease); }
  } held{&lease};
  auto control = platform::ManagedDirectory::Open(paths.game / ".playden-control", platform::DirectoryPolicy::kPrivate);
  if (!control) return false;
  const int fd = control->OpenRegularFile("publication-state.v1", true, 0600);
  if (fd < 0) return false;
  struct stat before{}, after{}, named{};
  bool valid = ::fstat(fd, &before) == 0 && before.st_nlink == 1 && before.st_size > 0 &&
      before.st_size <= PW_TITLE_PUBLICATION_MAX_BYTES;
  std::string bytes(valid ? static_cast<std::size_t>(before.st_size) : 0, '\0');
  std::size_t offset = 0;
  while (valid && offset < bytes.size()) {
    const auto count = ::read(fd, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) { valid = false; break; }
    offset += static_cast<std::size_t>(count);
  }
  valid = valid && ::fstat(fd, &after) == 0 && control->InspectChild("publication-state.v1", &named) &&
      before.st_dev == after.st_dev && before.st_ino == after.st_ino && before.st_size == after.st_size &&
      before.st_mode == after.st_mode && before.st_uid == after.st_uid && after.st_nlink == 1 &&
      after.st_dev == named.st_dev && after.st_ino == named.st_ino && after.st_size == named.st_size &&
      after.st_mode == named.st_mode && after.st_uid == named.st_uid && named.st_nlink == 1;
  const bool closed = ::close(fd) == 0;
  PwTitlePublication receipt{};
  if (!valid || !closed || pw_title_publication_parse(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &receipt) != PW_OK ||
      receipt.phase != PW_TITLE_PUBLICATION_COMMITTED || receipt.kit_id != kit_.kit_id ||
      receipt.install_id != record.install_id || receipt.title_id != record.title_id ||
      receipt.runtime_bundle_id != record.runtime_bundle_id || receipt.recipe_id != record.recipe_id ||
      receipt.recipe_version != record.recipe_version || receipt.settings_revision != record.settings_revision)
    return false;
  if (revision) *revision = receipt.settings_revision;
  return true;
}

bool FilesystemPackageInstaller::SyncPath(const fs::path& path, bool directory) const {
  return operations_.sync_path ? operations_.sync_path(path, directory)
                               : NativeSync(path, directory);
}

bool FilesystemPackageInstaller::SyncTree(const fs::path& root) const {
  if (!IsDirectoryNoSymlink(root)) return false;
  std::vector<fs::path> files;
  std::vector<fs::path> directories{root};
  std::error_code error;
  fs::recursive_directory_iterator iterator(root, fs::directory_options::none, error);
  const fs::recursive_directory_iterator end;
  while (!error && iterator != end) {
    const auto path = iterator->path();
    const auto status = iterator->symlink_status(error);
    if (error || fs::is_symlink(status)) return false;
    if (fs::is_directory(status)) {
      directories.push_back(path);
    } else if (fs::is_regular_file(status)) {
      files.push_back(path);
    } else {
      return false;
    }
    iterator.increment(error);
  }
  if (error) return false;
  for (const auto& file : files)
    if (!SyncPath(file, false)) return false;
  std::sort(directories.begin(), directories.end(), [](const fs::path& left,
                                                        const fs::path& right) {
    return std::distance(left.begin(), left.end()) >
           std::distance(right.begin(), right.end());
  });
  for (const auto& directory : directories)
    if (!SyncPath(directory, true)) return false;
  return true;
}

bool FilesystemPackageInstaller::SyncAncestors(const fs::path& directory) const {
  auto current = directory;
  while (true) {
    const auto relative = current.lexically_relative(paths_.host_data_root);
    if ((relative.empty() && current != paths_.host_data_root) ||
        (!relative.empty() && *relative.begin() == ".."))
      return false;
    if (!IsDirectoryNoSymlink(current) || !SyncPath(current, true)) return false;
    if (current == paths_.host_data_root) return true;
    const auto parent = current.parent_path();
    if (parent == current) return false;
    current = parent;
  }
}

bool FilesystemPackageInstaller::PublicationPathsSafe(
    const ProfilePublication& publication, const PublicationPaths& paths) const {
  const std::array<fs::path, 5> fixed = {paths.profile, paths.catalog,
                                         paths.input_directory, paths.descriptor,
                                         paths.metadata};
  for (const auto& path : fixed) {
    const auto relative = path.lexically_relative(paths_.host_data_root);
    if (relative.empty() || *relative.begin() == ".." ||
        !PathComponentsHaveNoSymlink(paths_.host_data_root, relative, true))
      return false;
  }
  for (const auto& preset : publication.input_presets) {
    const auto path = paths.input_directory / preset.filename;
    const auto relative = path.lexically_relative(paths_.host_data_root);
    if (relative.empty() || *relative.begin() == ".." ||
        !PathComponentsHaveNoSymlink(paths_.host_data_root, relative, true))
      return false;
  }
  return true;
}

PackageStatus FilesystemPackageInstaller::Inspect(const RuntimeInstall& record) {
  PackageStatus status;
  try {
    const auto paths = Resolve(record);
    if (!paths) return status;
    bool title_known = false;
    bool bottle_known = false;
    status.title_staged = ExistsNoThrow(paths->title, &title_known);
    status.mutable_bottle_present = ExistsNoThrow(paths->bottle, &bottle_known);
    if (!title_known || !bottle_known) return status;
    status.known = true;
    status.manifest_matches = status.title_staged && TitleMatches(record, *paths);
    status.template_owned = status.mutable_bottle_present && BottleOwned(record, *paths);
    status.mutable_bottle_owned = status.template_owned;
    status.artifacts_valid = status.manifest_matches && ArtifactsMatch(record, *paths);
    status.profile_published =
        status.manifest_matches && PublicationMatches(record, *paths,
                                                       &status.profile_settings_revision);
  } catch (const fs::filesystem_error&) {
    return PackageStatus{};
  }
  return status;
}

PackageActionResult FilesystemPackageInstaller::CopyMappings(
    const RuntimeInstall& record, const std::vector<Mapping>& mappings,
    const fs::path& staging, const fs::path& final,
    const std::string& stage_kind, const std::string& final_marker) {
  if (mappings.empty()) return Failed("kit-invalid", "kit mapping is empty or ambiguous");
  std::uint64_t required = 0;
  for (const auto& mapping : mappings) {
    if (!mapping.file || mapping.file->size > std::numeric_limits<std::uint64_t>::max() - required)
      return Failed("kit-invalid", "kit file sizes overflow");
    required += mapping.file->size;
    const auto source = kit_.root / mapping.source_relative;
    bool mode_ok = false;
    std::error_code error;
    if (!PathComponentsHaveNoSymlink(kit_.root, mapping.source_relative, true) ||
        !IsRegularNoSymlink(source) || fs::file_size(source, error) != mapping.file->size ||
        error || FileMode(source, &mode_ok) != mapping.file->mode || !mode_ok) {
      return Failed("kit-source-invalid", "kit source is missing, linked, or has wrong mode");
    }
    const auto hash = operations_.hash_file(source);
    if (!hash || *hash != mapping.file->sha256)
      return Failed("kit-source-invalid", "kit source hash does not match manifest");
  }
  const auto overhead = std::max(kCopyOverheadFloor, required / 10);
  if (required > std::numeric_limits<std::uint64_t>::max() - overhead)
    return Failed("kit-invalid", "space requirement overflows");
  std::error_code error;
  const auto staging_parent_relative = staging.parent_path().lexically_relative(paths_.host_data_root);
  if (staging_parent_relative.empty() ||
      !PathComponentsHaveNoSymlink(paths_.host_data_root, staging_parent_relative, true))
    return Failed("staging-unavailable", "owned staging root is unavailable");
  fs::create_directories(staging.parent_path(), error);
  if (error)
    return Failed("staging-unavailable", "owned staging root is unavailable");
  if (!SyncAncestors(staging.parent_path()))
    return Failed("sync-failed", "created staging ancestors were not durably synced");
  const auto available = operations_.free_bytes(staging.parent_path());
  if (!available || *available < required + overhead)
    return Failed("space-unavailable", "insufficient verified free space for package copy");

  const auto expected_stage = StageMarker(record, kit_, stage_kind);
  bool staging_known = false;
  const bool staging_exists = ExistsNoThrow(staging, &staging_known);
  if (!staging_known)
    return Failed("staging-unavailable", "staging state is unavailable");
  if (staging_exists) {
    if (stage_kind == "template" && !IsPrivateDirectory(staging))
      return Failed("staging-collision", "existing bottle staging has wrong owner or permissions");
    if (!MarkerEquals(staging / kStageOwner, expected_stage))
      return Failed("staging-collision", "staging path is not owned by this install");
    // RuntimeInstallService has already established stopped activity. Only our
    // identity-marked incomplete staging directory is eligible for cleanup.
    fs::remove_all(staging, error);
    if (error) return Failed("staging-cleanup", "owned incomplete staging could not be cleaned");
  }
  if (stage_kind == "template") {
    std::string detail;
    if (!EnsurePrivateDirectories(paths_.host_data_root, staging, staging, &detail))
      return Failed("private-directory", detail);
  } else {
    fs::create_directories(staging, error);
  }
  struct stat created_stage{};
  if (error || ::lstat(staging.c_str(), &created_stage) != 0)
    return Failed("staging-unavailable", "owned staging could not be initialized");
  if (!WriteFile(staging / kStageOwner, expected_stage, 0600)) {
    CleanInitialStage(staging, created_stage);
    return Failed("staging-unavailable", "owned staging could not be initialized");
  }
  if (!SyncPath(staging / kStageOwner, false) || !SyncPath(staging, true))
    return Failed("sync-failed", "owned staging identity was not durably synced");
  if (operations_.canceled && operations_.canceled()) return Canceled();

  std::array<char, 64 * 1024> buffer{};
  std::set<fs::path> copied_directories;
  for (const auto& mapping : mappings) {
    if (operations_.canceled && operations_.canceled()) return Canceled();
    const auto source = kit_.root / mapping.source_relative;
    const auto destination = staging / mapping.destination_relative;
    if (!PathComponentsHaveNoSymlink(staging, mapping.destination_relative, false))
      return Failed("destination-invalid", "staging destination contains a symbolic link");
    if (stage_kind == "template") {
      std::string detail;
      if (!EnsurePrivateDirectories(paths_.host_data_root, staging, destination.parent_path(), &detail))
        return Failed("private-directory", detail);
    } else {
      fs::create_directories(destination.parent_path(), error);
    }
    if (error) return Failed("copy-failed", "staging directory could not be created");
    std::ifstream input(source, std::ios::binary);
    const unsigned mode = stage_kind == "template" ? (0600 | (mapping.file->mode & 0100)) : mapping.file->mode;
    const int output = ::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, mode);
    if (!input || output < 0) {
      if (output >= 0) ::close(output);
      return Failed("copy-failed", "package file could not be opened exclusively");
    }
    struct stat destination_info{};
    if (::fchmod(output, mode) != 0 || ::fstat(output, &destination_info) != 0 ||
        !S_ISREG(destination_info.st_mode) || destination_info.st_uid != ::geteuid() ||
        (destination_info.st_mode & 07777) != mode) {
      ::close(output); return Failed("copy-failed", "package file mode could not be set before copying");
    }
    bool wrote = true;
    while (input) {
      input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      const auto count = input.gcount();
      std::streamsize offset = 0;
      while (offset < count) {
        const auto written = ::write(output, buffer.data() + offset, static_cast<std::size_t>(count - offset));
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) { wrote = false; break; }
        offset += written;
      }
      if (!wrote) break;
      if (operations_.canceled && operations_.canceled()) {
        ::close(output);
        return Canceled();
      }
    }
    const bool closed = ::close(output) == 0;
    if (!input.eof() || !wrote || !closed)
      return Failed("copy-failed", "package file copy or mode publication failed");
    const auto hash = operations_.hash_file(destination);
    if (!hash || *hash != mapping.file->sha256)
      return Failed("copy-verification", "copied package file hash does not match");
    if (!SyncPath(destination, false))
      return Failed("sync-failed", "copied package file was not durably synced");
    for (auto directory = destination.parent_path(); directory != staging.parent_path();
         directory = directory.parent_path()) {
      copied_directories.insert(directory);
      if (directory == staging) break;
    }
  }
  if (stage_kind == "template" && !operations_.validate_bottle(staging))
    return Failed("template-validation", "staged bottle failed semantic validation");
  if (!WriteFile(staging / final_marker, OwnerMarker(record, kit_, stage_kind.c_str()), 0600))
    return Failed("ownership-write", "package ownership marker could not be written");
  if (!SyncPath(staging / final_marker, false))
    return Failed("sync-failed", "package ownership marker was not durably synced");
  std::vector<fs::path> directories(copied_directories.begin(), copied_directories.end());
  std::sort(directories.begin(), directories.end(), [](const fs::path& left,
                                                        const fs::path& right) {
    return std::distance(left.begin(), left.end()) >
           std::distance(right.begin(), right.end());
  });
  for (const auto& directory : directories)
    if (!SyncPath(directory, true))
      return Failed("sync-failed", "staged package directory was not durably synced");
  if (!SyncPath(staging, true) || !SyncAncestors(staging.parent_path()))
    return Failed("sync-failed", "staging parent was not durably synced");
  const auto final_parent_relative = final.parent_path().lexically_relative(paths_.host_data_root);
  if (final_parent_relative.empty() ||
      !PathComponentsHaveNoSymlink(paths_.host_data_root, final_parent_relative, true))
    return Failed("destination-invalid", "final destination contains a symbolic link");
  if (stage_kind == "template") {
    std::string detail;
    const auto game = paths_.host_data_root / "playden/games" / record.install_id;
    if (!EnsurePrivateDirectories(paths_.host_data_root, game, final.parent_path(), &detail))
      return Failed("private-directory", detail);
  } else {
    fs::create_directories(final.parent_path(), error);
  }
  bool final_known = false;
  const bool final_exists = ExistsNoThrow(final, &final_known);
  if (error || !final_known || final_exists)
    return Failed("destination-collision", "final package path already exists");
  if (!SyncAncestors(final.parent_path()))
    return Failed("sync-failed", "destination parent was not durably synced before rename");
  fs::rename(staging, final, error);
  if (error)
    return Failed("atomic-rename-failed",
                  "package staging and destination must support one atomic rename");
  if (!SyncTree(final) || !SyncAncestors(final.parent_path()) ||
      !SyncAncestors(staging.parent_path()))
    return Failed("sync-failed", "atomic package rename was not durably synced");
  return Complete();
}

PackageActionResult FilesystemPackageInstaller::StageTitle(
    const RuntimeInstall& record) {
  const auto paths = Resolve(record);
  if (!paths) return Failed("kit-invalid", "record or verified kit view is invalid");
  if (TitleMatches(record, *paths))
    return SyncTree(paths->title) && SyncAncestors(paths->title.parent_path())
               ? Complete()
               : Failed("sync-failed", "owned native title durability could not be established");
  bool title_known = false;
  const bool title_exists = ExistsNoThrow(paths->title, &title_known);
  if (!title_known) return Failed("title-unavailable", "native title state is unavailable");
  if (title_exists)
    return Failed("title-collision", "native title source already exists and is not owned");
  return CopyMappings(record, TitleMappings(record), paths->title_staging, paths->title,
                      "title", kTitleOwner);
}

PackageActionResult FilesystemPackageInstaller::CopyTemplate(
    const RuntimeInstall& record) {
  const auto paths = Resolve(record);
  if (!paths) return Failed("kit-invalid", "record or verified kit view is invalid");
  const auto table = ReadBounded(kit_.root / kit_.template_drive_table, 1024);
  if (!table || *table != "c:\t../drive_c\nz:\t/\n")
    return Failed("template-links-invalid", "verified template drive table is not exact");
  bool bottle_known = false;
  const bool bottle_exists = ExistsNoThrow(paths->bottle, &bottle_known);
  if (!bottle_known) return Failed("bottle-unavailable", "mutable bottle state is unavailable");
  if (bottle_exists)
    return !BottleOwned(record, *paths)
               ? Failed("bottle-collision", "mutable bottle exists without matching ownership")
               : (SyncTree(paths->bottle) && SyncAncestors(paths->bottle.parent_path())
                      ? Complete()
                      : Failed("sync-failed",
                               "owned mutable bottle durability could not be established"));
  auto result = CopyMappings(record, TemplateMappings(), paths->bottle_staging,
                             paths->bottle, "template", kTemplateOwner);
  if (result.state == PackageActionState::kComplete && !BottleOwned(record, *paths))
    return Failed("template-validation", "published bottle failed semantic validation");
  return result;
}

PackageActionResult FilesystemPackageInstaller::ValidateArtifacts(
    const RuntimeInstall& record) {
  const auto paths = Resolve(record);
  if (!paths) return Failed("kit-invalid", "record or verified kit view is invalid");
  if (!TitleMatches(record, *paths) || !BottleOwned(record, *paths) ||
      !ArtifactsMatch(record, *paths))
    return Failed("artifact-invalid", "immutable artifacts or template provenance do not match");
  return Complete();
}

PackageActionResult FilesystemPackageInstaller::PublishProfile(const RuntimeInstall& record) {
  const auto paths = Resolve(record);
  if (!paths) return Failed("kit-invalid", "record or verified kit view is invalid");
  if (!TitleMatches(record, *paths) || !BottleOwned(record, *paths) || !ArtifactsMatch(record, *paths))
    return Failed("publication-prerequisite", "immutable title, artifacts, and bottle ownership must validate first");
  if (!operations_.provide_publication || !operations_.publish_owned)
    return Failed("publication-unavailable", "complete OWNER publication driver is unavailable");
  const auto publication = operations_.provide_publication(record);
  if (!publication || !PublicationIdentityMatches(record, *publication))
    return Failed("publication-invalid", "owner publication bytes do not match install identity");
  if (!PublicationPathsSafe(*publication, paths->publication))
    return Failed("managed-path-invalid", "managed publication path changed or contains a symbolic link");
  // Always hand off to the full driver, including committed retries: a receipt
  // alone cannot bypass durable OWNER/native reconciliation. No directory or
  // live settings effects may precede the driver's pending receipt protocol.
  const auto result = operations_.publish_owned(record, paths->publication, *publication);
  if (result.state != PackageActionState::kComplete) return result;
  const auto observed = Resolve(record);
  if (!observed || !PublicationMatches(record, *observed, nullptr))
    return Failed("publication-verification", "complete committed settings publication failed independent admission");
  return Complete();
}

}  // namespace playden::runtime
