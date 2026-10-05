# Native title contract for prospero-win integration

Task 1 contract record, 2026-10-05. Native handoff and manual provisioning paths are demonstrated; acceptance limits are listed below.
This describes existing PS5 services and the adapter boundaries; it does not enable
automatic game installation or declare a game ready to play.

## Launch boundary

`src/platform/ps5/title_runtime.{h,cpp}` exposes
`TitleBackend::Launch(title_id, arguments) -> TitleResult`. It validates a nine-byte
title ID (four uppercase letters and five digits), limits diagnostic argument vectors
to eight entries of at most 512 bytes, obtains the foreground user and submits the
request. The private provider first initializes LncUtil; the public provider does
not depend on its unavailable private entry points. Failure stops the sequence. LncUtil already-initialized
`0x80940018` is accepted. A returned app ID with its high bit clear means the request
was accepted; it is not proof that the target started or rendered.

The ABI matches the local Itemzflow, CheatRunner and PS5 MCP references:

| Parameter | Offset / value |
| --- | --- |
| `size` | 0 / 32 bytes |
| `user_id` | 4 / foreground user |
| `app_options` | 8 / 0 |
| `crash_report` | 16 / 0, naturally aligned |
| `check_flag` | 24 / public 1 (`SkipLaunchCheck`), private 0 |

Our old probe submitted without initialization, with `check_flag = 2`. The diagnostic
deployed on firmware 13.60 at 15:40:45 UTC established that **both direct LncUtil
imports were null**, while `sceUserServiceGetForegroundUser` resolved. The adapter
returned `ENOSYS` without calling either missing function. The first attempt did not retain a Playden crash backtrace. A later replay of the
untouched original executable captured SIGSEGV at instruction address zero (pid
535), with ShadowMount independently identifying that PID as FAKE77031. The null
import is consistent with this fault; identifying the exact import remains an
inference because the caller is not symbolized. See the replay evidence below.

The proposed native provider uses internal SystemService ID `0x80000010`, obtains its
system module handle, and resolves `sceLncUtilInitialize` and `sceLncUtilLaunchApp`
through `sceKernelDlsym`. The native title builder currently infers one export library
per SDK stub, while the SDK places LncUtil names in its SystemService stub. The provider
therefore must be checked on hardware instead of assuming an import present in the
ELF is callable. Resolution failure produces an unavailable provider, not a null call.
The existing development launch-request file now logs direct and resolved pointers
and the adapter result.

At 16:04:31 UTC the module load and handle query succeeded (handle 187), but
LncUtil lookup returned `0x80020003`. A second probe using the generated NID
also failed. Elevating the title did not expose LncUtil. The same handle **does**
resolve the public `sceSystemServiceLaunchApp` and `sceSystemServiceHideSplashScreen`.
The provider now falls back to public launch with the foreground user and explicit 32-byte parameters and preserves
the argument vector. Portable tests cover both its success and error result. Direct public import resolves without elevation. A test with null parameters
returned `0x80940005`. A candidate module ID `0x800000AB` from SharpProspero was
rejected with `0x805a1000`; the adapter retains the measured working `0x80000010`.
With foreground parameters, ordinary Playden returned `0x8094000f` (permission).
After its exact-title root grant, public launch with `SkipLaunchCheck = 1` returned
`0x80940010`, while ShadowMount prepared a launch retry, terminated Playden and
started PPSA99995 with `profile=playden-steam-220`. The code remains a reported
native error; the adapter does not relabel it as success. The service's handoff
and target PID/mount records establish actual startup independently.

The final adapter immediately submitted from fresh Playden pid 477 at 16:59:10
UTC and ShadowMount started target pid 482 at 16:59:11. Further checks with a
10-second diagnostic delay reached HL2's rendered menu at 17:00 and 17:01 UTC;
repeat handoff pid 497 -> 502 occurred at 17:01:41. The delay was removed: these
observations do not prove it necessary. Earlier direct-call probes also reached
HL2's menu twice at 16:47:09 and 16:47:58 UTC. There is no synchronous success-code
contract here. Task 6 must distinguish submission result, pending service handoff
and target startup, allowing for launcher termination and persisted status.

Elevation and public launch are worker/process lifecycle operations. The developer
probe performs elevation before UI/services start and opens its log beforehand.
Production integration must arrange a safe handoff lifecycle (Task 6); calling the
adapter alone from the existing sandbox does not establish a usable Play action.

## Filesystem grant and native installation capability probe

Normal Playden builds do not request root access. An explicit
`PLAYDEN_TITLE_CONTRACT_PROBE=ON` build consumes
`/app0/dev/title-contract-request.txt` once, before normal UI/services start. It uses
the existing prospero-win elevation client and records to an already-open log file,
because the grant changes the root view and `/app0` can disappear. After the probe,
the process parks; close it through the console controls. Starting the ordinary UI
in the changed root view would invalidate its current storage paths.

The helper comes from our patched `PS5-Lapy-JB-Daemon` checkout, based on
`611fa5f7ed46cdde7094b764168390ab5c4e89f8`, with committed exact-title build
extension `e9f2cfc2c3913e42fdf83d74291e270b041ff0d9`. Its build driver accepts
explicit `FAKE` IDs; the C helper's exact-title comparison is unchanged and wildcard
service discovery retains its existing PPSA-only scope. It is compiled specifically
for `FAKE77031` against the pinned foundation SDK v0.42. Do not use the working
`PPSA99995` helper for Playden. The helper manifest's conservative firmware claims
are not broadened by a single successful test.

The probe attempts a new state-file write under `/data/playden`, then loads
`/system/common/lib/libSceAppInstUtil.sprx` and resolves initialization, title-directory
registration (NID `Wudg3Xe3heE`) and uninstall. These are capability measurements;
actual registration and removal results are recorded below.

Hardware probes passed the exact-title helper request (`helper_completed=1`,
`helper_errno=0`, `/data` changed from inaccessible to accessible). The external
write was read back over FTP. Direct AppInstUtil load returned `0x80020063`, including
after elevation. Playden's loopback HTTP request to ShadowMount returned HTTP 200
with the installed-title snapshot. No LAN exposure or service configuration change
was needed. Evidence is under `results/20261005-prospero-contract/`.

The diagnostic request also accepts `register`, `query`, or `remove` for the fixed
disposable `FAKE78000`. Registration rejects existing source/installed paths and
unavailable inventory before copying the locally built tiny native probe into the
external title directory. Removal verifies its exact ownership marker, removes its
manual scan source and retires its directory before requesting uninstall. A busy
service response is recorded as a failure, not removal completion. These actions
are developer-only and are not connected to production UI.

Reproduction from the launcher checkout (local build only):

```sh
bash ../playden-ps5/scripts/with-env.sh python3 \
  ../ps5-homebrew/PS5-Lapy-JB-Daemon/tools/build_owned_daemon.py \
  --sdk ../playden-ps5/.deps/prospero-foundation/.deps/native/ps5-payload-sdk \
  --logging-client ../ps5-homebrew/prospero-win/native/ps5log \
  --elf-helper --title FAKE77031

bash ../playden-ps5/scripts/with-env.sh bash -c \
  'exec "$PS5_PAYLOAD_SDK/bin/prospero-cmake" -S . -B build/homebrew-ps5 -DPLAYDEN_TITLE_CONTRACT_PROBE=ON'
bash ../playden-ps5/scripts/with-env.sh cmake --build build/homebrew-ps5 \
  --target playden-homebrew-ps5-package
```

The upstream client source directory is explicitly configurable through
`PLAYDEN_PROSPERO_SOURCE_DIR` for this diagnostic. The planned pinned submodule/kit
will replace that development dependency in later tasks. After diagnostic use,
configure `PLAYDEN_TITLE_CONTRACT_PROBE=OFF` and rebuild the normal package.

## Registration/query/removal alternatives

Direct AppInstUtil is used by local payload installers. The ProsperoStore native
reference documents a module-load restriction on firmware 12.70 and delegates to
a file worker there. That does not establish our 13.60 result; the capability probe
checks this before choosing a registration path.

ShadowMountPlus already exposes a supported local HTTP API on
`127.0.0.1:10101`. Its current LAN connection refused; no API binding/configuration
was changed. `src/platform/ps5/title_registry.{h,cpp}` defines a synchronous,
worker-only adapter with an injected transport, independently tested against the
documented response format:

| Adapter | Existing route / completion condition |
| --- | --- |
| `Query(title_id)` | `/api/v1/games`; require `installed`, `managed`, `source_available`, folder source, and exact `/data/homebrew/<title_id>` path for ready |
| `Register(title_id)` | Check query/collision first, `/api/v1/manual/add` with exact folder path, then `/api/v1/scan`; queued scan is not registration completion |
| `RemoveManualSource(title_id)` | `/api/v1/manual/remove`; does not itself unregister |
| `Unregister(title_id)` | `/api/v1/games/uninstall`; API acceptance is not asynchronous removal completion |

`kAbsent` means absent from ShadowMount's cache, not the native install database.
`Register` requires the caller to check native install/source paths independently
and establish ownership. The fixed probe checks `/user/app/FAKE78000` and refuses
an existing external source before creating it. Cache absence alone cannot reserve
a title ID.

A missing/invalid provider response is unavailable, never absent or ready. A same-ID
foreign/unmanaged path is a collision and registration makes no mutations. These
portable tests alone do not prove service reachability; the native diagnostic now
proves loopback read access. No transport is
yet connected to production UI or download completion.

ShadowMount can defer scans or reject uninstall while a game/prepared mount/batch
installation is active. If Playden itself triggers that gate, the workflow must
explicitly account for it; it cannot silently mark readiness or removal complete.
Source folders must be retired/deleted after verifying install ownership before
unregister, otherwise rescanning can reinstall them. Removal completion requires
both registered-state disappearance and source cleanup.

## Title namespace and ownership

Proposed local game-title namespace: `FAKE78000`–`FAKE78999`. This is a Playden policy,
not an official global reservation. `FAKE77031` is the launcher, `PPSA99995` is the
runtime owner's title, and existing `FAKE99xxx` experiments are excluded. Before
reservation, check the install database, registered title inventory and source
folders; persist the chosen ID before publishing files. Exhaustion fails visibly.
Imported titles outside this namespace need a separately reviewed adoption policy.

Use `FAKE78000` for the disposable Task 1 registration check only after proving it
absent and unowned. Write an install-ownership marker and never replace an existing
directory or modify an unrelated title. Hardware checks refused an existing disposable source without overwriting it,
showed the title's name/icon, verified executable mode 0755, queried Ready after
registration and confirmed removal. Portable tests additionally reject same-ID
foreign paths and unavailable inventory. ShadowMount inventory is not a complete
console ID allocator: also inspect the install database and source paths before
reservation. Durable allocation/reinstall bookkeeping belongs to Task 4.

## Current validation

Selected host suite: 64/64 passed after adding launch sequencing/provider and
registration-state/collision tests. Patched helper Python suite: 49/49 passed.
Diagnostic native compile/link/sign/package passed. Final adapter startup is recorded in the service log and repeated rendered target
checks passed. External write,
registration/query/removal and repeated target handoff have been observed. The original artifact's fault location was captured by replay after fix development;
the first historical trace remains unrecovered. This timing deviation is recorded
explicitly, rather than claiming the trace preceded development.

## ShadowMount health check

The disposable source was written by native Playden and manual/add plus scan were
accepted. The scanner mounted it, staged `/user/app/FAKE78000` and then reported
`AppInstallAll hook is no longer installed` / `internal AppInstallTitleDir bridge
unavailable`. The service inventory correctly continued to report it absent.
The MCP filesystem inventory lists staging directories too, so its appearance
there was not successful registration. Launch showed the corrupted-app screen
and no startup marker. Repeating registration refused the existing directory.

At that stage general mounting worked: fresh PPSA99995 mount/handoff records and
live HL2 gameplay were observed. Registration was specifically broken in that
service instance. Source `sm_shellcore_hooks.c` retains `SHELLCORE_HOOKS_STALE`; its start
routine will not reinstall a nonempty state. A clean service baseline was needed before attributing this to the API or adding
a new broker; the existing payload restart below restored registration.
Both referenced AppInst internal IDs (`0x80000014`, `0x80000068`) also return
`0x80020063` from the native diagnostic.

## Existing payload restart, 16:37–16:39 UTC

At the user's request, reran the existing installed payload at
`/data/pldmgr/payloads/shadowmountplus/shadowmountplus.elf` through Payload Manager.
No service binary or configuration was replaced and no console reboot was performed.
The new instance restored the registration hook. Startup scanner consumed the
FAKE78000 TitleDir bridge request with result 0, logged `Installed NEW!`, and
synchronized nine titles. Native Playden query then returned Ready (state 3) with
the expected owned source path. The registration failure was stale service state.

The minimal executable still failed separately with `0xa0020102
(PRX_SCE_MODULE_LOAD_ERROR)` and wrote no startup marker. Its displayed title name
and icon were present; executable readback showed 0755. Native removal removed
its manual source and retired its owned folder, but uninstall returned EBUSY while
Playden was open. After closing Playden, the existing console uninstall path
accepted removal; `/user/app/FAKE78000` disappeared and ShadowMount confirmed
removal from app.db. The retired folder was deleted after verifying its exact
ownership marker and expected file inventory. Console claim released.

This restored the registration baseline without reboot. Native handoff subsequently
passed twice as described above. Disposable executable startup remains a separate
fixture limitation; successful registration is not executable compatibility. Native
uninstall while Playden is open remains busy: automated removal requires a reviewed
post-exit workflow, not a false success or a new unreviewed broker.

## Review and acceptance exceptions

Fresh independent review found the cache/native-inventory distinction above; the
contract and header now state the mandatory caller checks. Review found no other
critical or important implementation issues. The diagnostic remove action is not
a retryable unattended workflow after retirement: a busy response requires the
verified after-close cleanup path. The original artifact crash location is now captured by replay; the first historic
trace remains unrecovered. Fixture execution failed and is not claimed successful.

## Original probe reproduction and normal-build restoration

At 17:05:43 UTC redeployed the backed-up original Playden executable and its original
PPSA99995 launch request. Playden pid 519 crashed at 17:05:44, producing CE-108255-1;
its log contains the request and no result. ShadowMount recorded FAKE77031 as the
crash candidate, distinct from runtime crashes. This reproduces the original failure
with its retained artifact; no faulting instruction address/backtrace was captured.
The temporal requirement to retain the first crash before changes was missed and
remains explicit. The measured later null import is a plausible cause.

Restored the normal probe-OFF package, observed Playden Home, closed it and released
the console. Diagnostic requests were consumed. Artifact hashes and reproduction
logs are retained with the evidence; no original binary is committed.

## Captured original-artifact fault, 17:12:40 UTC

Replayed the unchanged backed-up original eboot (SHA-256 `713fffc424725906048a9936ccf810b447c7008eb14ad3aaae8d9d793c500e12`)
with its original one-shot launch request. The existing kernel-log payload collected
the failure immediately: SIGSEGV, pid 535, instruction-fetch page fault at address
`0x0`, RIP `0x0`. ShadowMount records FAKE77031 pid 535 starting at 17:12:39 UTC and
crashing at 17:12:40; the native log records the handoff request and no return.
The captured backtrace is limited to `0x40015b` and `0x0`, so the particular import
is not directly symbolized. Earlier instrumentation established null LncUtil imports.

This closes the crash-location evidence requirement through a reproducible replay
of the original artifact before restoring the fixed console executable. It does not
recover the first historical trace: capture occurred after fix development, a
recorded deviation from the requested initial investigation order. Independent
review verified the PID/title association, fault record and unchanged binary hash.
No new payload/broker was installed. Normal probe-OFF Playden was restored, its Home
rendered, it was closed, and the PS5 claim released.

## Task 1 completion audit

| Requirement | Verified evidence and scope |
| --- | --- |
| ABI, initialization, module loading, original crash location | 32-byte layout assertions and provider tests; native resolver logs; original-artifact replay kernel fault at RIP zero, correlated with FAKE77031 pid535. First historical capture was missed; replay timing is explicit. |
| Repaired isolated handoff and repeat after launcher restart | ShadowMount pid477 -> 482 immediate adapter handoff; repeated rendered HL2 menu screenshots, including pid497 -> 502. Native error80940010 retained separately from service retry/target startup. |
| External write and disposable registration/query/removal | Exact FAKE77031 helper grant and proof readback; FAKE78000 native source preparation, TitleDir bridge result0, native Ready query; manual-source retirement then existing console uninstall after Playden closes, app.db disappearance and owned-source cleanup. |
| Metadata, modes, collisions, namespace, unavailable provider | Registered Playden Contract Probe name/icon observed; executable0755 readback; existing source refused without overwrite; foreign-path/unavailable-provider host tests; FAKE78000-78999 policy with mandatory independent native/source checks. |
| Separate committed adapter and evidence; automated-install gate | df19870 adapter/evidence; e9f2cfc helper build extension on patched611fa5f; crash capture follow-up committed separately. Diagnostic gate defaults OFF and no production installer is enabled. |

The supported paths rely on the existing patched helper and installed ShadowMount
service. Native AppInstUtil is unavailable; no new persistent broker was introduced.
Task 1 proves the incremental contracts, not a production unattended installer,
removal service or fixture executable. Those limits are retained for later tasks.
