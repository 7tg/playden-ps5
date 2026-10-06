#include "library/query.h"
// simulation.cpp - Deterministic state transitions for the Playden UI comparison.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT

#include "homebrew/simulation.hpp"

#include "app/diagnostic_log.h"
#include "homebrew/browse.hpp"
#include "homebrew/json.hpp"
#include "homebrew/storage_sim.hpp"
#include "steam/backend/json.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <string_view>
#include <utility>
#include <type_traits>

namespace playden::homebrew {
namespace {

constexpr std::uint64_t kTransferPerTick = 67'108'864;
constexpr std::int64_t kFixtureClock = 1'760'000'000;
constexpr int kLiveNoticeSeconds = 6;  // how long a live notification stays on screen
constexpr std::size_t kOrganizationJournalEntries = 256;
constexpr std::size_t kOrganizationJournalBytes = 256 * 1024;

struct StringBuilder {
    std::string value;
    StringBuilder& operator<<(std::string_view text) { value.append(text); return *this; }
    StringBuilder& operator<<(const std::string& text) { value.append(text); return *this; }
    StringBuilder& operator<<(const char* text) { value.append(text); return *this; }
    StringBuilder& operator<<(char character) { value.push_back(character); return *this; }
    template <typename Integer>
        requires std::is_integral_v<Integer>
    StringBuilder& operator<<(Integer number) { value.append(std::to_string(number)); return *this; }
};

std::string read_file(const std::filesystem::path& path) {
    std::FILE* input = std::fopen(path.string().c_str(), "rb");
    if (!input) return {};
    std::string result;
    char buffer[4096];
    while (const auto count = std::fread(buffer, 1, sizeof(buffer), input)) result.append(buffer, count);
    std::fclose(input);
    return result;
}

std::string string_value(const json::Value* value, std::string fallback = {}) {
    if (value) if (const auto* text = value->string()) return *text;
    return fallback;
}

std::uint64_t uint_value(const json::Value* value, std::uint64_t fallback = 0) {
    if (value) if (const auto number = value->number()) return static_cast<std::uint64_t>(*number);
    return fallback;
}

bool bool_value(const json::Value* value, bool fallback = false) {
    if (value) if (const auto boolean = value->boolean()) return *boolean;
    return fallback;
}

std::optional<std::uint64_t> optional_uint(const json::Value* value) {
    if (!value) return std::nullopt;
    if (const auto number = value->number()) return static_cast<std::uint64_t>(*number);
    return std::nullopt;
}

std::string route_name(Route route) {
    switch (route) {
    case Route::Startup: return "startup";
    case Route::SignIn: return "qr";
    case Route::Home: return "home";
    case Route::Library: return "library";
    case Route::Downloads: return "downloads";
    case Route::Settings: return "settings";
    case Route::Details: return "details";
    case Route::Session: return "session";
    }
    return "home";
}

std::string auth_name(AuthState state) {
    switch (state) {
    case AuthState::SignedOut: return "signed-out";
    case AuthState::Restoring: return "restoring";
    case AuthState::Waiting: return "waiting";
    case AuthState::Expired: return "expired";
    case AuthState::Failed: return "failed";
    case AuthState::Cancelled: return "cancelled";
    case AuthState::Syncing: return "syncing";
    case AuthState::SigningOut: return "signing-out";
    case AuthState::SignedIn: return "signed-in";
    case AuthState::Offline: return "offline";
    }
    return "signed-out";
}

AuthState parse_auth_state(const std::string& state) {
    if (state == "waiting") return AuthState::Waiting;
    if (state == "expired") return AuthState::Expired;
    if (state == "failed") return AuthState::Failed;
    if (state == "cancelled") return AuthState::Cancelled;
    if (state == "syncing") return AuthState::Syncing;
    if (state == "signed-in") return AuthState::SignedIn;
    if (state == "offline") return AuthState::Offline;
    return AuthState::SignedOut;
}

std::string format_bytes(std::uint64_t bytes) {
    if (bytes == 0) return {};
    char text[32];
    if (bytes >= 1000ull * 1000 * 1000) std::snprintf(text, sizeof(text), "%.1f GB", static_cast<double>(bytes) / 1e9);
    else std::snprintf(text, sizeof(text), "%.0f MB", std::max(1.0, static_cast<double>(bytes) / 1e6));
    return text;
}

std::string job_state_name(JobState state) {
    switch (state) {
    case JobState::Queued: return "queued";
    case JobState::Downloading: return "downloading";
    case JobState::Pausing: return "pausing";
    case JobState::Paused: return "paused";
    case JobState::Verifying: return "verifying";
    case JobState::Downloaded: return "downloaded";
    case JobState::Failed: return "failed";
    case JobState::Cancelled: return "cancelled";
    case JobState::Interrupted: return "interrupted";
    }
    return "queued";
}

std::string job_stage_name(JobStage stage) {
    switch (stage) {
    case JobStage::Download: return "download";
    case JobStage::Verify: return "verify";
    case JobStage::Install: return "install";
    case JobStage::Uninstall: return "uninstall";
    }
    return "download";
}

JobState parse_job_state(const std::string& state) {
    if (state == "downloading") return JobState::Downloading;
    if (state == "pausing") return JobState::Pausing;
    if (state == "paused") return JobState::Paused;
    if (state == "verifying") return JobState::Verifying;
    if (state == "downloaded" || state == "completed") return JobState::Downloaded;
    if (state == "failed") return JobState::Failed;
    if (state == "cancelled") return JobState::Cancelled;
    if (state == "interrupted") return JobState::Interrupted;
    return JobState::Queued;
}

JobStage parse_job_stage(const std::string& stage) {
    if (stage == "verify" || stage == "verifying") return JobStage::Verify;
    if (stage == "install") return JobStage::Install;
    if (stage == "uninstall") return JobStage::Uninstall;
    return JobStage::Download;
}

const Game* find_game(const State& state, GameId id) {
    const auto found = std::find_if(state.games.begin(), state.games.end(),
                                    [id](const Game& game) { return game.id == id; });
    return found == state.games.end() ? nullptr : &*found;
}

Installation* find_installation(State& state, GameId id) {
    const auto found = std::find_if(state.installations.begin(), state.installations.end(),
                                    [id](const Installation& installation) { return installation.game_id == id; });
    return found == state.installations.end() ? nullptr : &*found;
}

bool is_installed(const State& state, GameId id) {
    const auto found = std::find_if(state.installations.begin(), state.installations.end(),
                                    [id](const Installation& installation) { return installation.game_id == id; });
    return found != state.installations.end() && found->installed_bytes > 0;
}

int next_queue_order(const State& state) {
    int order = -1;
    for (const auto& job : state.jobs) order = std::max(order, job.queue_order);
    return order + 1;
}

Job* find_job(State& state, const std::string& id) {
    const auto found = std::find_if(state.jobs.begin(), state.jobs.end(),
                                    [&id](const Job& job) { return job.id == id; });
    return found == state.jobs.end() ? nullptr : &*found;
}

Volume* find_volume(State& state, const std::string& id) {
    const auto found = std::find_if(state.volumes.begin(), state.volumes.end(),
                                    [&id](const Volume& volume) { return volume.id == id; });
    return found == state.volumes.end() ? nullptr : &*found;
}

std::string game_key(GameId id);

void clear_profile_customizations(State& state) { state.game_settings.clear(); }

namespace rt = playden::runtime;

std::string choice_label(const rt::SettingDefinition& definition, const std::string& value) {
    for (const auto& choice : definition.choices)
        if (choice.value == value) return choice.label;
    return value;
}

// Bindings offered for a controller button; index 0 inherits the recipe or preset value.
const std::vector<std::pair<std::string, std::string>>& common_bindings() {
    static const std::vector<std::pair<std::string, std::string>> bindings = {
        {"", "Inherit"}, {"none", "Nothing"}, {"escape", "Escape"}, {"enter", "Enter"}, {"space", "Space"},
        {"tab", "Tab"}, {"shift", "Shift"}, {"ctrl", "Ctrl"}, {"alt", "Alt"}, {"e", "E"}, {"f", "F"}, {"q", "Q"},
        {"r", "R"}, {"1", "1"}, {"2", "2"}, {"3", "3"}, {"4", "4"}, {"f5", "F5 (quick save)"}, {"f9", "F9 (quick load)"},
        {"mouse_left", "Left click"}, {"mouse_right", "Right click"}, {"mouse_middle", "Middle click"}};
    return bindings;
}

std::string binding_label(const std::string& value) {
    if (value.empty()) return "Not set";
    for (const auto& [binding, label] : common_bindings())
        if (binding == value) return label;
    return value;
}

const char* source_label(rt::ValueSource source) {
    switch (source) {
    case rt::ValueSource::kRecipe: return "From the game";
    case rt::ValueSource::kPreset: return "From preset";
    case rt::ValueSource::kUser: return "Your choice";
    default: return "Playden default";
    }
}

void reconcile_library_selection(State& state) {
    if (state.navigation.library_rail_focused) return;
    const auto games = filtered_games(state);
    const auto current = std::find_if(games.begin(), games.end(), [&state](const Game* game) {
        return game->id == state.navigation.selected_game_id;
    });
    if (current != games.end()) {
        state.navigation.selected_index = static_cast<int>(std::distance(games.begin(), current));
        state.navigation.focus_key = game_key(state.navigation.selected_game_id);
    } else if (!games.empty()) {
        state.navigation.selected_index = std::clamp(state.navigation.selected_index, 0, static_cast<int>(games.size()) - 1);
        state.navigation.selected_game_id = games[static_cast<std::size_t>(state.navigation.selected_index)]->id;
        state.navigation.focus_key = game_key(state.navigation.selected_game_id);
    } else {
        state.navigation.selected_index = 0;
        state.navigation.selected_game_id = 0;
        state.navigation.focus_key = state.library.query.empty() ? "library-empty" : "clear-search";
    }
    state.navigation.scroll_offsets["library-grid"] = state.navigation.selected_index / 6;
}

std::string game_key(GameId id) { return "game-" + std::to_string(id); }

std::vector<std::string> context_actions(bool live) {
    if (live) return {"favorite", "collection", "note", "hide", "uninstall", "cancel"};
    return {"favorite", "collection", "note", "hide", "verify", "uninstall", "logs", "cancel"};
}

// The name kept game data is shown with: the game's name, else the library's, else its app id.
std::string kept_name(const State& state, const LiveKeptBottle& kept) {
    if (!kept.name.empty()) return kept.name;
    if (const auto* game = find_game(state, kept.game_id); game && !game->title.empty()) return game->title;
    return "Steam app " + std::to_string(kept.game_id);
}

std::string kept_count(std::size_t count) {
    return std::to_string(count) + (count == 1 ? " game" : " games");
}

std::string action_label(const std::string& key) {
    static const std::map<std::string, std::string> labels = {
        {"cancel", "Cancel"}, {"confirm-destructive", "Confirm"}, {"ready-later", "Later"}, {"quit-to-home", "Play"}, {"favorite", "Favorite"},
        {"collection", "Collections"}, {"note", "Compatibility and note"}, {"verify", "Verify files"},
        {"uninstall", "Uninstall"}, {"uninstall-delete-saves", "Delete saves too"}, {"logs", "Open logs"}, {"logs-back", "Back"},
        {"logs-pick", "Logs"}, {"log-install", "Install log"}, {"log-auth", "Sign-in log"},
        {"log-launch", "Launch log"}, {"log-cloud", "Cloud saves log"}, {"logs-refresh", "Refresh"},
        {"cloud-sync", "Cloud saves"}, {"cloud-choose", "Cloud saves"}, {"cloud-use-cloud", "Use Cloud saves"},
        {"cloud-use-local", "Use console saves"}, {"cloud-confirm-account", "Use this account"},
        {"install-confirm", "Install"}, {"storage-picker", "Change storage"},
        {"preset-picker", "Preset"}, {"reset-settings", "Reset all to preset"}, {"clear-shader-cache", "Clear shader cache"},
        {"section-more", "More settings"}, {"section-advanced", "Advanced"},
        {"sort-name", "Name"}, {"sort-recent-played", "Recently played"}, {"sort-recent-added", "Recently added"},
        {"sort-playtime", "Playtime"}, {"sort-install-size", "Install size"}, {"filter-installed", "Installed"},
        {"filter-genre-adventure", "Adventure"}, {"filter-controller-full", "Full controller support"},
        {"filter-compat-playable", "Playable"}, {"filters-reset", "Reset filters"}, {"advance-clock", "Advance fixture clock"},
        {"inject-download-failed", "Inject download failure"}, {"inject-verification-failed", "Inject verification failure"},
        {"inject-uninstall-failed", "Inject uninstall failure"},
        {"inject-storage-disconnect", "Disconnect storage"}, {"inject-controller-disconnect", "Disconnect controller"},
        {"inject-launch-failure", "Inject launch failure"}, {"inject-crash", "Inject session crash"},
        {"runtime-retry", "Retry runtime check"}, {"inject-qr-approved", "Approve current challenge"},
        {"inject-qr-expired", "Expire current challenge"}, {"inject-qr-failure", "Inject network failure"},
        {"inject-qr-denied", "Deny current challenge"}, {"logs-retry", "Retry loading logs"},
        {"inject-logs-empty", "Inject empty logs"}, {"inject-logs-failure", "Inject log failure"},
        {"inject-logs-ready", "Inject ready logs"}, {"account-auth", "Authentication"},
        {"account-last-sync", "Last library sync"}, {"account-sync", "Sync now"},
        {"account-recover", "Reconnect account"}, {"account-switch", "Switch account"},
        {"about-build", "Build"}, {"about-framework", "Framework"}, {"about-platform", "Platform"},
        {"about-back", "Back"}, {"inject-library-refresh", "Refresh library fixture"},
        {"inject-startup-loading", "Show startup loading"}};
    if (const auto found = labels.find(key); found != labels.end()) return found->second;
    // Per-job actions carry the job id after the verb.
    for (const auto& [prefix, label] : std::initializer_list<std::pair<const char*, const char*>>{
             {"job-pause-", "Pause download"}, {"job-resume-", "Resume download"}, {"job-retry-", "Retry download"},
             {"job-cancel-", "Cancel download"}, {"job-up-", "Move up"}, {"job-down-", "Move down"},
             {"job-open-", "Open game"}, {"job-dismiss-", "Dismiss"}})
        if (key.rfind(prefix, 0) == 0) return label;
    std::string title = key;
    std::replace(title.begin(), title.end(), '-', ' ');
    if (!title.empty()) title.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(title.front())));
    return title;
}

std::string trimmed(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::size_t snapshot_size(const playden::OrganizationSnapshot& value) {
    std::size_t bytes = sizeof(value) + value.query.size() + value.scope.size() + value.sort.size();
    bytes += (value.favorites.size() + value.hidden.size()) * sizeof(GameId);
    bytes += value.ratings.size() * (sizeof(GameId) + sizeof(playden::CompatibilityRating));
    for (const auto& [id, note] : value.notes) bytes += sizeof(id) + note.size();
    for (const auto& filter : value.filters) bytes += filter.size();
    for (const auto& collection : value.collections)
        bytes += collection.id.size() + collection.name.size() + collection.members.size() * sizeof(GameId);
    return bytes;
}

bool snapshot_within_model_bounds(const playden::OrganizationSnapshot& value) {
    return playden::OrganizationSnapshotIsValid(value);
}

template <typename Value>
void replay_set_delta(std::set<Value>& base, const std::set<Value>& before,
                      const std::set<Value>& after) {
    for (const auto& value : before) if (!after.contains(value)) base.erase(value);
    for (const auto& value : after) if (!before.contains(value)) base.insert(value);
}

template <typename Key, typename Value>
void replay_map_delta(std::map<Key, Value>& base, const std::map<Key, Value>& before,
                      const std::map<Key, Value>& after) {
    for (const auto& [key, value] : before) {
        (void)value;
        if (!after.contains(key)) base.erase(key);
    }
    for (const auto& [key, value] : after) {
        const auto old = before.find(key);
        if (old == before.end() || old->second != value) base[key] = value;
    }
}

const playden::OrganizationCollection* find_collection(
    const std::vector<playden::OrganizationCollection>& collections, std::string_view id) {
    const auto found = std::find_if(collections.begin(), collections.end(), [id](const auto& value) {
        return value.id == id;
    });
    return found == collections.end() ? nullptr : &*found;
}

std::string free_collection_id(const playden::OrganizationSnapshot& base,
                               const playden::OrganizationSnapshot& local) {
    for (std::uint64_t serial = 1;; ++serial) {
        const auto candidate = "collection-" + std::to_string(serial);
        if (!find_collection(base.collections, candidate) &&
            !find_collection(local.collections, candidate)) return candidate;
    }
}

void replay_organization_delta(playden::OrganizationSnapshot& base,
                               const playden::OrganizationSnapshot& before,
                               const playden::OrganizationSnapshot& after,
                               bool reset, bool selected) {
    if (reset) {
        base = after;
        return;
    }
    replay_set_delta(base.favorites, before.favorites, after.favorites);
    replay_set_delta(base.hidden, before.hidden, after.hidden);
    replay_map_delta(base.ratings, before.ratings, after.ratings);
    replay_map_delta(base.notes, before.notes, after.notes);
    replay_set_delta(base.filters, before.filters, after.filters);

    for (const auto& old : before.collections) {
        if (find_collection(after.collections, old.id)) continue;
        base.collections.erase(std::remove_if(base.collections.begin(), base.collections.end(),
                                              [&old](const auto& value) { return value.id == old.id; }),
                               base.collections.end());
    }
    for (const auto& local : after.collections) {
        const auto* prior = find_collection(before.collections, local.id);
        auto current = std::find_if(base.collections.begin(), base.collections.end(),
                                    [&local](const auto& value) { return value.id == local.id; });
        if (!prior) {
            if (current != base.collections.end()) {
                const auto replacement = free_collection_id(base, after);
                if (base.scope == current->id) base.scope = replacement;
                current->id = replacement;
            }
            base.collections.push_back(local);
        } else if (*prior != local) {
            if (current == base.collections.end()) base.collections.push_back(local);
            else *current = local;
        }
    }
    if (before.query != after.query) base.query = after.query;
    if (before.scope != after.scope) base.scope = after.scope;
    if (before.sort != after.sort) base.sort = after.sort;
    if (selected || before.selected_game_id != after.selected_game_id)
        base.selected_game_id = after.selected_game_id;
}

} // namespace

Simulation::Simulation(std::filesystem::path fixture_directory, std::filesystem::path profile_path, ModelMode mode)
    : fixture_directory_(std::move(fixture_directory)), profile_path_(std::move(profile_path)), mode_(mode) {
    reset_default_state();
    if (live()) {
        rebuild_view();
        return;
    }
    load_fixtures();
    scenario("default");
    load_profile();
    rebuild_view();
}

void Simulation::reset_default_state() {
    state_ = {};
    state_.simulation_mode = !live();
    state_.clock_ticks = live() ? 0 : kFixtureClock;
    state_.navigation.scroll_offsets["library-grid"] = 0;
    state_.navigation.scroll_offsets["home"] = 0;
    state_.navigation.scroll_offsets["details"] = 0;
    state_.navigation.scroll_offsets["details-content"] = 0;
    selected_volume_id_ = "external";
    editing_setting_id_.clear();
    next_job_serial_ = 1;
    last_error_.clear();
    captured_organization_ = {};
    persisted_organization_ = {};
    captured_preferences_ = {};
}

void Simulation::SetControllerConnected(bool connected) {
    const bool changed = state_.controller_connected != connected;
    state_.controller_connected = connected;
    if (live() && changed) {
        if (!connected) {
            push_notice({"controller", "Controller disconnected", "", true, 0, true, "Reconnect it to keep playing"});
        } else {
            state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(),
                                                [](const Notice& notice) { return notice.id == "controller"; }),
                                 state_.notices.end());
            push_notice({"controller-connected", "Controller connected", "", false, kLiveNoticeSeconds, false, ""});
        }
    }
    rebuild_view();
}

void Simulation::SetWallClock(std::int64_t unix_seconds) {
    if (!live() || state_.clock_ticks == unix_seconds) return;
    // Notifications only count down while they can be seen, so none expires behind a dialog.
    const auto elapsed = state_.clock_ticks == 0 ? 0 : std::clamp<std::int64_t>(unix_seconds - state_.clock_ticks, 0, 60);
    state_.clock_ticks = unix_seconds;
    if (elapsed > 0 && live_notices_visible()) {
        for (auto& notice : state_.notices)
            if (!notice.persistent) notice.remaining_ticks -= static_cast<int>(elapsed);
        state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(),
                                            [](const Notice& notice) { return !notice.persistent && notice.remaining_ticks <= 0; }),
                             state_.notices.end());
    }
    rebuild_view();
}

void Simulation::push_notice(Notice notice) {
    state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(),
                                        [&notice](const Notice& existing) { return existing.id == notice.id; }),
                         state_.notices.end());
    state_.notices.push_back(std::move(notice));
    // Keep at most a few transient notifications; the oldest give way.
    constexpr std::size_t kMaxTransient = 8;
    std::size_t transient = 0;
    for (const auto& existing : state_.notices) transient += existing.persistent ? 0 : 1;
    for (auto it = state_.notices.begin(); transient > kMaxTransient && it != state_.notices.end();) {
        if (!it->persistent) { it = state_.notices.erase(it); --transient; }
        else ++it;
    }
}

bool Simulation::live_notices_visible() const noexcept {
    return state_.navigation.modals.empty() && !state_.editor.active &&
           state_.navigation.route != Route::Startup && state_.navigation.route != Route::SignIn;
}

std::vector<ServiceAction> Simulation::TakeServiceActions() {
    auto actions = std::move(pending_service_actions_);
    pending_service_actions_.clear();
    return actions;
}

std::vector<InstallRequest> Simulation::TakeInstallRequests() {
    auto requests = std::move(pending_install_requests_);
    pending_install_requests_.clear();
    return requests;
}

std::vector<CloudRequest> Simulation::TakeCloudRequests() {
    auto requests = std::move(pending_cloud_requests_);
    pending_cloud_requests_.clear();
    return requests;
}

std::vector<GameId> Simulation::TakeRuntimePrepareRequests() {
    auto requests = std::move(pending_runtime_prepare_requests_);
    pending_runtime_prepare_requests_.clear();
    return requests;
}

void Simulation::UpdateKeptBottles(std::vector<LiveKeptBottle> kept) {
    state_.kept_bottles = std::move(kept);
    std::erase_if(kept_selected_, [this](GameId id) {
        return std::none_of(state_.kept_bottles.begin(), state_.kept_bottles.end(), [id](const LiveKeptBottle& item) { return item.game_id == id; });
    });
    // An open manager lists what is kept now; with nothing left it closes.
    auto& modals = state_.navigation.modals;
    for (std::size_t index = 0; index < modals.size(); ++index) {
        auto& modal = modals[index];
        if (modal.id != "kept-data") continue;
        if (state_.kept_bottles.empty()) {
            if (index + 1 == modals.size()) close_modal();
            break;
        }
        const auto focused = modal.action_keys[static_cast<std::size_t>(modal.focused_action)];
        modal.action_keys = kept_manager_actions();
        const auto found = std::find(modal.action_keys.begin(), modal.action_keys.end(), focused);
        modal.focused_action = found == modal.action_keys.end()
            ? std::min(modal.focused_action, static_cast<int>(modal.action_keys.size()) - 1)
            : static_cast<int>(found - modal.action_keys.begin());
        if (index + 1 == modals.size()) state_.navigation.focus_key = modal.action_keys[static_cast<std::size_t>(modal.focused_action)];
    }
    rebuild_view();
}

std::vector<std::string> Simulation::kept_manager_actions() const {
    std::vector<std::string> actions{"kept-select-all"};
    for (const auto& kept : state_.kept_bottles) actions.push_back("kept-toggle-" + std::to_string(kept.game_id));
    actions.push_back("kept-selection-delete");
    actions.push_back("cancel");
    return actions;
}

std::vector<GameId> Simulation::TakeKeptDeleteRequests() {
    auto requests = std::move(pending_kept_delete_requests_);
    pending_kept_delete_requests_.clear();
    return requests;
}

void Simulation::NotifyUninstallStopped(GameId id, const std::string& reason) {
    const auto* game = find_game(state_, id);
    push_notice({"uninstall-" + std::to_string(id), "Uninstall stopped", "", false, kLiveNoticeSeconds * 2, true,
                 (game ? game->title : std::string("A game")) + " · " + reason});
    pending_uninstalls_.erase(id);
    rebuild_view();
}

void Simulation::RequestRuntimePrepare(GameId id) {
    if (id != 0) pending_runtime_prepare_requests_.push_back(id);
}

std::vector<GameId> Simulation::TakeRuntimePlayRequests() {
    auto requests = std::move(pending_runtime_play_requests_);
    pending_runtime_play_requests_.clear();
    return requests;
}

bool Simulation::TakeRuntimeCleanupRequest() {
    const bool requested = pending_runtime_cleanup_;
    pending_runtime_cleanup_ = false;
    return requested;
}

void Simulation::SetRuntimeLeftovers(std::size_t count) {
    if (!live() || state_.runtime_leftover_requests == count) return;
    state_.runtime_leftover_requests = count;
    rebuild_view();
}

void Simulation::UpdateRuntime(RuntimeState runtime, const std::vector<LiveRuntimeGame>& games) {
    if (!live()) return;
    state_.runtime = runtime;
    std::map<GameId, LiveRuntimeGame> next;
    for (const auto& game : games) next[game.game_id] = game;
    state_.runtime_games = std::move(next);
    for (auto& installation : state_.installations) {
        const auto found = state_.runtime_games.find(installation.game_id);
        installation.runtime_ready = found != state_.runtime_games.end() && found->second.ready;
    }
    show_ready_prompt();
    rebuild_view();
}

void Simulation::NotifyReadyToPlay(GameId id) {
    if (!live()) return;
    if (std::find(ready_prompts_.begin(), ready_prompts_.end(), id) == ready_prompts_.end()) ready_prompts_.push_back(id);
    show_ready_prompt();
    rebuild_view();
}

// "<game> is ready to play": a dialog, because notifications take no input. It waits
// until no other dialog, sheet or keyboard is open, and is dropped once the game no
// longer waits for the PS5. Play quits to the Home screen without asking again.
void Simulation::show_ready_prompt() {
    if (!live() || ready_prompts_.empty()) return;
    std::erase_if(ready_prompts_, [this](GameId id) {
        const auto found = state_.runtime_games.find(id);
        return found == state_.runtime_games.end() || !found->second.awaiting_registration;
    });
    const auto route = state_.navigation.route;
    if (ready_prompts_.empty() || !state_.navigation.modals.empty() || state_.editor.active ||
        route == Route::Startup || route == Route::SignIn || route == Route::Session)
        return;
    const auto id = ready_prompts_.front();
    ready_prompts_.erase(ready_prompts_.begin());
    const auto* game = find_game(state_, id);
    ModalEntry prompt{"confirmation", (game ? game->title : std::string("Your game")) + " is ready to play",
                      state_.navigation.focus_key, {"ready-later", "quit-to-home"}, 1, false};
    prompt.body = "Play quits Playden to the PS5 Home screen, where the game appears. Start it from there.";
    open_modal(std::move(prompt));
}

void Simulation::UpdateCloud(const std::vector<LiveCloudGame>& games) {
    if (!live()) return;
    for (const auto& game : games) {
        const auto previous = state_.cloud.find(game.game_id);
        const auto before = previous == state_.cloud.end() ? CloudStatus::None : previous->second.status;
        if (before != game.status) {
            const auto* known = find_game(state_, game.game_id);
            const std::string title = known ? known->title : "A game";
            const auto id = "cloud-" + std::to_string(game.game_id);
            if (game.status == CloudStatus::Synced)
                push_notice({id, "Cloud saves synced", "", false, kLiveNoticeSeconds, false, title + " · " + game.message});
            else if (game.status == CloudStatus::Failed)
                push_notice({id, "Cloud sync failed", "", false, kLiveNoticeSeconds * 2, true, title + " · " + game.message});
            else if (game.status == CloudStatus::Conflict || game.status == CloudStatus::NeedsAccount)
                push_notice({id, "Cloud saves need a choice", "", false, kLiveNoticeSeconds * 2, false, title + " · open its details"});
        }
        state_.cloud[game.game_id] = game;
    }
    rebuild_view();
}

void Simulation::UpdateInstalls(const std::vector<LiveInstallJob>& jobs, const std::vector<LiveInstalledGame>& installed) {
    if (!live()) return;
    std::map<GameId, JobState> job_states;
    for (const auto& job : jobs) job_states[job.game_id] = job.state;
    std::set<GameId> installed_ids;
    for (const auto& game : installed) installed_ids.insert(game.game_id);
    if (live_installs_seen_) {
        const auto title = [this](GameId id) {
            const auto* game = find_game(state_, id);
            return game ? game->title : std::string("A game");
        };
        for (const auto& [id, previous] : live_job_states_) {
            if (job_states.contains(id) || !installed_ids.contains(id)) continue;
            push_notice({"install-" + std::to_string(id), "Download complete", "", false, kLiveNoticeSeconds, false, title(id)});
        }
        for (const auto& job : jobs) {
            const auto previous = live_job_states_.find(job.game_id);
            if (job.state != JobState::Failed || (previous != live_job_states_.end() && previous->second == JobState::Failed)) continue;
            push_notice({"install-" + std::to_string(job.game_id), "Download failed", "", false, kLiveNoticeSeconds * 2, true,
                         title(job.game_id) + (job.failure.empty() ? std::string() : " · " + job.failure)});
        }
        for (const auto id : live_installed_ids_) {
            if (installed_ids.contains(id) || job_states.contains(id) || !pending_uninstalls_.contains(id)) continue;
            push_notice({"install-" + std::to_string(id), "Game uninstalled", "", false, kLiveNoticeSeconds, false, title(id)});
        }
    }
    for (auto it = pending_uninstalls_.begin(); it != pending_uninstalls_.end();)
        it = installed_ids.contains(*it) ? std::next(it) : pending_uninstalls_.erase(it);
    live_installs_seen_ = true;
    live_job_states_ = std::move(job_states);
    live_installed_ids_ = std::move(installed_ids);
    std::vector<Job> projected;
    int order = 0;
    for (const auto& live_job : jobs) {
        Job job;
        job.id = std::to_string(live_job.game_id);
        job.game_id = live_job.game_id;
        job.queue_order = order++;
        job.state = live_job.state;
        job.completed_bytes = live_job.done_bytes;
        job.total_bytes = live_job.total_bytes;
        job.failure = live_job.failure;
        job.volume_id = live_job.volume_id;
        projected.push_back(std::move(job));
    }
    std::vector<Installation> installations;
    for (const auto& game : installed)
        installations.push_back({game.game_id, game.volume_id, game.install_bytes, game.available,
                                 state_.runtime_games.count(game.game_id) && state_.runtime_games.at(game.game_id).ready, false});
    state_.jobs = std::move(projected);
    state_.installations = std::move(installations);
    apply_live_volume_usage();
    rebuild_view();
}

void Simulation::UpdateVolumes(const std::vector<LiveVolume>& volumes, const std::string& default_volume_id) {
    if (!live()) return;
    std::vector<Volume> projected;
    for (const auto& live_volume : volumes) {
        Volume volume;
        volume.id = live_volume.id;
        volume.label = live_volume.label;
        volume.available = live_volume.available;
        volume.writable = live_volume.available;
        volume.capacity_known = live_volume.total_bytes.has_value() && live_volume.free_bytes.has_value();
        if (volume.capacity_known) {
            volume.capacity_bytes = *live_volume.total_bytes;
            volume.base_used_bytes = *live_volume.total_bytes - std::min(*live_volume.free_bytes, *live_volume.total_bytes);
        }
        projected.push_back(std::move(volume));
    }
    state_.volumes = std::move(projected);
    selected_volume_id_ = default_volume_id;
    state_.default_volume_id = default_volume_id;
    apply_live_volume_usage();
    rebuild_view();
}

// Live volumes: used space is what the drive reports (or, without a report, the finished installs
// on it); reserved space is what queued and running downloads still need.
void Simulation::apply_live_volume_usage() {
    for (auto& volume : state_.volumes) {
        volume.games_bytes = 0;
        volume.reserved_bytes = 0;
        for (const auto& installation : state_.installations)
            if (installation.volume_id == volume.id) volume.games_bytes += installation.installed_bytes;
        for (const auto& job : state_.jobs)
            if (job.volume_id == volume.id && job.total_bytes > job.completed_bytes)
                volume.reserved_bytes += job.total_bytes - job.completed_bytes;
        volume.used_bytes = volume.capacity_known ? volume.base_used_bytes : volume.games_bytes;
        if (volume.capacity_known) volume.reserved_bytes = std::min(volume.reserved_bytes, volume.capacity_bytes - std::min(volume.used_bytes, volume.capacity_bytes));
    }
}

void Simulation::queue_service_action(ServiceAction action) {
    constexpr std::size_t kMaxPendingActions = 32;
    const bool barrier = action == ServiceAction::SignOut || action == ServiceAction::SwitchAccount;
    if (barrier && std::find(pending_service_actions_.begin(), pending_service_actions_.end(), action) != pending_service_actions_.end()) return;
    if (pending_service_actions_.size() >= kMaxPendingActions) {
        if (!barrier) return;
        const auto disposable = std::find_if(pending_service_actions_.begin(), pending_service_actions_.end(), [](ServiceAction pending) {
            return pending != ServiceAction::SignOut && pending != ServiceAction::SwitchAccount;
        });
        if (disposable == pending_service_actions_.end()) return;
        pending_service_actions_.erase(disposable);
    }
    pending_service_actions_.push_back(action);
}

void Simulation::clear_account_specific_state() {
    ++state_.account_generation;
    state_.account.challenge_url.clear();
    state_.account.challenge_expiry = 0;
    state_.account.last_sync = 0;
    state_.account.cache_available = false;
    state_.account.error.clear();
    state_.games.clear();
    library_catalog_received_ = false;
    state_.library = {};
    state_.installations.clear();
    state_.jobs.clear();
    state_.volumes.clear();
    state_.cloud.clear();
    // Bottle settings belong to the installed game, independently of Steam sign-in.
    if (!live()) state_.game_settings.clear();
    state_.navigation.detail_id.reset();
    state_.navigation.selected_game_id = 0;
    state_.navigation.selected_index = 0;
    state_.navigation.home_shelf = 0;
    state_.navigation.home_shelf_focus.clear();
    state_.navigation.scroll_offsets.clear();
    state_.navigation.modals.clear();
    state_.editor = {};
    state_.session = SessionState::Idle;
    state_.session_game_id = 0;
    state_.session_error.clear();
    state_.runtime = RuntimeState::Unavailable;
    state_.library_syncing = false;
    state_.library_loading = false;
    state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(),
                                        [](const Notice& notice) { return notice.id != "persistence"; }),
                         state_.notices.end());
    state_.log_state = LogState::Ready;
    state_.log_lines.clear();
    state_.log_offset = 0;
    state_.log_context.clear();
    pending_destructive_action_.clear();
    selected_volume_id_.clear();
    editing_setting_id_.clear();
    editing_target_id_.clear();
    organization_account_ = 0;
    organization_revision_ = 0;
    organization_saved_revision_ = 0;
    organization_load_token_ = {};
    organization_load_pending_ = false;
    organization_load_failed_ = false;
    organization_save_failed_ = false;
    organization_journal_full_ = false;
    organization_save_suppressed_ = false;
    organization_journal_.clear();
    organization_journal_bytes_ = 0;
    captured_organization_ = {};
    persisted_organization_ = {};
    update_persistence_notice();
}

void Simulation::UpdateAccount(AccountUpdate update) {
    if (!live()) return;
    if (update.clear_library) clear_account_specific_state();
    state_.account.authentication = update.authentication;
    state_.account.id = std::move(update.id);
    state_.account.display_name = std::move(update.display_name);
    state_.account.challenge_url = std::move(update.challenge_url);
    state_.account.error = std::move(update.error);
    state_.account.challenge_expiry = 0;
    state_.library_syncing = update.authentication == AuthState::Syncing;

    if (update.authentication == AuthState::Waiting || update.authentication == AuthState::Expired || update.authentication == AuthState::Failed) {
        state_.navigation.route = Route::SignIn;
        state_.navigation.focus_key = update.authentication == AuthState::Expired ? "qr-refresh"
            : update.authentication == AuthState::Failed ? "qr-retry" : "qr-cancel";
    } else if (update.authentication == AuthState::SignedIn || update.authentication == AuthState::Offline) {
        if (state_.navigation.route == Route::Startup || state_.navigation.route == Route::SignIn) state_.navigation.route = Route::Home;
        state_.navigation.focus_key = state_.games.empty() ? "home-library" : game_key(state_.games.front().id);
    } else if (update.authentication == AuthState::SignedOut || update.authentication == AuthState::Cancelled) {
        state_.navigation.route = Route::Startup;
        state_.navigation.focus_key = "startup-connect";
    } else if (update.authentication == AuthState::Restoring) {
        state_.navigation.route = Route::Startup;
        state_.navigation.focus_key = "startup-loading";
    }
    rebuild_view();
}

bool Simulation::UpdateAppDetails(GameId id, std::optional<std::uint64_t> download_bytes,
                                  std::optional<std::uint64_t> install_bytes,
                                  std::optional<bool> windows_installable) {
    if (!live()) return false;
    const auto game = std::find_if(state_.games.begin(), state_.games.end(),
                                   [id](const Game& candidate) { return candidate.id == id; });
    if (game == state_.games.end()) return false;
    game->download_bytes = download_bytes;
    game->install_bytes = install_bytes;
    game->windows_installable = windows_installable;
    rebuild_view();
    return true;
}

void Simulation::UpdateStoreDetails(GameId id, std::string description, std::vector<std::string> genres,
                                    std::string controller_support) {
    if (!live()) return;
    auto& facts = store_facts_[id];
    facts = {std::move(description), std::move(genres), std::move(controller_support)};
    for (auto& game : state_.games) {
        if (game.id != id) continue;
        if (!facts.description.empty()) game.description = facts.description;
        game.genres = facts.genres;
        game.controller_support = facts.controller_support;
    }
    rebuild_view();
}

bool Simulation::UpdateLibrary(LibraryUpdate update) {
    if (!live()) return false;

    std::set<GameId> identifiers;
    for (const auto& summary : update.games) {
        if (summary.id == 0 || trimmed(summary.title).empty()
            || !identifiers.insert(summary.id).second
            || (summary.last_played_unix_seconds
                && *summary.last_played_unix_seconds
                    > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            || (summary.added_at_unix_seconds
                && *summary.added_at_unix_seconds
                    > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))) {
            return false;
        }
    }
    if (update.retrieved_at && *update.retrieved_at < 0) return false;

    if (!update.loading && update.error.empty()) {
        library_catalog_received_ = true;
        const auto prior_visible = visible_games();
        const GameId prior_id = state_.navigation.selected_game_id;
        const auto prior_detail_id = state_.navigation.detail_id;
        const std::string prior_focus = state_.navigation.focus_key;
        const bool prior_overlay = state_.editor.active || !state_.navigation.modals.empty();
        const bool prior_catalog_empty = state_.games.empty();
        int prior_index = state_.navigation.selected_index;
        const auto prior_selected = std::find_if(prior_visible.begin(), prior_visible.end(), [prior_id](const Game* game) {
            return game->id == prior_id;
        });
        if (prior_selected != prior_visible.end())
            prior_index = static_cast<int>(std::distance(prior_visible.begin(), prior_selected));

        std::vector<Game> projected;
        projected.reserve(update.games.size());
        for (auto& summary : update.games) {
            Game game;
            game.id = summary.id;
            game.title = std::move(summary.title);
            game.folded_title = FoldLibraryText(game.title);
            game.description = std::move(summary.description);
            game.source = "Steam";
            game.artwork_source = std::move(summary.artwork_source);
            game.playtime_minutes = summary.playtime_minutes;
            if (summary.last_played_unix_seconds)
                game.last_played = static_cast<std::int64_t>(*summary.last_played_unix_seconds);
            if (summary.added_at_unix_seconds)
                game.added_at = static_cast<std::int64_t>(*summary.added_at_unix_seconds);
            if (const auto facts = store_facts_.find(game.id); facts != store_facts_.end()) {
                if (!facts->second.description.empty()) game.description = facts->second.description;
                game.genres = facts->second.genres;
                game.controller_support = facts->second.controller_support;
            }
            projected.push_back(std::move(game));
        }
        state_.games = std::move(projected);

        const auto current_visible = visible_games();
        const auto retained = std::find_if(current_visible.begin(), current_visible.end(), [prior_id](const Game* game) {
            return game->id == prior_id;
        });
        bool identity_retained = false;
        if (retained != current_visible.end()) {
            identity_retained = true;
            state_.navigation.selected_game_id = prior_id;
            state_.navigation.selected_index = static_cast<int>(std::distance(current_visible.begin(), retained));
        } else if (prior_catalog_empty && prior_id != 0 && find_game(state_, prior_id)) {
            identity_retained = true;
            state_.navigation.selected_game_id = prior_id;
            state_.navigation.selected_index = 0;
        } else if (!current_visible.empty()) {
            state_.navigation.selected_index = std::clamp(prior_index, 0, static_cast<int>(current_visible.size()) - 1);
            state_.navigation.selected_game_id = current_visible[static_cast<std::size_t>(state_.navigation.selected_index)]->id;
            if (!state_.navigation.modals.empty()) state_.navigation.modals.clear();
            state_.editor.active = false;
        } else {
            state_.navigation.selected_index = 0;
            state_.navigation.selected_game_id = 0;
            state_.navigation.modals.clear();
            state_.editor.active = false;
        }

        if (state_.navigation.route == Route::Details && prior_detail_id) {
            const auto retained_detail = std::find_if(state_.games.begin(), state_.games.end(), [prior_detail_id](const Game& game) {
                return game.id == *prior_detail_id;
            });
            if (retained_detail != state_.games.end()) {
                state_.navigation.detail_id = *prior_detail_id;
                state_.navigation.selected_game_id = *prior_detail_id;
                const auto visible_detail = std::find_if(current_visible.begin(), current_visible.end(), [prior_detail_id](const Game* game) {
                    return game->id == *prior_detail_id;
                });
                if (visible_detail != current_visible.end())
                    state_.navigation.selected_index = static_cast<int>(std::distance(current_visible.begin(), visible_detail));
                identity_retained = true;
            } else {
                state_.navigation.detail_id = state_.navigation.selected_game_id == 0
                    ? std::nullopt : std::optional<GameId>{state_.navigation.selected_game_id};
                identity_retained = false;
            }
        }

        if (state_.navigation.route == Route::Details && state_.navigation.selected_game_id == 0) {
            state_.navigation.route = state_.navigation.detail_origin == Route::Home
                ? Route::Home : Route::Library;
            state_.navigation.detail_id.reset();
        }

        if (prior_overlay && identity_retained) {
            state_.navigation.focus_key = prior_focus;
        } else if (state_.navigation.route == Route::Library && state_.navigation.library_rail_focused) {
            state_.navigation.focus_key = prior_focus;
        } else if (state_.navigation.route == Route::Home || state_.navigation.route == Route::Library) {
            state_.navigation.focus_key = state_.navigation.selected_game_id == 0
                ? (state_.library.query.empty() ? "library-empty" : "clear-search")
                : game_key(state_.navigation.selected_game_id);
        } else if (state_.navigation.route == Route::Details) {
            if (identity_retained) state_.navigation.focus_key = prior_focus;
            else {
                details_primary_pending_ = live();
                state_.navigation.focus_key = live() ? "details-favorite"
                                                     : (is_installed(state_, state_.navigation.selected_game_id)
                                                            ? "details-play" : "details-install");
            }
        } else {
            state_.navigation.focus_key = prior_focus;
        }

        const auto projected_shelves = shelves(state_);
        for (std::size_t shelf_index = 0; shelf_index < projected_shelves.size(); ++shelf_index) {
            const auto& shelf = projected_shelves[shelf_index];
            const auto selected = std::find(shelf.game_ids.begin(), shelf.game_ids.end(), state_.navigation.selected_game_id);
            if (selected == shelf.game_ids.end()) continue;
            state_.navigation.home_shelf = static_cast<int>(shelf_index);
            state_.navigation.home_shelf_focus[shelf.key] = static_cast<int>(std::distance(shelf.game_ids.begin(), selected));
            break;
        }
    }

    state_.library_loading = update.loading;
    state_.library_syncing = update.loading;
    state_.library_offline = update.stale;
    state_.account.cache_available = update.cache_available;
    state_.account.error = std::move(update.error);
    if (update.retrieved_at) state_.account.last_sync = *update.retrieved_at;
    captured_organization_.selected_game_id = state_.navigation.selected_game_id;
    rebuild_view();
    return true;
}

playden::OrganizationSnapshot Simulation::ExportOrganization() const {
    playden::OrganizationSnapshot result;
    result.favorites = state_.library.favorites;
    result.hidden = state_.library.hidden;
    result.query = state_.library.query;
    result.scope = state_.library.scope;
    result.sort = state_.library.sort;
    result.filters = state_.library.filters;
    result.selected_game_id = state_.navigation.selected_game_id;
    result.collections.reserve(state_.library.collections.size());
    for (const auto& value : state_.library.collections)
        result.collections.push_back({value.id, value.name, value.pinned, value.members});
    for (const auto& [id, rating] : state_.library.ratings)
        result.ratings[id] = static_cast<playden::CompatibilityRating>(rating);
    result.notes = state_.library.notes;
    return result;
}

playden::Preferences Simulation::ExportPreferences() const {
    return {state_.reduced_motion, state_.title_store_suffix};
}

void Simulation::apply_organization_snapshot(const playden::OrganizationSnapshot& snapshot) {
    const auto prior_selected_id = state_.navigation.selected_game_id;
    state_.library.favorites = snapshot.favorites;
    state_.library.hidden = snapshot.hidden;
    state_.library.query = snapshot.query;
    state_.library.scope = snapshot.scope;
    state_.library.sort = snapshot.sort;
    state_.library.filters = snapshot.filters;
    state_.library.collections.clear();
    for (const auto& value : snapshot.collections)
        state_.library.collections.push_back({value.id, value.name, value.pinned, value.members});
    state_.library.ratings.clear();
    for (const auto& [id, rating] : snapshot.ratings)
        state_.library.ratings[id] = static_cast<Compatibility>(rating);
    state_.library.notes = snapshot.notes;
    state_.navigation.selected_game_id = snapshot.selected_game_id;
    reconcile_applied_organization_selection(prior_selected_id);
}

void Simulation::reconcile_applied_organization_selection(GameId prior_selected_id) {
    if (state_.games.empty()) {
        if (!library_catalog_received_) return;
        state_.navigation.selected_game_id = 0;
        state_.navigation.selected_index = 0;
        if (state_.navigation.route == Route::Home) state_.navigation.focus_key = "home-library";
        else if (state_.navigation.route == Route::Library && !state_.navigation.library_rail_focused)
            state_.navigation.focus_key = state_.library.query.empty() ? "library-empty" : "clear-search";
        return;
    }
    if (state_.navigation.route == Route::Details &&
        state_.navigation.detail_id == state_.navigation.selected_game_id &&
        find_game(state_, state_.navigation.selected_game_id)) return;

    const auto visible = visible_games();
    const auto visible_candidate = [&](GameId wanted) -> std::vector<const Game*>::const_iterator {
        auto found = std::find_if(visible.begin(), visible.end(), [wanted](const Game* game) {
            return game->id == wanted;
        });
        if (found != visible.end()) return found;
        found = std::find_if(visible.begin(), visible.end(), [prior_selected_id](const Game* game) {
            return game->id == prior_selected_id;
        });
        if (found != visible.end() || visible.empty()) return found;
        return visible.begin() + std::clamp(state_.navigation.selected_index, 0,
                                             static_cast<int>(visible.size()) - 1);
    };
    const auto library_item = visible_candidate(state_.navigation.selected_game_id);
    const GameId library_candidate = library_item == visible.end() ? 0 : (*library_item)->id;
    if (library_item != visible.end()) {
        const int index = static_cast<int>(std::distance(visible.begin(), library_item));
        state_.navigation.selected_index = index;
        state_.navigation.library_grid_column = index % 6;
        state_.navigation.scroll_offsets["library-grid"] = index / 6;
    }

    GameId home_candidate = 0;
    std::string home_more_key;  // the focused "More" cell, kept across library updates
    const auto projected = shelves(state_);
    auto locate_home = [&projected](GameId id) -> std::optional<std::pair<int, int>> {
        for (std::size_t shelf_index = 0; shelf_index < projected.size(); ++shelf_index) {
            const auto& ids = projected[shelf_index].game_ids;
            const auto item = std::find(ids.begin(), ids.end(), id);
            if (item != ids.end())
                return std::pair<int, int>{static_cast<int>(shelf_index),
                                           static_cast<int>(std::distance(ids.begin(), item))};
        }
        return std::nullopt;
    };
    auto home_location = locate_home(state_.navigation.selected_game_id);
    if (!home_location) home_location = locate_home(prior_selected_id);
    if (!home_location && !projected.empty()) {
        const int shelf_index = std::clamp(state_.navigation.home_shelf, 0,
                                           static_cast<int>(projected.size()) - 1);
        const auto& shelf = projected[static_cast<std::size_t>(shelf_index)];
        const int item_index = std::clamp(state_.navigation.home_shelf_focus[shelf.key], 0,
                                          playden::homebrew::shelf_cells(shelf) - 1);
        home_location = std::pair<int, int>{shelf_index, item_index};
    }
    if (home_location) {
        const auto [shelf_index, item_index] = *home_location;
        const auto& shelf = projected[static_cast<std::size_t>(shelf_index)];
        if (item_index == static_cast<int>(shelf.game_ids.size())) home_more_key = playden::homebrew::shelf_more_key(shelf);
        else home_candidate = shelf.game_ids[static_cast<std::size_t>(item_index)];
        state_.navigation.home_shelf = shelf_index;
        state_.navigation.home_shelf_focus[shelf.key] = item_index;
        state_.navigation.scroll_offsets["home"] = shelf_index;
    } else {
        home_candidate = library_candidate;
    }

    if (state_.navigation.route == Route::Library) {
        state_.navigation.selected_game_id = library_candidate;
        if (!state_.navigation.library_rail_focused)
            state_.navigation.focus_key = library_candidate == 0
                ? (state_.library.query.empty() ? "library-empty" : "clear-search")
                : game_key(library_candidate);
    } else if (state_.navigation.route == Route::Home ||
               state_.navigation.route == Route::Settings ||
               state_.navigation.route == Route::Downloads) {
        state_.navigation.selected_game_id = home_candidate;
        if (state_.navigation.route == Route::Home)
            state_.navigation.focus_key = !home_more_key.empty() ? home_more_key
                                          : home_candidate == 0 ? "home-library" : game_key(home_candidate);
    }
}

PersistenceLoadToken Simulation::BeginOrganizationLoad(std::uint64_t account) {
    if (!live() || account == 0) return {};
    if (organization_account_ != account) {
        organization_account_ = account;
        organization_revision_ = 0;
        organization_saved_revision_ = 0;
        organization_journal_.clear();
        organization_journal_bytes_ = 0;
        captured_organization_ = ExportOrganization();
        persisted_organization_ = {};
        organization_save_suppressed_ = false;
        organization_journal_full_ = false;
    } else if (OrganizationDirty() && organization_journal_.empty()) {
        const auto bytes = snapshot_size(persisted_organization_) + snapshot_size(captured_organization_);
        if (bytes <= kOrganizationJournalBytes) {
            organization_journal_.push_back({persisted_organization_, captured_organization_, false, false, bytes});
            organization_journal_bytes_ = bytes;
        } else {
            organization_journal_full_ = true;
        }
    }
    organization_load_token_ = {++next_persistence_load_token_, organization_revision_};
    organization_load_pending_ = true;
    return organization_load_token_;
}

bool Simulation::ApplyOrganization(std::uint64_t account, PersistenceLoadToken token,
                                   std::optional<playden::OrganizationSnapshot> loaded) {
    if (!live() || account == 0 || account != organization_account_ ||
        !organization_load_pending_ || !(token == organization_load_token_)) return false;
    auto loaded_base = loaded.value_or(playden::OrganizationSnapshot{});
    auto merged = loaded_base;
    const bool had_local_edits = !organization_journal_.empty();
    const bool had_local_selection = std::any_of(
        organization_journal_.begin(), organization_journal_.end(), [](const auto& entry) {
            return entry.reset || entry.selected ||
                   entry.before.selected_game_id != entry.after.selected_game_id;
        });
    for (const auto& entry : organization_journal_)
        replay_organization_delta(merged, entry.before, entry.after, entry.reset, entry.selected);
    if (!had_local_selection && loaded_base.selected_game_id == 0)
        merged.selected_game_id = captured_organization_.selected_game_id;
    if (!snapshot_within_model_bounds(merged)) {
        organization_load_pending_ = false;
        organization_load_failed_ = true;
        update_persistence_notice();
        rebuild_view();
        return false;
    }
    apply_organization_snapshot(merged);
    merged.selected_game_id = state_.navigation.selected_game_id;
    captured_organization_ = merged;
    persisted_organization_ = std::move(loaded_base);
    organization_journal_.clear();
    organization_journal_bytes_ = 0;
    organization_load_pending_ = false;
    organization_load_failed_ = false;
    organization_journal_full_ = false;
    if (had_local_edits) {
        if (organization_revision_ == 0) organization_revision_ = 1;
        organization_saved_revision_ = 0;
    } else {
        organization_saved_revision_ = organization_revision_;
    }
    update_persistence_notice();
    rebuild_view();
    return true;
}

bool Simulation::FailOrganizationLoad(std::uint64_t account, PersistenceLoadToken token,
                                      playden::StorageError error) {
    if (!live() || error == playden::StorageError::kNone || account != organization_account_ ||
        !organization_load_pending_ || !(token == organization_load_token_)) return false;
    organization_load_pending_ = false;
    organization_load_failed_ = true;
    update_persistence_notice();
    rebuild_view();
    return true;
}

void Simulation::OrganizationCleared(std::uint64_t account) {
    if (!live() || account == 0 || account != organization_account_) return;
    organization_load_token_ = {};
    organization_load_pending_ = false;
    organization_load_failed_ = false;
    organization_save_failed_ = false;
    organization_journal_full_ = false;
    organization_journal_.clear();
    organization_journal_bytes_ = 0;
    organization_save_suppressed_ = true;
    organization_saved_revision_ = organization_revision_;
    captured_organization_ = ExportOrganization();
    persisted_organization_ = {};
    update_persistence_notice();
    rebuild_view();
}

PersistenceLoadToken Simulation::BeginPreferencesLoad() {
    if (!live()) return {};
    preferences_load_token_ = {++next_persistence_load_token_, preferences_revision_};
    preferences_load_pending_ = true;
    preferences_load_may_apply_ = !PreferencesDirty();
    return preferences_load_token_;
}

bool Simulation::ApplyPreferences(PersistenceLoadToken token,
                                  const playden::PreferencesLoadResult& loaded) {
    if (!live() || !preferences_load_pending_ || !(token == preferences_load_token_)) return false;
    preferences_load_pending_ = false;
    preferences_load_failed_ = loaded.status == playden::PreferencesLoadStatus::kDefaultCorrupt ||
                               loaded.status == playden::PreferencesLoadStatus::kDefaultReadError;
    if (preferences_load_may_apply_ && preferences_revision_ == token.baseline_revision) {
        state_.reduced_motion = loaded.preferences.reduce_motion;
        state_.title_store_suffix = loaded.preferences.title_store_suffix;
        captured_preferences_ = loaded.preferences;
        preferences_saved_revision_ = preferences_revision_;
    }
    update_persistence_notice();
    rebuild_view();
    return true;
}

bool Simulation::OrganizationDirty() const noexcept {
    return !organization_save_suppressed_ && organization_revision_ > organization_saved_revision_;
}

bool Simulation::PreferencesDirty() const noexcept {
    return preferences_revision_ > preferences_saved_revision_;
}

bool Simulation::OrganizationReadyForSave() const noexcept {
    return live() && organization_account_ != 0 && OrganizationDirty() &&
           !organization_load_pending_ && !organization_load_failed_ && !organization_journal_full_;
}

void Simulation::OrganizationSaveResult(std::uint64_t account, std::uint64_t revision,
                                        playden::StorageError error) {
    if (!live() || account != organization_account_ || revision > organization_revision_) return;
    if (error == playden::StorageError::kNone) {
        organization_saved_revision_ = std::max(organization_saved_revision_, revision);
        if (revision == organization_revision_) {
            organization_save_failed_ = false;
            persisted_organization_ = captured_organization_;
        }
    } else {
        organization_save_failed_ = true;
    }
    update_persistence_notice();
    rebuild_view();
}

void Simulation::PreferencesSaveResult(std::uint64_t revision, playden::StorageError error) {
    if (!live() || revision > preferences_revision_) return;
    if (error == playden::StorageError::kNone) {
        preferences_saved_revision_ = std::max(preferences_saved_revision_, revision);
        if (revision == preferences_revision_) preferences_save_failed_ = false;
    } else {
        preferences_save_failed_ = true;
    }
    update_persistence_notice();
    rebuild_view();
}

bool Simulation::TakePersistenceRetryRequest() {
    const bool requested = persistence_retry_requested_;
    persistence_retry_requested_ = false;
    return requested;
}

void Simulation::update_persistence_notice() {
    const bool failed = organization_load_failed_ || organization_save_failed_ ||
                        organization_journal_full_ || preferences_load_failed_ ||
                        preferences_save_failed_;
    state_.persistence_available = !failed;
    state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(),
                                        [](const Notice& notice) { return notice.id == "persistence"; }),
                         state_.notices.end());
    if (failed)
        state_.notices.push_back({"persistence", "Saved UI data is unavailable", "retry-save", true, 0});
}

bool Simulation::record_live_persistence() {
    auto after_organization = ExportOrganization();
    auto after_preferences = ExportPreferences();
    const bool organization_changed = force_organization_reset_ || force_organization_selection_ ||
                                      !(after_organization == captured_organization_);
    const bool preferences_changed = force_preferences_reset_ ||
                                     (after_preferences.reduce_motion != captured_preferences_.reduce_motion ||
                                      after_preferences.title_store_suffix != captured_preferences_.title_store_suffix);

    if (organization_changed && !snapshot_within_model_bounds(after_organization)) {
        apply_organization_snapshot(captured_organization_);
        state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(),
                                            [](const Notice& notice) { return notice.id == "persistence-validation"; }),
                             state_.notices.end());
        state_.notices.push_back({"persistence-validation", "This change is too large to save", "", true, 0});
        force_organization_reset_ = false;
        return false;
    }
    if (organization_changed && organization_account_ != 0) {
        if (organization_load_pending_ || organization_load_failed_) {
            const auto bytes = snapshot_size(captured_organization_) + snapshot_size(after_organization);
            if (organization_journal_.size() >= kOrganizationJournalEntries ||
                bytes > kOrganizationJournalBytes - std::min(organization_journal_bytes_, kOrganizationJournalBytes)) {
                apply_organization_snapshot(captured_organization_);
                state_.reduced_motion = captured_preferences_.reduce_motion;
                state_.title_store_suffix = captured_preferences_.title_store_suffix;
                organization_journal_full_ = true;
                force_organization_reset_ = false;
                force_organization_selection_ = false;
                force_preferences_reset_ = false;
                update_persistence_notice();
                return false;
            }
            organization_journal_.push_back({captured_organization_, after_organization,
                                             force_organization_reset_, force_organization_selection_, bytes});
            organization_journal_bytes_ += bytes;
        }
        captured_organization_ = std::move(after_organization);
        ++organization_revision_;
        organization_save_suppressed_ = false;
    }
    if (preferences_changed) {
        captured_preferences_ = after_preferences;
        ++preferences_revision_;
    }
    force_organization_reset_ = false;
    force_organization_selection_ = false;
    force_preferences_reset_ = false;
    update_persistence_notice();
    return true;
}

void Simulation::load_fixtures() {
    if (live()) return;
    const auto source = read_file(fixture_directory_ / "catalog.json");
    const auto root = json::parse(source);
    if (!root || !root->object()) {
        last_error_ = "Could not read fixtures/ui-comparison/catalog.json";
        state_.persistence_available = false;
        return;
    }
    state_.clock_ticks = static_cast<std::int64_t>(uint_value(root->get("clock"), kFixtureClock));
    if (const auto* values = root->get("games"); values && values->array()) {
        for (const auto& item : *values->array()) {
            Game game;
            game.id = static_cast<GameId>(uint_value(item.get("id")));
            game.title = string_value(item.get("title"));
            game.description = string_value(item.get("description"), "Description unavailable");
            game.source = string_value(item.get("source"));
            if (const auto* art = item.get("art")) {
                game.cover_key = string_value(art->get("cover"));
                game.hero_key = string_value(art->get("hero"));
                game.logo_key = string_value(art->get("logo"));
                game.art_failed = string_value(art->get("state")) == "failed";
            }
            if (const auto* genres = item.get("genres"); genres && genres->array())
                for (const auto& genre : *genres->array()) game.genres.push_back(string_value(&genre));
            game.controller_support = string_value(item.get("controller_support"), "unknown");
            game.download_bytes = optional_uint(item.get("download_bytes"));
            game.install_bytes = optional_uint(item.get("installed_bytes"));
            if (const auto added = optional_uint(item.get("added_at"))) game.added_at = static_cast<std::int64_t>(*added);
            if (const auto played = optional_uint(item.get("last_played"))) game.last_played = static_cast<std::int64_t>(*played);
            game.playtime_minutes = optional_uint(item.get("playtime_minutes"));
            state_.games.push_back(std::move(game));
        }
    }
    if (const auto* account = root->get("account")) {
        state_.account.id = string_value(account->get("id"));
        state_.account.display_name = string_value(account->get("display_name"));
        state_.account.authentication = AuthState::SignedIn;
        state_.account.last_sync = static_cast<std::int64_t>(uint_value(account->get("last_sync")));
        state_.account.cache_available = bool_value(account->get("cache_available"));
    }
    if (const auto* organization = root->get("organization")) {
        if (const auto* favorites = organization->get("favorites"); favorites && favorites->array())
            for (const auto& id : *favorites->array()) state_.library.favorites.insert(static_cast<GameId>(uint_value(&id)));
        if (const auto* hidden = organization->get("hidden"); hidden && hidden->array())
            for (const auto& id : *hidden->array()) state_.library.hidden.insert(static_cast<GameId>(uint_value(&id)));
        if (const auto* collections = organization->get("collections"); collections && collections->array()) {
            for (const auto& item : *collections->array()) {
                Collection collection;
                collection.id = string_value(item.get("id"));
                collection.name = string_value(item.get("name"));
                collection.pinned = bool_value(item.get("pinned"));
                if (const auto* ids = item.get("game_ids"); ids && ids->array())
                    for (const auto& id : *ids->array()) collection.members.insert(static_cast<GameId>(uint_value(&id)));
                state_.library.collections.push_back(std::move(collection));
                ++next_collection_serial_;
            }
        }
        state_.library.query = string_value(organization->get("query"));
        state_.library.scope = string_value(organization->get("scope"), "all");
        state_.library.sort = string_value(organization->get("sort"), "name");
    }
    if (const auto* values = root->get("installations"); values && values->array()) {
        for (const auto& item : *values->array()) {
            Installation installation;
            installation.game_id = static_cast<GameId>(uint_value(item.get("game_id")));
            installation.volume_id = string_value(item.get("volume_id"));
            installation.installed_bytes = uint_value(item.get("installed_bytes"));
            installation.available = bool_value(item.get("available"), true);
            installation.runtime_ready = bool_value(item.get("runtime_ready"), false);
            installation.saves_present = bool_value(item.get("saves_preserved"), true);
            state_.installations.push_back(std::move(installation));
        }
    }
    if (const auto* values = root->get("volumes"); values && values->array()) {
        for (const auto& item : *values->array()) {
            Volume volume;
            volume.id = string_value(item.get("id"));
            volume.label = string_value(item.get("label"));
            volume.capacity_bytes = uint_value(item.get("capacity_bytes"));
            volume.used_bytes = uint_value(item.get("used_bytes"));
            volume.reserved_bytes = uint_value(item.get("reserved_bytes"));
            volume.available = bool_value(item.get("available"), true);
            volume.writable = bool_value(item.get("writable"), true);
            volume.capacity_known = bool_value(item.get("capacity_known"), true);
            volume.base_used_bytes = volume.used_bytes;
            volume.base_reserved_bytes = volume.reserved_bytes;
            state_.volumes.push_back(std::move(volume));
        }
        for (auto& volume : state_.volumes) {
            std::uint64_t fixture_install_bytes = 0;
            for (const auto& installation : state_.installations) if (installation.volume_id == volume.id)
                fixture_install_bytes += installation.installed_bytes;
            volume.base_used_bytes = volume.used_bytes > fixture_install_bytes ? volume.used_bytes - fixture_install_bytes : 0;
        }
    }
    if (const auto* values = root->get("jobs"); values && values->array()) {
        for (const auto& item : *values->array()) {
            Job job;
            job.id = string_value(item.get("id"));
            job.game_id = static_cast<GameId>(uint_value(item.get("game_id")));
            job.volume_id = string_value(item.get("volume_id"));
            job.kind = string_value(item.get("kind"), "install");
            job.queue_order = static_cast<int>(uint_value(item.get("order")));
            job.stage = parse_job_stage(string_value(item.get("stage")));
            job.state = parse_job_state(string_value(item.get("state")));
            job.completed_bytes = uint_value(item.get("completed_bytes"));
            job.total_bytes = uint_value(item.get("total_bytes"));
            job.bytes_per_second = optional_uint(item.get("rate_bytes_per_second"));
            if (const auto eta = optional_uint(item.get("eta_seconds"))) job.eta_seconds = static_cast<int>(*eta);
            job.failure = string_value(item.get("failure"));
            job.retry_stage = parse_job_stage(string_value(item.get("retry_target")));
            state_.jobs.push_back(std::move(job));
        }
    }
    for (const auto& job : state_.jobs) {
        if (job.id.rfind("job-", 0) != 0) continue;
        std::uint64_t serial = 0;
        const auto text = job.id.substr(4);
        const auto result = std::from_chars(text.data(), text.data() + text.size(), serial);
        if (result.ec == std::errc{} && result.ptr == text.data() + text.size()) next_job_serial_ = std::max(next_job_serial_, serial + 1);
    }
}

void Simulation::load_profile() {
    if (live()) return;
    const auto source = read_file(profile_path_);
    if (source.empty()) return;
    const auto root = json::parse(source);
    if (!root || !root->object()) {
        state_.persistence_available = false;
        state_.notices.push_back({"persistence", "Saved UI state is corrupt", "retry-save", true, 0});
        return;
    }
    const auto saved_account = string_value(root->get("account_id"));
    const auto saved_auth = string_value(root->get("authentication"));
    const auto restored_auth = parse_auth_state(saved_auth);
    if (saved_account != state_.account.id) {
        state_.library = {};
        state_.game_settings.clear();
    }
    state_.account.id = saved_account;
    state_.account.display_name = saved_account.empty() ? std::string{} : string_value(root->get("account_display_name"), "Connected fixture account");
    state_.reduced_motion = bool_value(root->get("reduced_motion"));
    if (saved_auth == "signed-out" || saved_auth == "cancelled" || (saved_auth == "waiting" && saved_account.empty())) {
        state_.account.authentication = AuthState::SignedOut; state_.account.id.clear(); state_.account.display_name.clear();
        state_.account.cache_available = false; state_.account.challenge_expiry = 0; state_.navigation.route = Route::Startup; state_.navigation.focus_key = "startup-connect";
    } else if (saved_auth == "syncing") {
        state_.account.authentication = AuthState::Failed; state_.account.cache_available = false; state_.account.challenge_expiry = 0;
        state_.account.error = "Account sync was interrupted"; state_.navigation.route = Route::SignIn; state_.navigation.focus_key = "qr-retry";
    } else if (saved_auth == "waiting" || saved_auth == "expired") {
        state_.account.authentication = AuthState::Expired; state_.account.cache_available = false; state_.account.challenge_expiry = 0;
        state_.account.error = "Challenge expired during restart"; state_.navigation.route = Route::SignIn; state_.navigation.focus_key = "qr-refresh";
    } else if (saved_auth == "failed") {
        state_.account.authentication = AuthState::Failed; state_.account.cache_available = false; state_.account.challenge_expiry = 0;
        state_.account.error = "Account connection needs retry"; state_.navigation.route = Route::SignIn; state_.navigation.focus_key = "qr-retry";
    } else if (!saved_auth.empty()) {
        state_.account.authentication = restored_auth; state_.account.cache_available = restored_auth == AuthState::SignedIn || restored_auth == AuthState::Offline;
    }
    state_.account.last_sync = static_cast<std::int64_t>(uint_value(root->get("account_last_sync"), static_cast<std::uint64_t>(state_.account.last_sync)));
    state_.library.query = string_value(root->get("query"), state_.library.query);
    state_.library.scope = string_value(root->get("scope"), state_.library.scope);
    if (const auto* favorites = root->get("favorites"); favorites && favorites->array()) {
        state_.library.favorites.clear();
        for (const auto& id : *favorites->array()) state_.library.favorites.insert(static_cast<GameId>(uint_value(&id)));
    }
    if (const auto* hidden = root->get("hidden"); hidden && hidden->array()) {
        state_.library.hidden.clear();
        for (const auto& id : *hidden->array()) state_.library.hidden.insert(static_cast<GameId>(uint_value(&id)));
    }
    state_.library.sort = string_value(root->get("sort"), state_.library.sort);
    if (const auto* filters = root->get("filters"); filters && filters->array()) {
        state_.library.filters.clear();
        for (const auto& filter : *filters->array()) state_.library.filters.insert(string_value(&filter));
    }
    if (const auto* collections = root->get("collections"); collections && collections->array()) {
        state_.library.collections.clear();
        for (const auto& item : *collections->array()) {
            Collection collection;
            collection.id = string_value(item.get("id")); collection.name = string_value(item.get("name")); collection.pinned = bool_value(item.get("pinned"));
            if (const auto* members = item.get("members"); members && members->array()) for (const auto& id : *members->array()) collection.members.insert(static_cast<GameId>(uint_value(&id)));
            state_.library.collections.push_back(std::move(collection));
            ++next_collection_serial_;
        }
    }
    if (const auto* ratings = root->get("ratings"); ratings && ratings->array()) {
        state_.library.ratings.clear();
        for (const auto& item : *ratings->array()) state_.library.ratings[static_cast<GameId>(uint_value(item.get("id")))] = static_cast<Compatibility>(uint_value(item.get("value")));
    }
    if (const auto* notes = root->get("notes"); notes && notes->array()) {
        state_.library.notes.clear();
        for (const auto& item : *notes->array()) state_.library.notes[static_cast<GameId>(uint_value(item.get("id")))] = string_value(item.get("value"));
    }
    // Keep revision numbers lexical: the fixture reader's doubles cannot represent every uint64.
    playden::steam::json::Limits settings_limits;
    settings_limits.max_bytes = 16u * 1024u * 1024u;
    settings_limits.max_values = 262144;
    const auto exact_profile = playden::steam::json::Parse(source, settings_limits);
    const auto* settings = exact_profile ? exact_profile->Find("game_settings") : nullptr;
    if (settings && settings->is_array()) {
        state_.game_settings.clear();
        for (const auto& item : settings->array()) {
            const auto* identity = item.Find("game_id");
            const auto app = identity ? identity->AsUint64() : std::nullopt;
            const auto* value = item.Find("settings");
            if (!app || *app == 0 || *app > std::numeric_limits<std::uint32_t>::max() || !value || !value->is_object()) continue;
            rt::GameSettingsRecord record;
            if (const auto* preset = value->Find("preset")) {
                if (!preset->is_string()) continue;
                record.preset_id = preset->text();
            }
            if (const auto* revision = value->Find("revision")) {
                const auto parsed = revision->AsUint64();
                if (!parsed) continue;
                record.revision = *parsed;
            }
            if (const auto* overrides = value->Find("overrides"); overrides && overrides->is_object())
                for (const auto& [id, text] : overrides->object())
                    if (text.is_string() && rt::FindSetting(id)) record.overrides[id] = text.text();
            state_.game_settings[static_cast<GameId>(*app)] = std::move(record);
        }
    }
    if (const auto* jobs = root->get("jobs"); jobs && jobs->array()) {
        state_.jobs.clear();
        for (const auto& item : *jobs->array()) {
            Job job;
            job.id = string_value(item.get("id")); job.kind = string_value(item.get("kind"), "install"); job.game_id = static_cast<GameId>(uint_value(item.get("game_id"))); job.queue_order = static_cast<int>(uint_value(item.get("order")));
            job.stage = static_cast<JobStage>(uint_value(item.get("stage"))); job.state = static_cast<JobState>(uint_value(item.get("state")));
            job.completed_bytes = uint_value(item.get("completed")); job.total_bytes = uint_value(item.get("total")); job.volume_id = string_value(item.get("volume"));
            job.failure = string_value(item.get("failure")); job.retry_stage = static_cast<JobStage>(uint_value(item.get("retry"))); job.stage_ticks = static_cast<int>(uint_value(item.get("stage_ticks")));
            job.bytes_per_second = optional_uint(item.get("rate"));
            if (const auto eta = optional_uint(item.get("eta"))) job.eta_seconds = static_cast<int>(*eta);
            job.reserved_bytes = uint_value(item.get("reserved"));
            job.reservation_held = bool_value(item.get("reservation_held"));
            job.completion_applied = bool_value(item.get("completion_applied"));
            if (job.state == JobState::Downloading || job.state == JobState::Verifying || job.state == JobState::Pausing) job.state = JobState::Interrupted;
            state_.jobs.push_back(std::move(job));
        }
    }
    next_job_serial_ = std::max(next_job_serial_, uint_value(root->get("next_job_serial"), next_job_serial_));
    if (const auto* installations = root->get("installations"); installations && installations->array()) {
        state_.installations.clear();
        for (const auto& item : *installations->array()) state_.installations.push_back({static_cast<GameId>(uint_value(item.get("game_id"))), string_value(item.get("volume")), uint_value(item.get("bytes")), bool_value(item.get("available"), true), bool_value(item.get("runtime")), bool_value(item.get("saves"), true)});
    }
    if (const auto* volumes = root->get("volumes"); volumes && volumes->array()) {
        for (const auto& item : *volumes->array()) {
            const auto id = string_value(item.get("id"));
            const auto found = std::find_if(state_.volumes.begin(), state_.volumes.end(), [&id](const Volume& value) { return value.id == id; });
            if (found == state_.volumes.end()) continue;
            found->used_bytes = uint_value(item.get("used"), found->used_bytes);
            found->reserved_bytes = uint_value(item.get("reserved"), found->reserved_bytes);
            found->available = bool_value(item.get("available"), found->available);
        }
    }
    if (state_.account.authentication == AuthState::SignedOut) {
        state_.library = {};
        state_.game_settings.clear();
    }
    reconcile_storage(state_);
}

bool Simulation::save_profile() {
    settings_save_failed_ = true;
    if (live() && !settings_load_failed_ && !game_settings_file_.empty() && state_.game_settings != saved_game_settings_) {
        std::map<std::uint32_t, rt::GameSettingsRecord> records;
        for (const auto& [id, record] : state_.game_settings) records[static_cast<std::uint32_t>(id)] = record;
        std::string reason;
        if (rt::SaveGameSettingsFile(game_settings_file_.string(), records, &reason)) saved_game_settings_ = state_.game_settings;
        else push_notice({"settings-save-failed", "Game settings were not saved", "", false, kLiveNoticeSeconds, true, reason});
    }
    if (live()) {
        settings_save_failed_ = settings_load_failed_ || game_settings_file_.empty() || state_.game_settings != saved_game_settings_;
        const bool saved = record_live_persistence();
        return saved && !settings_save_failed_;
    }
    if (!state_.persistence_available) {
        state_.notices.push_back({"save-failed", "Changes cannot be saved", "retry-save", true, 0});
        return false;
    }
    std::error_code error;
    if (!profile_path_.parent_path().empty()) std::filesystem::create_directories(profile_path_.parent_path(), error);
    std::string output = "{\"account_id\":\"" + json::escape(state_.account.id) + "\",\"account_display_name\":\"" + json::escape(state_.account.display_name)
        + "\",\"authentication\":\"" + auth_name(state_.account.authentication) + "\",\"account_last_sync\":" + std::to_string(state_.account.last_sync)
        + ",\"next_job_serial\":" + std::to_string(next_job_serial_) + ",\"reduced_motion\":"
        + (state_.reduced_motion ? "true" : "false") + ",\"query\":\"" + json::escape(state_.library.query)
        + "\",\"scope\":\"" + json::escape(state_.library.scope) + "\",\"favorites\":[";
    bool first = true;
    for (const auto id : state_.library.favorites) {
        if (!first) output += ',';
        first = false;
        output += std::to_string(id);
    }
    output += "],\"hidden\":[";
    first = true;
    for (const auto id : state_.library.hidden) { if (!first) output += ','; first = false; output += std::to_string(id); }
    output += "],\"sort\":\"" + json::escape(state_.library.sort) + "\",\"filters\":[";
    first = true;
    for (const auto& filter : state_.library.filters) { if (!first) output += ','; first = false; output += '"' + json::escape(filter) + '"'; }
    output += "],\"collections\":[";
    first = true;
    for (const auto& collection : state_.library.collections) {
        if (!first) output += ','; first = false;
        output += "{\"id\":\"" + json::escape(collection.id) + "\",\"name\":\"" + json::escape(collection.name)
            + "\",\"pinned\":" + (collection.pinned ? "true" : "false") + ",\"members\":[";
        bool first_member = true;
        for (const auto id : collection.members) { if (!first_member) output += ','; first_member = false; output += std::to_string(id); }
        output += "]}";
    }
    output += "],\"ratings\":[";
    first = true;
    for (const auto& [id, rating] : state_.library.ratings) { if (!first) output += ','; first = false; output += "{\"id\":" + std::to_string(id) + ",\"value\":" + std::to_string(static_cast<int>(rating)) + '}'; }
    output += "],\"notes\":[";
    first = true;
    for (const auto& [id, note] : state_.library.notes) { if (!first) output += ','; first = false; output += "{\"id\":" + std::to_string(id) + ",\"value\":\"" + json::escape(note) + "\"}"; }
    output += "],\"game_settings\":[";
    first = true;
    for (const auto& [id, record] : state_.game_settings) {
        if (!first) output += ','; first = false;
        output += "{\"game_id\":" + std::to_string(id) + ",\"settings\":" + rt::SerializeGameSettingsRecord(record) + "}";
    }
    output += "],\"jobs\":[";
    first = true;
    for (const auto& job : state_.jobs) {
        if (!first) output += ','; first = false;
        output += "{\"id\":\"" + json::escape(job.id) + "\",\"kind\":\"" + json::escape(job.kind) + "\",\"game_id\":" + std::to_string(job.game_id)
            + ",\"order\":" + std::to_string(job.queue_order) + ",\"stage\":" + std::to_string(static_cast<int>(job.stage))
            + ",\"state\":" + std::to_string(static_cast<int>(job.state)) + ",\"completed\":" + std::to_string(job.completed_bytes)
            + ",\"total\":" + std::to_string(job.total_bytes) + ",\"volume\":\"" + json::escape(job.volume_id)
            + "\",\"failure\":\"" + json::escape(job.failure) + "\",\"retry\":" + std::to_string(static_cast<int>(job.retry_stage))
            + ",\"rate\":" + (job.bytes_per_second ? std::to_string(*job.bytes_per_second) : "null")
            + ",\"eta\":" + (job.eta_seconds ? std::to_string(*job.eta_seconds) : "null")
            + ",\"stage_ticks\":" + std::to_string(job.stage_ticks) + ",\"reserved\":" + std::to_string(job.reserved_bytes)
            + ",\"reservation_held\":" + (job.reservation_held ? "true" : "false")
            + ",\"completion_applied\":" + (job.completion_applied ? "true" : "false") + '}';
    }
    output += "],\"installations\":[";
    first = true;
    for (const auto& installation : state_.installations) {
        if (!first) output += ','; first = false;
        output += "{\"game_id\":" + std::to_string(installation.game_id) + ",\"volume\":\"" + json::escape(installation.volume_id)
            + "\",\"bytes\":" + std::to_string(installation.installed_bytes) + ",\"available\":" + (installation.available ? "true" : "false")
            + ",\"runtime\":" + (installation.runtime_ready ? "true" : "false") + ",\"saves\":" + (installation.saves_present ? "true" : "false") + '}';
    }
    output += "],\"volumes\":[";
    first = true;
    for (const auto& volume : state_.volumes) {
        if (!first) output += ','; first = false;
        output += "{\"id\":\"" + json::escape(volume.id) + "\",\"used\":" + std::to_string(volume.used_bytes)
            + ",\"reserved\":" + std::to_string(volume.reserved_bytes) + ",\"available\":" + (volume.available ? "true" : "false") + '}';
    }
    output += "]}";
    std::FILE* file = std::fopen(profile_path_.string().c_str(), "wb");
    if (!file) {
        state_.persistence_available = false;
        state_.notices.push_back({"save-failed", "Changes cannot be saved", "retry-save", true, 0});
        return false;
    }
    const bool written = std::fwrite(output.data(), 1, output.size(), file) == output.size();
    const bool closed = std::fclose(file) == 0;
    settings_save_failed_ = !(written && closed);
    return written && closed;
}

bool Simulation::scenario(const std::string& name) {
    if (live()) return false;
    if (name == "default") {
        state_.account.authentication = AuthState::SignedIn;
        state_.navigation.route = Route::Home;
        state_.navigation.selected_index = 0;
        state_.navigation.selected_game_id = state_.games.empty() ? 0 : state_.games.front().id;
        state_.navigation.focus_key = state_.games.empty() ? "home-library" : game_key(state_.navigation.selected_game_id);
        const auto projected = playden::homebrew::shelves(state_);
        for (std::size_t index = 0; index < projected.size(); ++index) {
            const auto game = std::find(projected[index].game_ids.begin(), projected[index].game_ids.end(), state_.navigation.selected_game_id);
            if (game != projected[index].game_ids.end()) {
                state_.navigation.home_shelf = static_cast<int>(index);
                state_.navigation.home_shelf_focus[projected[index].key] = static_cast<int>(std::distance(projected[index].game_ids.begin(), game));
                break;
            }
        }
    } else if (name == "first-run" || name == "signed-out") {
        state_.account.authentication = AuthState::SignedOut;
        state_.account.id.clear();
        state_.account.display_name.clear();
        state_.account.cache_available = false;
        state_.navigation.route = Route::Startup;
        state_.navigation.focus_key = "startup-connect";
    } else if (name == "startup-loading") {
        state_.startup_loading = true; state_.navigation.route = Route::Startup; state_.navigation.focus_key = "startup-loading";
    } else if (name == "persistence-corrupt") {
        state_.persistence_available = false;
        state_.navigation.route = Route::Startup;
        state_.notices.push_back({"persistence", "Saved state is unavailable", "retry-save", true, 0});
    } else if (name == "qr-waiting") {
        state_.navigation.route = Route::SignIn;
        state_.account.authentication = AuthState::Waiting;
        state_.account.challenge_expiry = state_.clock_ticks + 30;
        ++challenge_generation_;
        state_.navigation.focus_key = "qr-cancel";
    } else if (name == "qr-expired") {
        state_.navigation.route = Route::SignIn;
        state_.account.authentication = AuthState::Expired;
        state_.navigation.focus_key = "qr-refresh";
    } else if (name == "qr-failure" || name == "qr-denied") {
        state_.navigation.route = Route::SignIn;
        state_.account.authentication = AuthState::Failed;
        state_.account.error = name == "qr-denied" ? "Challenge denied" : "Network unavailable";
        state_.navigation.focus_key = "qr-retry";
    } else if (name == "qr-approved") {
        if (state_.account.authentication != AuthState::Cancelled && challenge_generation_ != cancelled_challenge_generation_) {
            if (state_.account.id.empty()) { state_.account.id = "fixture-connected"; state_.account.display_name = "Connected fixture account"; }
            state_.account.authentication = AuthState::Syncing;
            state_.navigation.route = Route::SignIn;
            state_.navigation.focus_key = "sync-status";
        }
    } else if (name == "late-approval") {
        if (state_.account.authentication != AuthState::Cancelled) state_.account.authentication = AuthState::Syncing;
    } else if (name == "syncing") {
        state_.account.authentication = AuthState::Syncing;
        state_.library_syncing = true;
        state_.navigation.route = Route::Home;
    } else if (name == "empty") {
        state_.navigation.route = Route::Library;
        state_.navigation.selected_game_id = 0;
        state_.navigation.focus_key = "library-empty";
        state_.library.query.clear();
        state_.library.filters.insert("empty-fixture");
    } else if (name == "offline") {
        state_.navigation.route = Route::Library;
        state_.account.authentication = AuthState::Offline;
        state_.library_offline = true;
        state_.notices.push_back({"offline", "Offline — showing cached library", "retry-sync", true, 0});
    } else if (name == "no-results") {
        state_.navigation.route = Route::Library;
        state_.library.query = "zzz-no-matches";
        state_.navigation.selected_game_id = 0;
        state_.navigation.focus_key = "clear-search";
    } else if (name == "delayed-art" || name == "failed-art") {
        state_.navigation.route = Route::Library;
        state_.navigation.selected_game_id = name == "delayed-art" ? 900007 : 900008;
        state_.navigation.focus_key = game_key(state_.navigation.selected_game_id);
        if (auto* game = const_cast<Game*>(find_game(state_, state_.navigation.selected_game_id))) {
            game->art_pending = name == "delayed-art";
            game->art_failed = name == "failed-art";
        }
    } else if (name == "install-ready" || name == "unknown-size" || name == "insufficient-space") {
        state_.navigation.route = Route::Details;
        state_.navigation.detail_id = name == "unknown-size" ? 900006 : 900012;
        state_.navigation.selected_game_id = *state_.navigation.detail_id;
        state_.navigation.focus_key = "details-install";
        if (name == "insufficient-space") {
            selected_volume_id_ = "internal";
            if (auto* volume = find_volume(state_, "internal")) volume->used_bytes = volume->capacity_bytes - 1'073'741'824;
        }
        open_modal({"install", "Install game", "details-install", {"install-confirm", "install-estimate-retry", "storage-picker", "cancel"}, 0, false});
    } else if (name == "storage-disconnect") {
        state_.navigation.route = Route::Downloads;
        if (auto* volume = find_volume(state_, "external")) volume->available = false;
        if (auto* job = find_job(state_, "job-1")) { job->state = JobState::Interrupted; job->failure = "Storage disconnected"; }
        state_.notices.push_back({"storage", "USB fixture library disconnected", "storage-retry", true, 0});
    } else if (name == "paused" || name == "download-failed" || name == "verification-failed" || name == "restart-recovery") {
        state_.navigation.route = Route::Downloads;
        if (auto* job = find_job(state_, "job-1")) {
            if (name == "paused") job->state = JobState::Paused;
            else if (name == "restart-recovery") job->state = JobState::Interrupted;
            else { job->state = JobState::Failed; job->failure = name == "verification-failed" ? "Verification failed" : "Download failed"; }
            job->stage = name == "verification-failed" ? JobStage::Verify : JobStage::Download;
            job->retry_stage = job->stage;
        }
        state_.navigation.focus_key = "job-job-1";
    } else if (name == "save-failure") {
        state_.persistence_available = false;
        state_.notices.push_back({"save-failed", "Changes cannot be saved", "retry-save", true, 0});
    } else if (name == "runtime-unavailable") {
        state_.runtime = RuntimeState::Unavailable;
        state_.navigation.route = Route::Details;
        state_.navigation.detail_id = 900001;
        state_.navigation.selected_game_id = 900001;
        if (auto* installation = find_installation(state_, 900001)) installation->runtime_ready = false;
        state_.navigation.focus_key = "details-runtime";
    } else if (name == "launch-failure" || name == "crash" || name == "stop-failure") {
        state_.navigation.route = Route::Session;
        state_.session_game_id = 900001;
        state_.session = name == "crash" ? SessionState::Crashed : SessionState::Failed;
        state_.session_error = name == "launch-failure" ? "Simulated launch failed" : (name == "crash" ? "Game exited unexpectedly" : "Game did not stop");
        state_.navigation.focus_key = name == "crash" ? "session-logs" : "session-retry";
    } else if (name == "controller-disconnected") {
        state_.controller_connected = false;
        state_.notices.push_back({"controller", "Controller disconnected", "controller-test", true, 0});
    } else if (name == "controller-reconnected") {
        state_.controller_connected = true;
        state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(),
                                            [](const Notice& notice) { return notice.id == "controller"; }), state_.notices.end());
    } else if (name == "reduced-motion") {
        state_.reduced_motion = true;
        save_profile();
    } else if (name == "stress") {
        const auto source = read_file(fixture_directory_.parent_path() / "library.json");
        const auto root = json::parse(source);
        if (!root || !root->array()) { last_error_ = "Could not read 1,000-game stress fixture"; return false; }
        state_.games.clear();
        state_.games.reserve(root->array()->size());
        for (const auto& item : *root->array()) {
            Game game;
            game.id = static_cast<GameId>(uint_value(item.get("id")));
            game.title = string_value(item.get("title"));
            game.description = string_value(item.get("description"));
            game.source = "Steam / Windows";
            game.cover_key = string_value(item.get("artwork_source"));
            state_.games.push_back(std::move(game));
        }
        state_.library = {};
        state_.navigation.route = Route::Library;
        state_.navigation.selected_index = 0;
        state_.navigation.selected_game_id = state_.games.empty() ? 0 : state_.games.front().id;
        state_.navigation.focus_key = game_key(state_.navigation.selected_game_id);
    } else if (name == "art-ready") {
        if (auto found = std::find_if(state_.games.begin(), state_.games.end(), [](const Game& game) { return game.id == 900007; }); found != state_.games.end()) {
            found->art_pending = false;
            found->art_failed = false;
        }
    } else if (name == "sync-complete") {
        state_.library_syncing = false;
        state_.account.authentication = AuthState::SignedIn;
        state_.navigation.route = Route::Home;
    } else {
        last_error_ = "Unknown scenario: " + name;
        return false;
    }
    rebuild_view();
    return true;
}

std::vector<const Game*> Simulation::visible_games() const {
    if (state_.editor.active && state_.editor.purpose == "Search") {
        auto preview = state_;
        preview.library.query = state_.editor.value;
        const auto filtered = filtered_games(preview);
        std::vector<const Game*> result;
        result.reserve(filtered.size());
        for (const auto* game : filtered) {
            const auto index = static_cast<std::size_t>(game - preview.games.data());
            result.push_back(&state_.games[index]);
        }
        return result;
    }
    return filtered_games(state_);
}

void Simulation::rebuild_view() {
    const bool has_overlay = state_.editor.active || !state_.navigation.modals.empty();
    if (has_overlay) {
        const auto editor = state_.editor;
        const auto modals = state_.navigation.modals;
        const auto focus = state_.navigation.focus_key;
        const int selected_index = state_.navigation.selected_index;
        const GameId selected_game_id = state_.navigation.selected_game_id;
        state_.editor.active = false;
        state_.navigation.modals.clear();
        rebuild_view();
        route_view_ = view_;
        state_.editor = editor;
        state_.navigation.modals = modals;
        state_.navigation.focus_key = focus;
        state_.navigation.selected_index = selected_index;
        state_.navigation.selected_game_id = selected_game_id;
    }
    view_ = {};
    view_.surface = route_name(state_.navigation.route);
    view_.show_simulation_label = state_.simulation_mode;
    if (state_.editor.active) view_.surface = "keyboard";
    else if (!state_.navigation.modals.empty()) view_.surface = state_.navigation.modals.back().id;

    auto row = [this](std::string key, RowKind kind, std::string title, std::string action) -> ViewRow& {
        view_.rows.push_back({});
        auto& result = view_.rows.back();
        result.key = std::move(key); result.kind = kind; result.title = std::move(title); result.action = std::move(action);
        return result;
    };

    if (state_.editor.active) {
        view_.title = state_.editor.purpose;
        view_.subtitle = state_.editor.value;
        if (state_.editor.purpose == "Search") view_.subtitle += " · " + std::to_string(visible_games().size()) + " results";
        row("keyboard-done", RowKind::Action, "Done", "editor-done");
        row("keyboard-cancel", RowKind::Action, "Cancel", "editor-cancel");
        view_.legends = {"Cross Select", "Square Backspace", "L1/R1 Move caret", "Triangle Clear", "Circle Cancel"};
    } else if (!state_.navigation.modals.empty()) {
        const auto& modal = state_.navigation.modals.back();
        view_.title = modal.title;
        if (modal.id == "logs") {
            view_.subtitle = state_.log_context;
            if (state_.log_state == LogState::Loading) row("logs-loading", RowKind::Status, "Loading fixture logs…", "").enabled = false;
            else if (state_.log_state == LogState::Failed) {
                auto& status = row("logs-error", RowKind::Status, "Logs unavailable", ""); status.subtitle = live() ? state_.log_context : "Simulated log service failure"; status.enabled = false;
            } else if (state_.log_lines.empty()) {
                row("logs-empty", RowKind::Status, "No log entries", "").enabled = false;
            } else {
                constexpr int visible_lines = 8;
                const int begin = std::clamp(state_.log_offset, 0, std::max(0, static_cast<int>(state_.log_lines.size()) - visible_lines));
                const int end = std::min(static_cast<int>(state_.log_lines.size()), begin + visible_lines);
                for (int index = begin; index < end; ++index) {
                    auto& line = row("log-line-" + std::to_string(index), RowKind::Text, state_.log_lines[static_cast<std::size_t>(index)], "");
                    line.enabled = false;
                }
            }
        }
        const GameId settings_game = state_.navigation.selected_game_id;
        const bool settings_modal = modal.id == "game-settings" || modal.id == "preset-picker" || modal.id == "button-mapping" ||
                                    !modal.setting_id.empty();
        rt::EffectiveSettings settings_values;
        const bool settings_ready = settings_modal && EffectiveSettingsFor(settings_game, &settings_values);
        const auto settings_record = state_.game_settings.find(settings_game);
        const bool settings_custom = settings_record != state_.game_settings.end() && !settings_record->second.overrides.empty();
        // A picker marks the value the game would have without the player's choice.
        rt::EffectiveValue inherited;
        if (settings_ready && !modal.setting_id.empty()) {
            auto overrides = settings_record == state_.game_settings.end() ? std::map<std::string, std::string>{} : settings_record->second.overrides;
            overrides.erase(modal.setting_id);
            rt::EffectiveSettings base;
            rt::SettingsError ignored;
            if (rt::BuildEffectiveSettings(RecipeFor(settings_game), preset_for(settings_game), overrides, kit_, &base, &ignored))
                inherited = base.values.at(modal.setting_id);
        }
        if (modal.id == "game-settings") {
            if (const auto* game = find_game(state_, settings_game)) view_.subtitle = game->title;
        } else if (!modal.setting_id.empty()) {
            if (const auto* definition = rt::FindSetting(modal.setting_id)) {
                view_.subtitle = definition->effect;
                view_.subtitle += settings_custom || inherited.source != rt::ValueSource::kPlayden
                    ? std::string(" ") + source_label(inherited.source) + ": " + (modal.setting_id == "launch_option" ? inherited.value : choice_label(*definition, inherited.value)) + "."
                    : "";
                view_.subtitle += " Applies on next launch.";
            }
        } else if (modal.id == "preset-picker") {
            view_.subtitle = "A preset sets several settings at once. Choosing one clears your changes for this game.";
        }
        if (modal.id == "kept-data")
            view_.subtitle = "Saves and settings kept when a game was uninstalled. Installing a game again brings its saves "
                             "back. Saves in Steam Cloud stay in Steam Cloud.";
        for (const auto& action_key : modal.action_keys) {
            auto& item = row(action_key, RowKind::Action, action_label(action_key), action_key);
            item.dangerous = modal.destructive && action_key != "cancel";
            if (settings_ready && action_key == "preset-picker") {
                item.kind = RowKind::Profile;
                const auto* preset = preset_for(settings_game);
                const std::string name = preset ? preset->name : "Playden default";
                item.value = settings_custom ? "Custom · from " + name : name;
                item.subtitle = "A ready-made bundle of the settings below.";
            } else if (settings_ready && modal.id == "preset-picker" && action_key.rfind("preset-", 0) == 0) {
                item.kind = RowKind::Profile;
                const auto id = action_key.substr(7);
                const auto* current = preset_for(settings_game);
                for (const auto& preset : settings_presets_)
                    if (preset.id == id) { item.title = preset.name; item.subtitle = preset.try_when; item.value = preset.short_hint; }
                item.selected = current ? current->id == id : id == "playden-default";
                if (item.selected) item.badge = settings_custom ? "Current · changed" : "Current";
            } else if (settings_ready && action_key.rfind("setting-", 0) == 0) {
                const auto id = action_key.substr(8);
                const auto* definition = rt::FindSetting(id);
                const auto& value = settings_values.values.at(id);
                item.kind = definition->kind == rt::SettingKind::kText ? RowKind::Text : RowKind::Choice;
                item.title = definition->label;
                item.subtitle = value.available ? definition->effect : value.reason;
                if (modal.id == "button-mapping") item.subtitle.clear();  // the sheet title says it once
                if (id == "launch_option") item.value = settings_values.launch->label;
                else if (id.rfind("button.", 0) == 0) item.value = binding_label(value.value);
                else if (definition->kind == rt::SettingKind::kText) item.value = value.value.empty() ? "Empty" : value.value;
                else item.value = choice_label(*definition, value.value);
                item.badge = value.locked ? "Required by this game"
                    : definition->apply == rt::ApplyMode::kGameClosed ? "Game must be closed"
                    : definition->apply == rt::ApplyMode::kPreparesBottle ? "Prepares bottle" : "";
                item.enabled = value.available && !value.locked;
                item.modified = value.source == rt::ValueSource::kUser;
            } else if (settings_ready && action_key == "button-mapping") {
                item.kind = RowKind::Choice;
                item.title = "Button mapping";
                item.subtitle = "Keys and mouse clicks the controller buttons send.";
                int bound = 0;
                bool changed = false;
                for (const auto& button : rt::ProsperoButtons()) {
                    const auto& value = settings_values.values.at("button." + button);
                    bound += value.value.empty() || value.value == "none" ? 0 : 1;
                    changed = changed || value.source == rt::ValueSource::kUser;
                }
                item.value = bound == 0 ? "None" : std::to_string(bound) + (bound == 1 ? " button" : " buttons");
                item.modified = changed;
            } else if (settings_ready && (action_key == "section-more" || action_key == "section-advanced")) {
                item.kind = RowKind::Heading;
                const auto tier = action_key == "section-more" ? rt::SettingTier::kCompatibility : rt::SettingTier::kAdvanced;
                int count = 0;
                for (const auto& definition : rt::SettingsCatalogue())
                    if (definition.tier == tier && rt::SettingShown(definition, kit_)) ++count;
                if (tier == rt::SettingTier::kCompatibility) ++count;  // Button mapping
                item.value = expanded_settings_sections_.count(action_key) ? "Hide" : "Show " + std::to_string(count);
            } else if (settings_ready && !modal.setting_id.empty() && action_key.rfind("choice-", 0) == 0 && action_key != "choice-custom") {
                item.kind = RowKind::Choice;
                const auto index = static_cast<std::size_t>(std::stoul(action_key.substr(7)));
                const auto& recipe = RecipeFor(settings_game);
                std::string value;
                if (modal.setting_id == "launch_option") {
                    value = recipe.launch_entries[index].id;
                    item.title = recipe.launch_entries[index].label;
                } else if (modal.setting_id.rfind("button.", 0) == 0) {
                    value = common_bindings()[index].first;
                    item.title = common_bindings()[index].second;
                    if (index == 0) item.subtitle = "Use the preset or game binding: " + binding_label(inherited.value) + ".";
                } else {
                    const auto& choice = rt::FindSetting(modal.setting_id)->choices[index];
                    value = choice.value;
                    item.title = choice.label;
                    item.subtitle = choice.explanation;
                }
                const bool button = modal.setting_id.rfind("button.", 0) == 0;
                const auto& current = settings_values.values.at(modal.setting_id);
                item.selected = button && index == 0 ? current.source != rt::ValueSource::kUser
                    : value == current.value && !(button && current.source != rt::ValueSource::kUser);
                item.value = button ? (item.selected && index != 0 ? "Your choice" : "")
                    : value == inherited.value ? source_label(inherited.source) : item.selected ? "Your choice" : "";
            } else if (settings_ready && action_key == "choice-custom") {
                item.kind = RowKind::Text;
                item.title = "Other key…";
                item.subtitle = "Type a key name, a letter, F1–F24 or vk:0xNN.";
            } else if (settings_ready && action_key == "reset-settings") {
                item.enabled = settings_custom;
            } else if (settings_ready && action_key == "clear-shader-cache") {
                item.subtitle = "Shaders compile again on the next launch. Saves and settings stay.";
                item.badge = "Game must be closed";
                item.enabled = !live() && (state_.session == SessionState::Idle || state_.session_game_id != settings_game);
                if (live()) item.subtitle = "Available when the runtime maintenance service is connected.";
            } else if (action_key.rfind("volume-", 0) == 0) {
                item.kind = RowKind::Volume;
                if (const auto* volume = find_volume(state_, action_key.substr(7))) {
                    item.title = volume->label;
                    const auto occupied = std::min(volume->capacity_bytes, volume->used_bytes + volume->reserved_bytes);
                    if (volume->capacity_known)
                        item.value = std::to_string((volume->capacity_bytes - occupied) / (1024U * 1024U * 1024U)) + " GiB free";
                    else
                        item.value = format_bytes(volume->games_bytes) + " in games";
                    item.subtitle = !volume->available ? "Disconnected" : !volume->writable ? "Read-only" : "Available";
                    item.enabled = volume->available && volume->writable;
                    item.selected = volume->id == selected_volume_id_;
                }
            }
            else if (action_key == "quit-to-home") item.dangerous = true;  // the dialog's primary action
            else if (action_key == "kept-select-all") {
                const bool all = !state_.kept_bottles.empty() && kept_selected_.size() == state_.kept_bottles.size();
                item.title = all ? "Clear selection" : "Select all";
                item.enabled = !state_.kept_bottles.empty();
            }
            else if (action_key.rfind("kept-toggle-", 0) == 0) {
                const auto id = static_cast<GameId>(std::strtoull(action_key.c_str() + 12, nullptr, 10));
                for (const auto& kept : state_.kept_bottles) {
                    if (kept.game_id != id) continue;
                    item.kind = RowKind::Toggle;
                    item.title = kept_name(state_, kept);
                    item.value = format_bytes(kept.bytes);
                    // The console has no gmtime_r: the date is the log timestamp's day.
                    if (kept.kept_at > 0) item.subtitle = "Kept " + playden::app::FormatLogTimestamp(kept.kept_at).substr(0, 10);
                    item.selected = kept_selected_.contains(id);
                }
            }
            else if (action_key == "kept-selection-delete") {
                std::uint64_t bytes = 0;
                for (const auto& kept : state_.kept_bottles) if (kept_selected_.contains(kept.game_id)) bytes += kept.bytes;
                item.title = "Delete selected";
                item.dangerous = true;
                item.enabled = !kept_selected_.empty();
                if (!kept_selected_.empty()) item.value = kept_count(kept_selected_.size()) + " · " + format_bytes(bytes);
            }
            else if (action_key == "uninstall-delete-saves") {
                item.kind = RowKind::Toggle;
                item.dangerous = false;
                item.selected = uninstall_delete_saves_;
                item.subtitle = "Deletes the game's private Windows folder instead of keeping it. Saves in Steam Cloud stay there.";
            }
            else if (action_key == "verify" || action_key == "uninstall") {
                if (!is_installed(state_, state_.navigation.selected_game_id)) { item.enabled = false; item.subtitle = "Game is not installed"; }
            } else if (action_key == "hide") item.title = state_.library.hidden.contains(state_.navigation.selected_game_id) ? "Unhide" : "Hide";
            else if (action_key == "runtime-retry") item.subtitle = state_.runtime == RuntimeState::Ready ? "Ready — simulated" : state_.runtime == RuntimeState::Checking ? "Checking — simulated" : state_.runtime == RuntimeState::Failed ? "Failed — retry available" : "Unavailable — retry available";
            else if (action_key == "account-auth") { item.kind = RowKind::Status; item.value = auth_name(state_.account.authentication); item.subtitle = state_.account.error; item.enabled = false; }
            else if (action_key == "account-last-sync") { item.kind = RowKind::Status; item.value = state_.account.last_sync == 0 ? "Never" : std::to_string(state_.account.last_sync); item.enabled = false; }
            else if (action_key.rfind("open-game:", 0) == 0) item.title = "Open game";
            else if (action_key == "about-build") { item.kind = RowKind::Status; item.value = live() ? "Live service model" : "UI comparison fixture"; item.enabled = false; }
            else if (action_key == "about-framework") { item.kind = RowKind::Status; item.value = "Homebrew native model + Hui"; item.enabled = false; }
            else if (action_key == "about-platform") { item.kind = RowKind::Status; item.value = "Host / PS5 homebrew"; item.enabled = false; }
            else if (action_key == "install-confirm" || action_key == "install-estimate-retry") {
                const auto assessment = assess_install(state_, state_.navigation.selected_game_id, selected_volume_id_);
                if (action_key == "install-estimate-retry") item.enabled = assessment.readiness == InstallReadiness::UnknownSize;
                else {
                    item.enabled = assessment.readiness == InstallReadiness::Ready && assessment.download_bytes.has_value();
                    item.subtitle = assessment.readiness == InstallReadiness::UnknownSize ? "Fetching size estimate" : assessment.readiness == InstallReadiness::Insufficient ? "Insufficient space" : assessment.readiness == InstallReadiness::Unavailable ? "Storage disconnected" : assessment.readiness == InstallReadiness::ReadOnly ? "Storage is read-only" : "Creates one download job";
                }
            }
            else if (action_key.rfind("rating-", 0) == 0) {
                item.kind = RowKind::Choice;
                const auto value = action_key.substr(7);
                item.title = value == "works" ? "Works" : value == "playable" ? "Playable" : value == "broken" ? "Broken" : "Untested";
                const auto rating = state_.library.ratings.find(state_.navigation.selected_game_id);
                const auto current = rating == state_.library.ratings.end() ? Compatibility::Untested : rating->second;
                item.selected = (value == "works" && current == Compatibility::Works) || (value == "playable" && current == Compatibility::Playable)
                    || (value == "broken" && current == Compatibility::Broken) || (value == "untested" && current == Compatibility::Untested);
            } else if (action_key.rfind("collection-", 0) == 0 && action_key != "collection-new") {
                const char* prefixes[] = {"collection-toggle-", "collection-rename-", "collection-pin-", "collection-delete-"};
                const char* verbs[] = {"Membership", "Rename", "Pin on Home", "Delete"};
                for (std::size_t index = 0; index < 4; ++index) if (action_key.rfind(prefixes[index], 0) == 0) {
                    const auto id = action_key.substr(std::char_traits<char>::length(prefixes[index]));
                    const auto found = std::find_if(state_.library.collections.begin(), state_.library.collections.end(), [&id](const Collection& value) { return value.id == id; });
                    if (found != state_.library.collections.end()) {
                        item.title = std::string(verbs[index]) + ": " + found->name;
                        if (index == 0) item.selected = found->members.contains(state_.navigation.selected_game_id);
                        if (index == 2) item.selected = found->pinned;
                        if (index == 3) item.dangerous = true;
                    }
                }
            }
        }
        view_.focused_row = modal.focused_action;
        view_.legends = {"Cross Select", "Circle Cancel"};
        if (modal.id == "game-settings") view_.legends = {"Cross Change", "Triangle Reset", "Circle Close"};
        else if (!modal.setting_id.empty()) view_.legends = {"Cross Select", "Triangle Reset", "Circle Back"};
    } else if (state_.navigation.route == Route::Startup) {
        view_.title = "Welcome to Playden";
        view_.subtitle = live() ? "Connect Steam to load your library." : "Browse the local UI fixture or connect a simulated Steam account.";
        if (state_.startup_loading || state_.account.authentication == AuthState::Restoring) {
            row("startup-loading", RowKind::Status, live() ? "Restoring Steam session…" : "Loading saved fixture state…", "").enabled = false;
            if (!live()) row("startup-advance", RowKind::Action, "Advance fixture clock", "advance-clock");
        }
        else if (!state_.persistence_available) {
            row("startup-retry-save", RowKind::Action, "Retry saved state", "retry-save");
            row("startup-continue", RowKind::Action, "Continue without saving", "continue-without-saving");
        } else {
            row("startup-connect", RowKind::Action, "Connect Steam", "connect");
            row("startup-skip", RowKind::Action, "Skip for now", "skip");
        }
    } else if (state_.navigation.route == Route::SignIn) {
        view_.title = "Connect Steam";
        view_.subtitle = state_.account.authentication == AuthState::Waiting
            ? (live() ? "Scan the QR code with the Steam mobile app." : "Simulated QR challenge — not a working Steam code")
            : state_.account.authentication == AuthState::Syncing ? "Loading your library…" : state_.account.error;
        if (state_.account.authentication == AuthState::Syncing) {
            if (live()) row("sync-status", RowKind::Status, "Loading library…", "").enabled = false;
            else row("sync-advance", RowKind::Action, "Advance fixture sync", "advance-clock");
        }
        else if (state_.account.authentication == AuthState::Expired) row("qr-refresh", RowKind::Action, "Refresh code", "qr-refresh");
        else if (state_.account.authentication == AuthState::Failed) row("qr-retry", RowKind::Action, "Retry", "qr-retry");
        else row("qr-cancel", RowKind::Action, "Cancel", "qr-cancel");
        if (!live() && state_.account.authentication != AuthState::Syncing) row("qr-fixture-controls", RowKind::Action, "QR fixture controls", "qr-fixture-controls");
    } else if (state_.navigation.route == Route::Home) {
        view_.title = "Home";
        view_.subtitle = state_.account.authentication == AuthState::SignedIn
            ? (live() ? (state_.account.error.empty() ? "Your Steam library" : state_.account.error) : "Fixture history — simulated")
            : state_.account.authentication == AuthState::Offline
                ? (state_.account.error.empty() ? "Offline — showing available library" : state_.account.error)
                : "Signed out";
        for (const auto* game : visible_games()) {
            if (view_.rows.size() >= 12) break;
            auto& item = row(game_key(game->id), RowKind::Game, game->title, "open-game");
            item.game_id = game->id;
            item.badge = state_.library.favorites.contains(game->id) ? "Favorite" : "";
        }
        if (view_.rows.empty()) {
            row("home-library", RowKind::Action, "Open Library", "library");
            if (state_.account.authentication != AuthState::SignedIn) row("home-connect", RowKind::Action, "Connect Steam", "connect");
        }
        auto add_shelf = [this](const playden::homebrew::Shelf& source, int shelf_index) {
            ViewData::Shelf shelf;
            shelf.key = source.key; shelf.title = source.title;
            for (const auto id : source.game_ids) if (const auto* game = find_game(state_, id)) {
                ViewRow item;
                item.key = "shelf-" + shelf.key + "-game-" + std::to_string(id); item.kind = RowKind::Game; item.title = game->title; item.action = "open-game"; item.game_id = id;
                shelf.items.push_back(std::move(item));
            }
            if (source.more > 0 && !shelf.items.empty()) {
                ViewRow more;
                more.key = playden::homebrew::shelf_more_key(source); more.kind = RowKind::Action;
                more.title = "See all in Library"; more.value = "+" + std::to_string(source.more); more.action = "shelf-more";
                shelf.items.push_back(std::move(more));
            }
            if (!shelf.items.empty()) {
                const auto saved = state_.navigation.home_shelf_focus.find(shelf.key);
                if (shelf_index == state_.navigation.home_shelf)
                    shelf.focused_item = saved == state_.navigation.home_shelf_focus.end() ? 0 : std::clamp(saved->second, 0, static_cast<int>(shelf.items.size()) - 1);
                view_.shelves.push_back(std::move(shelf));
            }
        };
        int shelf_index = 0;
        for (const auto& shelf : playden::homebrew::shelves(state_)) add_shelf(shelf, shelf_index++);
        view_.legends = {"Cross Open", "Square Favorite", "Options Actions", "L1/R1 Sections"};
    } else if (state_.navigation.route == Route::Library) {
        view_.title = "Library";
        const auto games = visible_games();
        view_.total_count = games.size();
        view_.subtitle = std::to_string(games.size()) + " games";
        if (state_.library_loading) view_.subtitle += live() ? " · Refreshing library…" : " · Refreshing fixture library…";
        else if (live() && state_.library_offline) view_.subtitle += " · Cached library";
        if (live() && !state_.account.error.empty()) view_.subtitle += " · " + state_.account.error;
        const std::size_t window_size = 24;
        const std::size_t selected = static_cast<std::size_t>(std::max(0, state_.navigation.selected_index));
        view_.first_index = games.size() <= window_size ? 0 : std::min(selected > window_size / 2 ? selected - window_size / 2 : 0, games.size() - window_size);
        const std::size_t end = std::min(games.size(), view_.first_index + window_size);
        for (std::size_t index = view_.first_index; index < end; ++index) {
            const auto* game = games[index];
            auto& item = row(game_key(game->id), RowKind::Game, game->title, "open-game");
            item.game_id = game->id;
            if (state_.library.favorites.contains(game->id)) item.badge = "Favorite";
            if (game->art_failed) item.subtitle = "Artwork unavailable";
        }
        if (view_.rows.empty()) row(state_.library.query.empty() ? "library-empty" : "clear-search", RowKind::Action,
                                    state_.library.query.empty() ? "Connect Steam" : "Clear search", state_.library.query.empty() ? "connect" : "clear-search");
        view_.legends = {"Cross Open", "Square Favorite", "Options Actions", "Triangle Filters", "Search"};
    } else if (state_.navigation.route == Route::Details) {
        const auto* game = find_game(state_, state_.navigation.detail_id.value_or(state_.navigation.selected_game_id));
        view_.title = game ? game->title : "Game unavailable";
        view_.subtitle = game ? game->description : "The selected fixture no longer exists.";
        Installation* installation = game ? find_installation(state_, game->id) : nullptr;
        // Live installations are only finished installs; their size may be unknown on an absent drive.
        const bool installed = installation && (live() || installation->installed_bytes > 0);
        if (!live()) {
            auto& primary = row(installed ? "details-play" : "details-install", RowKind::Action,
                                installed ? (installation->runtime_ready ? "Play" : "Runtime unavailable") : "Install",
                                installed ? (installation->runtime_ready ? "launch" : "runtime-status") : "install");
            primary.enabled = true;
        }
        if (live() && game) {
            // Real install state: an install job, a finished install, or what Steam allows.
            const auto* job = find_job(state_, std::to_string(game->id));
            if (installed) {
                const auto* volume = find_volume(state_, installation->volume_id);
                const std::string label = volume ? volume->label : "its storage";
                auto& status = row("details-installed", RowKind::Status,
                                   installation->available ? "Installed" : "Storage disconnected", "");
                status.value = installation->installed_bytes > 0 ? format_bytes(installation->installed_bytes) : "";
                status.subtitle = installation->available ? "On " + label : "Connect " + label + " to use this game";
                if (installation->available) {
                    // Play the game's own PS5 title. A game that is not prepared yet is prepared
                    // first and then starts, so the player never has to prepare it by hand.
                    const auto play_game = state_.runtime_games.find(game->id);
                    if (play_game != state_.runtime_games.end()) {
                        const auto& value = play_game->second;
                        const bool prepares_first = !value.ready && value.can_prepare && value.status == "Not prepared";
                        const bool enabled = value.can_play || prepares_first || value.awaiting_registration;
                        auto& play = row("details-play", RowKind::Action, "Play",
                                         value.awaiting_registration ? "quit-to-home" : enabled ? "runtime-play" : "");
                        // Otherwise Play names what holds it (Preparing, Update available…); the PS5 title row says why.
                        play.value = !value.play_status.empty() || value.ready || prepares_first ? value.play_status
                                                                                                 : value.status;
                        if (!value.uninstall_status.empty()) {
                            play.value = value.uninstall_status;
                            play.action.clear();
                            play.enabled = false;
                        }
                        play.subtitle = !value.play_detail.empty() ? value.play_detail
                                      : prepares_first ? "The first start sets the game up and adds it to the PS5 Home screen"
                                                       : std::string{};
                        if (value.awaiting_registration) {
                            play.value = "Ready to play";
                            play.subtitle = "Play quits to the PS5 Home screen, where the game appears";
                        }
                        play.enabled = enabled && value.uninstall_status.empty();
                        // A copy or deletion with a known fraction shows a bar; other steps only their words.
                        if (value.progress) play.progress = std::max(*value.progress, 0.001F);
                    }
                    // Steam Cloud saves: sync on demand; a pending choice opens its dialog.
                    const auto cloud = state_.cloud.find(game->id);
                    const auto cloud_status = cloud == state_.cloud.end() ? CloudStatus::None : cloud->second.status;
                    const bool busy = cloud_status == CloudStatus::Checking || cloud_status == CloudStatus::Syncing;
                    const bool choice = cloud_status == CloudStatus::Conflict || cloud_status == CloudStatus::NeedsAccount;
                    auto& saves = row("details-cloud", busy ? RowKind::Status : RowKind::Action, "Cloud saves",
                                      busy ? "" : choice ? "cloud-choose" : "cloud-sync");
                    saves.value = cloud_status == CloudStatus::None        ? "Sync"
                                  : busy                                  ? (cloud_status == CloudStatus::Checking ? "Checking…" : "Syncing…")
                                  : choice                                ? "Choose"
                                  : cloud_status == CloudStatus::Failed   ? "Retry"
                                  : cloud_status == CloudStatus::Unsupported ? "Unavailable"
                                                                          : "Synced";
                    if (cloud != state_.cloud.end()) saves.subtitle = cloud->second.message;
                    saves.enabled = !busy;
                    // Per-game runtime: a private bottle and the game's own PS5 title.
                    const auto runtime_game = state_.runtime_games.find(game->id);
                    if (runtime_game != state_.runtime_games.end()) {
                        const auto& value = runtime_game->second;
                        // Always a button so its state stays visible; disabled while nothing can be done.
                        // An interrupted removal continues through Uninstall, then removes the download.
                        const bool removal = value.status == "Removal incomplete";
                        auto& runtime = row("details-runtime", RowKind::Action, "PS5 title",
                                            !value.can_prepare ? "" : removal ? "runtime-remove-retry" : "runtime-prepare");
                        runtime.value = !value.can_prepare ? value.status
                                      : removal ? "Retry"
                                      : value.status == "Needs recovery" ? "Resume"
                                      : value.status == "Registration pending" || value.awaiting_registration ? "Check again"
                                      : value.status == "Failed" ? "Retry"
                                      : value.status == "Update available" ? "Update"
                                      : value.status == "Needs repair" ? "Repair" : "Prepare";
                        runtime.subtitle = value.detail;
                        runtime.enabled = value.can_prepare;
                        if (value.progress) runtime.progress = std::max(*value.progress, 0.001F);
                    }
                }
            } else if (job) {
                auto& status = row("details-download", RowKind::Action, "Download", "open-downloads");
                status.value = job_state_name(job->state);
                status.progress = job->total_bytes == 0 ? 0.0F : static_cast<float>(job->completed_bytes) / static_cast<float>(job->total_bytes);
            } else if (game->windows_installable.value_or(false)) {
                auto& install = row("details-install", RowKind::Action, "Install", "live-install");
                install.value = format_bytes(game->download_bytes.value_or(0));
                if (state_.volumes.size() > 1)
                    if (const auto* volume = find_volume(state_, selected_volume_id_)) install.subtitle = "To " + volume->label;
            } else if (game->windows_installable.has_value()) {
                row("details-unavailable", RowKind::Status, "No Windows version to install", "").enabled = false;
            } else {
                row("details-checking", RowKind::Status, "Checking Steam…", "").enabled = false;
            }
            // Saves kept when the game was uninstalled: Install brings them back.
            if (!installed)
                for (const auto& kept : state_.kept_bottles) {
                    if (kept.game_id != game->id) continue;
                    auto& saves = row("details-kept", RowKind::Status, "Saves from a previous install", "");
                    saves.value = format_bytes(kept.bytes);
                    saves.subtitle = "Installing the game again brings them back.";
                    saves.enabled = false;
                    row("details-kept-delete", RowKind::Action, "Delete saves", "kept-delete-" + std::to_string(game->id)).dangerous = true;
                }
        }
        row("details-favorite", RowKind::Toggle, "Favorite", "favorite").selected = game && state_.library.favorites.contains(game->id);
        if (!live()) {
            row("details-settings", RowKind::Action, "Game settings", "game-settings");
            row("details-simulate-launch", RowKind::Action, "Simulate launch", "launch");
        }
        row("details-more", RowKind::Action, "More", "context");
        view_.legends = {"Cross Select", "Options Actions", "Circle Back"};
    } else if (state_.navigation.route == Route::Downloads) {
        view_.title = "Downloads";
        std::vector<const Job*> jobs;
        for (const auto& job : state_.jobs) if (job.state != JobState::Cancelled) jobs.push_back(&job);
        std::stable_sort(jobs.begin(), jobs.end(), [](const Job* left, const Job* right) { return left->queue_order < right->queue_order; });
        for (const auto* job_pointer : jobs) {
            const auto& job = *job_pointer;
            if (job.state == JobState::Cancelled) continue;
            const auto* game = find_game(state_, job.game_id);
            auto& item = row("job-" + job.id, RowKind::Job, game ? game->title : job.id, "job-action");
            item.game_id = job.game_id;
            item.subtitle = job.failure;
            item.value = job_state_name(job.state);
            item.progress = job.total_bytes == 0 ? 0.0F : static_cast<float>(job.completed_bytes) / static_cast<float>(job.total_bytes);
        }
        if (view_.rows.empty()) row("downloads-empty", RowKind::Status, "No active downloads", "");
        view_.legends = {"Cross Actions", "Options Actions", "L1/R1 Sections"};
    } else if (state_.navigation.route == Route::Settings) {
        view_.title = "Settings";
        if (live()) view_.subtitle = state_.account.error;
        // Rows come grouped by the macOS sections; the rail shows the focused row's section.
        std::string section = "Account";
        const auto add = [&](std::string key, RowKind kind, std::string title, std::string action) -> ViewRow& {
            auto& added = row(std::move(key), kind, std::move(title), std::move(action));
            added.section = section;
            return added;
        };
        auto& account = add("settings-account", RowKind::Action, "Account", "account");
        account.value = state_.account.display_name;
        account.subtitle = state_.account.authentication == AuthState::Offline ? "Cached library · Offline"
            : state_.account.authentication == AuthState::SignedIn
                ? (!live() || state_.account.last_sync > 0 ? "Steam connected · Synced" : "Steam connected")
                : "Connect Steam";
        if (state_.account.authentication != AuthState::SignedOut)
            add("settings-signout", RowKind::Action, "Sign out", "signout-confirm").dangerous = true;
        section = "Library";
        auto& storage = add("settings-storage", RowKind::Action, "Library and storage", "storage-manager");
        if (live()) {
            const auto* volume = find_volume(state_, selected_volume_id_);
            storage.title = "Games storage";
            storage.value = volume ? volume->label : "";
            storage.subtitle = "Where new games are installed";
            if (state_.volumes.empty()) { storage.kind = RowKind::Status; storage.action.clear(); storage.enabled = false; storage.subtitle = "Storage is unavailable"; }
        }
        auto& runtime = add("settings-runtime", RowKind::Action, "Runtime status", "settings-runtime");
        runtime.value = state_.runtime == RuntimeState::Ready ? (live() ? "Ready" : "Ready — simulated") : state_.runtime == RuntimeState::Checking ? (live() ? "Checking" : "Checking — simulated") : state_.runtime == RuntimeState::Failed ? (live() ? "Failed" : "Failed — simulated") : (live() ? "Unavailable" : "Unavailable — simulated");
        if (live()) { runtime.subtitle = "Games run through prospero-win"; runtime.kind = RowKind::Status; runtime.action.clear(); runtime.enabled = false; }
        if (live()) {
            auto& suffix = add("settings-title-store", RowKind::Toggle, "Store name in PS5 titles", "toggle-title-store");
            suffix.selected = state_.title_store_suffix;
            suffix.subtitle = "Name game titles like \"Half-Life 2 (Steam)\". Prepared games change with their next Update.";
        }
        if (live()) {
            // Saves and settings kept from uninstalled games: one row that opens the manager,
            // where the player picks which to delete to reclaim their space.
            auto& kept = add("settings-kept", RowKind::Action, "Kept game data", "kept-manage");
            kept.subtitle = "Saves and settings kept when a game is uninstalled";
            if (state_.kept_bottles.empty()) {
                kept.kind = RowKind::Status; kept.action.clear(); kept.enabled = false; kept.value = "None";
            } else {
                std::uint64_t bytes = 0;
                for (const auto& item : state_.kept_bottles) bytes += item.bytes;
                kept.value = kept_count(state_.kept_bottles.size()) + " · " + format_bytes(bytes);
            }
        }
        if (live() && state_.runtime_leftover_requests > 0) {
            // Explicit recovery: settings requests no game is using any more.
            auto& leftovers = add("settings-runtime-cleanup", RowKind::Action, "Unused game settings data", "runtime-cleanup");
            leftovers.value = std::to_string(state_.runtime_leftover_requests);
            leftovers.subtitle = "Left by interrupted or finished preparations. Clean up removes only what no game is using.";
        }
        section = "Display";
        auto& motion = add("settings-motion", RowKind::Toggle, "Reduced motion", "toggle-motion");
        motion.selected = state_.reduced_motion;
        motion.subtitle = "Keep the focus ring; turn off scaling and transitions";
        section = "Controller";
        auto& controller = add("settings-controller", RowKind::Action, "Controller", "controller-test");
        controller.value = state_.controller_connected ? "Connected" : "Disconnected";
        controller.subtitle = "Test the buttons and sticks";
        section = "About";
        add("settings-about", RowKind::Action, "About", "about").subtitle = "Playden for PlayStation 5";
        auto& logs = add("settings-logs", RowKind::Action, "Logs", live() ? "logs-pick" : "logs");
        if (live()) {
            logs.subtitle = "Install, Cloud saves, sign-in and launch diagnostics";
            if (log_directory_.empty()) { logs.kind = RowKind::Status; logs.action.clear(); logs.enabled = false; logs.subtitle = "No logs on this device"; }
        }
        if (!state_.notices.empty()) add("settings-notices", RowKind::Action, "Warnings", "notices").value = std::to_string(state_.notices.size());
        if (!live()) {
            add("settings-simulation", RowKind::Action, "Simulation controls", "simulation-controls");
            add("settings-scenarios", RowKind::Action, "Fixture scenarios", "scenario-picker");
        }
        auto& reset = add("settings-reset", RowKind::Action, "Reset UI data", "reset-confirm");
        reset.dangerous = true;
        reset.subtitle = "Forget favorites, collections and view state on this console";
        section = "Quit Playden";
        // As on macOS, the rail entry itself quits: the section has no rows to review.
        auto& quit = add("settings-quit", RowKind::Action, "Quit Playden", "quit-confirm");
        quit.dangerous = true;
        quit.rail_only = true;
    } else if (state_.navigation.route == Route::Session) {
        view_.title = state_.session == SessionState::Running ? "Game running" : "Simulated session";
        view_.subtitle = state_.session_error;
        if (state_.session == SessionState::Running) {
            row("session-return", RowKind::Action, "Return to game", "return-game");
            row("session-quit", RowKind::Action, "Quit game", "quit-game");
        } else {
            if (state_.session_error == "Game did not stop") row("session-force-stop", RowKind::Action, "Force stop", "force-stop").dangerous = true;
            else row("session-retry", RowKind::Action, "Retry", "launch");
            row("session-logs", RowKind::Action, "Open logs", "logs");
            row("session-back", RowKind::Action, "Back to details", "details-back");
        }
        row("session-fixture-controls", RowKind::Action, "Session fixture controls", "session-fixture-controls");
    }
    // Live Details opens on Favorite while Steam's app info is still loading; once it arrives
    // focus moves to the first action (Install, the download, or Cloud saves), as on macOS
    // where the primary action is focused. An installed game waits for Play: while the runtime
    // is still checking, Play is absent or disabled, and focus moves to it once it is enabled.
    // Only until the user moves.
    if (details_primary_pending_ && state_.navigation.route == Route::Details && !has_overlay) {
        const auto first = std::find_if(view_.rows.begin(), view_.rows.end(), [](const ViewRow& item) {
            return item.enabled && (item.kind == RowKind::Action || item.kind == RowKind::Toggle);
        });
        if (first != view_.rows.end()) state_.navigation.focus_key = first->key;
        const auto has = [this](const char* key) {
            return std::any_of(view_.rows.begin(), view_.rows.end(), [key](const ViewRow& item) { return item.key == key; });
        };
        const auto play = std::find_if(view_.rows.begin(), view_.rows.end(), [](const ViewRow& item) { return item.key == "details-play"; });
        const bool awaiting_play = has("details-installed") && (play == view_.rows.end() || !play->enabled);
        if (!has("details-checking") && !awaiting_play) details_primary_pending_ = false;
    }
    auto focused = std::find_if(view_.rows.begin(), view_.rows.end(), [this](const ViewRow& item) { return item.key == state_.navigation.focus_key; });
    // Settings: the focused row went away (a dialog deleted it, or its data changed). Focus
    // the nearest row left in the same section, never the rail-only Quit entry.
    if (focused == view_.rows.end() && !has_overlay && state_.navigation.route == Route::Settings &&
        !state_.navigation.settings_rail_focused && !settings_focus_section_.empty() && !view_.rows.empty() &&
        state_.navigation.focus_key.rfind("settings-", 0) == 0) {
        const int last = static_cast<int>(view_.rows.size()) - 1;
        const int origin = std::clamp(settings_focus_index_, 0, last);
        int best = -1;
        for (int distance = 0; distance <= last && best < 0; ++distance)
            for (const int candidate : {origin - distance, origin + distance}) {
                if (candidate < 0 || candidate > last) continue;
                const auto& item = view_.rows[static_cast<std::size_t>(candidate)];
                if (item.section == settings_focus_section_ && !item.rail_only) { best = candidate; break; }
            }
        for (int distance = 0; distance <= last && best < 0; ++distance)
            for (const int candidate : {origin - distance, origin + distance})
                if (candidate >= 0 && candidate <= last && !view_.rows[static_cast<std::size_t>(candidate)].rail_only) { best = candidate; break; }
        if (best >= 0) {
            state_.navigation.selected_index = best;
            state_.navigation.focus_key = view_.rows[static_cast<std::size_t>(best)].key;
            focused = view_.rows.begin() + best;
        }
    }
    if (focused != view_.rows.end()) view_.focused_row = static_cast<int>(std::distance(view_.rows.begin(), focused));
    else if (!view_.rows.empty() && !(state_.navigation.route == Route::Library && state_.navigation.library_rail_focused)
             && !(state_.navigation.route == Route::Home && !view_.shelves.empty())) {
        view_.focused_row = std::clamp(state_.navigation.selected_index, 0, static_cast<int>(view_.rows.size()) - 1);
        if (state_.navigation.route == Route::Library && view_.first_index > 0)
            view_.focused_row = std::clamp(state_.navigation.selected_index - static_cast<int>(view_.first_index), 0, static_cast<int>(view_.rows.size()) - 1);
        state_.navigation.focus_key = view_.rows[static_cast<std::size_t>(view_.focused_row)].key;
        if (!has_overlay && view_.rows[static_cast<std::size_t>(view_.focused_row)].game_id != 0)
            state_.navigation.selected_game_id = view_.rows[static_cast<std::size_t>(view_.focused_row)].game_id;
    }
    if (!has_overlay && state_.navigation.route == Route::Settings && view_.focused_row >= 0 &&
        static_cast<std::size_t>(view_.focused_row) < view_.rows.size()) {
        settings_focus_index_ = view_.focused_row;
        settings_focus_section_ = view_.rows[static_cast<std::size_t>(view_.focused_row)].section;
    }
    if (!has_overlay) route_view_ = view_;
}

void Simulation::open_modal(ModalEntry modal) {
    if (!state_.navigation.modals.empty()) modal.opener_focus_key = state_.navigation.modals.back().action_keys[static_cast<std::size_t>(state_.navigation.modals.back().focused_action)];
    state_.navigation.modals.push_back(std::move(modal));
    state_.navigation.focus_key = state_.navigation.modals.back().action_keys[static_cast<std::size_t>(state_.navigation.modals.back().focused_action)];
}

void Simulation::close_modal() {
    if (state_.navigation.modals.empty()) return;
    const auto opener = state_.navigation.modals.back().opener_focus_key;
    state_.navigation.modals.pop_back();
    state_.navigation.focus_key = opener;
}

void Simulation::start_logs() {
    state_.log_state = LogState::Loading;
    state_.log_lines.clear();
    state_.log_offset = 0;
    if (state_.navigation.route == Route::Session || state_.session != SessionState::Idle)
        state_.log_context = "Session fixture · game " + std::to_string(state_.session_game_id);
    else if (state_.navigation.route == Route::Downloads)
        state_.log_context = "Transfer fixture · game " + std::to_string(state_.navigation.selected_game_id);
    else if (state_.navigation.selected_game_id != 0)
        state_.log_context = "Game fixture · " + std::to_string(state_.navigation.selected_game_id);
    else state_.log_context = "Playden fixture diagnostics";
}

void Simulation::SetLogDirectory(std::filesystem::path directory) {
    log_directory_ = std::move(directory);
    rebuild_view();
}

void Simulation::open_live_log(const std::string& topic, GameId game_id) {
    live_log_topic_ = topic;
    live_log_game_ = game_id;
    std::vector<std::string> actions;
    if (game_id != 0)
        if (const auto* job = find_job(state_, std::to_string(game_id)); job && job->state == JobState::Failed)
            actions.push_back("job-retry-" + job->id);
    actions.push_back("logs-refresh");
    actions.push_back("logs-back");
    const std::string title = topic == "auth" ? "Sign-in log" : topic == "launch" ? "Launch log"
                              : topic == "cloud"                       ? "Cloud saves log"
                                                                       : "Install log";
    load_live_log();
    open_modal({"logs", title, state_.navigation.focus_key, std::move(actions), 0, false});
}

// Reads the newest lines of the chosen log; a game's view keeps only that game's install lines.
void Simulation::load_live_log() {
    state_.log_lines.clear();
    state_.log_offset = 0;
    if (log_directory_.empty() || live_log_topic_.empty()) {
        state_.log_state = LogState::Failed;
        state_.log_context = "Logs are not available on this device";
        return;
    }
    const auto tail = playden::app::ReadLogTail((log_directory_ / (live_log_topic_ + ".log")).string());
    if (tail.read_error) {
        state_.log_state = LogState::Failed;
        state_.log_context = "The log file could not be read";
        return;
    }
    const std::string marker = "app=" + std::to_string(live_log_game_) + " ";
    for (const auto& line : tail.lines)
        if (live_log_game_ == 0 || line.find(marker) != std::string::npos) state_.log_lines.push_back(line);
    state_.log_state = LogState::Ready;
    std::string context;
    if (live_log_game_ != 0) {
        const auto* game = find_game(state_, live_log_game_);
        context = (game ? game->title : "This game") + std::string(" · ");
    }
    context += state_.log_lines.empty() ? std::string("nothing logged yet")
                                        : std::to_string(state_.log_lines.size()) + " lines, newest last";
    if (tail.truncated) context += " · older lines omitted";
    state_.log_context = std::move(context);
    // Open at the newest lines.
    state_.log_offset = std::max(0, static_cast<int>(state_.log_lines.size()) - 8);
}

void Simulation::finish_logs() {
    state_.log_state = LogState::Ready;
    state_.log_lines.clear();
    state_.log_lines.reserve(32);
    for (int index = 0; index < 32; ++index) {
        state_.log_lines.push_back("[fixture " + std::to_string(index + 1) + "] deterministic event for " + state_.log_context);
    }
    if (state_.log_lines.size() > 128) state_.log_lines.erase(state_.log_lines.begin(), state_.log_lines.end() - 128);
    state_.log_offset = 0;
}

std::string Simulation::next_job_id() {
    for (;;) {
        const auto id = "job-" + std::to_string(next_job_serial_++);
        if (!find_job(state_, id)) return id;
    }
}

void Simulation::start_editor(std::string purpose, std::string value) {
    const auto opener = state_.navigation.focus_key;
    editing_setting_id_.clear();
    state_.editor = {};
    state_.editor.active = true;
    state_.editor.purpose = std::move(purpose);
    state_.editor.value = value;
    state_.editor.original = std::move(value);
    state_.editor.opener_focus_key = opener;
    state_.editor.caret = state_.editor.value.size();
    state_.navigation.focus_key = "keyboard-done";
}

void Simulation::commit_editor() {
    if (state_.editor.purpose == "Search") { state_.library.query = state_.editor.value; reconcile_library_selection(state_); }
    else if (!editing_setting_id_.empty()) {
        if (!can_change_game_settings()) return;
        const auto value = trimmed(state_.editor.value);
        std::string reason;
        if (!value.empty() && !rt::ValidateSettingValue(RecipeFor(state_.navigation.selected_game_id), editing_setting_id_, value, &reason)) {
            state_.notices.push_back({"setting-validation", state_.editor.purpose + ": " + reason, "", true, 0, true});
            return;
        }
        state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(),
                                            [](const Notice& notice) { return notice.id == "setting-validation"; }),
                             state_.notices.end());
        set_game_setting(editing_setting_id_, value);
        editing_setting_id_.clear();
        state_.editor.active = false;
        state_.navigation.focus_key = state_.editor.opener_focus_key;
        refresh_game_settings();
        save_profile();
        return;
    }
    else if (state_.editor.purpose == "Note") state_.library.notes[state_.navigation.selected_game_id] = state_.editor.value;
    else if (state_.editor.purpose == "Collection name") {
        const auto name = trimmed(state_.editor.value);
        auto duplicate = std::find_if(state_.library.collections.begin(), state_.library.collections.end(), [&name](const Collection& value) { return value.name == name; });
        if (name.empty() || duplicate != state_.library.collections.end()) {
            state_.notices.push_back({"collection-validation", "Collection names must be non-empty and unique", "", true, 0});
            return;
        }
        std::string id;
        do { id = "collection-" + std::to_string(next_collection_serial_++); }
        while (std::any_of(state_.library.collections.begin(), state_.library.collections.end(), [&id](const Collection& value) { return value.id == id; }));
        state_.library.collections.push_back({std::move(id), name, false, {}});
    } else if (state_.editor.purpose == "Rename collection") {
        const auto name = trimmed(state_.editor.value);
        const auto duplicate = std::find_if(state_.library.collections.begin(), state_.library.collections.end(), [this, &name](const Collection& value) { return value.name == name && value.id != editing_target_id_; });
        const auto target = std::find_if(state_.library.collections.begin(), state_.library.collections.end(), [this](const Collection& value) { return value.id == editing_target_id_; });
        if (name.empty() || duplicate != state_.library.collections.end() || target == state_.library.collections.end()) {
            state_.notices.push_back({"collection-validation", "Collection names must be non-empty and unique", "", true, 0});
            return;
        }
        target->name = name;
    }
    state_.editor.active = false;
    state_.navigation.focus_key = state_.editor.opener_focus_key;
    save_profile();
}

void Simulation::cancel() {
    if (state_.navigation.route == Route::Settings && state_.navigation.settings_rail_focused &&
        state_.navigation.modals.empty() && !state_.editor.active) {
        state_.navigation.settings_rail_focused = false;
        return;
    }
    if (state_.editor.active) { state_.editor.active = false; state_.navigation.focus_key = state_.editor.opener_focus_key; return; }
    if (!state_.navigation.modals.empty()) {
        const bool crash_logs = state_.navigation.modals.back().id == "logs" && state_.session == SessionState::Crashed;
        const bool settings_sheet = state_.navigation.modals.back().id == "game-settings";
        close_modal();
        if (settings_sheet) announce_settings_saved();
        if (crash_logs) {
            state_.navigation.route = Route::Details;
            state_.navigation.detail_id = state_.session_game_id;
            state_.navigation.selected_game_id = state_.session_game_id;
            state_.navigation.focus_key = "details-simulate-launch";
        }
        return;
    }
    if (state_.navigation.route == Route::SignIn) {
        if (live()) queue_service_action(ServiceAction::CancelQr);
        cancelled_challenge_generation_ = challenge_generation_;
        state_.account.authentication = AuthState::Cancelled;
        state_.navigation.route = Route::Startup;
        state_.navigation.focus_key = "startup-connect";
    } else if (state_.navigation.route == Route::Details || state_.navigation.route == Route::Session) {
        state_.navigation.route = state_.navigation.route == Route::Session ? Route::Details : state_.navigation.detail_origin;
        state_.navigation.focus_key = state_.navigation.route == Route::Details ? "details-play" : game_key(state_.navigation.selected_game_id);
    } else if (state_.navigation.route != Route::Home) {
        state_.navigation.route = Route::Home;
        state_.navigation.focus_key = game_key(state_.navigation.selected_game_id);
    }
}

void Simulation::move(int delta_x, int delta_y) {
    if (state_.editor.active) {
        state_.editor.focus_row = std::clamp(state_.editor.focus_row + delta_y, 0, 4);
        const int max_key = state_.editor.focus_row == 4 ? (state_.editor.symbols ? 3 : 4) : 9;
        state_.editor.focus_key = std::clamp(state_.editor.focus_key + delta_x, 0, max_key);
        return;
    }
    if (!state_.navigation.modals.empty()) {
        auto& modal = state_.navigation.modals.back();
        if (modal.id == "logs" && state_.log_state == LogState::Ready && !state_.log_lines.empty()) {
            const int delta = delta_y != 0 ? delta_y : delta_x;
            state_.log_offset = std::clamp(state_.log_offset + delta, 0, std::max(0, static_cast<int>(state_.log_lines.size()) - 8));
            return;
        }
        const int delta = delta_y != 0 ? delta_y : delta_x;
        modal.focused_action = std::clamp(modal.focused_action + delta, 0, static_cast<int>(modal.action_keys.size()) - 1);
        state_.navigation.focus_key = modal.action_keys[static_cast<std::size_t>(modal.focused_action)];
        return;
    }
    if (move_browser(state_, delta_x, delta_y)) return;
    if (state_.navigation.route == Route::Settings && move_settings(delta_x, delta_y)) return;
    const int columns = state_.navigation.route == Route::Library ? 6 : 1;
    const int delta = delta_y != 0 ? delta_y * columns : delta_x;
    if (view_.rows.empty()) return;
    state_.navigation.selected_index = std::clamp(view_.focused_row + delta, 0, static_cast<int>(view_.rows.size()) - 1);
    const auto& selected = view_.rows[static_cast<std::size_t>(state_.navigation.selected_index)];
    state_.navigation.focus_key = selected.key;
    if (selected.game_id != 0) state_.navigation.selected_game_id = selected.game_id;
    if (state_.navigation.route == Route::Library) state_.navigation.scroll_offsets["library-grid"] = state_.navigation.selected_index / columns;
    if (state_.navigation.route == Route::Home) state_.navigation.scroll_offsets["home"] = state_.navigation.selected_index;
}

// Settings sections, as on macOS: Left moves to the section rail, Up/Down there switches
// section (focusing its first row), Right or Cross returns to the rows. Up/Down on the rows
// crosses into the next section so every row stays reachable in order.
bool Simulation::move_settings(int delta_x, int delta_y) {
    if (view_.rows.empty()) return true;
    const int current = std::clamp(view_.focused_row, 0, static_cast<int>(view_.rows.size()) - 1);
    if (!state_.navigation.settings_rail_focused) {
        if (delta_x < 0) { state_.navigation.settings_rail_focused = true; return true; }
        // A rail-only section (Quit Playden) has no card to move onto from the rows.
        if (delta_y != 0) {
            const int target = std::clamp(current + delta_y, 0, static_cast<int>(view_.rows.size()) - 1);
            if (view_.rows[static_cast<std::size_t>(target)].rail_only) return true;
        }
        return delta_x > 0;
    }
    if (delta_x > 0) {
        if (!view_.rows[static_cast<std::size_t>(current)].rail_only) state_.navigation.settings_rail_focused = false;
        return true;
    }
    if (delta_y == 0) return true;
    std::vector<std::string> sections;
    for (const auto& item : view_.rows)
        if (sections.empty() || sections.back() != item.section) sections.push_back(item.section);
    const auto here = std::find(sections.begin(), sections.end(), view_.rows[static_cast<std::size_t>(current)].section);
    const int index = std::clamp(static_cast<int>(std::distance(sections.begin(), here)) + (delta_y > 0 ? 1 : -1), 0,
                                 static_cast<int>(sections.size()) - 1);
    const auto first = std::find_if(view_.rows.begin(), view_.rows.end(), [&](const ViewRow& item) {
        return item.section == sections[static_cast<std::size_t>(index)];
    });
    state_.navigation.selected_index = static_cast<int>(std::distance(view_.rows.begin(), first));
    state_.navigation.focus_key = first->key;
    return true;
}

void Simulation::change_route(int delta) {
    if (state_.editor.active || !state_.navigation.modals.empty()) return;
    constexpr Route routes[] = {Route::Home, Route::Library, Route::Downloads, Route::Settings};
    auto found = std::find(std::begin(routes), std::end(routes), state_.navigation.route);
    int index = found == std::end(routes) ? 0 : static_cast<int>(std::distance(std::begin(routes), found));
    index = (index + delta + 4) % 4;
    state_.navigation.route = routes[index];
    state_.navigation.selected_index = 0;
    state_.navigation.focus_key.clear();
    state_.navigation.settings_rail_focused = false;
}

void Simulation::activate() {
    if (state_.navigation.route == Route::Settings && state_.navigation.settings_rail_focused &&
        state_.navigation.modals.empty() && !state_.editor.active) {
        // Cross on a rail-only entry (Quit Playden) runs its action, as on macOS.
        const bool rail_action = !view_.rows.empty() &&
            view_.rows[static_cast<std::size_t>(std::clamp(view_.focused_row, 0, static_cast<int>(view_.rows.size()) - 1))].rail_only;
        if (!rail_action) {
            state_.navigation.settings_rail_focused = false;
            return;
        }
    }
    if (state_.editor.active) {
        static constexpr const char* letters[] = {"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.?"};
        static constexpr const char* symbols[] = {"1234567890", "!@#$%^&*()", "-_=+[]{}<>", "/\\|:;\"'~`?"};
        if (state_.editor.focus_row < 4) {
            char inserted = (state_.editor.symbols ? symbols : letters)[state_.editor.focus_row][state_.editor.focus_key];
            if (state_.editor.shifted && !state_.editor.symbols) {
                if (inserted >= 'a' && inserted <= 'z') inserted = static_cast<char>(inserted - 'a' + 'A');
                else if (state_.editor.focus_row == 2 && state_.editor.focus_key == 9) inserted = '"';
                else if (state_.editor.focus_row == 3 && state_.editor.focus_key == 7) inserted = ';';
                else if (state_.editor.focus_row == 3 && state_.editor.focus_key == 8) inserted = ':';
                else if (state_.editor.focus_row == 3 && state_.editor.focus_key == 9) inserted = '!';
            }
            state_.editor.value.insert(state_.editor.caret, 1, inserted); ++state_.editor.caret;
            if (state_.editor.shifted) state_.editor.shifted = false;
        } else if ((!state_.editor.symbols && state_.editor.focus_key == 0)) state_.editor.shifted = !state_.editor.shifted;
        else {
            const int offset = state_.editor.symbols ? 0 : 1;
            const int special = state_.editor.focus_key - offset;
            if (special == 0) state_.editor.symbols = !state_.editor.symbols;
            else if (special == 1) { state_.editor.value.insert(state_.editor.caret, " "); ++state_.editor.caret; }
            else if (special == 2 && state_.editor.caret > 0) {
                const auto prior = previous_caret(state_.editor.value, state_.editor.caret);
                state_.editor.value.erase(prior, state_.editor.caret - prior); state_.editor.caret = prior;
            }
            else if (special == 3) commit_editor();
        }
        return;
    }
    if (state_.navigation.route == Route::Library && state_.navigation.library_rail_focused) {
        const auto scope = state_.navigation.focus_key.rfind("rail-", 0) == 0 ? state_.navigation.focus_key.substr(5) : state_.library.scope;
        if (scope == "new-collection") { start_editor("Collection name", ""); return; }
        state_.library.scope = scope;
        state_.navigation.library_rail_focused = false;
        const auto games = visible_games();
        if (games.empty()) { state_.navigation.selected_game_id = 0; state_.navigation.focus_key = state_.library.query.empty() ? "library-empty" : "clear-search"; }
        else {
            state_.navigation.selected_index = std::min(state_.navigation.selected_index, static_cast<int>(games.size()) - 1);
            state_.navigation.selected_game_id = games[static_cast<std::size_t>(state_.navigation.selected_index)]->id;
            state_.navigation.focus_key = game_key(state_.navigation.selected_game_id);
        }
        save_profile(); return;
    }
    if (state_.navigation.route == Route::Home && state_.navigation.modals.empty() && state_.navigation.selected_game_id == 0 &&
        state_.navigation.focus_key.rfind("shelf-", 0) == 0 && state_.navigation.focus_key.size() > 5 &&
        state_.navigation.focus_key.compare(state_.navigation.focus_key.size() - 5, 5, "-more") == 0) {
        // "More" at the end of Continue playing: every game in the Library, most recently played first.
        state_.library.scope = "all";
        state_.library.sort = "recent-played";
        state_.library.query.clear();
        state_.navigation.library_rail_focused = false;
        state_.navigation.route = Route::Library;
        state_.navigation.selected_index = 0;
        state_.navigation.library_grid_column = 0;
        state_.navigation.scroll_offsets["library-grid"] = 0;
        const auto games = visible_games();
        state_.navigation.selected_game_id = games.empty() ? 0 : games.front()->id;
        state_.navigation.focus_key = games.empty() ? "library-empty" : game_key(state_.navigation.selected_game_id);
        save_profile();
        return;
    }
    // Home opens the card under the focus ring, which is drawn from the focused
    // shelf and its remembered position; the selection can lag behind it after a
    // background profile update.
    if (state_.navigation.route == Route::Home && state_.navigation.modals.empty()) {
        const auto projected = playden::homebrew::shelves(state_);
        if (!projected.empty()) {
            const auto& shelf = projected[static_cast<std::size_t>(
                std::clamp(state_.navigation.home_shelf, 0, static_cast<int>(projected.size()) - 1))];
            const auto remembered = state_.navigation.home_shelf_focus.find(shelf.key);
            const int item = remembered == state_.navigation.home_shelf_focus.end() ? 0 : remembered->second;
            if (item >= 0 && item < static_cast<int>(shelf.game_ids.size()))
                state_.navigation.selected_game_id = shelf.game_ids[static_cast<std::size_t>(item)];
        }
    }
    if (state_.navigation.route == Route::Home && state_.navigation.modals.empty() && state_.navigation.selected_game_id != 0) {
        if (live()) force_organization_selection_ = true;
        state_.navigation.detail_origin = Route::Home;
        state_.navigation.detail_id = state_.navigation.selected_game_id;
        state_.navigation.route = Route::Details;
        details_primary_pending_ = live(); state_.navigation.focus_key = live() ? "details-favorite" : (is_installed(state_, state_.navigation.selected_game_id) ? "details-play" : "details-install");
        return;
    }
    if (view_.rows.empty()) return;
    const auto index = std::clamp(view_.focused_row, 0, static_cast<int>(view_.rows.size()) - 1);
    const ViewRow selected = view_.rows[static_cast<std::size_t>(index)];
    if (!selected.enabled) return;
    const auto& action_name = selected.action;
    const bool edits_settings = (action_name.rfind("preset-", 0) == 0 && action_name != "preset-picker") ||
                                (action_name.rfind("choice-", 0) == 0 && action_name != "choice-custom") ||
                                action_name == "reset-settings";
    if (edits_settings && !can_change_game_settings()) return;
    if (action_name == "connect" || action_name == "qr-retry" || action_name == "qr-refresh") {
        if (live()) {
            queue_service_action(ServiceAction::BeginQr);
            state_.account.authentication = AuthState::Waiting;
            state_.account.challenge_url.clear();
            state_.account.error.clear();
            state_.navigation.route = Route::SignIn;
            state_.navigation.focus_key = "qr-cancel";
        } else scenario("qr-waiting");
    }
    else if (action_name == "retry-save") {
        if (live()) persistence_retry_requested_ = true;
        else { state_.persistence_available = true; save_profile(); }
    }
    else if (action_name == "continue-without-saving") { state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(), [](const Notice& notice) { return notice.id == "persistence"; }), state_.notices.end()); state_.navigation.route = Route::Home; }
    else if (action_name == "skip" || action_name == "library") { state_.navigation.route = action_name == "skip" ? Route::Home : Route::Library; state_.navigation.focus_key.clear(); }
    else if (action_name == "qr-cancel" || action_name == "editor-cancel" || action_name == "cancel") cancel();
    else if (action_name == "editor-done") commit_editor();
    else if (action_name == "clear-search") { state_.library.query.clear(); reconcile_library_selection(state_); }
    else if (action_name == "open-game") {
        if (live()) force_organization_selection_ = true;
        state_.navigation.detail_origin = state_.navigation.route;
        state_.navigation.detail_id = selected.game_id;
        state_.navigation.selected_game_id = selected.game_id;
        state_.navigation.route = Route::Details;
        details_primary_pending_ = live(); state_.navigation.focus_key = live() ? "details-favorite" : (is_installed(state_, selected.game_id) ? "details-play" : "details-install");
    } else if (action_name == "favorite") {
        if (state_.library.favorites.contains(state_.navigation.selected_game_id)) state_.library.favorites.erase(state_.navigation.selected_game_id);
        else state_.library.favorites.insert(state_.navigation.selected_game_id);
        save_profile();
    } else if (action_name == "hide") {
        if (state_.library.hidden.contains(state_.navigation.selected_game_id)) state_.library.hidden.erase(state_.navigation.selected_game_id);
        else state_.library.hidden.insert(state_.navigation.selected_game_id);
        close_modal(); reconcile_library_selection(state_);
        const auto remaining = visible_games();
        if (remaining.empty()) state_.navigation.focus_key = state_.library.query.empty() ? "library-empty" : "clear-search";
        else {
            state_.navigation.selected_index = std::min(state_.navigation.selected_index, static_cast<int>(remaining.size()) - 1);
            state_.navigation.selected_game_id = remaining[static_cast<std::size_t>(state_.navigation.selected_index)]->id;
            state_.navigation.focus_key = game_key(state_.navigation.selected_game_id);
        }
    } else if (action_name == "context") open_modal({"context", "Game actions", "details-more", context_actions(live()), 0, false});
    else if (action_name == "install") open_modal({"install", "Install game", "details-install", {"install-confirm", "install-estimate-retry", "storage-picker", "cancel"}, 0, false});
    else if (action_name == "storage-picker" || action_name == "storage-manager") {
        std::vector<std::string> actions;
        for (const auto& volume : state_.volumes) actions.push_back("volume-" + volume.id);
        actions.push_back("cancel");
        open_modal({"storage", "Choose storage", state_.navigation.focus_key, std::move(actions), 0, false});
    } else if (action_name.rfind("volume-", 0) == 0) {
        const auto id = action_name.substr(7);
        if (live()) {
            // The install service owns the choice; reflect it now and let its next snapshot confirm.
            const auto* volume = find_volume(state_, id);
            if (volume && volume->available && id != selected_volume_id_) {
                pending_install_requests_.push_back({InstallRequestKind::SetDefaultVolume, 0, id});
                selected_volume_id_ = id;
                state_.default_volume_id = id;
            }
        } else {
            selected_volume_id_ = id;
        }
        close_modal();
    } else if (action_name == "install-confirm") {
        const auto assessment = assess_install(state_, state_.navigation.selected_game_id, selected_volume_id_);
        if (assessment.readiness == InstallReadiness::UnknownSize || !assessment.download_bytes) {
            state_.notices.push_back({"estimate", "Download or install size remains unknown", "install-estimate-retry", true, 0});
        } else if (assessment.readiness != InstallReadiness::Ready) {
            state_.notices.push_back({"install-blocked", "Selected storage is unavailable or has insufficient space", "storage-picker", true, 0});
        } else {
            const auto existing = std::find_if(state_.jobs.begin(), state_.jobs.end(), [this](const Job& job) {
                return job.game_id == state_.navigation.selected_game_id && job.kind == "install"
                    && job.state != JobState::Cancelled && job.state != JobState::Downloaded && job.state != JobState::Failed;
            });
            if (existing == state_.jobs.end()) {
                const auto required = assessment.required_bytes.value_or(0);
                state_.jobs.push_back({next_job_id(), state_.navigation.selected_game_id, "install", next_queue_order(state_), JobStage::Download, JobState::Queued, 0, *assessment.download_bytes, std::nullopt, std::nullopt, {}, JobStage::Download, selected_volume_id_, 0, required, true, false});
                reconcile_storage(state_);
            }
            close_modal();
            state_.navigation.route = Route::Downloads;
            state_.navigation.focus_key = "job-" + state_.jobs.back().id;
        }
    } else if (action_name == "install-estimate-retry") {
        state_.notices.push_back({"estimate", "Fixture estimate is still unavailable", "install-estimate-retry", true, 0});
    } else if (action_name == "job-action") {
        const auto id = selected.key.substr(4);
        const auto* job = find_job(state_, id);
        std::vector<std::string> actions;
        if (job && (job->state == JobState::Downloading || job->state == JobState::Verifying)) actions.push_back("job-pause-" + id);
        if (job && (job->state == JobState::Paused || job->state == JobState::Interrupted)) actions.push_back("job-resume-" + id);
        if (job && job->state == JobState::Failed) actions.push_back("job-retry-" + id);
        if (job && job->state == JobState::Queued) { actions.push_back("job-up-" + id); actions.push_back("job-down-" + id); }
        if (job && job->state == JobState::Downloaded) { actions.push_back("job-open-" + id); actions.push_back("job-dismiss-" + id); }
        else if (job && job->state != JobState::Cancelled) actions.push_back("job-cancel-" + id);
        actions.push_back("logs"); actions.push_back("cancel");
        open_modal({"job-actions", "Download actions", selected.key, std::move(actions), 0, false});
    } else if (live() && action_name == "cloud-sync") {
        const auto id = state_.navigation.detail_id.value_or(state_.navigation.selected_game_id);
        if (id != 0) pending_cloud_requests_.push_back({CloudRequestKind::Sync, id});
    } else if (live() && action_name == "cloud-choose") {
        const auto id = state_.navigation.detail_id.value_or(state_.navigation.selected_game_id);
        const auto cloud = state_.cloud.find(id);
        if (cloud != state_.cloud.end() && cloud->second.status == CloudStatus::Conflict) {
            const auto& facts = cloud->second;
            const auto age = [this](std::int64_t when) -> std::string {
                if (when <= 0 || state_.clock_ticks <= 0) return "";
                const auto days = (state_.clock_ticks - when) / 86'400;
                return days <= 0 ? ", changed today" : days == 1 ? ", changed yesterday" : ", changed " + std::to_string(days) + " days ago";
            };
            ModalEntry choice{"confirmation", "Which saves should Playden keep?", state_.navigation.focus_key,
                              {"cloud-use-cloud", "cloud-use-local", "cancel"}, 0, false};
            choice.body = "Steam Cloud: " + std::to_string(facts.remote_files) + " files, " + format_bytes(facts.remote_bytes) +
                          age(facts.remote_latest) + ". This console: " + std::to_string(facts.local_files) + " files, " +
                          format_bytes(facts.local_bytes) + age(facts.local_latest) +
                          ". Saves that get replaced here are backed up first.";
            open_modal(std::move(choice));
        } else if (cloud != state_.cloud.end() && cloud->second.status == CloudStatus::NeedsAccount) {
            ModalEntry confirm{"confirmation", "Upload saves to this Steam account?", state_.navigation.focus_key,
                               {"cloud-confirm-account", "cancel"}, 0, false};
            confirm.body = "Playden has not synced this game's saves for " +
                           (state_.account.display_name.empty() ? std::string("this account") : state_.account.display_name) +
                           " before. Confirm to upload this console's saves to its Steam Cloud.";
            open_modal(std::move(confirm));
        }
    } else if (live() && (action_name == "cloud-use-cloud" || action_name == "cloud-use-local" || action_name == "cloud-confirm-account")) {
        const auto id = state_.navigation.detail_id.value_or(state_.navigation.selected_game_id);
        const auto kind = action_name == "cloud-use-cloud" ? CloudRequestKind::UseCloud
                          : action_name == "cloud-use-local" ? CloudRequestKind::UseLocal
                                                             : CloudRequestKind::ConfirmAccount;
        if (id != 0) pending_cloud_requests_.push_back({kind, id});
        close_modal();
    } else if (live() && action_name == "runtime-prepare") {
        const auto id = state_.navigation.detail_id.value_or(state_.navigation.selected_game_id);
        if (id != 0) pending_runtime_prepare_requests_.push_back(id);
    } else if (live() && action_name == "runtime-remove-retry") {
        // Continues the saved removal (with its first choice about saves), then removes the download.
        const auto id = state_.navigation.detail_id.value_or(state_.navigation.selected_game_id);
        if (id != 0) {
            pending_install_requests_.push_back({InstallRequestKind::Uninstall, id, {}, false});
            pending_uninstalls_.insert(id);
        }
    } else if (live() && action_name == "runtime-cleanup") {
        pending_runtime_cleanup_ = true;
    } else if (live() && action_name == "runtime-play") {
        const auto id = state_.navigation.detail_id.value_or(state_.navigation.selected_game_id);
        if (id != 0) pending_runtime_play_requests_.push_back(id);
    } else if (live() && action_name == "live-install") {
        const auto id = state_.navigation.detail_id.value_or(state_.navigation.selected_game_id);
        if (id != 0) pending_install_requests_.push_back({InstallRequestKind::Install, id});
    } else if (live() && action_name == "open-downloads") {
        state_.navigation.route = Route::Downloads;
        state_.navigation.focus_key = "job-" + std::to_string(state_.navigation.detail_id.value_or(0));
    } else if (live() && (action_name.rfind("job-pause-", 0) == 0 || action_name.rfind("job-resume-", 0) == 0 ||
                          action_name.rfind("job-retry-", 0) == 0)) {
        const auto dash = action_name.find('-', 4);
        if (const auto* job = find_job(state_, action_name.substr(dash + 1))) {
            const auto kind = action_name.rfind("job-pause-", 0) == 0 ? InstallRequestKind::Pause : InstallRequestKind::Resume;
            pending_install_requests_.push_back({kind, job->game_id});
        }
        // Retry from a job's log dialog also leaves the job's actions behind it.
        if (!state_.navigation.modals.empty() && state_.navigation.modals.back().id == "logs") close_modal();
        close_modal();
    } else if (action_name.rfind("job-pause-", 0) == 0) { if (auto* job = find_job(state_, action_name.substr(10))) job->state = JobState::Pausing; close_modal(); }
    else if (action_name.rfind("job-resume-", 0) == 0) {
        if (auto* job = find_job(state_, action_name.substr(11))) {
            for (auto& other : state_.jobs) if (&other != job && (other.state == JobState::Downloading || other.state == JobState::Verifying)) other.state = JobState::Queued;
            job->state = job->stage == JobStage::Download ? JobState::Downloading : JobState::Verifying;
        }
        close_modal();
    }
    else if (action_name.rfind("job-retry-", 0) == 0) {
        if (auto* job = find_job(state_, action_name.substr(10))) {
            for (auto& other : state_.jobs) if (&other != job && (other.state == JobState::Downloading || other.state == JobState::Verifying)) other.state = JobState::Queued;
            job->stage = job->retry_stage; job->state = job->stage == JobStage::Download ? JobState::Downloading : JobState::Verifying; job->failure.clear();
        }
        close_modal();
    }
    else if (action_name.rfind("job-up-", 0) == 0 || action_name.rfind("job-down-", 0) == 0) {
        const bool up = action_name.rfind("job-up-", 0) == 0;
        const auto id = action_name.substr(up ? 7 : 9);
        if (auto* job = find_job(state_, id)) {
            const int wanted = job->queue_order + (up ? -1 : 1);
            auto other = std::find_if(state_.jobs.begin(), state_.jobs.end(), [wanted](const Job& value) { return value.queue_order == wanted; });
            if (other != state_.jobs.end()) std::swap(job->queue_order, other->queue_order);
        }
        close_modal();
    } else if (action_name.rfind("job-open-", 0) == 0) {
        if (const auto* job = find_job(state_, action_name.substr(9))) {
            state_.navigation.selected_game_id = job->game_id; state_.navigation.detail_id = job->game_id; state_.navigation.detail_origin = Route::Downloads; state_.navigation.route = Route::Details;
        }
        close_modal();
    } else if (action_name.rfind("job-dismiss-", 0) == 0) {
        const auto id = action_name.substr(12);
        close_modal();
        state_.jobs.erase(std::remove_if(state_.jobs.begin(), state_.jobs.end(), [&id](const Job& value) { return value.id == id && value.state == JobState::Downloaded; }), state_.jobs.end());
    } else if (action_name.rfind("job-cancel-", 0) == 0) {
        pending_destructive_action_ = action_name;
        open_modal({"confirmation", "Cancel download?", selected.key, {"cancel", "confirm-destructive"}, 0, true});
    } else if (action_name == "verify") {
        const auto duplicate = std::find_if(state_.jobs.begin(), state_.jobs.end(), [this](const Job& job) {
            return job.game_id == state_.navigation.selected_game_id && job.kind == "verify"
                && job.state != JobState::Downloaded && job.state != JobState::Cancelled && job.state != JobState::Failed;
        });
        if (duplicate == state_.jobs.end())
            state_.jobs.push_back({next_job_id(), state_.navigation.selected_game_id, "verify", next_queue_order(state_), JobStage::Verify, JobState::Queued, 0, 0, std::nullopt, std::nullopt, {}, JobStage::Verify, {}, 0, 0, false, false});
        while (!state_.navigation.modals.empty()) close_modal();
        state_.navigation.route = Route::Downloads;
    } else if (action_name == "uninstall" || action_name == "reset-confirm" || action_name == "signout-confirm" || action_name == "quit-confirm" || action_name == "quit-game") {
        pending_destructive_action_ = action_name;
        std::string title;
        if (action_name == "uninstall") title = "Uninstall this fixture game? Saves are preserved.";
        else if (action_name == "reset-confirm") title = "Reset preferences and organization? Installs, saves, and history remain.";
        else if (action_name == "signout-confirm") title = "Sign out? Account-visible cache will be cleared.";
        else if (action_name == "quit-confirm") title = "Quit Playden? Active jobs will recover after restart.";
        else title = "Quit the simulated game? Download jobs continue.";
        std::string body;
        if (live()) {
            // Live wording describes what actually happens; no simulated jobs or fixtures exist.
            if (action_name == "signout-confirm") {
                title = "Sign out of Steam?";
                body = "The saved sign-in, this account's library cache and its organization are removed from this console. Signing in again needs the Steam mobile app.";
            } else if (action_name == "quit-confirm") {
                title = "Quit Playden?";
                body = "Your sign-in, library and settings are kept for next time.";
            } else if (action_name == "reset-confirm") {
                body = "Favorites, collections, notes and display preferences return to their defaults.";
            } else if (action_name == "uninstall") {
                const auto id = state_.navigation.selected_game_id;
                const auto runtime_game = state_.runtime_games.find(id);
                const bool has_title = runtime_game != state_.runtime_games.end() && runtime_game->second.has_title;
                const auto cloud = state_.cloud.find(id);
                const bool no_cloud = cloud != state_.cloud.end() && cloud->second.status == CloudStatus::Unsupported;
                title = "Uninstall this game?";
                body = has_title ? "The game's files and its PS5 title are removed from this console. Its saves and settings are kept, "
                                   "and installing the game again brings them back."
                                 : "The game's files are removed from this console. You can install it again from Steam.";
                body += no_cloud ? " This game has no Steam Cloud saves: its saves exist only on this console."
                                 : " Steam Cloud saves are synced first; if that fails, Playden asks before going on.";
            }
        }
        std::vector<std::string> actions{"cancel", "confirm-destructive"};
        if (live() && action_name == "uninstall") {
            uninstall_delete_saves_ = false;
            const auto runtime_game = state_.runtime_games.find(state_.navigation.selected_game_id);
            if (runtime_game != state_.runtime_games.end() && runtime_game->second.has_title)
                actions = {"cancel", "uninstall-delete-saves", "confirm-destructive"};
        }
        ModalEntry confirmation{"confirmation", std::move(title), state_.navigation.focus_key, std::move(actions), 0, true};
        confirmation.body = std::move(body);
        open_modal(std::move(confirmation));
    } else if (action_name == "uninstall-delete-saves") {
        uninstall_delete_saves_ = !uninstall_delete_saves_;
    } else if (live() && action_name == "quit-to-home") {
        // Ready to play: ShadowMountPlus registers the title once Playden quits, and the
        // game appears on the Home screen. The dialog or Details already said so.
        state_.navigation.modals.clear();
        state_.quit_requested = true;
    } else if (action_name == "ready-later") {
        close_modal();
    } else if (live() && action_name == "kept-manage") {
        if (state_.kept_bottles.empty()) return;
        kept_selected_.clear();
        open_modal({"kept-data", "Kept game data", state_.navigation.focus_key, kept_manager_actions(), 0, false});
    } else if (live() && action_name == "kept-select-all") {
        if (kept_selected_.size() == state_.kept_bottles.size()) kept_selected_.clear();
        else for (const auto& kept : state_.kept_bottles) kept_selected_.insert(kept.game_id);
    } else if (live() && action_name.rfind("kept-toggle-", 0) == 0) {
        const auto id = static_cast<GameId>(std::strtoull(action_name.c_str() + 12, nullptr, 10));
        if (!kept_selected_.erase(id)) kept_selected_.insert(id);
    } else if (live() && action_name == "kept-selection-delete") {
        if (kept_selected_.empty()) return;
        std::uint64_t bytes = 0;
        std::string only;
        for (const auto& kept : state_.kept_bottles)
            if (kept_selected_.contains(kept.game_id)) { bytes += kept.bytes; only = kept_name(state_, kept); }
        pending_destructive_action_ = action_name;
        ModalEntry confirmation{"confirmation", "Delete kept game data?", state_.navigation.focus_key, {"cancel", "confirm-destructive"}, 0, true};
        confirmation.body = "The saves and settings kept from " + (kept_selected_.size() == 1 ? only : kept_count(kept_selected_.size())) +
                            " (" + format_bytes(bytes) + ") are deleted from this console. Saves in Steam Cloud stay in Steam Cloud.";
        open_modal(std::move(confirmation));
    } else if (action_name == "confirm-destructive" && live() && pending_destructive_action_ == "kept-selection-delete") {
        for (const auto& kept : state_.kept_bottles)
            if (kept_selected_.contains(kept.game_id)) pending_kept_delete_requests_.push_back(kept.game_id);
        kept_selected_.clear();
        pending_destructive_action_.clear();
        state_.navigation.focus_key = state_.navigation.modals.front().opener_focus_key;
        state_.navigation.modals.clear();
    } else if (live() && action_name.rfind("kept-delete-", 0) == 0) {
        pending_destructive_action_ = action_name;
        const auto id = static_cast<GameId>(std::strtoull(action_name.c_str() + 12, nullptr, 10));
        std::string name;
        for (const auto& kept : state_.kept_bottles) if (kept.game_id == id) name = kept.name;
        if (name.empty()) if (const auto* game = find_game(state_, id)) name = game->title;
        ModalEntry confirmation{"confirmation", "Delete kept saves?", state_.navigation.focus_key, {"cancel", "confirm-destructive"}, 0, true};
        confirmation.body = "The saves and settings kept from " + (name.empty() ? std::string("this game") : name) +
                            " are deleted from this console. Saves in Steam Cloud stay in Steam Cloud.";
        open_modal(std::move(confirmation));
    } else if (action_name == "confirm-destructive" && live() && pending_destructive_action_.rfind("kept-delete-", 0) == 0) {
        pending_kept_delete_requests_.push_back(static_cast<GameId>(std::strtoull(pending_destructive_action_.c_str() + 12, nullptr, 10)));
        pending_destructive_action_.clear();
        state_.navigation.focus_key = state_.navigation.modals.front().opener_focus_key;
        state_.navigation.modals.clear();
    } else if (action_name == "confirm-destructive" && live() &&
               (pending_destructive_action_.rfind("job-cancel-", 0) == 0 || pending_destructive_action_ == "uninstall")) {
        if (pending_destructive_action_ == "uninstall") {
            pending_install_requests_.push_back({InstallRequestKind::Uninstall, state_.navigation.selected_game_id, {},
                                                 uninstall_delete_saves_});
            pending_uninstalls_.insert(state_.navigation.selected_game_id);
            uninstall_delete_saves_ = false;
        } else if (const auto* job = find_job(state_, pending_destructive_action_.substr(11))) {
            pending_install_requests_.push_back({InstallRequestKind::Cancel, job->game_id});
        }
        pending_destructive_action_.clear();
        state_.navigation.modals.clear();
    } else if (action_name == "confirm-destructive") {
        if (pending_destructive_action_.rfind("job-cancel-", 0) == 0) {
            if (auto* job = find_job(state_, pending_destructive_action_.substr(11))) { job->state = JobState::Cancelled; job->reservation_held = false; }
            reconcile_storage(state_);
        }
        else if (pending_destructive_action_ == "uninstall") {
            if (const auto* installation = find_installation(state_, state_.navigation.selected_game_id)) {
                const auto duplicate = std::find_if(state_.jobs.begin(), state_.jobs.end(), [this](const Job& job) {
                    return job.game_id == state_.navigation.selected_game_id && job.kind == "uninstall"
                        && job.state != JobState::Downloaded && job.state != JobState::Cancelled;
                });
                const bool has_duplicate = duplicate != state_.jobs.end();
                const auto existing_id = has_duplicate ? duplicate->id : std::string{};
                if (!has_duplicate)
                    state_.jobs.push_back({next_job_id(), state_.navigation.selected_game_id, "uninstall", next_queue_order(state_), JobStage::Uninstall, JobState::Queued, 0, installation->installed_bytes, std::nullopt, std::nullopt, {}, JobStage::Uninstall, installation->volume_id, 0, 0, false, false});
                state_.navigation.route = Route::Downloads;
                state_.navigation.focus_key = "job-" + (has_duplicate ? existing_id : state_.jobs.back().id);
            }
        }
        else if (pending_destructive_action_ == "signout-confirm") {
            if (live()) { queue_service_action(ServiceAction::SignOut); state_.account.authentication = AuthState::SigningOut; }
            else { state_.account.authentication = AuthState::SignedOut; state_.account.id.clear(); state_.account.display_name.clear(); state_.account.cache_available = false; state_.library = {}; clear_profile_customizations(state_); state_.navigation.route = Route::Startup; }
        }
        else if (pending_destructive_action_ == "account-switch") {
            if (live()) { queue_service_action(ServiceAction::SwitchAccount); state_.account.authentication = AuthState::SigningOut; }
            else {
                state_.library = {}; clear_profile_customizations(state_); state_.account = {};
                state_.account.authentication = AuthState::Waiting; state_.account.challenge_expiry = state_.clock_ticks + 30;
                ++challenge_generation_; state_.navigation.route = Route::SignIn; state_.navigation.focus_key = "qr-cancel";
            }
        }
        else if (pending_destructive_action_ == "reset-confirm") { const auto installs = state_.installations; state_.library = {}; state_.installations = installs; clear_profile_customizations(state_); state_.reduced_motion = false; if (live()) { force_organization_reset_ = true; force_preferences_reset_ = true; } }
        else if (pending_destructive_action_ == "quit-game") { state_.session = SessionState::Stopping; state_.navigation.route = Route::Session; }
        else if (pending_destructive_action_ == "force-stop") { state_.session = SessionState::Idle; state_.navigation.route = Route::Details; state_.navigation.detail_id = state_.session_game_id; }
        else if (pending_destructive_action_ == "quit-confirm") state_.quit_requested = true;
        else if (pending_destructive_action_.rfind("collection-delete-", 0) == 0) {
            const auto id = pending_destructive_action_.substr(18);
            state_.library.collections.erase(std::remove_if(state_.library.collections.begin(), state_.library.collections.end(), [&id](const Collection& value) { return value.id == id; }), state_.library.collections.end());
            if (state_.library.scope == id) {
                state_.library.scope = "all";
                state_.navigation.library_rail_focused = false;
                reconcile_library_selection(state_);
            }
        }
        state_.navigation.modals.clear();
        pending_destructive_action_.clear();
    } else if (action_name.rfind("sort-", 0) == 0) {
        state_.library.sort = action_name.substr(5); close_modal(); reconcile_library_selection(state_); save_profile();
    } else if (action_name.rfind("filter-", 0) == 0) {
        const auto filter = action_name.substr(7);
        if (state_.library.filters.contains(filter)) state_.library.filters.erase(filter); else state_.library.filters.insert(filter);
        close_modal(); reconcile_library_selection(state_); save_profile();
    } else if (action_name == "filters-reset") {
        state_.library.filters.clear(); state_.library.sort = "name"; close_modal(); reconcile_library_selection(state_); save_profile();
    } else if (action_name == "game-settings") {
        expanded_settings_sections_.clear();
        const auto opened = state_.game_settings.find(state_.navigation.selected_game_id);
        settings_opened_record_ = opened == state_.game_settings.end() ? rt::GameSettingsRecord{} : opened->second;
        open_modal({"game-settings", "Game settings", "details-settings", game_settings_actions(), 0, false});
    }
    else if (action_name == "preset-picker") {
        std::vector<std::string> actions;
        int focused = 0;
        const auto* current = preset_for(state_.navigation.selected_game_id);
        for (const auto& preset : settings_presets_) {
            if (current && preset.id == current->id) focused = static_cast<int>(actions.size());
            actions.push_back("preset-" + preset.id);
        }
        actions.push_back("cancel");
        open_modal({"preset-picker", "Choose a preset", "preset-picker", std::move(actions), focused, false});
    } else if (action_name.rfind("preset-", 0) == 0 && !state_.navigation.modals.empty() && state_.navigation.modals.back().id == "preset-picker") {
        auto& record = state_.game_settings[state_.navigation.selected_game_id];
        const auto id = action_name.substr(7);
        record.preset_id = id == "playden-default" ? std::string{} : id;
        record.overrides.clear();
        ++record.revision;
        close_modal(); refresh_game_settings(); save_profile();
    } else if (action_name.rfind("setting-", 0) == 0) {
        const auto id = action_name.substr(8);
        const auto* definition = rt::FindSetting(id);
        rt::EffectiveSettings values;
        if (definition && EffectiveSettingsFor(state_.navigation.selected_game_id, &values)) {
            const auto& value = values.values.at(id);
            if (!value.available || value.locked) {
                // Disabled rows stay focusable so their reason can be read; Cross does nothing.
            } else if (id.rfind("button.", 0) == 0) {
                std::vector<std::string> actions;
                int focused = 0;
                for (std::size_t index = 0; index < common_bindings().size(); ++index) {
                    if (value.source == rt::ValueSource::kUser && common_bindings()[index].first == value.value) focused = static_cast<int>(index);
                    actions.push_back("choice-" + std::to_string(index));
                }
                actions.push_back("choice-custom");
                actions.push_back("cancel");
                open_modal({"choice-picker", definition->label, action_name, std::move(actions), focused, false, "", id});
            } else if (definition->kind == rt::SettingKind::kText) {
                start_editor(definition->label, value.value);
                editing_setting_id_ = id;
            } else {
                const auto& recipe = RecipeFor(state_.navigation.selected_game_id);
                const std::size_t count = id == "launch_option" ? recipe.launch_entries.size() : definition->choices.size();
                std::vector<std::string> actions;
                int focused = 0;
                for (std::size_t index = 0; index < count; ++index) {
                    const auto& choice = id == "launch_option" ? recipe.launch_entries[index].id : definition->choices[index].value;
                    if (choice == value.value) focused = static_cast<int>(index);
                    actions.push_back("choice-" + std::to_string(index));
                }
                actions.push_back("cancel");
                open_modal({"choice-picker", definition->label, action_name, std::move(actions), focused, false, "", id});
            }
        }
    } else if (action_name == "choice-custom" && !state_.navigation.modals.empty() && !state_.navigation.modals.back().setting_id.empty()) {
        const auto id = state_.navigation.modals.back().setting_id;
        rt::EffectiveSettings values;
        const bool known = EffectiveSettingsFor(state_.navigation.selected_game_id, &values);
        close_modal();
        start_editor(rt::FindSetting(id)->label, known ? values.values.at(id).value : std::string{});
        editing_setting_id_ = id;
    } else if (action_name == "button-mapping") {
        std::vector<std::string> actions;
        for (const auto& button : rt::ProsperoButtons()) actions.push_back("setting-button." + button);
        actions.push_back("cancel");
        open_modal({"button-mapping", "Button mapping", "button-mapping", std::move(actions), 0, false});
    } else if (action_name.rfind("choice-", 0) == 0 && !state_.navigation.modals.empty() && state_.navigation.modals.back().setting_id.rfind("button.", 0) == 0) {
        const auto id = state_.navigation.modals.back().setting_id;
        const auto index = static_cast<std::size_t>(std::stoul(action_name.substr(7)));
        auto& record = state_.game_settings[state_.navigation.selected_game_id];
        if (index == 0) record.overrides.erase(id);           // inherit the preset or recipe binding
        else if (index < common_bindings().size()) record.overrides[id] = common_bindings()[index].first;
        ++record.revision;
        close_modal(); refresh_game_settings(); save_profile();
    } else if (action_name.rfind("choice-", 0) == 0 && !state_.navigation.modals.empty() && !state_.navigation.modals.back().setting_id.empty()) {
        const auto id = state_.navigation.modals.back().setting_id;
        const auto index = static_cast<std::size_t>(std::stoul(action_name.substr(7)));
        const auto& recipe = RecipeFor(state_.navigation.selected_game_id);
        const auto* definition = rt::FindSetting(id);
        const bool launch = id == "launch_option";
        if (definition && index < (launch ? recipe.launch_entries.size() : definition->choices.size()))
            set_game_setting(id, launch ? recipe.launch_entries[index].id : definition->choices[index].value);
        close_modal(); refresh_game_settings(); save_profile();
    } else if (action_name == "section-more" || action_name == "section-advanced") {
        if (!expanded_settings_sections_.erase(action_name)) expanded_settings_sections_.insert(action_name);
        refresh_game_settings();
    } else if (action_name == "clear-shader-cache") {
        if (live()) return;
        // Fixture mode simulates maintenance; live mode requires the runtime service.
        push_notice({"shader-cache", "Shader cache cleared", "", false, kLiveNoticeSeconds, false,
                     "Shaders compile again on the next launch"});
    } else if (action_name == "reset-settings") {
        auto& record = state_.game_settings[state_.navigation.selected_game_id];
        if (!record.overrides.empty()) { record.overrides.clear(); ++record.revision; }
        refresh_game_settings(); save_profile();
    }
    else if (action_name == "note") open_modal({"compatibility", "Compatibility and note", "note", {"rating-untested", "rating-works", "rating-playable", "rating-broken", "edit-note", "cancel"}, 0, false});
    else if (action_name.rfind("rating-", 0) == 0) {
        const auto value = action_name.substr(7);
        state_.library.ratings[state_.navigation.selected_game_id] = value == "works" ? Compatibility::Works : value == "playable" ? Compatibility::Playable : value == "broken" ? Compatibility::Broken : Compatibility::Untested;
        save_profile(); close_modal();
    }
    else if (action_name == "edit-note") start_editor("Note", state_.library.notes[state_.navigation.selected_game_id]);
    else if (action_name == "collection") {
        std::vector<std::string> actions;
        for (const auto& collection : state_.library.collections) {
            actions.push_back("collection-toggle-" + collection.id);
            actions.push_back("collection-rename-" + collection.id);
            actions.push_back("collection-pin-" + collection.id);
            actions.push_back("collection-delete-" + collection.id);
        }
        actions.push_back("collection-new"); actions.push_back("cancel");
        open_modal({"collections", "Collections", "collection", std::move(actions), 0, false});
    }
    else if (action_name == "collection-new") { editing_target_id_.clear(); start_editor("Collection name", ""); }
    else if (action_name.rfind("collection-toggle-", 0) == 0) {
        const auto id = action_name.substr(18);
        auto found = std::find_if(state_.library.collections.begin(), state_.library.collections.end(), [&id](const Collection& value) { return value.id == id; });
        if (found != state_.library.collections.end()) {
            if (found->members.contains(state_.navigation.selected_game_id)) found->members.erase(state_.navigation.selected_game_id); else found->members.insert(state_.navigation.selected_game_id);
            save_profile();
        }
    }
    else if (action_name.rfind("collection-rename-", 0) == 0) {
        editing_target_id_ = action_name.substr(18);
        const auto found = std::find_if(state_.library.collections.begin(), state_.library.collections.end(), [this](const Collection& value) { return value.id == editing_target_id_; });
        if (found != state_.library.collections.end()) start_editor("Rename collection", found->name);
    }
    else if (action_name.rfind("collection-pin-", 0) == 0) {
        const auto id = action_name.substr(15);
        const auto found = std::find_if(state_.library.collections.begin(), state_.library.collections.end(), [&id](const Collection& value) { return value.id == id; });
        if (found != state_.library.collections.end()) { found->pinned = !found->pinned; save_profile(); }
    }
    else if (action_name.rfind("collection-delete-", 0) == 0) {
        pending_destructive_action_ = action_name;
        open_modal({"confirmation", "Delete collection? Games stay in your library.", action_name, {"cancel", "confirm-destructive"}, 0, true});
    }
    else if (action_name == "toggle-motion") { state_.reduced_motion = !state_.reduced_motion; save_profile(); }
    else if (action_name == "toggle-title-store") { state_.title_store_suffix = !state_.title_store_suffix; save_profile(); }
    else if (action_name == "launch") { state_.session_game_id = state_.navigation.selected_game_id; state_.session = SessionState::Preparing; state_.navigation.route = Route::Session; state_.navigation.focus_key = "session-return"; }
    else if (action_name == "runtime-status") open_modal({"runtime", "Runtime unavailable", "details-play", {"runtime-retry", "cancel"}, 0, false});
    else if (action_name == "return-game") state_.session = SessionState::Running;
    else if (action_name == "force-stop") { pending_destructive_action_ = "force-stop"; open_modal({"confirmation", "Force stop simulated session?", state_.navigation.focus_key, {"cancel", "confirm-destructive"}, 0, true}); }
    else if (action_name == "details-back") { state_.navigation.route = Route::Details; state_.navigation.detail_id = state_.session_game_id; }
    else if (live() && action_name == "logs") {
        // From a download's actions: that game's install log, with Retry when the download failed.
        GameId game_id = 0;
        if (!state_.navigation.modals.empty() && state_.navigation.modals.back().id == "job-actions") {
            const auto& opener = state_.navigation.modals.back().opener_focus_key;
            if (opener.rfind("job-", 0) == 0) game_id = static_cast<GameId>(std::strtoul(opener.c_str() + 4, nullptr, 10));
        }
        open_live_log("install", game_id);
    }
    else if (live() && action_name == "logs-pick")
        open_modal({"diagnostics", "Logs", state_.navigation.focus_key, {"log-install", "log-cloud", "log-auth", "log-launch", "cancel"}, 0, false});
    else if (live() && (action_name == "log-install" || action_name == "log-cloud" || action_name == "log-auth" ||
                        action_name == "log-launch"))
        open_live_log(action_name.substr(4), 0);
    else if (live() && action_name == "logs-refresh") load_live_log();
    else if (action_name == "logs") { start_logs(); open_modal({"logs", "Operation logs", state_.navigation.focus_key, {"logs-fixture-controls", "logs-back"}, 0, false}); }
    else if (action_name == "logs-retry") { start_logs(); state_.navigation.modals.back().action_keys = {"logs-fixture-controls", "logs-back"}; state_.navigation.modals.back().focused_action = 0; state_.navigation.focus_key = "logs-fixture-controls"; }
    else if (action_name == "logs-back") close_modal();
    else if (action_name == "logs-fixture-controls") open_modal({"logs-controls", "Log fixture controls", state_.navigation.focus_key,
        {"inject-logs-loading", "inject-logs-empty", "inject-logs-failure", "inject-logs-ready", "cancel"}, 0, false});
    else if (action_name == "controller-test") open_modal({"controller", "Controller diagnostics", state_.navigation.focus_key, {"cancel"}, 0, false});
    else if (action_name == "settings-runtime") open_modal({"runtime", "Runtime status — simulated", state_.navigation.focus_key, {"runtime-retry", "cancel"}, 0, false});
    else if (action_name == "runtime-retry") { state_.runtime = RuntimeState::Checking; state_.session_error.clear(); }
    else if (action_name == "qr-fixture-controls") open_modal({"qr-controls", "QR fixture controls", state_.navigation.focus_key,
        {"inject-qr-approved", "inject-qr-expired", "inject-qr-failure", "inject-qr-denied", "advance-clock", "cancel"}, 0, false});
    else if (action_name == "simulation-controls") open_modal({"simulation-controls", "Simulation controls", state_.navigation.focus_key,
        {"advance-clock", "inject-download-failed", "inject-verification-failed", "inject-uninstall-failed", "inject-storage-disconnect", "inject-storage-reconnect", "inject-controller-disconnect", "inject-controller-reconnect", "inject-launch-failure", "inject-stop-failure", "inject-runtime-failed", "inject-save-failure", "inject-logs-loading", "inject-logs-empty", "inject-logs-failure", "inject-logs-ready", "inject-delayed-art", "inject-failed-art", "inject-library-refresh", "inject-startup-loading", "cancel"}, 0, false});
    else if (action_name == "about") open_modal({"about", live() ? "About Playden" : "About Playden UI simulation", state_.navigation.focus_key,
        {"about-build", "about-framework", "about-platform", "about-back"}, 0, false});
    else if (action_name == "about-back") close_modal();
    else if (action_name == "scenario-picker") open_modal({"scenarios", "Fixture scenarios", state_.navigation.focus_key,
        {"scenario-default", "scenario-first-run", "scenario-signed-out", "scenario-qr-waiting", "scenario-offline", "scenario-empty", "scenario-no-results", "scenario-delayed-art", "scenario-failed-art", "scenario-install-ready", "scenario-unknown-size", "scenario-insufficient-space", "scenario-storage-disconnect", "scenario-paused", "scenario-download-failed", "scenario-verification-failed", "scenario-restart-recovery", "scenario-save-failure", "scenario-runtime-unavailable", "scenario-launch-failure", "scenario-crash", "scenario-stop-failure", "scenario-controller-disconnected", "scenario-reduced-motion", "scenario-stress", "cancel"}, 0, false});
    else if (action_name.rfind("scenario-", 0) == 0) { state_.navigation.modals.clear(); scenario(action_name.substr(9)); }
    else if (action_name == "session-fixture-controls") open_modal({"session-controls", "Session fixture controls", state_.navigation.focus_key,
        {"advance-clock", "inject-stop-failure", "inject-crash", "cancel"}, 0, false});
    else if (action_name == "advance-clock") tick(1);
    else if (action_name.rfind("inject-", 0) == 0) {
        const auto injected = action_name.substr(7);
        if (!state_.navigation.modals.empty() && (state_.navigation.modals.back().id == "qr-controls" || state_.navigation.modals.back().id == "logs-controls" || state_.navigation.modals.back().id == "session-controls")) close_modal();
        action("inject:" + injected);
    }
    else if (action_name == "notices") {
        std::vector<std::string> actions;
        for (const auto& notice : state_.notices) if (!notice.action.empty()) actions.push_back(notice.action);
        actions.push_back("cancel"); open_modal({"notices", "Warnings", state_.navigation.focus_key, std::move(actions), 0, false});
    }
    else if (action_name.rfind("open-game:", 0) == 0) {
        GameId id = 0; const auto number = action_name.substr(10);
        if (std::from_chars(number.data(), number.data() + number.size(), id).ec == std::errc{} && find_game(state_, id)) {
            state_.navigation.modals.clear(); state_.navigation.selected_game_id = id; state_.navigation.detail_id = id;
            state_.navigation.detail_origin = Route::Downloads; state_.navigation.route = Route::Details;
            details_primary_pending_ = live(); state_.navigation.focus_key = live() ? "details-favorite" : (is_installed(state_, id) ? "details-play" : "details-install");
        }
    }
    else if (action_name == "storage-retry") action("inject:storage-reconnect");
    else if (action_name == "retry-save") {
        if (live()) persistence_retry_requested_ = true;
        else { state_.persistence_available = true; save_profile(); }
    }
    else if (action_name == "account") {
        std::vector<std::string> actions = {"account-auth", "account-last-sync"};
        if (state_.account.authentication == AuthState::SignedIn) actions.push_back("account-sync");
        else actions.push_back("account-recover");
        actions.push_back("account-switch");
        if (state_.account.authentication != AuthState::SignedOut) actions.push_back("signout-confirm");
        actions.push_back("cancel");
        open_modal({"account", state_.account.display_name.empty() ? "Account" : state_.account.display_name, state_.navigation.focus_key, std::move(actions), 0, false});
    }
    else if (action_name == "account-sync") {
        close_modal();
        if (!live()) state_.account.authentication = AuthState::Syncing;
        state_.library_syncing = true;
        state_.library_loading = true;
        state_.navigation.route = Route::Home;
        if (live()) queue_service_action(ServiceAction::RefreshLibrary);
    }
    else if (action_name == "account-recover") {
        state_.navigation.modals.clear();
        if (live()) { queue_service_action(ServiceAction::BeginQr); state_.account.authentication = AuthState::Waiting; state_.account.challenge_url.clear(); state_.account.error.clear(); state_.navigation.route = Route::SignIn; state_.navigation.focus_key = "qr-cancel"; }
        else scenario("qr-waiting");
    }
    else if (action_name == "account-switch") { pending_destructive_action_ = "account-switch"; open_modal({"confirmation", "Switch account? Current account cache will be cleared.", state_.navigation.focus_key, {"cancel", "confirm-destructive"}, 0, true}); }
}

bool Simulation::action(const std::string& logical_action) {
    // Any input settles Details focus where the user put it.
    details_primary_pending_ = false;
    state_.controller = {};
    if (logical_action == "up") state_.controller.left_y = -1.0F;
    else if (logical_action == "down") state_.controller.left_y = 1.0F;
    else if (logical_action == "left") state_.controller.left_x = -1.0F;
    else if (logical_action == "right") state_.controller.left_x = 1.0F;
    else if (logical_action == "cross") state_.controller.cross = true;
    else if (logical_action == "circle") state_.controller.circle = true;
    else if (logical_action == "square") state_.controller.square = true;
    else if (logical_action == "triangle") state_.controller.triangle = true;
    else if (logical_action == "l1") state_.controller.l1 = true;
    else if (logical_action == "r1") state_.controller.r1 = true;
    else if (logical_action == "l2") state_.controller.l2 = 1.0F;
    else if (logical_action == "r2") state_.controller.r2 = 1.0F;
    else if (logical_action == "options") state_.controller.options = true;
    if (live()) {
        if (logical_action == "connect" || logical_action == "qr-retry" || logical_action == "qr-refresh") {
            queue_service_action(ServiceAction::BeginQr);
            state_.account.authentication = AuthState::Waiting;
            state_.account.challenge_url.clear(); state_.account.error.clear();
            state_.navigation.route = Route::SignIn; state_.navigation.focus_key = "qr-cancel";
            rebuild_view(); return true;
        }
        if (logical_action == "qr-cancel") {
            queue_service_action(ServiceAction::CancelQr);
            state_.account.authentication = AuthState::Cancelled;
            state_.navigation.route = Route::Startup; state_.navigation.focus_key = "startup-connect";
            rebuild_view(); return true;
        }
        if (logical_action == "account-sync" || logical_action == "refresh-library") {
            queue_service_action(ServiceAction::RefreshLibrary);
            state_.library_syncing = true;
            state_.library_loading = true;
            rebuild_view(); return true;
        }
        if (logical_action == "signout" || logical_action == "account-switch") {
            queue_service_action(logical_action == "signout" ? ServiceAction::SignOut : ServiceAction::SwitchAccount);
            state_.account.authentication = AuthState::SigningOut;
            rebuild_view(); return true;
        }
        const bool synthetic = logical_action.rfind("scenario:", 0) == 0 || logical_action.rfind("inject:", 0) == 0
            || logical_action.rfind("route:", 0) == 0 || logical_action.rfind("install:", 0) == 0
            || logical_action.rfind("job:", 0) == 0 || logical_action.rfind("session:", 0) == 0
            || logical_action == "advance-clock" || logical_action == "simulation-controls"
            || logical_action == "scenario-picker" || logical_action == "session-fixture-controls"
            || logical_action == "qr-fixture-controls" || logical_action == "logs-fixture-controls"
            || logical_action == "launch" || logical_action == "runtime-retry";
        if (synthetic) return false;
    }
    if (logical_action.rfind("collection:create:", 0) == 0) {
        const auto name = trimmed(logical_action.substr(18));
        if (name.empty() || std::any_of(state_.library.collections.begin(), state_.library.collections.end(), [&name](const Collection& value) { return value.name == name; })) return false;
        std::string id;
        do { id = "collection-" + std::to_string(next_collection_serial_++); }
        while (std::any_of(state_.library.collections.begin(), state_.library.collections.end(), [&id](const Collection& value) { return value.id == id; }));
        state_.library.collections.push_back({std::move(id), name, false, {}});
        save_profile(); rebuild_view(); return true;
    }
    if (logical_action.rfind("collection:add:", 0) == 0) {
        const auto remainder = logical_action.substr(15); const auto separator = remainder.find(':');
        if (separator == std::string::npos) return false;
        const auto id = remainder.substr(0, separator); GameId game_id = 0;
        const auto number = remainder.substr(separator + 1); const auto parsed = std::from_chars(number.data(), number.data() + number.size(), game_id);
        auto found = std::find_if(state_.library.collections.begin(), state_.library.collections.end(), [&id](const Collection& value) { return value.id == id; });
        if (parsed.ec != std::errc{} || found == state_.library.collections.end()) return false;
        found->members.insert(game_id); save_profile(); rebuild_view(); return true;
    }
    if (logical_action.rfind("collection:rename:", 0) == 0) {
        const auto remainder = logical_action.substr(18); const auto separator = remainder.find(':');
        if (separator == std::string::npos) return false;
        const auto id = remainder.substr(0, separator); const auto name = remainder.substr(separator + 1);
        auto found = std::find_if(state_.library.collections.begin(), state_.library.collections.end(), [&id](const Collection& value) { return value.id == id; });
        if (name.empty() || found == state_.library.collections.end()) return false;
        found->name = name; save_profile(); rebuild_view(); return true;
    }
    if (logical_action.rfind("collection:pin:", 0) == 0 || logical_action.rfind("collection:delete:", 0) == 0) {
        const bool remove = logical_action.rfind("collection:delete:", 0) == 0;
        const auto id = logical_action.substr(remove ? 18 : 15);
        auto found = std::find_if(state_.library.collections.begin(), state_.library.collections.end(), [&id](const Collection& value) { return value.id == id; });
        if (found == state_.library.collections.end()) return false;
        if (remove) {
            state_.library.collections.erase(found);
            if (state_.library.scope == id) {
                state_.library.scope = "all";
                state_.navigation.library_rail_focused = false;
                reconcile_library_selection(state_);
            }
        } else found->pinned = !found->pinned;
        save_profile(); rebuild_view(); return true;
    }
    if (logical_action.rfind("rate:", 0) == 0) {
        const auto remainder = logical_action.substr(5); const auto separator = remainder.find(':'); if (separator == std::string::npos) return false;
        GameId id = 0; const auto number = remainder.substr(0, separator); if (std::from_chars(number.data(), number.data() + number.size(), id).ec != std::errc{}) return false;
        const auto rating = remainder.substr(separator + 1);
        state_.library.ratings[id] = rating == "works" ? Compatibility::Works : rating == "playable" ? Compatibility::Playable : rating == "broken" ? Compatibility::Broken : Compatibility::Untested;
        save_profile(); rebuild_view(); return true;
    }
    if (logical_action.rfind("note:", 0) == 0) {
        const auto remainder = logical_action.substr(5); const auto separator = remainder.find(':'); if (separator == std::string::npos) return false;
        GameId id = 0; const auto number = remainder.substr(0, separator); if (std::from_chars(number.data(), number.data() + number.size(), id).ec != std::errc{}) return false;
        state_.library.notes[id] = remainder.substr(separator + 1); save_profile(); rebuild_view(); return true;
    }
    if (logical_action.rfind("install:", 0) == 0) {
        const auto remainder = logical_action.substr(8); const auto separator = remainder.find(':'); if (separator == std::string::npos) return false;
        GameId id = 0; const auto number = remainder.substr(0, separator); if (std::from_chars(number.data(), number.data() + number.size(), id).ec != std::errc{}) return false;
        const auto volume_id = remainder.substr(separator + 1);
        const auto assessment = assess_install(state_, id, volume_id);
        if (assessment.readiness != InstallReadiness::Ready || !assessment.download_bytes) return false;
        const auto existing = std::find_if(state_.jobs.begin(), state_.jobs.end(), [id](const Job& value) {
            return value.game_id == id && value.kind == "install" && value.state != JobState::Cancelled
                && value.state != JobState::Downloaded && value.state != JobState::Failed;
        });
        if (existing == state_.jobs.end()) {
            state_.jobs.push_back({next_job_id(), id, "install", next_queue_order(state_), JobStage::Download, JobState::Queued, 0, *assessment.download_bytes, std::nullopt, std::nullopt, {}, JobStage::Download, volume_id, 0, assessment.required_bytes.value_or(0), true, false});
            reconcile_storage(state_);
        }
        if (live() || state_.persistence_available) save_profile();
        rebuild_view(); return true;
    }
    if (logical_action.rfind("job:move-up:", 0) == 0) {
        auto* job = find_job(state_, logical_action.substr(12)); if (!job) return false;
        const int wanted = job->queue_order - 1; auto other = std::find_if(state_.jobs.begin(), state_.jobs.end(), [wanted](const Job& value) { return value.queue_order == wanted; });
        if (other != state_.jobs.end()) std::swap(job->queue_order, other->queue_order); save_profile(); rebuild_view(); return true;
    }
    if (logical_action.rfind("job:cancel:", 0) == 0) {
        const auto id = logical_action.substr(11); if (!find_job(state_, id)) return false;
        pending_destructive_action_ = "job-cancel-" + id; open_modal({"confirmation", "Cancel download?", "job-" + id, {"cancel", "confirm-destructive"}, 0, true}); rebuild_view(); return true;
    }
    if (logical_action.rfind("job:fail:", 0) == 0) {
        const auto remainder = logical_action.substr(9); const auto separator = remainder.find(':'); if (separator == std::string::npos) return false;
        auto* job = find_job(state_, remainder.substr(0, separator)); if (!job) return false;
        job->retry_stage = remainder.substr(separator + 1) == "verify" ? JobStage::Verify : JobStage::Download; job->stage = job->retry_stage; job->state = JobState::Failed; job->failure = "Injected failure"; save_profile(); rebuild_view(); return true;
    }
    if (logical_action.rfind("job:retry:", 0) == 0) {
        auto* job = find_job(state_, logical_action.substr(10)); if (!job) return false;
        for (auto& other : state_.jobs) if (&other != job && (other.state == JobState::Downloading || other.state == JobState::Verifying)) other.state = JobState::Queued;
        job->stage = job->retry_stage; job->state = job->stage == JobStage::Download ? JobState::Downloading : JobState::Verifying; job->failure.clear(); save_profile(); rebuild_view(); return true;
    }
    if (logical_action == "reset") {
        const auto installs = state_.installations; state_.library = {}; state_.installations = installs; clear_profile_customizations(state_); state_.reduced_motion = false; if (live()) { force_organization_reset_ = true; force_preferences_reset_ = true; } save_profile(); rebuild_view(); return true;
    }
    if (logical_action == "session:quit") {
        if (state_.session != SessionState::Running) return false; state_.session = SessionState::Stopping; rebuild_view(); return true;
    }
    if (logical_action.rfind("route:", 0) == 0) {
        const auto route = logical_action.substr(6);
        if (route == "home") state_.navigation.route = Route::Home;
        else if (route == "library") state_.navigation.route = Route::Library;
        else if (route == "downloads") state_.navigation.route = Route::Downloads;
        else if (route == "settings") state_.navigation.route = Route::Settings;
        else { last_error_ = "Unknown route: " + route; return false; }
        state_.navigation.modals.clear(); state_.editor.active = false; state_.navigation.focus_key.clear(); rebuild_view(); return true;
    }
    if (logical_action.rfind("select:", 0) == 0 || logical_action.rfind("details:", 0) == 0) {
        const bool details = logical_action.rfind("details:", 0) == 0;
        const auto text_id = logical_action.substr(details ? 8 : 7);
        GameId id = 0;
        const auto parsed = std::from_chars(text_id.data(), text_id.data() + text_id.size(), id);
        if (parsed.ec != std::errc{} || !find_game(state_, id)) { last_error_ = "Unknown game: " + text_id; return false; }
        state_.navigation.selected_game_id = id; state_.navigation.focus_key = game_key(id);
        if (live()) force_organization_selection_ = true;
        const auto games = visible_games();
        const auto found = std::find_if(games.begin(), games.end(), [id](const Game* game) { return game->id == id; });
        if (found != games.end()) state_.navigation.selected_index = static_cast<int>(std::distance(games.begin(), found));
        if (!details && state_.navigation.route == Route::Home) {
            const auto projected = playden::homebrew::shelves(state_);
            int chosen_shelf = -1;
            if (state_.navigation.home_shelf >= 0 && state_.navigation.home_shelf < static_cast<int>(projected.size())) {
                const auto& current = projected[static_cast<std::size_t>(state_.navigation.home_shelf)];
                if (std::find(current.game_ids.begin(), current.game_ids.end(), id) != current.game_ids.end()) chosen_shelf = state_.navigation.home_shelf;
            }
            if (chosen_shelf < 0) for (std::size_t index = 0; index < projected.size(); ++index)
                if (std::find(projected[index].game_ids.begin(), projected[index].game_ids.end(), id) != projected[index].game_ids.end()) { chosen_shelf = static_cast<int>(index); break; }
            if (chosen_shelf >= 0) {
                state_.navigation.home_shelf = chosen_shelf;
                const auto& shelf = projected[static_cast<std::size_t>(chosen_shelf)];
                state_.navigation.home_shelf_focus[shelf.key] = static_cast<int>(std::distance(shelf.game_ids.begin(), std::find(shelf.game_ids.begin(), shelf.game_ids.end(), id)));
                state_.navigation.scroll_offsets["home"] = chosen_shelf;
            }
        }
        if (details) { state_.navigation.detail_origin = state_.navigation.route; state_.navigation.route = Route::Details; state_.navigation.detail_id = id; details_primary_pending_ = live(); state_.navigation.focus_key = live() ? "details-favorite" : (is_installed(state_, id) ? "details-play" : "details-install"); }
        else if (state_.navigation.route == Route::Downloads) {
            const auto job = std::find_if(state_.jobs.begin(), state_.jobs.end(), [id](const Job& value) { return value.game_id == id && value.state != JobState::Cancelled; });
            if (job != state_.jobs.end()) state_.navigation.focus_key = "job-" + job->id;
        }
        save_profile(); rebuild_view(); return true;
    }
    if (logical_action.rfind("focus:", 0) == 0) {
        const auto key = logical_action.substr(6);
        const auto found = std::find_if(view_.rows.begin(), view_.rows.end(), [&key](const ViewRow& row) { return row.key == key; });
        if (found == view_.rows.end()) { last_error_ = "Unknown focus key: " + key; return false; }
        state_.navigation.focus_key = key; state_.navigation.selected_index = static_cast<int>(std::distance(view_.rows.begin(), found));
        if (!state_.navigation.modals.empty()) state_.navigation.modals.back().focused_action = state_.navigation.selected_index;
        save_profile(); rebuild_view(); return true;
    }
    if (logical_action.rfind("scenario:", 0) == 0) return scenario(logical_action.substr(9));
    if (logical_action == "advance-clock") { tick(1); return true; }
    if (logical_action == "done") { if (state_.editor.active) commit_editor(); rebuild_view(); return true; }
    if (logical_action == "backspace") {
        if (state_.editor.active && state_.editor.caret > 0) { const auto prior = previous_caret(state_.editor.value, state_.editor.caret); state_.editor.value.erase(prior, state_.editor.caret - prior); state_.editor.caret = prior; }
        rebuild_view(); return true;
    }
    if (logical_action == "clear") { if (state_.editor.active) { state_.editor.value.clear(); state_.editor.caret = 0; } rebuild_view(); return true; }
    if (logical_action == "space") { if (state_.editor.active) { state_.editor.value.insert(state_.editor.caret, " "); ++state_.editor.caret; } rebuild_view(); return true; }
    if (logical_action == "shift") { if (state_.editor.active) state_.editor.shifted = !state_.editor.shifted; rebuild_view(); return true; }
    if (logical_action == "symbols") { if (state_.editor.active) state_.editor.symbols = !state_.editor.symbols; rebuild_view(); return true; }
    if (logical_action == "caret-left") { if (state_.editor.active) state_.editor.caret = previous_caret(state_.editor.value, state_.editor.caret); rebuild_view(); return true; }
    if (logical_action == "caret-right") { if (state_.editor.active) state_.editor.caret = next_caret(state_.editor.value, state_.editor.caret); rebuild_view(); return true; }
    if (state_.editor.active && logical_action == "l1") { state_.editor.caret = previous_caret(state_.editor.value, state_.editor.caret); rebuild_view(); return true; }
    if (state_.editor.active && logical_action == "r1") { state_.editor.caret = next_caret(state_.editor.value, state_.editor.caret); rebuild_view(); return true; }
    if (state_.editor.active && logical_action == "triangle") { state_.editor.value.clear(); state_.editor.caret = 0; rebuild_view(); return true; }
    if (state_.editor.active && (logical_action == "l2" || logical_action == "r2" || logical_action == "options" || logical_action == "search")) return true;
    if (!state_.editor.active && state_.navigation.modals.empty() && state_.navigation.route == Route::Details
        && (logical_action == "l2" || logical_action == "r2")) {
        auto& offset = state_.navigation.scroll_offsets["details-content"];
        const auto* game = find_game(state_, state_.navigation.detail_id.value_or(state_.navigation.selected_game_id));
        const auto page = DetailsDescriptionPage(state_.simulation_mode);
        const int last_page = game ? std::max(0, static_cast<int>((game->description.size() + page - 1) / page) - 1) : 0;
        offset = std::clamp(offset + (logical_action == "l2" ? -1 : 1), 0, last_page);
        rebuild_view(); return true;
    }
    if (logical_action == "up") move(0, -1);
    else if (logical_action == "down") move(0, 1);
    else if (logical_action == "left") move(-1, 0);
    else if (logical_action == "right") move(1, 0);
    else if (logical_action == "l2") move(0, -4);
    else if (logical_action == "r2") move(0, 4);
    else if (logical_action == "l1") change_route(-1);
    else if (logical_action == "r1") change_route(1);
    else if (logical_action == "cross") activate();
    else if (logical_action == "circle") cancel();
    else if (logical_action == "search") { if (!state_.editor.active && state_.navigation.route == Route::Library && state_.navigation.modals.empty()) start_editor("Search", state_.library.query); }
    else if (logical_action == "triangle" && !state_.editor.active && !state_.navigation.modals.empty() &&
             (state_.navigation.modals.back().id == "game-settings" || !state_.navigation.modals.back().setting_id.empty())) {
        // Triangle returns the focused setting (or the open picker's setting) to the preset value.
        const auto& modal = state_.navigation.modals.back();
        const auto id = !modal.setting_id.empty() ? modal.setting_id
            : state_.navigation.focus_key.rfind("setting-", 0) == 0 ? state_.navigation.focus_key.substr(8) : std::string{};
        auto record = state_.game_settings.find(state_.navigation.selected_game_id);
        if (!id.empty() && record != state_.game_settings.end() && record->second.overrides.count(id) &&
            can_change_game_settings() && record->second.overrides.erase(id)) {
            ++record->second.revision;
            if (!modal.setting_id.empty()) close_modal();
            refresh_game_settings(); save_profile();
        }
    }
    else if (logical_action == "triangle") {
        if (!state_.editor.active && state_.navigation.route == Route::Library && state_.navigation.modals.empty()) {
            // Live mode offers only sorts and filters backed by facts the library actually has.
            // A restored "recent-added" sort stays listed so it is visible and can be changed.
            const bool dated = state_.library.sort == "recent-added" ||
                std::any_of(state_.games.begin(), state_.games.end(),
                            [](const Game& game) { return game.added_at.has_value(); });
            std::vector<std::string> actions = live()
            ? (dated ? std::vector<std::string>{"sort-name", "sort-recent-played", "sort-recent-added", "sort-playtime",
                                                "filter-compat-playable", "filters-reset", "cancel"}
                     : std::vector<std::string>{"sort-name", "sort-recent-played", "sort-playtime", "filter-compat-playable",
                                                "filters-reset", "cancel"})
            : std::vector<std::string>{"sort-name", "sort-recent-played", "sort-recent-added", "sort-playtime", "sort-install-size",
                                      "filter-installed", "filter-genre-adventure", "filter-controller-full", "filter-compat-playable",
                                      "filters-reset", "cancel"};
        open_modal({"filters", "Sort and filters", state_.navigation.focus_key, std::move(actions), 0, false});
        }
    }
    else if (logical_action == "options") {
        if (!state_.editor.active && state_.navigation.route == Route::Details && state_.navigation.modals.empty()) {
            if (live()) force_organization_selection_ = true;
            open_modal({"context", "Game actions", "details-more", context_actions(live()), 0, false});
        } else if (!state_.editor.active && state_.navigation.route == Route::Home && state_.navigation.selected_game_id != 0 && state_.navigation.modals.empty()) {
            if (live()) force_organization_selection_ = true;
            open_modal({"context", "Game actions", state_.navigation.focus_key, context_actions(live()), 0, false});
        }
        else if (!state_.editor.active && state_.navigation.route == Route::Library && !state_.navigation.modals.empty()) {}
        else if (!state_.editor.active && state_.navigation.route == Route::Library && state_.navigation.selected_game_id != 0 && state_.navigation.modals.empty()
                 && state_.navigation.focus_key.rfind("game-", 0) == 0) {
            if (live()) force_organization_selection_ = true;
            open_modal({"context", "Game actions", state_.navigation.focus_key, {"favorite", "collection", "note", "hide", "cancel"}, 0, false});
        }
        else if (!state_.editor.active && state_.navigation.route == Route::Downloads && state_.navigation.modals.empty()) activate();
    } else if (logical_action == "square") {
        if (state_.editor.active) {
            if (state_.editor.caret > 0) { const auto prior = previous_caret(state_.editor.value, state_.editor.caret); state_.editor.value.erase(prior, state_.editor.caret - prior); state_.editor.caret = prior; }
        } else if (state_.navigation.modals.empty() && (state_.navigation.route == Route::Home || state_.navigation.route == Route::Library || state_.navigation.route == Route::Details) && state_.navigation.selected_game_id != 0) {
            if (state_.library.favorites.contains(state_.navigation.selected_game_id)) state_.library.favorites.erase(state_.navigation.selected_game_id);
            else state_.library.favorites.insert(state_.navigation.selected_game_id);
            save_profile();
        }
    } else if (logical_action.rfind("inject:", 0) == 0) {
        auto injected = logical_action.substr(7);
        if (injected == "controller-disconnect") injected = "controller-disconnected";
        if (injected == "controller-reconnect") injected = "controller-reconnected";
        if (injected == "storage-disconnect") {
            if (auto* volume = find_volume(state_, "external")) volume->available = false;
            for (auto& installation : state_.installations) if (installation.volume_id == "external") installation.available = false;
            for (auto& job : state_.jobs) if (job.volume_id == "external" && (job.state == JobState::Downloading || job.state == JobState::Verifying)) { job.state = JobState::Interrupted; job.failure = "Storage disconnected"; }
            state_.notices.push_back({"storage", "USB fixture library disconnected", "storage-retry", true, 0});
        } else if (injected == "storage-reconnect") {
            if (auto* volume = find_volume(state_, "external")) volume->available = true;
            for (auto& installation : state_.installations) if (installation.volume_id == "external" && installation.installed_bytes > 0) installation.available = true;
            state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(), [](const Notice& notice) { return notice.id == "storage"; }), state_.notices.end());
        } else if (injected == "controller-disconnected") {
            state_.controller_connected = false;
            state_.notices.push_back({"controller", "Controller disconnected", "controller-test", true, 0});
        } else if (injected == "controller-reconnected") {
            state_.controller_connected = true;
            state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(), [](const Notice& notice) { return notice.id == "controller"; }), state_.notices.end());
        } else if (injected == "download-failed" || injected == "verification-failed") {
            Job* job = nullptr;
            const auto wanted_stage = injected == "verification-failed" ? JobStage::Verify : JobStage::Download;
            const auto active = std::find_if(state_.jobs.begin(), state_.jobs.end(), [wanted_stage](const Job& value) {
                return value.stage == wanted_stage && (value.state == JobState::Downloading || value.state == JobState::Verifying);
            });
            if (active != state_.jobs.end()) job = &*active;
            if (!job && state_.navigation.focus_key.rfind("job-", 0) == 0) job = find_job(state_, state_.navigation.focus_key.substr(4));
            if (!job) job = find_job(state_, "job-1");
            if (job) { job->state = JobState::Failed; job->retry_stage = wanted_stage; job->stage = job->retry_stage; job->failure = injected == "verification-failed" ? "Verification failed" : "Download failed"; }
        } else if (injected == "uninstall-failed") {
            const auto job = std::find_if(state_.jobs.begin(), state_.jobs.end(), [](const Job& value) { return value.kind == "uninstall" && value.state != JobState::Downloaded; });
            if (job != state_.jobs.end()) { job->state = JobState::Failed; job->stage = JobStage::Uninstall; job->retry_stage = JobStage::Uninstall; job->failure = "Uninstall failed"; }
        } else if (injected == "launch-failure" || injected == "stop-failure") {
            state_.session = SessionState::Failed; state_.session_error = injected == "launch-failure" ? "Simulated launch failed" : "Game did not stop"; state_.navigation.route = Route::Session; state_.navigation.focus_key = "session-retry";
        } else if (injected == "crash") {
            state_.session = SessionState::Crashed; state_.session_error = "Game exited unexpectedly"; state_.navigation.route = Route::Session; state_.navigation.focus_key = "session-logs";
        } else if (injected == "session-running") {
            state_.session = SessionState::Running; state_.session_error.clear(); state_.navigation.route = Route::Session; state_.navigation.focus_key = "session-return";
        } else if (injected == "runtime-ready") state_.runtime = RuntimeState::Ready;
        else if (injected == "runtime-failed") state_.runtime = RuntimeState::Failed;
        else if (injected == "logs-loading") start_logs();
        else if (injected == "logs-ready") finish_logs();
        else if (injected == "logs-empty") { state_.log_state = LogState::Ready; state_.log_lines.clear(); state_.log_offset = 0; }
        else if (injected == "logs-failure") {
            state_.log_state = LogState::Failed; state_.log_lines.clear(); state_.log_offset = 0;
            if (!state_.navigation.modals.empty() && state_.navigation.modals.back().id == "logs") {
                state_.navigation.modals.back().action_keys = {"logs-retry", "logs-fixture-controls", "logs-back"};
                state_.navigation.modals.back().focused_action = 0; state_.navigation.focus_key = "logs-retry";
            }
        } else if (injected == "delayed-art" || injected == "failed-art") {
            const GameId id = injected == "delayed-art" ? 900007 : 900008;
            auto game = std::find_if(state_.games.begin(), state_.games.end(), [id](const Game& value) { return value.id == id; });
            if (game != state_.games.end()) { game->art_pending = injected == "delayed-art"; game->art_failed = injected == "failed-art"; }
        } else if (injected == "library-refresh") state_.library_loading = true;
        else if (injected == "startup-loading") { state_.startup_loading = true; state_.navigation.route = Route::Startup; state_.navigation.focus_key = "startup-loading"; }
        else if (injected == "late-approval") {
            if (state_.account.authentication != AuthState::Cancelled && challenge_generation_ != cancelled_challenge_generation_) state_.account.authentication = AuthState::Syncing;
        } else {
            if (!scenario(injected)) return false;
            if (live() || state_.persistence_available) save_profile();
            return true;
        }
        if (live() || state_.persistence_available) save_profile();
        rebuild_view(); return true;
    }
    else { last_error_ = "Unknown action: " + logical_action; return false; }
    show_ready_prompt();  // a ready-to-play prompt waits for the player to close what was open
    rebuild_view();
    if (live() || state_.persistence_available) save_profile();
    return true;
}

void Simulation::text(const std::string& utf8) {
    if (!state_.editor.active) return;
    state_.editor.value.insert(state_.editor.caret, utf8);
    state_.editor.caret += utf8.size();
    rebuild_view();
}

void Simulation::controller_sample(ControllerState sample) {
    state_.controller = sample;
}

void Simulation::advance_jobs() {
    for (auto& job : state_.jobs) if (job.state == JobState::Pausing) job.state = JobState::Paused;
    Job* active = nullptr;
    for (auto& job : state_.jobs) if (job.state == JobState::Downloading || job.state == JobState::Verifying) { active = &job; break; }
    if (!active) {
        auto queued = std::min_element(state_.jobs.begin(), state_.jobs.end(), [](const Job& left, const Job& right) {
            const int left_order = left.state == JobState::Queued ? left.queue_order : std::numeric_limits<int>::max();
            const int right_order = right.state == JobState::Queued ? right.queue_order : std::numeric_limits<int>::max();
            return left_order < right_order;
        });
        if (queued != state_.jobs.end() && queued->state == JobState::Queued)
            queued->state = queued->stage == JobStage::Download ? JobState::Downloading : JobState::Verifying;
        return;
    }
    if (active->state == JobState::Downloading) {
        const auto* volume = find_volume(state_, active->volume_id);
        if (!volume || !volume->available) { active->state = JobState::Interrupted; active->failure = "Storage disconnected"; return; }
        active->completed_bytes = std::min(active->total_bytes, active->completed_bytes + kTransferPerTick);
        if (active->completed_bytes >= active->total_bytes) { active->stage = JobStage::Verify; active->state = JobState::Verifying; active->stage_ticks = 0; }
    } else if (active->state == JobState::Verifying) {
        ++active->stage_ticks;
        if (active->stage_ticks >= 2) {
            active->state = JobState::Downloaded;
            active->completed_bytes = active->total_bytes;
            reconcile_storage(state_);
            const auto message = active->kind == "verify" ? "Verification complete"
                : active->kind == "uninstall" ? "Uninstall complete; saves preserved" : "Download complete";
            state_.notices.push_back({"job-complete-" + active->id, message, "open-game:" + std::to_string(active->game_id), false, 5});
        }
    }
}

void Simulation::tick(int ticks) {
    if (live()) {
        rebuild_view();
        return;
    }
    for (int index = 0; index < std::max(0, ticks); ++index) {
        if (!state_.screenshot_mode) {
            ++state_.clock_ticks;
            advance_jobs();
            if (state_.account.authentication == AuthState::Waiting && state_.clock_ticks >= state_.account.challenge_expiry) {
                state_.account.authentication = AuthState::Expired;
                state_.navigation.focus_key = "qr-refresh";
            }
            if (state_.account.authentication == AuthState::Syncing) {
                state_.account.authentication = AuthState::SignedIn;
                state_.account.cache_available = true;
                state_.account.last_sync = state_.clock_ticks;
                state_.library_syncing = false;
                state_.navigation.route = Route::Home;
                state_.navigation.focus_key = game_key(state_.navigation.selected_game_id);
            }
            if (state_.session == SessionState::Preparing) state_.session = SessionState::Launching;
            else if (state_.session == SessionState::Launching) state_.session = SessionState::Running;
            else if (state_.session == SessionState::Stopping) { state_.session = SessionState::Idle; state_.navigation.route = Route::Details; state_.navigation.detail_id = state_.session_game_id; }
            if (state_.runtime == RuntimeState::Checking) state_.runtime = RuntimeState::Ready;
            if (state_.log_state == LogState::Loading) finish_logs();
            state_.startup_loading = false;
            state_.library_loading = false;
            for (auto& game : state_.games) if (game.art_pending) game.art_pending = false;
            for (auto& notice : state_.notices) if (!notice.persistent && notice.remaining_ticks > 0) --notice.remaining_ticks;
            state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(), [](const Notice& notice) { return !notice.persistent && notice.remaining_ticks == 0; }), state_.notices.end());
        }
    }
    if (ticks > 0 && (live() || state_.persistence_available)) save_profile();
    rebuild_view();
}

std::string Simulation::snapshot() const {
    StringBuilder output;
    const auto visible = visible_games();
    output << "{\"simulation\":" << (state_.simulation_mode ? "true" : "false") << ",\"clock\":" << state_.clock_ticks << ",\"route\":\""
           << route_name(state_.navigation.route) << "\",\"surface\":\"" << json::escape(view_.surface)
           << "\",\"focus\":\"" << json::escape(state_.navigation.focus_key) << "\",\"selected_game_id\":"
           << state_.navigation.selected_game_id << ",\"account\":\"" << auth_name(state_.account.authentication)
           << "\",\"detail_id\":";
    if (state_.navigation.detail_id) output << *state_.navigation.detail_id; else output << "null";
    output << ",\"query\":\"" << json::escape(state_.library.query) << "\",\"scope\":\""
           << json::escape(state_.library.scope) << "\",\"sort\":\"" << json::escape(state_.library.sort)
           << "\",\"visible_count\":" << visible.size() << ",\"materialized_row_count\":" << view_.rows.size()
           << ",\"modal_depth\":" << state_.navigation.modals.size()
           << ",\"startup_loading\":" << (state_.startup_loading ? "true" : "false")
           << ",\"library_loading\":" << (state_.library_loading ? "true" : "false")
           << ",\"logs\":{\"state\":\"" << (state_.log_state == LogState::Loading ? "loading" : state_.log_state == LogState::Failed ? "failed" : "ready")
           << "\",\"context\":\"" << json::escape(state_.log_context) << "\",\"count\":" << state_.log_lines.size()
           << ",\"offset\":" << state_.log_offset << "}"
           << ",\"editor\":{\"active\":" << (state_.editor.active ? "true" : "false") << ",\"value\":\""
           << json::escape(state_.editor.value) << "\",\"original\":\"" << json::escape(state_.editor.original)
           << "\",\"caret\":" << state_.editor.caret << "},\"scroll_offsets\":{";
    bool first = true;
    for (const auto& [key, value] : state_.navigation.scroll_offsets) {
        if (!first) output << ','; first = false;
        output << '"' << json::escape(key) << "\":" << value;
    }
    output << "},\"filters\":[";
    first = true;
    for (const auto& filter : state_.library.filters) { if (!first) output << ','; first = false; output << '"' << json::escape(filter) << '"'; }
    output << "],\"jobs\":[";
    first = true;
    for (const auto& job : state_.jobs) {
        if (!first) output << ',';
        first = false;
        output << "{\"id\":\"" << json::escape(job.id) << "\",\"game_id\":" << job.game_id
               << ",\"state\":\"" << job_state_name(job.state) << "\",\"stage\":\"" << job_stage_name(job.stage)
               << "\",\"retry_stage\":\"" << job_stage_name(job.retry_stage) << "\",\"volume\":\"" << json::escape(job.volume_id)
               << "\",\"completed\":" << job.completed_bytes << ",\"total\":" << job.total_bytes
               << ",\"stage_ticks\":" << job.stage_ticks << ",\"reserved\":" << job.reserved_bytes
               << ",\"reservation_held\":" << (job.reservation_held ? "true" : "false")
               << ",\"completion_applied\":" << (job.completion_applied ? "true" : "false")
               << ",\"failure\":\"" << json::escape(job.failure) << "\"}";
    }
    output << "],\"installations\":[";
    first = true;
    for (const auto& installation : state_.installations) {
        if (!first) output << ','; first = false;
        output << "{\"game_id\":" << installation.game_id << ",\"volume\":\"" << json::escape(installation.volume_id)
               << "\",\"available\":" << (installation.available ? "true" : "false") << ",\"runtime_ready\":"
               << (installation.runtime_ready ? "true" : "false") << ",\"saves_present\":" << (installation.saves_present ? "true" : "false") << '}';
    }
    output << "],\"volumes\":[";
    first = true;
    for (const auto& volume : state_.volumes) {
        if (!first) output << ','; first = false;
        output << "{\"id\":\"" << json::escape(volume.id) << "\",\"used\":" << volume.used_bytes
               << ",\"reserved\":" << volume.reserved_bytes << ",\"capacity\":" << volume.capacity_bytes
               << ",\"available\":" << (volume.available ? "true" : "false") << '}';
    }
    output << "],\"game_settings\":[";
    first = true;
    for (const auto& [id, record] : state_.game_settings) {
        if (!first) output << ','; first = false;
        output << "{\"game_id\":" << id << ",\"settings\":" << rt::SerializeGameSettingsRecord(record) << '}';
    }
    output << "],\"collections\":[";
    first = true;
    for (const auto& collection : state_.library.collections) {
        if (!first) output << ','; first = false;
        output << "{\"id\":\"" << json::escape(collection.id) << "\",\"name\":\"" << json::escape(collection.name)
               << "\",\"pinned\":" << (collection.pinned ? "true" : "false") << ",\"members\":[";
        bool first_member = true;
        for (const auto id : collection.members) { if (!first_member) output << ','; first_member = false; output << id; }
        output << "]}";
    }
    output << "],\"reduced_motion\":" << (state_.reduced_motion ? "true" : "false")
           << ",\"controller_connected\":" << (state_.controller_connected ? "true" : "false")
           << ",\"controller\":{\"left_x_milli\":" << static_cast<int>(state_.controller.left_x * 1000.0F)
           << ",\"left_y_milli\":" << static_cast<int>(state_.controller.left_y * 1000.0F)
           << ",\"right_x_milli\":" << static_cast<int>(state_.controller.right_x * 1000.0F)
           << ",\"right_y_milli\":" << static_cast<int>(state_.controller.right_y * 1000.0F)
           << ",\"l2_milli\":" << static_cast<int>(state_.controller.l2 * 1000.0F)
           << ",\"r2_milli\":" << static_cast<int>(state_.controller.r2 * 1000.0F) << "}}";
    return output.value;
}


void Simulation::SetSettingsData(std::vector<rt::SettingsPreset> presets, std::vector<rt::GameRecipe> recipes) {
    settings_presets_ = std::move(presets);
    recipes_.clear();
    for (auto& recipe : recipes) recipes_[recipe.app_id] = std::move(recipe);
    simulated_recipes_.clear();
    rebuild_view();
}

void Simulation::SetGameSettingsFile(std::filesystem::path path) {
    game_settings_file_ = std::move(path);
    std::map<std::uint32_t, rt::GameSettingsRecord> records;
    std::string reason;
    settings_load_failed_ = !rt::LoadGameSettingsFile(game_settings_file_.string(), &records, &reason);
    if (settings_load_failed_) {
        push_notice({"settings-load-failed", "Game settings could not be read", "", false, kLiveNoticeSeconds, true, reason});
        rebuild_view();
        return;
    }
    state_.game_settings.clear();
    for (auto& [id, record] : records) state_.game_settings[static_cast<GameId>(id)] = std::move(record);
    saved_game_settings_ = state_.game_settings;
    rebuild_view();
}

void Simulation::SetKitCapabilities(rt::KitCapabilities kit) {
    kit_ = kit;
    refresh_game_settings();
    rebuild_view();
}

void Simulation::SetGeneratedRecipes(const std::vector<rt::GameRecipe>& recipes) {
    std::map<std::uint32_t, rt::GameRecipe> generated;
    for (const auto& recipe : recipes) generated[recipe.app_id] = recipe;
    if (generated.size() == generated_recipes_.size() &&
        std::equal(generated.begin(), generated.end(), generated_recipes_.begin(), [](const auto& a, const auto& b) {
            return a.first == b.first && rt::SerializeGameRecipe(a.second) == rt::SerializeGameRecipe(b.second);
        }))
        return;
    generated_recipes_ = std::move(generated);
    refresh_game_settings();
    rebuild_view();
}

const rt::GameRecipe& Simulation::RecipeFor(GameId id) const {
    if (const auto found = recipes_.find(static_cast<std::uint32_t>(id)); found != recipes_.end()) return found->second;
    if (const auto found = generated_recipes_.find(static_cast<std::uint32_t>(id)); found != generated_recipes_.end())
        return found->second;
    // Games not prepared yet get a generic one so the sheet can be shown; preparing a real
    // install generates the game's own from Steam's launch options.
    auto& recipe = simulated_recipes_[id];
    if (recipe.launch_entries.empty()) {
        const auto* game = find_game(state_, id);
        recipe.id = "simulated-" + std::to_string(id);
        recipe.version = 1;
        recipe.app_id = static_cast<std::uint32_t>(id);
        recipe.name = game ? game->title : "Game " + std::to_string(id);
        recipe.launch_entries = {{"game", recipe.name, "game.exe", ""}};
        recipe.arguments = "{resolution}";
        recipe.graphics = "dxvk";
    }
    return recipe;
}

const rt::SettingsPreset* Simulation::preset_for(GameId id) const {
    const auto record = state_.game_settings.find(id);
    const std::string wanted = record == state_.game_settings.end() || record->second.preset_id.empty()
        ? std::string("playden-default") : record->second.preset_id;
    for (const auto& preset : settings_presets_)
        if (preset.id == wanted) return &preset;
    return nullptr;
}

bool Simulation::EffectiveSettingsFor(GameId id, rt::EffectiveSettings* settings) const {
    const auto record = state_.game_settings.find(id);
    static const std::map<std::string, std::string> none;
    rt::SettingsError error;
    return rt::BuildEffectiveSettings(RecipeFor(id), preset_for(id), record == state_.game_settings.end() ? none : record->second.overrides,
                                      kit_, settings, &error);
}

std::vector<std::string> Simulation::game_settings_actions() const {
    const auto shown = [this](const rt::SettingDefinition& definition) { return rt::SettingShown(definition, kit_); };
    std::vector<std::string> actions = {"preset-picker"};
    for (const auto& definition : rt::SettingsCatalogue())
        if (definition.tier == rt::SettingTier::kBasic && shown(definition)) actions.push_back("setting-" + definition.id);
    for (const auto& [section, tier] : {std::pair<const char*, rt::SettingTier>{"section-more", rt::SettingTier::kCompatibility},
                                       std::pair<const char*, rt::SettingTier>{"section-advanced", rt::SettingTier::kAdvanced}}) {
        actions.push_back(section);
        if (!expanded_settings_sections_.count(section)) continue;
        if (tier == rt::SettingTier::kCompatibility) actions.push_back("button-mapping");
        for (const auto& definition : rt::SettingsCatalogue())
            if (definition.tier == tier && shown(definition)) actions.push_back("setting-" + definition.id);
    }
    actions.push_back("reset-settings");
    actions.push_back("clear-shader-cache");
    actions.push_back("cancel");
    return actions;
}

void Simulation::refresh_game_settings() {
    for (auto& modal : state_.navigation.modals) {
        if (modal.id != "game-settings") continue;
        const auto focused = modal.action_keys[static_cast<std::size_t>(modal.focused_action)];
        modal.action_keys = game_settings_actions();
        const auto found = std::find(modal.action_keys.begin(), modal.action_keys.end(), focused);
        modal.focused_action = found == modal.action_keys.end() ? 0 : static_cast<int>(found - modal.action_keys.begin());
        if (&modal == &state_.navigation.modals.back()) state_.navigation.focus_key = modal.action_keys[static_cast<std::size_t>(modal.focused_action)];
    }
}

void Simulation::announce_settings_saved() {
    if (!settings_opened_record_) return;
    const auto before = *settings_opened_record_;
    settings_opened_record_.reset();
    if (settings_save_failed_ || settings_load_failed_) return;
    const auto found = state_.game_settings.find(state_.navigation.selected_game_id);
    const auto after = found == state_.game_settings.end() ? rt::GameSettingsRecord{} : found->second;
    // Count settings whose stored choice differs; a preset change counts once.
    std::set<std::string> ids;
    for (const auto& [id, value] : before.overrides) ids.insert(id);
    for (const auto& [id, value] : after.overrides) ids.insert(id);
    int changes = before.preset_id != after.preset_id ? 1 : 0;
    for (const auto& id : ids) {
        const auto old_value = before.overrides.find(id);
        const auto new_value = after.overrides.find(id);
        if ((old_value == before.overrides.end()) != (new_value == after.overrides.end()) ||
            (old_value != before.overrides.end() && old_value->second != new_value->second))
            ++changes;
    }
    if (changes == 0) return;
    const auto* preset = preset_for(state_.navigation.selected_game_id);
    const std::string name = preset ? preset->name : "Playden default";
    push_notice({"settings-saved", "Settings saved · " + (after.overrides.empty() ? name : "Custom"), "", false,
                 kLiveNoticeSeconds, false,
                 std::to_string(changes) + (changes == 1 ? " change applies" : " changes apply") + " on next launch"});
}

bool Simulation::can_change_game_settings() {
    const auto found = state_.game_settings.find(state_.navigation.selected_game_id);
    if (found == state_.game_settings.end() || found->second.revision != std::numeric_limits<std::uint64_t>::max()) return true;
    push_notice({"settings-revision-exhausted", "Game settings could not be changed", "", false,
                 kLiveNoticeSeconds, true, "The settings revision has reached its limit."});
    return false;
}

void Simulation::set_game_setting(const std::string& id, const std::string& value) {
    if (!can_change_game_settings()) return;
    const auto game = state_.navigation.selected_game_id;
    auto& record = state_.game_settings[game];
    auto overrides = record.overrides;
    overrides.erase(id);
    rt::EffectiveSettings base;
    rt::SettingsError error;
    // Choosing the inherited value removes the override, so the row is no longer "changed".
    const bool inherited = rt::BuildEffectiveSettings(RecipeFor(game), preset_for(game), overrides, kit_, &base, &error)
        && base.values.at(id).value == value;
    if (inherited) record.overrides.erase(id);
    else record.overrides[id] = value;
    ++record.revision;
}

// Player words for a preparation journal step; never the step's identifier.
std::string PreparationStepWords(const std::string& step) {
    if (step == "stage-title") return "Setting up the game";
    if (step == "copy-template") return "Copying the Windows environment";
    if (step == "validate-artifacts") return "Checking the game's files";
    if (step == "publish-profile" || step == "capture-settings" || step == "capture-update" ||
        step == "initialize-control" || step == "control-ready")
        return "Applying settings";
    if (step == "query-native" || step == "register" || step == "await-native") return "Waiting for the PS5";
    return step.empty() ? std::string{} : "Preparing";
}

} // namespace playden::homebrew
