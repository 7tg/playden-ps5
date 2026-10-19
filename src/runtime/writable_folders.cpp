// writable_folders.cpp - Checks (and fixes) the folders Playden writes into before it writes.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "runtime/writable_folders.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace playden::runtime {
namespace {
// access(2) is refused in the PS5 sandbox: create and remove a file instead. 0 or the errno.
int Probe(const std::filesystem::path& folder) {
  static std::atomic<unsigned long long> sequence{0};
  for (unsigned attempt = 0; attempt < 8; ++attempt) {
    const auto probe = folder / (".playden-writable-" + std::to_string(getpid()) + "-" +
                                 std::to_string(sequence.fetch_add(1)));
    const int fd = open(probe.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
      if (errno == EEXIST) continue;
      return errno;
    }
    const bool closed = close(fd) == 0;
    if (unlink(probe.c_str()) != 0) return errno;
    return closed ? 0 : EIO;
  }
  return EEXIST;
}

std::string Error(int error) { return std::to_string(error) + " (" + std::strerror(error) + ")"; }
}  // namespace

std::string EnsureWritableFolders(const std::vector<std::filesystem::path>& folders,
                                  const WritableFoldersAsRoot& as_root,
                                  const std::function<void(const std::string&)>& log) {
  const auto note = [&log](const std::string& line) { if (log) log(line); };
  for (const auto& folder : folders) {
    if (folder.empty()) continue;
    struct stat info{};
    // Missing folders are made by their owners; links are never followed.
    if (lstat(folder.c_str(), &info) != 0) {
      if (errno != ENOENT) note("writable folder skipped path=" + folder.string() + " lstat=" + Error(errno));
      continue;
    }
    if (!S_ISDIR(info.st_mode)) continue;
    const int first = Probe(folder);
    if (first == 0) continue;
    int fix_error = 0;
    bool ran = false;
    const bool fixed = as_root && as_root([&] {
      ran = true;
      // By descriptor: a link swapped in after the lstat is refused, never followed as root.
      const int fd = open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
      if (fd < 0) { fix_error = errno; return false; }
      struct stat current{};
      bool ok = fstat(fd, &current) == 0 || (fix_error = errno, false);
      if (ok && !S_ISDIR(current.st_mode)) ok = (fix_error = ENOTDIR, false);
      if (ok && fchmod(fd, 0777) != 0) ok = (fix_error = errno, false);
      close(fd);
      return ok;
    });
    const int second = fixed ? Probe(folder) : first;
    if (fixed && second == 0) {
      char mode[8]; std::snprintf(mode, sizeof(mode), "%04o", unsigned(info.st_mode & 07777));
      note("writable folder fixed path=" + folder.string() + " mode=" + mode + " -> 0777 uid=" +
           std::to_string(info.st_uid) + " probe=" + Error(first));
      continue;
    }
    note("writable folder failed path=" + folder.string() + " uid=" + std::to_string(info.st_uid) +
         " probe=" + Error(first) +
         (!as_root ? " fix=unavailable"
                   : !ran ? " fix=root-refused"
                          : fixed ? " after-fix=" + Error(second) : " chmod=" + Error(fix_error)));
    return "Playden can't write to " + folder.string() +
           ". Make the folder writable over FTP (permissions 777), then try again.";
  }
  return {};
}

}  // namespace playden::runtime
