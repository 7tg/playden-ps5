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

Next: repair and retest private wrapper session records; resolve the native relative directory API contract; then connect the validated installer, OWNER publication, and launch services to the live application and run the full installation/gameplay acceptance plan.
