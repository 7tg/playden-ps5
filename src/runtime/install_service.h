#pragma once

#include "runtime/game_install.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace playden::runtime {

struct RecordUpdateResult {
  bool ok = false;
  std::string code;
  std::string message;
};

class InstallRecordStore {
public:
  virtual ~InstallRecordStore() = default;
  virtual std::optional<RuntimeInstall>
  Find(const std::string &install_id) const = 0;
  virtual std::vector<RuntimeInstall> Snapshot() const = 0;
  virtual RecordUpdateResult Update(const RuntimeInstall &record) = 0;
};

class RepositoryInstallRecordStore final : public InstallRecordStore {
public:
  explicit RepositoryInstallRecordStore(RuntimeInstallRepository &repository);
  std::optional<RuntimeInstall>
  Find(const std::string &install_id) const override;
  std::vector<RuntimeInstall> Snapshot() const override;
  RecordUpdateResult Update(const RuntimeInstall &record) override;

private:
  RuntimeInstallRepository &repository_;
};

struct PackageStatus {
  bool known = false;
  bool title_staged = false;
  bool manifest_matches = false;
  bool template_owned = false;
  bool mutable_bottle_present = false;
  bool mutable_bottle_owned = false;
  bool artifacts_valid = false;
  bool profile_published = false;
  std::uint64_t profile_settings_revision = 0;
  // Physical staged source inspected by the immutable package adapter. Native
  // coordination must bind these ownership facts to its same mapped namespace.
  std::string title_source_path;
};

enum class PackageActionState { kComplete, kCanceled, kFailed };
struct PackageActionResult {
  PackageActionState state = PackageActionState::kFailed;
  std::string code;
  std::string message;
};

class PackageInstaller {
public:
  virtual ~PackageInstaller() = default;
  virtual PackageStatus Inspect(const RuntimeInstall &record) = 0;
  virtual PackageActionResult StageTitle(const RuntimeInstall &record) = 0;
  virtual PackageActionResult CopyTemplate(const RuntimeInstall &record) = 0;
  virtual PackageActionResult
  ValidateArtifacts(const RuntimeInstall &record) = 0;
  virtual PackageActionResult PublishProfile(const RuntimeInstall &record) = 0;
};

enum class NativeActivity { kStopped, kActive, kUnknown };
struct NativeStatus {
  bool available = false;
  bool installed = false;
  bool source_available = false;
  bool owned_manifest_matches = false;
  bool collision = false;
  bool scan_pending = false;
};
enum class RegistrationResultState {
  kAccepted,
  kScanQueued,
  kStaleHook,
  kFailed
};
struct RegistrationResult {
  RegistrationResultState state = RegistrationResultState::kFailed;
  std::string code;
  std::string message;
};

class NativeCoordinator {
public:
  virtual ~NativeCoordinator() = default;
  virtual NativeActivity Activity(const std::string &title_id) = 0;
  virtual NativeStatus Query(const RuntimeInstall &record) = 0;
  virtual RegistrationResult Register(const RuntimeInstall &record) = 0;
};

struct CompatibilityResult {
  bool compatible = false;
  std::string reason;
};
using CompatibilityQuery =
    std::function<CompatibilityResult(const RuntimeInstall &)>;
using InstallCancellationQuery = std::function<bool()>;

enum class InstallServiceError {
  kNone,
  kNotFound,
  kInvalidState,
  kIncompatible,
  kPersistence,
  kCanceled,
  kActivityActive,
  kActivityUnknown,
  kPackageUnavailable,
  kPackageFailed,
  kOwnershipConflict,
  kNativeUnavailable,
  kNativeCollision,
  kNativeIncoherent,
  kRegistrationPending
};

struct InstallServiceResult {
  bool success = false;
  InstallServiceError error_code = InstallServiceError::kNone;
  std::string message;
  static InstallServiceResult Success();
  static InstallServiceResult Failure(InstallServiceError error,
                                      std::string message);
  bool ok() const { return success; }
  InstallServiceError error() const { return error_code; }
};

// A complete publication driver owns durable control/publication/registration
// journals and readiness. The service must never overwrite its latest OWNER
// snapshot with the pre-callback preparation record. Captured collaborators must
// outlive the service. Empty retains legacy adapter compatibility; managed
// journals always require an explicit complete driver.
using InstallPublicationDriver = std::function<PackageActionResult(const RuntimeInstall&)>;

class RuntimeInstallService {
public:
  RuntimeInstallService(InstallRecordStore &records, PackageInstaller &package,
                        NativeCoordinator &native,
                        CompatibilityQuery compatibility,
                        InstallCancellationQuery canceled,
                        InstallPublicationDriver publication = {});
  InstallServiceResult Prepare(const std::string &install_id);
  InstallServiceResult Retry(const std::string &install_id);
  InstallServiceResult Step(const std::string &install_id);
  std::vector<RuntimeInstall> Snapshot() const;

private:
  InstallServiceResult Persist(RuntimeInstall record);
  InstallServiceResult Fail(RuntimeInstall record, InstallServiceError error,
                            std::string code, std::string message);
  InstallServiceResult Advance(RuntimeInstall record, PreparationState state,
                               const std::string &step);
  InstallServiceResult HandlePackageAction(RuntimeInstall record,
                                           const std::string &next_step,
                                           PackageActionResult result);
  InstallServiceResult StepPublication(const RuntimeInstall& record);
  InstallServiceResult StepNative(RuntimeInstall record,
                                  const PackageStatus &package_status);

  InstallRecordStore &records_;
  PackageInstaller &package_;
  NativeCoordinator &native_;
  CompatibilityQuery compatibility_;
  InstallCancellationQuery canceled_;
  InstallPublicationDriver publication_;
};

} // namespace playden::runtime
