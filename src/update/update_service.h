#pragma once
// update_service.h - Finds a newer Playden release on GitHub, downloads and stages it, and swaps
// the app folder when Playden quits. One background worker; the UI reads Snapshot().
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "update/asset_download.h"
#include "update/release.h"
#include "update/version.h"

namespace playden::update {

struct UpdateConfig {
  std::string latest_release_url;               // https://api.github.com/repos/<owner>/<repo>/releases/latest
  std::string releases_page;                    // https://github.com/<owner>/<repo>/releases (release notes links)
  std::string title_id = "FAKE77031";
  std::string installed_root;                   // /data/homebrew/FAKE77031
  std::string work_root;                        // /data/playden/self-update
  std::string kits_root;                        // /data/playden/kits; the kit lives at <kits_root>/<kit_id>
  std::string appmeta_root;                     // /user/appmeta/FAKE77031
  std::string registered_app_root;              // /user/app/FAKE77031
  Version current;
  Fetch fetch;
  std::function<std::optional<std::string>(const std::string& path)> hash_file;
  // The verified kit's manifest SHA-256, or nullopt when the folder is not a complete kit.
  std::function<std::optional<std::string>(const std::string& kit_root)> verify_kit;
  // Runs work with root's effective user id (the title's folders under /data are root-owned).
  std::function<bool(const std::function<bool()>& work)> as_root;
  std::function<std::optional<std::uint64_t>(const std::string& path)> free_bytes;
  std::function<void(const std::string& line)> log;
  std::function<std::int64_t()> now;            // seconds since the epoch
  std::function<void(int attempt)> wait_before_retry;
  std::size_t worker_stack_bytes = 1 << 20;
  // Console testing: when this folder holds release.json, the check reads the release from it and
  // takes playden-update.json and the release's zips from it too, instead of from GitHub.
  std::string local_release_dir;
};

enum class UpdatePhase {
  kIdle,         // nothing known
  kChecking,
  kUpToDate,
  kAvailable,    // a newer release can be installed
  kDownloading,
  kUnpacking,
  kStaged,       // ready: restart Playden to finish
  kFailed,
};

const char* UpdatePhaseName(UpdatePhase phase);

struct UpdateSnapshot {
  std::uint64_t revision = 0;
  UpdatePhase phase = UpdatePhase::kIdle;
  Version current;
  Version available;
  std::string notes;
  std::string notes_url;       // the available release's page
  // The running version's release notes and page, for Settings and the first start after an update.
  std::string current_notes;
  std::string current_notes_url;
  bool whats_new = false;      // the previous run updated Playden: show what's new (cleared with the message)
  std::string part;            // "kit" or "app" while downloading or unpacking
  std::uint64_t done = 0;      // bytes in the current step
  std::uint64_t total = 0;
  std::uint64_t download_bytes = 0;  // what Install will download
  bool kit_needed = false;
  std::string error;           // for kFailed: one short sentence
  bool user_check = false;     // the last check was asked for in Settings
  bool restart_requested = false;
  // The version kept from the last update, which "go back" restores; empty when there is none.
  std::string previous;
  bool rollback_requested = false;
  // The available version is one the player went back from: Settings lists it, no dialog offers it.
  bool quiet = false;
  // One-shot result of the previous run's update ("Playden was updated to 0.3.0."), shown once.
  std::string finished_message;
  bool finished_failed = false;
};

class UpdateService {
 public:
  explicit UpdateService(UpdateConfig config);
  ~UpdateService();
  UpdateService(const UpdateService&) = delete;
  UpdateService& operator=(const UpdateService&) = delete;

  // Reads the saved state: confirms or reports an update applied on the previous quit, and offers
  // an update that is already staged. Call once at startup.
  void Start();
  // Looks for a newer release in the background. Ignored while the worker is busy.
  void Check(bool user_asked);
  // Downloads, unpacks and stages the available release in the background.
  void Install();
  void Cancel();
  // Marks a staged update to be applied when Playden quits.
  void RequestRestart();
  // Marks the previous version (and its kit) to be put back when Playden quits.
  void RequestRollback();
  UpdateSnapshot Snapshot() const;
  // True while the background worker runs.
  bool Busy() const;
  // Clears finished_message (and whats_new) after the UI showed it.
  void AcknowledgeFinished();

  // Stops the worker (cancels a download; the bytes are kept for next time).
  void Shutdown();
  // Last step before the process exits, with every other service stopped: swaps the app folder
  // when a staged update was requested. Returns true when the new version is in place.
  bool ApplyAtQuit();
  // The newest app backup (".../backup/<version>-<time>") and its version, when it is complete.
  struct Backup {
    std::string path;
    Version version;
  };
  std::optional<Backup> LatestBackup() const;

 private:
  struct Shared;
  void Run(std::function<void()> work);
  void DoCheck(bool user_asked);
  void DoInstall();
  void ConfirmApplied(const std::string& state);
  bool RollBackAtQuit();
  void FindPrevious();
  void LoadCurrentNotes();
  std::string PageFor(const std::string& tag) const;
  // The folder holding a checked copy of the release's kit: the installed kit, a kit staged
  // earlier (".next-<kit_id>-<revision>"), or empty when it must be downloaded.
  std::string ReadyKit(const KitPart& kit) const;
  bool StageKit(const Release& release, const UpdateManifest& manifest);
  bool StageApp(const Release& release, const UpdateManifest& manifest);
  void Fail(const std::string& message, const std::string& detail);
  void Set(const std::function<void(UpdateSnapshot&)>& change);
  DownloadOptions Downloads(const std::string& part);
  void Log(const std::string& line) const;
  std::string StatePath() const;
  bool SaveState(const std::string& phase, const std::string& extra = {});
  std::string StagedFields() const;
  bool PrepareWorkRoot();
  bool RemoveOwnedTree(const std::string& path);

  UpdateConfig config_;
  std::shared_ptr<Shared> shared_;
  std::optional<Release> release_;
  std::optional<UpdateManifest> manifest_;
  // The staged update (from this run or a previous one): app folder, version, and the staged kit
  // folder with its kit id (empty when the installed kit is already the release's).
  std::string staged_folder_;
  Version staged_version_;
  std::string staged_kit_;
  std::string staged_kit_id_;
};

}  // namespace playden::update
