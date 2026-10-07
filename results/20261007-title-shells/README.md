# One PS5 title per game — 2026-10-07

Plan: [title shells on device](../../docs/superpowers/plans/2026-10-07-title-shells-on-device.md). FW 13.60, console 10.0.1.68.

## Step 0: console probes (21:17–21:35)

Probe shell: prospero-win `feat/title-shells` 665f84b, built with `PW_TITLE_PROBE=1 PW_TITLE_ID=FAKE78090`. At startup it
writes what it can learn about its own identity to `/app0/title-probe.txt`, requests the /data grant through `/app0/lapy.elf`,
records the same facts again, tries `seteuid(0)` and a root-owned write, then exits (the exit shows CE-108255-1; that is
the probe returning from `main`, not a failure). Logs are in [probe-logs/](probe-logs/).

Probe helpers: PS5-Lapy-JB-Daemon `feat/title-rule` 66ab4f1. Exact helper `--title FAKE78090`; rule helpers
`--title-rule FAKE78 --allow-list /data/title-shell-probe/rule/allow-list` and, for the out-of-range case, `--title-rule
FAKE781`.

Registration: ShadowMountPlus's API only listens on 127.0.0.1:10101, so a one-shot payload sent to the ELF loader (9021)
posted `manual/add` and `scan` (log: [shadowmount-api.log](probe-logs/shadowmount-api.log)). The first registration failed
with "TitleDir bridge unavailable" (`AppInstallAll hook is no longer installed`), the same stale-hook state seen on
2026-10-05. Reloading `shadowmountplus.elf` through Payload Manager (:8084) fixed it; Playden was closed for that.

### 1. Own title ID from inside the app — passes

| Source | Before the /data grant | After the grant |
| --- | --- | --- |
| `sceKernelGetAppInfo(getpid())` | `rc=0 title_id=FAKE78090` | same |
| `/app0` | visible | gone (ENOENT) |
| `/app0/sce_sys/param.json` | readable, `"titleId": "FAKE78090"` | gone |
| `/mnt/sandbox` listing | not visible (ENOENT) | visible, includes `FAKE78090_000` |
| `kern.proc.pathname` sysctl | ENOENT | ENOENT |
| `getcwd` | **terminates the title** (CE-108255-1); removed from the probe | — |

`sceKernelGetAppInfo` was already in the SDK's libkernel stub; no stub change was needed.

### 2. Same binary, two IDs — passes

The same `eboot.bin` (sha256 `2199f1bc…`), compiled for FAKE78090, was copied unchanged to FAKE78091 with only
`param.json` changed. Both registered and both started. FAKE78091 reported `title_id=FAKE78091`. With FAKE78090's exact
helper its grant was refused (`helper_errno=3`, ESRCH = target mismatch), as expected.

### 3. Helper rule — passes

| Case | Result |
| --- | --- |
| a. FAKE78090, listed | granted |
| b. FAKE78091, not listed | refused (ESRCH) |
| c. FAKE78091 after it was added to the list | granted |
| d. FAKE78090, list chmod 0666 | refused |
| e. FAKE78090, helper rule `FAKE781` (out of range) | refused |
| f. FAKE78119, FAKE78105 (in range, not listed) | refused |

The list owner/mode refusals (non-root owner, group-writable, writable directory, hard link, symbolic link, partial line,
too big) are covered by host tests (`tests/test_title_rule.c`); the console can't easily make a non-root file there.

The allow-list was written by the granted probe itself: after the grant the title keeps a saved UID of 0, so
`seteuid(0)` works, the file is created as uid 0 (dir 0755, file 0644, one link), and `seteuid` back to the app user
restores the effective UID. Playden already holds the same grant, so it can write the list directly.

### 4. How many titles PS5 Home shows — 30 pass

FAKE78090–FAKE78119 (30 copies) all registered: in `/user/app`, in `list_apps`, and `installed:true` in ShadowMountPlus.
Home showed the probe tiles; FAKE78105 and FAKE78119 started and logged their own IDs. No limit was reached (36 FAKE78
titles and 104 app.db entries at the peak).

### Cleanup

`manual/remove` + `games/uninstall` through the API raced with ShadowMountPlus's rescan (it re-installed FAKE78090 once,
and uninstall answered "busy" for all but the first). The direct `sceAppInstUtilAppUnInstall` payload (copy of
`probes/title-uninstall`, limited to FAKE78090–FAKE78199) then returned 0 for all 30; after the folders were deleted, a
second run cleared the last `/user/app` entries. Final state: `/user/app` and `/data/homebrew` have only FAKE77031,
FAKE78001–FAKE78006 and the other existing titles, app.db is back to 74 entries, manual sources are the five games,
`/data/title-shell-probe` is gone, and Home shows no probe tiles. Playden, its kit and the game titles were not changed.

Lesson for step 4: Playden's removal must remove the manual source and wait for the rescan before it uninstalls, one title
at a time (ShadowMountPlus refuses concurrent uninstalls with "busy").

### Decisions

- **The shell learns its ID with `sceKernelGetAppInfo(getpid())`** at startup: it works before and after the grant and is
  the same call the root helper trusts. The compiled `PW_TITLE_ID` stays as the fallback when the call fails or returns
  something that is not a title ID. No file next to the shell is needed.
- **Playden writes the allow-list itself** (temporary `seteuid(0)` after its grant) instead of a second helper request
  kind. The wire protocol and its hash stay unchanged; the rule helper only adds a check.

## Steps 1–4: code and kit (2026-10-07, host-tested)

Branches (local, not pushed):

| Repository | Branch | Tip | Base |
| --- | --- | --- | --- |
| prospero-win | `feat/title-shells-kit-a` | 7c50df5 | `feat/playden-ps5` 394bba1 (kit A line) |
| prospero-win | `feat/title-shells` | 87ebf18 | `feat/upstream-rebase` 4298adf (kit B line) |
| PS5-Lapy-JB-Daemon | `feat/title-rule` | 66ab4f1 | `feat/playden-ps5` a8f391b |
| Playden | `feat/title-shells-on-device` | see `git log` | main cf42395 |

Kit B failed on the console for reasons unrelated to this work (its shells return to Home), so the kit for step 5 is
built on kit A. `feat/title-shells` carries the same commits on the kit B line for later.

### Shell (prospero-win)

- `src/pw_title_id.c`: the shell asks `sceKernelGetAppInfo(getpid())` for its title ID at startup. The sandbox path,
  restart path, descriptor check and log name use that ID. `PW_TITLE_ID` is only the fallback. Host test
  `test_pw_title_id`.
- **The saved root UID is dropped after the /data grant.** Probe 1 showed that after the grant a title can
  `seteuid(0)` and create root-owned files. A game could therefore add titles to the root-owned allow-list. Now
  `pw_data_mount` sets the real, effective and saved UIDs to the app user with `setuid` while it is still root, and fails
  the grant if `seteuid(0)` still works. A host model of FreeBSD's rules is in `test_pw_data_mount`; the console proof
  is in step 5.
- `PW_ANY_TITLE=1` (`tools/build_native.sh`): one shell for every title. `PW_TITLE_ID` becomes the template ID
  (FAKE78000), the Lapy helper must be a title-rule helper whose range holds it, and `native-title-build.json` records
  `any_title` and the rule.
- Packager: one `any_title` variant per kit together with the `title-any-id` capability; the rule helper is pinned
  as `FAKE78*`; Lapy 66ab4f1 is an accepted helper revision with its own base (b48b7d7). Fixed variants are unchanged.
  Tests: `tests/test_playden_kit.py` (47 on the kit A line), `tests/test_fetch_lapy_helper.py`.

### Root helper (Lapy)

- `--title-rule FAKE78 --allow-list /data/playden/title-rule/allow-list` builds a helper that grants a caller whose
  `sceKernelGetAppInfo` title is FAKE78 plus three digits **and** is a whole line in the allow-list. The list and its
  directory must belong to root and be writable by nobody else, the list must be a regular file with one link and
  under 64 KiB. Each decision is logged (`title_rule … result=`). Exact-title helpers and the wire protocol are
  unchanged (protocol sha256 still `bb02c4aa…`). Tests: `tests/test_title_rule.c`, `tests/test_owned_daemon_title.py`.
- Rule helper sha256 `17f7fc80688273aa7ef807de8f7040aabaf307049125abb460bcde5011ce1792` (reproduced by two builds).

### Playden

- Kit admission: an `any_title` variant needs `title-any-id`, a FAKE78 rule helper reading Playden's allow-list
  (`runtime::kTitleRuleAllowList`) and a shell built for that rule. A build that doesn't know the capability refuses
  the variant. `KitCapabilities::title_any_id`.
- `KitVariantFor`: a title uses its own fixed variant, otherwise the any-title shell (never for the template ID).
- New games: `KitTitleCandidates` offers fixed titles first and then FAKE78001–FAKE78999. The repository takes the
  lowest one that has no record, no console registration and no folder. Games on fixed titles keep them. A title is
  reused after its game is removed. A reservation survives restarts. The repository's record limit (128 games) is now
  the only bound.
- Title parameters: the any-title shell's `param.json` gets the game's `titleId`, `conceptId` and `contentId` as well
  as its name and icon. The installer refuses parameters that name another title, and never stages the template's.
- Update moves a game prepared on a fixed kit title onto the any-title shell (same title and bottle).
- Allow-list: `RuntimeHost::SyncTitleAllowList` writes every recorded game title to the list at startup, after a new
  title is reserved (before it is staged) and after a removal. `ps5::WriteTitleAllowList` raises the effective UID
  to 0 only around `mkdir`/create/`rename` (the effective UID is process-wide) and syncs as the app user. Host test:
  `title_allow_list_test` (user stands in for root).
- Host tests: 117/117 (`ctest --test-dir build/homebrew-host`), including the new installer, repository,
  admission, allow-list and runtime-host cases.

### Kit C

`build/kit-c/scripts/make_kit.sh` (not tracked) builds the rule helper and the any-title shell, and packages kit A's
runtime, template and recipe with the one any-title variant. It ran on the Mac; no VM was used.

| | kit A line (for step 5) | kit B line |
| --- | --- | --- |
| directory | `build/kit-c/kit-c-kit-a-7c50df5/kit` | `build/kit-c/kit-c-e7a7683/kit` (packaged before the base-check fix) |
| manifest sha256 | `508c36b382c3626f4bcfa2ca8f8b9eef76fea997107c9a47b3b5af7fa8f3a9dc` | `3f2de1d7bbe7896a8fcf0822966efd8f03657489b01635022489d36761ceb8dd` |
| files | 3,548 (kit A 3,638 minus the ten titles plus one) | 3,551 |
| capabilities | dxvk, offline_bottle, per_game_title, title-any-id, wine-shared-sections, wow64_i386 | same |
| Playden admission | kVerified, 0 ignored capabilities | — |
