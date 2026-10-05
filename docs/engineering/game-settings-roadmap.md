# Game settings roadmap

Date: 2026-10-05. Purpose: turn the [Wine bottle settings catalogue](wine-bottle-settings.md)
into a per-game **Game settings** UI that matches macOS Playden. Settings are
tiered by how often players change them, and each one is scheduled by what
prospero-win can actually apply today.

This document is the settings spec for two tasks in the
[integration plan](../superpowers/plans/2026-10-05-prospero-win-integration.md):
Task 4 (`game_settings.{h,cpp}`, `BuildEffectiveSettings(recipe, overrides)`) and
Task 8 (settings UI). It does not restate their lifecycle, repair or uninstall work.
Dependencies and their local paths are in [dependencies.md](dependencies.md).

**Change 2026-10-05 (later):** frame limit, VSync, frame latency and reported
GPU/VRAM moved from "needs prospero-win" to "Playden only, if a console probe
passes". DXVK 2.6.2 loads `dxvk.conf` from the working directory
(`src/util/config/config.cpp:1576-1581`), and prospero-win sets the Wine working
directory to the profile's `working_directory` before launch
(`native/wine64_main.c:1055-1061`). Added the ownership table and the
implementation plan.

**S0 result 2026-10-05:** the probe passed. A `dxvk.conf` with `d3d9.maxFrameRate = 30`
in the HL2 folder dropped the menu from 59.1 to 29.9 FPS on the console
([evidence](../../results/20261005-dxvk-conf-probe/README.md)). The "Probe" rows below are
Playden-only work.

## Terms

The word "profile" means three different things across the projects. This
document uses the terms below and nothing else.

| Term | Meaning | Where it lives |
| --- | --- | --- |
| **Preset** | A named bundle of setting values the player picks from a list ("Older 3D game"). Equivalent to the macOS `profiles.json` entries. | Playden data, bundled JSON |
| **Recipe** | Per-game install and compatibility data: executable, required arguments, required DLL overrides, setup steps, recipe defaults. Equivalent to the planned `runtime/recipes/*.json`. | Playden data, bundled JSON (later: community) |
| **Overrides** | The player's changes for one game. Absent means "use the recipe/preset value". | Per-game install record |
| **prospero profile** | The strict INI file prospero-win reads (`[application]`, `[display]`, `[input]`, `[debug]`). Always **generated output**, never edited by hand in Playden. | `<bottle>/profiles/<id>.profile` |
| **Input preset** | A prospero-win `.input` file referenced by `[input] preset`. | `input/<name>.input` |

Resolution order for every setting:
**Playden default → recipe default → preset → overrides**, then kit capabilities
decide whether the value applies. Presets list only the settings they change. A
setting the recipe locks (graphics for HL2) keeps the recipe value. Recipe
arguments and DLL overrides (for example HL2's `d3d9=n` and `-game hl2`) are
always written first; the player can only add to them.
A recipe can also declare **parameterised arguments** that a setting fills. For
example, HL2 declares `resolution_args = "-w {w} -h {h}"`, and the Resolution
setting writes both `[display] desktop` and those arguments. Those arguments are
locked in form, not in value.

Invariants carried from the catalogue:

- An absent override is not the same as an explicit `false`, `0` or `off`.
- Home and Playden launches use the same persisted effective prospero profile.
- A control that the current kit cannot honor is either hidden or shown disabled
  with a reason. It is never an interactive control that silently does nothing.

## What players change most

There is no published aggregate of ProtonDB data, so we counted it ourselves from
the public dump ([bdefore/protondb-data](https://github.com/bdefore/protondb-data),
`reports_sep1_2026`, 389,717 reports, 58,839 with launch options). The
percentages are regex estimates over the launch-option text and checkboxes, not
exact figures. The Android launchers (Winlator, GameNative) and Steam Deck
guides were used to order settings that do not appear as launch options.

| Rank | Setting | Evidence | Why players change it |
| --- | --- | --- | --- |
| 1 | Graphics backend (DXVK / WineD3D / VKD3D) | WineD3D flag or checkbox in ~7.7k reports; 2nd–3rd step in Winlator guides | Fix it |
| 2 | Resolution / screen size | First step in the [Winlator](https://www.xda-developers.com/winlator-guide/) and [GameNative](https://www.joeysretrohandhelds.com/guides/gamenative-setup-guide/) guides | Preference, performance |
| 3 | Performance overlay | MangoHud / `DXVK_HUD` in ~18% of launch options | Diagnostics |
| 4 | Frame-rate cap | `DXVK_FRAME_RATE` and similar ~5%; top Steam Deck per-game setting | Performance, sometimes a fix |
| 5 | Controller layout | 18.6% of reports changed the layout; "controller not detected" in 2.8k | Fix, preference |
| 6 | Skip intro / launcher (game arguments) | ~8% (`-novid`, `--launcher-skip`, `-skipintro`) | Comfort, sometimes a fix |
| 7 | Game's own renderer argument (`-dx11`, `-vulkan`) | ~7.8% | Fix |
| 8 | DLL overrides | ~6.3%; top: `dinput8`, `version`, `winmm`, `dxgi`, `ddraw` | Fix, mods |
| 9 | Upscaling / sharpening | ~9%, mostly Linux-only gamescope | Preference |
| 10 | GPU vendor spoofing | NVAPI / `HIDE_NVIDIA` ~8k combined | Fix |
| 11 | Windowed / fullscreen | ~4.5% | Fix, preference |
| 12 | Video memory size | 2nd–4th step in Winlator guides | Fix |
| 13 | VSync | Common Winlator tip | Comfort |
| 14 | Wine/runtime version | ~25% use a non-default Proton | Fix |
| 15 | Redistributables (winetricks) | ~11k reports | Fix |
| 16 | CPU cores / topology | ~2.2% | Performance, fix |
| 17 | Audio latency / driver | 5k "crackling" reports | Fix |
| 18 | Windows version | Exposed everywhere, rarely a first step | Fix |

The most-changed settings are mostly the ones prospero-win cannot apply yet.
Frame cap and VSync have no profile field or DXVK config route, the graphics
backend is recipe-locked for HL2, and video memory and vendor spoofing need a
managed `dxvk.conf`. The honest plan is to ship the existing fields well first
and drive the runtime work for the popular rows next.

## Who implements what

prospero-win checked at `defd38b` (2026-10-05). No open prospero-win PR changes the
profile schema. "Reads it" still means Playden work:
`src/runtime/prospero_profile.cpp` writes only `[application]` today. It does not
write `[display]`, `[input]`, `[debug]` or `dll_overrides`.

| Readiness | Meaning |
| --- | --- |
| **Ready** | prospero-win reads it now. Only Playden work. |
| **Ready, path-gated** | Reads it, but only some graphics paths honor it. Playden must gate the row. |
| **Probe** | Works today through `dxvk.conf` in the game folder (S0 passed for the frame limit). Each option still needs its own console check. |
| **prospero-win change** | Needs a new profile key, section or tool in prospero-win. |
| **prospero-win backend** | Needs native PS5 driver, input or audio work in prospero-win. |

| Setting | Readiness | prospero-win work | Playden work |
| --- | --- | --- | --- |
| Preset | Ready | None | Preset catalogue and chooser |
| Launch option | Ready | None | Recipe launch entries; write `executable`, `working_directory` |
| Resolution 720p/1080p/1440p | Ready, path-gated | None. Only 1080p is proven on hardware | Write `desktop` and `resolution_args`; verify each size on the console |
| Controller (XInput / keyboard) | Ready | None | Always write `mode` |
| Controller preset | Ready | None | Ship `.input` files into the bottle's `input/` folder |
| Performance overlay (FPS) | Ready, path-gated | None (DXVK and OpenGL; not GDI) | Write `show_fps`; disable for GDI |
| Extra arguments | Ready | Optional: escape syntax in the argument parser | Merge after locked recipe arguments; validate limits |
| Button mapping | Ready | None | Per-button picker for 17 buttons |
| Pointer stick and speed | Ready | None | Write `mouse`, `mouse_speed` |
| Scaling | Ready, path-gated | Pass the mode to the DXVK swapchain/display-plane path | Hide on the DXVK path until then |
| Window view | Ready | None | Write `view` |
| 120 Hz, OpenGL thread | Ready, path-gated (OpenGL only) | DXVK 120 Hz needs RADV/display-mode work | Show only for OpenGL |
| Graphics backend | Ready, but locked by recipe | None while locked | Read-only row |
| Library overrides | Ready | None | Merge after locked overrides; validate |
| Logging | Ready | None | Presets plus revert-after-one-session |
| Frame limit | Probe (passed) | None | Write `dxvk.conf` in the game folder; keep it through repair and verify |
| VSync | prospero-win backend | VSync off was checked: still paced at 60 Hz (56–59 FPS). Needs immediate/mailbox present on the RADV/display-plane path | Row disabled with that reason |
| Frame latency | Probe (not observable yet) | None expected | Row disabled until a console check shows an effect |
| Reported GPU vendor / VRAM | Probe (not observable yet) | None expected | Row disabled until checked; recipe-scoped values |
| Overlay detail (frame time, memory) | Probe, with a decision | `show_fps` sets env `DXVK_HUD=fps`, which overrides `dxvk.hud`. Either Playden writes `dxvk.hud` and keeps `show_fps=false`, or prospero-win takes an element list | Depends on decision D1 |
| Environment variables | prospero-win change | New typed `[environment]` section with an allowlist, plus a schema/minimum-runtime marker so older runtimes fail clearly | Text row with validation |
| Windows version, text scale, locale, DLL load order in registry, CPU topology | prospero-win change, or Playden | A registry overlay helper with backup, both registry views, run while the title is stopped | Rows with "Game must be closed"; decision D2 |
| Stick deadzones, triggers, rumble, mouse wheel | prospero-win backend | Native input transforms before Wine pad publication | Rows when it lands |
| Per-game volume and mute | prospero-win backend | Gain and mute in the audio sink | Rows when it lands |
| Audio device, microphone, surround, HDR, VRR, upscaling, multiple controllers | prospero-win backend (large) | Driver work, catalogue §2–§4 | Not scheduled |

### Decisions needed

- **D1, overlay detail — decided (a), checked on the console** (frame-time graph shown from
  `dxvk.hud` with `show_fps = false`; see the probe record).
  Original options:
- **D1, overlay detail.** (a) Playden writes `dxvk.hud` into `dxvk.conf` and writes
  `show_fps=false`. No upstream change, but the overlay then depends on the
  `dxvk.conf` route. (b) prospero-win accepts an element list for `show_fps`, which is
  cleaner and needs an upstream change. Recommendation: (a) for DXVK, keep `show_fps` for OpenGL.
- **D2, registry overlays — decided (b): Playden edits the stopped bottle's registry.**
  Original options:
- **D2, registry overlays.** (a) prospero-win owns a registry overlay tool, which
  the catalogue assumes. One owner of the `.reg` format, but an upstream dependency.
  (b) Playden edits the stopped bottle's `user.reg`/`system.reg` itself, since it
  runs natively on the console. No upstream dependency, but Playden then owns Wine
  registry-format edits. Recommendation: (a), and propose it upstream early.

## Tiers and phases

**Tier** is where the setting appears in the UI. **Phase** is when it can ship.

| Tier | UI location | Audience |
| --- | --- | --- |
| **T1 Basic** | "Settings" section, always visible | Every player |
| **T2 Compatibility** | "More settings", collapsed by default | Players fixing a game |
| **T3 Advanced** | "Advanced", text entry | Players copying a fix from a guide |
| **T4 Bottle & saves** | Separate "Maintenance" actions on the game page | Repair and recovery |
| **Dev** | Diagnostics view, developer builds only: read-only `id`, `prefix`, `runtime`, `architecture`, kit and module hashes, effective command, setting sources | Us |

| Phase | Depends on | Integration plan |
| --- | --- | --- |
| **P1** Existing profile fields | Nothing new in prospero-win | Task 4 + Task 8 |
| **P2** Launch-time config | `dxvk.conf` in the game folder (S0 probe); prospero-win `[environment]` section for env vars (U1) | Playden S5, S8 |
| **P3** Stopped-bottle registry overlays | prospero-win/host: transactional registry overlay with backup | After Task 8 |
| **P4** Input transforms, audio | prospero-win native input and audio work | After P3 |
| **P5** Shared presets and recipes | Agreement with prospero-win-profiles | Last |

### Apply tags

macOS uses one "CHANGES BOTTLE" tag. On PS5 the catalogue's `apply_mode` needs
three, shown on the row and in the picker:

- *(no tag)* **Next launch**: regenerates the prospero profile or launch config.
- **Game must be closed**: mutates the bottle (registry, DLLs, cache). Blocked while
  the title is active or its state is unknown.
- **Prepares bottle**: offline preparation or a job with progress (dependencies,
  runtime change).

### Settings map

Status values come from the catalogue: Existing, Conditional, Missing exposure,
Missing backend, Preparation, Application, Managed.

#### P1: ship with the per-game title (existing fields)

| Setting | Tier | Pop. rank | macOS equivalent | Binding | Status | Control and values |
| --- | --- | --- | --- | --- | --- | --- |
| Preset | T1 | — | Profile row | Playden data | — | Opens preset chooser |
| Launch option | T1 | 6 | Launch option | `executable`, `working_directory` from recipe launch entries | Existing | Picker of the game's launch entries |
| Resolution | T1 | 2 | Virtual desktop | `[display] desktop` **and** the recipe's resolution arguments (`-w/-h`) together | Conditional | 1280×720, 1920×1080 (default), 2560×1440. 4K hidden until presentation is proven. Recipes without `resolution_args` set only the desktop and say so in the effect line |
| Controller | T1 | 5 | Controller | `[input] mode` (always written explicitly) | Existing | Xbox controller (default) · Keyboard and mouse mapping |
| Controller preset | T1 | 5 | — | `[input] preset` | Existing | Picker of bundled `.input` files; "Recipe default" first |
| Performance overlay | T1 | 3 | Performance overlay | `[display] show_fps` | Conditional (DXVK/OpenGL only) | Off · On. Disabled with reason for GDI |
| Extra arguments | T1 | 6, 7 | Launch arguments | `arguments`, appended after locked recipe arguments | Existing | Text entry; shows the effective command read-only. ≤512 bytes, ≤32 words, no escapes |
| Button mapping | T2 | 5 | — | 17 `[input]` button keys | Existing | Per-button picker: Inherit · None · key list · mouse buttons. Both modes; in Xbox controller mode bindings are added to the native pad (HL2 uses `options = escape`) |
| Pointer stick | T2 | 5 | — | `[input] mouse` | Existing | None · Left stick · Right stick |
| Pointer speed | T2 | 5 | — | `[input] mouse_speed` | Existing | Picker of steps (600, 900, 1200, 1800, 2400 px/s); shown only with a pointer stick. No slider widget exists |
| Scaling | T2 | — | High resolution mode (loosely) | `[display] scaling` | Conditional (CPU and exported-Vulkan paths only) | Fit · Integer · Stretch. Hidden on the DXVK display-plane path until it honors the mode |
| Window view | T2 | 11 | — | `[display] view` | Existing | Game windows · Entire desktop |
| 120 Hz output | T2 | — | — | `[display] refresh` | Conditional (OpenGL only) | Shown only when graphics is OpenGL |
| OpenGL worker thread | T2 | — | — | `[display] opengl_thread` | Conditional (OpenGL only) | Shown only when graphics is OpenGL |
| Graphics backend | T2 | 1 | Graphics | `graphics` + `dll_overrides` | Conditional | Shown read-only ("Set by recipe") until a kit ships more than one validated backend for the game |
| Library overrides | T3 | 8 | Library overrides | `dll_overrides`, merged after locked recipe overrides | Existing | Text entry, Wine syntax, semantic validation; ≤256 bytes |
| Logging | Dev → T2 | — | — | `[debug] winedebug` | Existing | Normal · Verbose · Custom (text). Verbose auto-reverts after one session |

#### P2: launch-time configuration

The DXVK rows can be Playden-only: Playden writes `dxvk.conf` into the game's
working directory, if the S0 probe passes. Environment variables still need the
prospero-win `[environment]` section (U1). Option names must come from the pinned
DXVK 2.6.2 `dxvk.conf`.

| Setting | Tier | Pop. rank | macOS equivalent | Binding (DXVK 2.6.2) | Control |
| --- | --- | --- | --- | --- | --- |
| Frame limit | T1 | 4 | Frame limit | `d3d9.maxFrameRate` / `dxgi.maxFrameRate` | Off · 30 · 60 |
| VSync | T1 | 13 | — | `d3d9.presentInterval` / `dxgi.syncInterval` | Game default · On · Off |
| Overlay detail | T2 | 3 | — | `DXVK_HUD` element list | FPS · FPS + frame time · Full |
| Frame latency | T2 | — | — | `*.maxFrameLatency` | Game default · 1 · 2 · 3 |
| Reported GPU / VRAM | T2 | 10, 12 | — | `dxgi.customVendorId`, `dxgi.maxDeviceMemory` | Recipe default · picker of tested values |
| Shader compiler threads | Dev | — | — | `dxvk.numCompilerThreads` | Kit default · numbers |
| Environment variables | T3 | — | Environment variables | Allowlisted env section | Text entry; reserved keys rejected |

Acceptance for each row: the effect is measured on the console (pacing, tearing,
overlay content), not inferred from the file contents.

#### P3: bottle registry overlays (game must be closed)

| Setting | Tier | Pop. rank | macOS equivalent | Control |
| --- | --- | --- | --- | --- |
| Windows version | T2 | 18 | Windows version | Windows 10 (default) · 8.1 · 7 · XP |
| DLL load order in registry | T3 | 8 | — | Structured override list, both registry views |
| Text scale (DPI) | T2 | — | — | 96 · 120 · 144 |
| Locale | T2 | — | — | Picker of installed locales |
| CPU topology | T2 | 16 | (Preset "Hides too many cores") | Kit default · 4 · 8 cores, if the pinned Wine supports it |

#### P4: input and audio backend work

Stick deadzones and inversion, trigger thresholds, rumble on/off and strength,
per-game volume and mute, mouse wheel bindings. Each needs native prospero-win
work listed in catalogue §4 and the audio rows of §2. Plan these per feature;
none is a profile key today.

#### T4 maintenance actions (separate from settings)

Reset settings, repair dependencies, clear shader cache, back up saves and
recreate bottle. These are actions with confirmation and progress, not rows in
the settings list, and they belong to integration plan Task 8.

#### Do not expose

These are popular elsewhere but do not apply to this backend. Do not add rows for
them, even as disabled entries:

| Setting | Reason |
| --- | --- |
| esync / fsync / ntsync, `PROTON_NO_ESYNC` | Linux kernel primitives; PS5 uses its own `WINE_PS5_*` sync paths |
| `DXVK_ASYNC` | Removed in DXVK 2.x; graphics pipeline library replaced it |
| `gamemoderun`, `LD_PRELOAD`, gamescope | Linux host tools |
| Box64 / FEX presets | The PS5 CPU is x86-64; no CPU emulation |
| Large address aware via PE patch | Edits the game binary; handle per recipe only |
| `WINE_PS5_DESKTOP`, `WINE_PS5_VIEW`, `WINE_PS5_OPENGL`, `WINE_PS5_GL_REFRESH` | Duplicates of typed `[display]` fields |
| `WINE_PS5_*` sync and fast-path switches | Developer and recipe switches; catalogue §5 |
| `WINEPREFIX`, `HOME`, `USER`, loader paths | Managed by the installer |

## UI design

Adopt the macOS Playden idiom (`App/GameSettingsSheetView.swift`,
`docs/design/GAME_SETTINGS.md` in the macOS repo), with the changes below.

**Keep from macOS**

- One right-hand sheet with a scrolling list. The Preset row is first, then
  sections Settings (T1), More settings (T2, collapsed), Advanced (T3).
- Every enumerated setting opens a picker sheet; no inline toggles. Each row
  shows the name, a one-line effect, an "also called" line with the technical
  key, and the current value.
- Each picker choice shows its source on the right: "Playden default",
  "From preset", "Required by recipe". This is the catalogue's `source` field.
- A modified row shows an accent dot and an accent value. △ resets the focused
  row; the footer "Reset all to preset" is active only when there are overrides.
- Picking a preset clears overrides. Any override changes the preset label to
  "Custom · from <preset>". The preset chooser shows a Current → This preset
  comparison table.
- Close toast: "Settings saved · <preset>" and "n changes apply on next launch".
- L1/R1 jump five rows; focus stays centred.

**Change for PS5**

- Replace "CHANGES BOTTLE" with the three apply tags above.
- Add a **disabled row** state: dimmed value, reason as the effect line
  (for example "Only available with OpenGL"). Use reason codes from catalogue §7.
- Locked recipe values show a lock and "Required by recipe" and cannot be picked
  away. Text fields show locked parts read-only before the editable part.
- Show "Game must be closed" rows disabled while the title is running.
- Add a read-only "Effective command" row at the bottom of Advanced.

**Code plan**

- Supersede the simulation-only modal at `src/homebrew/simulation.cpp:2795-2836`
  and its `struct Profile` (`simulation.hpp:203`) rather than extend it. Its
  fields (frame limit, VSync, resolution) do not match P1 and it uses inline
  toggles. Its persistence (`selected_profiles`, `custom_profiles`,
  `save_profile()`) and the cases in `tests/homebrew_simulation_test.cpp` move
  to the new model in the same change.
- Reuse `RowKind::Choice`, `RowKind::Text`, `RowKind::Heading`, the
  `choice-picker` modal and the text editor overlay. Add a section-collapse
  behavior and a disabled-with-reason row decoration.
- Drive rows from a static settings catalogue in `src/runtime/game_settings.cpp`
  (id, tier, phase, binding, apply mode, allowed values, requires). The UI
  hides rows whose phase is not enabled by the kit capabilities, so P2 rows
  appear without UI changes when the runtime supports them.
- Show the game settings entry on the details screen in live mode once the
  game has a prepared bottle.

## Shared presets and recipes (P5, planning only)

[prospero-win-profiles](https://github.com/mpereiraesaa/prospero-win-profiles)
(MIT, started 2026-09-28, very active) holds 15 hand-written prospero profiles,
4 input presets, and 3 Lutris-style recipes with a `prospero:` block. Its
`tools/pw_install.py` generates a prospero profile from a recipe. This
matches our model: recipes are the shareable unit, prospero profiles are output.

Facts that limit how we can share the format:

- prospero-win parsers reject unknown sections and keys. Any metadata added to a
  `.profile` breaks older runtimes, and a newer key (`opengl_thread`) already
  does.
- No schema version, minimum runtime or checksum. Compatibility lives in
  comments ("needs prospero-win #373").
- Profiles are keyed by a slug only. There is no Steam AppID, executable hash or
  depot build, so a launcher cannot match a profile to an installed game.
- No override layer. `pw_prefix.py push` overwrites the file.
- `profiles.lst` is capped at 16 entries and holds 15. Our per-game titles each
  publish a one-entry list, so the cap does not affect us.

Proposal:

1. Keep the prospero profile INI as prospero-win's contract and never extend it
   for metadata.
2. Propose a **sidecar JSON** per recipe in prospero-win-profiles, next to the
   existing files: `schema_version`, game identity (`steam_appid`, executable
   path, optional depot manifest IDs), `min_runtime`, tested firmware and date,
   status (Works / Playable / Broken), required arguments and DLL overrides,
   default settings by our setting ids, setup steps, notes, author. A launcher
   reads the sidecar; `pw_install.py` keeps reading the YAML.
3. Use the same preset shape as macOS `profiles.json`
   (`id, name, tryWhen, shortHint, settings{}`) with setting ids from one shared
   catalogue, so macOS v2 community recipes and PS5 share the files. macOS
   already plans "community" as a `RuntimeProfileSource`, and its
   `RuntimeProfile { base, overrides, source }` is the same model as
   `BuildEffectiveSettings(recipe, overrides)`.
4. Playden fetches a pinned, hashed index from the repo (a static JSON export,
   as the macOS PRD suggests) and shows "Community recipe" with a one-press
   apply and revert. User overrides stay local and survive recipe updates.
5. License: the repo is MIT. Ask whether recipes and outcome data can also be
   CC0, as the macOS PRD proposes, before Playden redistributes them.

Open questions for the prospero-win maintainer: whether a sidecar is welcome,
whether `min_runtime` can become a real version rather than a PR number, and
whether the runtime could ignore an optional `[x-*]` section in future so the
INI can carry an id link to its sidecar.

## Implementation plan

Two tracks run in parallel. The upstream track starts first because it has the
longest lead time.

### Playden track

Progress 2026-10-05 (branch `feat/game-settings`): ✅ done, ◐ partly done.

- S0: `results/20261005-dxvk-conf-probe`.
- S1/S2: `src/runtime/game_settings.{h,cpp}`, `runtime/presets.json`,
  `runtime/recipes/steam-220-hl2-v1.json`; profiles checked against prospero-win's parser rules.
- S3: the sheet, pickers, preset chooser, button mapping and save toast in simulation mode.
- Live mode keeps settings in `playden-data/game-settings.json` (ready for S4).
- S8: the Environment variables row writes `[environment]` only for a runtime that reads it;
  disabled until U1 lands.
- S10: `ParseProfileSidecar` turns a sidecar into a "Community" preset; fetching waits for U3.
- U1 drafted locally (not pushed): prospero-win `feat/profile-environment` in
  `~/.chauffeur/worktrees/prospero-win-environment`, parser tests pass.
- U4 first slice drafted locally (not pushed): prospero-win `feat/profile-input-tuning` in
  `~/.chauffeur/worktrees/prospero-win-input-tuning`, `[input] stick_deadzone` (0-50%, round,
  rescaled) and `rumble` (0-100%) for the XInput controller; parser and native tests pass.
- S9 first rows: Stick dead zone and Rumble (`KitCapabilities::input_tuning`), hidden until a
  runtime with U4 is in the kit; the checker accepts the keys only for such a runtime.
- R1 (2026-10-06): Trigger dead zone (`input_tuning`), Mouse wheel stick (`input_wheel`) and
  Volume (`audio_volume`, a basic row) write `[input] trigger_deadzone`, `[input] wheel` and
  `[audio] volume`, each hidden until the kit lists its capability. prospero-win side drafted
  locally on `feat/profile-input-audio` (`~/.chauffeur/worktrees/prospero-win-input-tuning`):
  parser, native and audio tests pass and the title sources compile with the PS5 toolchain. The
  wheel also needs Wine patch 0745, which applies to the patched driver source but is not built
  (no `PS5_PRX_FOUNDATION` on this Mac). None of it is checked on a console.
- U3 drafted locally (not pushed): prospero-win-profiles `feat/recipe-sidecar` in
  `~/.chauffeur/worktrees/prospero-win-profiles-sidecar`, `SIDECARS.md` and checker.
- S6: "Clear shader cache" deletes only `<exe>.dxvk-cache` (`ClearShaderCache`); the sheet's
  action is simulated until the install service exposes game folders (Task 8). Repair,
  backup and bottle reset stay with Task 8.
- S7 (decision D2): `ApplyRegistrySettings` edits the stopped bottle's `user.reg` for Windows
  version (`AppDefaults\<exe>`) and text scale (`Control Panel\Desktop` `LogPixels`), keeping
  every other line, after a `user.reg.playden-backup`. Console check
  (`results/20261005-registry-probe`): Wine reports the edited Windows version and runs at the
  edited DPI. The rows stay disabled until the production path can reach the stopped bottle
  (S4, integration-owned).
- S5: `ApplyGameSettings` writes the profile and `dxvk.conf`. Still open: call it from
  the real Play action (S4). VSync off was checked and has no effect (row disabled); DXVK
  applies frame latency and reported VRAM (D3D9 and DXGI options), so those rows are enabled.

| Step | Work | Depends on | Done when |
| --- | --- | --- | --- |
| **S0** ✅ | Console probe: put a `dxvk.conf` with `d3d9.maxFrameRate = 30` in the HL2 folder and launch it with the existing profile path | Console, HL2 install | `hl2_d3d9.log` shows `Found config file` and the frame rate is capped. The result decides whether frame limit, VSync, frame latency and reported VRAM are Playden-only |
| **S1** ✅ | Settings catalogue and model in `src/runtime/game_settings.{h,cpp}`: setting ids, tiers, bindings, apply modes, `requires`; overrides; `BuildEffectiveSettings(recipe, preset, overrides)` | — | Host tests: absent vs explicit values, locked recipe values, preset reset |
| **S2** ✅ | Full prospero profile writer: `[display]`, `[input]`, `[debug]`, `dll_overrides`; recipe format with locked arguments and `resolution_args`; HL2 recipe; bundled presets and `.input` files | S1 | Round-trip tests against the upstream parser rules (`check_profiles.py`); HL2 output equals the owner-verified profile |
| **S3** ✅ | P1 settings UI in simulation: sheet, picker, preset chooser, disabled and locked rows, toast; replaces the simulation-only modal and its tests | S1 | Host graphical tests pass |
| **S4** | Live wiring: settings entry in live mode, flush before handoff, Home and Playden produce the same profile | Integration Tasks 4–6 | Console: each P1 row has a visible effect on HL2 |
| **S5** ◐ | `dxvk.conf` settings: frame limit, VSync, frame latency, reported VRAM/vendor, overlay detail (D1) | S0 passed, S2 | Console: cap, tearing and overlay measured; file kept by repair and verify |
| **S6** ◐ | Maintenance actions: reset, repair, clear shader cache, back up saves, recreate bottle | Integration Task 8 | Task 8 acceptance |
| **S7** ◐ | P3 registry rows (console-checked; live wiring with S4) | U2 or D2(b) | Fixture reports the version; reset restores it |
| **S8** ◐ | Environment variables row | U1 | Values reach the Wine process; reserved keys rejected |
| **S9** ◐ | P4 rows, one at a time (dead zone, rumble done; triggers, wheel, audio open) | U4 per feature | Per-feature console fixture |
| **S10** ◐ | Community recipes: fetch the pinned index, "Community recipe" banner, apply and revert | U3 | Apply and revert keep local overrides |

### Upstream track (prospero-win and prospero-win-profiles)

| Step | Proposal | Unblocks |
| --- | --- | --- |
| **U1** ◐ | Typed `[environment]` section with an allowlist, `DXVK_CONFIG_FILE` forwarding, and a profile schema or minimum-runtime marker | S8; fallback for S5 if the S0 probe fails |
| **U2** | Registry overlay tool: backup, both registry views, run while stopped (if D2 is (a)) | S7 |
| **U3** ◐ | Sidecar JSON per recipe in prospero-win-profiles (identity, `min_runtime`, tested firmware, status, defaults) | S10 |
| **U4** ◐ | Native input transforms (deadzones, triggers, rumble gain, mouse wheel) and audio gain/mute | S9 |
| **U5** | Scaling on the DXVK path; DXVK 120 Hz | Un-hides Scaling and 120 Hz for DXVK |

### Integration contract for S4 and Task 8

What the settings module gives the integration's production adapters (`src/runtime/game_settings.h`,
`src/runtime/settings_publication.h`). The adapters keep ownership of stopped-title checks, the
journaled profile/descriptor publication and the kit trust configuration.

1. **Kit capabilities.** Add `SettingsKitCapabilityNames()` (`settings-audio-volume`,
   `settings-dxvk-config`, `settings-dxvk-present`, `settings-input-tuning`,
   `settings-input-wheel`, `settings-profile-environment`) to the
   kit trust config's `allowed_capabilities`. After `VerifyKitManifest`, call
   `KitCapabilitiesFromManifest(result.capabilities, bottle_access)` and pass the result to the
   settings sheet (`Simulation::SetKitCapabilities`) and to the launch plan. `bottle_access`
   is true only when the adapter can reach the stopped bottle. A kit that lists none of these
   keeps every gated row disabled, which is the safe default. Manifests list capabilities in
   sorted order, so these names sort between `ps5-…` and `wine-…` entries. The released prospero-win kit
   should list `settings-dxvk-config` (checked on the console); the others need U4 (input,
   wheel, volume), U5 (VSync) and U1 (environment).
2. **Launch plan.** `BuildSettingsLaunchPlan({recipe, preset, record, kit, {profile_id,
   install_path}, runtime_dir})` returns the profile bytes, the input presets the profile
   references (bundled in `runtime/input`), `dxvk.conf`, registry values and
   `settings_revision` (the record's `revision`). A missing preset fails the plan; stale
   player values are ignored, as in the sheet. The plan owns the selected entry and captures
   app/recipe identity, so a recipe reload cannot change it. A named record preset must
   match the supplied preset; an empty record accepts null or the empty `playden-default`.
   The player can change Launch option, so the adapter should compare `plan.app_id`,
   `plan.recipe_id`, `plan.recipe_version`, `plan.launch_entry_id` and the profile's `executable`/`working_directory` with the
   `RuntimeInstall` record, and either update the record or refuse to publish.
3. **Publication.** In `provide_publication`, call `FillProfilePublication(plan, &publication)`;
   it sets captured recipe identity, `profile_id`, `profile_bytes`, `settings_revision`,
   `required_input_presets` and `input_presets`. The adapter compares these with its install
   record, fills the remaining identity fields, and sets `profile_references_validated`
   only after its pinned parser validates the profile and each included input preset. Republish when `PackageStatus::profile_settings_revision` differs from
   the record's revision. Home and Playden then read the same published profile.
4. **Game folder and bottle.** After `NativeCoordinator::Activity` reports `kStopped`, call
   `ApplySettingsFiles(plan, install_path, prefix_dir, now)`. It writes or removes Playden's
   `dxvk.conf` in the launch working folder (never a game's own) and edits `user.reg` with a
   backup. Validation refusals before mutation leave these files unchanged; I/O failures
   after mutation require reconciliation. The helper preserves file modes, syncs writes
   and directories, and attempts config rollback if the registry fails. Failed rollback
   has a distinct diagnostic. This is not a crash-atomic cross-directory transaction:
   the owner must journal the apply/publication and block readiness on failure or
   interruption until reconciliation succeeds. Unknown or active state must not reach it.
5. **Maintenance (Task 8).** Reset clears the record's overrides (the chosen preset stays) and bumps its
   revision; `ClearShaderCache(launch, install_path)` removes only `<exe>.dxvk-cache`. Both
   need the stopped-title check. The sheet's Clear shader cache stays simulated until wired.

### Remaining roadmap (2026-10-05)

| # | Work | Owner / blocked by | Done when |
| --- | --- | --- | --- |
| R1 ◐ | U4 rest: trigger dead zone, mouse wheel on a stick, per-game volume and mute, then the matching S9 rows | Settings track; local prospero-win branch | Done in code and host tests; wheel's Wine patch unbuilt; console check needs a test title build |
| R2 ✅ | prospero-win-profiles checker accepts the new `[input]` keys, `[environment]` and `[audio]` when the sidecar declares the feature | Settings track; local `feat/checker-runtime-features` (`646aac0`) | `check_profiles.py` tests pass |
| R3 ◐ | U5: Scaling and 120 Hz on the DXVK path. Investigated in [dxvk-scaling-and-120hz.md](dxvk-scaling-and-120hz.md): needs Wine patches (scaling behavior after 0456, a 120 Hz mode near 0460) and RADV WSI work | Blocked on a Wine/RADV build environment (`PS5_PRX_FOUNDATION`, ps5vk sources) and a test title | Console: fit, integer and stretch differ on HL2; 120 Hz reported |
| R4 | S4 live wiring: profile, `dxvk.conf` and registry applied before Play | Integration Tasks 4–6 | Console: each P1 row has a visible effect on HL2 launched from Playden |
| R5 | S6 maintenance: clear shader cache, repair, save backup, recreate bottle | Integration Task 8 | Task 8 acceptance |
| R6 | Kit adoption: U1 and U4 land upstream (needs approval to push), then the kit sets `environment` and `input_tuning` | Owner approval; prospero-win maintainers | Console: an environment variable and the dead zone reach HL2 |
| R7 | S10 community presets: fetch pinned sidecars, "Community" banner, apply and revert | U3 published (owner approval) | Apply and revert keep local overrides |

Order: S0 and the U1–U3 proposals in the first week. Then S1 → S2 → S3, which do not
need the console. S4 follows integration Task 6. S5 follows S0. S6–S10 follow their
dependencies.
