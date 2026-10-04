#include "library/query.h"
// simulation.cpp - Deterministic state transitions for the Playden UI comparison.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT

#include "homebrew/simulation.hpp"

#include "homebrew/browse.hpp"
#include "homebrew/json.hpp"
#include "homebrew/storage_sim.hpp"

#include <algorithm>
#include <cstdio>
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

Profile& custom_profile(State& state, GameId id) {
    if (const auto found = state.custom_profiles.find(id); found != state.custom_profiles.end()) return found->second;
    Profile profile;
    if (!state.profiles.empty()) profile = state.profiles.front();
    profile.id = "custom";
    profile.name = "Custom";
    profile.summary = "Default values plus your overrides";
    profile.is_default = false;
    return state.custom_profiles.emplace(id, std::move(profile)).first->second;
}

const Profile* effective_profile(const State& state, GameId id, const std::string& selected_id) {
    if (selected_id == "custom") {
        if (const auto found = state.custom_profiles.find(id); found != state.custom_profiles.end()) return &found->second;
    }
    const auto found = std::find_if(state.profiles.begin(), state.profiles.end(), [&selected_id](const Profile& profile) { return profile.id == selected_id; });
    return found == state.profiles.end() ? (state.profiles.empty() ? nullptr : &state.profiles.front()) : &*found;
}

std::string game_key(GameId id);

void clear_profile_customizations(State& state) {
    state.selected_profiles.clear();
    state.custom_profiles.clear();
    if (state.profiles.size() > 1) {
        auto custom = state.profiles.front();
        custom.id = "custom"; custom.name = "Custom"; custom.summary = "Default values plus your overrides"; custom.is_default = false;
        state.profiles[1] = std::move(custom);
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
    if (live) return {"favorite", "collection", "note", "hide", "cancel"};
    return {"favorite", "collection", "note", "hide", "verify", "uninstall", "logs", "cancel"};
}

std::string action_label(const std::string& key) {
    static const std::map<std::string, std::string> labels = {
        {"cancel", "Cancel"}, {"confirm-destructive", "Confirm"}, {"favorite", "Favorite"},
        {"collection", "Collections"}, {"note", "Compatibility and note"}, {"verify", "Verify files"},
        {"uninstall", "Uninstall"}, {"logs", "Open logs"}, {"logs-back", "Back"},
        {"install-confirm", "Install"}, {"storage-picker", "Change storage"},
        {"profile-picker", "Profile"}, {"resolution-picker", "Resolution"}, {"frame-picker", "Frame limit"},
        {"toggle-vsync", "VSync"}, {"edit-arguments", "Launch arguments"}, {"reset-settings", "Reset all overrides"},
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
    selected_profile_id_ = "default";
    next_job_serial_ = 1;
    last_error_.clear();
    captured_organization_ = {};
    persisted_organization_ = {};
    captured_preferences_ = {};
}

void Simulation::SetControllerConnected(bool connected) {
    state_.controller_connected = connected;
    rebuild_view();
}

void Simulation::SetWallClock(std::int64_t unix_seconds) {
    if (!live() || state_.clock_ticks == unix_seconds) return;
    state_.clock_ticks = unix_seconds;
    rebuild_view();
}

std::vector<ServiceAction> Simulation::TakeServiceActions() {
    auto actions = std::move(pending_service_actions_);
    pending_service_actions_.clear();
    return actions;
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
    state_.profiles.clear();
    state_.selected_profiles.clear();
    state_.custom_profiles.clear();
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
    selected_profile_id_.clear();
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
            else state_.navigation.focus_key = live() ? "details-favorite"
                                                       : (is_installed(state_, state_.navigation.selected_game_id)
                                                              ? "details-play" : "details-install");
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
    return {state_.reduced_motion};
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
                                          static_cast<int>(shelf.game_ids.size()) - 1);
        home_location = std::pair<int, int>{shelf_index, item_index};
    }
    if (home_location) {
        const auto [shelf_index, item_index] = *home_location;
        const auto& shelf = projected[static_cast<std::size_t>(shelf_index)];
        home_candidate = shelf.game_ids[static_cast<std::size_t>(item_index)];
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
            state_.navigation.focus_key = home_candidate == 0 ? "home-library" : game_key(home_candidate);
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
                                     after_preferences.reduce_motion != captured_preferences_.reduce_motion;

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
    if (const auto* values = root->get("profiles"); values && values->array()) {
        for (const auto& item : *values->array()) {
            Profile profile;
            profile.id = string_value(item.get("id"));
            profile.name = string_value(item.get("name"));
            profile.summary = string_value(item.get("summary"));
            profile.is_default = profile.id == "default";
            if (const auto* settings = item.get("values")) {
                profile.resolution = string_value(settings->get("resolution"), profile.resolution);
                profile.frame_limit = string_value(settings->get("frame_limit"), profile.frame_limit);
                profile.vsync = bool_value(settings->get("vsync"), profile.vsync);
                profile.launch_arguments = string_value(settings->get("launch_arguments"));
            }
            state_.profiles.push_back(std::move(profile));
        }
        if (state_.profiles.size() > 1 && state_.profiles[1].id == "custom" && state_.profiles.front().id == "default") {
            state_.profiles[1].resolution = state_.profiles.front().resolution;
            state_.profiles[1].frame_limit = state_.profiles.front().frame_limit;
            state_.profiles[1].vsync = state_.profiles.front().vsync;
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
        state_.selected_profiles.clear();
        state_.custom_profiles.clear();
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
    if (const auto* profiles = root->get("profiles"); profiles && profiles->array()) {
        for (const auto& item : *profiles->array()) {
            const auto id = string_value(item.get("id"));
            auto found = std::find_if(state_.profiles.begin(), state_.profiles.end(), [&id](const Profile& value) { return value.id == id; });
            if (found == state_.profiles.end()) continue;
            found->resolution = string_value(item.get("resolution"), found->resolution);
            found->frame_limit = string_value(item.get("frame_limit"), found->frame_limit);
            found->vsync = bool_value(item.get("vsync"), found->vsync);
            found->launch_arguments = string_value(item.get("arguments"));
        }
    }
    if (const auto* profiles = root->get("selected_profiles"); profiles && profiles->array()) {
        state_.selected_profiles.clear();
        for (const auto& item : *profiles->array()) state_.selected_profiles[static_cast<GameId>(uint_value(item.get("id")))] = string_value(item.get("profile"));
    }
    if (const auto* profiles = root->get("custom_profiles"); profiles && profiles->array()) {
        state_.custom_profiles.clear();
        for (const auto& item : *profiles->array()) {
            const auto id = static_cast<GameId>(uint_value(item.get("game_id")));
            auto profile = state_.profiles.empty() ? Profile{} : state_.profiles.front();
            profile.id = "custom"; profile.name = "Custom"; profile.summary = "Default values plus your overrides"; profile.is_default = false;
            profile.resolution = string_value(item.get("resolution"), profile.resolution);
            profile.frame_limit = string_value(item.get("frame_limit"), profile.frame_limit);
            profile.vsync = bool_value(item.get("vsync"), profile.vsync);
            profile.launch_arguments = string_value(item.get("arguments"));
            state_.custom_profiles[id] = std::move(profile);
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
        state_.selected_profiles.clear();
        state_.custom_profiles.clear();
    }
    reconcile_storage(state_);
}

bool Simulation::save_profile() {
    if (live()) return record_live_persistence();
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
    output += "],\"profiles\":[";
    first = true;
    for (const auto& profile : state_.profiles) {
        if (!first) output += ','; first = false;
        output += "{\"id\":\"" + json::escape(profile.id) + "\",\"resolution\":\"" + json::escape(profile.resolution)
            + "\",\"frame_limit\":\"" + json::escape(profile.frame_limit) + "\",\"vsync\":" + (profile.vsync ? "true" : "false")
            + ",\"arguments\":\"" + json::escape(profile.launch_arguments) + "\"}";
    }
    output += "],\"selected_profiles\":[";
    first = true;
    for (const auto& [id, profile] : state_.selected_profiles) { if (!first) output += ','; first = false; output += "{\"id\":" + std::to_string(id) + ",\"profile\":\"" + json::escape(profile) + "\"}"; }
    output += "],\"custom_profiles\":[";
    first = true;
    for (const auto& [id, profile] : state_.custom_profiles) {
        if (!first) output += ','; first = false;
        output += "{\"game_id\":" + std::to_string(id) + ",\"resolution\":\"" + json::escape(profile.resolution)
            + "\",\"frame_limit\":\"" + json::escape(profile.frame_limit) + "\",\"vsync\":" + (profile.vsync ? "true" : "false")
            + ",\"arguments\":\"" + json::escape(profile.launch_arguments) + "\"}";
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
                auto& status = row("logs-error", RowKind::Status, "Logs unavailable", ""); status.subtitle = "Simulated log service failure"; status.enabled = false;
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
        const Profile* profile_values = effective_profile(state_, state_.navigation.selected_game_id, selected_profile_id_);
        for (const auto& action_key : modal.action_keys) {
            auto& item = row(action_key, RowKind::Action, action_label(action_key), action_key);
            item.dangerous = modal.destructive && action_key != "cancel";
            if (action_key.rfind("volume-", 0) == 0) {
                item.kind = RowKind::Volume;
                if (const auto* volume = find_volume(state_, action_key.substr(7))) {
                    item.title = volume->label;
                    const auto occupied = std::min(volume->capacity_bytes, volume->used_bytes + volume->reserved_bytes);
                    item.value = std::to_string((volume->capacity_bytes - occupied) / (1024U * 1024U * 1024U)) + " GiB free";
                    item.subtitle = !volume->available ? "Disconnected" : !volume->writable ? "Read-only" : "Available";
                    item.enabled = volume->available && volume->writable;
                    item.selected = volume->id == selected_volume_id_;
                }
            } else if (action_key == "profile-picker") {
                item.kind = RowKind::Profile;
                if (profile_values) { item.value = profile_values->name; item.subtitle = profile_values->summary; }
            } else if (action_key.rfind("profile-", 0) == 0) {
                item.kind = RowKind::Profile;
                const auto id = action_key.substr(8);
                const auto found = std::find_if(state_.profiles.begin(), state_.profiles.end(), [&id](const Profile& profile) { return profile.id == id; });
                if (found != state_.profiles.end()) { item.title = found->name; item.subtitle = found->summary; item.selected = id == selected_profile_id_; }
            } else if (action_key == "toggle-vsync") {
                item.kind = RowKind::Toggle; item.selected = profile_values && profile_values->vsync;
                item.value = item.selected ? "On" : "Off";
            } else if (action_key == "resolution-picker" && profile_values) item.value = profile_values->resolution;
            else if (action_key == "frame-picker" && profile_values) item.value = profile_values->frame_limit;
            else if (action_key == "edit-arguments" && profile_values) item.value = profile_values->launch_arguments.empty() ? "None" : profile_values->launch_arguments;
            else if (action_key.rfind("resolution-", 0) == 0 && state_.profiles.size() > 1) {
                const auto value = action_key.substr(11);
                const auto display = value == "1280x720" ? "1280×720" : value == "3840x2160" ? "3840×2160" : "1920×1080";
                item.title = display; item.selected = profile_values && display == profile_values->resolution;
            }
            else if (action_key.rfind("frame-", 0) == 0 && profile_values) item.selected = action_key.substr(6) == profile_values->frame_limit;
            else if (action_key == "verify" || action_key == "uninstall") {
                if (!is_installed(state_, state_.navigation.selected_game_id)) { item.enabled = false; item.subtitle = "Game is not installed"; }
            } else if (action_key == "hide") item.title = state_.library.hidden.contains(state_.navigation.selected_game_id) ? "Unhide" : "Hide";
            else if (action_key == "graphics-backend") { item.title = "Graphics backend"; item.value = "Unavailable"; item.subtitle = "Simulated settings; PS5 runtime support is unavailable."; item.enabled = false; }
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
        auto add_shelf = [this](std::string key, std::string title, const std::vector<GameId>& ids, int shelf_index) {
            ViewData::Shelf shelf;
            shelf.key = std::move(key); shelf.title = std::move(title);
            for (const auto id : ids) if (const auto* game = find_game(state_, id)) {
                ViewRow item;
                item.key = "shelf-" + shelf.key + "-game-" + std::to_string(id); item.kind = RowKind::Game; item.title = game->title; item.action = "open-game"; item.game_id = id;
                shelf.items.push_back(std::move(item));
            }
            if (!shelf.items.empty()) {
                const auto saved = state_.navigation.home_shelf_focus.find(shelf.key);
                if (shelf_index == state_.navigation.home_shelf)
                    shelf.focused_item = saved == state_.navigation.home_shelf_focus.end() ? 0 : std::clamp(saved->second, 0, static_cast<int>(shelf.items.size()) - 1);
                view_.shelves.push_back(std::move(shelf));
            }
        };
        int shelf_index = 0;
        for (const auto& shelf : playden::homebrew::shelves(state_)) add_shelf(shelf.key, shelf.title, shelf.game_ids, shelf_index++);
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
        const bool installed = installation && installation->installed_bytes > 0;
        if (!live()) {
            auto& primary = row(installed ? "details-play" : "details-install", RowKind::Action,
                                installed ? (installation->runtime_ready ? "Play" : "Runtime unavailable") : "Install",
                                installed ? (installation->runtime_ready ? "launch" : "runtime-status") : "install");
            primary.enabled = true;
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
        row("settings-account", RowKind::Action, "Account", "account").value = state_.account.display_name;
        auto& storage = row("settings-storage", live() ? RowKind::Status : RowKind::Action, "Library and storage", live() ? "" : "storage-manager");
        if (live()) { storage.subtitle = "Available after library integration"; storage.enabled = false; }
        row("settings-motion", RowKind::Toggle, "Reduced motion", "toggle-motion").selected = state_.reduced_motion;
        row("settings-controller", RowKind::Action, "Controller", "controller-test").value = state_.controller_connected ? "Connected" : "Disconnected";
        auto& runtime = row("settings-runtime", RowKind::Action, "Runtime status", "settings-runtime");
        runtime.value = state_.runtime == RuntimeState::Ready ? (live() ? "Ready" : "Ready — simulated") : state_.runtime == RuntimeState::Checking ? (live() ? "Checking" : "Checking — simulated") : state_.runtime == RuntimeState::Failed ? (live() ? "Failed" : "Failed — simulated") : (live() ? "Unavailable" : "Unavailable — simulated");
        if (!live()) {
            row("settings-simulation", RowKind::Action, "Simulation controls", "simulation-controls");
            row("settings-scenarios", RowKind::Action, "Fixture scenarios", "scenario-picker");
        }
        row("settings-about", RowKind::Action, "About", "about");
        auto& logs = row("settings-logs", live() ? RowKind::Status : RowKind::Action, "Logs", live() ? "" : "logs");
        if (live()) { logs.subtitle = "No operation logs available"; logs.enabled = false; runtime.kind = RowKind::Status; runtime.action.clear(); runtime.enabled = false; }
        if (!state_.notices.empty()) row("settings-notices", RowKind::Action, "Warnings", "notices").value = std::to_string(state_.notices.size());
        row("settings-reset", RowKind::Action, "Reset UI data", "reset-confirm").dangerous = true;
        if (state_.account.authentication != AuthState::SignedOut)
            row("settings-signout", RowKind::Action, "Sign out", "signout-confirm").dangerous = true;
        row("settings-quit", RowKind::Action, "Quit", "quit-confirm").dangerous = true;
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
    auto focused = std::find_if(view_.rows.begin(), view_.rows.end(), [this](const ViewRow& item) { return item.key == state_.navigation.focus_key; });
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
    else if (state_.editor.purpose == "Launch arguments" && state_.profiles.size() > 1) {
        auto& profile = custom_profile(state_, state_.navigation.selected_game_id);
        profile.launch_arguments = state_.editor.value;
        state_.profiles[1] = profile;
        selected_profile_id_ = "custom";
        state_.selected_profiles[state_.navigation.selected_game_id] = "custom";
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
    if (state_.editor.active) { state_.editor.active = false; state_.navigation.focus_key = state_.editor.opener_focus_key; return; }
    if (!state_.navigation.modals.empty()) {
        const bool crash_logs = state_.navigation.modals.back().id == "logs" && state_.session == SessionState::Crashed;
        close_modal();
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

void Simulation::change_route(int delta) {
    if (state_.editor.active || !state_.navigation.modals.empty()) return;
    constexpr Route routes[] = {Route::Home, Route::Library, Route::Downloads, Route::Settings};
    auto found = std::find(std::begin(routes), std::end(routes), state_.navigation.route);
    int index = found == std::end(routes) ? 0 : static_cast<int>(std::distance(std::begin(routes), found));
    index = (index + delta + 4) % 4;
    state_.navigation.route = routes[index];
    state_.navigation.selected_index = 0;
    state_.navigation.focus_key.clear();
}

void Simulation::activate() {
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
    if (state_.navigation.route == Route::Home && state_.navigation.modals.empty() && state_.navigation.selected_game_id != 0) {
        if (live()) force_organization_selection_ = true;
        state_.navigation.detail_origin = Route::Home;
        state_.navigation.detail_id = state_.navigation.selected_game_id;
        state_.navigation.route = Route::Details;
        state_.navigation.focus_key = live() ? "details-favorite" : (is_installed(state_, state_.navigation.selected_game_id) ? "details-play" : "details-install");
        return;
    }
    if (view_.rows.empty()) return;
    const auto index = std::clamp(view_.focused_row, 0, static_cast<int>(view_.rows.size()) - 1);
    const ViewRow selected = view_.rows[static_cast<std::size_t>(index)];
    if (!selected.enabled) return;
    const auto& action_name = selected.action;
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
        state_.navigation.focus_key = live() ? "details-favorite" : (is_installed(state_, selected.game_id) ? "details-play" : "details-install");
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
        selected_volume_id_ = action_name.substr(7);
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
            }
        }
        ModalEntry confirmation{"confirmation", std::move(title), state_.navigation.focus_key, {"cancel", "confirm-destructive"}, 0, true};
        confirmation.body = std::move(body);
        open_modal(std::move(confirmation));
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
        const auto selected = state_.selected_profiles.find(state_.navigation.selected_game_id);
        selected_profile_id_ = selected == state_.selected_profiles.end() ? "default" : selected->second;
        open_modal({"game-settings", "Game settings", "details-settings", {"profile-picker", "resolution-picker", "reset-resolution", "frame-picker", "reset-frame", "toggle-vsync", "reset-vsync", "edit-arguments", "reset-arguments", "graphics-backend", "reset-settings", "cancel"}, 0, false});
    }
    else if (action_name == "profile-picker") {
        std::vector<std::string> actions;
        for (const auto& profile : state_.profiles) actions.push_back("profile-" + profile.id);
        actions.push_back("cancel"); open_modal({"profile-picker", "Choose profile", "profile-picker", std::move(actions), 0, false});
    } else if (action_name.rfind("profile-", 0) == 0) {
        selected_profile_id_ = action_name.substr(8);
        state_.selected_profiles[state_.navigation.selected_game_id] = selected_profile_id_;
        if (selected_profile_id_ == "custom") state_.profiles[1] = custom_profile(state_, state_.navigation.selected_game_id);
        close_modal(); save_profile();
    }
    else if (action_name == "resolution-picker") open_modal({"choice-picker", "Resolution", "resolution-picker", {"resolution-1280x720", "resolution-1920x1080", "resolution-3840x2160", "cancel"}, 1, false});
    else if (action_name == "frame-picker") open_modal({"choice-picker", "Frame limit", "frame-picker", {"frame-30", "frame-60", "frame-Unlimited", "cancel"}, 1, false});
    else if (action_name.rfind("resolution-", 0) == 0) {
        if (state_.profiles.size() > 1) {
            const auto value = action_name.substr(11);
            auto& profile = custom_profile(state_, state_.navigation.selected_game_id);
            profile.resolution = value == "1280x720" ? "1280×720" : value == "3840x2160" ? "3840×2160" : "1920×1080";
            state_.profiles[1] = profile;
            selected_profile_id_ = "custom"; state_.selected_profiles[state_.navigation.selected_game_id] = "custom";
        }
        close_modal(); save_profile();
    }
    else if (action_name.rfind("frame-", 0) == 0) { if (state_.profiles.size() > 1) { auto& profile = custom_profile(state_, state_.navigation.selected_game_id); profile.frame_limit = action_name.substr(6); state_.profiles[1] = profile; selected_profile_id_ = "custom"; state_.selected_profiles[state_.navigation.selected_game_id] = "custom"; } close_modal(); save_profile(); }
    else if (action_name == "toggle-vsync") { if (state_.profiles.size() > 1) { auto& profile = custom_profile(state_, state_.navigation.selected_game_id); profile.vsync = !profile.vsync; state_.profiles[1] = profile; selected_profile_id_ = "custom"; state_.selected_profiles[state_.navigation.selected_game_id] = "custom"; } save_profile(); }
    else if (action_name == "edit-arguments") start_editor("Launch arguments", custom_profile(state_, state_.navigation.selected_game_id).launch_arguments);
    else if (action_name == "reset-settings") {
        if (state_.profiles.size() > 1) {
            Profile reset;
            reset.id = "custom";
            reset.name = "Custom";
            reset.summary = "Default values plus your overrides";
            state_.custom_profiles[state_.navigation.selected_game_id] = reset;
            state_.profiles[1] = std::move(reset);
        }
        save_profile();
    }
    else if (action_name == "reset-resolution") { if (state_.profiles.size() > 1) { auto& profile = custom_profile(state_, state_.navigation.selected_game_id); profile.resolution = state_.profiles.front().resolution; state_.profiles[1] = profile; } save_profile(); }
    else if (action_name == "reset-frame") { if (state_.profiles.size() > 1) { auto& profile = custom_profile(state_, state_.navigation.selected_game_id); profile.frame_limit = state_.profiles.front().frame_limit; state_.profiles[1] = profile; } save_profile(); }
    else if (action_name == "reset-vsync") { if (state_.profiles.size() > 1) { auto& profile = custom_profile(state_, state_.navigation.selected_game_id); profile.vsync = state_.profiles.front().vsync; state_.profiles[1] = profile; } save_profile(); }
    else if (action_name == "reset-arguments") { if (state_.profiles.size() > 1) { auto& profile = custom_profile(state_, state_.navigation.selected_game_id); profile.launch_arguments.clear(); state_.profiles[1] = profile; } save_profile(); }
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
    else if (action_name == "launch") { state_.session_game_id = state_.navigation.selected_game_id; state_.session = SessionState::Preparing; state_.navigation.route = Route::Session; state_.navigation.focus_key = "session-return"; }
    else if (action_name == "runtime-status") open_modal({"runtime", "Runtime unavailable", "details-play", {"runtime-retry", "cancel"}, 0, false});
    else if (action_name == "return-game") state_.session = SessionState::Running;
    else if (action_name == "force-stop") { pending_destructive_action_ = "force-stop"; open_modal({"confirmation", "Force stop simulated session?", state_.navigation.focus_key, {"cancel", "confirm-destructive"}, 0, true}); }
    else if (action_name == "details-back") { state_.navigation.route = Route::Details; state_.navigation.detail_id = state_.session_game_id; }
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
            state_.navigation.focus_key = live() ? "details-favorite" : (is_installed(state_, id) ? "details-play" : "details-install");
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
        if (details) { state_.navigation.detail_origin = state_.navigation.route; state_.navigation.route = Route::Details; state_.navigation.detail_id = id; state_.navigation.focus_key = live() ? "details-favorite" : (is_installed(state_, id) ? "details-play" : "details-install"); }
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
        const int last_page = game ? std::max(0, static_cast<int>((game->description.size() + 419) / 420) - 1) : 0;
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
    output << "],\"custom_profiles\":[";
    first = true;
    for (const auto& [id, profile] : state_.custom_profiles) {
        if (!first) output << ','; first = false;
        output << "{\"game_id\":" << id << ",\"resolution\":\"" << json::escape(profile.resolution)
               << "\",\"frame_limit\":\"" << json::escape(profile.frame_limit) << "\",\"vsync\":"
               << (profile.vsync ? "true" : "false") << ",\"arguments\":\"" << json::escape(profile.launch_arguments) << "\"}";
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

} // namespace playden::homebrew
