# More games without a recipe: graphics defaults and Steamworks emulation — plan

Written 2026-10-07 on `feat/new-games`. Not implemented yet. Covers options 1 and 2 from the BALL x PIT review, plus the
session-record bug that hides every failure of a game without a shipped recipe. Builds on
[ready to play](2026-10-07-ready-to-play.md) (in progress on `main`); rebase on `main` once that lands, because both
touch `RuntimeHost` prepare and the controller.

## What BALL x PIT showed

Run on 2026-10-07 ([evidence](../../../results/20261007-game-removal/README.md),
[title log](../../../results/20261007-game-removal/title-FAKE78001.log)). Steam app 2062430, 64-bit Unity, `Balls.exe`.

1. **Wrong graphics.** `GenerateGameRecipe` (`src/runtime/game_settings.cpp:864`) always writes `graphics = auto` and
   no DLL overrides. prospero-win's `graphics` does not add overrides (`pw_app_profile_effective_dll_overrides`,
   `third_party/prospero-win/src/pw_app_profile.c:166`); HL2 reaches DXVK only through its recipe's `d3d9=n`. The log
   shows `dll_overrides=-`, then Wine's builtin `wined3d`, `dxgi`, `d3d11`, `UnityPlayer.dll`, and the log ends during
   Direct3D start-up (CE-108255-1). The template bottle already holds DXVK 2.6.2 for both architectures
   (`system32` and `syswow64`: `d3d8`, `d3d9`, `d3d10core`, `d3d11`, `dxgi`), so this is a recipe change, not a kit change.
2. **No session evidence.** `session-owner-invalid` ("managed bottle ownership marker is invalid") then
   `launch-no-evidence`. Cause: `OwnerMarker` (`src/runtime/package_installer.cpp:291`) writes `recipe_id` and
   `recipe_version` from the **kit** recipe (`steam-220-hl2-v1`), while the session reader
   (`src/runtime/session_storage.cpp:150`) compares them with the **install record** (`steam-2062430-auto`). The
   installer's own checks pass because they compare against `OwnerMarker` too. HL2 is unaffected (record and kit recipe
   are the same). No test reads session evidence for a generated recipe (`tests/homebrew_runtime_host_test.cpp:740`).
3. **No Steam.** The bottle has no Steam client or emulator; HL2 runs with its own `steam_api.dll` and tolerates the
   failed Steam start. Most Steamworks games quit or restart through Steam when `SteamAPI_Init` /
   `SteamAPI_RestartAppIfNecessary` fail. The log ends before `steam_api64.dll` loads, so this was not BALL x PIT's
   crash yet, but it is the next likely blocker.

Facts that bound the design:

- The console cannot start child processes (`NtCreateUserProcess` unsupported, title log line 34). Installers,
  launchers, Steamless and redistributable setups cannot run on the console.
- D3D11 through DXVK passed prospero-win's console controls (x64 and x86, `docs/WINE_PS5_BUILD.md:745`), but no real
  D3D11 game has run yet. BALL x PIT would be the first.
- The template has no Wine Mono (`drive_c/windows/mono` absent); Wine's `d3d12` (vkd3d) is present but untested.
- Cloud already expects the emulator's layout: bare Cloud names map to
  `drive_c/Program Files (x86)/Steam/userdata/0/<appid>/remote` (`src/steam/backend/cloud_mapping.h:19`).
- The macOS Playden reference does the Steamworks swap with gbe_fork (`GameNative/playden`,
  `Packages/SteamKit/Sources/SteamCore/Prepare.swift`, `PE.swift`, `SteamSettingsINI.swift`;
  `PlaydenKit/.../SteamInstaller.swift` for verify/repair). It chooses graphics per bottle by hand; it has no API
  detection to copy.

## Step 0 — session evidence for generated recipes (prerequisite)

- [x] `OwnerMarker` writes `record.recipe_id` / `record.recipe_version` instead of the kit recipe's. HL2's marker stays
  byte-identical (same values), so its prepared title and bottle keep validating.
- [x] Test (host, failed before the fix): prepare `steam-4242-auto`, Play, write session records; the session runs and
  exits instead of `session-owner-invalid`.
- `RespinIdentity` is unchanged: a prepared game keeps the recipe version it was prepared with (see step 1).
- No migration: no generated install exists on the console now (BALL x PIT was removed), and nothing is released.

## Step 1 — generated graphics and launch defaults (option 1)

### Detection (`src/runtime/game_probe.{h,cpp}`, new, host-testable)

Read only, bounded (file count, depth 3 below the program's folder, 64 MB per PE header read window), no links followed.
For each generated launch entry it returns:

- **Imports** of the program and of the DLLs in its folder (PE import directory only, both PE32 and PE32+; the
  existing `ExecutableArchitecture` reader in `game_settings.cpp` is the starting point): `d3d8`, `d3d9`, `d3d10*`,
  `d3d11`, `dxgi`, `d3d12`, `opengl32`, `vulkan-1`, `ddraw`, `mscoree`, `steam_api(64)`.
  Imports alone are not enough: checked on games from the library (2026-10-07), Unity's `UnityPlayer.dll`, GameMaker
  (Risk of Rain Returns) and METAL GEAR RISING load Direct3D at run time, so none of them import it. Also scan the
  program's and engine DLL's bytes for the DLL names (`d3d11.dll`, `d3d9.dll`, `dxgi.dll`, `d3d12.dll`,
  `vulkan-1.dll`, `opengl32.dll`); engine rules take precedence (Unity lists `d3d12.dll` and `vulkan-1.dll` but uses
  D3D11 with `-force-d3d11`). Read the import directory through the section table with seeks: it can sit past the
  first megabytes of a large program.
- **Engine**, from files: Unity (`UnityPlayer.dll` or `<exe>_Data/`; IL2CPP if `GameAssembly.dll`), Unreal
  (`Engine/Binaries/Win64` or `*-Win64-Shipping.exe`), Source (`hl2.exe` + `bin/engine.dll`), GameMaker (`data.win`),
  Godot (`*.pck` beside the program), XNA/FNA/.NET (`mscoree` import).
- **SteamStub**: the entry point lies in a section named `.bind` (same rule as macOS `PE.swift`).

### Defaults from detection

| Detected | `graphics` | DLL overrides | Arguments |
| --- | --- | --- | --- |
| Any of `d3d9`, `d3d10*`, `d3d11`, `dxgi` (program, its DLLs, or Unity/Unreal/GameMaker) | `dxvk` | `d3d9,d3d10core,d3d11,dxgi=n` (all four; DXVK is in the bottle for both architectures, as Proton does) | — |
| `d3d8` | `dxvk` | add `d3d8=n` (DXVK's `d3d8.dll` is in the template for both architectures; it needs DXVK's `d3d9`) | — |
| `opengl32` only | `opengl` | — | — |
| `ddraw` or none of the above | `auto` | — | — |
| Unity | as above | as above | `-force-d3d11`; resolution via `-screen-width {w} -screen-height {h}` (`resolution_arguments`). Fullscreen or windowed is a console check, not a default (HL2 runs fullscreen in the desktop) |
| Unreal | as above | as above | `-dx11`; launch the `*-Win64-Shipping.exe` directly: the root bootstrap program starts it as a child process, which the console cannot do |
| Source | as above | as above | `-w {w} -h {h}` |

The DXVK overrides are **derived from the effective graphics value at profile time**, not stored as fixed recipe
overrides, so a player who switches graphics gets consistent overrides: `BuildEffectiveSettings` adds them when the
effective graphics is `dxvk` and the recipe is generated. Shipped recipes (HL2) keep their explicit `d3d9=n`.

### Refuse early instead of crashing at Play

Prepare stops with a plain message (no PS5 title is staged) when the chosen program:

- is SteamStub-protected: "This game's program has Steam DRM that Playden can't unpack on the PS5 yet."
- imports `mscoree` (.NET/XNA): "This game needs .NET, which Playden's runtime doesn't include yet."
- imports only `d3d12`: "This game needs Direct3D 12, which isn't supported on the PS5 yet."
- has no launch option left after skipping launchers (an Unreal bootstrap with no Shipping program found).

Anti-cheat folders (`EasyAntiCheat/`, `BattlEye/`) only add a warning line to Details; some games run with it absent.

### Settings and records

- [x] `graphics` is locked only when the recipe lists it in `locked` (HL2 does). Drop the unconditional lock at
  `game_settings.cpp:377`. Generated recipes leave it open; changing it is "Next launch".
- [x] Generator version 2: `GenerateGameRecipe` takes the probe result. A saved recipe is kept as it was prepared; to
  pick up newer defaults, remove the game's PS5 title and prepare it again (no generated install exists yet, so no
  upgrade path was built).
- [x] Record why each default was chosen (`detected=unity,d3d11 graphics=dxvk`) in the install log, and show
  "Detected: Unity · Direct3D 11" in the game's settings sheet (Dev builds show the full probe).
- [x] Session logs: when a session ends without evidence or with a crash, also copy the newest Unity `Player.log`
  (`AppData/LocalLow/<company>/<product>/Player.log`) or Unreal `Saved/Logs/*.log` into the title log, capped at the
  existing 128 KB tail.

### Tests

`game_probe` unit tests over synthetic PE files (imports, PE32/PE32+, `.bind` entry), engine fixtures (directory
trees), the defaults table, refusal messages, unlocking, override derivation when graphics changes, generator version
upgrade. Targets: `game_settings`, `homebrew_runtime_host`, new `runtime_game_probe`.

## Step 2 — Steamworks emulation (option 2)

### Binaries and provenance

- [ ] Ship gbe_fork's `steam_api.dll` and `steam_api64.dll` unmodified in Playden's title package
  (`assets/steamworks/`), taken from the macOS reference's pinned copies (SHA-256 `4cc9dfca…d08ed` and
  `513430ac…cdd1b`, `Resources/steampipe/PROVENANCE.md`). Pin both hashes in `dependencies.lock.json`; Playden checks
  them before every use.
- [ ] gbe_fork is LGPL-3.0 (macOS `THIRD_PARTY_NOTICES.md:64`): add the notice and source pointer, and add it to the
  publish checklist in STATUS. Record the gbe_fork revision when known; the macOS copy has none for the Windows DLLs.
- Steamless is **not** ported: it is a .NET program (needs a child process and Mono) and CC BY-NC-ND. SteamStub stays a
  refusal (step 1).

### Prepare step `steamworks` (after the game files are known, before `stage-title`)

Runs when the recipe's new `steamworks` field is `emulate` (generated default when any `steam_api(64).dll` is found);
shipped recipes default to `original` (HL2's `steam_api.dll` must stay byte-for-byte, integration handoff:66).

- [ ] Find `steam_api.dll` / `steam_api64.dll` below the game root: case-insensitive, depth ≤ 10, no links, no hidden
  folders. The PE machine must match the name.
- [ ] For each: back up to `<dll>.orig` once (copy, fsync, rename; never overwrite an existing `.orig`), then replace
  the DLL with the pinned emulator (temp file + rename). Already-swapped check: hash equals the pinned emulator and
  `.orig` exists. Emulator hash with no `.orig` fails the step ("game files changed; verify the download").
- [ ] Write `steam_settings/` next to each DLL (plain files, written atomically, merged keys as macOS
  `SteamSettingsINI` does):
  - `steam_appid.txt` (also beside the DLL, only if missing).
  - `configs.user.ini`: `account_name` and `account_steamid` from the signed-in account (offline: "Playden", steamid 0),
    `language` from the console language mapped to Steam's names, `local_save_path=C:\Program Files (x86)\Steam\userdata\0`
    so ISteamRemoteStorage files land where Cloud mapping already expects them (`<appid>/remote`). Check gbe's layout
    on the console before relying on it.
  - `configs.main.ini`: `offline=1`, `disable_networking=1`, `disable_lan_only=0`.
  - `configs.app.ini`: `[app::dlcs] unlock_all=0` plus one line per **owned** DLC from PICS and the license list.
  - `depots.txt`: installed depot IDs. `supported_languages.txt`: the fixed macOS list.
  - `steam_interfaces.txt` (in `steam_settings/` and beside the DLL): extracted from the `.orig` by the macOS string
    scan (printable runs ≥ 10 chars, known interface prefix + digits, `SteamClient017` rule). Port the prefix list.
  - Achievements and stats: not now.
- [ ] Record every mutated path with its original and new SHA-256 in the install record (`game_mutations`). Startup
  validation re-checks them; a mismatch (e.g. the DLL was restored by hand) re-runs the step on the next prepare.
- [ ] Undo: setting **Steam features** (T2, "Game must be closed"): "Offline emulation" (default for generated) /
  "Game's own Steam files". Switching back copies each `.orig` over its DLL, keeps the `.orig` and `steam_settings/`.
- [ ] Uninstall: no change needed; `RemoveManagedInstall` falls back to `RemoveTree` for extra files.
- [ ] Future verify/repair (not built on the PS5 yet): must check `.orig` instead of the swapped DLL and re-download
  originals to `.orig`, as `SteamInstaller.swift:102-125, 291-310` does. Note this in the verify to-do.
- Wine has no builtin `steam_api`, so no DLL override is needed; the native DLL in the game folder loads.
- Plain step word for the UI (UI polish made steps plain): "Setting up Steam features".

### Tests

Host tests: discovery (case, depth, links, wrong architecture), backup-once and idempotent re-run, the "emulator without
`.orig`" failure, interface extraction against a synthetic DLL, INI contents (DLCs only when owned, save path), the
mutation record and its startup re-check, undo, HL2 untouched (`original`).

## Order and acceptance

1. Step 0, then step 1 (host tests green), then step 2.
2. Console acceptance with BALL x PIT (after ready to play lands): download → prepare (log shows `detected=unity,d3d11
   graphics=dxvk`, the `steamworks` step, `steam_settings/` written) → "ready to play" → Play quits to Home → start from
   Home → the game reaches its menu with DXVK (FPS overlay on) → controller works → quit → Playden reads the session
   record (no `session-owner-invalid`) and syncs Cloud. Turn Wine's `+seh` logging on for the first run so a crash names
   its exception.
3. HL2 regression: after deploying, read `/data/playden/games/steam-220/library/prefixes/game/.playden-template-owner`
   and confirm it is unchanged; then Play → menu → quit, with `steam_api.dll` unchanged.
4. Test games, picked 2026-10-07 from the owner's Steam library (ProtonDB platinum, full controller support,
   single-player; sizes from Steam's public depot manifests). Each covers a different path:

   | Game | App | Size | Path |
   | --- | --- | --- | --- |
   | BALL x PIT | 2062430 | 0.51 GB | Unity (pe64), D3D11 loaded at run time, Steamworks |
   | Super Meat Boy | 40800 | 0.93 GB | Own engine (pe32), D3D9 imported, `steam_api.dll` |
   | Pizza Tower | 2231450 | 0.40 GB | GameMaker (pe64), D3D11 loaded at run time |
   | Balatro | 2379780 | 0.07 GB | LÖVE, OpenGL; Steam DRM listed: Steamworks emulation or a SteamStub refusal |

   Not covered by these: Unreal (`*-Shipping.exe`); the smallest platinum Unreal games in the library are 3 GB or more
   (AMID EVIL lists 13 GB). Refusals were checked on the Mac copies: Mirror's Edge Catalyst (Direct3D 12), Geometry
   Wars 3 and NightSky (SteamStub).
