# Ready-title requested settings handoff

Ready-title updates now persist the exact desired settings request and then a
capture-update OWNER journal. The OWNER retains its currently committed revision
while that journal is present. Play and native handoff reject any active OWNER
journal, so the retained Ready state cannot bypass pending publication.

New update requests require complete committed package inspection at the current
revision, stopped activity and coherent existing native registration. Recovery
uses the immutable request before capture, and its exact prepared intent after
capture. Retirement still verifies the prior committed set, current revision,
matching requested transaction and native registration under the publisher lease.
The complete publication changes OWNER to Preparing before file effects and
advances the revision only after committed verification. Updates never repeat
native registration or restage the title/bottle.

Regression tests failed on Ready update request acceptance and launch admission
before the implementation. Cold repository/service recovery now succeeds both
before capture and after prepared handoff. Tests verify previous revision and
profile preservation, the desired DXVK bytes after recovery, cleared journal and
exactly one registration across initial installation and update. Launch tests
refuse both new Play requests and handoff when a settings request appears.

Full host suite passed 108/108 in 25.96 seconds, including native-policy launch
coverage. SDK v0.43 compiled the OWNER/publication and launch archives and linked
the developer package. No new PS5 deployment was performed for these OWNER
changes; the prior request storage hardware probe does not prove native Ready
update execution. Production UI/application composition and full native
publication/interruption/Wine lifetime/gameplay acceptance remain open.
