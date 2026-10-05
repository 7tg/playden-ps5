# PS5 session privacy and relative-directory contract

Date: 2026-10-06 local (console/log timestamps 2026-10-05 UTC).

The isolated `FAKE78001` wrapper was rebuilt against native foundation `9c0b994` with the patched title-specific Lapy helper. Developer wrapper eboot SHA-256: `0dc9860a1a4634895963cfe077c7323c32df60a6d64dbbf0fdce884fafd17e76`.

[Wrapper output](wrapper-private-session.log) records successful helper/credential restoration and a fresh Home session, then fails closed at the intentionally incomplete managed library. [Playden native inspection](native-private-session.log) verifies private root/log directories and a UID-1/mode-0600 failed session record. The old public log directory was preserved separately before creating the new private one; no saved data was deleted.

The same native evidence compares libc-created and direct-kernel directory FDs. Both are valid directories; relative `openat` fails with errno 22 for all tested flags, while absolute `openat` succeeds. Production descriptor-relative bottle preparation remains gated. The source of that refusal is not yet established.

After both diagnostics, normal `FAKE77031` eboot SHA-256 `109e55a60d1e554104ba787421cce77ef2c153acc2379284cf5364af6d1552b1` and its package identity were restored byte-for-byte. The diagnostic was closed and the PS5 claim released. This proves private persistence/access for failed startup, not a complete install, game execution, successful exit, or gameplay.
