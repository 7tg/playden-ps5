#include "runtime/settings_publication.h"
#include "runtime/publication_contract.h"
#include "runtime/artifact_hash.h"
extern "C" {
#include "pw_title_publication_native.h"
}
#include "platform/managed_file_lock.h"
#include <cerrno>
#include <atomic>
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
std::optional<FrozenFileSnapshot> ReadPublicationFile(const ManagedDirectory& directory,
    const std::string& name, std::size_t maximum) {
  struct stat before{}, opened{}, after{};
  if (!directory.InspectChild(name, &before)) {
    if (errno == ENOENT && directory.Sync()) return FrozenFileSnapshot{};
    return std::nullopt;
  }
  auto same = [](const struct stat& a, const struct stat& b) {
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_mode == b.st_mode &&
        a.st_uid == b.st_uid && a.st_nlink == b.st_nlink && a.st_size == b.st_size &&
        a.st_mtime == b.st_mtime && a.st_ctime == b.st_ctime;
  };
  if (!S_ISREG(before.st_mode) || before.st_uid != geteuid() || before.st_nlink != 1 ||
      before.st_size < 0 || static_cast<std::uint64_t>(before.st_size) > maximum ||
      ((before.st_mode & 07777) != 0600 && (before.st_mode & 07777) != 0644)) return std::nullopt;
  int fd = directory.OpenRegularFile(name, true, before.st_mode & 07777);
  if (fd < 0) return std::nullopt;
  bool valid = fstat(fd, &opened) == 0 && same(before, opened);
  std::string bytes; char buffer[4096];
  while (valid) {
    const auto count = read(fd, buffer, sizeof(buffer));
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 || static_cast<std::size_t>(count) > maximum - bytes.size()) { valid = false; break; }
    if (!count) break;
    bytes.append(buffer, count);
  }
  valid = valid && fstat(fd, &opened) == 0 && same(before, opened) &&
      directory.InspectChild(name, &after) && same(before, after) &&
      bytes.size() == static_cast<std::uint64_t>(before.st_size) && fsync(fd) == 0;
  const bool closed = close(fd) == 0;
  if (!valid || !closed || !directory.Sync()) return std::nullopt;
  return FrozenFileSnapshot{true, static_cast<unsigned>(before.st_mode & 07777), bytes.size(), {}, std::move(bytes)};
}
bool WritePublicationFile(const ManagedDirectory& directory, const std::string& name,
    const std::string& bytes, const FrozenFileSnapshot& before, const std::function<bool()>& held) {
  static std::atomic<unsigned long> sequence{0}; std::string temporary; int fd = -1;
  for (unsigned attempt = 0; attempt < 128; ++attempt) {
    temporary = "." + name + ".publication-" + std::to_string(getpid()) + "-" + std::to_string(sequence++);
    fd = directory.CreateExclusiveFile(temporary, 0600);
    if (fd >= 0 || errno != EEXIST) break;
  }
  if (fd < 0) return false;
  std::size_t offset = 0; bool valid = true;
  while (offset < bytes.size()) {
    const auto count = write(fd, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) { valid = false; break; }
    offset += static_cast<std::size_t>(count);
  }
  const bool synced = valid && fsync(fd) == 0; const bool closed = close(fd) == 0;
  if (!synced || !closed || !held()) return false;
  auto observed = ReadPublicationFile(directory, name, kMaxSettingsPublicationOperationBytes);
  if (!observed || observed->present != before.present ||
      (before.present && (observed->bytes != before.bytes || observed->mode != before.mode))) return false;
  return held() && directory.RenameChild(temporary, directory, name, before.present) && directory.Sync() && held();
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
PackageActionResult ApplyPendingPublicationRegistry(const SettingsPublicationOperation& o,
    int borrowed, const std::filesystem::path& prefix) {
  std::string reason;
  auto encoded = EncodeSettingsPublicationOperation(o, &reason);
  if (!encoded || (!prefix.empty() && (!prefix.is_absolute() || prefix != prefix.lexically_normal())))
    return Failure("publication-invalid", "Frozen registry operation or namespace is invalid.");
  const auto control = Map(prefix, o.install.data_root + "/.playden-control");
  auto directory = ManagedDirectory::Open(control, DirectoryPolicy::kPrivate);
  struct stat opened{}, named{};
  auto held = [&] {
    return directory && borrowed >= 0 && directory->Validate() &&
        fstat(borrowed, &opened) == 0 && directory->InspectChild("publication.lock", &named) &&
        S_ISREG(opened.st_mode) && S_ISREG(named.st_mode) && opened.st_uid == geteuid() &&
        named.st_uid == geteuid() && opened.st_nlink == 1 && named.st_nlink == 1 &&
        (opened.st_mode & 07777) == 0600 && (named.st_mode & 07777) == 0600 &&
        opened.st_dev == named.st_dev && opened.st_ino == named.st_ino;
  };
  if (!held()) return Failure("publication-lock", "Registry publisher descriptor is unavailable or changed.");
  auto journal = LoadSettingsPublicationOperation(control);
  auto recovered = journal.operation ? EncodeSettingsPublicationOperation(*journal.operation, &reason) : std::nullopt;
  auto receipt = ReadReceipt(control, *directory);
  if (journal.state != PublicationRecoveryState::kValid || !recovered || *recovered != *encoded ||
      !receipt.ok || receipt.absent || receipt.bytes != o.pending_receipt_bytes || !held())
    return Failure("publication-registry-ownership", "Registry effects require the exact durable pending operation.");
  if (o.plan.registry.empty()) return {PackageActionState::kComplete, {}, {}};
  if (!o.registry_backup) return Failure("publication-registry-backup", "Registry snapshot binding is missing.");
  auto backup = LoadPublicationRegistryBackup(control, o.transaction_id, *o.registry_backup);
  if (backup.state != PublicationRecoveryState::kValid)
    return Failure("publication-registry-backup", "Immutable registry backup is unavailable.");
  auto intended = backup.bytes;
  if (!ApplyRegistryValues(&intended, o.plan.registry, o.registry_unix_time, &reason) ||
      intended.size() > kMaxPublicationRegistryBackupBytes)
    return Failure("publication-registry-render", "Frozen registry edits cannot be safely rendered.");
  const auto prefix_path = Map(prefix, o.install.data_root + "/library/prefixes/game");
  auto bottle = ManagedDirectory::Open(prefix_path, DirectoryPolicy::kPrivate);
  if (!bottle) return Failure("publication-registry-directory", "Private bottle directory is unavailable.");
  Storage storage(prefix_path.string());
  auto read = [&](std::string* bytes, unsigned* mode) {
    struct stat before{}, after{};
    if (!bottle->InspectChild("user.reg", &before) || !S_ISREG(before.st_mode) ||
        before.st_uid != geteuid() || before.st_nlink != 1 || before.st_size < 0 ||
        static_cast<std::uint64_t>(before.st_size) > kMaxPublicationRegistryBackupBytes ||
        ((before.st_mode & 07777) != 0600 && (before.st_mode & 07777) != 0644)) return false;
    auto observed = storage.ReadDurable("user.reg", kMaxPublicationRegistryBackupBytes);
    if (!observed.ok() || !bottle->InspectChild("user.reg", &after) ||
        before.st_dev != after.st_dev || before.st_ino != after.st_ino || before.st_mode != after.st_mode ||
        before.st_uid != after.st_uid || before.st_nlink != after.st_nlink || before.st_size != after.st_size ||
        before.st_mtime != after.st_mtime || before.st_ctime != after.st_ctime ||
        observed.value().size() != static_cast<std::uint64_t>(before.st_size)) return false;
    bytes->assign(observed.value().begin(), observed.value().end()); *mode = before.st_mode & 07777;
    return true;
  };
  std::string current; unsigned mode = 0;
  if (!read(&current, &mode) || !held())
    return Failure("publication-registry-read", "Registry identity or durability could not be verified.");
  const bool original = current == backup.bytes && mode == o.registry_backup->source_mode;
  const bool applied = current == intended && mode == 0600;
  if (!original && !applied)
    return Failure("publication-registry-collision", "Live registry matches neither frozen original nor intended bytes.");
  if (!applied) {
    Bytes bytes(intended.begin(), intended.end());
    if (!storage.WriteDurable("user.reg", bytes).ok())
      return Failure("publication-registry-write", "Registry publication failed; pending evidence must be recovered.");
  }
  if (!held() || !read(&current, &mode) || current != intended || mode != 0600 || !held())
    return Failure("publication-registry-verify", "Published registry could not be verified durably.");
  return {PackageActionState::kComplete, {}, {}};
}
PackageActionResult ApplyPendingPublicationDxvk(const SettingsPublicationOperation& o,
    int borrowed, const std::filesystem::path& prefix) {
  std::string reason;
  auto encoded = EncodeSettingsPublicationOperation(o, &reason);
  if (!encoded || (!prefix.empty() && (!prefix.is_absolute() || prefix != prefix.lexically_normal())))
    return Failure("publication-invalid", "Frozen DXVK operation or namespace is invalid.");
  const auto control = Map(prefix, o.install.data_root + "/.playden-control");
  auto directory = ManagedDirectory::Open(control, DirectoryPolicy::kPrivate);
  struct stat opened{}, named{};
  auto held = [&] {
    return directory && borrowed >= 0 && directory->Validate() &&
        fstat(borrowed, &opened) == 0 && directory->InspectChild("publication.lock", &named) &&
        S_ISREG(opened.st_mode) && S_ISREG(named.st_mode) && opened.st_uid == geteuid() &&
        named.st_uid == geteuid() && opened.st_nlink == 1 && named.st_nlink == 1 &&
        (opened.st_mode & 07777) == 0600 && (named.st_mode & 07777) == 0600 &&
        opened.st_dev == named.st_dev && opened.st_ino == named.st_ino;
  };
  if (!held()) return Failure("publication-lock", "Registry publisher descriptor is unavailable or changed.");
  auto journal = LoadSettingsPublicationOperation(control);
  auto recovered = journal.operation ? EncodeSettingsPublicationOperation(*journal.operation, &reason) : std::nullopt;
  auto receipt = ReadReceipt(control, *directory);
  if (journal.state != PublicationRecoveryState::kValid || !recovered || *recovered != *encoded ||
      !receipt.ok || receipt.absent || receipt.bytes != o.pending_receipt_bytes || !held())
    return Failure("publication-dxvk-ownership", "Registry effects require the exact durable pending operation.");
  PwTitlePublicationMetadata metadata{};
  if (pw_title_publication_metadata_parse(reinterpret_cast<const uint8_t*>(o.metadata_bytes.data()),
      o.metadata_bytes.size(), &metadata) != PW_OK)
    return Failure("publication-invalid", "Frozen DXVK metadata is invalid.");
  const auto target = Map(prefix, metadata.dxvk_path);
  auto game = ManagedDirectory::Open(target.parent_path(), DirectoryPolicy::kApprovedTree);
  struct stat game_info{};
  if (!game || lstat(target.parent_path().c_str(), &game_info) || game_info.st_uid != geteuid() ||
      ((game_info.st_mode & 07777) != 0700 && (game_info.st_mode & 07777) != 0755))
    return Failure("publication-dxvk-directory", "Owned game working directory is unavailable.");
  auto same = [](const struct stat& a, const struct stat& b) {
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_mode == b.st_mode &&
        a.st_uid == b.st_uid && a.st_nlink == b.st_nlink && a.st_size == b.st_size &&
        a.st_mtime == b.st_mtime && a.st_ctime == b.st_ctime;
  };
  auto read = [&](FrozenFileSnapshot* snapshot) {
    *snapshot = {}; struct stat before{}, opened{}, after{};
    if (!game->InspectChild("dxvk.conf", &before)) return errno == ENOENT && game->Sync();
    if (!S_ISREG(before.st_mode) || before.st_uid != geteuid() || before.st_nlink != 1 ||
        before.st_size < 0 || before.st_size > 256 * 1024 ||
        ((before.st_mode & 07777) != 0600 && (before.st_mode & 07777) != 0644)) return false;
    int fd = game->OpenRegularFile("dxvk.conf", true, before.st_mode & 07777);
    if (fd < 0) return false;
    bool valid = fstat(fd, &opened) == 0 && same(before, opened);
    std::string bytes; char buffer[4096];
    while (valid) {
      auto count = ::read(fd, buffer, sizeof(buffer));
      if (count < 0 && errno == EINTR) continue;
      if (count < 0 || static_cast<std::size_t>(count) > 256 * 1024 - bytes.size()) { valid = false; break; }
      if (!count) break;
      bytes.append(buffer, count);
    }
    valid = valid && fstat(fd, &opened) == 0 && same(before, opened) &&
        game->InspectChild("dxvk.conf", &after) && same(before, after) &&
        bytes.size() == static_cast<std::uint64_t>(before.st_size) && fsync(fd) == 0;
    const bool closed = close(fd) == 0;
    if (!valid || !closed || !game->Sync()) return false;
    *snapshot = {true, static_cast<unsigned>(before.st_mode & 07777), bytes.size(), {}, std::move(bytes)};
    return true;
  };
  FrozenFileSnapshot current;
  if (!read(&current) || !held()) return Failure("publication-dxvk-read", "DXVK identity or durability could not be verified.");
  const bool desired_present = !o.plan.dxvk_config.empty();
  const unsigned desired_mode = o.dxvk_before.present ? o.dxvk_before.mode : 0644;
  const bool original = current.present == o.dxvk_before.present && (!current.present ||
      (current.bytes == o.dxvk_before.bytes && current.mode == o.dxvk_before.mode));
  const bool applied = current.present == desired_present && (!current.present ||
      (current.bytes == o.plan.dxvk_config && current.mode == desired_mode));
  if (!original && !applied)
    return Failure("publication-dxvk-collision", "Live DXVK matches neither frozen original nor intended bytes.");
  if (!applied && !desired_present) {
    if (!held() || !game->RemoveRegularFile("dxvk.conf") || !game->Sync())
      return Failure("publication-dxvk-remove", "DXVK removal failed; recover the pending operation.");
  } else if (!applied) {
    static std::atomic<unsigned long> sequence{0}; std::string temporary; int fd = -1;
    for (unsigned attempt = 0; attempt < 128; ++attempt) {
      temporary = ".dxvk.conf.publication-" + std::to_string(getpid()) + "-" + std::to_string(sequence++);
      fd = game->CreateExclusiveFile(temporary, desired_mode);
      if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) return Failure("publication-dxvk-write", "Exclusive DXVK temporary creation failed.");
    std::size_t offset = 0; bool valid = true;
    while (offset < o.plan.dxvk_config.size()) {
      const auto count = write(fd, o.plan.dxvk_config.data() + offset, o.plan.dxvk_config.size() - offset);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) { valid = false; break; }
      offset += static_cast<std::size_t>(count);
    }
    const bool synced = valid && fsync(fd) == 0; const bool closed = close(fd) == 0;
    if (!synced || !closed || !held() || !game->RenameChild(temporary, *game, "dxvk.conf", current.present) || !game->Sync())
      return Failure("publication-dxvk-write", "DXVK publication failed; pending effects require recovery.");
  }
  if (!held() || !read(&current) || current.present != desired_present ||
      (current.present && (current.bytes != o.plan.dxvk_config || current.mode != desired_mode)) || !held())
    return Failure("publication-dxvk-verify", "DXVK intended presence/bytes/mode could not be verified durably.");
  return {PackageActionState::kComplete, {}, {}};
}
PackageActionResult ApplyPendingPublicationFiles(const SettingsPublicationOperation& o,
    const PublicationPaths& paths, int borrowed, const std::filesystem::path& prefix,
    const std::function<bool(const std::filesystem::path&)>& after_effect) {
  std::string reason;
  if (!EncodeSettingsPublicationOperation(o, &reason) || !PathsMatch(o, paths, prefix))
    return Failure("publication-invalid", "Frozen file operation or canonical paths are invalid.");
  auto result = ApplyPendingPublicationRegistry(o, borrowed, prefix);
  if (!Complete(result)) return result;
  const auto control = Map(prefix, o.install.data_root + "/.playden-control");
  auto directory = ManagedDirectory::Open(control, DirectoryPolicy::kPrivate);
  auto held = [&] {
    struct stat opened{}, named{};
    return directory && directory->Validate() && borrowed >= 0 && fstat(borrowed, &opened) == 0 &&
        directory->InspectChild("publication.lock", &named) && S_ISREG(opened.st_mode) && S_ISREG(named.st_mode) &&
        opened.st_uid == geteuid() && named.st_uid == geteuid() && opened.st_nlink == 1 && named.st_nlink == 1 &&
        (opened.st_mode & 07777) == 0600 && (named.st_mode & 07777) == 0600 &&
        opened.st_dev == named.st_dev && opened.st_ino == named.st_ino;
  };
  auto checkpoint = [&](const std::filesystem::path& path) { return (!after_effect || after_effect(path)) && held(); };
  if (!checkpoint(Map(prefix, o.install.data_root + "/library/prefixes/game/user.reg")))
    return Failure("publication-interrupted", "Registry effect requires fresh recovery.");
  result = ApplyPendingPublicationDxvk(o, borrowed, prefix);
  if (!Complete(result)) return result;
  PwTitlePublicationMetadata current{}, previous{};
  if (pw_title_publication_metadata_parse(reinterpret_cast<const uint8_t*>(o.metadata_bytes.data()),
      o.metadata_bytes.size(), &current) != PW_OK || (!o.previous_metadata_bytes.empty() &&
      pw_title_publication_metadata_parse(reinterpret_cast<const uint8_t*>(o.previous_metadata_bytes.data()),
      o.previous_metadata_bytes.size(), &previous) != PW_OK))
    return Failure("publication-invalid", "Frozen publication metadata is invalid.");
  if (!checkpoint(Map(prefix, current.dxvk_path))) return Failure("publication-interrupted", "DXVK effect requires fresh recovery.");
  auto library = ManagedDirectory::Open(Map(prefix, o.install.data_root + "/library"), DirectoryPolicy::kPrivate);
  if (!library) return Failure("publication-directory", "Owned private library is unavailable.");
  auto profiles = library->OpenChild("profiles", DirectoryPolicy::kPrivate, true);
  if (!profiles || !profiles->Sync() || !library->Sync() || !checkpoint(paths.profile.parent_path()))
    return Failure("publication-directory", "Private profile directory requires recovery.");
  auto inputs = library->OpenChild("input", DirectoryPolicy::kPrivate, true);
  if (!inputs || !inputs->Sync() || !library->Sync() || !checkpoint(paths.input_directory))
    return Failure("publication-directory", "Private input directory requires recovery.");
  auto title = ManagedDirectory::Open(paths.descriptor.parent_path(), DirectoryPolicy::kApprovedTree);
  struct stat title_info{};
  if (!title || lstat(paths.descriptor.parent_path().c_str(), &title_info) || title_info.st_uid != geteuid() ||
      ((title_info.st_mode & 07777) != 0700 && (title_info.st_mode & 07777) != 0755))
    return Failure("publication-directory", "Owned staged title directory is unavailable.");
  auto publish = [&](const ManagedDirectory& parent, const std::filesystem::path& path,
                     const std::string& bytes, const std::string& prior_hash) -> PackageActionResult {
    if (!held()) return Failure("publication-lock", "Publisher ownership changed before file effects.");
    auto observed = ReadPublicationFile(parent, path.filename().string(), kMaxSettingsPublicationOperationBytes);
    if (!observed) return Failure("publication-file-read", "Publication file identity or durability is unavailable.");
    const bool intended = observed->present && observed->mode == 0600 && observed->bytes == bytes;
    const auto digest = observed->present ? HashArtifactBytes(observed->bytes) : std::nullopt;
    const bool original = prior_hash.empty() ? !observed->present :
        observed->present && digest && *digest == prior_hash;
    if (!original && !intended)
      return Failure("publication-file-collision", "Publication file matches neither owned prior nor intended bytes.");
    if (!intended && !WritePublicationFile(parent, path.filename().string(), bytes, *observed, held))
      return Failure("publication-file-write", "Publication file write failed; recover retained pending evidence.");
    auto verified = ReadPublicationFile(parent, path.filename().string(), kMaxSettingsPublicationOperationBytes);
    if (!verified || !verified->present || verified->mode != 0600 || verified->bytes != bytes || !held())
      return Failure("publication-file-verify", "Publication file readback could not be verified.");
    if (!checkpoint(path)) return Failure("publication-interrupted", "Durable publication file requires fresh recovery.");
    return {PackageActionState::kComplete, {}, {}};
  };
  result = publish(*profiles, paths.profile, o.plan.profile_bytes,
      previous.profile_id == o.plan.profile_id ? previous.profile_sha256 : "");
  if (!Complete(result)) return result;
  for (const auto& input : o.plan.input_presets) {
    std::string prior;
    for (std::size_t i = 0; i < previous.input_count; ++i)
      if (input.first == previous.inputs[i].name) prior = previous.inputs[i].sha256;
    result = publish(*inputs, paths.input_directory / input.first, input.second, prior);
    if (!Complete(result)) return result;
  }
  result = publish(*profiles, paths.catalog, o.catalog_bytes, previous.catalog_sha256);
  if (!Complete(result)) return result;
  result = publish(*title, paths.descriptor, o.descriptor_bytes, previous.descriptor_sha256);
  if (!Complete(result)) return result;
  std::string previous_digest;
  if (!o.previous_metadata_bytes.empty()) {
    auto digest = HashArtifactBytes(o.previous_metadata_bytes);
    if (!digest) return Failure("publication-hash", "Prior metadata hashing is unavailable.");
    previous_digest = *digest;
  }
  return publish(*profiles, paths.metadata, o.metadata_bytes, previous_digest);
}
}  // namespace playden::runtime
