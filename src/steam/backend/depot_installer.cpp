#include "steam/backend/depot_installer.h"

#include "steam/backend/depot_chunk.h"
#include "steam/backend/gzip.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>

namespace playden::steam {
namespace {

std::string Hex(const std::array<std::uint8_t, 20>& bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string text;
  for (const auto byte : bytes) {
    text.push_back(kDigits[byte >> 4]);
    text.push_back(kDigits[byte & 15]);
  }
  return text;
}

std::string Lower(std::string text) {
  for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return text;
}

bool MakeDirs(const std::string& path) {
  if (path.empty()) return false;
  std::string current;
  std::size_t start = 0;
  if (path[0] == '/') { current = "/"; start = 1; }
  while (start <= path.size()) {
    const auto slash = path.find('/', start);
    const auto part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    if (!part.empty()) {
      if (current.size() > 1 || (current.size() == 1 && current[0] != '/')) current += '/';
      current += part;
      if (mkdir(current.c_str(), 0777) != 0) {
        struct stat info {};
        if (stat(current.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) return false;
      }
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  return true;
}

std::string Parent(const std::string& path) {
  const auto slash = path.rfind('/');
  return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

bool WriteAt(const std::string& path, std::uint64_t file_size, std::uint64_t offset,
             const std::vector<std::uint8_t>& data) {
  const int fd = open(path.c_str(), O_WRONLY | O_CREAT, 0666);
  if (fd < 0) return false;
  bool ok = true;
  struct stat info {};
  if (fstat(fd, &info) != 0) ok = false;
  if (ok && static_cast<std::uint64_t>(info.st_size) != file_size &&
      ftruncate(fd, static_cast<off_t>(file_size)) != 0)
    ok = false;
  std::size_t written = 0;
  while (ok && written < data.size()) {
    const auto result = pwrite(fd, data.data() + written, data.size() - written,
                               static_cast<off_t>(offset + written));
    if (result <= 0) ok = false;
    else written += static_cast<std::size_t>(result);
  }
  if (close(fd) != 0) ok = false;
  return ok;
}

bool EnsureFileSize(const std::string& path, std::uint64_t size) {
  const int fd = open(path.c_str(), O_WRONLY | O_CREAT, 0666);
  if (fd < 0) return false;
  const bool ok = ftruncate(fd, static_cast<off_t>(size)) == 0;
  return close(fd) == 0 && ok;
}

std::uint64_t DefaultFreeBytes(const std::string& path) {
  struct statvfs info {};
  if (statvfs(path.c_str(), &info) != 0) return 0;
  return static_cast<std::uint64_t>(info.f_bavail) * static_cast<std::uint64_t>(info.f_frsize);
}

bool RemoveTree(const std::string& path) {
  struct stat info {};
  if (lstat(path.c_str(), &info) != 0) return true;
  if (!S_ISDIR(info.st_mode)) return unlink(path.c_str()) == 0;
  DIR* dir = opendir(path.c_str());
  if (!dir) return false;
  bool ok = true;
  while (dirent* entry = readdir(dir)) {
    const std::string name = entry->d_name;
    if (name == "." || name == "..") continue;
    ok = RemoveTree(path + "/" + name) && ok;
  }
  closedir(dir);
  return rmdir(path.c_str()) == 0 && ok;
}

bool Exists(const std::string& path) {
  struct stat info {};
  return stat(path.c_str(), &info) == 0;
}

bool SameFile(const ManifestFile& left, const ManifestFile& right) {
  if (left.size != right.size || left.flags != right.flags || left.link_target != right.link_target ||
      left.sha_content != right.sha_content || left.chunks.size() != right.chunks.size())
    return false;
  for (std::size_t index = 0; index < left.chunks.size(); ++index)
    if (left.chunks[index].sha != right.chunks[index].sha) return false;
  return true;
}

InstallError FromCm(CmError error) {
  switch (error) {
    case CmError::kNone: return InstallError::kNone;
    case CmError::kCanceled: return InstallError::kCanceled;
    case CmError::kNoSession: return InstallError::kNoSession;
    case CmError::kRejected: return InstallError::kRejected;
    case CmError::kNetwork:
    case CmError::kTimeout: return InstallError::kNetwork;
    case CmError::kProtocol: return InstallError::kProtocol;
  }
  return InstallError::kProtocol;
}

struct Placement {
  std::size_t depot = 0;
  const ManifestFile* file = nullptr;
};

std::string JournalKey(std::uint32_t depot, const std::string& path, std::uint64_t offset) {
  return std::to_string(depot) + "\t" + std::to_string(offset) + "\t" + path;
}

}  // namespace

const char* InstallErrorName(InstallError error) {
  switch (error) {
    case InstallError::kNone: return "none";
    case InstallError::kCanceled: return "canceled";
    case InstallError::kNoSession: return "no_session";
    case InstallError::kRejected: return "rejected";
    case InstallError::kNetwork: return "network";
    case InstallError::kCorrupt: return "corrupt";
    case InstallError::kNoSpace: return "no_space";
    case InstallError::kStorage: return "storage";
    case InstallError::kProtocol: return "protocol";
  }
  return "protocol";
}

DepotInstaller::DepotInstaller(CmClient& cm, CdnClient& cdn) {
  hooks_.access = [&cm](const StoredSession& session, std::uint32_t app_id,
                        const std::vector<DepotRequest>& depots, const std::atomic<bool>& cancel) {
    return cm.FetchDepotAccess(session, app_id, depots, cancel);
  };
  hooks_.servers = [&cdn](std::uint32_t cell, const std::atomic<bool>& cancel, std::vector<ContentServer>* out) {
    return cdn.Servers(cell, cancel, out);
  };
  hooks_.manifest = [&cdn](const ContentServer& server, const DepotGrant& grant, const std::atomic<bool>& cancel,
                           DepotManifest* out) {
    std::vector<std::uint8_t> zip;
    const auto fetched = cdn.Manifest(server, grant.depot_id, grant.manifest_gid, grant.manifest_request_code,
                                      cancel, &zip);
    if (fetched == CdnError::kCanceled) return InstallError::kCanceled;
    if (fetched != CdnError::kNone) return InstallError::kNetwork;
    std::vector<std::uint8_t> raw;
    if (!UnzipFirstEntry(zip, &InflateRaw, &raw) || !ParseDepotManifest(raw, out)) return InstallError::kCorrupt;
    if (out->filenames_encrypted && !DecryptManifestFilenames(out, grant.key, &SymmetricDecrypt))
      return InstallError::kCorrupt;
    std::string reason;
    if (out->depot_id != grant.depot_id || out->gid != grant.manifest_gid || !ValidateDepotManifest(*out, &reason))
      return InstallError::kCorrupt;
    return InstallError::kNone;
  };
  hooks_.chunk = [&cdn](const ContentServer& server, std::uint32_t depot, const std::string& sha,
                        std::size_t max_bytes, const std::atomic<bool>& cancel, std::vector<std::uint8_t>* out) {
    return cdn.Chunk(server, depot, sha, max_bytes, cancel, out);
  };
  hooks_.free_bytes = &DefaultFreeBytes;
  hooks_.last_failure = [&cdn] { return cdn.last_failure(); };
}

DepotInstaller::DepotInstaller(InstallerHooks hooks) : hooks_(std::move(hooks)) {
  if (!hooks_.free_bytes) hooks_.free_bytes = &DefaultFreeBytes;
}

InstallOutcome DepotInstaller::Run(const StoredSession& session, const InstallTarget& target,
                                   const std::atomic<bool>& cancel,
                                   const std::function<void(const InstallProgress&)>& on_progress) {
  InstallOutcome outcome;
  auto fail = [&outcome](InstallError error, const char* detail) {
    outcome.error = error;
    outcome.detail = detail;
    return outcome;
  };
  if (target.app_id == 0 || target.install_root.empty() || target.install_root[0] != '/' ||
      target.depots.empty() || target.install_root.find("/..") != std::string::npos)
    return fail(InstallError::kProtocol, "invalid install target");
  if (cancel.load()) return fail(InstallError::kCanceled, "canceled");

  std::vector<DepotRequest> requests;
  for (const auto& depot : target.depots) {
    if (!depot.manifest_gid || *depot.manifest_gid == 0) return fail(InstallError::kProtocol, "depot without manifest");
    requests.push_back({depot.id, *depot.manifest_gid});
  }
  const auto access = hooks_.access(session, target.app_id, requests, cancel);
  if (!access.ok()) {
    outcome.error = FromCm(access.error);
    outcome.detail = access.detail.empty() ? "depot access failed" : access.detail;
    return outcome;
  }
  if (access.grants.size() != requests.size()) return fail(InstallError::kProtocol, "incomplete depot access");

  std::vector<ContentServer> servers;
  const auto listed = hooks_.servers(session.cell_id.value_or(0), cancel, &servers);
  if (listed == CdnError::kCanceled) return fail(InstallError::kCanceled, "canceled");
  if (listed != CdnError::kNone || servers.empty()) return fail(InstallError::kNetwork, "no content servers");

  // Manifests, in mounting order.
  std::vector<DepotManifest> manifests(access.grants.size());
  for (std::size_t index = 0; index < access.grants.size(); ++index) {
    InstallError fetched = InstallError::kNetwork;
    for (std::size_t attempt = 0; attempt < servers.size() * 2 && attempt < 6; ++attempt) {
      fetched = hooks_.manifest(servers[attempt % servers.size()], access.grants[index], cancel, &manifests[index]);
      if (fetched != InstallError::kNetwork) break;
    }
    if (fetched != InstallError::kNone) {
      outcome.error = fetched;
      outcome.detail = "manifest unavailable";
      if (hooks_.last_failure) outcome.detail += " " + hooks_.last_failure();
      return outcome;
    }
  }

  // Later depots replace the same path from earlier ones; identical copies stay with the first.
  std::map<std::string, Placement> placements;
  for (std::size_t depot = 0; depot < manifests.size(); ++depot) {
    for (const auto& file : manifests[depot].files) {
      const auto key = Lower(file.path);
      const auto found = placements.find(key);
      if (found != placements.end()) {
        if (SameFile(*found->second.file, file)) continue;
        if (found->second.file->is_directory() != file.is_directory()) return fail(InstallError::kCorrupt, "conflicting depots");
      }
      placements[key] = {depot, &file};
    }
  }

  InstallProgress progress;
  for (const auto& [key, placement] : placements) {
    if (placement.file->is_directory() || placement.file->is_symlink()) continue;
    progress.install_total += placement.file->size;
    ++progress.files_total;
    for (const auto& chunk : placement.file->chunks) progress.download_total += chunk.compressed_size;
  }

  // Journal of chunks already written in a previous run.
  const std::string journal_dir = target.install_root + "/" + kJournalDir;
  const std::string journal_path = journal_dir + "/journal";
  if (!MakeDirs(journal_dir)) return fail(InstallError::kStorage, "cannot create install folder");
  std::set<std::string> done;
  if (FILE* journal = std::fopen(journal_path.c_str(), "r")) {
    char line[8192];
    while (std::fgets(line, sizeof(line), journal)) {
      std::string entry(line);
      while (!entry.empty() && (entry.back() == '\n' || entry.back() == '\r')) entry.pop_back();
      if (!entry.empty()) done.insert(entry);
    }
    std::fclose(journal);
  }

  std::uint64_t remaining_bytes = 0;
  for (const auto& [key, placement] : placements) {
    if (placement.file->is_directory() || placement.file->is_symlink()) continue;
    const auto depot_id = manifests[placement.depot].depot_id;
    for (const auto& chunk : placement.file->chunks) {
      if (done.count(JournalKey(depot_id, placement.file->path, chunk.offset))) progress.download_done += chunk.compressed_size;
      else remaining_bytes += chunk.original_size;
    }
  }
  if (hooks_.free_bytes(target.install_root) < remaining_bytes + 64ull * 1024 * 1024)
    return fail(InstallError::kNoSpace, "not enough free space");
  if (on_progress) on_progress(progress);

  FILE* journal = std::fopen(journal_path.c_str(), "a");
  if (!journal) return fail(InstallError::kStorage, "cannot write journal");
  std::size_t server_cursor = 0;
  for (const auto& [key, placement] : placements) {
    const auto& file = *placement.file;
    const std::string path = target.install_root + "/" + file.path;
    if (file.is_directory()) {
      if (!MakeDirs(path)) { std::fclose(journal); return fail(InstallError::kStorage, "cannot create folder"); }
      continue;
    }
    if (file.is_symlink()) continue;  // recorded in the install record; the console has no symlinks
    if (!MakeDirs(Parent(path))) { std::fclose(journal); return fail(InstallError::kStorage, "cannot create folder"); }
    if (file.chunks.empty() || !Exists(path)) {
      if (!EnsureFileSize(path, file.size)) { std::fclose(journal); return fail(InstallError::kStorage, "cannot create file"); }
    }
    const auto& grant = access.grants[placement.depot];
    for (const auto& chunk : file.chunks) {
      const auto entry = JournalKey(grant.depot_id, file.path, chunk.offset);
      if (done.count(entry)) continue;
      if (cancel.load()) { std::fclose(journal); return fail(InstallError::kCanceled, "canceled"); }
      std::vector<std::uint8_t> plain;
      InstallError result = InstallError::kNetwork;
      const auto attempts = static_cast<std::size_t>(kChunkAttempts) * std::max<std::size_t>(1, std::min<std::size_t>(servers.size(), 2));
      for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
        std::vector<std::uint8_t> encrypted;
        const auto fetched = hooks_.chunk(servers[server_cursor % servers.size()], grant.depot_id, Hex(chunk.sha),
                                          static_cast<std::size_t>(chunk.compressed_size) + 4096, cancel, &encrypted);
        if (fetched == CdnError::kCanceled) { std::fclose(journal); return fail(InstallError::kCanceled, "canceled"); }
        if (fetched != CdnError::kNone) { ++server_cursor; result = InstallError::kNetwork; continue; }
        const auto processed = ProcessDepotChunk(encrypted, grant.key, chunk.original_size, chunk.crc, chunk.sha, &plain);
        if (processed == ChunkError::kNone) { result = InstallError::kNone; break; }
        ++server_cursor;
        result = InstallError::kCorrupt;
      }
      if (result != InstallError::kNone) {
        std::fclose(journal);
        outcome.error = result;
        outcome.detail = "chunk unavailable";
        if (hooks_.last_failure && result == InstallError::kNetwork) outcome.detail += " " + hooks_.last_failure();
        return outcome;
      }
      if (!WriteAt(path, file.size, chunk.offset, plain)) { std::fclose(journal); return fail(InstallError::kStorage, "cannot write file"); }
      std::fprintf(journal, "%s\n", entry.c_str());
      std::fflush(journal);
      progress.download_done += chunk.compressed_size;
      if (on_progress) on_progress(progress);
    }
  }
  std::fclose(journal);

  // Install record, then drop the journal: the record marks a complete, verified install.
  const std::string record_path = target.install_root + "/" + kInstallRecord;
  const std::string temporary = record_path + ".tmp";
  FILE* record = std::fopen(temporary.c_str(), "w");
  if (!record) return fail(InstallError::kStorage, "cannot write install record");
  std::fprintf(record, "{\"version\":1,\"app_id\":%u,\"install_bytes\":%llu,\"files\":%u,\"depots\":[",
               target.app_id, static_cast<unsigned long long>(progress.install_total), progress.files_total);
  for (std::size_t index = 0; index < access.grants.size(); ++index)
    std::fprintf(record, "%s{\"id\":%u,\"manifest\":\"%llu\"}", index ? "," : "", access.grants[index].depot_id,
                 static_cast<unsigned long long>(access.grants[index].manifest_gid));
  std::fprintf(record, "]}\n");
  const bool closed = std::fclose(record) == 0;
  if (!closed || std::rename(temporary.c_str(), record_path.c_str()) != 0)
    return fail(InstallError::kStorage, "cannot write install record");
  RemoveTree(journal_dir);
  outcome.error = InstallError::kNone;
  outcome.progress = progress;
  return outcome;
}

bool RemoveManagedInstall(const std::string& install_root) {
  if (install_root.size() < 2 || install_root[0] != '/' || install_root.find("/..") != std::string::npos) return false;
  if (!Exists(install_root)) return true;
  if (!Exists(install_root + "/" + DepotInstaller::kInstallRecord) &&
      !Exists(install_root + "/" + DepotInstaller::kJournalDir))
    return false;  // not a Playden-managed folder
  return RemoveTree(install_root);
}

}  // namespace playden::steam
