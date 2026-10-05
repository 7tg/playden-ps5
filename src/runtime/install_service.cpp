#include "runtime/install_service.h"

#include <utility>

namespace playden::runtime {
namespace {

bool IsNativeStep(const std::string &step) {
  return step == "query-native" || step == "register" || step == "await-native";
}

bool PackageReady(const PackageStatus &status, const RuntimeInstall &record) {
  return status.known && status.title_staged && status.manifest_matches &&
         status.template_owned && status.mutable_bottle_present &&
         status.mutable_bottle_owned && status.artifacts_valid &&
         status.profile_published &&
         status.profile_settings_revision == record.settings_revision;
}

std::string ErrorCode(const std::string &code, const char *fallback) {
  return code.empty() ? fallback : code;
}

std::string ErrorMessage(const std::string &message, const char *fallback) {
  return message.empty() ? fallback : message;
}

} // namespace

RepositoryInstallRecordStore::RepositoryInstallRecordStore(
    RuntimeInstallRepository &repository)
    : repository_(repository) {}

std::optional<RuntimeInstall>
RepositoryInstallRecordStore::Find(const std::string &install_id) const {
  return repository_.Find(install_id);
}

std::vector<RuntimeInstall> RepositoryInstallRecordStore::Snapshot() const {
  return repository_.Snapshot();
}

RecordUpdateResult
RepositoryInstallRecordStore::Update(const RuntimeInstall &record) {
  const auto result = repository_.Update(record);
  return result.ok()
             ? RecordUpdateResult{true, {}, {}}
             : RecordUpdateResult{false, "repository-write", result.message()};
}

InstallServiceResult InstallServiceResult::Success() {
  return {true, InstallServiceError::kNone, {}};
}

InstallServiceResult InstallServiceResult::Failure(InstallServiceError error,
                                                   std::string message) {
  return {false, error, std::move(message)};
}

RuntimeInstallService::RuntimeInstallService(InstallRecordStore &records,
                                             PackageInstaller &package,
                                             NativeCoordinator &native,
                                             CompatibilityQuery compatibility,
                                             InstallCancellationQuery canceled)
    : records_(records), package_(package), native_(native),
      compatibility_(std::move(compatibility)), canceled_(std::move(canceled)) {
}

InstallServiceResult RuntimeInstallService::Persist(RuntimeInstall record) {
  const auto result = records_.Update(record);
  if (!result.ok) {
    return InstallServiceResult::Failure(
        InstallServiceError::kPersistence,
        result.message.empty() ? "runtime install state could not be persisted"
                               : result.message);
  }
  return InstallServiceResult::Success();
}

InstallServiceResult RuntimeInstallService::Fail(RuntimeInstall record,
                                                 InstallServiceError error,
                                                 std::string code,
                                                 std::string message) {
  record.state = PreparationState::kFailed;
  record.last_failure = RuntimeFailure{std::move(code), message};
  const auto persisted = Persist(std::move(record));
  return persisted.ok()
             ? InstallServiceResult::Failure(error, std::move(message))
             : persisted;
}

InstallServiceResult RuntimeInstallService::Advance(RuntimeInstall record,
                                                    PreparationState state,
                                                    const std::string &step) {
  record.state = state;
  record.last_failure.reset();
  if (step.empty()) {
    record.journal.reset();
  } else if (record.journal) {
    record.journal->step = step;
  } else {
    record.journal =
        PreparationJournal{"prepare-" + record.install_id, "prepare", step};
  }
  return Persist(std::move(record));
}

InstallServiceResult
RuntimeInstallService::Prepare(const std::string &install_id) {
  auto record = records_.Find(install_id);
  if (!record) {
    return InstallServiceResult::Failure(InstallServiceError::kNotFound,
                                         "runtime install was not found");
  }
  if (record->state != PreparationState::kDownloaded) {
    return InstallServiceResult::Failure(
        InstallServiceError::kInvalidState,
        "only a downloaded runtime install can begin preparation");
  }
  const auto compatibility =
      compatibility_ ? compatibility_(*record) : CompatibilityResult{};
  if (!compatibility.compatible) {
    record->last_failure =
        RuntimeFailure{"recipe-unavailable",
                       compatibility.reason.empty()
                           ? "compatible preparation recipe is unavailable"
                           : compatibility.reason};
    record->journal.reset();
    const auto persisted = Persist(*record);
    return persisted.ok() ? InstallServiceResult::Failure(
                                InstallServiceError::kIncompatible,
                                record->last_failure->message)
                          : persisted;
  }
  record->journal = PreparationJournal{"prepare-" + record->install_id,
                                       "prepare", "stage-title"};
  return Advance(*record, PreparationState::kPreparing, "stage-title");
}

InstallServiceResult
RuntimeInstallService::Retry(const std::string &install_id) {
  auto record = records_.Find(install_id);
  if (!record) {
    return InstallServiceResult::Failure(InstallServiceError::kNotFound,
                                         "runtime install was not found");
  }
  if (record->state != PreparationState::kFailed &&
      record->state != PreparationState::kUnavailable) {
    return InstallServiceResult::Failure(InstallServiceError::kInvalidState,
                                         "runtime install is not retryable");
  }
  const auto compatibility =
      compatibility_ ? compatibility_(*record) : CompatibilityResult{};
  if (!compatibility.compatible) {
    record->state = PreparationState::kDownloaded;
    record->journal.reset();
    record->last_failure =
        RuntimeFailure{"recipe-unavailable",
                       compatibility.reason.empty()
                           ? "compatible preparation recipe is unavailable"
                           : compatibility.reason};
    const auto persisted = Persist(*record);
    return persisted.ok() ? InstallServiceResult::Failure(
                                InstallServiceError::kIncompatible,
                                record->last_failure->message)
                          : persisted;
  }
  if (!record->journal) {
    record->journal = PreparationJournal{"prepare-" + record->install_id,
                                         "prepare", "stage-title"};
  }
  if (record->journal->step == "register" ||
      record->journal->step == "await-native") {
    record->journal->step = "query-native";
  }
  const auto state = IsNativeStep(record->journal->step)
                         ? PreparationState::kRegistering
                         : PreparationState::kPreparing;
  return Advance(*record, state, record->journal->step);
}

InstallServiceResult
RuntimeInstallService::HandlePackageAction(RuntimeInstall record,
                                           const std::string &next_step,
                                           PackageActionResult result) {
  if (result.state == PackageActionState::kCanceled) {
    return Fail(std::move(record), InstallServiceError::kCanceled,
                ErrorCode(result.code, "canceled"),
                ErrorMessage(result.message, "preparation was canceled"));
  }
  if (result.state == PackageActionState::kFailed) {
    return Fail(std::move(record), InstallServiceError::kPackageFailed,
                ErrorCode(result.code, "package-step-failed"),
                ErrorMessage(result.message, "package preparation failed"));
  }
  const auto state = next_step == "query-native"
                         ? PreparationState::kRegistering
                         : PreparationState::kPreparing;
  return Advance(std::move(record), state, next_step);
}

InstallServiceResult
RuntimeInstallService::StepNative(RuntimeInstall record,
                                  const PackageStatus &package_status) {
  if (!PackageReady(package_status, record)) {
    return Fail(
        std::move(record), InstallServiceError::kOwnershipConflict,
        "package-incoherent",
        "staged package, owned bottle, artifacts and profile are not coherent");
  }
  const std::string journal_step = record.journal ? record.journal->step : "";
  const auto native = native_.Query(record);
  if (!native.available) {
    return Fail(std::move(record), InstallServiceError::kNativeUnavailable,
                "native-unavailable",
                "native registration inventory is unavailable");
  }
  if (native.collision) {
    return Fail(std::move(record), InstallServiceError::kNativeCollision,
                "native-collision",
                "native title does not match Playden ownership");
  }
  if (native.installed && native.source_available &&
      native.owned_manifest_matches) {
    record.state = PreparationState::kReady;
    record.journal.reset();
    record.last_failure.reset();
    return Persist(std::move(record));
  }
  if (native.scan_pending) {
    if (journal_step == "await-native")
      return InstallServiceResult::Success();
    return Advance(std::move(record), PreparationState::kRegistering,
                   "await-native");
  }
  if ((native.installed || native.source_available) &&
      !native.owned_manifest_matches) {
    return Fail(std::move(record), InstallServiceError::kNativeCollision,
                "native-collision",
                "native title does not match Playden ownership");
  }
  if (native.installed && !native.source_available) {
    return Fail(std::move(record), InstallServiceError::kNativeIncoherent,
                "installed-source-missing",
                "installed native title has no matching owned source");
  }
  if (!native.source_available) {
    return Fail(std::move(record), InstallServiceError::kNativeIncoherent,
                "native-source-missing",
                "staged owned native source is not visible to registration");
  }
  if (journal_step == "register" || journal_step == "await-native") {
    return Fail(std::move(record), InstallServiceError::kRegistrationPending,
                "registration-uncertain",
                "registration completion is not visible; retry explicitly "
                "after checking the service");
  }

  record.state = PreparationState::kRegistering;
  record.journal->step = "register";
  record.last_failure.reset();
  const auto armed = Persist(record);
  if (!armed.ok())
    return armed;

  const auto submitted = native_.Register(record);
  if (submitted.state == RegistrationResultState::kStaleHook) {
    return Fail(
        std::move(record), InstallServiceError::kNativeUnavailable,
        ErrorCode(submitted.code, "stale-hook"),
        ErrorMessage(submitted.message,
                     "native registration hook is stale; restart it manually"));
  }
  if (submitted.state == RegistrationResultState::kFailed) {
    return Fail(std::move(record), InstallServiceError::kNativeUnavailable,
                ErrorCode(submitted.code, "registration-failed"),
                ErrorMessage(submitted.message, "native registration failed"));
  }
  return Advance(std::move(record), PreparationState::kRegistering,
                 "await-native");
}

InstallServiceResult
RuntimeInstallService::Step(const std::string &install_id) {
  auto record = records_.Find(install_id);
  if (!record) {
    return InstallServiceResult::Failure(InstallServiceError::kNotFound,
                                         "runtime install was not found");
  }
  if ((record->state != PreparationState::kPreparing &&
       record->state != PreparationState::kRegistering) ||
      !record->journal) {
    return InstallServiceResult::Failure(
        InstallServiceError::kInvalidState,
        "runtime install has no active preparation journal");
  }
  if (canceled_ && canceled_()) {
    return Fail(*record, InstallServiceError::kCanceled, "canceled",
                "preparation was canceled");
  }
  const auto activity = native_.Activity(record->title_id);
  if (activity == NativeActivity::kUnknown) {
    return Fail(*record, InstallServiceError::kActivityUnknown,
                "title-activity-unknown",
                "native title activity is unknown; preparation is blocked");
  }
  if (activity == NativeActivity::kActive) {
    return Fail(*record, InstallServiceError::kActivityActive, "title-active",
                "native title is active; stop it before preparation");
  }

  const auto status = package_.Inspect(*record);
  if (!status.known) {
    return Fail(*record, InstallServiceError::kPackageUnavailable,
                "package-state-unknown", "package state is unavailable");
  }
  const auto &step = record->journal->step;
  if (IsNativeStep(step))
    return StepNative(*record, status);

  if (step == "stage-title") {
    if (status.title_staged) {
      if (!status.manifest_matches) {
        return Fail(*record, InstallServiceError::kOwnershipConflict,
                    "title-stage-collision",
                    "staged native title is not owned by this install");
      }
      return Advance(*record, PreparationState::kPreparing, "copy-template");
    }
    return HandlePackageAction(*record, "copy-template",
                               package_.StageTitle(*record));
  }
  if (step == "copy-template") {
    if (status.mutable_bottle_present) {
      if (!status.mutable_bottle_owned || !status.template_owned) {
        return Fail(
            *record, InstallServiceError::kOwnershipConflict,
            "bottle-ownership-conflict",
            "existing mutable bottle is not an owned validated template");
      }
      return Advance(*record, PreparationState::kPreparing,
                     "validate-artifacts");
    }
    return HandlePackageAction(*record, "validate-artifacts",
                               package_.CopyTemplate(*record));
  }
  if (step == "validate-artifacts") {
    if (status.artifacts_valid) {
      return Advance(*record, PreparationState::kPreparing, "publish-profile");
    }
    return HandlePackageAction(*record, "publish-profile",
                               package_.ValidateArtifacts(*record));
  }
  if (step == "publish-profile") {
    if (status.profile_published &&
        status.profile_settings_revision == record->settings_revision) {
      return Advance(*record, PreparationState::kRegistering, "query-native");
    }
    return HandlePackageAction(*record, "query-native",
                               package_.PublishProfile(*record));
  }
  return Fail(*record, InstallServiceError::kPackageFailed,
              "journal-step-invalid", "preparation journal step is invalid");
}

std::vector<RuntimeInstall> RuntimeInstallService::Snapshot() const {
  return records_.Snapshot();
}

} // namespace playden::runtime
