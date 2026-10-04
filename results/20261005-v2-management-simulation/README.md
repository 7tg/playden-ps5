# v2 acceptance: install and management UI on the simulated backend

PRD gate for v2: *all queue, storage, confirmation, recovery and restart scenarios are controller-accessible and testable without Wine or real installation.* The management screens come from the merged UI-comparison work; this records how each PRD journey is covered. Simulation runs only in the explicit simulation build/profile; normal builds keep these actions unavailable (live mode explains them).

| PRD journey or failure | Fixture scenario | Behavioral test | Controller-only path |
| --- | --- | --- | --- |
| Choose Install, review required space | `install-ready`, `unknown-size` | `t05_install_rechecks_storage_and_submits_once`, storage `assessment_uses_installed_footprint_and_preserves_unknowns` | `homebrew_management_tour` |
| Select storage, unusable volumes shown | `install-ready` | `t05`, storage `assessment_rechecks_availability_and_writability` | `homebrew_management_tour` |
| Insufficient space blocks submission | `insufficient-space` | storage assessment (footprint larger than free bytes) | `homebrew_management_tour` |
| Queue work, one active transfer, reorder | `default` | `t06_queue_transitions_are_single_active_and_safe` | `homebrew_management_tour` |
| Inspect progress | `default` | `t06`; progress advances in the tour | `homebrew_management_tour` |
| Pause / resume | `paused`, `default` | `t06` | `homebrew_management_tour` |
| Cancel with safe default | `default` | `t06` (declining keeps the job) | `homebrew_management_tour` |
| Transfer failure and retry | `download-failed` | `t06` | `homebrew_management_tour` |
| Verify files; verification failure and retry | `verification-failed` | `t06` (Verify queued once), `t07_interruption_and_verification_recovery` | `homebrew_management_tour` |
| Storage becomes unavailable | `storage-disconnect` | `t05` (blocked before confirm), `t07` (interrupted during transfer) | scenario start; resume in tour |
| Restart recovery | `restart-recovery` | `t07` (no invented completion after restart) | `homebrew_management_tour` |
| Confirm uninstall; saves preserved | `default` | `t09_uninstall_and_reset_preserve_declared_data`, storage `completion_moves_reservation_to_used_and_uninstall_preserves_saves` | `homebrew_management_tour` |
| Downloaded is distinct from runtime readiness | `runtime-unavailable` | `t07` | — |
| Session launch, running, stop, crash (simulated) | `launch-failure`, `crash`, `stop-failure` | `t10_session_failure_running_stop_and_crash` | `homebrew_controller_tour` (failed stop recovery) |

`homebrew_management_tour` (new) starts each journey from a fixture scenario and then uses only d-pad, Cross, Circle, Options and L1/R1 presses. A row reachable only through replay setters fails the test.

Validation: selected host CTest **49/49**, including `homebrew_simulation`, `homebrew_storage`, `homebrew_controller_tour` and `homebrew_management_tour`.

Limits: these journeys run against the deterministic simulator only. Real downloads, storage, verification and uninstall are v3; runtime launch is the prospero-win milestone.
