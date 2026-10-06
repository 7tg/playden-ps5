# Current Playden preview and artwork cache

The normal FAKE77031 package from integration HEAD `310b3f2` was built with
native runtime access enabled and the developer probe disabled. The previous
remote executable identity was `109e55a6…`; the installed executable is
`74d299eb…`. The executable, package identity, icon and both DDS backgrounds were
read back over FTP and matched the local package; see `deployment.json`.
The title launched and reached Steam sign-in. It was closed for the user to
launch with their own controller. Console ownership was released.

Uploading `/data/homebrew/FAKE77031/sce_sys` did not refresh ShadowMount's
existing metadata copies. All seven artwork copies in `/user/appmeta/FAKE77031`,
`/user/app/FAKE77031` and its `sce_sys` subdirectory contained old bytes.
They were backed up locally under `build/icon-preview-20261006/cache-before`,
then updated and read back; see `cache-before.json` and `cache-after.json`.
Reselecting the title displayed the new portal background. The home tile still
showed the old bear icon. The app database's icon reference retained its old
`?ts=` cache timestamp. No system database edits, uninstall or console restart
were performed. Visible orange-icon acceptance remains open; correct staged
files alone do not prove the shell refreshed its icon.

Screenshots and the local database snapshot are not committed. The PS5 is
released while local integration work continues.
