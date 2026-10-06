/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Fixture bytes adapted from prospero-win tests/test_pw_title_publication_native.c.
 * Developer diagnostic only: no Wine, title registration or existing game data. */
#include "pw_title_publication_native.h"
#include "pw_title_publication_ps5_lock.h"
#include "pw_title_publication.h"
#include "pw_publication_sha256.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

extern int playden_ps5_file_lock(int, int);
extern int sceKernelDlsym(int, const char *, void **);
static char nonce[17], mode[32], install_id[65], data_root[256];
static const char title_root[] = "/data/homebrew/FAKE78997";
static uid_t owner;
typedef struct Fixture {
    PwTitlePublicationPlatform platform;
    PwTitlePublicationDescriptor before;
    char descriptor[8192], profile[8192], input[8192];
    char metadata[32768], receipt[8192];
} Fixture;
static Fixture fixture;
#ifdef PWPA_NATIVE_TITLE
static FILE *pwpa_output;
#define printf(...) fprintf(pwpa_output, __VA_ARGS__)
#define PWPA_OUTPUT pwpa_output
#else
#define PWPA_OUTPUT stdout
#endif
static void fail(unsigned line) {
    printf("PWPA1 run=%s mode=%s stage=result pid=%d pass=0 line=%u errno=%d\n",
           nonce, mode, getpid(), line, errno); fflush(PWPA_OUTPUT); exit(1);
}
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
static void replace_once(char *text, size_t capacity, const char *old, const char *replacement);
static void expand(char *text, size_t capacity) {
    while (strstr(text, "@INSTALL@")) replace_once(text, capacity, "@INSTALL@", install_id);
}
static void path(char *out, size_t size, const Fixture *f, const char *canonical) {
    (void)f; CHECK(strlen(canonical) < size); strcpy(out, canonical); expand(out, size);
}
static int owned(const char *name, int directory, mode_t expected) {
    struct stat s; return !lstat(name, &s) && s.st_uid == owner &&
       (directory ? S_ISDIR(s.st_mode) : S_ISREG(s.st_mode) && s.st_nlink == 1) &&
       (s.st_mode & 07777) == expected;
}
static void sync_directory(const char *name) {
    int fd = open(name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    CHECK(fd >= 0); CHECK(fsync(fd) == 0); CHECK(close(fd) == 0);
}
static void directory(const char *name, mode_t permissions) {
    CHECK(mkdir(name, permissions) == 0); CHECK(owned(name, 1, permissions));
    char parent[1200]; CHECK(strlen(name) < sizeof(parent)); strcpy(parent, name);
    char *slash = strrchr(parent, '/'); CHECK(slash); *slash = 0; sync_directory(parent);
}
static void write_file(Fixture *f, const char *canonical, const void *bytes, size_t length, mode_t permissions) {
    char name[1200]; path(name, sizeof(name), f, canonical);
    int fd = open(name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, permissions);
    CHECK(fd >= 0); CHECK(fchmod(fd, permissions) == 0);
    size_t offset = 0;
    while (offset < length) { ssize_t n = write(fd, (const char *)bytes + offset, length - offset);
        if (n < 0 && errno == EINTR) continue; CHECK(n > 0); offset += (size_t)n; }
    CHECK(fsync(fd) == 0); CHECK(close(fd) == 0); CHECK(owned(name, 0, permissions));
    char *slash = strrchr(name, '/'); CHECK(slash); *slash = 0; sync_directory(name);
}
static void digest(const void *bytes, size_t length, char out[65])
{
    static const char hex[] = "0123456789abcdef";
    uint8_t raw[32];
    CHECK(pw_publication_sha256_bytes(bytes, length, raw) == 0);
    for (size_t i = 0; i < sizeof(raw); ++i) {
        out[i * 2] = hex[raw[i] >> 4];
        out[i * 2 + 1] = hex[raw[i] & 15];
    }
    out[64] = 0;
}

static void hex(const char *plain, char *out, size_t capacity)
{
    static const char digits[] = "0123456789abcdef";
    size_t length = strlen(plain);
    CHECK(length * 2 + 1 <= capacity);
    for (size_t i = 0; i < length; ++i) {
        unsigned value = (unsigned char)plain[i];
        out[i * 2] = digits[value >> 4]; out[i * 2 + 1] = digits[value & 15];
    }
    out[length * 2] = 0;
}

static void replace_once(char *text, size_t capacity, const char *old, const char *replacement)
{
    char *at = strstr(text, old); size_t prefix, suffix, replacement_length;
    CHECK(at); prefix = (size_t)(at - text); suffix = strlen(at + strlen(old));
    replacement_length = strlen(replacement); CHECK(prefix + replacement_length + suffix < capacity);
    memmove(at + replacement_length, at + strlen(old), suffix + 1);
    memcpy(at, replacement, replacement_length);
}

static void fixture_refresh(Fixture *f)
{
    char profile_hash[65], descriptor_hash[65], catalog_hash[65], input_hash[65];
    char dxvk_hash[65], metadata_hash[65];
    char input_name[129], dxvk_path[1025], key0[2049], name0[129], value0[129];
    char key1[2049], name1[129];
    const char catalog[] = "playden-steam-220.profile\n";
    const char dxvk[] = "dxgi.maxFrameLatency = 1\n";
    const char user_reg[] =
        "WINE REGISTRY Version 2\n\n"
        "[Control Panel\\\\Desktop]\n\"LogPixels\"=dword:00000078\n\n"
        "[Software\\\\Wine\\\\AppDefaults\\\\probe.exe] 123\n"
        "\"Version\"=\"win7\"\n\n"
        "[Software\\\\Wine\\\\Unrelated]\n\"Keep\"=\"yes\"\n";

    digest(f->profile, strlen(f->profile), profile_hash);
    digest(f->descriptor, strlen(f->descriptor), descriptor_hash);
    digest(catalog, strlen(catalog), catalog_hash);
    digest(f->input, strlen(f->input), input_hash);
    digest(dxvk, strlen(dxvk), dxvk_hash);
    hex("gamepad.input", input_name, sizeof(input_name));
    hex("/data/homebrew/FAKE78997/games/220/dxvk.conf", dxvk_path, sizeof(dxvk_path));
    hex("Software\\Wine\\AppDefaults\\probe.exe", key0, sizeof(key0));
    hex("Version", name0, sizeof(name0)); hex("win7", value0, sizeof(value0));
    hex("Control Panel\\Desktop", key1, sizeof(key1)); hex("LogPixels", name1, sizeof(name1));
    CHECK(snprintf(f->metadata, sizeof(f->metadata),
        "schema=2\ninstall_id=@INSTALL@\ntitle_id=FAKE78997\n"
        "data_root=/data/playden/games/@INSTALL@\nprofile_id=playden-steam-220\n"
        "runtime_bundle_id=wine-wow64\nrecipe_id=steam-220-hl2-v1\nrecipe_version=1\n"
        "settings_revision=7\nprofile_sha256=%s\ndescriptor_sha256=%s\n"
        "catalog_sha256=%s\nkit_id=kit-hl2\ninput_count=1\n"
        "input.0.name_hex=%s\ninput.0.sha256=%s\ndxvk.path_hex=%s\n"
        "dxvk.present=1\ndxvk.sha256=%s\nregistry_count=2\n"
        "registry.0.key_hex=%s\nregistry.0.name_hex=%s\nregistry.0.kind=string\n"
        "registry.0.string_hex=%s\nregistry.0.dword=\n"
        "registry.1.key_hex=%s\nregistry.1.name_hex=%s\nregistry.1.kind=dword\n"
        "registry.1.string_hex=\nregistry.1.dword=00000078\n",
        profile_hash, descriptor_hash, catalog_hash, input_name, input_hash,
        dxvk_path, dxvk_hash, key0, name0, value0, key1, name1) > 0);
    expand(f->metadata, sizeof(f->metadata));
    digest(f->metadata, strlen(f->metadata), metadata_hash);
    CHECK(snprintf(f->receipt, sizeof(f->receipt),
        "{\"schema_version\":1,\"phase\":\"committed\","
        "\"transaction_id\":\"00000000-0000-1000-8000-000000000001\","
        "\"install_id\":\"@INSTALL@\",\"title_id\":\"FAKE78997\","
        "\"kit_id\":\"kit-hl2\",\"runtime_bundle_id\":\"wine-wow64\","
        "\"recipe_id\":\"steam-220-hl2-v1\",\"recipe_version\":\"1\","
        "\"previous_revision\":6,\"settings_revision\":7,"
        "\"plan_sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
        "\"publication_sha256\":\"%s\"}\n", metadata_hash) > 0);
    expand(f->receipt, sizeof(f->receipt));
    PwTitlePublication parsed;
    CHECK(pw_title_publication_parse((const uint8_t *)f->receipt, strlen(f->receipt), &parsed) == PW_OK);
    write_file(f, "/data/homebrew/FAKE78997/game-launch.json", f->descriptor, strlen(f->descriptor), 0600);
    write_file(f, "/data/playden/games/@INSTALL@/library/profiles/playden-steam-220.profile", f->profile, strlen(f->profile), 0600);
    write_file(f, "/data/playden/games/@INSTALL@/library/profiles/profiles.lst", catalog, strlen(catalog), 0600);
    write_file(f, "/data/playden/games/@INSTALL@/library/input/gamepad.input", f->input, strlen(f->input), 0600);
    write_file(f, "/data/playden/games/@INSTALL@/library/profiles/.playden-publication", f->metadata, strlen(f->metadata), 0600);
    write_file(f, "/data/playden/games/@INSTALL@/.playden-control/publication-state.v1", f->receipt, strlen(f->receipt), 0600);
    write_file(f, "/data/homebrew/FAKE78997/games/220/dxvk.conf", dxvk, strlen(dxvk), 0644);
    write_file(f, "/data/playden/games/@INSTALL@/library/prefixes/game/user.reg", user_reg, strlen(user_reg), 0644);
}


static const char *private_dirs[] = {
    "", "/library", "/library/profiles", "/library/input", "/library/prefixes",
    "/library/prefixes/game", "/.playden-control"
};
static const char *data_files[] = {
    "/.pw-accept-owner", "/library/profiles/playden-steam-220.profile",
    "/library/profiles/profiles.lst", "/library/input/gamepad.input",
    "/library/profiles/.playden-publication", "/library/prefixes/game/user.reg",
    "/.playden-control/publication-state.v1", "/.playden-control/publication.lock"
};
static const char *title_dirs[] = {"", "/games", "/games/220"};
static const char *title_files[] = {"/.pw-accept-owner", "/game-launch.json", "/games/220/dxvk.conf"};
static void joined(char *out, size_t capacity, const char *root, const char *tail) {
    int n = snprintf(out, capacity, "%s%s", root, tail); CHECK(n >= 0 && (size_t)n < capacity);
}
static void marker_bytes(char *out, size_t capacity) {
    struct stat a, b; CHECK(lstat(data_root, &a) == 0 && lstat(title_root, &b) == 0);
    int n = snprintf(out, capacity, "PWPA1 %s 1bdebc081f872789b4bc39f1656629ef2b03d523 %llu %llu %llu %llu\n",
       nonce, (unsigned long long)a.st_dev, (unsigned long long)a.st_ino,
       (unsigned long long)b.st_dev, (unsigned long long)b.st_ino);
    CHECK(n > 0 && (size_t)n < capacity);
}
static void verify_markers(void) {
    char expected[512]; marker_bytes(expected, sizeof(expected));
    CHECK(owned(data_root, 1, 0700) && owned(title_root, 1, 0755));
    for (unsigned i = 0; i < 2; ++i) {
        char name[512], bytes[512]; joined(name, sizeof(name), i ? title_root : data_root, "/.pw-accept-owner");
        CHECK(owned(name, 0, 0600)); int fd = open(name, O_RDONLY | O_NOFOLLOW); CHECK(fd >= 0);
        ssize_t n = read(fd, bytes, sizeof(bytes)); CHECK(close(fd) == 0);
        CHECK(n == (ssize_t)strlen(expected) && !memcmp(bytes, expected, (size_t)n));
    }
}
static void create_fixture(void) {
    struct stat info; errno = 0; CHECK(lstat(data_root, &info) == -1 && errno == ENOENT);
    errno = 0; CHECK(lstat(title_root, &info) == -1 && errno == ENOENT);
    char name[1200];
    directory(data_root, 0700); directory(title_root, 0755);
    char marker[512]; marker_bytes(marker, sizeof(marker));
    joined(name, sizeof(name), data_root, "/.pw-accept-owner"); write_file(&fixture, name, marker, strlen(marker), 0600);
    joined(name, sizeof(name), title_root, "/.pw-accept-owner"); write_file(&fixture, name, marker, strlen(marker), 0600);
    for (unsigned i = 1; i < sizeof(private_dirs) / sizeof(private_dirs[0]); ++i) {
        joined(name, sizeof(name), data_root, private_dirs[i]); directory(name, 0700);
    }
    for (unsigned i = 1; i < sizeof(title_dirs) / sizeof(title_dirs[0]); ++i) {
        joined(name, sizeof(name), title_root, title_dirs[i]); directory(name, 0755);
    }
    joined(name, sizeof(name), data_root, "/.playden-control/publication.lock"); write_file(&fixture, name, "", 0, 0600);
    snprintf(fixture.descriptor, sizeof(fixture.descriptor),
        "{\"schema_version\":1,\"app_id\":220,\"settings_revision\":7,"
        "\"title_id\":\"FAKE78997\",\"install_id\":\"%s\",\"data_root\":\"%s\","
        "\"default_profile_id\":\"playden-steam-220\",\"runtime_bundle_id\":\"wine-wow64\","
        "\"recipe_id\":\"steam-220-hl2-v1\",\"recipe_version\":\"1\",\"exit_policy\":\"exit-title\"}\n",
        install_id, data_root);
    strcpy(fixture.profile,
        "[application]\nid = playden-steam-220\nname = Publication Probe\n"
        "executable = Z:\\data\\homebrew\\FAKE78997\\games\\220\\probe.exe\n"
        "working_directory = Z:\\data\\homebrew\\FAKE78997\\games\\220\n"
        "arguments = -steam\nprefix = game\nruntime = wine-wow64\narchitecture = pe64\ngraphics = dxvk\n"
        "\n[input]\npreset = gamepad\nmode = keyboard\ncross = space\n");
    strcpy(fixture.input, "[input]\nmode = keyboard\ncross = space\n");
    CHECK(pw_title_publication_descriptor_parse((const uint8_t *)fixture.descriptor,
        strlen(fixture.descriptor), "FAKE78997", &fixture.before, NULL, 0) == PW_OK);
    fixture_refresh(&fixture);
}
static int load_descriptor(void) {
    char error[256]; return pw_title_publication_read_descriptor(
       "/data/homebrew/FAKE78997/game-launch.json", "FAKE78997", &fixture.platform, &fixture.before, error, sizeof(error));
}
static int writer_open(void) {
    char name[512]; joined(name, sizeof(name), data_root, "/.playden-control/publication.lock");
    CHECK(owned(name, 0, 0600)); struct stat before, opened, after;
    CHECK(lstat(name, &before) == 0); int fd = open(name, O_RDONLY | O_NOFOLLOW); CHECK(fd >= 0);
    CHECK(fstat(fd, &opened) == 0 && before.st_dev == opened.st_dev && before.st_ino == opened.st_ino);
    errno = 0; int rc = playden_ps5_file_lock(fd, LOCK_EX | LOCK_NB); int error = errno;
    if (rc) { CHECK(close(fd) == 0); errno = error; return -1; }
    CHECK(lstat(name, &after) == 0 && owned(name, 0, 0600) &&
          opened.st_dev == after.st_dev && opened.st_ino == after.st_ino);
    return fd;
}
static void publish_pending(void) {
    char receipt_path[512], temporary[512], bytes[8192];
    joined(receipt_path, sizeof(receipt_path), data_root, "/.playden-control/publication-state.v1");
    CHECK(owned(receipt_path, 0, 0600)); int fd = open(receipt_path, O_RDONLY | O_NOFOLLOW); CHECK(fd >= 0);
    ssize_t n = read(fd, bytes, sizeof(bytes)-1); CHECK(n > 0); bytes[n] = 0; CHECK(close(fd) == 0);
    replace_once(bytes, sizeof(bytes), "\"phase\":\"committed\"", "\"phase\":\"pending\"");
    char *hash = strstr(bytes, "\"publication_sha256\":\""); CHECK(hash);
    hash += strlen("\"publication_sha256\":\""); CHECK(strlen(hash) >= 65 && hash[64] == '"');
    memmove(hash, hash + 64, strlen(hash + 64) + 1);
    PwTitlePublication parsed; CHECK(pw_title_publication_parse((const uint8_t *)bytes, strlen(bytes), &parsed) == PW_OK);
    joined(temporary, sizeof(temporary), data_root, "/.playden-control/pending.tmp");
    write_file(&fixture, temporary, bytes, strlen(bytes), 0600);
    CHECK(rename(temporary, receipt_path) == 0);
    joined(temporary, sizeof(temporary), data_root, "/.playden-control"); sync_directory(temporary);
}
static void cleanup(void) {
    int lock = writer_open(); CHECK(lock >= 0);
    const char *roots[] = {data_root, title_root};
    const char **files[] = {data_files, title_files}; const char **dirs[] = {private_dirs, title_dirs};
    unsigned file_counts[] = {8, 3}, dir_counts[] = {7, 3};
    // Validate every entry against the fixed inventory before removing anything.
    for (unsigned r = 0; r < 2; ++r) for (unsigned d = 0; d < dir_counts[r]; ++d) {
        char name[512]; joined(name, sizeof(name), roots[r], dirs[r][d]);
        CHECK(owned(name, 1, r ? 0755 : 0700)); DIR *stream = opendir(name); CHECK(stream);
        struct dirent *entry; errno = 0;
        while ((entry = readdir(stream))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            char tail[512]; snprintf(tail, sizeof(tail), "%s/%s", dirs[r][d], entry->d_name);
            int known = 0;
            for (unsigned i = 0; i < file_counts[r]; ++i) if (!strcmp(tail, files[r][i])) known = 1;
            for (unsigned i = 0; i < dir_counts[r]; ++i) if (!strcmp(tail, dirs[r][i])) known = 1;
            CHECK(known);
        }
        CHECK(errno == 0); CHECK(closedir(stream) == 0);
    }
    struct stat snapshot[11]; unsigned count = 0;
    for (unsigned r = 0; r < 2; ++r) for (unsigned i = 0; i < file_counts[r]; ++i) {
        char name[512]; joined(name, sizeof(name), roots[r], files[r][i]);
        mode_t permissions = (r && i == 2) || (!r && i == 5) ? 0644 : 0600;
        CHECK(owned(name, 0, permissions)); CHECK(lstat(name, &snapshot[count++]) == 0);
    }
    count = 0;
    for (unsigned r = 0; r < 2; ++r) {
        for (unsigned i = 0; i < file_counts[r]; ++i) {
            char name[512]; struct stat now; joined(name, sizeof(name), roots[r], files[r][i]);
            CHECK(lstat(name, &now) == 0 && now.st_dev == snapshot[count].st_dev && now.st_ino == snapshot[count].st_ino);
            ++count; CHECK(unlink(name) == 0);
        }
        for (unsigned i = dir_counts[r]; i > 0; --i) {
            char name[512]; joined(name, sizeof(name), roots[r], dirs[r][i-1]); CHECK(rmdir(name) == 0);
        }
    }
    sync_directory("/data/playden/games"); sync_directory("/data/homebrew"); CHECK(close(lock) == 0);
}
static void hold(void) {
    printf("PWPA1 run=%s mode=%s stage=ready pid=%d pass=1 rc=0\n", nonce, mode, getpid()); fflush(PWPA_OUTPUT);
    struct pollfd p = {.fd = STDIN_FILENO, .events = POLLIN}; CHECK(poll(&p, 1, 30000) > 0 && (p.revents & POLLIN));
    char command[32]; CHECK(fgets(command, sizeof(command), stdin));
    size_t n = strlen(command); CHECK(n && command[n-1] == '\n'); command[n-1] = 0;
    CHECK(!strcmp(command, "return") || !strcmp(command, "_exit") || !strcmp(command, "kill"));
    printf("PWPA1 run=%s mode=%s stage=terminating pid=%d command=%s\n", nonce, mode, getpid(), command); fflush(PWPA_OUTPUT);
    if (!strcmp(command, "_exit")) _exit(0);
    if (!strcmp(command, "kill")) { kill(getpid(), SIGKILL); CHECK(0); }
}
#ifdef PWPA_NATIVE_TITLE
/* Developer-only bridge. Existing nonce-owned fixtures only; no creation,
 * registration, Wine or UID changes from the native title. */
int playden_publication_native_probe(FILE *log, const char *run, const char *operation) {
    if (!log || !run || strlen(run) != 16 || !operation ||
        (strcmp(operation, "reader-try") && strcmp(operation, "reader-hold") &&
         strcmp(operation, "writer-try") && strcmp(operation, "cleanup"))) return -1;
    for (unsigned i = 0; i < 16; ++i)
        if (!((run[i] >= '0' && run[i] <= '9') || (run[i] >= 'a' && run[i] <= 'f'))) return -1;
    pwpa_output = log; strcpy(nonce, run); strcpy(mode, operation);
#else
int main(void) {
    setvbuf(stdin, NULL, _IONBF, 0); setvbuf(stdout, NULL, _IONBF, 0);
    char request[512], extra; CHECK(fgets(request, sizeof(request), stdin));
    CHECK(sscanf(request, "PWPA1 %16s %31s %c", nonce, mode, &extra) == 2 && strlen(nonce) == 16);
    for (unsigned i = 0; i < 16; ++i) CHECK((nonce[i] >= '0' && nonce[i] <= '9') || (nonce[i] >= 'a' && nonce[i] <= 'f'));
    CHECK(!strcmp(mode, "init") || !strcmp(mode, "reader-hold") || !strcmp(mode, "writer-try") ||
          !strcmp(mode, "reader-try") || !strcmp(mode, "writer-pending-hold") || !strcmp(mode, "cleanup"));
    #endif
    CHECK(getpid() > 0); uid_t initial = geteuid();
    /* Inventory is root-only; inspect absence before dropping the payload UID.
     * All fixture creation and admission still run as captured app owner UID1. */
    if (!strcmp(mode, "init")) {
        struct stat native; errno = 0;
        CHECK(lstat("/user/app/FAKE78997", &native) == -1 && errno == ENOENT);
    }
#ifndef PWPA_NATIVE_TITLE
    if (initial == 0) CHECK(seteuid(1) == 0);
#endif
    owner = geteuid(); CHECK(owner == 1);
    printf("PWPA1 run=%s mode=%s stage=preflight pid=%d initial_euid=%u owner=%u\n", nonce, mode, getpid(), initial, owner);
    struct stat parent;
    const char *parents[] = {"/data", "/data/homebrew", "/data/playden", "/data/playden/games"};
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(lstat(parents[i], &parent) == 0 && S_ISDIR(parent.st_mode));
        CHECK(parent.st_uid == 0 || parent.st_uid == owner);
        printf("PWPA1 run=%s mode=%s stage=parent%u pid=%d uid=%u permissions=%o\n",
               nonce, mode, i, getpid(), parent.st_uid, parent.st_mode & 07777);
    }
    /* Shared /data parents are legacy scaffolding. Match native guard policy:
     * real canonical ancestors; newly created game/control trees are private.
     * Never adopt, chmod or chown a shared parent. */
    snprintf(install_id, sizeof(install_id), "pw-accept-79cf-%s", nonce);
    snprintf(data_root, sizeof(data_root), "/data/playden/games/%s", install_id);
    fixture.platform = (PwTitlePublicationPlatform){.host_root = NULL, .owner_uid = owner,
        .try_lock_shared = pw_title_publication_ps5_lock};
    if (!strcmp(mode, "init")) create_fixture();
    verify_markers();
    if (!strcmp(mode, "cleanup")) {
        cleanup(); printf("PWPA1 run=%s mode=%s stage=result pid=%d pass=1\n", nonce, mode, getpid()); return 0;
    }
    if (!strcmp(mode, "writer-try") || !strcmp(mode, "writer-pending-hold")) {
        int fd = writer_open(); int error = errno;
        if (!strcmp(mode, "writer-try")) {
            CHECK(fd >= 0 || error == EWOULDBLOCK || error == EAGAIN);
            if (fd >= 0) CHECK(close(fd) == 0);
            printf("PWPA1 run=%s mode=%s stage=result pid=%d pass=1 rc=%d errno=%d\n",
                   nonce, mode, getpid(), fd >= 0 ? 0 : -1, fd >= 0 ? 0 : error); return 0;
        }
        CHECK(fd >= 0); publish_pending(); hold(); return 0;
    }
    void *symbol = NULL; int lookup = sceKernelDlsym(0x2001, "getpid", &symbol);
    printf("PWPA1 run=%s mode=%s stage=provider pid=%d lookup_rc=%d symbol_present=%d\n",
           nonce, mode, getpid(), lookup, symbol != NULL);
    CHECK(load_descriptor() == PW_OK); PwTitlePublicationLease lease; char error[256];
    int rc = pw_title_publication_open(&fixture.before, &fixture.platform, &lease, error, sizeof(error));
    if (!strcmp(mode, "reader-hold")) {
        CHECK(rc == 0);
#ifdef PWPA_NATIVE_TITLE
        /* Caller parks the title. Keep this production lease open until the
         * process exits; no host/socket timeout or explicit close substitutes. */
        printf("PWPA1 run=%s mode=%s stage=ready pid=%d pass=1 rc=0\n", nonce, mode, getpid());
        fflush(PWPA_OUTPUT);
#else
        hold();
#endif
        return 0;
    }
    int stage = -1; if (rc == -1) CHECK(sscanf(error, "publication admission refused at stage %d", &stage) == 1);
    CHECK(rc == 0 || rc == -2 || (rc == -1 && stage >= 0));
    printf("PWPA1 run=%s mode=%s stage=admission pid=%d rc=%d errstage=%d errno=%d\n",
           nonce, mode, getpid(), rc, stage, errno);
    if (!strcmp(mode, "init")) CHECK(rc == 0);
    if (rc == 0) pw_title_publication_close(&lease);
    printf("PWPA1 run=%s mode=%s stage=result pid=%d pass=1 rc=%d errstage=%d\n", nonce, mode, getpid(), rc, stage);
    return 0;
}
