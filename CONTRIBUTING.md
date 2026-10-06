# Contributing to Playden for PS5

Playden is a controller-first launcher for Windows games on a jailbroken PS5. It signs in to Steam, downloads
games, prepares a private Wine bottle for each one with [prospero-win](https://github.com/mpereiraesaa/prospero-win),
and registers each game as its own PS5 title.

This guide covers setup, builds, tests, running on a console, and how to change a dependency.

## Requirements

- macOS on Apple Silicon (the only host the build supports today).
- Xcode command line tools.
- Homebrew packages: `brew install cmake ninja pkgconf sdl2-compat llvm@18 python`.
  LLVM 18 builds the PS5 code. Apple clang builds the macOS host.
- About 2 GB of free space for `deps/` and `build/`.
- A checkout path without symbolic links. Playden refuses symlinked paths when it reads its data, so two tests fail
  in a link such as macOS `/tmp` (use `/private/tmp` or a folder in your home directory).
- For console work: a PS5 on firmware 13.60 with a jailbreak (see [Running on a console](#running-on-a-console)).

## Get the source

```sh
git clone --recursive https://github.com/leolobato/playden-ps5.git
cd playden-ps5
scripts/fetch-deps.sh
```

`--recursive` checks out the submodules in `third_party/`. Most of them are forks that carry our patches on a
`feat/playden-ps5` branch (see [Dependencies](#dependencies)).

`scripts/fetch-deps.sh` downloads the prebuilt PS5 toolchain pieces into `deps/`: two PS5 payload SDK releases, the
ps5-opengl SDK and the LLVM compiler-rt builtins. It checks every download against the SHA-256 in
`dependencies.lock.json`. You can run it again at any time; it skips what is already correct.

## Build and test on macOS

```sh
scripts/build-homebrew.sh
cmake --build --preset homebrew-host
ctest --preset homebrew-host --output-on-failure
build/homebrew-host/playden-homebrew
```

The first run also builds curl and Mbed TLS from pinned source archives. The host app signs in to Steam like the
console app does and stores its data under `build/homebrew-host/playden-data`.

You do not need a console or a Steam account for the tests. To see the screens with synthetic data, use the
simulation mode:

```sh
build/homebrew-host/playden-homebrew --simulation --scenario signed-out
```

## Build the PS5 package

```sh
scripts/build-lapy-helper.sh
scripts/build-homebrew-ps5.sh
```

The first script builds the root helper that gives the title access to `/data`. The second builds the launcher
title `FAKE77031` into `build/homebrew-ps5/dist/FAKE77031`. Neither script contacts a console.

The build checks the pinned revisions and hashes. If it stops with a mismatch, do not edit the hash: find out why
the input changed. The PS5 build is reproducible, so the same sources give the same `eboot.bin` in any checkout.

`FAKE77031` is allocated to Playden. Do not use it for another application.

## Running on a console

You need:

- A PS5 on firmware 13.60, jailbroken (for example with the WebKit autoloader).
- These payloads loaded after each boot:
  - **kstuff** (required: without it every title fails with CE-100008-9).
  - **ShadowMountPlus** (registers folder titles under `/data/homebrew`).
  - **zftpd** (FTP on port 2120; the console tools use it).
  - **ps5-web-file-manager** (port 8888; useful for recovery).
- The Mac and the PS5 on the same network. The tools use `10.0.1.68` by default; pass `--host` to change it.

If zftpd does not start after a reboot, delete the stale `/data/zftpd/instance.pid` and start it again.

### Install or update the launcher

The first time, copy `build/homebrew-ps5/dist/FAKE77031` to `/data/homebrew/FAKE77031` on the console.
ShadowMountPlus registers it in about 10 seconds. After that, update only the changed files:

```sh
python3 tools/console_title_deploy.py deploy build/homebrew-ps5/dist/FAKE77031 --backup build/console-backups/<name>
```

The tool backs up each console file it replaces. `restore` puts a backup back. It never touches game downloads,
logs or user data.

### The compatibility kit

Games run with a compatibility kit: the Wine runtime, a prepared bottle template and the per-game title files. The
launcher accepts only the kit whose manifest is pinned in `dependencies.lock.json` and `src/runtime/kit_trust.cpp`.

The kit is not published yet. When it is, it will be a release download. To install a kit on the console:

```sh
python3 tools/console_kit_upload.py <kit folder> --replace
```

The upload verifies every file by read-back before it switches to the new kit.

### Logs

Playden writes its logs to `/data/homebrew/FAKE77031/logs/` (`runtime.log`, `launch.log`, `cloud.log` and others).
Game bottles are private to their titles. When Playden starts, it copies the newest game session report to
`logs/title-<TITLE>.log`.

## Dependencies

| Location | What | How it is pinned |
| --- | --- | --- |
| `third_party/<fork>` | Patched projects: `prospero-win`, `ps5-homebrew-ui`, `ps5-opengl`, `prospero-foundation`, `PS5-Lapy-JB-Daemon` | Submodule commit on the fork's `feat/playden-ps5` branch |
| `third_party/ps5-native-app-boilerplate` | Native title tooling | Submodule commit (upstream) |
| `third_party/{stb,sqlite3,zstd,lzma-sdk,…}` | Small vendored sources | File hashes in `dependencies.lock.json` |
| `deps/` | Prebuilt SDKs and builtins | Download hashes in `dependencies.lock.json` |

[docs/engineering/dependencies.md](docs/engineering/dependencies.md) lists our commits in each fork.

### Change a forked dependency

1. Work in the submodule on its `feat/playden-ps5` branch:

   ```sh
   cd third_party/prospero-win
   git switch feat/playden-ps5
   # edit, test, commit
   ```

2. Update the pin in Playden and anything that records the revision (`dependencies.lock.json`, and for
   ps5-homebrew-ui the revision in `scripts/build-homebrew.sh` and `cmake/HomebrewUI.cmake`). Commit the submodule
   change with it.
3. Push the fork branch before you push Playden, so the pin is fetchable.
4. Send the change upstream when it is useful there. When upstream merges it, rebase `feat/playden-ps5` onto
   upstream and drop the commit from our branch.

To take a new upstream version, merge or rebase upstream into `feat/playden-ps5`, test, and move the pin.

### Patch a downloaded dependency

The downloads in `deps/` are used unmodified. If one needs a patch, turn it into a fork: add its source as a
submodule on a `feat/playden-ps5` branch, add a build step that writes the result into `deps/`, and pin the result
in `dependencies.lock.json`. You can attach the built result to the fork as a release, so others download it
instead of building it.

### Change Wine

Wine is not forked. prospero-win pins an upstream Wine commit and applies the patch series in
`wine/patches/` on top of it. Our Wine changes are patch files in that series, on our prospero-win fork.

To write or change a Wine patch:

1. Make a scratch Wine branch: check out the pinned Wine commit and apply the series as commits.
2. Make your change there with normal git tools.
3. Export it with `git format-patch` into prospero-win's `wine/patches/`, with the next free number.
4. Check the series: `tools/build_wine_ps5.sh --check-patches` in prospero-win.
5. Commit only the patch file on `feat/playden-ps5`.

When prospero-win moves to a newer Wine, merge it into our branch and refresh only our patch files if they no
longer apply. Rebuilding the runtime and the kit follows prospero-win's `docs/WINE_PS5_BUILD.md` (Linux host).

## Code and commits

- Match the style of the code around your change. Keep product behaviour in this repository and reusable UI fixes
  in ps5-homebrew-ui.
- Add or update tests with each change. Run the host tests before you send a change.
- A build or upload is not console acceptance. Say what you ran on a console, and keep the logs.
- Commit subjects under 60 characters. In the body, use `-` bullets that describe the visible behaviour.

## License

Playden's source files are marked GPL-3.0-or-later (SPDX headers). The submodules and vendored sources keep their
own licenses.
