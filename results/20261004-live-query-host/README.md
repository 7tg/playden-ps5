# Live Unicode query host verification

These captures use the selected graphical Hui consumer, real library worker/controller and synthetic records. They make no real-account or console claim.

- `library.png`: production rail offers All, Favorites, Hidden and personal collections. Installation facts are not yet retrieved, so the Installed scope is omitted.
- `search.png`: entering `CAFÉ` through the existing search editor matches `Café on the coast`. The same test verifies cancel retains the committed query and clearing a no-results query restores all records.

The query/name-sort path reuses pinned utf8proc Unicode case folding. Production deliveries derive title keys once; rendering reuses them. Additional tests cover sharp-s expansion, Greek case folding, exact CJK substring, punctuation, malformed UTF-8 fallback and deterministic app-ID ties. This is not locale-aware collation or general font/shaping coverage.

Reproduce with `ctest --preset homebrew-host -R 'homebrew_(browse|library_model|library_graphical)' --output-on-failure`.
