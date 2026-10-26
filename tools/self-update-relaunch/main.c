// main.c - Playden's one-shot relaunch payload for its self-update.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A title cannot start itself after it exits. Just before Playden quits to finish an update it
// sends this payload to the payload loader on 127.0.0.1:9021. The payload reads the title to open
// from REQUEST's first line ("FAKE77031\n"), and the quitting title from its second line (the same
// title when there is none), waits until the quitting title's sandbox (/mnt/sandbox/<id>_*) is
// gone, and starts the title with sceLncUtilLaunchApp, falling back to sceSystemServiceLaunchApp.
// It gives up after 120 s. If the start fails, a notification asks the player to open Playden.
// Measured in results/20261015-self-update-probes: the sandbox goes about 5 s after the quit.
//
// When Playden moves to another title ("PPSA97031\nFAKE77031\n"), ShadowMountPlus registers the
// new title only once the old one has closed (results/20261025-ppsa97031-probe: about 1 s later),
// so the payload waits up to 60 s for its /user/app/<id>/mount.lnk. If the new title can't be
// started, it opens the old one, which tells the player where the update went. Playden rewrites
// REQUEST when the move fails at quit, so the first line is read again once the old title is gone.
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define REQUEST "/data/playden/self-update/relaunch-title"
#define LOG "/data/playden/self-update/relaunch.log"

typedef struct {
    uint32_t size;
    uint32_t user_id;
    uint32_t app_opt;
    uint32_t pad;
    uint64_t crash_report;
    uint64_t check_flag;
} launch_params;

typedef struct {
    int type, req_id, priority, msg_id, target_id, user_id, unk1, unk2, app_id, error_num, unk3;
    char use_icon_image_uri;
    char message[1024];
    char icon_uri[1024];
    char unk[1024];
} notify_request;

int sceKernelSendNotificationRequest(int, notify_request*, size_t, int);
int sceUserServiceInitialize(void*);
int sceUserServiceGetForegroundUser(uint32_t*);
int sceLncUtilInitialize(void);
uint32_t sceLncUtilLaunchApp(const char*, const char**, launch_params*);
uint32_t sceSystemServiceLaunchApp(const char*, const char**, launch_params*);

static FILE* out;

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static int valid_title(const char* title) {
    if (strlen(title) != 9) return 0;
    for (int i = 0; i < 9; ++i)
        if (i < 4 ? title[i] < 'A' || title[i] > 'Z' : title[i] < '0' || title[i] > '9') return 0;
    return 1;
}

// 1 while the title's sandbox exists, 0 when it is gone, -1 when /mnt/sandbox can't be read.
static int running(const char* title) {
    DIR* folder = opendir("/mnt/sandbox");
    if (!folder) return -1;
    char prefix[16];
    snprintf(prefix, sizeof prefix, "%s_", title);
    int found = 0;
    struct dirent* entry;
    while ((entry = readdir(folder)))
        if (!strncmp(entry->d_name, prefix, strlen(prefix))) found = 1;
    closedir(folder);
    return found;
}

static void notify(const char* text) {
    notify_request request;
    memset(&request, 0, sizeof request);
    strncpy(request.message, text, sizeof request.message - 1);
    fprintf(out, "notify rc=0x%08x\n", sceKernelSendNotificationRequest(0, &request, sizeof request, 0));
}

// 1 once ShadowMountPlus has registered the title (its mount.lnk), 0 when it has not after 60 s.
static int registered(const char* title) {
    char link[64];
    snprintf(link, sizeof link, "/user/app/%s/mount.lnk", title);
    const double start = now();
    while (access(link, F_OK) != 0) {
        if (now() - start > 60) return 0;
        usleep(250000);
    }
    usleep(1000000);  // the Home screen entry follows the link by about a second
    return 1;
}

static uint32_t launch(const char* title, uint32_t user) {
    launch_params params = {sizeof params, user, 0, 0, 0, 0};
    uint32_t rc = sceLncUtilLaunchApp(title, NULL, &params);
    fprintf(out, "%s sceLncUtilLaunchApp rc=0x%08x\n", title, rc);
    if (rc & 0x80000000u) {
        launch_params fallback = {sizeof fallback, user, 0, 0, 0, 1};
        rc = sceSystemServiceLaunchApp(title, NULL, &fallback);
        fprintf(out, "%s sceSystemServiceLaunchApp rc=0x%08x\n", title, rc);
    }
    return rc;
}

int main(void) {
    out = fopen(LOG, "w");
    if (!out) return 1;
    setvbuf(out, NULL, _IONBF, 0);
    char title[32] = {0}, quitting[32] = {0};
    FILE* request = fopen(REQUEST, "r");
    if (!request || !fgets(title, sizeof title, request)) {
        fprintf(out, "no request\n");
        if (request) fclose(request);
        return 1;
    }
    if (!fgets(quitting, sizeof quitting, request)) quitting[0] = 0;
    fclose(request);
    title[strcspn(title, "\r\n")] = 0;
    quitting[strcspn(quitting, "\r\n")] = 0;
    if (!quitting[0]) strcpy(quitting, title);
    if (!valid_title(title) || !valid_title(quitting)) {
        fprintf(out, "bad title id\n");
        return 1;
    }
    const double start = now();
    int state;
    while ((state = running(quitting)) == 1 && now() - start < 120) usleep(250000);
    fprintf(out, "title=%s quitting=%s wait state=%d s=%.2f\n", title, quitting, state, now() - start);
    if (state != 0) {
        notify("Playden was updated. Open it again from the Home screen.");
        return 1;
    }
    char final_title[32] = {0};
    if ((request = fopen(REQUEST, "r"))) {
        if (fgets(final_title, sizeof final_title, request)) {
            final_title[strcspn(final_title, "\r\n")] = 0;
            if (valid_title(final_title) && strcmp(final_title, title) != 0) {
                fprintf(out, "title changed to %s\n", final_title);
                strcpy(title, final_title);
            }
        }
        fclose(request);
    }
    sceUserServiceInitialize(NULL);
    uint32_t user = 0;
    sceUserServiceGetForegroundUser(&user);
    sceLncUtilInitialize();
    uint32_t rc = 0x80000000u;
    if (strcmp(title, quitting) == 0) {
        rc = launch(title, user);
    } else {
        const int ready = registered(title);
        fprintf(out, "%s registered=%d s=%.2f\n", title, ready, now() - start);
        if (ready) rc = launch(title, user);
        if (rc & 0x80000000u) rc = launch(quitting, user);
    }
    if (rc & 0x80000000u) notify("Playden was updated. Open it again from the Home screen.");
    fclose(out);
    return 0;
}
