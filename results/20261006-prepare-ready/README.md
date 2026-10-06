# HL2 prepared to Ready on the PS5 — 2026-10-06

First hardware run of the composed runtime host in the normal Playden app
(diagnostics OFF) on FW 13.60. Half-Life 2's existing download at
`/data/homebrew/FAKE77031/games/220` was prepared in place into its own PS5
title, `FAKE78002`, with a private bottle at
`/data/playden/games/steam-220`. The game files were not moved.

Scope: preparation, OWNER publication, native registration and Ready, then
Home launches of the prepared title: gameplay, an in-game save loaded after a
restart, and a clean quit (later sections). No Cloud, Play-from-Playden or
settings-update acceptance is claimed.

## Result

- Kit `hl2-fw1360-dev1` (two variants) installed at
  `/data/playden/kits/hl2-fw1360-dev1` by `tools/console_kit_upload.py`:
  3559 files and 124 directories verified by read-back SHA-256 and mode.
- Kit admission on the console: production SHA-256 over the whole kit in about
  14 s (`runtime.log`).
- Preparation (`runtime.log`): reserve, stage title (58 s), copy bottle
  (63 s), validate artifacts, persist the immutable settings request, capture,
  publish, one registration submission, and Ready after explicit resumes.
- ShadowMountPlus: `Installing: Prospero Win (FAKE78002)`, `AppInstallAll
  bridge consumed ... result=0x00000000`, `Installed NEW!`
  ([excerpt](shadowmount-excerpt.log)).
- Details shows `PS5 title · Ready · FAKE78002`
  ([screenshot](details-ready-fake78002.jpg)). The registration was never
  resubmitted: the later resumes only queried.
- The new `FAKE78002` Lapy helper completed its grant on hardware:
  `request_result stage=complete error=0 ... cred_changed=1`
  ([log](fake78002-lapy-helper.log)). This is the first console run of that
  helper.

## Problems found on hardware and fixed

| Symptom | Cause | Fix |
| --- | --- | --- |
| Kit refused for firmware | `sceKernelGetSystemSwVersion` reports `13.590.001`; `kern.sdk_version` is `0x13600007` | Firmware from `kern.sdk_version` |
| Capture refused: "Owned game working directory is unavailable" | Console downloads are `0777` | Prepare tightens an app-owned download root to `0755` |
| Resume refused: "no valid frozen operation" | Control journal routed to `Resume` | Route through `ResumeRequested` |
| Re-capture refused an orphan registry backup | Backup written before the preflight that refused | Accept only this transaction's identical backup |
| Registration never completed | ShadowMount cannot read an app-owned `0700` title folder ("expected game directory") | Title directories are app-owned `0755` |
| Registration pending while Playden is open | ShadowMount pauses scans while any title runs, Playden included | Close Playden; explicit resume then queries |

## Home launch diagnostic

`FAKE78002` launched without arguments ([session log](fake78002-wrapper-session.log)).
It read the schema-2 descriptor and passed publication admission (one
admission with the SDK v0.42 wrapper; this is not a lease-lifetime claim).
It selected `playden-steam-220` with
`exe=Z:\data\homebrew\FAKE77031\games\220\hl2.exe`, loaded `ntdll`, and
initialized video, pad, HID and audio. Wine then stopped with:

```text
wineserver: /data/playden/games/steam-220/library/prefixes/game is not owned by you
```

The PS5 shows CE-108255-1. After the grant the helper leaves the real UID at
0, and the wrapper restored only the effective UID. wineserver compares the
prefix owner with the real UID. Upstream `06bc6b6` restores the real UID while
the grant still holds root. It is host-tested and not yet on the console.
Delivering it requires refreshing the installed title to a new kit.

## Other hardware facts

- zftpd cannot read app-owned `0700` directories either; Playden now exports
  the newest wrapper session log to `logs/title-<TITLE>.log`.
- The console clock runs about four minutes behind the build host.
- The deployed Playden build is the runtime-host build. The pre-session
  preview (`eboot.bin` `d76d1a42…`, identity `c7593741…`) is backed up in
  `build/console-backups/20261006-runtime-host` for restore with
  `tools/console_title_deploy.py restore`.

## HL2 gameplay from the prepared title (later the same day)

After Playden's title refresh installed the `9f31431` wrapper (eboot verified
by SHA-256 over FTP; Ready kept, no new registration), `FAKE78002` launched
from Home with no arguments:

- The data grant reported `credential_errno=0`: the real-UID restore works on
  hardware, and wineserver accepted the app-owned private bottle.
- HL2 main menu ([screenshot](hl2-menu-fake78002-home-launch.jpg)); Cross opened
  New Game; Chapter 1 loaded and the G-Man intro and train arrival played
  ([train](hl2-train-ingame.jpg), [moved](hl2-moved-and-turned.jpg)).
- 20 min 27 s of continuous gameplay (10:13:23 to 10:33:50, build-host
  clock) with controller movement, look and interaction prompts responding
  ([last frame](hl2-21min-interact.jpg)).
- Audio: the capture setup has no audio input. The wrapper's own counters at
  the end of the session report `audio=267978 audible=264313` buffers, about
  98 % non-silent. That is the only audio check available here.
- Save: the pause menu's Save Game wrote `hl2/save/half-life-001.sav`
  (1046391 bytes) into the recorded download root
  `/data/homebrew/FAKE77031/games/220`.

## Relaunch, save load and clean quit

The first relaunches after that session failed with CE-108255-1 about two
seconds after a successful grant. Wine runs with the title's umask 0, so it
rewrote the bottle's `user.reg` as `0666`, and the publication guard refuses a
group- or world-writable registry (stage 7). Two fixes:

- Playden repairs the bottle: Details offers Repair, which sets the
  app-owned registry files back to `0600` and refreshes the title.
- The wrapper sets a private umask after the grant (prospero-win `eba69f8`).
  Before the grant the libkernel gateway is not resolvable. The session report
  shows `umask private=1 previous=0 errno=0`. Files HL2 wrote afterwards are
  `0600` (`half-life-002.sav`). Every later admission check reported
  `result=ok` with no repair.

After the repair, `FAKE78002` launched from Home. Load Game listed the save
([dialog](hl2-load-dialog.jpg)), and it loaded at the locker where it was made
([loaded](hl2-save-loaded-after-restart.jpg)).

HL2's own Quit then showed CE-108255-1
([session](fake78002-quit-session.log): `exit` and `session_end
reason=wine64-exit`, no fault). The wrapper let Wine's `exit()` end the
process. The system reports a title that ends its own process as a crash.
prospero-win `1a8a0d5` closes a per-game title through
`sceSystemServiceLoadExec("exit")`, as other titles do. With that kit
installed through Playden's Update (`FAKE78002` eboot `17f97d45…` verified
over FTP), the main menu's Quit returned to Home with no error
([Home](hl2-quit-clean-home.jpg),
[session](fake78002-clean-quit-session.log)).

A separate hazard: launching a title while ShadowMount is still finalizing its
previous instance also fails. Wait for `runtime layers released`.
