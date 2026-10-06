#include "runtime/settings_publication.h"
#include "runtime/publication_contract.h"
#include "runtime/artifact_hash.h"
extern "C" {
#include "pw_title_publication_native.h"
}
#include "platform/managed_file_lock.h"
#include <cerrno>
#include <unistd.h>

namespace playden::runtime {
namespace {
using platform::ManagedDirectory;
using platform::DirectoryPolicy;
PackageActionResult Failure(const char* code, const char* message) {
  return {PackageActionState::kFailed, code, message};
}
bool Complete(const PackageActionResult& result) { return result.state == PackageActionState::kComplete; }
std::filesystem::path Map(const std::filesystem::path& prefix, const std::string& canonical) {
  return prefix.empty() ? std::filesystem::path(canonical) : prefix / std::filesystem::path(canonical).relative_path();
}
bool PathsMatch(const SettingsPublicationOperation& o, const PublicationPaths& p, const std::filesystem::path& prefix) {
  if (!prefix.empty() && (!prefix.is_absolute() || prefix != prefix.lexically_normal())) return false;
  const auto profiles = o.install.data_root + "/library/profiles/";
  return p.profile == Map(prefix, profiles + o.plan.profile_id + ".profile") &&
      p.catalog == Map(prefix, profiles + "profiles.lst") &&
      p.input_directory == Map(prefix, o.install.data_root + "/library/input") &&
      p.descriptor == Map(prefix, "/data/homebrew/" + o.install.title_id + "/game-launch.json") &&
      p.metadata == Map(prefix, profiles + ".playden-publication");
}
struct ReceiptRead {
  bool ok = false;
  bool absent = false;
  std::string bytes;
};
ReceiptRead ReadReceipt(const std::filesystem::path& control, const ManagedDirectory& directory) {
  constexpr const char* name = "publication-state.v1";
  struct stat before{}, after{};
  if (!directory.InspectChild(name, &before)) {
    const bool absent = errno == ENOENT && directory.Sync();
    return {absent, absent, {}};
  }
  if (!S_ISREG(before.st_mode) || before.st_uid != geteuid() || before.st_nlink != 1 ||
      (before.st_mode & 07777) != 0600 || before.st_size <= 0 || before.st_size > 8192) return {};
  Storage storage(control.string()); auto read = storage.ReadDurable(name, 8192);
  if (!read.ok() || !directory.InspectChild(name, &after) || before.st_dev != after.st_dev ||
      before.st_ino != after.st_ino || before.st_uid != after.st_uid || before.st_mode != after.st_mode ||
      before.st_nlink != after.st_nlink || before.st_size != after.st_size) return {};
  std::string bytes(read.value().begin(), read.value().end()); PwTitlePublication parsed{};
  if (pw_title_publication_parse(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &parsed) != PW_OK) return {};
  return {true, false, std::move(bytes)};
}
}
PackageActionResult PublishSettingsOperation(const SettingsPublicationOperation& o,
    const PublicationPaths& paths, PublicationDependencies& deps) {
  std::string reason; auto encoded = EncodeSettingsPublicationOperation(o, &reason);
  if (!encoded || !PathsMatch(o, paths, deps.host_namespace_prefix))
    return Failure("publication-invalid", "Frozen operation or canonical publication paths are invalid.");
  if (!deps.activity || !deps.block_owner || !deps.preflight || !deps.apply_files ||
      !deps.verify_files || !deps.reconcile_committed)
    return Failure("publication-unavailable", "Required publication adapters are unavailable.");
  if (deps.activity(o.install) != NativeActivity::kStopped)
    return Failure("publication-not-stopped", "The title is not confirmed stopped.");
  const auto control = Map(deps.host_namespace_prefix, o.install.data_root + "/.playden-control");
  auto directory = ManagedDirectory::Open(control, DirectoryPolicy::kPrivate);
  if (!directory) return Failure("publication-control", "Private publication control directory is unavailable.");
  auto lease = platform::ManagedFileLock::Acquire(*directory, "publication.lock", platform::LockMode::kPublisher);
  if (!lease) return Failure(errno == EWOULDBLOCK || errno == EAGAIN ? "publication-busy" : "publication-lock",
                             "Publisher lease could not be acquired.");
  auto held = [&] { return lease->Validate(*directory, "publication.lock"); };
  auto stopped = [&] { return held() && deps.activity(o.install) == NativeActivity::kStopped; };
  auto checkpoint = [&](PublicationStep step) { return (!deps.after_step || deps.after_step(step)) && held(); };
  if (!stopped()) return Failure("publication-not-stopped", "Native activity changed before publisher effects.");
  auto result = deps.block_owner(o);
  if (!Complete(result)) return result;
  if (!checkpoint(PublicationStep::kOwnerBlocked))
    return Failure("publication-interrupted", "OWNER persistence requires fresh recovery.");
  auto journal = LoadSettingsPublicationOperation(control);
  const bool fresh = journal.state == PublicationRecoveryState::kAbsent;
  if (!fresh) {
    if (journal.state != PublicationRecoveryState::kValid || !journal.operation)
      return Failure("publication-journal", "Operation recovery is corrupt or unavailable.");
    auto recovered = EncodeSettingsPublicationOperation(*journal.operation, &reason);
    if (!recovered || *recovered != *encoded)
      return Failure("publication-drift", "A different frozen operation must be reconciled first.");
  }
  auto receipt = ReadReceipt(control, *directory);
  if (!receipt.ok) return Failure("publication-receipt", "Publication receipt is corrupt or unavailable.");
  bool committed = !receipt.absent && receipt.bytes == o.committed_receipt_bytes;
  bool pending = !receipt.absent && receipt.bytes == o.pending_receipt_bytes;
  const bool before = o.previous_receipt_bytes.empty() ? receipt.absent :
      !receipt.absent && receipt.bytes == o.previous_receipt_bytes;
  if ((!pending && !committed && !before) || ((pending || committed) && fresh))
    return Failure("publication-collision", "Receipt does not prove this operation's recovery ownership.");
  if (!pending && !committed) {
    result = deps.preflight(o, paths, lease->fd());
    if (!Complete(result)) return result;
    if (!stopped()) return Failure("publication-not-stopped", "Preflight or native activity changed.");
  }
  if (fresh) {
    auto created = CreateSettingsPublicationOperation(control, o);
    if (!created.ok()) return Failure("publication-journal", "Operation persistence failed; reload before effects.");
  }
  if (!checkpoint(PublicationStep::kJournalDurable))
    return Failure("publication-interrupted", "Operation persistence requires fresh recovery.");
  auto write_receipt = [&](const std::string& bytes) {
    if (!stopped()) return false;
    Storage storage(control.string()); Bytes data(bytes.begin(), bytes.end());
    if (!storage.WriteDurable("publication-state.v1", data).ok() || !held()) return false;
    auto observed = ReadReceipt(control, *directory);
    return observed.ok && !observed.absent && observed.bytes == bytes;
  };
  if (!committed) {
    if (!write_receipt(o.pending_receipt_bytes))
      return Failure("publication-pending", "Pending receipt persistence failed; reload before effects.");
    if (!checkpoint(PublicationStep::kPendingDurable))
      return Failure("publication-interrupted", "Pending publication requires fresh recovery.");
    if (!stopped()) return Failure("publication-not-stopped", "Native activity changed before file edits.");
    result = deps.apply_files(o, paths, lease->fd());
    if (!Complete(result)) return result;
    if (!checkpoint(PublicationStep::kFilesApplied))
      return Failure("publication-interrupted", "Publication file effects require fresh recovery.");
    result = deps.verify_files(o, paths, lease->fd(), false);
    if (!Complete(result)) return result;
    auto still_pending = ReadReceipt(control, *directory);
    if (!still_pending.ok || still_pending.bytes != o.pending_receipt_bytes || !stopped())
      return Failure("publication-changed", "Publication receipt, lease or native activity changed during verification.");
    if (!checkpoint(PublicationStep::kPendingVerified))
      return Failure("publication-interrupted", "Pending verification requires fresh recovery.");
    if (!write_receipt(o.committed_receipt_bytes))
      return Failure("publication-commit", "Committed receipt persistence failed; reload before reconciliation.");
    if (!checkpoint(PublicationStep::kCommitDurable))
      return Failure("publication-interrupted", "Commit persistence requires fresh recovery.");
  }
  result = deps.verify_files(o, paths, lease->fd(), true);
  if (!Complete(result)) return result;
  auto observed = ReadReceipt(control, *directory);
  if (!observed.ok || observed.bytes != o.committed_receipt_bytes || !stopped())
    return Failure("publication-changed", "Committed publication, lease or native activity changed.");
  if (!checkpoint(PublicationStep::kCommitVerified))
    return Failure("publication-interrupted", "Committed verification requires fresh OWNER reconciliation.");
  return deps.reconcile_committed(o);
}
PackageActionResult VerifySettingsPublicationFiles(const SettingsPublicationOperation& o,
    const PublicationPaths& paths, int borrowed, bool committed, const std::filesystem::path& prefix) {
  std::string reason;
  if (!EncodeSettingsPublicationOperation(o, &reason) || !PathsMatch(o, paths, prefix))
    return Failure("publication-invalid", "Frozen verification operation or canonical paths are invalid.");
  PwTitlePublicationDescriptor descriptor{}; char error[256]{};
  if (pw_title_publication_descriptor_parse(reinterpret_cast<const uint8_t*>(o.descriptor_bytes.data()),
        o.descriptor_bytes.size(), o.install.title_id.c_str(), &descriptor, error, sizeof(error)) != PW_OK)
    return Failure("publication-descriptor", "Frozen descriptor is invalid.");
  const auto digest = HashArtifactBytes(o.metadata_bytes);
  if (!digest) return Failure("publication-hash", "Metadata hashing is unavailable.");
  const auto host = prefix.string(); PwTitlePublicationPlatform platform{};
  platform.host_root = host.empty() ? nullptr : host.c_str(); platform.owner_uid = geteuid();
  const auto& receipt = committed ? o.committed_receipt_bytes : o.pending_receipt_bytes;
  PwTitlePublicationContents contents{};
  if (pw_title_publication_verify_locked(&descriptor, &platform, borrowed,
        reinterpret_cast<const uint8_t*>(receipt.data()), receipt.size(), digest->c_str(), &contents, error, sizeof(error)) != PW_OK)
    return {PackageActionState::kFailed, "publication-verification", error};
  return {PackageActionState::kComplete, {}, {}};
}
}  // namespace playden::runtime
