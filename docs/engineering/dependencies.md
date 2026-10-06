# Dependencies and patched projects

Updated 2026-10-06. Everything the build needs is in this repository: patched projects are submodules under
`third_party/`, unmodified prebuilt toolchain pieces are downloaded into `deps/` by `scripts/fetch-deps.sh`, and
`dependencies.lock.json` pins every revision and hash. Setup steps are in [CONTRIBUTING.md](../../CONTRIBUTING.md).

## Submodules

Patched projects are forks under `github.com/leolobato`. Our changes live on each fork's `feat/playden-ps5` branch,
which the submodule tracks. The forks are not published yet, so a fresh clone cannot fetch them until they are.

| Submodule | Purpose | Fork / upstream | Our commits on `feat/playden-ps5` |
| --- | --- | --- | --- |
| `prospero-win` | Wine for PS5: per-game wrapper sources the launcher compiles, kit tooling, Wine patch series | `leolobato/prospero-win` / `mpereiraesaa/prospero-win` | Firmware 13.60 native startup (`824b4ec`), per-game wrapper mode through `301246d` (audio port fix), display-handoff input (`a4c3856`), OpenGL drawable patch `0663` (`4234987`), sync-header patch `0895` |
| `ps5-homebrew-ui` | Launcher UI toolkit | `leolobato/ps5-homebrew-ui` / `blackbearreloaded/ps5-homebrew-ui` | Latin glyph baking (3 commits), macOS OpenGL 4.1 host, CFF SDF font atlases |
| `ps5-opengl` | Native-title link inputs (`native-app/`); legacy varying fix used by the Wine runtime | `leolobato/ps5-opengl` / `blackbearreloaded/ps5-opengl` | Legacy OpenGL varying linkage (`e9c1104`) |
| `prospero-foundation` | PRX conversion and linking for the Wine runtime (`ps5-native-tool`, `ps5-pie.ld`) | `leolobato/prospero-foundation` / `mpereiraesaa/ps5-native-app-boilerplate` | Module hash tests aligned with the measured loader contract (`182f1ea`) |
| `PS5-Lapy-JB-Daemon` | Title-scoped root helper that gives a title `/data` access | `leolobato/PS5-Lapy-JB-Daemon` / `mpereiraesaa/PS5-Lapy-JB-Daemon` | Exact FAKE title IDs (`e9f2cfc`), location-independent helper builds (`5898dde`). Our firmware 13.60 fix (PR #49) is merged upstream |
| `ps5-native-app-boilerplate` | Native title tooling and `libc.prx` shim | upstream `blackbearreloaded/ps5-native-app-boilerplate` | None |

`prospero-foundation` is a fork of `ps5-native-app-boilerplate` under another name, because GitHub allows one fork per
repository network per account.

The `prospero-win` submodule is pinned to `301246d`, the wrapper the current kit was built from. The branch tip adds
the two runtime fixes the console runtime was built with; move the pin and the kit together at the next runtime build.

## Downloads (`scripts/fetch-deps.sh`)

| Component | Version | Source | Used for |
| --- | --- | --- | --- |
| PS5 payload SDK | v0.43 | `ps5-payload-dev/sdk` release | Playden PS5 build |
| PS5 payload SDK | v0.42 | `ps5-payload-dev/sdk` release | Lapy helper build (`deps/ps5-payload-sdk-helper`) |
| ps5-opengl SDK | 1.0.0 | `blackbearreloaded/ps5-opengl` release | `libPS5OpenGL.a` and headers |
| compiler-rt builtins | LLVM 18.1.8 | Ubuntu `libclang-rt-18-dev` package | `__emutls_get_address` and other builtins; Homebrew LLVM ships only Darwin builtins |

These are used unmodified. If one ever needs a patch, it becomes a fork submodule with a build step, like the
projects above.

## Vendored in the repository

`third_party/stb` (`stb_image.h`), `third_party/sqlite3` (from `notmaj0r/CheatRunner`), `third_party/zstd`,
`third_party/lzma-sdk`, the QR code generator, and curl 8.22 / Mbed TLS built from pinned source archives by
`probes/steam-network/prepare-*-deps.sh`. All are pinned by hash in `dependencies.lock.json`.

## Not in the build

- **Wine runtime and compatibility kit.** Built separately (Linux) from prospero-win's pinned Wine `490f6d5`, its
  `wine/patches/` series and the forks above; the recipe is in the release-kit evidence. The launcher admits a kit by
  its pinned manifest and file hashes.
- **DXVK 2.6.2** is installed into each game bottle by the kit template.
- **prospero-win-profiles** (`mpereiraesaa/prospero-win-profiles`): the bundled `runtime/input/{gamepad,mouse}.input`
  presets come from `3d3be53`, with its MIT notice in `runtime/input/LICENSE`.
