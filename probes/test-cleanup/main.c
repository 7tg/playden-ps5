// main.c - One-shot payload: deletes leftover probe folders that FTP cannot reach.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reads absolute paths (one per line) from /data/uninstall-probe/cleanup.lst and
// deletes each tree. Only these leftovers are accepted: /data/homebrew/FAKE99xxx,
// /data/playden/games/acceptance-hl2-fresh-v16[.ftp-staging] and
// /data/playden/games/probe-session-* and the scratchpad probe folders under /data/playden. Symbolic links are removed as links and never
// followed (Wine prefixes link z: to /). Logs to /data/uninstall-probe/cleanup.log.
#include <ps5/kernel.h>

#include <dirent.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static FILE *log_file;
static unsigned long removed, failed;

static int allowed(const char *path) {
  const char *homebrew = "/data/homebrew/FAKE99";
  if (strncmp(path, homebrew, strlen(homebrew)) == 0) {
    const char *digits = path + strlen(homebrew);
    return strlen(digits) == 3 && digits[0] >= '0' && digits[0] <= '9' && digits[1] >= '0' && digits[1] <= '9' &&
           digits[2] >= '0' && digits[2] <= '9';
  }
  if (!strcmp(path, "/data/playden/games/acceptance-hl2-fresh-v16") ||
      !strcmp(path, "/data/playden/games/acceptance-hl2-fresh-v16.ftp-staging"))
    return 1;
  // Leftovers of the earlier scratchpad probes directly under /data/playden.
  static const char *const leftovers[] = {"/data/playden/logs", "/data/playden/wine", "/data/playden/development",
                                          "/data/playden/probe", "/data/playden/request-probe-1168"};
  for (unsigned i = 0; i < sizeof leftovers / sizeof leftovers[0]; ++i)
    if (!strcmp(path, leftovers[i])) return 1;
  const char *managed = "/data/playden/managed-fs-";
  if (strncmp(path, managed, strlen(managed)) == 0 && !strchr(path + strlen(managed), '/') && !strstr(path, ".."))
    return strlen(path) > strlen(managed);
  const char *session = "/data/playden/games/probe-session-";
  return strncmp(path, session, strlen(session)) == 0 && !strchr(path + strlen(session), '/') &&
         !strstr(path, "..") && strlen(path) > strlen(session);
}

static void remove_tree(const char *path) {
  struct stat info;
  if (lstat(path, &info) != 0) { ++failed; fprintf(log_file, "lstat %s errno=%d\n", path, errno); return; }
  if (!S_ISDIR(info.st_mode)) {
    if (unlink(path) == 0) ++removed; else { ++failed; fprintf(log_file, "unlink %s errno=%d\n", path, errno); }
    return;
  }
  // Root may change modes here but not read another user's private directory, so a
  // directory about to be deleted is opened up first.
  chmod(path, 0777);
  DIR *directory = opendir(path);
  if (!directory) { ++failed; fprintf(log_file, "opendir %s errno=%d\n", path, errno); return; }
  struct dirent *entry;
  char child[1024];
  while ((entry = readdir(directory))) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
    if (snprintf(child, sizeof child, "%s/%s", path, entry->d_name) >= (int)sizeof child) {
      ++failed; fprintf(log_file, "too long under %s\n", path); continue;
    }
    remove_tree(child);
  }
  closedir(directory);
  if (rmdir(path) == 0) ++removed; else { ++failed; fprintf(log_file, "rmdir %s errno=%d\n", path, errno); }
}

int main(void) {
  log_file = fopen("/data/uninstall-probe/cleanup.log", "a");
  if (!log_file) return 1;
  setvbuf(log_file, NULL, _IONBF, 0);
  fprintf(log_file, "start time=%ld uid=%d\n", (long)time(NULL), (int)getuid());
  // Private probe folders belong to other users: act as root for this run only.
  const int as_root = kernel_set_ucred_uid(-1, 0) | kernel_set_ucred_ruid(-1, 0) | kernel_set_ucred_svuid(-1, 0);
  static const uint8_t all_caps[16] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  const int caps = kernel_set_ucred_caps(getpid(), all_caps);
  fprintf(log_file, "root=%d uid=%d caps=%d\n", as_root, (int)getuid(), caps);
  FILE *list = fopen("/data/uninstall-probe/cleanup.lst", "r");
  if (!list) { fprintf(log_file, "no list\nend\n"); fclose(log_file); return 1; }
  char line[512];
  while (fgets(line, sizeof line, list)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!line[0]) continue;
    struct stat info;
    if (!allowed(line)) { fprintf(log_file, "refused %s\n", line); continue; }
    if (lstat(line, &info) != 0) { fprintf(log_file, "absent %s\n", line); continue; }
    if (!S_ISDIR(info.st_mode)) { fprintf(log_file, "refused %s (not a directory)\n", line); continue; }
    const unsigned long before_removed = removed, before_failed = failed;
    remove_tree(line);
    fprintf(log_file, "tree %s removed=%lu failed=%lu\n", line, removed - before_removed, failed - before_failed);
  }
  fclose(list);
  fprintf(log_file, "end removed=%lu failed=%lu time=%ld\n", removed, failed, (long)time(NULL));
  fclose(log_file);
  return 0;
}
