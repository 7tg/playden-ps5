# Framework and platform patch inventory

The shared framework source is unchanged. Candidate-specific changes are applied only to the detached `build/dependencies/homebrew-ui` checkout at `8a716f294835e26a24ef5612f1535aabffef4080`.

| Delta | Purpose | Evidence |
| --- | --- | --- |
| `patches/homebrew-ui-macos-4.1.patch` | Apple GL declaration wrapper; GLSL 4.1 and linked uniform lookup; base-instance attribute rebasing; portable canvas copy; valid font sampler bindings; corpus-driven font baker ranges | `host-compatibility.md`, native host build and captures |
| `patches/homebrew-ui-cff-sdf.patch` | Offline SDF baking for CFF cubic outlines, correcting Greek/CJK fallback corruption | `font-baking-fix.md`, `homebrew_font_cff` regression |
| Candidate `src/homebrew/view.cpp` | Shared Barlow plus Noto fallback atlas selection; custom Playden shell/shelves/details/management/modal composition using public kit drawing/controls | `framework-gap.md` and matching captures |
| Candidate native linker integration | Explicit Hui/UI/model archives and read-only PS5 SDK/OpenGL/runtime dependencies; no Rml archives | `cmake/HomebrewPS5.cmake`, PS5 build output |
| Copied native module writer heap patch | 256 MiB initial title heap in candidate-local generated host-tool source | `cmake/HomebrewPS5.cmake`; no shared boilerplate edits |
| Candidate splash alias | Strong `hui_release_splash` wrapper resolves the kit's optional callback for the native signer, releases the held splash after first swap | `src/homebrew/ps5.cpp` |
| Native I/O adaptation | Stdio persistence and string builder avoid unavailable SDK locale/fstream symbols; UTF-8 codepoint caret/backspace | Native link success and editor tests |

The PS5 host tooling and OpenGL source retain their existing pinned inputs from `dependencies.lock.json`; CMake verifies the OpenGL archive, compiler builtins and libc PRX hashes. No candidate patch changes a fixture volume into a real installation path. No console patches were installed or executed.
