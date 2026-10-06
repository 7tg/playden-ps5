# Wine bottle settings: prospero-win contract and Playden UI

Date: 2026-10-05. Purpose: inventory the settings we can expose, identify what
prospero-win must implement, and define the UI and verification for each option.
This is a configuration reference and implementation backlog, not a claim that
every setting works on the PS5 today.

## Scope and evidence

The current-profile inventory below covers **every setting accepted by the
baseline prospero-win profile parser**, including internal fields. The remaining
tables cover Wine/bottle configuration families and graphics/input extensions
needed for a useful game-settings UI. Wine also accepts application-specific
registry values and backend-specific options; those form an open-ended namespace.
Use the versioned upstream references and the proposed capability catalogue to
extend this document rather than treating an arbitrary registry editor as a
complete, supported settings API.

Baseline: wrapper/source `54caac176e86c60c7c407972583b491f67838ed9`;
tested Wine artifacts `e16ec8f3a19dec4a1162e7554ab73ce41f83bdb9`; DXVK 2.6.2.
Per-game descriptor/session work is underway and is identified as proposed here.
Settings found in newer source patches are not proof that the retained Wine
artifacts contain those patches.

Primary local evidence:

- prospero-win `src/pw_app_profile.{h,c}`: application fields and strict parsing.
- `src/pw_game_profile.{h,c}`: complete display/input/debug profile schema.
- `native/wine64_main.c`: environment forwarding, backend conditions and lifecycle.
- `src/pw_wine_launch.{h,c}`: command-line parsing and argument serialization.
- `docs/CONTROLS.md`, `wine/patches/`, `tools/pw_prefix.py`, `tools/pw_install.py`:
  input limitations, PS5 switches and offline preparation.
- [Runtime owner's hardware report](../../../ps5-homebrew/prospero-win/docs/FW1360_HALFLIFE2_REPORT.md)
  and [integration plan](../superpowers/plans/2026-10-05-prospero-win-integration.md).

Upstream Wine configuration establishes the general families of Windows-version,
library, graphics, drive and audio preferences. Its desktop backends differ from
the PS5 driver; these references establish possible mappings, not PS5 support.
See [Wine configuration sources](https://github.com/wine-mirror/wine/tree/master/programs/winecfg).
Resolve exact keys and accepted values against the **pinned Wine source** when
implementing registry overlays; do not silently follow master.

## Status, ownership and application rules

`feat/game-settings` owns the Playden settings model, persistence, profile writer
and settings UI, including its prospero-win/dependency patches. This catalogue
is the shared requirements reference; the integration branch owns kit/bottle
validation, installation, launch and lifecycle services. Use the owner's
`BuildEffectiveSettings` and `RenderProsperoProfile` through the injected profile
publisher rather than implementing another settings model. The
[integration boundaries](../superpowers/plans/2026-10-05-prospero-win-integration.md)
record revision-width, recipe-identity and capability requirements.

The owner's branch also contains settings persistence, environment-variable UI,
community profile sidecars and shader-cache actions (inspected at `7937f02`).
Their presence in the UI does not establish support in the retained Wine kit.
Reconcile each entry below against the combined parser/runtime implementation
and its acceptance evidence before changing its support status. Dependency
patches must join the per-title wrapper changes before the resulting source and
kit are pinned.

| Status | Meaning |
| --- | --- |
| Existing | Parser and wrapper consume the setting. Hardware support still depends on backend and kit. |
| Conditional | Existing field, but only some backends or paths use it. |
| Missing exposure | Underlying Wine/backend mechanism exists; no supported prospero-win profile/API path exposes it. |
| Missing backend | Requires PS5 driver/runtime functionality, not only a new profile key. |
| Preparation | Change a validated offline bottle/template or immutable kit; not a normal launch toggle. |
| Application | A game-specific option or argument; requires a recipe, not a universal Wine mapping. |
| Managed | Installer/lifecycle owns the value. Display in diagnostics where useful; do not make it a free-form player setting. |

### Native persistence gate (2026-10-06)

The settings core now uses the explicit checked absolute-path PS5 backend.
FW13.60 relative `openat` returned `EINVAL`, and tested descriptor aliases
returned `ENOENT`; see [the directory investigation](../../results/20261006-private-session/native-directory-alias.log).
The replacement's private-directory, publication, readback, hashing, named
removal and enumeration primitives passed on PS5 under the restored app UID.
Game settings migration and save/reopen also passed natively. These checks do
not establish a complete native game launch or a cross-directory transaction.

| Operation | Current implementation / remaining reason | Implementation needed | Acceptance |
| --- | --- | --- | --- |
| Load/save game settings | Migrated to the measured backend. Owned legacy 0644 reads are admitted; replacements are `0600` under private `0700` parents. Native migration/save/reopen/no-overwrite/source-preservation passed. | Host and PS5 callers provision a private settings child and import legacy records explicitly; existing malformed private records block writes. | Validate the live application path as well as API fixture evidence; retain corruption, symlink and foreign/public-parent refusal. |
| Apply profile/registry/DXVK configuration | Filesystem calls now use the managed backend; host tests cover registry backup, mode migration and DXVK rollback. Native combined application/publication remains untested. | Connect the existing model/writers to the OWNER install journal while the title is stopped, then publish matching profile/descriptor/revision. | Apply/reopen/reset on PS5; interrupt publication and reconcile without partial readiness. |
| Clear shader cache | Named deletion now uses the managed backend; host tests retain scope and symlink checks. Native targeted cache lifecycle remains untested. | Connect confirmed stopped-title checks and owned cache roots to the live maintenance action. | Clear only the selected game's cache on PS5; preserve saves and other games and refuse unknown/running activity. |
| Prepare private bottle | Actual private-directory creation and hashing passed natively. The installer cleanup suite also passes with relative operations unavailable; full native kit copy/retry is still open. | Provision the durable OWNER repository's dedicated private root and wire installer/registration to independently admitted kit inputs. | Fresh app-owned `0700` bottle and `0600` files, validated copy, retry/recovery and foreign-tree refusal on PS5. |

See [managed filesystem contract](native-managed-filesystem.md) and
[native diagnostic evidence](../../results/20261006-managed-filesystem/README.md).

### Required detail for unsupported and partial settings

Keep unavailable settings in this document as implementation backlog entries.
Every unsupported, conditional or unverified setting must state:

1. **Reason:** the concrete missing parser field, wrapper mapping, preparation
   operation, runtime dependency, driver capability or hardware evidence.
2. **Implementation location:** prospero-win profile/parser/wrapper, Wine PS5
   backend, graphics/input/audio backend, host bottle preparation, or Playden
   settings/persistence/UI. A Playden control alone cannot enable a missing
   runtime mechanism.
3. **Work needed:** the configuration binding and backend/preparation changes
   required, including architecture and version constraints where applicable.
4. **Acceptance:** the fixture or PS5 behavior that proves the setting works,
   including reset and persistence where relevant.

The tables below provide these details in their reason, implementation and
verification columns. Do not reduce an entry to an unexplained “unsupported,”
remove it because it is unavailable, or treat lack of testing as proof that Wine
cannot implement it. If capability is unknown in the retained artifacts, record
that uncertainty and investigate the exact binary/source revision before choosing
between exposing an existing mechanism and implementing a backend.

For example, frame-rate limits need a prospero-win configuration-file mapping
and Playden persistence/UI; DXVK high refresh needs graphics/display backend work
as well; Windows-version selection needs offline registry overlays and a typed
Playden setting. These remain separate implementation tasks even when all three
are currently unavailable in the UI.

All profile/environment/display/input changes take effect on the **next launch**
unless an explicitly tested live-update mechanism is added. Registry, dependency,
drive, bottle and runtime changes require the title to be confirmed stopped.
Unknown activity blocks mutation. Restart required is a property of each setting,
not a suggestion to restart Wine while a game is using the bottle.

Effective configuration: kit capabilities → recipe defaults/requirements → user
overrides → validated generated profile and bottle overlay. Store user overrides
separately from recipe defaults so reset and recipe upgrades preserve intent.
An absent override means “use recipe default”; explicit false, zero or disabled
must not be confused with absence.

Use separate actions for **reset settings**, **repair dependencies**, **clear
shader cache**, **restore backup** and **recreate bottle**. Only the last can
replace mutable bottle state; back up saves first. Keep Home and Playden launches
on the same persisted effective configuration and settings revision.

## 1. Complete existing profile inventory

Profiles are strict INI, at most 8192 bytes. Unknown/duplicate fields are rejected.
Adding a UI control requires matching parser, serializer and wrapper changes;
writing an extra INI key is not sufficient. String limits below exclude the NUL.
Application identifiers use lowercase letters/digits with hyphen/underscore;
the first character must be a letter/digit. Input preset names also use lowercase
letters/digits/hyphen/underscore and must pass the actual preset parser.

### Application and launch

| Field | Values / default / limit | Status and actual behavior | UI and implementation work |
| --- | --- | --- | --- |
| `[application] id` | Required identifier; ≤64 bytes | Managed profile identity | Read-only diagnostics. Keep stable across user setting changes. |
| `name` | Required; ≤96 bytes | Existing display name | Use game name; optional rename affects title metadata through a separate installer operation. |
| `executable` | Required absolute DOS path; ≤259 bytes | Existing launch target | Launch-entry selector. Validate PE and resolve within the recorded install; parser path validation alone does not establish ownership. |
| `working_directory` | Required absolute DOS path; ≤259 bytes | Existing process working directory | Advanced folder picker constrained to install/approved roots; verify relative file access. |
| `arguments` | Optional; ≤512 bytes and at most 32 launch words | Existing limited splitting: spaces/tabs separate words; double quotes group and are removed; unclosed quotes fail; no escape syntax | Editable additional arguments, plus read-only effective command. Preserve recipe-required arguments. Define/test compatible serialization or extend parser for escaping; do not assume shell quoting works. |
| `prefix` | Required identifier; ≤64 bytes | Managed selection of prepared prefix | Per-game value `game`; read-only bottle information. Changing a string does not create/migrate a bottle. |
| `runtime` | Required identifier; ≤64 bytes | Conditional: stored identifier; current native wrapper resolves a fixed bundled runtime tree | Curated runtime selector requires kit lookup, compatibility validation and staged replacement/rollback. Arbitrary labels currently do not select another Wine installation. |
| `architecture` | Required `pe32` / `pe64` | Existing metadata; PE32 relies on matching WoW64 runtime/template | Show detected architecture. Changing this does not convert an executable or bottle. Validate against PE header. |
| `graphics` | Required `auto`, `gdi`, `dxvk`, `opengl` | Conditional backend intent. OpenGL has explicit wrapper activation; other modes rely on module/DLL state and overrides rather than actively switching/installing a backend | Advanced compatible-backend selector needs dependency/load-order implementation. DXVK requires matching DLLs. GDI is not a generic accelerated Direct3D fallback. Confirm each kit/backend before exposing. |
| `dll_overrides` | Optional; ≤256 bytes; `WINEDLLOVERRIDES` form | Existing environment override; OpenGL composes/requires builtin `opengl32` | Structured DLL/load-order editor. Add semantic validation of entries and DLL availability; character validation alone is not full syntax/compatibility validation. Preserve recipe-required overrides. |
| `startup_command_id` | Optional integer 0–65535; default zero | Missing exposure/wiring: parser stores it, but native wrapper does not consume it | Propagate to an implemented Wine initial-window command mechanism and test delivery exactly once. Recipe/developer-only; adding the field currently does not inject a command. |

### Display and rendering

| Field | Values / default | Status and limitation | UI / missing implementation / acceptance |
| --- | --- | --- | --- |
| `[display] desktop` | `WIDTHxHEIGHT`; parser bounds 320×200–3840×2160; omitted uses driver default | Conditional virtual-monitor size, forwarded as `WINE_PS5_DESKTOP`. Parser bounds exceed demonstrated presentation support. | Resolution list from kit+recipe. HL2 baseline 1920×1080. Coordinate game arguments/display config; test actual rendered size. A 4K parser value is not 4K support. |
| `scaling` | `fit` default, `integer`, `stretch` | Conditional: wrapper passes scaling to CPU-frame and exported Vulkan-frame presentation; Wine/RADV swapchain/display-plane presentation is a separate path | Expose tested path-specific modes. Universal behavior needs mode propagation into independent swapchain/display-plane presentation; test aspect/pixel ratios across paths. |
| `view` | `window` default / `desktop` | Existing PS5 driver selection of visible windows versus whole virtual desktop | Advanced “Game windows / Entire desktop”. Test secondary dialogs, cursor bounds and crop behavior. This is separate from game fullscreen. |
| `show_fps` | `true` default / `false` | Conditional: DXVK FPS HUD or Mesa HUD for OpenGL; not a universal GDI overlay | Basic FPS toggle when supported. Expose observed overlay capability, not benchmark or guaranteed frame rate. |
| `refresh` | 60 default / 120 | Conditional **OpenGL only**; display may remain 60 if 120 unsupported | Backend/display-gated refresh selector. DXVK 120 Hz needs RADV/display-mode plumbing and measured output verification; setting this field currently does not request DXVK 120 Hz. |
| `opengl_thread` | `false` default / `true` | Conditional Mesa OpenGL worker-thread option; ignored for DXVK/GDI | Advanced OpenGL-only toggle. Verify driver support, correctness and pacing with the pinned Mesa build. |

### Controller, keyboard and pointer

| Field | Values / default | Status and limitation | UI / verification |
| --- | --- | --- | --- |
| `[input] preset` | Identifier ≤64 bytes; optional | Existing shared `.input` preset reference; profile overlays preset | Controller-preset selector. Copy referenced file into this game's library; expose effective settings and missing-preset error. |
| `mode` | `keyboard` / `xinput` | Existing. If unspecified and no binding/pointer is set, effective mode becomes XInput; otherwise keyboard | “Xbox controller / Keyboard and mouse mapping”. Make explicit in generated profile to avoid implicit changes when a binding is added. XInput can also send configured keyboard bindings. |
| `mouse` | `none` default / `left_stick` / `right_stick` | Existing stick-to-pointer mapping | Pointer-stick selector; explain native gamepad look and pointer mapping can both affect input. |
| `mouse_speed` | 1–20000 pixels/s; effective initialized default 1200 | Existing pointer speed, not universal in-game camera sensitivity | Slider/numeric field, shown only with pointer stick. Test movement across resolutions. |
| `cross`, `circle`, `square`, `triangle`, `l1`, `r1`, `l2`, `r2`, `l3`, `r3`, `up`, `down`, `left`, `right`, `options`, `create`, `touchpad` | Each optional: named key, letter/digit, F1–F24, nonzero `vk:0xNN`, `mouse_left/right/middle`, `none` | Existing 17 digital button bindings; unset inherits preset | Button editor with inherit/none distinction. Single target per binding; these are not native XInput remaps or analog trigger curves. |

Named keys include space, enter, escape, tab, backspace, shift, ctrl, alt, pause,
pageup/down, home/end, insert/delete, arrows and punctuation names. Source of
truth is the `keys[]` table in `src/pw_game_profile.c`. Binding a modifier and a
second key to separate buttons works as simultaneous inputs; a one-button chord
or timed macro needs new functionality.

### Troubleshooting

| Field | Values / default | Status | UI / missing implementation |
| --- | --- | --- | --- |
| `[debug] winedebug` | ≤127 bytes; omitted uses wrapper's compile-time channels, baseline `err+all,+loaddll,+process` | Existing per-game `WINEDEBUG` | Normal/verbose/channel presets and advanced editor. Validate grammar, bound disk use, retain session/version metadata, restore temporary tracing after diagnosis. |

## 2. Bottle and Wine configuration not exposed by the profile

“Missing exposure” below does **not** mean Wine lacks the feature. It means we
have no typed, supported prospero-win path to apply it, preserve it and verify it
on this PS5 runtime. Offline editing must preserve unrelated registry sections
and the 32/64-bit views; do not duplicate all registry sections indiscriminately.

| Setting family / proposed UI | Status and reason it is unavailable now | Work required in prospero-win / preparation | Verification before enabling |
| --- | --- | --- | --- |
| Windows version emulation | Missing exposure: no profile field or managed registry overlay | Add version enum derived from pinned Wine; apply global/per-executable version settings, including version-report registry values consistently | A PE fixture reports expected version; game still launches; reset restores inherited default |
| Per-executable compatibility overrides | Missing exposure: one generated profile does not expose Wine AppDefaults overlays | Address settings by executable basename and registry scope; define precedence with bottle-wide values | Two executable fixtures show different effective settings without leakage |
| DLL load order in bottle registry | Preparation/missing exposure: profile environment exists, but registry changes are unmanaged | Structured overlay with inheritance and native/builtin/disabled choices; define environment precedence | Both architectures load intended module; remove override restores default |
| Additional environment variables | Missing exposure: wrapper forwards a fixed curated list and strict parser has no environment section | Typed allowlist, size/count bounds and next-launch forwarding; reserve prefix/runtime/user/loader variables for installer | Childless Wine entry sees exact values; prohibited identities cannot be overridden; Home equals Playden |
| Language / locale / code page | Missing exposure: no persisted locale preference or verified PS5 locale mapping | Enumerate installed locales; map supported environment/Windows locale registry settings and fonts; recipe may also set game language | Unicode/code-page fixtures and a localized game render/read filenames correctly |
| Time zone | Missing exposure: no bottle-scoped timezone model or verified runtime mapping | Define host inheritance vs explicit override and pinned Wine mapping; persist independently of game locale | Windows time APIs report expected offset and transitions |
| DPI / text scale | Missing exposure: profile resolution is not Wine DPI | Add bounded DPI overlay and driver-aware font metrics; verify registry mapping against pinned Wine | Text, dialogs and mouse coordinates remain correct at each allowed scale |
| Font packs, substitutions and smoothing | Preparation/missing exposure: no dependency/font catalogue or supported overlay | Bundle licensed fonts; install font registration/cache offline; allow tested substitution/smoothing presets | Glyph coverage, metrics and settings survive restart without replacing user files |
| Desktop theme, colors and appearance | Missing exposure; Windows desktop appearance can exist in a bottle, but no supported PS5 preference or packaged theme contract exists | Add optional licensed theme resources and typed appearance overlays only for actual Wine controls/desktop components in the kit | Dialog/control rendering and reset work without affecting game shaders |
| Wine virtual desktop enable/name | Conditional: current PS5 driver already supplies a virtual monitor and `view`; desktop Wine explorer behavior is not a supported equivalent | Define PS5-specific semantics first; add desktop management only if runtime can support the required explorer/process behavior | Dialogs/window positioning work; no unsupported child-process dependency |
| Decorated windows / window manager controls | Missing backend for desktop Wine equivalents: PS5 does not use an X11 window manager | Implement required PS5 window decoration/focus behavior; copying Wine X11 registry toggles alone has no established effect | Focus, move/resize/close and fullscreen transitions work on PS5 |
| Capture pointer in fullscreen | Missing exposure/backend parity: no profile capture preference; PS5 driver differs from X11 | Add driver cursor confinement/relative-input contract and typed profile setting | Cursor cannot escape intended bounds; unlock on menu/exit; no stuck input |
| Audio enabled / mute / per-game volume | Missing exposure: audio bridge opens its port without profile mute/gain | Add sink gain/mute with clipping-safe mixing; distinguish from game master volume and console system volume | Sound amplitude/mute changes correctly; no clipping; survives next launch |
| Output/voice device selection | Missing backend/exposure: no enumerated selectable PS5 endpoint contract | Enumerate actual native routes, expose stable IDs, map Wine endpoint preference and fallback | Device switch/disconnect produces correct output or actionable fallback |
| Microphone / voice input | Missing backend: inspected native wrapper installs an output sink; no matching configurable capture route is established | Native capture → Wine capture endpoint, permissions, lifecycle and device enumeration | Record/playback and an in-game voice capture fixture |
| Speaker layout / surround | Missing backend/exposure: baseline sink is stereo; no selectable multichannel path | Extend channel mapping/port/backend; expose only actual supported layouts | Per-channel fixture verifies channel order and game negotiation |
| Sample rate / buffer size / latency | Managed backend constants, not profile fields; arbitrary values can violate sink format | Capability-negotiated audio configuration and resampling/buffer contract; retain compatible defaults | Format conversion, underrun counts and measured latency under load |
| MIDI / soundfont / DirectSound/XAudio backend | Preparation/backend dependent: no curated selectable implementation or resource mapping | Establish module/backend support, package soundfont/runtime dependencies, then add tested presets | API-specific audio fixtures; selecting a label alone cannot add implementation |
| Drive letters, type, label and serial | Preparation: prefix tables exist, but no safe UI drive-mapping transaction | Typed mappings inside approved game/storage roots; reuse `.pw-symlinks` conversion; preserve mandatory C:/Z: mappings | File access works after reopen and volume loss; escaping paths rejected |
| Windows user folders / Documents / Saved Games | Managed/preparation: template expects user `prospero`; arbitrary remapping can break saves and Cloud | Controlled backed-up folder migration; update registry, mapping and CloudLocation together | Save/load and Cloud conflict tests before/after migration |
| Bottle architecture / WINEARCH | Preparation: fixed WoW64 startup and matching 32/64-bit trees; not mutable after creation | Offer only compatible prepared templates; validate PE architecture and WoW64 CPU selection | PE32/PE64 fixtures launch; wrong template rejected before registration |
| `WINEPREFIX`, `HOME`, `USER` | Managed: startup selects the prepared prefix and Windows user; these are not ordinary environment overrides | Derive from owned install/template, reserve names in any environment editor, migrate state through bottle operations | Correct prefix and user save paths; no shared-bottle leakage |
| Redistributables (.NET, VC++, DirectX components, codecs) | Preparation: runtime cannot assume ordinary installer child-process support | Versioned host-side installers/overlays, hashes, licenses and rollback; recipe declares exact dependencies | Required APIs initialize in an offline fresh template; no setup runs implicitly on PS5 |
| COM registrations, crypto and WoW64 CPU repair | Preparation: essential baseline correctness, not player preferences | Validate/repair precise keys and correct registry views offline; preserve unrelated entries | MMDeviceEnumerator, DirectInput8, crypto and WoW64 fixtures pass |
| Registry editor / import export | Missing exposure: no transactional, scoped editor/API | Export backup, validate types/view/allowed scope, preview diff and atomically apply while stopped | Failure leaves original registry readable; restore works; no host personal paths |
| File associations / shell integration | Preparation/backend dependent: Windows shell/process launch not generally available | Implement only a concrete required game workflow; declare unavailable external helpers | Association opens supported target without requiring unsupported subprocesses |
| DLL search paths / loader paths | Managed: wrapper constructs bundled runtime paths; arbitrary paths undermine reproducibility | Kit-owned loader configuration, separate from optional game DLL overrides | Loaded module hashes/architecture match selected kit |
| Bottle directory / game storage volume | Managed: per-game bottle root fixed internally; game files use selected volume | Migration transaction with ownership, space, mount and Cloud checks; descriptor update only after commit | Both launch paths work after move; disconnect never creates false readiness |
| Wine/runtime version | Preparation: current `runtime` label does not replace native/PE modules | Curated immutable kits with template/schema capabilities, staged title variant update and rollback | Old/new runtime coherence, saved state compatibility and failure rollback |
| Synchronization mode: esync/fsync/ntsync | Missing backend for Linux mechanisms: PS5 is not a Linux kernel with those primitives | Do not forward Linux/Proton toggles as working features; implement/validate PS5 equivalent or use existing PS5 switches below | Mutex/event/semaphore wait, cancellation and lifetime stress tests on PS5 |
| CPU count / affinity / reported topology | Missing exposure/backend: no generic profile setting; Wine topology and native scheduler are separate | Typed topology override if supported by pinned Wine, plus actual PS5 affinity adapter if needed | Reported topology and actual scheduling measured separately; game correctness |
| Memory/VRAM limits | Missing generic mechanism: reported GPU memory is not an enforced allocator budget | Separate reported compatibility values from actual native/RADV allocation budgets; expose validated capabilities only | Memory pressure/failure recovery; diagnostics distinguish reported and allocated bytes |
| Large-address awareness / PE patching | Application/preparation: modifying executable is not a normal Wine switch | Prefer existing game option/recipe; any binary edit needs explicit recipe provenance, original backup and integrity handling | Hash/PE diff, memory behavior and rollback; avoid automatic game binary changes |
| Network proxy / offline mode / adapter identity | Missing exposure and potentially backend gaps; game offline arguments differ from Wine networking | Separate game offline recipe from validated network/WinINet/WinHTTP configuration and implemented socket capability | Relevant API fixture and game networking; no credentials in profile/descriptor |
| Steam/DRM compatibility | Application/backend limitation: passing `-steam` cannot add unsupported Steamworks/launchers | Recipe-specific verified execution/dependencies; preserve shipped DLLs; reject incompatible process/service requirements | Actual entitled game startup; never imply universal DRM compatibility |
| Child processes / launcher support | Missing backend: ordinary process creation is unavailable in this runtime model | Runtime architecture work to support spawning/loading/handoff, not a UI boolean | Parent/child/wait/exit fixtures plus required launcher behavior |

General Wine graphics preferences include DPI; X11 preferences cover window
management and mouse capture. The latter cannot be assumed to control the PS5
driver. See [Wine graphics implementation](https://github.com/wine-mirror/wine/blob/master/programs/winecfg/graphics.c)
and [X11 configuration implementation](https://github.com/wine-mirror/wine/blob/master/programs/winecfg/x11drvdlg.c).
Windows-version application scopes, DLL orders, drive metadata and audio-device
preferences are implemented in Wine's [application](https://github.com/wine-mirror/wine/blob/master/programs/winecfg/appdefaults.c),
[library](https://github.com/wine-mirror/wine/blob/master/programs/winecfg/libraries.c),
[drive](https://github.com/wine-mirror/wine/blob/master/programs/winecfg/drive.c)
and [audio](https://github.com/wine-mirror/wine/blob/master/programs/winecfg/audio.c) configuration sources.

## 3. Graphics extensions: concrete missing mappings

For **DXVK 2.6.2**, frame caps use `d3d9.maxFrameRate` / `dxgi.maxFrameRate`;
presentation overrides use `d3d9.presentInterval` / `dxgi.syncInterval`;
queue controls use `d3d9.maxFrameLatency` / `dxgi.maxFrameLatency`.
These are version-specific candidates, not working PS5 profile settings.
The authoritative option families and bounds are in [DXVK 2.6.2 configuration](https://github.com/doitsujin/dxvk/blob/v2.6.2/dxvk.conf).
Its [README](https://github.com/doitsujin/dxvk/blob/v2.6.2/README.md) documents
configuration-file/environment and HUD mechanisms. Do not copy newer DXVK option
names into this kit without checking the pinned version.

| Setting / proposed UI | Why unsupported or incomplete now | Implementation needed | Acceptance |
| --- | --- | --- | --- |
| Frame-rate limit | No profile field, generated DXVK configuration or per-game forwarded configuration path | Emit validated API-specific cap; route `DXVK_CONFIG_FILE` to an owned file; define independent OpenGL limiter if wanted | Measured pacing/cap in game; 0/inherit/disabled semantics round-trip; Home/Playden equal |
| VSync | No generic wrapper toggle; game's setting, DXVK interval and PS5 present scheduling can differ | API-specific DXVK mapping plus driver present-mode capabilities; explicit game/default/on/off semantics | Measure tearing/pacing; verify effective present mode, not only config-file contents |
| Frame latency / buffering | No exposed queue controls or tested PS5 interaction | Add API-specific DXVK queue setting, bounds and recipe exclusions | Input/present latency and stability under both GPU/CPU load |
| Tearing policy / mailbox modes | Available modes not established by a UI flag; RADV/display-plane constraints matter | Query supported present modes and implement actual selection/fallback | Requested vs effective mode visible; incompatible modes rejected |
| DXVK 120 Hz / VRR | Existing `refresh` branch is OpenGL-only; no proven DXVK output negotiation | RADV/display-plane refresh/VRR implementation and display capability reporting | HDMI output/pacing verification on compatible and incompatible displays |
| HDR / color space / bit depth | No verified PS5 end-to-end HDR mapping; advertising DXVK HDR alone does not implement output | Wine/DXVK surface colorspace → RADV → VideoOut format/metadata, with SDR fallback | HDR patterns/game output, metadata and fallback correct |
| Fullscreen / borderless / windowed | No universal profile mapping; game arguments/config and PS5 window presentation are distinct | Recipe-specific game mode plus driver transition support; keep `view` separate | Resize/focus/menu/exit behavior for each allowed mode |
| Upscaling: FSR/NIS/integer at Vulkan layer | Existing software scaling is not an upscaler; no general postprocess path | Implement swapchain/display-plane upscale/sharpen pass and inputs; disclose render versus output resolution | Image/cursor/aspect correctness and measured cost |
| Antialiasing, anisotropic filtering, LOD | No managed DXVK config; game defaults and API-specific overrides differ | Curated version-specific graphics options, ranges and API gating | Known scene/image comparison; reset preserves recipe requirements |
| GPU identity / reported VRAM / feature level | No typed configuration; reporting capabilities beyond implementation can break games | Recipe-controlled DXVK reporting overrides; distinguish spoofed reports from implemented features | API capability fixture + target game; no claim that spoofing creates support |
| Shader/pipeline cache location and enablement | No owned configurable cache policy; behavior depends on actual DXVK/driver build | Identify caches used by pinned artifacts, forward only supported paths, persist kit/game scope and migration rules | Warm/cold runs confirm cache use; read-only/offline failures handled |
| Clear shader cache | No lifecycle/ownership-controlled maintenance operation | Delete only known cache paths while stopped; preserve saves, profile and bottle registry | Recompilation occurs; saved game loads unchanged |
| Shader compiler thread count | DXVK has a setting, but wrapper has no validated route or PS5 scheduling evidence | Add curated config and CPU bounds; default to kit behavior | Compilation load, stutter and game stability measured |
| Extended HUD (frametime, memory, compiler activity) | Profile only exposes boolean FPS HUD; no element selection | Typed backend-specific HUD elements and validated environment/config precedence | Each metric appears and reset removes it; OpenGL availability kept separate |
| Driver/renderer selection (RADV/Mesa/software) | Runtime modules fixed by kit; no interchangeable driver selector | Kit variants and actual driver/module selection, capability validation and rollback | Driver identity and rendered output confirm selected variant |
| D3D12 / VKD3D backend | No supported `graphics` value/dependency/runtime capability established | Implement/package compatible VKD3D and Vulkan requirements; extend parser/launcher/recipe | D3D12 feature/startup/render tests; adding enum alone is insufficient |
| WineD3D compatibility options | No dedicated configurable WineD3D path in current graphics enum | Establish whether pinned modules implement usable backend, then add exact typed registry/settings mappings | D3D fixtures prove selected backend; do not treat `gdi` as WineD3D selection |
| Remaining DXVK driver workarounds | Upstream options exist, but unrestricted free-form settings bypass version/API validation | Generate reviewed option catalogue from pinned configuration/source, grouped by API; recipe defaults and advanced expert view | Reject unknown/type-invalid keys; target-game regression for every enabled workaround |

The final row includes device filtering/reporting, surface-creation workarounds,
shader semantics, synchronization/barriers, resource placement, pipeline
compilation/lifetime, memory management, D3D9 format/emulation and D3D11 feature
compatibility options. Keep their exact catalogue versioned with DXVK; separate
recipes can require different defaults. It is unnecessary to present every driver
workaround as a permanent top-level player control.

## 4. Input extensions and missing PS5 backend support

| Setting / action | Why unsupported now | Required implementation | Acceptance |
| --- | --- | --- | --- |
| Stick deadzones / response curve | No profile fields for per-game calibrated native-XInput or pointer curves | Typed inner/outer deadzones and curves in appropriate input transform; avoid double-applying game deadzones | Drift-free center and full-range output fixture |
| Stick sensitivity / invert axes | Pointer speed exists; native XInput camera sensitivity is game-owned; no generic axis transforms | Separate pointer gain/inversion from XInput transforms and game-recipe sensitivity | Axis sign/range and in-game feel verified |
| Native XInput button/stick remapping | Digital profile bindings emit keys/mouse; native pad mapping is fixed | Transform native controller state before Wine pad publication, including axis/trigger swap | XInput fixture reports correct buttons/axes and no duplicate inputs |
| Trigger threshold / analog curves | Keyboard bindings are digital; no exposed trigger response model | Typed analog transform and independent digital activation/release thresholds | Full-range values and no chatter at threshold |
| Rumble enabled / strength | Runtime forwards game rumble; no per-profile gate or gain | Clamp/scale motor output, stop on exit/disconnect; persist preference | Motor levels and stop behavior verified |
| Controller slot / multiple controllers | Current wrapper uses its controller/user path; no configurable multi-pad contract | Enumerate pads/users, route to Wine slots and define reconnect behavior | Multi-controller fixture and slot persistence |
| DirectInput controller mode | Repairing DirectInput8 COM registration does not prove controller enumeration; profile only has keyboard/XInput | Confirm actual enumeration path; implement missing PS5 driver/device bridge if absent | DirectInput fixture enumerates and reads controller, not merely COM creation |
| Raw Input devices | Existing controls docs identify raw-input limitations; XInput workarounds are not raw-input implementation | Device enumeration, HID identity/data and Wine raw-input message/API bridge | Raw-input fixture and an SDL game using that path |
| Mouse wheel / extra buttons | Controls docs explicitly say wheel does not work; bindings list only three mouse buttons | Extend HID event decode, Wine event representation and forwarding; add profile binding vocabulary | Wheel direction/delta and extra-button fixture |
| Relative mouse / pointer capture preference | USB pointer input exists; configurable relative/capture semantics unproven | PS5 driver relative events/confinement and lifecycle; separate from stick-to-pointer speed | FPS mouse look/menu transitions with no stuck capture |
| Keyboard layout / remapping | Keys arrive through HID; no bottle/profile layout selector or translator | Map supported layouts and text/virtual-key semantics; separate hotkeys from typed text | Non-US layout, modifiers and text fixture |
| Chords / macros / turbo | Single target per binding; script replay is developer testing, not player macro API | Explicit multi-event/timing schema, bounded scheduling and release-all handling | Held/modifier/exit/disconnect cases do not leave stuck keys |
| Touchpad gestures / gyro | Profile exposes touchpad button only; no gesture/gyro processing contract | Native sampling, calibration and typed action/axis transforms | Gesture/gyro fixtures, reset calibration and disconnect handling |
| Adaptive triggers / advanced haptics | Existing XInput rumble carries motor amplitudes, not DualSense-specific effects | Native effect API plus game/recipe trigger source; expose capability separately from rumble | Effect resets on exit; unsupported games show unavailable |

## 5. PS5-specific Wine switches: developer and recipe settings

These mechanisms appear in baseline **source patches**. They are absent from the
profile schema and must be checked against the retained Wine binary revision
before exposing them. Some use prefix marker files as well as environment
variables; introduce a single typed preference and explicit precedence rather
than maintaining contradictory copies.

| Source mechanism | Baseline source behavior / purpose | Missing work and reason to keep out of basic UI |
| --- | --- | --- |
| `WINE_PS5_SERVER_DIRECT` | Default direct request path when export available; `0` disables | Recipe/developer toggle requires exact binary export/capability checks and sync correctness tests |
| `WINE_PS5_MUTEX_FAST` / `pw_mutex_fast` | Immediate mutex fast path; source default off | No profile forwarding; experimental behavior needs mutex/race/lifetime regression tests |
| `WINE_PS5_MUTEX_SHARED` / `pw_mutex_shared` | Shared mutex backend; source defaults enabled after patch 0880 | Add typed override and show effective fallback; benchmark alone does not prove concurrency correctness |
| `WINE_PS5_SYNC_SHARED` / `pw_sync_shared` | Event/semaphore shared synchronization; source defaults enabled after patch 0888 | No managed profile API; test cross-thread waits, cancellation, cleanup and compatibility |
| `WINE_PS5_IMAGE_VIEW_FD_RELEASE` / `pw_image_view_fd_release` | Source default enabled after patch 0880; descriptor cleanup | Developer escape hatch requires descriptor-lifetime tests; do not label generic performance boost |
| `WINE_PS5_DIR_LISTINGS` | Cached directory listings when server change sharing works; `0` disables | Add only if binary supports it; verify visibility after rename/create/delete and cross-thread changes |
| `WINE_PS5_WAIT_WATCHDOG` | Presence enables wait/slow-operation diagnostics | Compile-time wrapper option today; expose bounded temporary diagnostic setting and clear by unset, not string `0` |
| `WINE_PS5_TRACE_STARTUP` | Wrapper enables startup tracing | Add reviewed debug policy only if log volume needs configuration; keep essential failure breadcrumbs |
| `WINE_PS5_NTDLL_DIR` | Internal loader directory | Managed kit path; not a user override |
| `WINE_PS5_DESKTOP`, `WINE_PS5_VIEW`, `WINE_PS5_OPENGL`, `WINE_PS5_GL_REFRESH` | Derived from existing display/backend fields | Use their typed profile settings; do not create duplicate environment controls |
| `SDL_JOYSTICK_RAWINPUT=0` | Wrapper workaround for XInput mode and relevant SDL games | Recipe-scoped compatibility behavior; no generic “SDL support” toggle or proof of raw-input support |

Kernel provider selection, credential offsets, helper paths, root grant, startup
watchdog build flags, scripted test input/cycles, native heap/stack size and module
paths remain build/install/developer responsibilities. If needed, expose them in
a diagnostic kit configuration with its own verification, not a per-bottle
player preference. Changes to these may require rebuilding native artifacts.

## 6. Lifecycle, saves and maintenance preferences

| Preference / action | Current gap / reason | Required contract and UI |
| --- | --- | --- |
| Default launch entry | Per-game descriptor is being implemented; generic mode uses catalog/arguments | Persist one default profile; Home needs no arguments; same effective settings from Playden |
| Exit behavior | Generic baseline restarts catalog; per-game exit-to-Home is underway | Keep `exit-title` managed. Return-to-Playden requires a proven native return API/handoff; no toggle until implemented |
| Cloud enabled / offline play | Legacy Cloud assumes another prefix path; Home has no Playden prelaunch sync | Map actual bottle, account and session; explicit play-without-sync; next-open reconciliation only after title stops |
| Save retention / bottle reset | No complete owned maintenance transaction yet | Default preserve saves; separate delete-saves choice; backup and explicit confirmation for destructive reset |
| Repair / dependency reset | No typed operation proving what is replaced | Revalidate kit/recipe and preserve saves/overrides; report precise changed files and failures |
| Runtime/recipe update and rollback | Source pin alone cannot safely update existing installs | Stage immutable versions, record previous manifest and bottle backup if migration required |
| Log retention / export | Per-session records underway; no complete UI retention/export policy | Bounded logs, export versions/effective settings; redact account/session secrets and personal save data |

## 7. Proposed prospero-win API and UI schema

This is proposed work. No `ListSettings`/`ApplySettings` interface exists today.
Keep the C runtime bounded; a host-generated JSON catalogue can drive Playden's
UI without embedding a generic configuration engine in Wine.

Each catalogue entry needs:

| Property | Purpose |
| --- | --- |
| `id`, `label`, `description`, `group` | Stable persisted identity and understandable UI text |
| `type`, `allowed_values`, `min`, `max`, `unit` | Typed control and validation; include inherit separately from explicit values |
| `default`, `effective_value`, `source` | Display recipe/kit/user origin and final effective setting |
| `scope` | Profile, bottle registry, game config, kit, native backend or maintenance action |
| `support_status`, `reason_code`, `reason_detail` | Actionable unsupported reason; keep backlog item linked |
| `requires`, `conflicts`, `recipe_required` | Backend/architecture/display dependencies and locked recipe requirements |
| `apply_mode` | Next launch, stopped-bottle mutation, offline preparation, kit replacement or verified live update |
| `binding` | Exact profile key, allowed environment name, typed registry overlay or owned config file |
| `capability_version`, `implementation_reference`, `acceptance_reference` | Versioned evidence; schema acceptance and hardware support tracked separately |

Example reason codes: `PROFILE_FIELD_MISSING`, `WRAPPER_MAPPING_MISSING`,
`BACKEND_NOT_IMPLEMENTED`, `KIT_DEPENDENCY_MISSING`, `ARTIFACT_CAPABILITY_UNKNOWN`,
`BACKEND_MISMATCH`, `DISPLAY_MODE_UNAVAILABLE`, `REQUIRES_OFFLINE_PREPARATION`,
`RECIPE_REQUIREMENT`, `TITLE_ACTIVE`, `TITLE_ACTIVITY_UNKNOWN`.

For example, DXVK refresh 120 should explain “Current refresh field controls
OpenGL only; DXVK output-mode negotiation is missing,” while frame cap should
explain “DXVK supports a cap; prospero-win has no configuration-file mapping yet.”
Those are different implementation tasks and should not share a generic
“unsupported” message.

Suggested operations:

1. `DescribeCapabilities(kit, recipe, display)` returns settings, limits and reasons.
2. `ValidateSettings(defaults, overrides)` returns effective config and field errors.
3. `PreviewSettingsChange(...)` returns profile/config/registry diff and required actions.
4. `ApplySettings(install_id, expected_revision, change)` performs atomic publication
   or queues stopped-title preparation; increment revision only on successful commit.
5. `ReadEffectiveSettings(install_id)` powers both UI and diagnostic export.
6. Separate maintenance APIs implement repair, cache clear, backup, reset and rollback.

Version new profile sections deliberately. Existing strict parsers must continue
to reject unknown keys; use descriptor/kit schema capabilities to select compatible
serialization. Registry mutations need backups and a small tested editor/overlay
format. Ordinary PS5 play must not launch `winecfg`, `wineboot` or redistributable
installers to apply settings.

## 8. UI layout and implementation order

**Game settings:** compatibility preset/reset, launch entry, extra arguments,
validated resolution, controller preset/button editor, pointer options and FPS.

**Advanced compatibility:** curated runtime/backend, DLL overrides, Windows
version, locale, DPI, supported frame cap/VSync, backend-specific rendering/input
options and reviewed diagnostics. Show unavailable options with a short reason
when useful for discovery; avoid interactive controls that silently do nothing.

**Bottle and saves:** version/architecture/path information, installed dependencies,
Cloud behavior, backup/export, repair, cache clear and explicit bottle reset.

**Developer diagnostics:** effective command, setting provenance, kit/module/helper
versions, source-specific switches, log export and precise missing capabilities.

Implementation priorities:

1. Round-trip every existing profile field through a structured model and upstream
   parser. Capability-gate scaling, refresh, threading and HUD; preserve HL2 recipe.
2. Add typed environment/config-file forwarding and pinned DXVK settings for cap,
   VSync, latency, cache paths and expanded HUD; test actual PS5 effects.
3. Add offline registry-overlay operations for Windows version, DPI, locale and
   DLL preferences, with backup/view/rollback tests.
4. Add input transforms and audio gain/mute, then backend-dependent device/input
   features with dedicated PS5 fixtures.
5. Implement larger driver work separately: DXVK high-refresh/VRR, HDR, upscaling,
   multichannel/capture, Raw Input, multi-controller and child processes.

This settings backlog supplements the existing integration milestones; it does
not make every feature a prerequisite for the first HL2 per-game title. Update
support status only when both the mapping and its required acceptance evidence
exist for the recorded kit.

### Selected managed filesystem contract

The shared `ManagedDirectory` backend now selects checked absolute paths on PS5
and descriptor-relative operations on hosts. Private bottle directory creation
and artifact hashing use it; settings persistence/publication/cache deletion
still need migration. See [the contract and acceptance gates](native-managed-filesystem.md).
This is an explicit stable-namespace policy, with static symlink and replacement
checks; it does not claim protection against a hostile concurrent same-UID/root
writer. Do not enable unsupported settings or readiness from host tests alone.
