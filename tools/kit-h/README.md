# Kit H build scripts (record)

These are the scripts that built runtime kit H, the kit this release uses. They are kept as a record. **not** a turnkey build yet: read this first.

## What they need

- **A Linux x86-64 machine or VM** (the maintainer uses an Ubuntu VM). Wine, Mesa and the PS5 libraries do not build
  on macOS. The kit is packaged and uploaded from the Mac.
- LLVM 18, both MinGW cross compilers (`i686-w64-mingw32`, `x86_64-w64-mingw32`), llvm-mingw for Mesa, and the PS5
  payload SDK.
- Inputs from earlier kits that are **not in this repository**: kit A's template bottle (prepared from a fresh
  Half-Life 2 install), kit F's runtime file list, kit D's Wine source checkout and the prospero-win title foundation.
- The Playden checkout shared between the Mac and the VM at the same path, so the VM scripts can write into its
  `build/` folder.

## Settings

The scripts read these environment variables. The defaults fit a plain checkout.

| Variable | Used by | Default |
| --- | --- | --- |
| `PLAYDEN_KIT_WORK` | VM scripts: the build folder for Wine and its logs | `~/playden-kit` |
| `PROSPERO_WIN_SOURCE` | `setup_vm.sh`: the prospero-win repository to clone | `https://github.com/leolobato/prospero-win.git` |
| `PROSPERO_FREETYPE_TARBALL` | `build_cross.sh`: the FreeType 2.13.3 source tarball | none, required |
| `PLAYDEN_KIT_DIR` | `make_shell.sh`, `make_spec.py`, `stage_kit_h.py`: where kit H is staged | `build/kit-h` |
| `PLAYDEN_KIT_FROM` | `update_r4.py`, `update_r5.py`, `update_r6.py`, `update_r8.py`: the kit whose inputs were copied (its paths in the spec are rewritten) | `build/kit-h-r3`, `build/kit-h-r4`, `build/kit-h-r5`, `build/kit-h-r6` |
| `PLAYDEN_DOTNET_DIR` | `update_r6.py`: the prefixes before and after Wine Mono's support MSI install, and the MSI's tables | `$PLAYDEN_KIT_DIR/dotnet` |
| `PLAYDEN_RUNTIME_REVISION` | `update_r5.py`, `update_r8.py`: the prospero-win commit the runtime and its pin come from | none, required |
| `PLAYDEN_CROSS_PRX` | `update_r8.py`: the cross build's `prx/sce_module` folder | `$PLAYDEN_KIT_WORK/wine-ps5/prx/sce_module` |
| `PLAYDEN_WINE_MONO_ARCHIVE` | `update_r4.py`: the Wine Mono 11.3.0 archive (its SHA-256 is checked) | none, required |
| `PLAYDEN_BUILD` | `stage_kit_h.py`: the build folder that holds kits A and F | `build/` |
| `PLAYDEN_KIT_F` | `make_spec.py`: kit F's build, whose spec kit H starts from | `build/kit-f` |
| `PLAYDEN_MESA_ARTIFACTS` | `stage_kit_h.py`: the output of `tools/build_mesa_zink.sh` | `~/mesa-zink/work/artifacts` |
| `PLAYDEN_MEDIA_WORK` | `build_cross.sh`, `stage_kit_h.py`: pinned media manifests and `root/` static archives; unset builds and stages without the media backend | none |
| `PLAYDEN_MEDIA_REGISTRATION` | `stage_kit_h.py`: matching disposable-prefix registry and DLL hash evidence | none, required with `PLAYDEN_MEDIA_WORK` |
| `PS5_NATIVE_FOUNDATION` | `make_shell.sh`: the prospero-foundation checkout | `third_party/prospero-foundation` |

## What does the work

The scripts are glue. The real build steps are prospero-win's tools, in this order:

| Step | prospero-win tool | Script here |
| --- | --- | --- |
| Check out prospero-win on the VM | — | `setup_vm.sh` |
| Wine for the host (PE modules, winebuild) with the patch series | `tools/build_host_wine.sh` | `build_host.sh` |
| Wine's Unix side for the PS5, linked to RADV | `tools/build_wine_ps5.sh`, `tools/link_radv_prx.sh` | `build_cross.sh` |
| schannel through GnuTLS, with a CA bundle | `tools/build_tls_ps5.sh` | `build_cross.sh` |
| 32-bit support: the WoW64 translator and the native CPU path | `tools/build_wowprospero.sh`, `tools/build_wow64native.sh` | `build_cross.sh` |
| Mesa WGL/Zink for OpenGL games | `tools/build_mesa_zink.sh` | (run on its own) |
| The per-game title shell and root helper (on the Mac) | `tools/build_native.sh`, Lapy `tools/build_owned_daemon.py` | `make_shell.sh` |
| Stage the runtime, the template bottle and Mesa | — | `stage_kit_h.py` |
| Write the kit spec | — | `make_spec.py` |
| Package and validate the kit | `tools/package_playden_kit.py` | — |

Kit H is prospero-win rebased onto upstream 644e32e (runtime `bceed2d-kit-h-r1`). Kit H r2 (runtime
`2c4eaa6-kit-h-r2`) adds Wine patch 0170: `build_cross.sh` again, then `update_r2.py` on a copy of kit H's inputs and
spec with the runtime revisions set to 2c4eaa6; only `ntdll.prx` changes. Kit H r3 (runtime `2b91e15-kit-h-r3`)
adds Wine patch 0957 the same way: `build_cross.sh`, then `update_r3.py` on a copy of r2's inputs and
spec with the revisions set to 2b91e15; only `kernelbase.dll` changes, in the runtime and in the template bottle.
Kit H r4 (same runtime as r3) adds Wine Mono 11.3.0 for .NET programs. It needs no VM:
`update_r4.py` on a copy of r3's inputs and spec, with `PLAYDEN_WINE_MONO_ARCHIVE` set to the official
`wine-mono-11.3.0-x86.tar.xz`. Mono goes to the kit's `mono/wine-mono-11.3.0`, once per console rather than once per game,
and the template bottle's `user.reg` points Wine at it (`HKCU\Software\Wine\Mono` `RuntimePath`). A bottle made from an
earlier template does not have that value: use Recreate bottle. Kit H r5 (runtime `70bfc92-kit-h-r5`) reads a profile's `[environment]` section and has Wine patch 0958, so Playden's Environment variables and
CPU cores settings reach the game (capability `settings-profile-environment`). Copy r4's inputs (without `titles/`) and
spec, run `make_shell.sh` with `PLAYDEN_KIT_DIR` on the prospero-win commit that pins the runtime (the new title shell),
`build_cross.sh` on 70bfc92, then `update_r5.py` in the VM. Only the shell's `eboot.bin` and `ntdll.prx` change; the
template is r4's, so bottles need no Recreate. Kit H r6 (r5's runtime and shell) gives the
template bottle what Wine Mono's support MSI writes when Wine installs it, which kit H bottles never run: the .NET Framework
`InstallRoot` (both registry views) and the other keys of the MSI's Registry table, and `C:\windows\Microsoft.NET`. Without
them mscoree's `LoadLibraryShim` fails (Antichamber). Install the MSI once with kit H's host Wine on an x86_64 Linux host
(the commands are in `update_r6.py`; Rosetta can't run wow64 Wine), then run `update_r6.py` on the Mac on a copy of r5's
inputs and spec. It takes only what the MSI accounts for and checks each file against the MSI's hashes or the runtime's
builtins. Bottles made from an earlier template need Recreate bottle. Kit H r8 (runtime
`13fa077-kit-h-r8`; r7 was a development kit and was not released) is r6 with Wine patch 0603: Windows memory queries
report the title's direct-memory budget instead of about zero, so Fallout: New Vegas starts. Build the cross PRXs with
`build_wine_ps5.sh` on 13fa077 into the same `wine-ps5` work folder as before (assert messages keep the source path, so
another folder name changes every PRX), then run `update_r8.py` on a copy of r6's inputs and spec with
`PLAYDEN_CROSS_PRX` at that build's `prx/sce_module`. Only `ntdll.prx` changes, so bottles need no Recreate. Kit H r9
(the kit this release uses; runtime `d069101-kit-h-r9`) is r8 with Wine patches 0959-0963: vkd3d-shader builds the D3DX
effects that DoDonPachi Resurrection and Star Wars: Empire at War compile at runtime. Build with `build_wine_ps5.sh` on
d069101 into the same `wine-ps5` folder (every cross PRX must still be r8's), strip the build's `pe/*/wined3d.dll` with
`*-mingw32-strip -s`, then run `update_r9.py` on a copy of r8's inputs and spec with `PLAYDEN_CROSS_PE` at those PE files.
Only `wined3d.dll` changes, in the runtime and the template, and Wine loads the runtime's builtin, so bottles need no
Recreate. RADV (`libvulkan.prx`) comes
from [PS5_Vulkan](https://github.com/mpereiraesaa/PS5_Vulkan) 8aee3af / [PS5_Mesa](https://github.com/mpereiraesaa/PS5_Mesa)
01d7057, the same as kit G r2. OpenGL games under Zink need at least PS5_Mesa f2ee389.

## Media backend

Wine's `winegstreamer` can be built with a static GStreamer/FFmpeg backend. Media Foundation, DirectShow and the WMA
decoder DMO (`wmadmod`, which XAudio2 uses for xWMA) need it. Build the pinned
LGPL dependencies with prospero-win's `tools/build_media_ps5.py`, then set
`PLAYDEN_MEDIA_WORK` to its work directory. `build_cross.sh` passes its `root/` to
`tools/build_wine_ps5.sh --media-root`. Both PE architectures and all static support
libraries need the builder's file-prefix mapping; stripping debug sections alone
leaves `__FILE__` paths in diagnostics.

Register the matching i386 and x86_64 `winegstreamer.dll` in a disposable native x86
Wine prefix using each architecture's `regsvr32.exe`. Preserve `system-before.reg`,
`system-after.reg` and `dll-sha256.json` (keys `i386-windows` and `x86_64-windows`),
and set `PLAYDEN_MEDIA_REGISTRATION` to that directory. With both set, `stage_kit_h.py FULL_COMMIT`
includes the backend; without them it leaves `winegstreamer` out. Staging validates the PRX's
native import audit, both DLL architectures and registration hashes, both media
registry views, dependency/archive hashes and license hashes before copying inputs.
Host devices and other unrelated registry changes are excluded.

The runtime includes `share/wine/licenses/media/` with each component's license text
and `source-manifest.json` (pinned source releases, checksums and linked archives),
plus `share/wine/THIRD_PARTY` and neutral build provenance in `share/wine/media-build/`.
FFmpeg configurations with `--enable-gpl` or `--enable-nonfree` are rejected. Use only
LGPL GStreamer plugin sets. Package with the matching runtime revision's
`tools/package_playden_kit.py` in strict mode: omit `--development-build-paths`.
A release requires zero build-path exceptions. Decoder probes alone do not qualify
the kit for playback capability.

Kit H r10 is r9 with the media backend (Wine patches 0480-0482 and 0964). Build `build_wine_ps5.sh` on the
prospero-win commit with `--media-root` (or `build_cross.sh` with `PLAYDEN_MEDIA_WORK` set). Keep r9's build `report.json`:
the media build maps build paths out of every module, so every cross PRX and PE module differs from r9's and is replaced.
Strip the build's PE modules except `winegstreamer.dll` with `*-mingw32-strip -s` (the registration records that DLL's
hash), register `winegstreamer.dll` as above, then run `update_r10.py --kit NEW_KIT --from-kit R9_KIT --cross WINE_WORK
--registration REGISTRATION_DIR --media-work MEDIA_WORK --base-report R9_REPORT --revision FULL_COMMIT` on a copy of r9's
inputs and spec, where the absolute archive paths in `media-flags.json` resolve. Staging stops before it writes when the
build's Wine commit or patch list is not r9's plus those four patches, or when the build makes a module r9 lacks (besides
`winegstreamer`; `rtworkq.dll` is now cross-built). `--name r10-dev-YYYYMMDD.N` stages a development kit with the same checks.

Install a matching Playden app with the kit. On the next Play, Playden merges the
kit's media COM/parser values into the stopped bottle's `system.reg`, in both the
64-bit and WoW64 registry views. It backs up and atomically replaces the hive,
preserves unrelated values, and leaves the file untouched when the values already
match. It also copies the kit's `winegstreamer.dll` into the bottle's `system32` and
`syswow64` when missing: once a prefix exists, Wine loads a builtin DLL only through its
file there, and kit bottles never run wineboot. Existing bottles get the backend
without Recreate bottle (Fallout 4's r9 bottle plays its xWMA sound after one Play).
Older kits without the media metadata keep working.

For a game that cannot handle the backend, add `PW_MEDIA_DISABLE=1` to Playden's
Environment variables setting. Playden writes it as a `[debug] env` line, which every
kit H title shell accepts. The runtime then rejects media creation, so games that
check errors can skip movies; a game that ignores errors can still fail (Metal Wolf
Chaos XD crashes with it). Remove the switch when testing actual video and audio.

## Not done yet

A build from a clean machine, with every input fetched or built by a script, is not written yet.
