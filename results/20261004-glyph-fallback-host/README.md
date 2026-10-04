# Title glyph fallback host verification

The baked CJK and Arabic atlases cover only the characters of the comparison catalog, plus the European set. This step makes the behavior for every other character explicit and checks it on the selected graphical consumer, using synthetic titles whose non-Latin characters are **not** in the catalog (checked when they were chosen; some common Arabic letters are).

`library.png` shows six titles as placeholder cards (all covers unavailable). `details.png` shows the mixed Latin/Arabic title opened.

| Title | Rendered | Why |
| --- | --- | --- |
| `Persona 5 ザ・ロイヤル` | `Persona 5 ??????` | The kana are in no atlas, so the primary face draws `?` |
| `東方紅魔郷` | `?????` | Same: CJK outside the catalog |
| `เกมส์ไทย` | `????????` | No Thai face at all |
| `Party Night 🎮` | `Party Night ?` | No emoji face |
| `Ωmega Ρrotocol` | correct | The Greek fallback face covers the full European set, beyond the catalog |
| `FIFA 23 كرة القدم` | Latin correct; absent Arabic letters `?` | Arabic face chosen for the whole string; it includes the needed Latin letters |

Rules, now tested:

- A missing glyph always draws a visible `?` with normal advance, never a blank. The font atlas tests require `?` in every fallback face the renderer can choose (Latin, CJK, Arabic, Greek). Cards and Details keep their fixed layout.
- The renderer picks one face per string: Greek or Arabic when the string contains them, the CJK fallback for a character only it has, otherwise the primary face.
- **Arabic is not shaped and has no bidirectional layout.** Letters render in isolated forms, in logical order, drawn left to right, so Arabic words read visually reversed. This remains a documented limitation.

Why this is acceptable for the MVP: the live library comes from `GetOwnedGames` without a language parameter, so Steam returns default (English) store names, which are overwhelmingly Latin. Broad coverage is being built in ps5-homebrew-ui's `feat/unicode-text` branch (run-time glyphs from a fallback font, per-glyph fallback, CJK line breaks). That branch is owned by another effort and still reducing its heap (about 95 MB against 34 MB on `main`). Adopt it when it lands rather than baking a larger CJK atlas here.

Validation: selected host CTest including `homebrew_glyph_fallback_graphical` and the `?`-requiring font atlas tests; captures inspected. No console deployment was performed.
