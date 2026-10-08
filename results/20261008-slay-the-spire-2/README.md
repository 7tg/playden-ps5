# Slay the Spire 2 on the console — 2026-10-08

Playden `main` 1077446 (deployed build), kit C `hl2-fw1360-dev1` (shell 850e62c, runtime bea2be5), FW 13.60. Slay the
Spire 2 (Steam 2868840) was the first game picked from the [library ranking](https://artifacts.cliq.dev/d/EHFkTcnrMq):
Godot 4.5.1 "MegaDot" with C# (.NET 9.0.7 CoreCLR, self-contained), FMOD, Spine, Sentry/crashpad. 2.9 GB, 64-bit.

Result: **reaches its first screen with the controller detected, then runs out of flexible memory.** Two runtime gaps
stop it; neither is in Playden.

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

## Open

- prospero-win: direct-memory heap for the unix side (needs a runtime rebuild), then retry past the first screen.
- Playden: apply `System.GC.HeapHardLimit` to self-contained CoreCLR games during preparation, and add the case to the
  probe.
