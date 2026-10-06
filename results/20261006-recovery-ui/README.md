# Explicit recovery: unused settings requests — 2026-10-06

Build `cdf8cd7` on FW 13.60 with HL2 (`FAKE78002`) Ready.

- Settings > Library listed "Unused game settings data: 1" ([screenshot](settings-unused-requests.jpg)): the request of
  HL2's finished publication, which no preparation journal names.
- Selecting it removed `settings-request-de94cd03-….v1` (`runtime.log`: `removed`, `kept=0 remaining=0`); the row
  disappeared.
- After restarting Playden, HL2 stayed Ready and its start check passed (`admission check ... result=ok`).

Host tests cover the other side: while a preparation journal names its request, the request is not counted and
cleanup leaves it in place.

Also in this round, host-tested only: a preparation stopped before the console confirmed its registration shows
"Registration pending" with "Check again", which only queries (`5ddeeac`).

Not covered: corrupt install records, and a Ready game whose files are gone. Both already surface as Unavailable or
Failed with their reason; there is no repair action beyond Retry.
