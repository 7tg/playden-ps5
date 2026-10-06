#include "runtime/settings_publication_owner.h"
#include "platform/managed_file_lock.h"
#include <cerrno>
#include <unistd.h>
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
PackageActionResult SettingsPublicationOwner::InitializeControl(const RuntimeInstall& record, const std::string& transaction,
    const std::filesystem::path& prefix, const std::function<bool(PublicationControlStep)>& after_step) {
  if (!IsSettingsPublicationTransactionId(transaction) ||
      (!prefix.empty() && (!prefix.is_absolute() || prefix != prefix.lexically_normal())))
    return Failure("publication-control-invalid", "Invalid control initialization identity/namespace.");
  auto current = records_.Find(record.install_id);
  if (!current || !SameOwner(*current, record) || current->settings_revision != record.settings_revision ||
      current->state == PreparationState::kRemoving || current->state == PreparationState::kReady ||
      current->state == PreparationState::kRegistering || native_.Activity(record.title_id) != NativeActivity::kStopped ||
      !OwnedPackage(package_.Inspect(record)))
    return Failure("publication-control-owner", "Initial control setup requires reserved stopped OWNER/package provenance.");
  const bool recovering = current->journal && current->journal->operation == "settings-control";
  if (recovering && (current->journal->operation_id != transaction || current->state != PreparationState::kPreparing ||
      (current->journal->step != "initialize-control" && current->journal->step != "control-ready")))
    return Failure("publication-control-conflict", "A different control initialization intent must be reconciled first.");
  if (!recovering && current->journal && current->journal->operation != "prepare")
    return Failure("publication-control-conflict", "An existing OWNER operation cannot be replaced by control initialization.");
  const auto canonical = std::filesystem::path(record.data_root);
  const auto data_path = prefix.empty() ? canonical : prefix / canonical.relative_path();
  auto data = platform::ManagedDirectory::Open(data_path, platform::DirectoryPolicy::kPrivate);
  if (!data) return Failure("publication-control-data", "Existing owned private bottle data root is unavailable.");
  struct stat info{};
  const bool exists = data->InspectChild(".playden-control", &info);
  if (!exists && errno != ENOENT)
    return Failure("publication-control-collision", "Control path is foreign or unavailable.");
  if (!recovering) {
    if (exists || !data->Sync())
      return Failure("publication-control-collision", "Control initialization cannot adopt a preexisting path.");
    // The reserved OWNER intent is durable before mkdir or exclusive lock
    // creation. ManagedDirectory requires coordinated namespace writers; this
    // journal is not a boundary against hostile concurrent ancestor replacement.
    current->state = PreparationState::kPreparing;
    current->journal = PreparationJournal{transaction, "settings-control", "initialize-control"};
    current->last_failure.reset();
    auto saved = Persist(*current); if (saved.state != PackageActionState::kComplete) return saved;
  }
  const bool prepared = current->journal->step == "control-ready";
  auto checkpoint = [&](PublicationControlStep step) {
    return !after_step || after_step(step);
  };
  if (!checkpoint(PublicationControlStep::kOwnerArmed))
    return Failure("publication-control-interrupted", "Control setup interrupted after durable OWNER intent.");
  if (!data->Validate() || native_.Activity(record.title_id) != NativeActivity::kStopped ||
      !OwnedPackage(package_.Inspect(record)))
    return Failure("publication-control-owner", "Control ownership/activity changed after durable intent.");
  // Recheck absence after persistence; a prepared setup never recreates a
  // missing directory or lock, because replacement would change lease identity.
  const bool now_exists = data->InspectChild(".playden-control", &info);
  if ((!now_exists && errno != ENOENT) || (!now_exists && prepared) || (!recovering && now_exists))
    return Failure("publication-control-collision", "Control path changed after admission; retain the OWNER intent.");
  auto control = data->OpenChild(".playden-control", platform::DirectoryPolicy::kPrivate, !prepared);
  if (!control || !control->Sync() || !data->Sync())
    return Failure("publication-control-directory", "Private control creation/sync failed; recover from durable OWNER intent.");
  if (!checkpoint(PublicationControlStep::kDirectorySynced))
    return Failure("publication-control-interrupted", "Control setup interrupted after directory sync.");
  if (!control->Validate() || native_.Activity(record.title_id) != NativeActivity::kStopped ||
      !OwnedPackage(package_.Inspect(record)))
    return Failure("publication-control-owner", "Control ownership/activity changed before lock creation.");
  const bool lock_exists = control->InspectChild("publication.lock", &info);
  if (!lock_exists && (errno != ENOENT || prepared))
    return Failure("publication-control-lock", "Stable publication lock is missing or foreign.");
  int fd = lock_exists ? control->OpenRegularFile("publication.lock", true, 0600) :
      control->CreateExclusiveFile("publication.lock", 0600);
  if (fd < 0) return Failure("publication-control-lock", "Stable publication lock creation/admission failed.");
  const bool lock_valid = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == geteuid() &&
      (info.st_mode & 07777) == 0600 && info.st_nlink == 1 && info.st_size == 0 && fsync(fd) == 0;
  const bool closed = close(fd) == 0;
  if (!lock_valid || !closed || !control->Sync())
    return Failure("publication-control-lock", "Stable empty private lock failed durable validation; preserve it.");
  auto lease = platform::ManagedFileLock::Acquire(*control, "publication.lock", platform::LockMode::kPublisher);
  if (!lease || !checkpoint(PublicationControlStep::kLockSynced))
    return Failure("publication-control-lock", "Control setup lock is busy or interrupted after sync.");
  auto observed = records_.Find(record.install_id);
  if (!observed || !SameOwner(*observed, *current) || observed->settings_revision != current->settings_revision ||
      observed->state != PreparationState::kPreparing || !observed->journal ||
      observed->journal->operation != "settings-control" || observed->journal->operation_id != transaction ||
      observed->journal->step != current->journal->step || !lease->Validate(*control, "publication.lock") ||
      native_.Activity(record.title_id) != NativeActivity::kStopped || !OwnedPackage(package_.Inspect(record)))
    return Failure("publication-control-owner", "OWNER/control/activity changed before setup completion.");
  if (!prepared) {
    current->journal->step = "control-ready";
    auto saved = Persist(*current); if (saved.state != PackageActionState::kComplete) return saved;
  }
  if (!checkpoint(PublicationControlStep::kOwnerPrepared))
    return Failure("publication-control-interrupted", "Control setup interrupted after durable completion intent.");
  return {PackageActionState::kComplete, {}, {}};
}
Result<SettingsPublicationOperation> SettingsPublicationOwner::Prepare(const RuntimeInstall& record,
    const SettingsLaunchPlan& plan, const std::string& kit, const std::string& transaction, std::int64_t time,
    const std::filesystem::path& prefix) {
  auto fail = [](const std::string& reason) { return Result<SettingsPublicationOperation>::Failure(StorageError::kInvalidData, reason); };
  if (!prefix.empty() && (!prefix.is_absolute() || prefix != prefix.lexically_normal())) return fail("Invalid canonical namespace.");
  auto current = records_.Find(record.install_id);
  if (!current || !SameOwner(*current, record) || current->state == PreparationState::kRemoving ||
      native_.Activity(record.title_id) != NativeActivity::kStopped || !OwnedPackage(package_.Inspect(record)))
    return fail("Reserved stopped OWNER/package provenance does not authorize operation capture.");
  const auto canonical = std::filesystem::path(record.data_root + "/.playden-control");
  const auto control_path = prefix.empty() ? canonical : prefix / canonical.relative_path();
  // Initial callers need no FTP-created control paths. A matching setup intent
  // also revalidates the stable inode on interrupted preparation.
  struct stat control_info{};
  const bool control_missing = lstat(control_path.c_str(), &control_info) != 0 && errno == ENOENT;
  if (!control_missing && current->state != PreparationState::kReady &&
      (!current->journal || (current->journal->operation != "settings-control" &&
                            current->journal->operation != "settings-publication")))
    return fail("Initial preparation cannot adopt a preexisting control path without durable creation intent.");
  if (control_missing || (current->journal && current->journal->operation == "settings-control")) {
    auto initialized = InitializeControl(record, transaction, prefix);
    if (initialized.state != PackageActionState::kComplete) return fail(initialized.message);
    current = records_.Find(record.install_id);
    if (!current || !SameOwner(*current, record)) return fail("OWNER changed after control initialization.");
  }
  auto control = platform::ManagedDirectory::Open(control_path, platform::DirectoryPolicy::kPrivate);
  auto lease = control ? platform::ManagedFileLock::Acquire(*control, "publication.lock", platform::LockMode::kPublisher) : std::nullopt;
  if (!lease || native_.Activity(record.title_id) != NativeActivity::kStopped)
    return fail("Existing publication control/lease or stopped native activity is unavailable.");
  auto recovered = LoadSettingsPublicationOperation(control_path);
  if (recovered.state == PublicationRecoveryState::kCorrupt || recovered.state == PublicationRecoveryState::kUnavailable)
    return fail("Existing frozen operation is corrupt or unavailable.");
  auto intent = LoadPreparedSettingsPublicationOperation(control_path, transaction);
  if (intent.state == PublicationRecoveryState::kCorrupt || intent.state == PublicationRecoveryState::kUnavailable)
    return fail("Prepared intent is corrupt or unavailable; preserve its evidence.");
  if (intent.operation) {
    if (recovered.operation && recovered.operation->transaction_id == transaction) {
      std::string reason;
      if (EncodeSettingsPublicationOperation(*recovered.operation, &reason) != EncodeSettingsPublicationOperation(*intent.operation, &reason))
        return fail("Active journal and prepared intent disagree.");
    }
    if (current->journal && current->journal->operation == "settings-publication" && current->journal->operation_id != transaction)
      return fail("A different blocked OWNER operation must first be reconciled.");
    if (recovered.operation && recovered.operation->transaction_id != transaction &&
        (current->state != PreparationState::kReady || current->journal || current->last_failure))
      return fail("Previous active operation must first reconcile to durable Ready.");
    recovered = intent;
  }
  if (recovered.operation && recovered.operation->transaction_id == transaction) {
    const auto& frozen = *recovered.operation;
    if (!SameOwner(frozen.install, record) || record.settings_revision != frozen.plan.settings_revision)
      return fail("Recovery OWNER intent changed.");
    auto candidate = frozen; candidate.plan = plan; candidate.kit_id = kit; std::string reason;
    auto checked = FreezeSettingsPublicationOperation(candidate, &reason);
    if (!checked || checked->plan_sha256 != frozen.plan_sha256 || !lease->Validate(*control, "publication.lock") ||
        native_.Activity(record.title_id) != NativeActivity::kStopped)
      return fail("Recovery settings intent changed; retain the original frozen operation.");
    return Result<SettingsPublicationOperation>::Success(frozen);
  }
  if (current->journal && current->journal->operation == "settings-publication")
    return fail("Unreconciled OWNER intent requires explicit recovery before another operation capture.");
  if (recovered.operation && (current->state != PreparationState::kReady || current->journal || current->last_failure ||
      current->settings_revision != recovered.operation->plan.settings_revision))
    return fail("Previous frozen operation must first reconcile to durable Ready.");
  if (!lease->Validate(*control, "publication.lock") || native_.Activity(record.title_id) != NativeActivity::kStopped)
    return fail("Publisher ownership/activity changed before capture.");
  auto snapshot = *current; snapshot.settings_revision = record.settings_revision;
  auto captured = CaptureSettingsPublicationOperation(snapshot, plan, kit, transaction, time, lease->fd(), prefix);
  if (!captured.ok()) return captured;
  if (!lease->Validate(*control, "publication.lock") || native_.Activity(record.title_id) != NativeActivity::kStopped)
    return fail("Publisher ownership/activity changed before durable capture handoff.");
  auto saved = CreatePreparedSettingsPublicationOperation(control_path, captured.value());
  if (!saved.ok()) return fail("Durable prepared intent failed; reload before any publication effects.");
  if (!lease->Validate(*control, "publication.lock") || native_.Activity(record.title_id) != NativeActivity::kStopped)
    return fail("Publisher ownership/activity changed after durable capture handoff.");
  return captured;
}
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
PackageActionResult SettingsPublicationOwner::AuthorizeRetirement(const SettingsPublicationOperation& previous,
    const SettingsPublicationOperation& next) {
  auto record = records_.Find(previous.install.install_id);
  if (!record || !SameOwner(*record, previous.install) || !SameOwner(*record, next.install) ||
      record->state != PreparationState::kReady || record->journal || record->last_failure ||
      record->settings_revision != previous.plan.settings_revision ||
      native_.Activity(record->title_id) != NativeActivity::kStopped ||
      !OwnedPackage(package_.Inspect(previous.install)) || !OwnedPackage(package_.Inspect(next.install)))
    return Failure("publication-retirement-owner", "Previous operation lacks a coherent durable Ready OWNER record.");
  auto status = native_.Query(previous.install);
  if (!status.available || !status.installed || status.collision || !status.source_available || !status.owned_manifest_matches)
    return Failure("publication-retirement-owner", "Previous operation lacks the existing coherent native registration.");
  return {PackageActionState::kComplete, {}, {}};
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
  deps.authorize_retirement = [this](const auto& previous, const auto& next) { return AuthorizeRetirement(previous, next); };
  return deps;
}
}  // namespace playden::runtime
