# Direct Hui comparison implementation plan

**Goal:** Implement the Build surfaces of the comparison brief on feat/ui-homebrew with direct ps5-homebrew-ui.

**Architecture:** A separate C++20 executable consumes the unchanged shared fixture package. Native simulation records own actions and persistence, while Hui owns drawing and public controls. The original Rml presentation executable remains outside this candidate's dependency graph.

**Spec:** ../specs/2026-10-04-ui-framework-comparison.md

**Constraints:** Framework baseline 8a716f294835e26a24ef5612f1535aabffef4080; reference de952b3d49167e635945ada88cd8533e07d40d44. Import only Agent A's shared package. Isolated dependency changes. No deployment, merge, real services or deferred profiling.

- [x] Freeze shared fixture commit and hashes; inspect pinned reference.
- [x] Host worker: isolated kit checkout, explicit paths, macOS GL compatibility, SDL input and captures.
- [x] Simulation worker: U01–U26 native records/actions, modal focus restoration, deterministic injections and isolated persistence, meaningful T01–T12 tests.
- [x] Presentation worker: shared shell, browse/organize, account/settings, management and session surfaces in Hui, native 1080p/4K geometry.
- [x] Lead: integrate executable and build/package PS5 without deployment; record framework patches.
- [x] Run acceptance actions, inspect captures, independent review, repair findings and publish complete coverage/gap evidence.

Review priorities: late async cancellation; dialog input isolation; removed/reordered selection; account cache boundaries; storage disconnect and persistence failure recovery. Tests must pin those transitions, and evidence must distinguish a host replay from physical controller or PS5 operation.

Final host verification: six CTests, strict UBSan simulation, 52 common native captures plus nine supplemental proofs, independent critical-defect review and signed local PS5 package. Actual background artwork decode/upload remains a documented component gap. Console/physical-pad/performance verification remains outside this run.
