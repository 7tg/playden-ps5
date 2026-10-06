#include "platform/managed_directory.h"
#include "platform/storage.h"
#include "runtime/artifact_hash.h"
#include "runtime/private_directories.h"
#include "runtime/game_settings.h"
#include "runtime/settings_storage.h"
#include "runtime/session_storage.h"
#include "runtime/launch_service.h"

#include <cerrno>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <limits>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

extern "C" int playden_ps5_probe_flock(int descriptor, int operation);

namespace playden::ps5 {

void RunManagedFilesystemProbe(FILE* log) {
  using platform::DirectoryPolicy;
  using platform::ManagedDirectory;
  const auto check = [&](bool ok, const char* step) {
    std::fprintf(log, "managed-filesystem step=%s ok=%d errno=%d\n", step, ok, ok ? 0 : errno);
    std::fflush(log);
    return ok;
  };
  const auto root = std::filesystem::path("/data/playden") /
      ("managed-fs-" + std::to_string(getpid()) + "-" + std::to_string(std::time(nullptr)));
  const auto bottle = root / "library/prefixes/game";
  std::fprintf(log, "managed-filesystem begin euid=%u root=%s\n", unsigned(geteuid()), root.c_str());
  struct stat info{};
  if (!check(lstat(root.c_str(), &info) != 0 && errno == ENOENT, "fresh-root")) return;
  std::string error;
  if (!check(runtime::EnsurePrivateDirectories("/data", root, bottle, &error), "private-directories")) {
    std::fprintf(log, "managed-filesystem detail=%s\n", error.c_str());
    return;
  }
  if (!check(runtime::IsPrivateDirectoryTree(root, bottle), "private-tree-readback")) return;
  auto prefix = ManagedDirectory::Open(bottle, DirectoryPolicy::kPrivate);
  auto parent = ManagedDirectory::Open(root, DirectoryPolicy::kPrivate);
  if (!check(prefix && parent, "open-contexts")) return;
  const int output = prefix->CreateExclusiveFile("proof.tmp", 0600);
  if (!check(output >= 0, "exclusive-create")) return;
  const bool private_file = fstat(output, &info) == 0 && S_ISREG(info.st_mode) &&
      info.st_uid == geteuid() && (info.st_mode & 07777) == 0600;
  std::fprintf(log, "managed-filesystem output uid=%u mode=%o\n", unsigned(info.st_uid), unsigned(info.st_mode & 07777));
  const bool wrote = private_file && write(output, "abc", 3) == 3 && fsync(output) == 0;
  const bool closed = close(output) == 0;
  if (!check(wrote && closed, "private-write-file-sync")) return;
  if (!check(prefix->RenameChild("proof.tmp", *prefix, "proof.txt", false) && prefix->Sync(), "publish-file")) return;
  const int input = prefix->OpenRegularFile("proof.txt", true, 0600);
  if (!check(input >= 0, "read-open")) return;
  char bytes[4]{};
  const auto count = read(input, bytes, sizeof(bytes));
  const bool read_closed = close(input) == 0;
  if (!check(count == 3 && bytes[0] == 'a' && bytes[1] == 'b' && bytes[2] == 'c' && read_closed, "readback")) return;
  const auto digest = runtime::HashArtifactFile(bottle / "proof.txt");
  if (!check(digest && *digest == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "production-hash")) return;
  auto staging = parent->OpenChild("staging", DirectoryPolicy::kPrivate, true);
  if (!check(staging && staging->Sync() && parent->Sync(), "create-staging")) return;
  if (!check(parent->RenameChild("staging", *parent, "published", false) && parent->Sync(), "publish-directory")) return;
  auto published = parent->OpenChild("published", DirectoryPolicy::kPrivate);
  if (!check(published && published->Sync(), "published-directory-readback")) return;
  if (!check(!staging->Validate() && staging->CreateExclusiveFile("wrong-file", 0600) < 0 &&
             !published->InspectChild("wrong-file", &info) && errno == ENOENT, "stale-context-refusal")) return;
  if (!check(parent->RemoveEmptyDirectory("published") && parent->Sync(), "remove-owned-directory")) return;
  const int removable = prefix->CreateExclusiveFile("delete-me", 0600);
  if (!check(removable >= 0, "create-removal-fixture")) return;
  const bool synced = fsync(removable) == 0;
  const bool removable_closed = close(removable) == 0;
  if (!check(synced && removable_closed && prefix->Sync(), "removal-fixture-sync")) return;
  if (!check(prefix->RemoveRegularFile("delete-me") && prefix->Sync(), "remove-owned-file")) return;
  if (!check(!prefix->InspectChild("delete-me", &info) && errno == ENOENT &&
             runtime::HashArtifactFile(bottle / "proof.txt") == digest, "sentinel-preserved")) return;
  const int listing_fd = dup(prefix->fd());
  if (!check(listing_fd >= 0, "directory-listing-dup")) return;
  DIR* listing = fdopendir(listing_fd);
  if (!listing) { close(listing_fd); check(false, "directory-listing-open"); return; }
  bool listed_proof = false;
  bool only_proof = true;
  errno = 0;
  while (const auto* entry = readdir(listing)) {
    const std::string name = entry->d_name;
    if (name == "proof.txt") listed_proof = true;
    else if (name != "." && name != "..") only_proof = false;
  }
  const bool listed_ok = errno == 0;
  const bool listing_closed = closedir(listing) == 0;
  if (!check(listed_ok && listing_closed && listed_proof && only_proof && prefix->Validate(), "directory-listing")) return;
  const auto legacy = root / "game-settings.json";
  const auto settings_root = root / "settings";
  const auto settings = settings_root / "game-settings.json";
  const std::map<std::uint32_t, runtime::GameSettingsRecord> original{{220, {"", {}, 1}}};
  std::map<std::uint32_t, runtime::GameSettingsRecord> loaded;
  if (!check(runtime::SaveGameSettingsFile(legacy.string(), original, &error) &&
             chmod(legacy.c_str(), 0644) == 0, "legacy-settings-fixture")) return;
  if (!check(runtime::EnsurePrivateDirectories(root, settings_root, settings_root, &error) &&
             runtime::MigrateLegacyGameSettingsFile(legacy.string(), settings.string(), &error), "private-settings-migration")) return;
  if (!check(runtime::LoadGameSettingsFile(settings.string(), &loaded, &error) && loaded == original &&
             !runtime::MigrateLegacyGameSettingsFile(legacy.string(), settings.string(), &error), "settings-reopen-no-overwrite")) return;
  const std::map<std::uint32_t, runtime::GameSettingsRecord> updated{{220, {"", {}, 2}}};
  if (!check(runtime::SaveGameSettingsFile(settings.string(), updated, &error) &&
             runtime::LoadGameSettingsFile(settings.string(), &loaded, &error) && loaded == updated &&
             lstat(settings.c_str(), &info) == 0 && info.st_uid == geteuid() &&
             (info.st_mode & 07777) == 0600, "private-settings-save-reopen")) return;
  if (!check(runtime::LoadGameSettingsFile(legacy.string(), &loaded, &error) && loaded == original, "legacy-settings-preserved")) return;
  const auto selected_settings = runtime::PrepareGameSettingsStorage(root, &error);
  if (!check(selected_settings &&
             runtime::LoadGameSettingsFile(selected_settings->string(), &loaded, &error) && loaded == original &&
             runtime::PrepareGameSettingsStorage(root, &error) == selected_settings &&
             lstat(selected_settings->c_str(), &info) == 0 && info.st_uid == geteuid() &&
             (info.st_mode & 07777) == 0600, "shared-settings-storage-setup")) return;
  Storage journal(root.string());
  const Bytes record{'a', 'b', 'c'};
  if (!check(journal.WriteDurable("journal.json", record).ok(), "durable-journal-publication")) return;
  const auto recovered = journal.ReadDurable("journal.json");
  if (!check(recovered.ok() && recovered.value() == record, "durable-journal-recovery")) return;
  if (!check(runtime::HashArtifactFile(root / "journal.json") == digest, "durable-journal-hash")) return;
  // Fresh synthetic wrapper records exercise the production native source and
  // journal. The host contract test separately consumes the actual C writer.
  runtime::RuntimeInstall install;
  install.install_id = "probe-session-" + std::to_string(getpid()) + "-" +
                       std::to_string(std::time(nullptr));
  install.title_id = "FAKE78001";
  install.app_id = 220;
  install.data_root = "/data/playden/games/" + install.install_id;
  install.install_path = "/games/220";
  install.runtime_bundle_id = "fixture-runtime";
  install.recipe_id = "hl2";
  install.recipe_version = "1";
  install.settings_revision = 1;
  const std::filesystem::path session_root(install.data_root);
  const auto session_bottle = session_root / "library/prefixes/game";
  const auto sessions_path = session_root / "logs/sessions";
  if (!check(lstat(session_root.c_str(), &info) != 0 && errno == ENOENT,
             "session-fresh-root")) return;
  if (!check(runtime::EnsurePrivateDirectories("/data", session_root, session_bottle, &error) &&
             runtime::EnsurePrivateDirectories("/data", session_root, sessions_path, &error),
             "session-private-directories")) return;
  Storage session_files(session_root.string());
  const auto write_fixture = [&](const char* path, const std::string& content) {
    return session_files.WriteDurable(path, Bytes(content.begin(), content.end())).ok();
  };
  const std::string marker = "schema=1\nkind=template\ninstall_id=" + install.install_id +
      "\ntitle_id=FAKE78001\nkit_id=fixture-kit\nfiles_sha256=" + std::string(64, 'a') +
      "\nruntime_bundle_id=fixture-runtime\nruntime_revision=fixture\nruntime_path=runtime\n"
      "variant_id=hl2\ntemplate_path=template\nrecipe_id=hl2\nrecipe_version=1\n";
  constexpr const char* session_id = "123e4567-e89b-72d3-c456-426614174000";
  const std::string session_json = "{\"schema_version\":1,\"session_id\":\"" +
      std::string(session_id) + "\",\"install_id\":\"" + install.install_id +
      "\",\"title_id\":\"FAKE78001\",\"origin\":\"home\",\"settings_revision\":1,"
      "\"runtime_bundle_id\":\"fixture-runtime\",\"phase\":\"exited\",\"exit_code\":0}\n";
  const auto filename = std::string("logs/sessions/") + session_id + ".json";
  if (!check(write_fixture("library/prefixes/game/.playden-template-owner", marker) &&
             write_fixture(filename.c_str(), session_json), "session-fixture-publication")) return;
  runtime::FilesystemReconcileSessionSource session_source;
  const auto enumerated = session_source.Enumerate(install, 8);
  if (!check(enumerated.ok && enumerated.sessions.size() == 1 &&
             enumerated.sessions[0].session_id == session_id &&
             enumerated.sessions[0].origin == runtime::SessionOrigin::kHome &&
             enumerated.sessions[0].exit_code == 0, "session-native-enumeration")) return;
  const auto reconcile_root = root / "session-journal";
  if (!check(runtime::EnsurePrivateDirectories(root, reconcile_root, reconcile_root, &error),
             "session-journal-private-root")) return;
  Storage reconcile_storage(reconcile_root.string());
  runtime::DurableReconcileJournal reconcile_journal(reconcile_storage);
  runtime::ReconcileEntry entry;
  entry.session = enumerated.sessions[0];
  entry.account_token = "fixture-account-revision";
  entry.phase = runtime::ReconcilePhase::kCompletionPending;
  entry.operation_id = "fixture-completed-operation";
  if (!check(reconcile_journal.Save(entry).ok, "session-journal-publication")) return;
  runtime::DurableReconcileJournal reopened_journal(reconcile_storage);
  const auto restored = reopened_journal.Load(session_id);
  if (!check(restored.ok && restored.entry &&
             restored.entry->session.install_id == install.install_id &&
             restored.entry->phase == entry.phase &&
             restored.entry->operation_id == entry.operation_id, "session-journal-reopen")) return;
  if (!check(write_fixture("logs/sessions/pending.lock", "fixture pending evidence\n") &&
             !session_source.Enumerate(install, 8).ok, "session-pending-evidence-refusal")) return;
  const auto launch_root = root / "launch-journal";
  if (!check(runtime::EnsurePrivateDirectories(root, launch_root, launch_root, &error),
             "launch-journal-private-root")) return;
  Storage launch_storage(launch_root.string());
  runtime::StorageLaunchSessionJournal launch_journal(launch_storage);
  runtime::LaunchSession launch_session;
  launch_session.session_id = "d9428888-122b-4b1e-8d5d-fd3b5f4b9b2c";
  launch_session.install_id = install.install_id;
  launch_session.title_id = install.title_id;
  launch_session.runtime_bundle_id = install.runtime_bundle_id;
  launch_session.settings_revision = std::numeric_limits<std::uint64_t>::max();
  launch_session.account_token = "fixture-account-revision";
  if (!check(launch_journal.CreateHistory(launch_session).ok &&
             !launch_journal.CreateHistory(launch_session).ok &&
             launch_journal.PersistPending(launch_session).ok,
             "launch-journal-history-exclusive-pending")) return;
  runtime::StorageLaunchSessionJournal launch_reopened(launch_storage);
  const auto pending_launch = launch_reopened.LoadPending();
  if (!check(pending_launch.ok && pending_launch.session &&
             pending_launch.session->session_id == launch_session.session_id &&
             pending_launch.session->settings_revision == launch_session.settings_revision,
             "launch-journal-reopen")) return;
  if (!check(launch_reopened.ClearPending().ok &&
             launch_reopened.ClearPending().ok, "launch-journal-clear-and-missing-sync")) return;
  const auto cleared_launch = launch_journal.LoadPending();
  if (!check(cleared_launch.ok && !cleared_launch.session, "launch-journal-absence-recovery")) return;

  // Independent open descriptions must conflict and closing the game reader
  // must release its lock. All requests are nonblocking; no existing file is used.
  const int lock_output = parent->CreateExclusiveFile("publication.lock", 0600);
  if (!check(lock_output >= 0, "publication-lock-private-fixture")) return;
  const bool lock_synced = fsync(lock_output) == 0;
  const bool lock_output_closed = close(lock_output) == 0;
  if (!check(lock_synced && lock_output_closed && parent->Sync(),
             "publication-lock-fixture-sync")) return;
  int reader = parent->OpenRegularFile("publication.lock", true, 0600);
  int writer = parent->OpenRegularFile("publication.lock", true, 0600);
  const auto close_locks = [&] {
    if (reader >= 0) close(reader);
    if (writer >= 0) close(writer);
  };
  if (!check(reader >= 0 && writer >= 0, "publication-lock-independent-open")) {
    close_locks(); return;
  }
  if (!check(playden_ps5_probe_flock(reader, LOCK_SH | LOCK_NB) == 0 &&
             playden_ps5_probe_flock(writer, LOCK_SH | LOCK_NB) == 0 &&
             playden_ps5_probe_flock(writer, LOCK_UN) == 0,
             "publication-lock-shared-readers")) { close_locks(); return; }
  errno = 0;
  const bool blocked_writer = playden_ps5_probe_flock(writer, LOCK_EX | LOCK_NB) < 0 &&
                              (errno == EWOULDBLOCK || errno == EAGAIN);
  if (!check(blocked_writer, "publication-lock-reader-blocks-writer")) { close_locks(); return; }
  const bool reader_closed = close(reader) == 0;
  reader = -1;
  if (!check(reader_closed && playden_ps5_probe_flock(writer, LOCK_EX | LOCK_NB) == 0,
             "publication-lock-reader-close-release")) { close_locks(); return; }
  reader = parent->OpenRegularFile("publication.lock", true, 0600);
  errno = 0;
  const bool blocked_reader = reader >= 0 &&
      playden_ps5_probe_flock(reader, LOCK_SH | LOCK_NB) < 0 &&
      (errno == EWOULDBLOCK || errno == EAGAIN);
  if (!check(blocked_reader, "publication-lock-writer-blocks-reader")) { close_locks(); return; }
  const bool writer_closed = close(writer) == 0;
  writer = -1;
  if (!check(writer_closed && playden_ps5_probe_flock(reader, LOCK_SH | LOCK_NB) == 0,
             "publication-lock-writer-close-release")) { close_locks(); return; }
  close_locks();
  std::fprintf(log, "managed-filesystem complete euid=%u sha256=%s\n", unsigned(geteuid()), digest->c_str());
  std::fflush(log);
}

}  // namespace playden::ps5
