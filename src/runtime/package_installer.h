#pragma once

#include "runtime/install_service.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace playden::runtime {

// The kit adapter constructs this only after validating manifest schema, the exact
// file inventory and aggregate digest. Package installation treats the view as the
// immutable validation boundary, then rechecks every source file before copying.
struct KitFile {
  std::string path;
  std::uint64_t size = 0;
  std::string sha256;
  unsigned mode = 0;
};

struct KitRuntime {
  std::string bundle_id;
  std::string artifact_revision;
  std::string path;
};

struct KitRecipe {
  std::string id;
  std::string version;
  std::string path;
  std::string sha256;
};

struct KitTitleVariant {
  std::string id;
  std::string title_id;
  std::string path;
};

enum class ImmutableArtifactLocation { kKit, kStagedTitle };
struct KitArtifact {
  std::string artifact_id;
  std::string identity;
  std::string sha256;
  ImmutableArtifactLocation location = ImmutableArtifactLocation::kKit;
  std::string path;
};

struct VerifiedKitView {
  std::filesystem::path root;
  std::string kit_id;
  std::string files_sha256;
  KitRuntime runtime;
  std::string template_path;
  std::string template_drive_table;
  KitRecipe recipe;
  std::vector<KitTitleVariant> variants;
  std::vector<KitFile> files;
  std::vector<KitArtifact> artifacts;
};

// Canonical paths remain /data/... in RuntimeInstall and descriptors. Tests map
// that namespace beneath host_data_root without weakening canonical validation.
struct PackageInstallPaths {
  std::string console_data_root = "/data";
  std::filesystem::path host_data_root = "/data";
};

struct ProfilePublication {
  std::string install_id;
  std::string title_id;
  std::string data_root;
  std::string profile_id;
  std::string runtime_bundle_id;
  std::string recipe_id;
  std::string recipe_version;
  std::uint64_t settings_revision = 0;
  std::string profile_bytes;
  std::string descriptor_bytes;
  // Populated by the OWNER adapter from its pinned profile parser. This is a
  // validation boundary, not a second settings/profile parser.
  bool profile_references_validated = false;
  std::vector<std::string> required_input_presets;
  struct InputPreset {
    std::string filename;
    std::string bytes;
  };
  // Every preset referenced by the effective profile must be present here. An
  // unsupported/unavailable preset therefore blocks publication and readiness.
  std::vector<InputPreset> input_presets;
};

struct PublicationPaths {
  std::filesystem::path profile;
  std::filesystem::path catalog;
  std::filesystem::path input_directory;
  std::filesystem::path descriptor;
  std::filesystem::path metadata;
};

struct PackageInstallerOperations {
  std::function<std::optional<std::string>(const std::filesystem::path&)> hash_file;
  std::function<std::optional<std::string>(std::string_view)> hash_bytes;
  std::function<std::optional<std::uint64_t>(const std::filesystem::path&)> free_bytes;
  std::function<bool()> canceled;
  // Optional test/platform seam. With no override, the implementation uses
  // open(2)+fsync(2). `directory` distinguishes directory durability barriers.
  std::function<bool(const std::filesystem::path&, bool directory)> sync_path;
  // Validates immutable template provenance requirements that are semantic rather
  // than file-level (including the required core registry entries).
  std::function<bool(const std::filesystem::path&)> validate_bottle;
  std::function<std::optional<ProfilePublication>(const RuntimeInstall&)>
      provide_publication;
  // The OWNER callback atomically publishes profile, descriptor and metadata.
  std::function<PackageActionResult(const PublicationPaths&,
                                    const ProfilePublication&,
                                    const std::string& metadata)>
      publish_owned;
};

class FilesystemPackageInstaller final : public PackageInstaller {
 public:
  // Callers must gate every mutating method on a positively stopped title.
  // Unknown/active state never reaches this filesystem component.
  FilesystemPackageInstaller(VerifiedKitView kit, PackageInstallPaths paths,
                             PackageInstallerOperations operations);

  PackageStatus Inspect(const RuntimeInstall& record) override;
  PackageActionResult StageTitle(const RuntimeInstall& record) override;
  PackageActionResult CopyTemplate(const RuntimeInstall& record) override;
  PackageActionResult ValidateArtifacts(const RuntimeInstall& record) override;
  PackageActionResult PublishProfile(const RuntimeInstall& record) override;

 private:
  struct Mapping;
  struct ResolvedPaths;

  std::optional<ResolvedPaths> Resolve(const RuntimeInstall& record) const;
  const KitTitleVariant* Variant(const RuntimeInstall& record) const;
  bool ValidView() const;
  bool TitleMatches(const RuntimeInstall& record, const ResolvedPaths& paths) const;
  bool BottleOwned(const RuntimeInstall& record, const ResolvedPaths& paths) const;
  bool ArtifactsMatch(const RuntimeInstall& record, const ResolvedPaths& paths) const;
  bool PublicationMatches(const RuntimeInstall& record, const ResolvedPaths& paths,
                          std::uint64_t* revision) const;
  bool SyncPath(const std::filesystem::path& path, bool directory) const;
  bool SyncTree(const std::filesystem::path& root) const;
  bool SyncAncestors(const std::filesystem::path& directory) const;
  bool SyncPublication(const ProfilePublication& publication,
                       const PublicationPaths& paths) const;
  bool PublicationPathsSafe(const ProfilePublication& publication,
                            const PublicationPaths& paths) const;
  std::vector<Mapping> TitleMappings(const RuntimeInstall& record) const;
  std::vector<Mapping> TemplateMappings() const;
  PackageActionResult CopyMappings(const RuntimeInstall& record,
                                   const std::vector<Mapping>& mappings,
                                   const std::filesystem::path& staging,
                                   const std::filesystem::path& final,
                                   const std::string& stage_kind,
                                   const std::string& final_marker);

  VerifiedKitView kit_;
  PackageInstallPaths paths_;
  PackageInstallerOperations operations_;
};

}  // namespace playden::runtime
