# Production persistence host verification

The selected Hui graphical consumer restores real organization/preferences files (with account organization stored privately) using synthetic account and library records. No real Steam credentials or account data are used.

`restored.png` shows the Library after a fresh live model and persistence service reload the favorite selected through controller actions before shutdown. Favorites count is one and the selected title remains Café on the coast. The same test toggles reduced motion through Settings and asserts that the restored model has it enabled; the static capture does not establish animation timing.

Production Mac/PS5 entry points now construct PreferencesStore, OrganizationStore, PersistenceService and PersistenceController. Global reduced motion is separate from account favorites, hidden games, collections, ratings, notes, queries, filters, sorting and selected identity. Simulation profiles remain separate.

Behavioral regressions cover pending-load edits and collection-ID collisions, explicit reset, failed reads/writes and retry, bounded journals and codec limits, selected focus after delayed loading, stale-account results, deletion failure and retry, and the final input frame being saved during shutdown. Organization deletion completes before credential deletion; failed deletion retains the account and allows retry. Accepted account saves survive invalidation; global preferences survive account changes.

Validation:

- Selected host build and CTest: **38/38 passed**, including actual graphical rendering.
- Historical legacy regression build and CTest: **21/21 passed**.
- Selected PS5 compile, link, sign and package succeeded locally for `FAKE77031`.
- Final packaged eboot SHA-256: `ba57cf4daf9efca8701f7be634e6631814ccdbebb70129b92aa8089306d6407e`.

Reproduce with `cmake --build --preset homebrew-host -j 4` and `ctest --preset homebrew-host --output-on-failure`; package with `scripts/build-homebrew-ps5.sh`.

No console deployment or hardware acceptance was performed. Native storage/restart, real Steam account journeys and physical DualSense checks remain pending. 4K performance work remains deferred. Artwork retrieval, broader font coverage and authenticated CM are separate remaining v1 work.
