#include "runtime/package_installer.h"

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
#include <fcntl.h>
#include <unistd.h>

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
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) return false;
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  return stream.good() && ::chmod(path.c_str(), mode) == 0;
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

std::optional<std::uint64_t> DecimalRevision(const std::string& value) {
  if (value.empty()) return std::nullopt;
  std::uint64_t result = 0;
  for (const unsigned char byte : value) {
    if (byte < '0' || byte > '9') return std::nullopt;
    const auto digit = static_cast<std::uint64_t>(byte - '0');
    if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
      return std::nullopt;
    result = result * 10 + digit;
  }
  return result;
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
      !operations_.validate_bottle || !operations_.provide_publication ||
      !operations_.publish_owned)
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
  return IsDirectoryNoSymlink(paths.bottle) &&
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
  const auto profile = ReadBounded(paths.publication.profile, kMaxPublicationBytes);
  const auto descriptor = ReadBounded(paths.publication.descriptor, kMaxPublicationBytes);
  const auto catalog = ReadBounded(paths.publication.catalog, 256);
  const auto metadata = ReadBounded(paths.publication.metadata, 4096);
  if (!profile || !descriptor || !catalog || !metadata ||
      *catalog != record.default_profile_id + ".profile\n" ||
      !DescriptorMatches(record, *descriptor))
    return false;
  const auto values = ParseMarker(*metadata);
  const auto profile_hash = operations_.hash_file(paths.publication.profile);
  const auto descriptor_hash = operations_.hash_file(paths.publication.descriptor);
  if (!profile_hash || !descriptor_hash ||
      values.find("schema") == values.end() || values.at("schema") != "1" ||
      values.find("install_id") == values.end() ||
      values.at("install_id") != record.install_id ||
      values.find("title_id") == values.end() || values.at("title_id") != record.title_id ||
      values.find("data_root") == values.end() || values.at("data_root") != record.data_root ||
      values.find("profile_id") == values.end() ||
      values.at("profile_id") != record.default_profile_id ||
      values.find("runtime_bundle_id") == values.end() ||
      values.at("runtime_bundle_id") != record.runtime_bundle_id ||
      values.find("recipe_id") == values.end() || values.at("recipe_id") != record.recipe_id ||
      values.find("recipe_version") == values.end() ||
      values.at("recipe_version") != record.recipe_version ||
      values.find("settings_revision") == values.end() ||
      values.find("profile_sha256") == values.end() ||
      values.at("profile_sha256") != *profile_hash ||
      values.find("descriptor_sha256") == values.end() ||
      values.at("descriptor_sha256") != *descriptor_hash ||
      values.find("kit_id") == values.end() || values.at("kit_id") != kit_.kit_id ||
      values.find("input_count") == values.end())
    return false;
  const auto input_count = DecimalRevision(values.at("input_count"));
  if (!input_count || *input_count > 64 || values.size() != 13 + *input_count * 2)
    return false;
  for (std::uint64_t index = 0; index < *input_count; ++index) {
    const auto prefix = "input." + std::to_string(index);
    const auto name = values.find(prefix + ".name");
    const auto expected_hash = values.find(prefix + ".sha256");
    if (name == values.end() || expected_hash == values.end() ||
        !IsSafeRelative(name->second) ||
        fs::path(name->second).parent_path() != fs::path{} ||
        fs::path(name->second).extension() != ".input")
      return false;
    const auto preset_path = paths.publication.input_directory / name->second;
    if (!PathComponentsHaveNoSymlink(paths.library,
                                     fs::path("input") / name->second, true) ||
        !IsRegularNoSymlink(preset_path))
      return false;
    const auto actual_hash = operations_.hash_file(preset_path);
    if (!actual_hash || *actual_hash != expected_hash->second) return false;
  }
  const auto parsed_revision = DecimalRevision(values.at("settings_revision"));
  if (!parsed_revision || *parsed_revision != record.settings_revision) return false;
  if (revision) *revision = *parsed_revision;
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

bool FilesystemPackageInstaller::SyncPublication(
    const ProfilePublication& publication, const PublicationPaths& paths) const {
  if (!PublicationPathsSafe(publication, paths) ||
      !SyncPath(paths.profile, false) || !SyncPath(paths.catalog, false) ||
      !SyncPath(paths.descriptor, false) || !SyncPath(paths.metadata, false))
    return false;
  for (const auto& preset : publication.input_presets)
    if (!SyncPath(paths.input_directory / preset.filename, false)) return false;
  if (!SyncAncestors(paths.profile.parent_path()) ||
      (!publication.input_presets.empty() && !SyncAncestors(paths.input_directory)) ||
      !SyncAncestors(paths.descriptor.parent_path()))
    return false;
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
    if (!MarkerEquals(staging / kStageOwner, expected_stage))
      return Failed("staging-collision", "staging path is not owned by this install");
    // RuntimeInstallService has already established stopped activity. Only our
    // identity-marked incomplete staging directory is eligible for cleanup.
    fs::remove_all(staging, error);
    if (error) return Failed("staging-cleanup", "owned incomplete staging could not be cleaned");
  }
  fs::create_directories(staging, error);
  if (error || !WriteFile(staging / kStageOwner, expected_stage, 0600))
    return Failed("staging-unavailable", "owned staging could not be initialized");
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
    fs::create_directories(destination.parent_path(), error);
    if (error) return Failed("copy-failed", "staging directory could not be created");
    std::ifstream input(source, std::ios::binary);
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!input || !output) return Failed("copy-failed", "package file could not be opened");
    while (input) {
      input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      const auto count = input.gcount();
      if (count > 0) output.write(buffer.data(), count);
      if (!output) return Failed("copy-failed", "package file write failed");
      if (operations_.canceled && operations_.canceled()) {
        output.close();
        return Canceled();
      }
    }
    output.close();
    if (!input.eof() || !output.good() || ::chmod(destination.c_str(), mapping.file->mode) != 0)
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
  fs::create_directories(final.parent_path(), error);
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

PackageActionResult FilesystemPackageInstaller::PublishProfile(
    const RuntimeInstall& record) {
  const auto paths = Resolve(record);
  if (!paths) return Failed("kit-invalid", "record or verified kit view is invalid");
  if (!TitleMatches(record, *paths) || !BottleOwned(record, *paths) ||
      !ArtifactsMatch(record, *paths))
    return Failed("publication-prerequisite",
                  "immutable title, artifacts, and bottle ownership must validate first");
  const auto current_publication = operations_.provide_publication(record);
  if (!current_publication || !PublicationIdentityMatches(record, *current_publication))
    return Failed("publication-invalid", "owner publication bytes do not match install identity");
  const auto callback_paths = Resolve(record);
  if (!callback_paths ||
      !PublicationPathsSafe(*current_publication, callback_paths->publication))
    return Failed("managed-path-invalid",
                  "managed publication path changed or contains a symbolic link");
  if (PublicationMatches(record, *callback_paths, nullptr))
    return SyncPublication(*current_publication, callback_paths->publication)
               ? Complete()
               : Failed("sync-failed", "published profile set was not durably synced");
  bool profile_known = false, catalog_known = false, descriptor_known = false;
  bool metadata_known = false, input_known = false;
  const bool profile_exists = ExistsNoThrow(callback_paths->publication.profile, &profile_known);
  const bool catalog_exists = ExistsNoThrow(callback_paths->publication.catalog, &catalog_known);
  const bool descriptor_exists = ExistsNoThrow(callback_paths->publication.descriptor, &descriptor_known);
  const bool metadata_exists = ExistsNoThrow(callback_paths->publication.metadata, &metadata_known);
  const bool input_exists = ExistsNoThrow(callback_paths->publication.input_directory, &input_known);
  const bool any_existing = profile_exists || catalog_exists || descriptor_exists ||
                            metadata_exists || input_exists;
  if (!profile_known || !catalog_known || !descriptor_known || !metadata_known || !input_known)
    return Failed("publication-unavailable", "publication filesystem state is unavailable");
  if (any_existing) {
    const auto metadata = ReadBounded(callback_paths->publication.metadata, 4096);
    const auto values = metadata ? ParseMarker(*metadata)
                                 : std::map<std::string, std::string>{};
    const auto owned = [&](const char* key, const std::string& expected) {
      const auto found = values.find(key);
      return found != values.end() && found->second == expected;
    };
    if (!owned("schema", "1") || !owned("install_id", record.install_id) ||
        !owned("title_id", record.title_id) || !owned("data_root", record.data_root) ||
        !owned("profile_id", record.default_profile_id) ||
        !owned("runtime_bundle_id", record.runtime_bundle_id) ||
        !owned("recipe_id", record.recipe_id) ||
        !owned("recipe_version", record.recipe_version) || !owned("kit_id", kit_.kit_id))
      return Failed("publication-collision",
                    "existing profile or descriptor has no matching ownership metadata");
  }
  const auto& publication = *current_publication;
  const auto profile_hash = operations_.hash_bytes(publication.profile_bytes);
  const auto descriptor_hash = operations_.hash_bytes(publication.descriptor_bytes);
  if (!profile_hash || !descriptor_hash || !IsHexDigest(*profile_hash) ||
      !IsHexDigest(*descriptor_hash))
    return Failed("hash-unavailable", "publication hash operation is unavailable");
  std::ostringstream metadata;
  metadata << "schema=1\ninstall_id=" << record.install_id << "\ntitle_id="
           << record.title_id << "\ndata_root=" << record.data_root << "\nprofile_id="
           << record.default_profile_id << "\nruntime_bundle_id="
           << record.runtime_bundle_id << "\nrecipe_id=" << record.recipe_id
           << "\nrecipe_version=" << record.recipe_version << "\nsettings_revision="
           << record.settings_revision << "\nprofile_sha256=" << *profile_hash
           << "\ndescriptor_sha256=" << *descriptor_hash << "\nkit_id=" << kit_.kit_id
           << "\ninput_count=" << publication.input_presets.size() << "\n";
  for (std::size_t index = 0; index < publication.input_presets.size(); ++index) {
    const auto hash = operations_.hash_bytes(publication.input_presets[index].bytes);
    if (!hash || !IsHexDigest(*hash))
      return Failed("hash-unavailable", "input preset hash operation is unavailable");
    metadata << "input." << index << ".name=" << publication.input_presets[index].filename
             << "\ninput." << index << ".sha256=" << *hash << "\n";
  }
  const auto final_paths = Resolve(record);
  if (!final_paths || !PublicationPathsSafe(publication, final_paths->publication))
    return Failed("managed-path-invalid",
                  "managed publication path changed or contains a symbolic link");
  auto result = operations_.publish_owned(final_paths->publication, publication, metadata.str());
  if (result.state != PackageActionState::kComplete) return result;
  const auto published_paths = Resolve(record);
  if (!published_paths || !PublicationPathsSafe(publication, published_paths->publication))
    return Failed("managed-path-invalid",
                  "owner publication created a symbolic-link path");
  if (!SyncPublication(publication, published_paths->publication))
    return Failed("sync-failed", "published profile set was not durably synced");
  std::uint64_t revision = 0;
  if (!PublicationMatches(record, *published_paths, &revision))
    return Failed("publication-verification", "published bytes or metadata do not match");
  return Complete();
}

}  // namespace playden::runtime
