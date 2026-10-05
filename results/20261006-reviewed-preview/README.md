# Reviewed Playden preview on PS5

Deployed the normal `FAKE77031` package after the final game-settings merge
(`37fa032`) and bottle admission correction (`30d5433`). The developer title
contract probe is disabled. Eboot SHA-256:
`109e55a60d1e554104ba787421cce77ef2c153acc2379284cf5364af6d1552b1`.

Fresh host build and all 75 CTest tests pass. The PS5 package target also passes.
Changed files were backed up locally and every uploaded file was read back and
checked by SHA-256. `deployment.json` records the exact replacements. Existing
account, game downloads, settings and logs were preserved.

The console launched Playden and rendered Home successfully. The account name
is absent from the top navigation; the account indicator and clock remain.
Half-Life 2 and 140 both appeared under Recently Installed. The startup log
confirms the helper grant, restoration to EUID 1, readable private account state
and successful UserService initialization. This checks native startup and UI;
live Wine settings publication and per-game gameplay remain unverified.

Playden was closed and the console claim released so the user can launch it
from PS5 Home with their controller. No reboot or ShadowMount restart was needed
for this preview launch. The separate staged `FAKE78001` bootstrap fixture was
not launched or registered during this check.

The check occurred on 2026-10-06 local time; the console log timestamps are UTC
on 2026-10-05.
