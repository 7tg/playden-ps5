// main.c - One-shot payload: does the native uninstall work while an app runs?
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reads title IDs (one per line) from /data/uninstall-probe/uninstall.lst and calls
// sceAppInstUtilAppUnInstall for each, the way ShadowMountPlus does, logging every
// result to /data/uninstall-probe/uninstall.log. Only disposable probe titles
// (FAKE99xxx) are accepted. Launched through the web file manager's launch-elf.
#include <ps5/kernel.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppUnInstall(const char *title_id);

#define AUTHID_BASE 0x4800000000000006ull  // ShadowMountPlus main.c:54

static int disposable(const char *id) {
  if (strlen(id) != 9 || strncmp(id, "FAKE99", 6) != 0) return 0;
  for (int i = 6; i < 9; ++i) if (id[i] < '0' || id[i] > '9') return 0;
  return 1;
}

int main(void) {
  mkdir("/data/uninstall-probe", 0777);
  FILE *log = fopen("/data/uninstall-probe/uninstall.log", "a");
  if (!log) return 1;
  setvbuf(log, NULL, _IONBF, 0);
  fprintf(log, "start time=%ld\n", (long)time(NULL));
  fprintf(log, "authid_before=%016llx\n", (unsigned long long)kernel_get_ucred_authid(-1));
  fprintf(log, "appinst_init=%08x\n", (unsigned)sceAppInstUtilInitialize());
  // ShadowMountPlus initializes first and then takes this authid (main.c:538-539).
  fprintf(log, "authid_set=%d\n", kernel_set_ucred_authid(-1, AUTHID_BASE));
  fflush(log);
  FILE *list = fopen("/data/uninstall-probe/uninstall.lst", "r");
  if (!list) { fprintf(log, "no list\nend\n"); fclose(log); return 1; }
  char line[64];
  while (fgets(line, sizeof line, list)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!line[0]) continue;
    if (!disposable(line)) { fprintf(log, "refused %s (not FAKE99xxx)\n", line); continue; }
    const int result = sceAppInstUtilAppUnInstall(line);
    fprintf(log, "uninstall %s result=%08x\n", line, (unsigned)result);
    fflush(log);
  }
  fclose(list);
  fprintf(log, "end time=%ld\n", (long)time(NULL));
  fclose(log);
  return 0;
}
