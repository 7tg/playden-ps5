# Live library projection verification

These are **host-rendered synthetic data**, not a real Steam account or PS5 framebuffer. The dedicated graphical test uses Live model mode with three explicit synthetic records, a synthetic account, and an injected wall clock. No network or credential material participates.

- [Home](home.png): known play history controls its shelf; placeholder titles remain in their cards.
- [Library](library.png): stable Steam-style IDs, Unicode titles, no fixture artwork or download badge.
- [Details](details.png): personal actions focus correctly; unavailable installation/launch is explained and unknown metadata stays unknown.

Reproduce with:

```sh
cmake --build --preset homebrew-host
ctest --preset homebrew-host --output-on-failure
build/homebrew-host/homebrew_library_graphical_test build/homebrew-host/synthetic-live-library
```

Selected host tests: **31/31 passed**. Local PS5 packaging passed; eboot SHA-256 `35d193ee51f3ccfd3e18b68c3a28a40d01b4c0ebab88e24b3c39e1d5c58c7710`. No console deployment or acceptance, performance measurement, real owned-library request, offline-cache integration or artwork download is established by these captures.
