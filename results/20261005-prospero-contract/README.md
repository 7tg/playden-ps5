# Task 1 native contract evidence

Firmware 13.60, 2026-10-05. Claims were released between test rounds. Runtime owner
PPSA99995 files and bottle were not changed by this work.

- Direct LncUtil imports are null; dynamic private lookup fails `0x80020003`.
  Internal SystemService `0x80000010` loads, handle 187. Public launch resolves
  after elevation. The first historical crash trace was not retained; replay of the untouched
  original artifact captured SIGSEGV/RIP zero for FAKE77031 pid 535.
- Exact-title FAKE77031 patched Lapy helper granted external access; native Playden
  wrote the external proof file and FTP readback confirmed it. Helper SHA-256 is
  `33c42097d30af928011ac415daa04b3ad10e9fe8f1bf4586bb283a1d2ded4938`.
- Native AppInstUtil is unavailable (`0x80020063`). Elevated Playden reaches the
  existing ShadowMount loopback API without changing its LAN binding.
- Native Playden checked disposable FAKE78000 inventory/paths, provisioned its
  source and ownership marker, and requested manual registration/scan. Initial
  registration failed because the existing service hook was stale. Repeating
  creation refused the existing source, preserving it.
- At the user's request, rerunning the installed ShadowMount payload restored
  registration without reboot or replacing service files/configuration. Bridge
  result 0, `Installed NEW!`, native Query Ready (state 3), Home title name/icon and
  executable mode 0755 were verified.
- The tiny fixture separately failed startup with `0xa0020102` (PRX module load)
  and produced no startup marker. This is not a successfully launched fixture.
- Native removal removed its manual source and retired its owned folder; uninstall
  returned EBUSY (16) while Playden was open. After closing it, the existing console
  uninstall accepted removal. Install directory and app.db entry disappeared, and
  the retired source was deleted after checking exact ownership/file inventory.
- Root-granted public handoff with check flag 1 started PPSA99995 directly with
  `profile=playden-steam-220`, twice from fresh Playden starts at 16:47:09 and
  16:47:58 UTC. Both reached HL2's rendered menu. The existing service completes
  handoff even when the caller returns `0x80940010`; the adapter preserves that
  error. Service startup and rendering are separate evidence.

`launch.log` and `title-contract.log` are accumulated native logs.
`shadowmount-excerpt.log` records stale-hook failure; `shadowmount-restart.log`
records recovery/removal; `shadowmount-handoff.log` records target starts.
`handoff-repeat-hl2.jpg` shows the earlier repeat; `adapter-repeat-hl2.jpg` shows
the final adapter repeat. Immediate final adapter submit at 16:59:10 UTC started
target pid 482 at 16:59:11. Additional delayed checks reached rendered menus twice;
the diagnostic delay was removed because it is not established as necessary. `local-artifacts.json`
identifies diagnostic artifacts, not a release kit. See the contract document for
reproduction and lifecycle constraints. No automated game installation is enabled.

Local verification: host suite 64/64, helper Python suite 49/49, native diagnostic
compile/link/sign/package passed. Original-artifact crash location is captured by replay after fix development;
automated post-exit removal and fixture compatibility are future work.

Original executable reproduction: pid 519 crashed with CE-108255-1 at 17:05:44 UTC.
`original-probe-reproduction.log` records the Playden-specific crash candidate;
`launch.log` records the original request with no return. That attempt did not
capture the fault instruction; the later replay below did. Normal probe-OFF Playden was restored, its Home rendered, it
was closed and the console claim released.

Final crash-location capture: unchanged original executable replay at 17:12:40 UTC
faulted at RIP/address zero with SIGSEGV, pid535.
`original-fault-kernel-excerpt.log` contains the fatal-signal record;
`original-fault-shadowmount-excerpt.log` independently associates pid535 with
FAKE77031. The manifest identifies the binary and full-capture hash. This is a
subsequent replay, not a recovered first historical trace. The particular failing
import is not symbolized. Independent review verified the association and evidence.
