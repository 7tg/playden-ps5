# prospero-win investigation: packaging and installing downloaded games

Written 2026-10-05 (roadmap step 2). Read-only survey of `../ps5-homebrew/prospero-win` (PW below) and the macOS reference at `de952b3d`. Paths are relative to each repository. Items marked **unknown** were not established from the sources.

## What prospero-win is

- One native PS5 folder app, title `PPSA99995`, installed at `/data/homebrew/PPSA99995` and registered by ShadowMountPlus (`docs/GETTING_STARTED.md:25-39`, `tools/package_release.sh`). Wine's Unix side loads as PRX modules; 64-bit games run natively, 32-bit through its `wowprospero` translator; Direct3D 8–11 through DXVK on Vulkan, optional OpenGL, GDI for 2D (`README.md:89-103`, `docs/ARCHITECTURE.md`).
- At start it sends a bundled Lapy helper to elfldr (port 9021) for `/data` access; the helper is built for PPSA99995 only.
- Status: experimental, tested on one console (firmware 12.02). Playable list in `COMPATIBILITY.md`. Limits: no process creation on the console (no installers, launcher stubs or child processes), no DNS, GDI frames up to 1280×1024, library at most 32 entries, `profiles.lst` at most 16.

## How a game is laid out and launched

- Root `/data/prospero-win` (fallback `/download0/prospero-win`): `prefixes/<slug>/`, `profiles/<slug>.profile` plus `profiles/profiles.lst`, `input/<preset>.input`, `logs/session-N.log`.
- A profile is a strict INI (`src/pw_app_profile.h`, `src/pw_game_profile.h`; writer example `tools/pw_install.py:436-472`): `[application]` id, name, absolute Windows `executable`, `working_directory`, `arguments`, `dll_overrides`, `prefix`, `runtime = wine-wow64`, `architecture`, `graphics` (gdi/dxvk/opengl/auto); `[display]`; `[input]` (keyboard or xinput, bindings, mouse-stick preset); `[debug]`.
- `C:` is the prefix's `drive_c`, `Z:` the host root. Games outside the prefix (`Z:\data\...`) look possible in code but are **untested**.

## Handing a game to prospero-win

- No IPC, command file, socket or URL scheme exists. PW reads its own launch arguments (`profile=<id>`, `path=<exe>`, `cycle=<n>`; `src/pw_wine_launch.h`) and restarts itself with `sceSystemServiceLoadExec`. After a game exits it always returns to its own launcher; no exit status reaches a caller. Status is only in `logs/` and ps5log.
- **Unknown:** whether `sceSystemServiceLaunchApp("PPSA99995", argv)` from FAKE77031 delivers arguments, and how control returns to Playden.

## Installation model

- Games live inside the one PW installation, not as separate PS5 titles.
- Prefixes cannot be created on the console (`wineboot` blocked, build patch 0550). Today they are built on a Linux PC with the pinned host Wine and Lutris-style recipes (`tools/pw_install.py`; Steam sources unsupported), then pushed with `tools/pw_prefix.py push`, which also: switches `HKLM\...\Wow64\x86` to `wowprospero.dll` and places the DLL, converts symlinks to `.pw-symlinks` files, creates `C:\users\prospero`, and appends `profiles.lst`.
- Each prefix carries about 300 MB of Wine DLLs plus the game. Redistributables are only handled by PC-side winetricks recipes. Filesystem case sensitivity and exFAT/UFS are undocumented. Steamworks/DRM handling is undocumented; PW states it does not bypass DRM.

## What the macOS reference does (to replicate)

- Installs to `<volume>/Playden/games/playden-steam-<appid>/game/` with `.playden-install.json`, outside a per-game bottle cloned from a template (`InstallStorage.swift`, `CrossOverGameBottles.swift`).
- Launch config from PICS `config/launch` entries (executable, arguments, workingdir, oslist, osarch), filtered in `SteamInstallPlan.swift`; launched via `cxstart` with workdir and DLL overrides.
- Steamworks: replaces `steam_api(64).dll` with gbe_fork and writes `steam_settings/` (`SteamCore/Prepare.swift`); unpacks SteamStub with Steamless inside Wine; pins prerequisites per game (`SteamRecipes.swift`).

## What Playden on the PS5 would have to do

Depot download to `/data`; app PICS launch entries (text VDF, deferred from the CM work); create a prefix without console `wineboot` (for example copy a PC-built template prefix); apply the `pw_prefix.py` conversions; write the `.profile` and `profiles.lst` entry; gbe_fork swap; and hand off to PW. SteamStub unpacking and redistributable installers cannot run on the console (child processes, .NET), so they need PC-side preparation or must be excluded.

## Runtime attempt on the console (2026-10-05)

Tried to start prospero-win (PW, PPSA99995) on the dev PS5 to validate the handoff. Two blockers were found; the first is solved, the second is PW's own and remains open.

### 1. Execute bit on the application image (solved)

Every launch of PW failed at the system level with `Can't start the game or app` (0x80aa001a), before any PW code ran. The cause was file permissions: files uploaded to `/data/homebrew/PPSA99995` over the web file manager land as `rw-` (no execute bit), while a working folder title (PPSA99050) has `rwx` files. Replacing PW's `eboot.bin` with Playden's own known-good eboot reproduced the same refusal, confirming it was not PW-specific. Setting the whole folder to `0777` (`/api/chmod ... recursive=1`) let PW start: it now reaches its own runtime and shows its launcher error UI instead of the system refusal.

This is a packaging/deploy fact, not a code change: a title's `eboot.bin` (and `sce_module/*.prx`) must be executable on the console. Playden's own deploy path already sets this; a note was added so PW and any future runtime image are chmod'd on upload.

### 2. Lapy data-mount helper does not complete (open, PW's own dependency)

With the execute bit fixed, PW starts and then stops itself at its data-mount step:

```
PW_WINE64 data_mount data_before=0 helper_completed=0 helper_errno=54 ... root=unavailable
PW_WINE64 startup blocked reason=data-helper-incomplete
```

PW streams a bundled one-shot ELF (`lapy.elf`) to an `elfldr` listener on `127.0.0.1:9021`, which must clone root credentials so PW can mount `/data`. On this console `elfldr` was not running; starting the standard `elfldr` v0.26 payload (checksum-verified against the payload-manager repository, port 9021 confirmed open) did not fix it — the helper still returns `errno=54` (ECONNRESET): the loader accepts the ELF and closes without the handshake completing. PW's helper (`PS5-Lapy-JB-Daemon`) is a firmware-specific privilege-elevation daemon gated to firmware 12.02 (`offsets_v940`, `kernel_get_fw_version() != 0x12020000` guards). This is PW's own infrastructure, not Playden's; per the project constraints PW is not modified here. Resolving it needs the PW owner (a helper/loader build that completes on this console's firmware), and is tracked as decision 5 below.

Conclusion: PW cannot yet run a game on this console because of its own helper, independent of anything Playden produces. Playden's side of the handoff — drafting a valid PW profile from an installed game — is implemented and tested (`src/runtime/prospero_profile.{h,cpp}`, `tests/prospero_profile_test.cpp`): `DraftProsperoProfile` emits PW's strict `[application]` INI (Z: paths, Steam launch args, PE32/PE64 from the executable header, shared prefix) and `PublishProsperoProfile` writes it into `profiles/` and `profiles.lst` within PW's limits. When PW can mount `/data` on this firmware, the remaining work is a prefix and the LaunchApp handoff (decisions 1–2).

## Decisions needed (user and prospero-win owner)

1. Handoff: LaunchApp with `profile=` arguments, or a request file/IPC added to PW? Return path to Playden and exit status?
2. A published template prefix, or on-console prefix creation?
3. Games outside the prefix to share one copy of Wine DLLs; shared `prefix = default`?
4. The 32-entry library and 16-entry `profiles.lst` limits.
5. `/data` access for FAKE77031 (the Lapy helper accepts PPSA99995 only).
6. Steamworks (gbe_fork acceptable?), SteamStub and redistributables without process creation.
7. Filesystem case sensitivity and exFAT images.
8. Which runtime build (with or without OpenGL) to target.
9. Other stores: the PRD defers them; confirm whether v3 stays Steam-only.
