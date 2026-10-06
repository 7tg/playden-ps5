# Development compatibility kit hl2-fw1360-dev1 — 2026-10-06

Scope: host packaging and C++ admission only. Nothing was installed on the PS5.
This kit is **not releasable**.

## Result

| Item | Value |
| --- | --- |
| Kit | `hl2-fw1360-dev1`, 3548 files, local path `build/kits/hl2-fw1360-dev1` (not in Git) |
| Manifest SHA-256 | `368fb24b6dc68bf503a0d410bf2ae4a2ead55091f9d257a3e72498e26fe8ceb6` |
| Files digest | `bce3081187599a18cefe9fd65a2ecb443bc5aaac5fa39928d9c7fc33f7b2e6b9` |
| prospero-win source | `672d905209d38c444f3c96f001a19ef4ccc2ea62` (local, unpublished), clean |
| FAKE78001 wrapper eboot (SDK v0.42, foundation `9c0b994`) | `60225c6d98443ab4e9fd4f96fbeca68e9485282611133f151febe3134a701053`, `source_dirty: false` |
| Helper / protocol | `5608915…` / `bb02c4aa…` (independent pins) |
| Template | approved console template `candidate-v16`, preparation `hl2-fresh-v16` (lock-pinned evidence verified by `tools/package_prospero_kit.py`) |
| Capabilities | `dxvk`, `offline_bottle`, `per_game_title`, `wow64_i386`; the publication-guard capability is not claimed or trusted |

`runtime_kit_trust` (host) checks that `src/runtime/kit_trust.cpp` matches the
`prospero_kit` lock entry and that `VerifyInstalledKit` admits this kit with
production SHA-256. It also refuses the kit for empty or `12.02` firmware.

The runtime bundle id is `wine-wow64`, the value the HL2 profile's `runtime`
key carries; OWNER validation requires the two to match.

## Runtime provenance

The retained runtime was bound to an independent source: the gameplay-proven
runtime installed at `/data/homebrew/PPSA99995/win` on the console. All 1703
files were read over FTP and hashed on 2026-10-06
([inventory](console-ppsa99995-runtime-inventory.json)).

The local copy in `build/per-game-acceptance/candidate-v16/FAKE78001/win`
differed in two files: `lib/wine/x86_64-unix/ntdll.prx` and `win32u.prx`.
Their provenance is unknown. The kit input uses the console bytes for both
files; the assembled runtime matches the console inventory exactly.

## Release blockers

1044 compiled modules (PE, ELF and PS5 SELF) in the runtime and the bottle
template embed host build paths such as `/home/leolobato/prospero-install/...`
from compiler `__FILE__`/assert strings. Packaging used the development-only
`--development-build-paths` flag, which exempts compiled modules only. Text,
registry and config files are still scanned and refused. `--release` refuses
the flag. The list is in [development-exceptions.txt](development-exceptions.txt).
A releasable kit needs a runtime rebuilt with path remapping and requalified.

## Packager corrections found by the real inputs (upstream)

- `107779d`, `fa064cc`, `7d1cccd`: development build-path exemption, limited to
  compiled modules; PS5 SELF modules recognized.
- `3219c89`: manifest entries now use byte order of the full path. Component
  ordering produced manifests that the validator and C++ consumer refuse.
- `467ba57`: console-form templates keep their `.pw-symlinks` drive table.
- `672d905`: removes `<stdio.h>`/`snprintf` from the portable title-launch core
  (a `439ae06` regression found by the x86 Linux run below) and makes the
  native publication fixture portable to Linux.

## Upstream verification on x86_64 Linux

A delegated Codex worker ran the full upstream suite on the Linux build VM
(`~/build/codex-upstream-verify`, report `REPORT.txt`). At `439ae06`,
`7d1cccd` and `467ba57`: 97/101 make-test targets pass when run individually.
`test_native_contract` regressed at `439ae06` (fixed in `672d905`). The
other three failures exist at the parent `79cf85c`: the `/private/tmp`
native fixture (fixed in `672d905`), `test_title_identity` expectations and
`test_test_reachability`. `test_playden_kit` passes 39/0. GCC 15 and Clang 21
compile gates fail on pre-existing test code; LLVM 18 builds everything.

Follow-up at `672d905` (LLVM 18, same VM): `test_pw_title_publication_native`
now runs on Linux and passes all five contract groups, including the explicit
game-root admission cases. `test_native_contract` passes 6/6. Individual sweep:
99/101 make-test targets, kit 39/0. The remaining failures
(`test_title_identity`, `test_test_reachability`) predate `439ae06`.
