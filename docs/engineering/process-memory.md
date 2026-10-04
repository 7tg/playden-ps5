# PS5 process memory and presentation counters

## Current resident memory

`playden::ps5::ReadProcessResidentBytes()` reads the current process record with
`sysctl({CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()}, ...)` and returns
`ki_rssize * getpagesize()`. Before supplying an output buffer, it makes the
SDK-documented read-only size query with a null output pointer. The public
header contains only fixed-width integer types and `ProcessMemoryError`; the
SDK's `kinfo_proc` type remains inside the PS5 implementation. Failed platform
calls also return the `errno` value captured immediately after the call as
`native_error`; zero means no meaningful native code was supplied.

`required_record_bytes` records the length advertised by the size query.
`returned_record_bytes` records the length reported by the fetch call, including
when that call fails. The implementation never allocates from the advertised
length: it fetches only when the length is exactly the pinned 1088-byte ABI and
otherwise returns `kUnsupportedRecordLayout`. This both bounds memory use and
prevents reading the known RSS offset from an unknown layout.

The source is the [payload SDK v0.43 release](https://github.com/ps5-payload-dev/sdk/releases/tag/v0.43)
pinned by
`../playden-ps5/toolchain.lock.json` at SHA-256
`a9cc9929f21b2b2c5d5b309f3bab4997067c45281c0622cf4838b1aecba66fcb`.
Its `sys/user.h` identifies its upstream base as FreeBSD `releng/11.4` revision
341778; the corresponding [FreeBSD source header](https://cgit.freebsd.org/src/tree/sys/sys/user.h?h=releng/11.4)
documents the same ABI. That exact installed SDK provides these facts:

- `target/include/sys/user.h` defines `ki_rssize` as the current resident set
  size in pages.
- `target/include/machine/proc.h` defines `KINFO_PROC_SIZE` as 1088 bytes.
- `target/include/sys/sysctl.h` defines `KERN_PROC_PID` and the `sysctl`
  declaration.
- `target/lib/libkernel_sys.so` exports `sysctl`, `getpid`, and `getpagesize`.
- `target/include/machine/param.h` defines the Prospero page size as 16 KiB.
- `samples/ps/main.c` demonstrates the native process-table contract: first
  call `sysctl` with a null output pointer to obtain the required byte count,
  then allocate and issue the fetch.

The SDK header identifies FreeBSD `releng/11.4` as its upstream base. Its
[`kern_proc.c`](https://cgit.freebsd.org/src/tree/sys/kern/kern_proc.c?h=releng/11.4)
handles `KERN_PROC_PID` without thread expansion and writes one
`sizeof(kinfo_proc)` record through `SYSCTL_OUT`. This corroborates the two-call
contract and explains why a capacity mismatch can produce `ENOMEM`; it does not
establish that the PS5 runtime kernel retained the 1088-byte FreeBSD layout.

PS5-native reference implementations explain the likely eight-byte mismatch.
EchoStretch's kstuff-lite
[`find_proc`](https://github.com/EchoStretch/kstuff-lite/blob/9465c61c6ead03637bdb9fefcd88bd49bcbf862c/ps5-kstuff/main.c#L530-L540)
uses a 1096-byte buffer for `KERN_PROC_PID`, while still reading the established
prefix. Other supplied native references obtain the process-list size first
and read `ki_rssize` through the SDK structure. These implementations are
strong evidence that the target kernel may advertise 1096 bytes and preserve
the early fields, but they are not an authoritative declaration of the larger
layout. The launcher therefore records that size without interpreting it.

The implementation checks the compiled structure size, the offsets of
`ki_structsize`, `ki_pid`, and `ki_rssize`, and that the complete RSS field lies
within the record. `ki_layout` is reserved and no value is assumed. At runtime
it requires a successful self query, one complete 1088-byte record, the
requested PID, a positive resident-page count, and a positive page size
equal to the SDK's `PAGE_SIZE`. Multiplication is overflow checked. Any failed
process-ID lookup returns `kProcessIdUnavailable`; failure of the size-only call
returns `kSizeQueryUnavailable`; and a failed record fetch returns
`kQueryUnavailable`. Malformed platform data, an unsupported record size, and
arithmetic overflow return their specific errors without publishing a byte
count. The stable numeric mapping used in native logs is:

| Value | Error | Meaning |
| ---: | --- | --- |
| 0 | `kNone` | Current resident bytes are available. |
| 1 | `kProcessIdUnavailable` | `getpid()` did not produce a valid process ID. |
| 2 | `kQueryUnavailable` | The `KERN_PROC_PID` record fetch failed. |
| 3 | `kInvalidResponse` | The returned process record or page size failed validation. |
| 4 | `kOverflow` | Resident pages cannot be represented as a 64-bit byte count. |
| 5 | `kSizeQueryUnavailable` | The read-only process-record size query failed. |
| 6 | `kUnsupportedRecordLayout` | The kernel advertised a size other than the pinned 1088-byte ABI. |

The helper was cross-compiled without warnings using the pinned toolchain:

```sh
bash ../playden-ps5/scripts/with-env.sh bash -c \
  'exec "$PS5_PAYLOAD_SDK/bin/prospero-clang++" -std=c++17 -O2 \
  -Wall -Wextra -Werror -Isrc -c src/platform/ps5/process_memory.cpp \
  -o /tmp/playden-process-memory-ps5.o'
```

The packaged title records the result enum, native error, bytes,
`required_record_bytes`, and `returned_record_bytes`. Success may be labeled
`process_resident_bytes`. An unavailable result must leave the acceptance field
unknown. `getrusage().ru_maxrss`, ps5-opengl's owned heap, flexible-memory
availability, and direct-memory availability measure different domains and
must remain separately labeled supplementary values.

The first native-title run on 2026-10-04 recorded `available=0`, `bytes=0`, and
the former undifferentiated error value `1`. A subsequent native-title run with
the split diagnostic recorded `error=2` (`kQueryUnavailable`) and
`native_error=12` (`ENOMEM`) from the direct 1088-byte fetch. That proves
`getpid()` succeeded and the process-table fetch rejected the supplied capacity.
It was consistent with a running-kernel record larger than the pinned SDK
definition, but did not prove that size mismatch because the old call did not
first request the required length.

The size-query native-title run on 2026-10-04 then recorded `error=6`
(`kUnsupportedRecordLayout`), `native_error=0`,
`required_record_bytes=1096`, and `returned_record_bytes=0`. This confirms the
kernel advertises the same 1096-byte size used by the kstuff-lite reference.
The zero returned size is expected because the launcher rejected the unknown
layout before fetching it. No RSS value was accepted. Reading the 1088-byte
prefix of that larger record remains disabled until its compatibility is
independently justified and reviewed; until then the resident-memory acceptance
requirement remains incomplete.

## Presentation-status ABI probe

The packaged ps5-opengl runtime owns the VideoOut handle. The probe therefore
belongs behind a temporary build flag in that runtime; the launcher must not
open a second VideoOut port or guess the private handle. No status layout is
accepted from PS4 headers alone. Those headers are useful only for selecting
candidate observations.

The probe build should make these isolated changes:

1. Add `sceVideoOutGetVblankStatus(int32_t, void*)` to `video_api_t` and load it
   through the same native or dynamic path as `sceVideoOutGetFlipStatus`.
2. Allocate separate, aligned flip and vblank buffers with at least 128 payload
   bytes and 64-byte guards on both sides. Fill every region with a changing
   sentinel before each call. Reject a nonzero return or any changed guard.
3. Emit every payload word, a byte-change mask, the submitted flip marker,
   buffer index, monotonic timestamp, and phase/step number as JSON lines. Record
   the title revision, ps5-opengl revision, SDK archive hash, firmware version,
   requested mode, and accepted mode in the same artifact.
4. Use unique monotonically increasing 64-bit flip arguments. Do not derive a
   display event from `eglSwapBuffers`, submission success, frame-start time, or
   ps5-opengl's `runtime_present_count`.

Run the following phases after the output has accepted 3840x2160 at 60 Hz:

1. **Baseline:** query both statuses repeatedly without submitting. Call
   `sceVideoOutWaitVblank` once between samples for at least 16 samples. A
   candidate vblank counter must increase by exactly one for every successful
   wait, while the flip marker and flip counter remain unchanged.
2. **One flip per refresh:** for at least 120 samples, submit one unique marker,
   wait for vblank, and query both statuses. The accepted flip-marker field must
   reproduce each submitted value in order. The candidate displayed-flip count
   must advance once per newly observed marker and never on submission alone.
3. **Known missed bursts:** after a displayed marker, perform one, then two,
   then three successful vblank waits without submitting a flip, querying after
   every wait. The vblank counter must continue advancing and the displayed
   flip count and marker must remain fixed. Submit a new marker after each burst
   and verify that exactly one display change follows.
4. **Submission distinction:** submit a marker and query immediately before a
   vblank when possible. The marker must not be reported as displayed merely
   because submission returned success. Then wait and verify its transition.
5. **Repeatability:** repeat the four phases three times in one launch and once
   after a fresh launch. Counter deltas and field positions must agree. Preserve
   raw output even when an invariant fails.

The probe validates a field by behavior rather than by its presumed offset. It
must establish all of these facts on the target firmware before a reusable API
sets a validity flag:

- a cumulative counter tracks physical vblank slots;
- a separate cumulative counter tracks flips actually displayed;
- a field reports the exact application-supplied marker of the latest displayed
  flip;
- counter width and wrap behavior are defined or safely handled;
- neither status call writes outside its measured buffer;
- readings are monotonic and coherent enough to sample at the chosen boundary.

Once validated, ps5-opengl can expose a versioned query such as
`eglGetPresentationStatsPS5`. Begin the timed interval with one status snapshot
after warmup and finish it with another snapshot. At 60 Hz with flip rate zero,
for each pair of observations:

```text
refresh_slots = vblank_count_delta
displayed = displayed_flip_count_delta
missed = max(0, refresh_slots - displayed)
```

Sampling at every vblank gives an exact consecutive-miss count. If production
sampling observes several vblanks at once, report the misses in that interval as
one conservative burst upper bound. This can reject an uncertain run, but it
cannot turn an unknown ordering into a false pass. A run whose upper bound is
three or more needs per-vblank event sampling before it can satisfy the burst
gate.

The evidence artifact must include the initial and final raw counters, all
intermediate observations used for the burst calculation, status return codes,
and validity flags. If the ABI probe has not passed for the target firmware, the
diagnostic source remains `unknown`; frame intervals and swap counts are not a
replacement.
