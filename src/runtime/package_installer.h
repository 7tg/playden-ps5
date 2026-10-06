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
  // The PS5 title cannot query free space (statfs terminates it). When set, an
  // empty free_bytes result does not refuse the copy; a full volume then fails
  // the copy itself with a write error. Never set where free space is readable.
  bool free_space_unverifiable = false;
  std::function<bool()> canceled;
  // Optional test/platform seam. With no override, the implementation uses
  // open(2)+fsync(2). `directory` distinguishes directory durability barriers.
  std::function<bool(const std::filesystem::path&, bool directory)> sync_path;
  // Validates immutable template provenance requirements that are semantic rather
  // than file-level (including the required core registry entries).
  std::function<bool(const std::filesystem::path&)> validate_bottle;
  std::function<std::optional<ProfilePublication>(const RuntimeInstall&)>
      provide_publication;
  // Complete OWNER driver: owns control setup, pending receipt, frozen operation,
  // registry/DXVK effects and individually durable file publication. This
  // installer never creates publication directories or writes completion
  // metadata. Success is independently checked through committed admission.
  std::function<PackageActionResult(const RuntimeInstall&, const PublicationPaths&,
                                    const ProfilePublication&)> publish_owned;
};

class FilesystemPackageInstaller final : public PackageInstaller {
 public:
  // Callers must gate every mutating method on a positively stopped title.
  // Unknown/active state never reaches this filesystem component.
  FilesystemPackageInstaller(VerifiedKitView kit, PackageInstallPaths paths,
                             PackageInstallerOperations operations);

  PackageStatus Inspect(const RuntimeInstall& record) override;
  // The game's own title name and icon, kept private beside its bottle. Staging
  // and refresh copy them over the kit's param.json and icon0.png; no icon keeps
  // the kit's.
  PackageActionResult WriteTitleMetadata(const RuntimeInstall& record, const std::string& param_json,
                                         const std::optional<std::string>& icon_png) override;
  // The metadata was generated and the installed title carries it.
  bool TitleMetadataCurrent(const RuntimeInstall& record) override;
  PackageActionResult StageTitle(const RuntimeInstall& record) override;
  PackageActionResult CopyTemplate(const RuntimeInstall& record) override;
  PackageActionResult ValidateArtifacts(const RuntimeInstall& record) override;
  PackageActionResult PublishProfile(const RuntimeInstall& record) override;
  bool TitleCurrent(const RuntimeInstall& record) override;
  std::string AdmissionDiagnostic(const RuntimeInstall& record) override;
  PackageActionResult RepairBottle(const RuntimeInstall& record) override;
  // Replaces changed title files (temp, fsync, rename) and rewrites the title
  // and bottle owner markers. Refuses unless each existing marker equals the
  // new one except for the kit's files_sha256, and the record's artifacts are
  // the kit's. The bottle's contents and the publication are not touched.
  PackageActionResult RefreshTitle(const RuntimeInstall& record) override;

 private:
  struct Mapping;
  struct ResolvedPaths;

  std::optional<ResolvedPaths> Resolve(const RuntimeInstall& record) const;
  bool RecipeAccepted(const RuntimeInstall& record) const;
  const KitTitleVariant* Variant(const RuntimeInstall& record) const;
  bool ValidView() const;
  // Owner marker, exact file contents and modes, and published (0755) folders.
  bool TitleMatches(const RuntimeInstall& record, const ResolvedPaths& paths) const;
  std::filesystem::path TitleMetadataPath(const RuntimeInstall& record, const std::string& relative) const;
  // Exact file contents and modes only, whatever the marker and folder modes say.
  bool TitleFilesMatch(const RuntimeInstall& record, const ResolvedPaths& paths) const;
  bool BottleOwned(const RuntimeInstall& record, const ResolvedPaths& paths) const;
  bool ArtifactsMatch(const RuntimeInstall& record, const ResolvedPaths& paths) const;
  bool PublicationMatches(const RuntimeInstall& record, const ResolvedPaths& paths,
                          std::uint64_t* revision) const;
  bool SyncPath(const std::filesystem::path& path, bool directory) const;
  bool SyncTree(const std::filesystem::path& root) const;
  bool SyncAncestors(const std::filesystem::path& directory) const;
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
