# One PS5 title per game — 2026-10-07

Plan: [title shells on device](../../docs/superpowers/plans/2026-10-07-title-shells-on-device.md). FW 13.60, console 10.0.1.68.

## Final outcome — completed 2026-10-08 Europe/Berlin

Kit C on fixed kit B passed the accepted console checks. Half-Life 2, Super Meat Boy, Pizza Tower and Hexcells
launch from Home using one any-title shell. Hexcells reached **FAKE78011**, then reused the lowest free
**FAKE78005** after cleanup and a console-discovered ShadowMount tombstone fix. The final action started
Hexcells from Home successfully; it was left running when the console was released.

140 (01) and Look Outside (06) retain their original executable/helper/metadata hashes. All dummy/probe titles
and scratch folders are gone; exactly six real FAKE78 titles remain. Kit backups were preserved. **Option A
retains root recovery by game code**; the allow-list is not isolation. No pushes, PRs or merge commits.

See the [resumed acceptance](#resumed-acceptance-fixed-kit-b-and-option-a-2026-10-07) below for current kit hashes,
source revisions, tests and evidence. Earlier stopped kit-A/drop experiments are historical.

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

## Step 5: console acceptance — stopped (22:19–22:24)

- Deployed Playden (fa4d59e package; backup `build/console-backups/20261007-title-shells`) and kit C on kit A
  (`508c36b3…`, 3,549 files verified).
- Saved-UID proof with the probe shell built from `feat/title-shells-kit-a` 7c50df5 (exact FAKE78090 helper),
  [log](probe-logs/5-saved-uid-dropped.txt): the helper completed (`helper_errno=0`) but the drop did not take:
  `credential_errno=1`, and `seteuid(0)` still worked afterwards (`root-write seteuid=0 … file_uid=0`). On the
  console, `setuid(app user)` from root does not clear the saved root UID. So `pw_data_mount` fails the grant
  closed, and every title built from this shell would fail its /data grant. Kit C was therefore not tested with games.
- Restored: kit A back in place (manifest `1fe7c211…`; kit C kept as `.hl2-fw1360-dev1.retired-kit-c-508c36b3`),
  Playden restored from the backup, FAKE78090 unregistered and deleted, `/data/title-shell-probe` removed. `/user/app`
  has FAKE78001–FAKE78006 only. Games were not touched.

Open decision: how to keep game code from regaining root after the grant (see the report), and whether kit C ships
without the drop until then.

## Resumed acceptance: fixed kit B and option A (2026-10-07)

The earlier kit-A and stopped-acceptance sections above are historical. This run uses fixed kit B
`build/kit-b-25a967d` plus the any-title shell. All five PS5 MCP tools were present, and the console was claimed.

### Option B rejected by console evidence

- [Probe 6](probe-logs/6-helper-drop-root.txt): the exact FAKE78090 drop helper completed, but the shell
  rejected the grant (`credential_errno=1`); `root-write seteuid=0 … file_uid=0`.
- [Helper log](probe-logs/6-helper-drop-ps5log.txt): `root_dropped … uid=1 ruid=1 svuid=1 kept_authority=1`.
  Thus the helper really wrote and verified the UIDs; retaining Sony privileges still permitted root recovery.
- [Probe 7](probe-logs/7-original-authority.txt): restoring all original Sony credentials blocked
  `seteuid(0)` (`errno=1`), but `/data/homebrew` could not be opened (`errno=1`).
- [Probe 8](probe-logs/8-original-caps.txt): restoring just the original capabilities, retaining grant authid
  and attributes, produced the same denial. `data_after=1` only proves the `/data` stat, not useful file access.
- These are bounded probe-only experiments, not a complete mapping of firmware capabilities. The specified
  option B cannot meet its gate; the authorized option A fallback is used. Games retain the existing ability
  to regain root. The allow-list does not isolate running game code. No claim of a secure drop is made.
- FAKE78090 was removed from ShadowMountPlus manual sources, uninstalled (`result=00000000`), and deleted.
  It is absent from `/user/app` and `/data/homebrew`; `/data/title-shell-probe` was removed.

### Build and recovery record

- Lapy `feat/title-rule`: `9adc6b7` reverts the ineffective drop; sources match `66ab4f1`.
- prospero-win `feat/title-shells`: `850e62c` removes the failing shell drop gate and accepts that helper revision.
- Kit: `build/kit-c/kit-c-kit-b-850e62c/kit`, 3,551 manifest files, one any-title variant.
- Manifest SHA-256: `8943e45e5e1b024f075e830753ab0ebcb52ae89134d4606365c53223c33aea97`.
- Files SHA-256: `c836be09e758c46ef57b17ef58e3cfabc35dcfeb7f7c97edef586beeb0beef29`.
- Shell SHA-256: `ad96e879757d71195006030bce26cf9ea1b5a9ceceac196ba1e0c909e1b5ee14`.
- Rule helper SHA-256: `17f7fc80688273aa7ef807de8f7040aabaf307049125abb460bcde5011ce1792`.
- Runtime/template/recipe come from fixed kit B's spec. Runtime remains `bea2be5`; native WoW64 fallback is
  inherited from shell base `25a967d`. Reproducible build script: [make-kit-c.sh](make-kit-c.sh), default `BASE=kit-b`.
- Original Playden backup: `build/console-backups/20261007-title-shells-kit-c-b`.
- Host: 117/117 after rebase and again after the prospero source pin changed; Lapy 51 Python tests and C
  title-rule test pass; prospero 46 kit tests, 5 helper tests and title-ID/data-mount tests pass.

For FAKE78011, temporarily occupied FAKE78005 after removing Hexcells as well as 07–10: otherwise
lowest-free allocation must reuse 05. These were empty directories, without title metadata or registration.

### Existing-title Home launches

Playden updated Pizza Tower (04), Hexcells (05), Super Meat Boy (03), then Half-Life 2 (02).
All four remained on their original IDs and have identical shell and helper hashes:
[read-back hashes](console/updated-title-hashes.json). Every native build manifest reports
`any_title=true`, `source_dirty=false`, and shell revision `850e62c`.

All four started using PS5 MCP `launch`, after closing Playden. Each session log reports its actual
`FAKE7800x` ID, `source=kernel compiled=FAKE78000`, `origin=home`, and a successful `/data` grant.
Evidence in [console/](console/): Half-Life 2 and Hexcells menus, Super Meat Boy title screen, Pizza Tower
animated intro, and session-log excerpts. This is launch acceptance, not an extended gameplay test.

The installed kit B was preserved as `/data/playden/kits/.hl2-fw1360-dev1.retired-c836be09e758`.
No kit backup was deleted. Upload verified 3,552 files (including manifest) and 119 directories.

The real-kit trust test initially rejected the one-shell kit because it still hardcoded ten variants.
It now checks the single any-title variant and resolution of FAKE78011; re-running against kit C passes.

### Past ten and recovery checks

- Hexcells was removed with `delete_saves=0`; the UI confirmed retained saves and settings.
- With empty 05 and 07–10 occupying the lower free slots, preparation allocated **FAKE78011**.
- After leaving Playden, PS5 Home registered Hexcells as FAKE78011. MCP `launch(FAKE78011)` reached its
  save-slot menu: [screenshot](console/hexcells-11.jpg), [session](console/hexcells-11-session.log.excerpt.txt).
  The session reports `id=FAKE78011 source=kernel compiled=FAKE78000`, `origin=home`, successful grant.
- [Read-back hashes](console/FAKE78011-hashes.json) match the same kit C shell and helper as the four updated games.
- All five dummy directories were checked empty and removed using FTP RMD only:
  [cleanup](console/dummy-cleanup.txt). No fake registered titles were used for the occupancy test.
- [140 and Look Outside verification](console/preserved-games-verified.json): executable, helper and `param.json`
  exactly match the pre-run hashes. Neither game was updated, removed or launched by this acceptance run.
- Review found two allow-list write recovery defects. Existing records now re-sync before update/resume;
  failed reservations finish missing artifact/metadata initialization on retry without replacing bound artifacts.
  Regression tests cover reservation failure/retry and startup failure/refused update/recovery. Review of the fix
  found no remaining Important/Critical issues. Final full host suite: **117/117**, 54.98 seconds.
- Final Playden package includes the recovery and tombstone fixes; deployed with backup
  `build/console-backups/20261007-title-shells-reuse`. Original pre-task backup remains `...-kit-c-b`.

### Source revisions

| Repository / branch | Verified implementation tip |
| --- | --- |
| Playden `feat/title-shells-on-device` | `8e5ba1957f982c4bda6df7a40de4de9295e5215b` (followed by this acceptance-evidence commit) |
| prospero-win `feat/title-shells` | `850e62c9243c551684dfea5dada7bb2689a9ba4f` |
| Lapy `feat/title-rule` | `9adc6b72ced860e4630d9c3c53271da094eea291` |

Playden's source submodules are prospero-win `850e62c` and prospero-foundation `98f5582`.
The separate rule-helper worktree is used by the kit builder; Playden's exact-title helper stays unchanged.
Final Playden executable SHA-256: `b4174e12854be3f4b56ba8243f7a4a8cd122e1d001aafa498b43497ffa885898`.
The final Playden branch tip is the commit containing this evidence; its full hash is in the Chauffeur completion report.

### Console-discovered reuse bug

The first attempt after dummy cleanup allocated 07 instead of 05. ShadowMount kept removed 05 and 11 as
[unmanaged tombstones](console/shadowmount-tombstones.json) (`installed=false`, `managed=false`, `mounted=false`,
`source_available=false`). Playden treated every such row as a collision. The registry now ignores only this
explicit tombstone with canonical source/runtime paths and folder type. Missing/malformed flags, other paths/types,
live rows and duplicates still block reuse. The coordinator still independently requires the title folder absent,
native database free, and activity stopped. Tests cover all these guards, plus add/scan for a reclaimed tombstone.
The intermediate 07 preparation was removed through Playden with saves retained. On the corrected build,
Hexcells re-preparation stamped **FAKE78005**, the lowest free ID:
[metadata read-back](console/FAKE78005-reused-param.json). Only 01–06 title folders remained.

### Final reuse, Home launch and cleanup

- Playden completed preparation at `2026-10-07T22:07:41Z` and displayed
  [PS5 title Ready · FAKE78005](console/hexcells-reused-ready.jpg).
- Closed Playden, then MCP `launch(FAKE78005)` reached the Hexcells save-slot menu:
  [final screenshot](console/hexcells-05-final.jpg), [final session](console/hexcells-05-final-session.log.excerpt.txt).
  The log reports `id=FAKE78005 source=kernel compiled=FAKE78000`, `origin=home`, and a successful grant.
- [Final inventory and hashes](console/final-inventory.json): exactly FAKE78001–FAKE78006 registered and present;
  updated games 02–05 match kit C, 01/06 match their pre-run bytes, kit C manifest matches the local build.
- [Runtime acceptance log](console/runtime-acceptance.log) records removals with `delete_saves=0`, preparation
  and admission checks. [Helper evidence](console/helper-rule.log.excerpt.txt) shows the rule admitted 02–05 and 11.
- Empty dummies 05/07–10 were removed with RMD, intermediate 07 and 11 were removed through Playden, and probe90
  was uninstalled. `/data/title-shell-probe` and `/data/title-shell-acceptance` were removed after evidence export.
  The local ps5log listener was stopped. No kit backups were deleted.
- Console released successfully after the final Home launch, leaving Hexcells running. No Steam/Playden sign-out.
- Verification: [117/117 host tests](console/host-tests.txt), [tombstone/coordinator checks](console/reuse-focused-tests.txt),
  actual kit C admission, PS5 build and verified [deployment](console/playden-deploy.txt). Review found no remaining
  Important/Critical defects in the recovery and tombstone changes.

This proves launch past the old ten-ID boundary and lowest-free reuse using temporary occupancy, as accepted in
this handoff. It does not claim twelve concurrently installed real games or extended gameplay coverage.
