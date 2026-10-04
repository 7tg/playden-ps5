# Playden PS5 Launcher

A controller-first launcher for Windows games on a jailbroken PS5, adapting the macOS Playden experience to PS5 navigation.

This repository owns the launcher and Steam connectivity. Wine development lives separately. Initial releases focus on a native 4K60 interface, direct Steam QR sign-in, library browsing, and game-management UI before real installation and runtime integration.

The reference implementation is the sibling `../playden` launcher branch. The requested name was `feature/launcher`; the existing checked-out branch is `feature/launchers`, used provisionally as that reference. See the PRD for the recorded revision.

The [PRD and roadmap](docs/superpowers/specs/2026-10-04-playden-ps5-launcher-design.md) records the agreed direction and proposed acceptance criteria. This repository currently contains planning documents only.

The [Foundation to Connected Library plan](docs/superpowers/plans/2026-10-04-foundation-to-connected-library.md) breaks the first four milestones into tasks: launcher shell, native 4K60 validation, Steam connectivity, and the integrated MVP.
