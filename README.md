# Playden PS5 Launcher

A controller-first launcher for Windows games on a jailbroken PS5, adapting the macOS Playden experience to PS5 navigation.

This repository owns the launcher and Steam connectivity. Wine development lives separately. Initial releases focus on a native 4K60 interface, direct Steam QR sign-in, library browsing, and game-management UI before real installation and runtime integration.

The reference implementation is sibling `../playden`, branch `feature/launchers`, inspected at `de952b3d49167e635945ada88cd8533e07d40d44`. The earlier singular branch name was a naming discrepancy; the reference checkout is not modified.

The [PRD and roadmap](docs/superpowers/specs/2026-10-04-playden-ps5-launcher-design.md) records the agreed direction and proposed acceptance criteria. Milestone 1 implements a graphical Mac/PS5 shell with Home, Library, Settings, controller navigation, atomic preferences, and frame diagnostics. The active scope is the connected-library v1 in the approved four-milestone plan; Wine integration remains independent and out of scope.

The selected presentation is **direct ps5-homebrew-ui**, merged into `main` in `3bc1e38`. Its [post-selection evidence](results/ui-comparison/homebrew-ui/fixes-2026-10-04/README.md) records the verified layout, font and focus fixes. The [comparison brief](docs/superpowers/specs/2026-10-04-ui-framework-comparison.md) remains the screen/navigation reference. These screens currently use simulated services; real Steam integration is the next work. The old RmlFocus shell remains a historical regression path.

The [Foundation to Connected Library plan](docs/superpowers/plans/2026-10-04-foundation-to-connected-library.md) breaks the first four milestones into tasks: launcher shell, native 4K60 validation, Steam connectivity, and the integrated MVP.

Current work and unproven acceptance gates are tracked in the [v1 execution ledger](docs/engineering/v1-progress.md).

## Build and run

Build the selected presentation with the commands below. See its [build notes](results/ui-comparison/homebrew-ui/README.md) for prerequisites/overrides and the [execution plan](docs/superpowers/plans/2026-10-04-foundation-to-connected-library.md) for remaining production integration. The older [build instructions](docs/engineering/build.md) describe the historical RmlFocus target; the existing `host` preset still selects that target.

```sh
scripts/build-homebrew.sh
cmake --build build/homebrew-host
ctest --test-dir build/homebrew-host --output-on-failure
build/homebrew-host/playden-homebrew
```

Selected PS5 packaging uses the allocated `FAKE77031` title ID and does not contact the console:

```sh
scripts/build-homebrew-ps5.sh
```

`FAKE77031` is allocated to this launcher. Do not use it for another application.
4K performance work is deferred by user direction. Native 3840 × 2160 at 60 Hz remains the eventual MVP requirement; shell builds and brief smoke tests do not establish the milestone 2 performance gate.
