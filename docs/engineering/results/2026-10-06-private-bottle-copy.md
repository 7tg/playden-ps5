# App-owned bottle copying: implementation and console evidence

Playden must create its private per-game directories as its ordinary effective user, then copy validated kit contents into them. FTP may populate an upload/staging area; it must not create the final private bottle. Existing public or foreign-owned private paths are rejected without changing ownership, permissions, or saves.

## Implemented in Playden

The package installer creates private staging and the per-game `library/prefixes/game` hierarchy with descriptor-relative, no-follow operations. Private directories require the current effective UID and mode `0700`. Both fresh copies and later ownership admission check every directory from the game root through the bottle. Template files are opened exclusively, assigned mode `0600` (plus owner execute where the source requires it), and checked for owner/type/mode before payload bytes are written. Hash and durability barriers remain required.

Profile publication creates private `profiles` and `input` directories before invoking the OWNER adapter. Readiness also verifies private directory ancestry and UID/mode `0600` for profiles, catalogs, metadata, and referenced input presets. The public title descriptor retains its title policy. A failed initial staging marker write cleans only the just-created directory and its initial regular marker after identity checks; it does not recursively delete an unidentified tree.

Host regression tests cover mode drift on intermediate directories, preservation of existing content, a partial marker write injected using `RLIMIT_FSIZE`, retry after that failure, and refusal of a publication callback that writes a public profile. All 76 host tests passed. The SDK package build with the developer probe disabled and the SDK package-installer static library compilation passed. Static library compilation alone does not prove its final native link or console execution.

## Console evidence and remaining gate

Developer diagnostics ran against the isolated `FAKE78001` / `acceptance-hl2-fresh-v16` fixture. Native `fstat`, rather than FTP's reported ownership, supplied the observations:

- Ordinary app EUID was `1` after bootstrap restoration.
- The original empty UID-0 FTP scaffold was preserved as `acceptance-hl2-fresh-v16.ftp-staging`; unrelated game directories and `/data/playden` were not moved.
- A fixed developer-only fixture path created with ordinary `mkdir` had app-owned `0700` directories. Copying its validated descriptor produced an app-owned `0600` file, with file/directory sync and exact readback succeeding.
- The production descriptor-relative directory walk still failed: relative `openat` returned `EINVAL` on an existing directory FD. Absolute `openat(AT_FDCWD, ...)` succeeded. A libkernel syscall gateway validated against `getpid` did not resolve the relative-path failure. The fixed fixture proof is therefore **not** proof that the production copier works on PS5, and no generic path-based fallback was enabled.
- The wrapper subsequently created Home session `3771c232-a7b7-47eb-af39-fdb231176185` and recorded `failed` when the fixture lacked profiles/input and a complete runtime. The session was readable by Playden, UID `1`, but mode `0644`; the wrapper session writer must be repaired to `0600` and retested. No gameplay or ready install is claimed.

Local raw evidence: `build/ownership-api-20261006/title-contract.log` and `wrapper-private-tree.log`. The normal `FAKE77031` preview was restored with byte-for-byte readback after diagnostics and the PS5 claim was released. The new production build has not been deployed for user acceptance.

Next: resolve the native relative directory API contract; then connect the validated installer, OWNER publication, and launch services to the live application and run the full installation/gameplay acceptance plan.

## Wrapper privacy repair and acceptance

The upstream `feat/playden-per-game-title` worktree now creates session log directories as `0700`, session temporary records/reservations as `0600`, and verifies file ownership/type/mode before writing bytes. Existing public or foreign-owned session directories are refused rather than changed. Session phase updates also refuse a log directory that has become non-private. Host lifecycle assertions first reproduced the `0644` record and now pass, including a public-parent rejection and later mode drift without advancing the recorded phase.

The patched wrapper rebuilt successfully with Lapy `611fa5f` and native foundation `9c0b994` (SDK v0.42). Developer artifact: `dist/FAKE78001-playden-private-session/eboot.bin` in that upstream worktree, SHA-256 `0dc9860a1a4634895963cfe077c7323c32df60a6d64dbbf0fdce884fafd17e76`. The per-game wrapper and private lifecycle are committed at upstream `3fa908c` after independent review. This remains a development source pin; final kit/runtime provenance and full acceptance are open. Console acceptance below verifies their privacy behavior; real runtime/gameplay/normal exits remain open.


## Console follow-up: privacy confirmed; relative API still unavailable

The developer diagnostic preserved the fixture's old app-owned `0755` logs as `logs.before-private-session`, after checking the exact fixture identity, private root, prior failed record, and absence of the destination. The rebuilt wrapper then created Home session `084963e7-2dea-486e-9104-e679c1d70904`. Playden confirmed private root/logs/sessions directories and read the resulting failed session record: UID `1`, mode `0600`. Directory sync, session atomic replacement, and cross-title private-record access therefore worked for this failed startup case. The fixture still lacks managed profiles/input and a complete runtime; its native startup fails closed. No ready install or gameplay is claimed.

The additional read-only syscall comparison opened `/data` both through libc and directly through the validated syscall gateway. Both directory FDs were valid according to `fstat`, and every relative `openat` flag combination still returned `EINVAL`; absolute paths succeeded. This rules out the tested libc-versus-kernel FD-origin hypothesis. It does not prove why the console refuses relative paths, nor admit a generic path-based fallback.

[Trimmed native and wrapper evidence](../../../results/20261006-private-session/README.md). After testing, the normal preview executable and identity were restored with exact readback, the diagnostic was closed, and the console was released. The isolated `FAKE78001` source retains the privacy-fixed developer wrapper. The launcher developer probe is disabled in the normal SDK build.


## Next runtime build dependency prepared

A separate foundation worktree, `.worktrees/prospero-foundation-prx` on `feat/playden-wine-prx`, starts from `30597512539e7edfde079cbcaf4a626bc0a948c5` (the module-export converter required by Wine). Its host `ps5-native-tool` compiled using LLVM18 and host zlib. The shared native-title foundation remains exactly `9c0b994`.

An initial module test failed because its independent reader still queried hash buckets with encoded symbol IDs. The upstream `fffeab6` hardware-backed fix deliberately hashes library/module names while retaining encoded symbol strings. The isolated branch updates the reader and module documentation to that published contract; it does not change the converter behavior. All 9 module-writer checks now pass; the isolated test/documentation correction is committed at `182f1ea`. Host module checks do not establish FW13.60 loader acceptance. The settings peer also reported Wine patch `0895` (`7182441`) fixing the generated header inventory so the pinned Wine ELF stage can compile; its PRX output still needs reproduction and validation. Retained runtime `e16ec8f` and kit capability gates are unchanged.

## Runtime build and filesystem follow-up

The header fix is now adopted on the per-game wrapper branch at `3d05a57`. The
isolated Wine build completed with process exit 0 after supplying all host tool
dependencies. All 11 PRX and 14 selected PE DLL hashes were checked against disk;
see [build inventory and limits](../../../results/20261006-wine-build/README.md).
This is not a complete kit or console loading acceptance; graphics driver inputs
and full runtime/template/provenance acceptance remain open.

The additional read-only native probe also ruled out the tested `/dev/fd` and
proc descriptor aliases (all `ENOENT`). The same directory API gate affects
settings loading/saving, registry/DXVK/profile application and cache clearing;
their reasons and implementation/acceptance work are recorded in the
[settings catalogue](../wine-bottle-settings.md#native-persistence-gate-2026-10-06).
No remembered-path substitute was enabled. The normal preview was restored and
the PS5 released after the test.
