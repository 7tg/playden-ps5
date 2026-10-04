# Representative library fixture

`library.json` contains exactly 1,000 deterministic synthetic game records. IDs, metadata, missing artwork, and invalid artwork sources exist only for development performance runs and never represent Steam ownership or installation state.

The valid poster and hero PNGs under `assets/ui/art/` are copied from the pinned RmlFocus demo fixture at revision `be7b7e48875ab5b87d2184ae57caed6a58c47cf8`. They are synthetic offline artwork used to exercise decode and texture upload costs.

Names intentionally cover ASCII, Latin Extended, Greek, and Cyrillic code points. The packaged Inter font was checked for those ranges; this fixture makes no claim about CJK or Arabic coverage.


Build the explicit development configuration with `cmake --preset host-fixtures` and
`cmake --build --preset host-fixtures`, then run
`build/host-fixtures/playden-launcher --developer-fixtures`. Both the build option and
runtime flag are required on Mac. The generator validates and compiles this exact JSON;
it does not synthesize substitute records. Normal `host` builds exclude fixture data.
For PS5, `scripts/build-ps5.sh --title-id FAKE77031 --developer-fixtures` explicitly
compiles and enables the synthetic scenes. Omitting that option resets the mode to OFF.
