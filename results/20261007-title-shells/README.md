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
