# Connected-library host verification

The selected Hui renderer is driven by `LibraryController` and the asynchronous `LibraryService` in Live mode, with explicitly synthetic session/backend/cache test doubles. No Steam request, live account, or console is involved.

`home.png`, `library.png`, and `details.png` show the fetched synthetic records. `offline.png` shows Details after cache-only activation, preserving the selected game. Known history is supplied only for the first record; missing metadata remains unknown. Artwork remains a placeholder until the separate asynchronous artwork task.

Reproduce with `ctest --preset homebrew-host -R homebrew_library_graphical --output-on-failure`. Generated captures are under `build/homebrew-host/synthetic-live-library/`.

This verifies actual graphical consumption of worker events and cached records. Authentication/controller tests separately cover offline restart, cache-delete failure/retry, duplicate destructive actions, and queued sign-out at shutdown. It does not establish real Steam retrieval, physical controller behavior, native PS5 acceptance, or frame-rate performance.
