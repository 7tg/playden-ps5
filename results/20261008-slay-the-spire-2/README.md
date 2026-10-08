# Slay the Spire 2 on the console — 2026-10-08

Playden `main` 1077446 (deployed build), kit C `hl2-fw1360-dev1` (shell 850e62c, runtime bea2be5), FW 13.60. Slay the
Spire 2 (Steam 2868840) was the first game picked from the [library ranking](https://artifacts.cliq.dev/d/EHFkTcnrMq):
Godot 4.5.1 "MegaDot" with C# (.NET 9.0.7 CoreCLR, self-contained), FMOD, Spine, Sentry/crashpad. 2.9 GB, 64-bit.

Result: **plays, slowly outside the map.** From the exFAT USB disk it reaches the main menu, starts a run and shows the
act map with the controller. The map is smooth; the title, main menu, character select and dialogs ("What's Early
Access?", tutorials) run at about 4 fps (seen on the TV and in `PW_GL`), a GL driver cost described under
[Fixed](#fixed-same-day). The first runs below show what it took.

## Steps

1. Downloaded to the new USB disk (`/mnt/usb1`, exFAT) first: Prepare was refused, "The game's folder belongs to another
   user" (exFAT records no owners). Uninstalled through the UI and downloaded again to console storage (15 min).
2. Prepare generated `godot,d3d12,dxgi,opengl,vulkan graphics=opengl architecture=pe64`, arguments
   `--rendering-driver opengl3`; Steamworks emulation changed `data_sts2_windows_x86_64/steam_api64.dll`. The title is
   FAKE78007; it registered after Playden was closed.
3. Three starts from Home (`mcp launch FAKE78007`); session logs copied by Playden are in this folder.

| Run | `sts2.runtimeconfig.json` | Result |
| --- | --- | --- |
| [session-0](session-0-gc-reservation.log) | as shipped | Godot starts, `OpenGL API 4.6 (Core Profile) Mesa 26.2.0 - Compatibility - PS5 AGC`, hostfxr and coreclr load, then `map_view anon mmap error … size 4000010000`, `GC heap initialization failed with error 0x8007000E`, exit |
| [session-1](session-1-region-range-ignored.log) | `System.GC.RegionRange` = 4 GiB | Same 256 GiB reservation: the GC does not read that setting from runtimeconfig |
| [session-2](session-2-flexible-memory.log) | `System.GC.HeapHardLimit` = 2 GiB | CoreCLR starts. "What's Early Access?" screen at 31.7 fps; moving the stick shows "Controller Detected" with Xbox glyphs. About 5 s in, flexible memory is 0 and the game drops to 4 fps; Triangle (Y, "Proceed") gets no visible response; null-page faults, then `Exception frame is not in stack limits`, exit |

## Findings

- **.NET 7+ regions GC reserves 256 GiB** of address space at start; the console refuses it. `System.GC.HeapHardLimit`
  in `<assembly>.runtimeconfig.json` shrinks the reservation (2 GiB works); `System.GC.RegionRange` has no runtimeconfig
  name (only the `DOTNET_GCRegionRange` variable, which the kit can't set yet).
- **The probe misses self-contained .NET.** Its .NET refusal matches an `mscoree` import; a game shipping its own
  `hostfxr.dll`/`coreclr.dll` with `*.runtimeconfig.json` passes. Such games need the heap limit applied at preparation
  (not built). On the console the file is hand-edited (original in
  [sts2.runtimeconfig.original.json](sts2.runtimeconfig.original.json)); a Steam verify or reinstall undoes it.
- **Flexible memory runs out.** A title has about 440 MiB. At 17:28:21 the holders were 535 MiB mapped, led by one
  caller (`anon:0000801788d2`) with 237 MiB in 161 mappings; telemetry shows Wine's heap at 276 MiB. The caller's address
  is at the start of the PRX range where `ntdll.prx` loads, so this is most likely ntdll's malloc heap (used by Mesa and
  the rest of the unix side), not Mesa's own mappings; there are no module load addresses in the log to prove it.
  Direct memory was not short (`dmem` 1.57 GiB, no failures). Fix in prospero-win: back that heap with direct memory as
  patch 0600 does for Wine's anonymous memory. No runtime switch for it exists today.
- Godot's OpenGL compatibility renderer, FMOD audio (`audible` frames counted) and XInput all work in this kit.

## Fixed (same day)

| Gap | Fix | Where |
| --- | --- | --- |
| .NET 7+ GC reserves 256 GiB | Playden writes `System.GC.HeapHardLimit` = 2 GiB into the runtimeconfig of games that ship `coreclr.dll` (prepare and every Play; the game's own limit wins) | `src/runtime/dotnet_heap.cpp`, called from `RuntimeHost::ApplySteamworks`; log `dotnet heap app=… changed=…` |
| Wine's Unix-side heap in flexible memory | Heap spans and large blocks come from main direct memory once direct memory passes its self-check (`WINE_PS5_HEAP_FLEXIBLE` turns it off) | prospero-win 67fba29 (`wine/ps5/pw_wine_heap.c`, `pw_wine_dmem_ps5.c`); kit `hl2-fw1360-dev1` manifest 229ea5f4 with ntdll/win32u/wineserver PRXs rebuilt from bea2be5 + that change |
| exFAT USB game folder | See [results/20261008-exfat-usb-games](../20261008-exfat-usb-games/README.md) | — |

Run on the fixed kit ([session-heap-direct-memory](session-heap-direct-memory.log), 18:52-18:54): `flexible_free` stays at
265 MiB (was 0), heap 336 MiB, no faults. `PW_GL` reports 4 fps on the title, menu and character-select screens (the
log keeps its last 128 KB; the 22-56 fps at its end is the quit sequence). The map ran smoothly on the TV. During the
slow frames the PS5 Mesa driver's CPU cache flushing grows by about 4.3 GB per 5 s (`ps5-cpu-flush-summary`), roughly
200 MB per frame: these screens are full-screen animated layers and screen-space effects whose render targets the
driver resolves through full CPU cache flushes. That is a PS5 Mesa (gallium `ps5_screen.c`) performance item.

Also seen: changing the kit while a game waits for registration strands it ("Recovery has no matching stopped reserved
OWNER intent/provenance"); uninstall needed the ELF loader reloaded and a native uninstall of the stale title (see
`ps5-title-identity-and-registration` notes).

## Open

- PS5 Mesa: frames on the menus and dialogs flush ~200 MB of CPU cache each and run at 4 fps; the map is smooth.
- Playden: a kit change must not strand a game waiting for registration.
