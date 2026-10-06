#include "platform/managed_directory.h"
#include "runtime/artifact_hash.h"
#include "runtime/private_directories.h"

#include <cerrno>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

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
  std::fprintf(log, "managed-filesystem complete euid=%u sha256=%s\n", unsigned(geteuid()), digest->c_str());
  std::fflush(log);
}

}  // namespace playden::ps5
