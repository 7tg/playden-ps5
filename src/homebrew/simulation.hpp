// simulation.hpp - Renderer-neutral deterministic Playden UI simulation.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "app/preferences.h"
#include "library/organization.h"
#include "library/model.h"

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
enum class InstallRequestKind { Install, Pause, Resume, Cancel, Uninstall };
struct InstallRequest {
    InstallRequestKind kind = InstallRequestKind::Install;
    GameId game_id = 0;
};
// Live install state projected from the real install service.
struct LiveInstallJob {
    GameId game_id = 0;
    JobState state = JobState::Queued;
    std::uint64_t done_bytes = 0;
    std::uint64_t total_bytes = 0;
    std::string failure;
};
struct LiveInstalledGame {
    GameId game_id = 0;
    std::uint64_t install_bytes = 0;
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
};

struct Profile {
    std::string id;
    std::string name;
    std::string summary;
    std::string resolution = "1920x1080";
    std::string frame_limit = "60";
    bool vsync = true;
    std::string launch_arguments;
    bool is_default = false;
    bool save_failed = false;
};

struct ModalEntry {
    std::string id;
    std::string title;
    std::string opener_focus_key;
    std::vector<std::string> action_keys;
    int focused_action = 0;
    bool destructive = false;
    std::string body;  // explanatory text; empty uses the simulation's default wording
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
    int remaining_ticks = 0;
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
    std::vector<Profile> profiles;
    Navigation navigation;
    EditorState editor;
    std::vector<Notice> notices;
    std::map<GameId, std::string> selected_profiles;
    std::map<GameId, Profile> custom_profiles;
    SessionState session = SessionState::Idle;
    RuntimeState runtime = RuntimeState::Unavailable;
    LogState log_state = LogState::Ready;
    std::vector<std::string> log_lines;
    int log_offset = 0;
    std::string log_context;
    GameId session_game_id = 0;
    std::string session_error;
    bool reduced_motion = false;
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
    std::string selected_profile_id_;
    std::string editing_target_id_;
    std::uint64_t challenge_generation_ = 0;
    std::uint64_t cancelled_challenge_generation_ = 0;
    std::uint64_t next_collection_serial_ = 1;
    std::uint64_t next_job_serial_ = 1;
    std::vector<ServiceAction> pending_service_actions_;
    std::vector<InstallRequest> pending_install_requests_;
    bool library_catalog_received_ = false;

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
    void change_route(int delta);
    void open_modal(ModalEntry modal);
    void close_modal();
    void start_editor(std::string purpose, std::string value);
    void commit_editor();
    void advance_jobs();
    void start_logs();
    void finish_logs();
    void queue_service_action(ServiceAction action);
    void clear_account_specific_state();
    void apply_organization_snapshot(const playden::OrganizationSnapshot& snapshot);
    void reconcile_applied_organization_selection(GameId prior_selected_id);
    bool record_live_persistence();
    void update_persistence_notice();
    [[nodiscard]] bool live() const noexcept { return mode_ == ModelMode::Live; }
    [[nodiscard]] std::string next_job_id();
};

} // namespace playden::homebrew
