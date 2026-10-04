// simulation.hpp - Renderer-neutral deterministic Playden UI simulation.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace playden::homebrew {

using GameId = std::uint32_t;

enum class Route { Startup, SignIn, Home, Library, Downloads, Settings, Details, Session };
enum class AuthState { SignedOut, Waiting, Expired, Failed, Cancelled, Syncing, SignedIn, Offline };
enum class JobState { Queued, Downloading, Pausing, Paused, Verifying, Downloaded, Failed, Cancelled, Interrupted };
enum class JobStage { Download, Verify, Install, Uninstall };
enum class SessionState { Idle, Preparing, Launching, Running, Stopping, Failed, Crashed };
enum class RuntimeState { Unavailable, Checking, Ready, Failed };
enum class LogState { Loading, Ready, Failed };
enum class Compatibility { Untested, Works, Playable, Broken };
enum class RowKind { Heading, Action, Toggle, Choice, Text, Game, Job, Volume, Profile, Status, Separator };

struct Account {
    std::string id;
    std::string display_name;
    AuthState authentication = AuthState::SignedOut;
    std::int64_t challenge_expiry = 0;
    std::int64_t last_sync = 0;
    bool cache_available = false;
    std::string error;
};

struct Game {
    GameId id = 0;
    std::string title;
    std::string description;
    std::string source;
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
    Simulation(std::filesystem::path fixture_directory, std::filesystem::path profile_path);

    bool scenario(const std::string& name);
    bool action(const std::string& logical_action);
    void controller_sample(ControllerState sample);
    void text(const std::string& utf8);
    void tick(int ticks = 1);

    [[nodiscard]] const State& state() const noexcept { return state_; }
    [[nodiscard]] const ViewData& view() const noexcept { return view_; }
    [[nodiscard]] const ViewData& route_view() const noexcept { return route_view_; }
    [[nodiscard]] std::vector<const Game*> visible_games() const;
    [[nodiscard]] std::string snapshot() const;
    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }

private:
    std::filesystem::path fixture_directory_;
    std::filesystem::path profile_path_;
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
    [[nodiscard]] std::string next_job_id();
};

} // namespace playden::homebrew
