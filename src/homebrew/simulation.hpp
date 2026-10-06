// simulation.hpp - Renderer-neutral deterministic Playden UI simulation.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "app/preferences.h"
#include "library/organization.h"
#include "library/model.h"
#include "runtime/game_settings.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace playden::homebrew {

using GameId = playden::GameId;

enum class Route { Startup, SignIn, Home, Library, Downloads, Settings, Details, Session };
enum class ModelMode { Simulation, Live };
enum class ServiceAction { BeginQr, CancelQr, SignOut, SwitchAccount, RefreshLibrary };
enum class AuthState { SignedOut, Restoring, Waiting, Expired, Failed, Cancelled, Syncing, SigningOut, SignedIn, Offline };
enum class JobState { Queued, Downloading, Pausing, Paused, Verifying, Downloaded, Failed, Cancelled, Interrupted };
enum class JobStage { Download, Verify, Install, Uninstall };
enum class SessionState { Idle, Preparing, Launching, Running, Stopping, Failed, Crashed };
enum class RuntimeState { Unavailable, Checking, Ready, Failed };
enum class LogState { Loading, Ready, Failed };
enum class InstallRequestKind { Install, Pause, Resume, Cancel, Uninstall, SetDefaultVolume };
struct InstallRequest {
    InstallRequestKind kind = InstallRequestKind::Install;
    GameId game_id = 0;
    std::string volume_id;  // SetDefaultVolume only
};
// Live install state projected from the real install service.
struct LiveInstallJob {
    GameId game_id = 0;
    JobState state = JobState::Queued;
    std::uint64_t done_bytes = 0;
    std::uint64_t total_bytes = 0;
    std::string failure;
    std::string volume_id;
};
struct LiveInstalledGame {
    GameId game_id = 0;
    std::uint64_t install_bytes = 0;
    std::string volume_id;
    bool available = true;  // false while the game's drive is absent
};
// A drive the real install service can use. Space is absent when the platform cannot report it.
struct LiveVolume {
    std::string id;
    std::string label;
    bool available = true;
    std::optional<std::uint64_t> total_bytes;
    std::optional<std::uint64_t> free_bytes;
};
// Live per-game runtime (private bottle and PS5 title) state, projected from the runtime host.
struct LiveRuntimeGame {
    GameId game_id = 0;
    std::string status;   // short value, e.g. "Not prepared", "Preparing", "Ready"
    std::string detail;   // why, or what happens next
    bool can_prepare = false;
    bool ready = false;
    // Play: whether it can start now, and the current or last launch.
    bool can_play = false;
    std::string play_status;  // e.g. "Starting…", "Playing"; empty when idle
    std::string play_detail;  // the last launch's outcome or refusal
};
// Live Steam Cloud save state for one game, projected from the cloud service.
enum class CloudStatus { None, Checking, Syncing, Synced, UpToDate, Conflict, NeedsAccount, Unsupported, Failed };
struct LiveCloudGame {
    GameId game_id = 0;
    CloudStatus status = CloudStatus::None;
    std::string message;
    std::size_t local_files = 0;
    std::size_t remote_files = 0;
    std::uint64_t local_bytes = 0;
    std::uint64_t remote_bytes = 0;
    std::int64_t local_latest = 0;
    std::int64_t remote_latest = 0;
};
enum class CloudRequestKind { Sync, UseLocal, UseCloud, ConfirmAccount };
struct CloudRequest {
    CloudRequestKind kind = CloudRequestKind::Sync;
    GameId game_id = 0;
};
enum class Compatibility { Untested, Works, Playable, Broken };
enum class RowKind { Heading, Action, Toggle, Choice, Text, Game, Job, Volume, Profile, Status, Separator };

struct Account {
    std::string id;
    std::string display_name;
    std::string challenge_url;
    AuthState authentication = AuthState::SignedOut;
    std::int64_t challenge_expiry = 0;
    std::int64_t last_sync = 0;
    bool cache_available = false;
    std::string error;
};

struct AccountUpdate {
    AuthState authentication = AuthState::SignedOut;
    std::string id;
    std::string display_name;
    std::string challenge_url;
    std::string error;
    bool clear_library = false;
};

struct LibraryUpdate {
    std::vector<playden::GameSummary> games;
    bool loading = false;
    std::string error;
    bool stale = false;
    bool cache_available = false;
    std::optional<std::int64_t> retrieved_at;
};

struct PersistenceLoadToken {
    std::uint64_t id = 0;
    std::uint64_t baseline_revision = 0;
};

inline bool operator==(PersistenceLoadToken left, PersistenceLoadToken right) {
    return left.id == right.id && left.baseline_revision == right.baseline_revision;
}

struct Game {
    GameId id = 0;
    std::string title;
    std::string description;
    std::string source;
    std::string artwork_source;
    std::string folded_title; // Derived once for production query/sort; never persisted.
    std::string cover_key;
    std::string hero_key;
    std::string logo_key;
    std::vector<std::string> genres;
    std::string controller_support;
    std::optional<std::uint64_t> download_bytes;
    std::optional<std::uint64_t> install_bytes;
    std::optional<std::int64_t> added_at;
    std::optional<std::int64_t> last_played;
    std::optional<std::uint64_t> playtime_minutes;
    std::optional<bool> windows_installable;  // from Steam app info; absent until fetched
    bool art_pending = false;
    bool art_failed = false;
};

struct Collection {
    std::string id;
    std::string name;
    bool pinned = false;
    std::set<GameId> members;
};

struct UserLibrary {
    std::set<GameId> favorites;
    std::set<GameId> hidden;
    std::vector<Collection> collections;
    std::map<GameId, Compatibility> ratings;
    std::map<GameId, std::string> notes;
    std::string query;
    std::string scope = "all";
    std::string sort = "name";
    std::set<std::string> filters;
};

struct Installation {
    GameId game_id = 0;
    std::string volume_id;
    std::uint64_t installed_bytes = 0;
    bool available = true;
    bool runtime_ready = true;
    bool saves_present = true;
};

struct Job {
    std::string id;
    GameId game_id = 0;
    std::string kind = "install";
    int queue_order = 0;
    JobStage stage = JobStage::Download;
    JobState state = JobState::Queued;
    std::uint64_t completed_bytes = 0;
    std::uint64_t total_bytes = 0;
    std::optional<std::uint64_t> bytes_per_second;
    std::optional<int> eta_seconds;
    std::string failure;
    JobStage retry_stage = JobStage::Download;
    std::string volume_id;
    int stage_ticks = 0;
    std::uint64_t reserved_bytes = 0;
    bool reservation_held = false;
    bool completion_applied = false;
};

struct Volume {
    std::string id;
    std::string label;
    std::uint64_t capacity_bytes = 0;
    std::uint64_t used_bytes = 0;
    std::uint64_t reserved_bytes = 0;
    bool available = true;
    bool writable = true;
    std::uint64_t base_used_bytes = 0;
    std::uint64_t base_reserved_bytes = 0;
    bool capacity_known = true;    // live volumes on the console cannot report capacity
    std::uint64_t games_bytes = 0; // live: bytes of finished installs on this volume
};

struct ModalEntry {
    std::string id;
    std::string title;
    std::string opener_focus_key;
    std::vector<std::string> action_keys;
    int focused_action = 0;
    bool destructive = false;
    std::string body;  // explanatory text; empty uses the simulation's default wording
    std::string setting_id;  // Game settings picker: the catalogue setting being chosen
};

struct Navigation {
    Route route = Route::Startup;
    Route detail_origin = Route::Library;
    std::optional<GameId> detail_id;
    std::vector<ModalEntry> modals;
    std::string focus_key = "startup-connect";
    std::map<std::string, int> scroll_offsets;
    GameId selected_game_id = 0;
    int selected_index = 0;
    int home_shelf = 0;
    std::map<std::string, int> home_shelf_focus;
    bool library_rail_focused = false;
    bool settings_rail_focused = false;
    int library_grid_column = 0;
};

struct EditorState {
    bool active = false;
    std::string purpose;
    std::string value;
    std::string original;
    std::string opener_focus_key;
    std::size_t caret = 0;
    bool shifted = false;
    bool symbols = false;
    int focus_row = 0;
    int focus_key = 0;
};

struct Notice {
    std::string id;
    std::string message;
    std::string action;
    bool persistent = false;
    int remaining_ticks = 0;  // simulation ticks; live mode counts wall-clock seconds while visible
    bool failure = false;     // drawn with the failure tone
    std::string detail;       // second line; when empty the action key is shown, as fixtures expect
};

struct ControllerState {
    float left_x = 0.0F;
    float left_y = 0.0F;
    float right_x = 0.0F;
    float right_y = 0.0F;
    float l2 = 0.0F;
    float r2 = 0.0F;
    bool cross = false;
    bool circle = false;
    bool square = false;
    bool triangle = false;
    bool l1 = false;
    bool r1 = false;
    bool options = false;
};

struct State {
    Account account;
    std::uint64_t account_generation = 1; // Invalidates account-scoped presentation resources.
    std::vector<Game> games;
    UserLibrary library;
    std::vector<Installation> installations;
    std::vector<Job> jobs;
    std::vector<Volume> volumes;
    std::string default_volume_id;  // live: where new installs go
    std::map<GameId, LiveCloudGame> cloud;  // live: Steam Cloud save state by game
    std::map<GameId, LiveRuntimeGame> runtime_games;  // live: per-game runtime state
    std::size_t runtime_leftover_requests = 0;          // live: unused settings requests to clean up
    Navigation navigation;
    EditorState editor;
    std::vector<Notice> notices;
    // Per-game settings: preset and player overrides (see runtime/game_settings.h).
    std::map<GameId, playden::runtime::GameSettingsRecord> game_settings;
    SessionState session = SessionState::Idle;
    RuntimeState runtime = RuntimeState::Unavailable;
    LogState log_state = LogState::Ready;
    std::vector<std::string> log_lines;
    int log_offset = 0;
    std::string log_context;
    GameId session_game_id = 0;
    std::string session_error;
    bool reduced_motion = false;
    bool title_store_suffix = false;  // PS5 titles named "<game> (Steam)"
    bool controller_connected = true;
    ControllerState controller;
    bool persistence_available = true;
    bool library_syncing = false;
    bool startup_loading = false;
    bool library_loading = false;
    bool library_offline = false;
    bool screenshot_mode = false;
    bool quit_requested = false;
    bool simulation_mode = true;
    std::int64_t clock_ticks = 0;
};

struct ViewRow {
    std::string key;
    RowKind kind = RowKind::Action;
    std::string title;
    std::string subtitle;
    std::string value;
    std::string badge;
    std::string action;
    GameId game_id = 0;
    float progress = 0.0F;
    bool enabled = true;
    bool selected = false;
    bool dangerous = false;
    bool modified = false;  // Game settings: the player changed this value
    std::string section;  // Settings: the rail section the row belongs to
    bool rail_only = false;  // Settings: the section's rail entry is the action (Quit Playden), no card
};

struct ViewData {
    std::string surface;
    std::string title;
    std::string subtitle;
    std::vector<ViewRow> rows;
    std::vector<ViewRow> actions;
    struct Shelf {
        std::string key;
        std::string title;
        std::vector<ViewRow> items;
        int focused_item = -1;
    };
    std::vector<Shelf> shelves;
    std::vector<std::string> legends;
    int focused_row = 0;
    int focused_action = 0;
    std::size_t total_count = 0;
    std::size_t first_index = 0;
    bool show_simulation_label = true;
};

class Simulation {
public:
    Simulation(std::filesystem::path fixture_directory, std::filesystem::path profile_path,
               ModelMode mode = ModelMode::Simulation);

    bool scenario(const std::string& name);
    bool action(const std::string& logical_action);
    void controller_sample(ControllerState sample);
    void text(const std::string& utf8);
    void tick(int ticks = 1);
    void SetControllerConnected(bool connected);
    void SetWallClock(std::int64_t unix_seconds);
    void UpdateAccount(AccountUpdate update);
    bool UpdateLibrary(LibraryUpdate update);
    // Live mode: requests for the real install service, and its state for display.
    [[nodiscard]] std::vector<InstallRequest> TakeInstallRequests();
    void UpdateInstalls(const std::vector<LiveInstallJob>& jobs, const std::vector<LiveInstalledGame>& installed);
    // Live mode: Steam Cloud save requests for the cloud service, and its state for display.
    [[nodiscard]] std::vector<CloudRequest> TakeCloudRequests();
    void UpdateCloud(const std::vector<LiveCloudGame>& games);
    // Live mode: games to prepare (bottle + PS5 title), and the runtime state for display.
    [[nodiscard]] std::vector<GameId> TakeRuntimePrepareRequests();
    // Queues Prepare without a button press: after a download, or before Play.
    void RequestRuntimePrepare(GameId id);
    [[nodiscard]] std::vector<GameId> TakeRuntimePlayRequests();
    [[nodiscard]] bool TakeRuntimeCleanupRequest();
    void SetRuntimeLeftovers(std::size_t count);
    void UpdateRuntime(RuntimeState runtime, const std::vector<LiveRuntimeGame>& games);
    // Live mode: the folder holding install.log, auth.log and launch.log for the log viewer.
    void SetLogDirectory(std::filesystem::path directory);
    // Live mode: the drives installs can use and the one new installs go to.
    void UpdateVolumes(const std::vector<LiveVolume>& volumes, const std::string& default_volume_id);
    // Live mode: Steam app-info estimates for one game (download/install size, installability).
    bool UpdateAppDetails(GameId id, std::optional<std::uint64_t> download_bytes,
                          std::optional<std::uint64_t> install_bytes, std::optional<bool> windows_installable);
    PersistenceLoadToken BeginOrganizationLoad(std::uint64_t account);
    bool ApplyOrganization(std::uint64_t account, PersistenceLoadToken token,
                           std::optional<playden::OrganizationSnapshot> loaded);
    bool FailOrganizationLoad(std::uint64_t account, PersistenceLoadToken token,
                              playden::StorageError error);
    void OrganizationCleared(std::uint64_t account);
    PersistenceLoadToken BeginPreferencesLoad();
    bool ApplyPreferences(PersistenceLoadToken token,
                          const playden::PreferencesLoadResult& loaded);
    [[nodiscard]] playden::OrganizationSnapshot ExportOrganization() const;
    [[nodiscard]] playden::Preferences ExportPreferences() const;
    [[nodiscard]] std::uint64_t OrganizationAccount() const noexcept { return organization_account_; }
    [[nodiscard]] std::uint64_t OrganizationRevision() const noexcept { return organization_revision_; }
    [[nodiscard]] std::uint64_t PreferencesRevision() const noexcept { return preferences_revision_; }
    [[nodiscard]] bool OrganizationDirty() const noexcept;
    [[nodiscard]] bool PreferencesDirty() const noexcept;
    [[nodiscard]] bool OrganizationReadyForSave() const noexcept;
    void OrganizationSaveResult(std::uint64_t account, std::uint64_t revision,
                                playden::StorageError error);
    void PreferencesSaveResult(std::uint64_t revision, playden::StorageError error);
    bool TakePersistenceRetryRequest();
    [[nodiscard]] std::vector<ServiceAction> TakeServiceActions();
    // Presets and recipes for the Game settings sheet (runtime/presets.json, runtime/recipes/).
    void SetSettingsData(std::vector<playden::runtime::SettingsPreset> presets,
                         std::vector<playden::runtime::GameRecipe> recipes);
    void SetKitCapabilities(playden::runtime::KitCapabilities kit);
    // Live mode: where per-game settings are kept (loaded now, saved on every change).
    void SetGameSettingsFile(std::filesystem::path path);
    // The recipe and effective settings the sheet shows for a game.
    [[nodiscard]] const playden::runtime::GameRecipe& RecipeFor(GameId id) const;
    [[nodiscard]] bool EffectiveSettingsFor(GameId id, playden::runtime::EffectiveSettings* settings) const;

    [[nodiscard]] const State& state() const noexcept { return state_; }
    [[nodiscard]] const ViewData& view() const noexcept { return view_; }
    [[nodiscard]] const ViewData& route_view() const noexcept { return route_view_; }
    [[nodiscard]] std::vector<const Game*> visible_games() const;
    [[nodiscard]] std::string snapshot() const;
    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

private:
    std::filesystem::path fixture_directory_;
    std::filesystem::path profile_path_;
    ModelMode mode_ = ModelMode::Simulation;
    State state_;
    ViewData view_;
    ViewData route_view_;
    std::string last_error_;
    std::string pending_destructive_action_;
    std::string selected_volume_id_;
    std::vector<playden::runtime::SettingsPreset> settings_presets_;
    std::map<std::uint32_t, playden::runtime::GameRecipe> recipes_;
    mutable std::map<GameId, playden::runtime::GameRecipe> simulated_recipes_;
    playden::runtime::KitCapabilities kit_;
    std::set<std::string> expanded_settings_sections_;
    std::string editing_setting_id_;
    std::filesystem::path game_settings_file_;
    std::map<GameId, playden::runtime::GameSettingsRecord> saved_game_settings_;
    bool settings_save_failed_ = false;
    bool settings_load_failed_ = false;
    std::optional<playden::runtime::GameSettingsRecord> settings_opened_record_;  // for the close toast
    std::string editing_target_id_;
    std::uint64_t challenge_generation_ = 0;
    std::uint64_t cancelled_challenge_generation_ = 0;
    std::uint64_t next_collection_serial_ = 1;
    std::uint64_t next_job_serial_ = 1;
    std::vector<ServiceAction> pending_service_actions_;
    std::vector<InstallRequest> pending_install_requests_;
    std::vector<GameId> pending_runtime_prepare_requests_;
    std::vector<GameId> pending_runtime_play_requests_;
    bool pending_runtime_cleanup_ = false;
    bool library_catalog_received_ = false;
    // Live install events become notifications; the first snapshot is restored history, not an event.
    bool live_installs_seen_ = false;
    std::map<GameId, JobState> live_job_states_;
    std::set<GameId> live_installed_ids_;
    std::set<GameId> pending_uninstalls_;
    std::filesystem::path log_directory_;
    std::vector<CloudRequest> pending_cloud_requests_;
    std::string live_log_topic_;
    GameId live_log_game_ = 0;
    bool details_primary_pending_ = false;  // live Details: move focus to the first action once known

    struct OrganizationJournalEntry {
        playden::OrganizationSnapshot before;
        playden::OrganizationSnapshot after;
        bool reset = false;
        bool selected = false;
        std::size_t bytes = 0;
    };
    std::uint64_t next_persistence_load_token_ = 0;
    std::uint64_t organization_account_ = 0;
    std::uint64_t organization_revision_ = 0;
    std::uint64_t organization_saved_revision_ = 0;
    std::uint64_t preferences_revision_ = 0;
    std::uint64_t preferences_saved_revision_ = 0;
    PersistenceLoadToken organization_load_token_;
    PersistenceLoadToken preferences_load_token_;
    bool organization_load_pending_ = false;
    bool organization_load_failed_ = false;
    bool organization_save_failed_ = false;
    bool organization_journal_full_ = false;
    bool organization_save_suppressed_ = false;
    bool preferences_load_pending_ = false;
    bool preferences_load_may_apply_ = false;
    bool preferences_load_failed_ = false;
    bool preferences_save_failed_ = false;
    bool persistence_retry_requested_ = false;
    bool force_organization_reset_ = false;
    bool force_organization_selection_ = false;
    bool force_preferences_reset_ = false;
    std::size_t organization_journal_bytes_ = 0;
    std::vector<OrganizationJournalEntry> organization_journal_;
    playden::OrganizationSnapshot captured_organization_;
    playden::OrganizationSnapshot persisted_organization_;
    playden::Preferences captured_preferences_;

    void load_fixtures();
    void load_profile();
    bool save_profile();
    void reset_default_state();
    void rebuild_view();
    void activate();
    void cancel();
    void move(int delta_x, int delta_y);
    bool move_settings(int delta_x, int delta_y);
    void change_route(int delta);
    void open_modal(ModalEntry modal);
    void close_modal();
    void start_editor(std::string purpose, std::string value);
    void commit_editor();
    void advance_jobs();
    [[nodiscard]] std::vector<std::string> game_settings_actions() const;
    void refresh_game_settings();
    bool can_change_game_settings();
    void set_game_setting(const std::string& id, const std::string& value);
    void announce_settings_saved();
    [[nodiscard]] const playden::runtime::SettingsPreset* preset_for(GameId id) const;
    void start_logs();
    void finish_logs();
    void queue_service_action(ServiceAction action);
    void clear_account_specific_state();
    void apply_organization_snapshot(const playden::OrganizationSnapshot& snapshot);
    void reconcile_applied_organization_selection(GameId prior_selected_id);
    bool record_live_persistence();
    void update_persistence_notice();
    void apply_live_volume_usage();
    void push_notice(Notice notice);
    void open_live_log(const std::string& topic, GameId game_id);
    void load_live_log();
    [[nodiscard]] bool live_notices_visible() const noexcept;
    [[nodiscard]] bool live() const noexcept { return mode_ == ModelMode::Live; }
    [[nodiscard]] std::string next_job_id();
};

} // namespace playden::homebrew
