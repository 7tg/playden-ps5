# Milestone 1 shell acceptance

Tasks 1A–1B are implemented and shell-verified on Mac and PS5. Steam, performance fixtures, installation, and Wine integration were not added.

## Reproduction and artifacts

Run `cmake --preset host`, `cmake --build --preset host`, and `ctest --preset host --output-on-failure`. Package with `scripts/build-ps5.sh --title-id FAKE77031`; the script never contacts the console. See [build instructions](../../docs/engineering/build.md).

RmlFocus is pinned to `be7b7e48875ab5b87d2184ae57caed6a58c47cf8`. The macOS Playden reference remained untouched at `feature/launchers`, revision `de952b3d49167e635945ada88cd8533e07d40d44`.

The accepted signed executable is `build/ps5/dist/FAKE77031/eboot.bin`, SHA-256 `d7910b2790566707af92ea64f23aec312bf69e4fa40a0b1910e6ce2cdfe09d42`. `package-manifest-accepted.json` records every packaged file; executable bytes were read back from the console and matched. `source-manifest.json` identifies the source snapshot, including the final Shell cleanup regression. Implementation commit: `262fff9`. A final whitespace-only normalization was rebuilt and produced the identical accepted executable hash. Runtime metadata honestly records the planning parent revision plus `+dirty`, since the build preceded implementation commits. Exact source hashes and package hashes identify the tested artifacts.

`FAKE77031` was absent from both staging and installed-title directories before allocation. User confirmed console availability. All three launches used the existing app-owned MCP controller service with human control false; no runtime title, payload, or controller service was stopped. The transfer command tried existing zftpd and fell back to Web File Manager because zftpd was not running. It did not start another payload. The console was left on its home screen after the final clean exit.

## Host verification

- Configure and graphical build passed; all three CTest suites passed (`host-configure.log`, `host-build.log`, `host-tests.log`).
- Tests cover path rejection, bounded corrupt reads, atomic replacement/failure, preference defaults and restart, locale-independent frame JSON, bounded frame samples, layout, geometric D-pad movement, selected section versus focus, L1/R1, modal focus trapping/restoration, exit cancellation, and destruction without caller-side document flushing.
- Real patched OpenGL captures at 3840×2160 were inspected: `host-home.png` and `host-settings.png`. These are actual framebuffer captures, separate from null-renderer behavioral tests. Scripted `--keys R1,R1` and timed exit ran successfully.
- Initial overlapping text was fixed with explicit block layout and a red/green geometric regression. A heap-owned Shell shutdown regression reproduced a SEGFAULT and passed after Shell itself drained deferred RmlUi documents while its event listener remained alive.

## Console verification

The three runs distinguish intermediate evidence from the final artifact:

| Run | Executable SHA prefix | Result |
| --- | --- | --- |
| First | `431f31bc` | Navigation, saved preference, storage probe, and Settings clean exit passed. Superseded because an incremental-link defect omitted new memory code. |
| Second | `7557b672` | Saved preference restored after relaunch, corrected modal hints inspected, owned-heap samples present, held-Options clean exit passed. |
| Accepted | `d7910b27` | Final Shell cleanup fix included; Library sheet and restored focus, restored preference, two further preference writes, memory records, and Settings clean exit passed. |

`console-accepted.log` reports an actual EGL surface and render viewport of 3840×2160, accepted scanout mode 3840×2160 at 60 Hz, a successful atomic write/read probe under `/app0/playden-data`, nonzero memory samples, and `exit status=0`. Saved `preferences.conf` was read back separately. `console-accepted-preference.png` shows the restored setting, and `console-accepted-exit.png` shows the console home after system-managed exit. Input user/handle identifiers were redacted from logs.

The actual controller sequence was R1 → Library, Cross → sheet, Circle → restored opener, R1 → Settings, Cross → toggle/save. After relaunch, Settings showed the saved value. The accepted run toggled twice and confirmed Exit Playden with Right/Cross; the second run separately tested held Options.

PS5 captures are 1920×1080 HDMI captures; native render resolution is established by the EGL/render diagnostics, not those image dimensions. Memory measures the ps5-opengl owned heap, not total resident process memory. Missed hardware presentations remain explicitly unknown; frame-start intervals are not substituted for hardware counters.

## Review and limits

Independent cross-review resolved locale-sensitive JSON numbers, GL teardown ordering, source/binary pin checks, asset refresh, incremental PS5 object dependencies, and deferred-document lifetime. A new high-reasoning reviewer could not be spawned because the runtime counted completed agents against its thread limit; gpt-5.6-sol/medium workers reviewed components they did not implement. The final reviewer reported no remaining important code findings.

This is brief shell acceptance, not the milestone 2 sustained native 4K60 gate. Physical DualSense feel, long-run memory/performance, representative library/artwork workload, and broader firmware compatibility remain unverified and out of this milestone. The console firmware baseline is inherited from the sibling project's 13.60 environment; this run did not independently query firmware or alter it. Atomic rename is verified across normal app restarts, not sudden power loss.

Early `host-initial.log`, `console-first-live.log`, and first/second manifests are retained as superseded integration evidence, not final acceptance claims.
