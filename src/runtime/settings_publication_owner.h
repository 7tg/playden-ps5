#pragma once
#include "runtime/settings_publication.h"
namespace playden::runtime {
// Binds publication callbacks to durable OWNER persistence, immutable package
// ownership and native registration. Use through the coordinator so callbacks
// run under its qualified publisher lease and committed semantic verification.
// Dependencies capture this object; it and its collaborators must outlive them.
class SettingsPublicationOwner {
 public:
  SettingsPublicationOwner(InstallRecordStore& records, PackageInstaller& package, NativeCoordinator& native);
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
