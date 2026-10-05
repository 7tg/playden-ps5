# Dependencies and patched projects

Date: 2026-10-05. The repositories needed to run Windows games through Playden on
firmware 13.60, the patches we carry in each, and where each checkout is on the
development Mac.

Paths are relative to the main checkout of this repository,
`~/Documents/Projetos/Personal/GameNative/playden-ps5-launcher`. Worktrees under
`~/.chauffeur/worktrees/` are temporary branch checkouts of the same repositories.

## Repositories

| Repo | Purpose | Local path | Remote | Our patches |
| --- | --- | --- | --- | --- |
| **playden-ps5-launcher** | Downloads, bottles, game-title installation, game settings | `.` | this repo | Wine integration still pending ([integration plan](../superpowers/plans/2026-10-05-prospero-win-integration.md)) |
| **prospero-win** | Runs Windows games (Wine for PS5); our Wine patches live here | `../ps5-homebrew/prospero-win` | `mpereiraesaa/prospero-win` | `824b4ec` firmware 13.60 kernel provider selection; `a4c3856` input updates during display handoff; `4234987` attach OpenGL drawables before resize flush. All on `feat/fw-v13.60-support`; `824b4ec` also on `feat/playden-per-game-title` |
| **PS5-Lapy-JB-Daemon** | Grants the title access to `/data` | `../ps5-homebrew/PS5-Lapy-JB-Daemon` | `mpereiraesaa/PS5-Lapy-JB-Daemon` (fork of `ArkSama/PS5-Lapy-JB-Daemon`) | `611fa5f` preserve SDK credential attribute arrays for the 13.60 helper; [PR #49](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon/pull/49) open |
| **ps5-opengl** | Launcher graphics and OpenGL games | `../ps5-homebrew/ps5-opengl` | `blackbearreloaded/ps5-opengl` | `e9c1104` link legacy OpenGL texture and color varyings, on `fix/legacy-varying-linkage` |
| **ps5-native-app-boilerplate** | Native title runtime and packaging | `third_party/ps5-native-app-boilerplate` (submodule); standalone at `../ps5-homebrew/ps5-native-app-boilerplate` | `blackbearreloaded/ps5-native-app-boilerplate` | Existing foundation fixes; nothing new from the 13.60 investigation |
| **ps5-prx-foundation** | Wine PRX conversion and linking (`ps5-native-tool`, `ps5-pie.ld`) | **Not checked out on this Mac.** prospero-win's `tools/build_wine_ps5.sh` takes it as `--prx-foundation DIR` or `$PS5_PRX_FOUNDATION`, a foundation checkout with module-export publishing (branch `exp/prx-module`) | — | Existing module-export support; no new patch |
| **ps5-payload-dev/sdk** | Compiler and platform headers | `../ps5-homebrew/ps5-payload-dev-sdk` (source). Builds use the released SDK through the environment wrapper in `../playden-ps5`; see [build.md](build.md) | `ps5-payload-dev/sdk` | None; released SDKs only |
| **ps5-homebrew-ui** | Launcher UI toolkit | `third_party/ps5-homebrew-ui` (submodule); standalone at `../ps5-homebrew/ps5-homebrew-ui` | `blackbearreloaded/ps5-homebrew-ui` | None |
| **prospero-win-profiles** | Hand-written prospero profiles, input presets, recipes | Not checked out | `mpereiraesaa/prospero-win-profiles` | None; see [game settings roadmap](game-settings-roadmap.md) P5 |

Our fixes from the 13.60 investigation are in three repositories: **prospero-win**,
**PS5-Lapy-JB-Daemon** and **ps5-opengl**. Playden still has to consume those
builds and implement the bottle and title integration.

## What this repository pins

`dependencies.lock.json` pins what the launcher builds against. The fixes above are
not consumed yet:

| Dependency | Pinned | Includes our 13.60 fix? |
| --- | --- | --- |
| PS5 payload SDK | release `v0.43` | Not applicable |
| ps5-opengl | SDK `1.0.0`, source `dce7491` | No. `e9c1104` is on an unreleased branch |
| ps5-native-app-boilerplate | `f98de73` | Not applicable |
| ps5-homebrew-ui | `8a716f2` | Not applicable |
| prospero-win | Not pinned yet. Integration plan Task 3 adds the submodule and kit | No |

## Branch checkouts in use

| Repo | Path | Branch | Head |
| --- | --- | --- | --- |
| prospero-win | `../ps5-homebrew/prospero-win` | `zcode/prospero-win-13.60` | `defd38b` |
| prospero-win | `~/.chauffeur/worktrees/7449CB0F-…/feat-fw-v13-60-support` | `feat/fw-v13.60-support` | `a1f349e` |
| prospero-win | `.worktrees/prospero-win-per-game` | `feat/playden-per-game-title` (development; final pin pending) | `c1b81d3` |
| prospero-win | `~/.chauffeur/worktrees/prospero-win-environment` | `feat/profile-environment` (U1, local) | `06e6399` |
| prospero-win | `~/.chauffeur/worktrees/prospero-win-input-tuning` | `feat/profile-input-audio` (U4/R1, local; stacked on `feat/profile-input-tuning` `410ac1c`) | `eeac05b` |
| prospero-win-profiles | `~/.chauffeur/worktrees/prospero-win-profiles-sidecar` | `feat/checker-runtime-features` (R2, local; stacked on `feat/recipe-sidecar` `9325e0c`) | `8fa9213` |
| ps5-opengl | `~/.chauffeur/worktrees/ps5-opengl-legacy-varyings` | `fix/legacy-varying-linkage` | `e9c1104` |
| PS5-Lapy-JB-Daemon | `../ps5-homebrew/PS5-Lapy-JB-Daemon` | `feat/playden-title-helper` | `e9f2cfc` |

These are snapshots. Check `git worktree list` in each repository before you rely on them.

## Runtime and build dependencies

These are not separate patched repositories:

- **Wine.** Patches are in prospero-win `wine/patches/`. `tools/build_wine_ps5.sh`
  applies them to a work copy under `.deps/wine-ps5`. No separate patched Wine repo.
- **DXVK 2.6.2.** Its DLLs are installed into the game bottle. It reads `dxvk.conf`
  from the working directory, which the game settings roadmap uses.
- **Vulkan driver (RADV/Mesa, ps5vk).** Linked from its SDK through
  `--ps5vk-sdk DIR` or `--radv DIR` in `build_wine_ps5.sh`.

The bundled `runtime/input/{gamepad,mouse}.input` presets come from
prospero-win-profiles `3d3be53`. Its MIT copyright and permission notice ships
with them at `runtime/input/LICENSE`.
