# Game settings roadmap

Date: 2026-10-05. Purpose: turn the [Wine bottle settings catalogue](wine-bottle-settings.md)
into a per-game **Game settings** UI that matches macOS Playden. Settings are
tiered by how often players change them, and each one is scheduled by what
prospero-win can actually apply today.

This document is the settings spec for two tasks in the
[integration plan](../superpowers/plans/2026-10-05-prospero-win-integration.md):
Task 4 (`game_settings.{h,cpp}`, `BuildEffectiveSettings(recipe, overrides)`) and
Task 8 (settings UI). It does not restate their lifecycle, repair or uninstall work.

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
**kit capabilities → Playden default → preset → recipe requirements → overrides**.
Recipe requirements (for example HL2's `d3d9=n` and `-game hl2`) are locked and
cannot be removed by an override; the player can only add to them.
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
| **P2** Launch-time config | prospero-win: typed env allowlist and an owned `DXVK_CONFIG_FILE` per game | New prospero-win work, then a Playden follow-up |
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

#### P2: launch-time configuration (needs prospero-win work)

prospero-win must first: forward a typed allowlist of environment variables from a
new profile section, and point `DXVK_CONFIG_FILE` at a per-game file that
Playden generates. Option names must come from the pinned DXVK 2.6.2 `dxvk.conf`.

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

## Order of work

1. P1 catalogue and `BuildEffectiveSettings` with round-trip tests against the
   upstream parser (`check_profiles.py` rules); HL2 recipe values unchanged.
2. P1 UI: sheet, picker, preset chooser, disabled and locked states; Home and
   Playden produce identical prospero profiles.
3. Bundled presets for PS5 (start with: Playden default, Mouse-driven game,
   Keyboard game with controller mapping, OpenGL game).
4. Upstream P2 prospero-win changes, then frame limit, VSync and overlay detail.
5. P3 registry overlays, then P4 per feature.
6. P5 sidecar proposal upstream, then community fetch.
