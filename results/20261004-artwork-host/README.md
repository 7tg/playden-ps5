# Asynchronous artwork host verification

The selected Hui graphical consumer loads portrait covers in the background and uploads them on the graphics thread. These captures use synthetic striped covers served by an injected backend, plus one deliberate failure. No real Steam account or artwork is committed.

- `pending.png`: Library while the worker is delayed. Every cell keeps its fixed-size placeholder and title, and focus has already moved (Green → Red) during the delay.
- `ready.png`: the same screen after the covers arrive. The green and red covers appear in their own cells; the failed cell keeps its "Unavailable cover" placeholder.

Production builds derive the cover from the Steam app ID, as the macOS reference does: `https://cdn.cloudflare.steamstatic.com/steam/apps/<app_id>/library_600x900.jpg`. The HTTP backend accepts only that exact URL for the key, with verified TLS, no redirects, a 10-second deadline and a 4 MiB transfer cap. JPEG/PNG decoding checks dimensions before decoding (at most 1024×1024, 4 MiB RGBA) and caps the decoder's own allocations at 16 MiB.

Service, cache and limits:

- At most 64 subscriptions, keyed by account generation, app ID and source. Removing a key cancels its active load and drops its queued results; late results for a removed key, another account or another source are discarded.
- A failed load is retried while the cover stays on screen, after 15 s and then 30 s, for at most 3 attempts. This lets covers recover when the network comes up after boot. A loaded cover is never fetched again while subscribed.
- An unexpected exception from a backend is reduced to a failed load on host builds, and the worker keeps running. PS5 builds compile without exceptions, so the guard compiles away there.
- At most 16 MiB of decoded results waiting, 64 textures / 64 MiB on the GPU, and at most two uploads / 8 MiB per frame. Eviction never releases a texture referenced by the current frame. A GL upload error is reported as a failure, so the placeholder stays.

Validation:

- Selected host build and CTest: **42/42 passed**, including the actual graphical consumer (`homebrew_artwork_graphical`). The artwork tests passed in 5 consecutive runs, and the service test ran clean under ThreadSanitizer.
- Historical legacy regression build and CTest: **21/21 passed**.
- Public HTTPS cover smoke (`homebrew_artwork_backend_test --live-smoke`): decoded **300×450**, 540,000 RGBA bytes.
- Selected PS5 compile, link, sign and package succeeded locally for `FAKE77031`. Final eboot SHA-256: `5701c3589591435179dbe2c9fb642b275dfef5d1ba61b5752688edb235b3551c`.
- An independent final review found no blockers. Its fixes are applied: timed retry, a full-PNG dimension test, a deterministic eviction-protection test and the GL error check.

Known limits, accepted for now: if more on-screen covers than the 64 MiB budget are visible (about 31 at 600×900), the extra ones wait until the user scrolls; the current grid shows about 18–24. During `realloc` the decoder can briefly exceed its 16 MiB budget by the old block's size.

Reproduce with `cmake --build --preset homebrew-host -j 4` and `ctest --preset homebrew-host --output-on-failure`; package with `scripts/build-homebrew-ps5.sh`.

No console deployment or hardware acceptance was performed in this step. Real covers on the PS5 belong to the connected journey. 4K performance work remains deferred.
