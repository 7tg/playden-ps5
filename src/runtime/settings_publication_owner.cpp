#include "runtime/settings_publication_owner.h"
namespace playden::runtime {
namespace {
PackageActionResult Failure(const char* code, const char* message) {
  return {PackageActionState::kFailed, code, message};
}
bool SameOwner(const RuntimeInstall& a, const RuntimeInstall& b) {
  if (!ValidateRuntimeInstallRecord(a) || !ValidateRuntimeInstallRecord(b) ||
      a.schema_version != b.schema_version || a.install_id != b.install_id || a.title_id != b.title_id ||
      a.app_id != b.app_id || a.data_root != b.data_root || a.default_profile_id != b.default_profile_id ||
      a.runtime_bundle_id != b.runtime_bundle_id || a.recipe_id != b.recipe_id || a.recipe_version != b.recipe_version ||
      a.exit_policy != b.exit_policy || a.launch_entry_id != b.launch_entry_id || a.launch_path != b.launch_path ||
      a.launch_arguments != b.launch_arguments || a.launch_working_directory != b.launch_working_directory ||
      a.install_path != b.install_path || a.volume_id != b.volume_id || a.artifacts.size() != b.artifacts.size()) return false;
  for (std::size_t i = 0; i < a.artifacts.size(); ++i)
    if (a.artifacts[i].artifact_id != b.artifacts[i].artifact_id || a.artifacts[i].identity != b.artifacts[i].identity ||
        a.artifacts[i].sha256 != b.artifacts[i].sha256) return false;
  return true;
}
bool OwnedPackage(const PackageStatus& s) {
  // Complete publication is checked separately by the coordinator's shared
  // semantic verifier; the legacy profile-only status cannot authorize Ready.
  return s.known && s.title_staged && s.manifest_matches && s.template_owned &&
      s.mutable_bottle_present && s.mutable_bottle_owned && s.artifacts_valid;
}
bool MatchingJournal(const RuntimeInstall& r, const SettingsPublicationOperation& o) {
  return r.journal && r.journal->operation == "settings-publication" && r.journal->operation_id == o.transaction_id;
}
}
SettingsPublicationOwner::SettingsPublicationOwner(InstallRecordStore& records, PackageInstaller& package,
    NativeCoordinator& native) : records_(records), package_(package), native_(native) {}
PackageActionResult SettingsPublicationOwner::Persist(const RuntimeInstall& record) {
  auto result = records_.Update(record);
  if (!result.ok) return {PackageActionState::kFailed, result.code.empty() ? "publication-owner-persistence" : result.code,
      result.message.empty() ? "OWNER persistence failed; recover from durable state." : result.message};
  auto observed = records_.Find(record.install_id);
  if (!observed || !SameOwner(*observed, record) || observed->state != record.state ||
      observed->settings_revision != record.settings_revision || observed->last_failure.has_value() != record.last_failure.has_value() ||
      (record.last_failure && (observed->last_failure->code != record.last_failure->code ||
      observed->last_failure->message != record.last_failure->message)) || observed->journal.has_value() != record.journal.has_value() ||
      (record.journal && (observed->journal->operation != record.journal->operation ||
      observed->journal->operation_id != record.journal->operation_id || observed->journal->step != record.journal->step)))
    return Failure("publication-owner-readback", "OWNER persistence readback disagrees with the intended state.");
  return {PackageActionState::kComplete, {}, {}};
}
PackageActionResult SettingsPublicationOwner::Block(const SettingsPublicationOperation& o) {
  std::string reason;
  if (!EncodeSettingsPublicationOperation(o, &reason)) return Failure("publication-invalid", "Frozen OWNER operation is invalid.");
  auto record = records_.Find(o.install.install_id);
  if (!record || !SameOwner(*record, o.install) || record->state == PreparationState::kRemoving ||
      (record->settings_revision != o.previous_revision && record->settings_revision != o.plan.settings_revision))
    return Failure("publication-owner-conflict", "Reserved OWNER identity/revision disagrees with the frozen operation.");
  if (record->journal && record->journal->operation == "settings-publication" && !MatchingJournal(*record, o))
    return Failure("publication-owner-conflict", "A different OWNER publication operation must be reconciled first.");
  if (native_.Activity(record->title_id) != NativeActivity::kStopped)
    return Failure("publication-not-stopped", "OWNER publication requires positively stopped native activity.");
  if (!OwnedPackage(package_.Inspect(o.install)))
    return Failure("publication-owner-package", "Immutable title, bottle provenance and artifacts do not prove ownership.");
  const bool armed = MatchingJournal(*record, o) &&
      (record->journal->step == "register" || record->journal->step == "await-native");
  record->state = armed ? PreparationState::kRegistering : PreparationState::kPreparing;
  if (!armed) record->journal = PreparationJournal{o.transaction_id, "settings-publication", "publish-settings"};
  record->last_failure.reset();
  return Persist(*record);
}
PackageActionResult SettingsPublicationOwner::Reconcile(const SettingsPublicationOperation& o) {
  auto record = records_.Find(o.install.install_id);
  if (!record || !SameOwner(*record, o.install) || !MatchingJournal(*record, o) ||
      (record->settings_revision != o.previous_revision && record->settings_revision != o.plan.settings_revision))
    return Failure("publication-owner-conflict", "Committed operation has no matching blocked OWNER record.");
  if (!OwnedPackage(package_.Inspect(o.install)))
    return Failure("publication-owner-package", "Committed OWNER package provenance or artifacts changed.");
  if (native_.Activity(record->title_id) != NativeActivity::kStopped)
    return Failure("publication-not-stopped", "Native activity changed before committed OWNER reconciliation.");
  auto status = native_.Query(o.install);
  auto validate = [&](const NativeStatus& s) {
    return s.available && !s.collision && s.source_available && s.owned_manifest_matches;
  };
  if (!validate(status)) return Failure("publication-native-incoherent", "Native inventory/source ownership is unavailable or incoherent.");
  if (!status.installed) {
    if (!o.previous_receipt_bytes.empty())
      return Failure("publication-native-incoherent", "Settings updates require the existing owned native registration.");
    const bool armed = record->journal->step == "register" || record->journal->step == "await-native";
    if (status.scan_pending || armed) {
      if (!armed) {
        record->state = PreparationState::kRegistering; record->journal->step = "await-native";
        auto persisted = Persist(*record); if (persisted.state != PackageActionState::kComplete) return persisted;
      }
      return Failure("publication-registration-pending", "Native registration is pending or uncertain; query before any repeat submission.");
    }
    record->state = PreparationState::kRegistering; record->journal->step = "register";
    auto persisted = Persist(*record); if (persisted.state != PackageActionState::kComplete) return persisted;
    if (native_.Activity(record->title_id) != NativeActivity::kStopped)
      return Failure("publication-not-stopped", "Native activity changed after registration was durably armed.");
    auto submitted = native_.Register(o.install);
    if (submitted.state == RegistrationResultState::kFailed || submitted.state == RegistrationResultState::kStaleHook)
      return {PackageActionState::kFailed, submitted.code.empty() ? "publication-registration-failed" : submitted.code,
          submitted.message.empty() ? "Native registration submission failed; its durable arm is retained." : submitted.message};
    record->journal->step = "await-native";
    persisted = Persist(*record); if (persisted.state != PackageActionState::kComplete) return persisted;
    status = native_.Query(o.install);
    if (!validate(status)) return Failure("publication-native-incoherent", "Submitted registration has no coherent owned source/inventory.");
    if (!status.installed) return Failure("publication-registration-pending", "Native registration completion is not yet visible.");
  }
  if (native_.Activity(record->title_id) != NativeActivity::kStopped)
    return Failure("publication-not-stopped", "Native activity changed before Ready persistence.");
  record->state = PreparationState::kReady; record->settings_revision = o.plan.settings_revision;
  record->journal.reset(); record->last_failure.reset();
  return Persist(*record);
}
PublicationDependencies SettingsPublicationOwner::Dependencies(const std::filesystem::path& prefix) {
  PublicationDependencies deps; deps.host_namespace_prefix = prefix;
  deps.activity = [this](const RuntimeInstall& r) { return native_.Activity(r.title_id); };
  deps.block_owner = [this](const auto& o) { return Block(o); };
  deps.preflight = [prefix](const auto& o, const auto& paths, int fd) { return PreflightSettingsPublication(o, paths, fd, prefix); };
  deps.apply_files = [prefix](const auto& o, const auto& paths, int fd) { return ApplyPendingPublicationFiles(o, paths, fd, prefix); };
  deps.verify_files = [prefix](const auto& o, const auto& paths, int fd, bool committed) {
    return VerifySettingsPublicationFiles(o, paths, fd, committed, prefix);
  };
  deps.reconcile_committed = [this](const auto& o) { return Reconcile(o); };
  return deps;
}
}  // namespace playden::runtime
