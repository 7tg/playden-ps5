#pragma once
#include "runtime/settings_publication.h"
namespace playden::runtime {
// Binds publication callbacks to durable OWNER persistence, immutable package
// ownership and native registration. Use through the coordinator so callbacks
// run under its qualified publisher lease and committed semantic verification.
// Dependencies capture this object; it and its collaborators must outlive them.
enum class PublicationControlStep { kOwnerArmed, kDirectorySynced, kLockSynced, kOwnerPrepared };
class SettingsPublicationOwner {
 public:
  SettingsPublicationOwner(InstallRecordStore& records, PackageInstaller& package, NativeCoordinator& native);
  // Creates only the private control directory and stable empty lock, after a
  // durable initial OWNER intent. Staged title/bottle/data root must already be
  // owned. Matching interrupted intent can resume; foreign paths are refused.
  // after_step is a test-only crash seam; production leaves it empty.
  PackageActionResult InitializeControl(const RuntimeInstall& record, const std::string& transaction_id,
      const std::filesystem::path& host_namespace_prefix = {},
      const std::function<bool(PublicationControlStep)>& after_step = {});
  Result<SettingsPublicationOperation> Prepare(const RuntimeInstall& record, const SettingsLaunchPlan& plan,
      const std::string& kit_id, const std::string& transaction_id, std::int64_t registry_unix_time,
      const std::filesystem::path& host_namespace_prefix = {});
  // Routes a persisted settings-control/settings-publication UUID to its exact
  // frozen active/prepared operation. No plan/recipe regeneration. The complete
  // coordinator rechecks ownership/receipt and reconciles durable Ready.
  PackageActionResult Resume(const RuntimeInstall& record,
      const std::filesystem::path& host_namespace_prefix = {});
  PublicationDependencies Dependencies(const std::filesystem::path& host_namespace_prefix = {});
 private:
  PackageActionResult Block(const SettingsPublicationOperation& operation);
  PackageActionResult Reconcile(const SettingsPublicationOperation& operation);
  PackageActionResult AuthorizeRetirement(const SettingsPublicationOperation& previous,
                                         const SettingsPublicationOperation& next);
  PackageActionResult Persist(const RuntimeInstall& record);
  InstallRecordStore& records_;
  PackageInstaller& package_;
  NativeCoordinator& native_;
};
}  // namespace playden::runtime
