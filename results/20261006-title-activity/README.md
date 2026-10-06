# Native title activity prerequisite, 6 October 2026

The rebuilt developer Playden title on firmware 13.60 correctly identifies its
own running title `FAKE77031` as active and the stopped Wine title `FAKE78001`
as stopped. This is read-only activity observation, not installed-title ownership
or complete runtime/publication acceptance. Production publication trust remains
disabled and the normal app does not yet compose this adapter.

`native-before-fix.log` records successful AppInfo calls but an unknown result for
Wine. PID 1 has app ID zero with valid system title `NPXS45095`; rejecting that
successful identity prevented complete observations. `native-after-fix.log`
records the correction: valid title identities are accepted regardless of app ID,
and any matching title is active. Untitled identities require app ID zero. Failed
calls, malformed identities, missing APIs, partial/duplicate/mixed-layout process
records, or changed inventories/identities refuse a stopped result.

The process decoder uses checked record lengths and the native PID field at
byte 72. This console returns 1096-byte records, despite SDK `kinfo_proc` being
1088 bytes. Every record must have the same bounded stride and the independently
known self PID must occur in each inventory. The self AppInfo must have a valid
title and nonzero app ID. Absence of a target becomes stopped only after two
complete identity passes enclosed by three identical inventories. The final
native run observed 89 processes and all AppInfo calls succeeded.

Host tests exercise malformed/truncated records, invalid identities, failed API
calls, changed inventories and same-PID title changes, including the system
identity regression observed RED then GREEN. The full host suite passes 106/106.
The actual SDK v0.43 developer packaging target links and signs the tested app.
Building only the executable target is insufficient: it leaves the packaged SELF
stale. Native deployment also preserves `eboot.bin` mode 0755; staged FTP files
otherwise default to 0666 and cannot launch. The initial permission-related
launch refusal disappeared after setting the executable mode, without restarting
ShadowMountPlus or rebooting.

The diagnostic was terminated using its freshly observed PID. Normal executable
and package identity bytes were restored from the independently verified actual
preview backup, re-read and SHA-256 checked, and modes verified. The console was
released. `verification.json` contains command, result and restoration hashes;
full build/test logs remain under `build/title-activity-*-final-*.log`.

This check is a point observation. The publication lease/receipt and Wine reader
lifetime must still protect the interval after it, and real publication/crash/
retry/gameplay acceptance remains open. Other firmware and SDK contexts are not
qualified by this run. No guard capability was enabled and no bottle was edited.
