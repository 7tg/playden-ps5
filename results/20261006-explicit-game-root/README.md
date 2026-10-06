# Explicit game root in title descriptors — 2026-10-06

Scope: host tests and actual SDK builds only. Nothing was deployed to the PS5.
Existing-download and USB adoption still need native qualification.

## Source

- prospero-win `439ae06f46462959fac23fedb0424403ebf78c62` (local commit, not
  pushed, `published: false`): schema-2 `game_root` in `game-launch.json`.
  Roots under `/data/playden` are refused.
- Submodule `third_party/prospero-win` checked out at that exact revision, clean.

## Host verification

- Upstream focused binaries `test_pw_title_launch`, `test_pw_title_publication`,
  `test_pw_title_publication_cpp`, `test_pw_title_publication_native`: pass
  (macOS ARM). The full upstream `make test` was not run; it does not build on
  macOS ARM (see the handoff).
- Parent `homebrew-host`: 108/108 pass (`build/game-root-schema2-host-test.log`).
  This includes the real filesystem publisher regression for
  `/data/homebrew/FAKE77031/games/220` and `/mnt/usb0/playden/games/220`. That
  regression failed at native stage 6 before this change. The test keeps
  `probe.exe`, `steam_api.dll` and `player-save.dat` byte for byte.
- New legacy coverage: schema-1 descriptors freeze and publish only for the
  historical `/data/homebrew/<title>/games/<app>` root.

## SDK builds

| Artifact | SHA-256 |
| --- | --- |
| Playden SDK v0.43 developer package `FAKE77031/eboot.bin` (`build/homebrew-ps5-publication-native`) | `784826e4ded1f7da3c7f7a0af74123c58ffe84b5915eca6390c7c7018f5bcde1` |
| SDK v0.42 FAKE78001 wrapper `eboot.bin` (foundation `9c0b994`) | `8fa7308199997fac9fb000a3950ed8127758a9276c072a43f8ba97e005ba347c` |
| Wrapper `eboot.elf` | `6de0b8f7c9ad67bde3680a46c17230f6088ec5edf5034b188fddd8f3f7284f70` |
| Wrapper Lapy helper (reviewed FAKE78001) | `5608915201204dfcd1846b01dd2a9e60d03aa390c69b1dc61f3b150566bf1935` |

The wrapper manifest ([native-title-build.json](native-title-build.json)) records
`source_dirty: false` and source `439ae06`. Its dynamic imports are in
[native-dynamic-imports.txt](native-dynamic-imports.txt). The wrapper is not
native-qualified. Production publication-guard trust stays disabled.
