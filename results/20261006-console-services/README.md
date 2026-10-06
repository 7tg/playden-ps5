# Console service checks, 6 October 2026

The read-only 13.60 measurement completed successfully. KEKCALL_CHECK reports
kstuff loaded; native and PS4 replacement tables contain the expected INT3
hooks. Older ShadowMount “already disabled” log lines are not a reliable state
predicate for this kstuff build. No kstuff state was changed by this measurement.

ShadowMountPlus was scanning but repeatedly skipped the existing `FAKE78001`
bootstrap title after reaching its 2/2 mount/register retry limit. Starting the
same installed ShadowMountPlus payload through Payload Manager invoked its
normal replacement-instance restart. The new instance mounted the existing
source and registered `FAKE78001` successfully (`AppInstallAll` result zero).
No reboot or game/bottle replacement was performed. Its HTTP API remains bound
to loopback `127.0.0.1:10101`; LAN port refusal is expected in that configuration.

Elfldr v0.26 was started from the existing local updater artifact, SHA-256
`ed6d587a057c09d6a95b2176e336d9ffc605e547ac56462128b029e47b8ccea1`.
Its listener is port 9021. Port 9305 is the controller payload and must not be
used for raw ELF injection. The normal Playden preview was not replaced.
The console was released after each action/test session.

## Later live check

The fresh read-only probe in `current-output.jsonl` again reports kstuff loaded
and the expected native/PS4 hooks. Payload Manager lists kstuff PID 92 and
ShadowMountPlus PID 1095. ShadowMount continues its 15-second scans and mounted
Playden successfully at 08:38:23 console time. No restart was needed.
The developer diagnostic was stopped, the normal Playden executable and identity
were restored and independently re-read/hash-verified, and the console was
released. `current-check.json` records the restored hashes.
