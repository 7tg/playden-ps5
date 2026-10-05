# macOS feature ports: volumes, notifications, logs, cloud saves

Plan written 2026-10-05. Ports four features from the macOS Playden app (`../playden` at `de952b3`, branch `feature/launchers`) that are not yet in the PS5 launcher. Order is fixed: **1 install-volume picker → 2 notifications → 3 log viewer → 4 cloud saves**. The first three are independent of the (currently blocked) prospero-win runtime; cloud saves is sequenced last because it only becomes useful once games run, though its sync engine is built and tested against Steam on its own.

Other-store support (GOG/Epic, `StoresModel`, `WebLoginWindow`, publisher accounts) is explicitly out of scope, per the user.

## Global constraints

- Work on `main`, commit coherent increments, keep the selected ps5-homebrew-ui presentation; product behavior lives here, reusable renderer fixes go to ps5-homebrew-ui.
- Read the macOS app only with `git -C ../playden show de952b3:<path>`; never disturb that checkout.
- PS5 build is C++20 with `-fno-exceptions -fno-rtti`; every feature needs host unit tests that run off-console. Follow the user's console etiquette (claim/refresh/release, never sign out) for any console verification.
- Each feature keeps its model/logic in testable host code (`src/...` + `tests/...`), with the platform entry points (`src/homebrew/ps5.cpp`, `src/homebrew/host.cpp`) only wiring real implementations.

## Current baseline

- Installs: `InstallService(runner, sessions, games_root)` with a single hard-coded root (`/app0/games` on PS5, `data_dir/games` on host). `RootFor(id) = games_root + "/" + id`. A `.playden-games` registry and per-install `.playden-files` list drive scan/uninstall because the console's `readdir` is unreliable and `statfs` terminates the title.
- The homebrew model (`src/homebrew/simulation.hpp`) already defines `Volume`, `Installation` (with `volume_id`, `available`, `saves_present`), `Job` (with `volume_id`, `reserved_bytes`), `Profile`, and `RowKind::{Volume,Profile,Status}` plus `selected_volume_id_` — scaffolding left from the v2 management **simulation**. The live-mode path does not populate them yet (open TODOs: empty Storage panel in live mode, "0.0 GB" labels).
- Logs are append-only text at `/app0/logs/{auth,install,launch}.log`; nothing reads them in the UI.
- `PreferencesStore` persists a tiny global `Preferences{reduce_motion}` (not account-scoped) — the right place for a global install-volume default.
- Steam CM client already does: logon with refresh token, access tokens, PICS (packages + apps), depot keys, manifest request codes, CDN download, gzip/zstd/lzma. Cloud saves builds on exactly these primitives.

---

## 1 Install-volume picker

**Goal.** Let the user pick which PS5 drive new installs use — internal console storage (`/app0/games`) or the M.2 SSD (`/mnt/ext1`, ~315 GB free on this console) — remember the default, route installs/uninstall/scan to the right drive, and show a per-game "drive disconnected" state when a game's drive is absent. Also populate the Downloads **Storage** panel with the selected volume's real figures in live mode (closes the existing TODO).

macOS reference: `App/InstallationDrives.swift`, `App/InstallVolumesPicker.swift`, `App/DownloadsStorage.swift`, `GameLocation`/`InstallationRecord`/`VolumeManaging` in `Packages/PlaydenKit`. The macOS model is heavy (security-scoped bookmarks, arbitrary external volumes, `resolve`); the PS5 has a small fixed set of mounts, so we port the *behavior* (choose default, route, disconnected state, storage bar) over a far simpler volume source.

### 1A Console spike — is the M.2 writable from the title sandbox? (gate)

Before any code: verify the FAKE77031 sandbox can create and write under `/mnt/ext1/...` (installs today only touch `/app0`). On the console, attempt a small write via the real install path (a dev request that targets an M.2 root) and confirm files land and read back. If the sandbox cannot write `/mnt/ext1`, keep a single volume, ship the picker **disabled** with a recorded reason, and stop after 1B's storage-panel fix. Record the result in `results/<date>-install-volumes/`.

### 1B Volume source and install routing (host)

- New `VolumeCatalog` abstraction (`src/steam/` or `src/platform/`): returns ordered candidate volumes `{id, label, games_root, present, writable}`. PS5 implementation returns internal always, and M.2 when `/mnt/ext1` is present and writable; host returns one `data_dir/games` volume. Keep it injectable for tests.
- Extend `Preferences` with `install_volume_id` (default empty = first/internal). Persist through `PreferencesStore`.
- Teach `InstallService` about multiple roots: construct it with the `VolumeCatalog` and the default id instead of one `games_root`. `RootFor(id)` resolves a game to its volume via a per-game index; new installs use the default volume. Record `volume_id` in the install record and keep one `.playden-games` registry per volume root. `Scan()` reads every known volume's registry; a game whose volume is absent is surfaced as `available=false` (→ `InstallStatus::driveDisconnected`, matching `applyInstallationDriveStatuses`).
- Uninstall and resume already go through `RootFor`, so they follow automatically once routing is correct.

### 1C Picker UI and live storage (model + view)

- Populate the model's existing `volumes`/`selected_volume_id_` in **live** mode from the `VolumeCatalog`. A "Games volume" panel (reachable from Settings) lists volumes and sets the default, mirroring `InstallVolumesPicker` focus/activation (`moveInstallVolumeFocus`, `activateInstallVolumePicker`): up/down move rows, select toggles/sets default.
- Fill the Downloads `Storage` card (`DownloadsStorageCard` equivalent) with the selected volume's figures. Free/total come from a **permitted** source only — the title cannot `statfs`. Pursue, in order: (a) a system-service query if one is reachable from the sandbox; (b) otherwise show used-by-games (summed from install records) and reserved-by-queue, and render free/total as "—" with a note. Do not reintroduce `statfs`. Whatever (a) yields is recorded in the 1A spike notes.
- Per-game Details/Library badge for `driveDisconnected`.

### 1D Tests and evidence

- Host: `VolumeCatalog` resolution (present/absent/writable), `InstallService` routing + multi-volume scan + uninstall-on-correct-volume + disconnected status, `Preferences` round-trip of the default, model rows for the picker and storage card.
- Console: install one small app to the M.2 (if 1A passes), show it under the right volume, and the Storage panel reflecting the choice. Evidence in `results/<date>-install-volumes/README.md`.

---

## 2 Notifications (toasts)

**Goal.** A transient toast for install/verify/uninstall job completion and failure, and for controller connect/disconnect, shown only when the user is not inside a panel, dialog, auth, or overlay.

macOS reference: `App/NotificationModel.swift`, `App/NotificationViews.swift`. Port the model closely: `LauncherNotification{source, tone(success/failure/warning), title, detail, guidance?}`, a bounded queue (max 8) deduplicated by `source`, shown via a `visibleNotification` gate, driven by job-state transitions and controller connection.

### 2A Model (host)

- Add the notification types and queue to the live model in `simulation.{hpp,cpp}`: `enqueue` (dedup by source, cap 8), `expire`, and a `visible()` accessor gated on "no panel / no dialog / no auth / no active session / no errors" (the PS5 analogue of macOS `visibleNotification`).
- Drive job notifications from the existing live-install snapshot diff already consumed by `TickInstalls`: keep the previous job→state map, and on a transition into completed/failed emit a success/failure toast with wording from the macOS switch (install/repair/uninstall × completed/failed) plus the failure reason and a guidance line ("Open Downloads for Retry and View logs"). Restored history (first snapshot) is not an event.
- Controller connect/disconnect: emit from the input layer's pad-presence change (minimal on PS5 since the DualSense is usually present, but keep the path for parity and for completion toasts).

### 2B View and timing

- `Controller::TickNotifications` advances an expiry timer; the top toast auto-dismisses after a few seconds. `view.cpp` renders one tone-colored toast in a corner that does not fight focus.

### 2C Tests

- Host: dedup + cap, state-transition → toast mapping (each kind/outcome), visibility gating against panels/overlays, expiry, "no toast for restored history".

---

## 3 Log viewer

**Goal.** View the on-device sanitized logs (`auth.log`, `install.log`, `launch.log`) from the UI, with recovery actions on a failed job — valuable for unattended runs where logs are otherwise only reachable over FTP.

macOS reference: `App/LogModel.swift`, `App/LogViewer.swift` (`LogDocument`, `LogRecovery`, `logActions` = Close / Retry / Reveal, `activateLogAction`). PS5 differences: logs are plain files already on disk; there is no "Reveal in Finder".

### 3A Reader (host)

- `LogReader` that returns the **tail** of a chosen log (bounded, e.g. last 128 KB — never read past the 16 MiB HTTP/response limits or load a whole large file). Pure, file-backed, testable with a temp file.

### 3B Panel, recovery, scrolling (model + view)

- A "Logs" panel reachable from Settings and from a Downloads job's actions ("Open logs" — the action label already exists in the v2 model and macOS). Render the tail as scrollable `Text`/`Status` rows; controller up/down and L2/R2 page.
- Actions mirror macOS `logActions`: **Close** always; **Retry** when the panel was opened for a failed job/session (wire to `InstallService` Resume/Retry via the existing live-job action path); no reveal action.
- `logRecovery` analogue: offer Retry only when the focused job is `failed` (or a session ended in launch-failed/crash once the runtime exists).

### 3C Tests

- Host: bounded tail (small and over-large files), action set given job state (failed → Retry present; completed → absent), retry dispatches the right job id.

---

## 4 Cloud saves

**Goal.** Sync Steam Cloud saves for installed games — download remote saves before a game launches, upload local saves after it exits, and resolve local-vs-remote conflicts (the macOS `CloudChoice`: use local / use remote / retry / play offline). Sequenced last: it is only *useful* once prospero-win runs games and produces saves, but the sync engine is built and host-tested against Steam independently of the runtime.

macOS reference: `App/CloudModel.swift`, `App/CloudViews.swift`; engine in `Packages/PlaydenKit/Sources/Sources/SteamCloud{Reader,Uploader}.swift`, `CloudZIP.swift`, `Packages/PlaydenKit/Sources/Installs/{CloudSyncPlanner,SaveStore,SteamSaveAccount,SteamSaveMapping}.swift`, `Packages/PlaydenKit/Sources/Catalog/CloudJournal.swift`, `Packages/SteamKit/Sources/SteamCore/PICS.swift` (`parseUFS`), protos `Packages/PlaydenKit/Protos/cloud_{read,write}.proto`.

**Protocol verdict (confirmed).** Playden uses the **modern unified `Cloud.*` service methods over the CM connection** — carried in `kEMsgServiceMethodCallFromClient` (151) with `CMsgProtoBufHeader.target_job_name = "Cloud.<Method>#1"`, responses matched purely on `header.jobid_target` with the eresult read from the header. This is the **exact primitive the PS5 client already uses for `ContentServerDirectory.GetManifestRequestCode#1`** (`cm_messages.h:154`). It is **not** the legacy `ClientUFS*` EMsgs and **not** the Steam Web API; the only HTTP is the signed per-file transfer URL returned inside the responses. So Cloud adds no new CM machinery.

**Already in the PS5 codebase (reuse):** the service-method framing (`kEMsgServiceMethodCallFromClient`/`kEMsgServiceMethodResponse`, `target_job_name`, `jobid_*`), CM logon + the logged-on SteamID, PICS appinfo fetch, **SHA-1** (`cm_client.cpp`, `depot_chunk.cpp`), **zlib inflate + single-entry unzip** (`InflateRaw`, `UnzipFirstEntry`), and a **certificate-validated `HttpClient`** (HTTPS with `cacert.pem`). The genuinely new code is the 7 Cloud messages, an `fnmatch` glob, the UFS/path mapper, and the staging/journal/merge.

### 4A Cloud transport (host) — `src/steam/backend/cloud_client.{h,cpp}`

The 7 `Cloud.*` service methods (protobuf subset of `steammessages_cloud`), each encode/decode like the existing manifest-request-code call:

| `target_job_name` | Purpose |
|---|---|
| `Cloud.GetAppFileChangelist#1` | Full remote file list (send `synced_change_number=0`; reject delta). Fields per file: `file_name`, `sha_file` (SHA-1), `time_stamp`, `raw_file_size`, `persist_state` (present/forgotten/deleted), `platforms_to_sync`, `path_prefix_index`. App `revision = current_change_number`. |
| `Cloud.ClientFileDownload#1` | Signed HTTPS GET URL + headers for one file (reject `encrypted`/explicit-delete). |
| `Cloud.BeginAppUploadBatch#1` | Open batch → `batch_id`, `app_change_number` (must equal `revision+1`). |
| `Cloud.ClientBeginFileUpload#1` | Per-file `block_requests[]` (method + explicit body or offset/length slice). |
| `Cloud.ClientCommitFileUpload#1` | Commit/abort one file. |
| `Cloud.ClientDeleteFile#1` | Delete one remote file in the batch. |
| `Cloud.CompleteAppUploadBatchBlocking#1` | Close the batch (note the **Blocking** variant). |

- **Download** (`CloudZIP`/`SteamCloudReader.download`): GET the signed URL over the existing HTTPS client; if `file_size != raw_file_size` the body is a single-entry ZIP (store or deflate) → reuse `UnzipFirstEntry`+`InflateRaw`; verify `(sha1, raw_file_size)`.
- **Upload** (`SteamCloudBatchWriter`): re-fetch changelist and require `revision` + file-set unchanged; **journal the `batch_id` before any transfer**; per file BeginFileUpload → HTTP block writes → CommitFileUpload; then deletes; then CompleteBlocking; re-verify final `revision == app_change_number`.
- No cloud-level encryption (client sets `can_encrypt=false`). Limits: 64 MiB/file, 512 MiB staging.
- Constants: `machine_name = "Playden"`, `app_build_id = 0`, a persisted random `UInt64` cloud `client_id`, `account_key = SHA256("steam:" + steamid64)`.

### 4B UFS appinfo + path mapping (host) — extend the appinfo parser + `src/steam/backend/cloud_mapping.{h,cpp}`

- Parse the app's `ufs` section from PICS appinfo (add to `app_info.cpp`): `ufs/quota`, `ufs/maxnumfiles`, `ufs/savefiles/<n>/{root,path,pattern,recursive,platforms/<n>}`, `ufs/rootoverrides/<n>/{root,useinstead,addpath,os,oslist,oscompare,pathtransforms/<n>/{find,replace}}`.
- `PathType` root tokens (`%...%`, case-insensitive): `GameInstall`, `WinMyDocuments` (alias `SteamCloudDocuments`), `WinAppDataLocal/LocalLow/Roaming`, `WinSavedGames`, `WinProgramData`, `Root`, `SteamUserData`, etc. Map each to a concrete path inside the installed game and its **prospero-win prefix** (the macOS table maps to the CrossOver bottle; the PS5 table maps to PW's `drive_c/users/<prosperouser>/...` and the GBE `userdata/0/<appid>/remote` root). This mapping is why cloud is sequenced after the runtime defines the prefix layout.
- Remote-name ↔ local-path resolver: remote filename prefix is `%<OriginalRoot>%<uploadPath>` (pre-override values), except `SteamUserData` → bare name; match with `fnmatch` (case-insensitive; `recursive` controls whether `/` is allowed).
- Account tokens `{64BitSteamID}`/`{Steam3AccountID}`: expand the **local** path with the locally stored SteamID and the **cloud** prefix with the logged-on CM SteamID.

**SteamID source (shared with the runtime):** the Goldberg/GBE emulator INI `[user::general] account_steamid=` (under the emulator root or `drive_c/.../Steam/userdata/0/settings/configs.user.ini`), validated as individual/public (`id >> 32 == 0x01100001`), generated if missing. The PS5 runtime's GBE integration and the cloud mapper must read/write the **same** file, or account-scoped games won't map — a coordination point with the prospero-win work.

### 4C Sync engine, conflict model, lifecycle (host + runtime hook)

- **Three-way, content-based merge** (`CloudSyncPlanner`): fingerprint = `(sha1, bytes)`; compare local vs remote vs a persisted `CloudSyncBaseline`. **Timestamps never pick a winner** (shown in the dialog for information only). The baseline is trusted only when the save root's identity still matches (macOS uses `stat` dev+inode+birthtime). Decisions: `unchanged/upload/download/deleteLocal/deleteRemote/conflict/unavailable`.
- **Conflict + consent UI** → the live model's `CloudChoice`: `use local / use remote / use this Steam account (attach) / retry / play offline / close`. Offer local/remote only on a real `conflict`; require explicit account-attach consent before any remote mutation when the attached account key ≠ the remote key (never inferred). Reuse the existing panel/dialog surface.
- **Durable journal + baseline** (SQLite-equivalent via the existing `Storage`): a `CloudSyncOperation` (phases checking→ready→uploading→applyingLocal→verifying→pending/conflict/failed/completed) with a worker claim fence, the batch list for resume, and the baseline. `recoverInterruptedOperations()` on startup.
- **Local staging** (`SaveStore`): immutable snapshot dirs with a manifest + content hashes, atomic publish, apply-with-re-verify. The PS5 version can be lighter but must keep the content-based merge, the attach-consent gate, and the pre-write re-verification.
- **Lifecycle hooks:** download-before-launch (block the launch until resolved or "play offline") and upload-after-exit, wired into the prospero-win handoff; the save files must be synced only when no game process holds them (map the macOS writer-liveness guard to PW process state).

### 4D PS5-specific risks to settle during 4C

- **Root identity**: confirm PW's prefix filesystem exposes a stable inode + `st_birthtim`; if not, degrade safely to "ask on any difference" or use a marker file.
- **Atomic publish / mtime**: `renameat2(RENAME_NOREPLACE)` (or rename + existence check) and `futimens` availability on the target FS.
- **Single CM connection**: Cloud runs on the shared logged-on connection and must not drop it; reuse the existing reconnect/`logonSessionReplaced` handling.

### 4E Tests

- Host: the 7 messages encode/decode and the download/upload flow against a scripted CM transport (as existing CM tests do); ZIP download (store + deflate) + SHA-1/size verify; UFS parsing and name↔path mapping incl. rootoverrides and account-token expansion; `fnmatch`; three-way merge (unchanged / local-only / remote-only / both-changed→conflict / forgotten→unavailable); account-attach gating; journal resume of an interrupted batch.

---

## Order, gates, and file ownership

1. **Install-volume picker** — gated on the 1A M.2-writable spike. New: `VolumeCatalog`; changed: `InstallService`, `Preferences`/`PreferencesStore`, `simulation.{hpp,cpp}`, `view.cpp`, `ps5.cpp`/`host.cpp` wiring.
2. **Notifications** — self-contained in `simulation.{hpp,cpp}`, `controller.{hpp,cpp}`, `view.cpp`.
3. **Log viewer** — new `LogReader`; changed `simulation.{hpp,cpp}`, `controller.{hpp,cpp}`, `view.cpp`; reuses the live-job retry path.
4. **Cloud saves** — new `src/steam/backend/cloud_client.{h,cpp}` and `cloud_mapping.{h,cpp}`, the 7 `Cloud.*` messages in `cm_messages`, a `ufs` parser in `app_info.cpp`, a sync planner + journal + staging, and the live `CloudChoice` dialog; integrates at the prospero-win launch/exit hooks and shares the GBE SteamID file with the runtime.

Each step lands as its own commit(s) with host tests green and, where it touches the console, an evidence folder under `results/`. Steps 1–3 can ship before the runtime is unblocked; step 4 follows once its research is folded in and (for the launch/exit hooks) the runtime runs.

## Scope after these tasks

Still unported afterwards and intentionally deferred: runtime-dependent per-game settings/profiles/display/input (port alongside prospero-win), first-run setup wizard, reset-app-data UI, ambient backdrop, and other-store support.
