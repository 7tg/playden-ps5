# Playden for PS5

A controller-first launcher for Windows games on a jailbroken PS5, adapting the macOS Playden experience to PS5
navigation.

Playden signs in to Steam with a QR code, shows your library, downloads games, and runs them with
[prospero-win](https://github.com/mpereiraesaa/prospero-win) (Wine for PS5). Each supported game gets its own
private Wine bottle and its own PS5 Home title, so it can start from Playden or from the Home screen. Steam Cloud
saves are checked before a game starts and synced after it ends.

Half-Life 2 is the first supported game. Other games download and show their compatibility status.

## Status

See [STATUS.md](STATUS.md) for what works on the console, the remaining roadmap and the next steps.

## Build

```sh
git clone --recursive https://github.com/leolobato/playden-ps5.git
cd playden-ps5
scripts/fetch-deps.sh
scripts/build-homebrew.sh && cmake --build --preset homebrew-host && ctest --preset homebrew-host
scripts/build-lapy-helper.sh && scripts/build-homebrew-ps5.sh
```

[CONTRIBUTING.md](CONTRIBUTING.md) explains the requirements, the console setup, and how to change a dependency.
The submodule forks are not published yet, so a fresh clone cannot fetch them until they are.

## Documentation

- [Build notes](docs/engineering/build.md) and [dependencies](docs/engineering/dependencies.md)
- [PRD and roadmap](docs/superpowers/specs/2026-10-04-playden-ps5-launcher-design.md)
- [prospero-win integration plan](docs/superpowers/plans/2026-10-05-prospero-win-integration.md) and
  [roadmap](docs/engineering/prospero-win-roadmap.md)
- Console evidence for each feature is under [results/](results/)

The behaviour reference is the macOS Playden app, branch `feature/launchers`.
