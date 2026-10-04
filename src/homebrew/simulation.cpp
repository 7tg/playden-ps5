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
#include <utility>
#include <type_traits>

namespace playden::homebrew {
namespace {

constexpr std::uint64_t kTransferPerTick = 67'108'864;
constexpr std::int64_t kFixtureClock = 1'760'000'000;

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
    case AuthState::Waiting: return "waiting";
    case AuthState::Expired: return "expired";
    case AuthState::Failed: return "failed";
    case AuthState::Cancelled: return "cancelled";
    case AuthState::Syncing: return "syncing";
    case AuthState::SignedIn: return "signed-in";
    case AuthState::Offline: return "offline";
    }
    return "signed-out";
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

void clear_profile_customizations(State& state) {
    state.selected_profiles.clear();
    state.custom_profiles.clear();
    if (state.profiles.size() > 1) {
        auto custom = state.profiles.front();
        custom.id = "custom"; custom.name = "Custom"; custom.summary = "Default values plus your overrides"; custom.is_default = false;
        state.profiles[1] = std::move(custom);
    }
}

std::string game_key(GameId id) { return "game-" + std::to_string(id); }

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
        {"inject-qr-denied", "Deny current challenge"}};
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

} // namespace

Simulation::Simulation(std::filesystem::path fixture_directory, std::filesystem::path profile_path)
    : fixture_directory_(std::move(fixture_directory)), profile_path_(std::move(profile_path)) {
    reset_default_state();
    load_fixtures();
    scenario("default");
    load_profile();
    rebuild_view();
}

void Simulation::reset_default_state() {
    state_ = {};
    state_.clock_ticks = kFixtureClock;
    state_.navigation.scroll_offsets["library-grid"] = 0;
    state_.navigation.scroll_offsets["home"] = 0;
    state_.navigation.scroll_offsets["details"] = 0;
    selected_volume_id_ = "external";
    selected_profile_id_ = "default";
    last_error_.clear();
}

void Simulation::load_fixtures() {
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
}

void Simulation::load_profile() {
    const auto source = read_file(profile_path_);
    if (source.empty()) return;
    const auto root = json::parse(source);
    if (!root || !root->object()) {
        state_.persistence_available = false;
        state_.notices.push_back({"persistence", "Saved UI state is corrupt", "retry-save", true, 0});
        return;
    }
    const auto saved_account = string_value(root->get("account_id"));
    if (!saved_account.empty() && saved_account != state_.account.id) return;
    state_.reduced_motion = bool_value(root->get("reduced_motion"));
    const auto saved_auth = string_value(root->get("authentication"));
    if (saved_auth == "signed-out") { state_.account.authentication = AuthState::SignedOut; state_.account.cache_available = false; state_.navigation.route = Route::Startup; }
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
            if (job.state == JobState::Downloading || job.state == JobState::Verifying || job.state == JobState::Pausing) job.state = JobState::Interrupted;
            state_.jobs.push_back(std::move(job));
        }
    }
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
    reconcile_storage(state_);
}

bool Simulation::save_profile() {
    if (!state_.persistence_available) {
        state_.notices.push_back({"save-failed", "Changes cannot be saved", "retry-save", true, 0});
        return false;
    }
    std::error_code error;
    if (!profile_path_.parent_path().empty()) std::filesystem::create_directories(profile_path_.parent_path(), error);
    std::string output = "{\"account_id\":\"" + json::escape(state_.account.id) + "\",\"authentication\":\"" + auth_name(state_.account.authentication) + "\",\"reduced_motion\":"
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
            + ",\"reservation_held\":" + (job.reservation_held ? "true" : "false") + '}';
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
        state_.account.cache_available = false;
        state_.navigation.route = Route::Startup;
        state_.navigation.focus_key = "startup-connect";
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
    return filtered_games(state_);
}

void Simulation::rebuild_view() {
    const bool has_overlay = state_.editor.active || !state_.navigation.modals.empty();
    if (has_overlay) {
        const auto editor = state_.editor;
        const auto modals = state_.navigation.modals;
        const auto focus = state_.navigation.focus_key;
        const int selected_index = state_.navigation.selected_index;
        state_.editor.active = false;
        state_.navigation.modals.clear();
        rebuild_view();
        route_view_ = view_;
        state_.editor = editor;
        state_.navigation.modals = modals;
        state_.navigation.focus_key = focus;
        state_.navigation.selected_index = selected_index;
    }
    view_ = {};
    view_.surface = route_name(state_.navigation.route);
    view_.show_simulation_label = true;
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
        row("keyboard-done", RowKind::Action, "Done", "editor-done");
        row("keyboard-cancel", RowKind::Action, "Cancel", "editor-cancel");
        view_.legends = {"Cross Select", "Square Backspace", "L1/R1 Move caret", "Triangle Clear", "Circle Cancel"};
    } else if (!state_.navigation.modals.empty()) {
        const auto& modal = state_.navigation.modals.back();
        view_.title = modal.title;
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
                if (!find_installation(state_, state_.navigation.selected_game_id)) { item.enabled = false; item.subtitle = "Game is not installed"; }
            } else if (action_key == "hide") item.title = state_.library.hidden.contains(state_.navigation.selected_game_id) ? "Unhide" : "Hide";
            else if (action_key == "graphics-backend") { item.title = "Graphics backend"; item.value = "Unavailable"; item.subtitle = "Simulated settings; PS5 runtime support is unavailable."; item.enabled = false; }
            else if (action_key == "runtime-retry") item.subtitle = state_.runtime == RuntimeState::Ready ? "Ready — simulated" : state_.runtime == RuntimeState::Checking ? "Checking — simulated" : state_.runtime == RuntimeState::Failed ? "Failed — retry available" : "Unavailable — retry available";
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
        view_.subtitle = "Browse the local UI fixture or connect a simulated Steam account.";
        if (!state_.persistence_available) {
            row("startup-retry-save", RowKind::Action, "Retry saved state", "retry-save");
            row("startup-continue", RowKind::Action, "Continue without saving", "continue-without-saving");
        } else {
            row("startup-connect", RowKind::Action, "Connect Steam", "connect");
            row("startup-skip", RowKind::Action, "Skip for now", "skip");
        }
    } else if (state_.navigation.route == Route::SignIn) {
        view_.title = "Connect Steam";
        view_.subtitle = state_.account.authentication == AuthState::Waiting ? "Simulated QR challenge — not a working Steam code" : state_.account.error;
        if (state_.account.authentication == AuthState::Syncing) row("sync-advance", RowKind::Action, "Advance fixture sync", "advance-clock");
        else if (state_.account.authentication == AuthState::Expired) row("qr-refresh", RowKind::Action, "Refresh code", "qr-refresh");
        else if (state_.account.authentication == AuthState::Failed) row("qr-retry", RowKind::Action, "Retry", "qr-retry");
        else row("qr-cancel", RowKind::Action, "Cancel", "qr-cancel");
        if (state_.account.authentication != AuthState::Syncing) row("qr-fixture-controls", RowKind::Action, "QR fixture controls", "qr-fixture-controls");
    } else if (state_.navigation.route == Route::Home) {
        view_.title = "Home";
        view_.subtitle = state_.account.authentication == AuthState::SignedIn ? "Fixture history — simulated" : "Signed out";
        for (const auto* game : visible_games()) {
            if (view_.rows.size() >= 12) break;
            auto& item = row(game_key(game->id), RowKind::Game, game->title, "open-game");
            item.game_id = game->id;
            item.badge = state_.library.favorites.contains(game->id) ? "Favorite" : "";
        }
        if (view_.rows.empty()) row("home-library", RowKind::Action, "Open Library", "library");
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
    } else if (state_.navigation.route == Route::Library) {
        view_.title = "Library";
        const auto games = visible_games();
        view_.total_count = games.size();
        view_.subtitle = std::to_string(games.size()) + " games";
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
        auto& primary = row(installation ? "details-play" : "details-install", RowKind::Action,
                            installation ? (installation->runtime_ready ? "Play" : "Runtime unavailable") : "Install",
                            installation ? (installation->runtime_ready ? "launch" : "runtime-status") : "install");
        primary.enabled = true;
        row("details-favorite", RowKind::Toggle, "Favorite", "favorite").selected = game && state_.library.favorites.contains(game->id);
        row("details-settings", RowKind::Action, "Game settings", "game-settings");
        row("details-simulate-launch", RowKind::Action, "Simulate launch", "launch");
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
        row("settings-account", RowKind::Action, "Account", "account").value = state_.account.display_name;
        row("settings-storage", RowKind::Action, "Library and storage", "storage-manager");
        row("settings-motion", RowKind::Toggle, "Reduced motion", "toggle-motion").selected = state_.reduced_motion;
        row("settings-controller", RowKind::Action, "Controller", "controller-test").value = state_.controller_connected ? "Connected" : "Disconnected";
        auto& runtime = row("settings-runtime", RowKind::Action, "Runtime status", "settings-runtime");
        runtime.value = state_.runtime == RuntimeState::Ready ? "Ready — simulated" : state_.runtime == RuntimeState::Checking ? "Checking — simulated" : state_.runtime == RuntimeState::Failed ? "Failed — simulated" : "Unavailable — simulated";
        row("settings-simulation", RowKind::Action, "Simulation controls", "simulation-controls");
        row("settings-logs", RowKind::Action, "Logs", "logs");
        if (!state_.notices.empty()) row("settings-notices", RowKind::Action, "Warnings", "notices").value = std::to_string(state_.notices.size());
        row("settings-reset", RowKind::Action, "Reset UI data", "reset-confirm").dangerous = true;
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
    }
    auto focused = std::find_if(view_.rows.begin(), view_.rows.end(), [this](const ViewRow& item) { return item.key == state_.navigation.focus_key; });
    if (focused != view_.rows.end()) view_.focused_row = static_cast<int>(std::distance(view_.rows.begin(), focused));
    else if (!view_.rows.empty() && !(state_.navigation.route == Route::Library && state_.navigation.library_rail_focused)) {
        view_.focused_row = std::clamp(state_.navigation.selected_index, 0, static_cast<int>(view_.rows.size()) - 1);
        if (state_.navigation.route == Route::Library && view_.first_index > 0)
            view_.focused_row = std::clamp(state_.navigation.selected_index - static_cast<int>(view_.first_index), 0, static_cast<int>(view_.rows.size()) - 1);
        state_.navigation.focus_key = view_.rows[static_cast<std::size_t>(view_.focused_row)].key;
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
    if (state_.editor.purpose == "Search") state_.library.query = state_.editor.value;
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
        state_.navigation.detail_origin = Route::Home;
        state_.navigation.detail_id = state_.navigation.selected_game_id;
        state_.navigation.route = Route::Details;
        state_.navigation.focus_key = find_installation(state_, state_.navigation.selected_game_id) ? "details-play" : "details-install";
        return;
    }
    if (view_.rows.empty()) return;
    const auto index = std::clamp(view_.focused_row, 0, static_cast<int>(view_.rows.size()) - 1);
    const ViewRow selected = view_.rows[static_cast<std::size_t>(index)];
    if (!selected.enabled) return;
    const auto& action_name = selected.action;
    if (action_name == "connect" || action_name == "qr-retry" || action_name == "qr-refresh") scenario("qr-waiting");
    else if (action_name == "retry-save") { state_.persistence_available = true; save_profile(); }
    else if (action_name == "continue-without-saving") { state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(), [](const Notice& notice) { return notice.id == "persistence"; }), state_.notices.end()); state_.navigation.route = Route::Home; }
    else if (action_name == "skip" || action_name == "library") { state_.navigation.route = action_name == "skip" ? Route::Home : Route::Library; state_.navigation.focus_key.clear(); }
    else if (action_name == "qr-cancel" || action_name == "editor-cancel" || action_name == "cancel") cancel();
    else if (action_name == "editor-done") commit_editor();
    else if (action_name == "clear-search") { state_.library.query.clear(); state_.navigation.selected_index = 0; }
    else if (action_name == "open-game") {
        state_.navigation.detail_origin = state_.navigation.route;
        state_.navigation.detail_id = selected.game_id;
        state_.navigation.selected_game_id = selected.game_id;
        state_.navigation.route = Route::Details;
        state_.navigation.focus_key = find_installation(state_, selected.game_id) ? "details-play" : "details-install";
    } else if (action_name == "favorite") {
        if (state_.library.favorites.contains(state_.navigation.selected_game_id)) state_.library.favorites.erase(state_.navigation.selected_game_id);
        else state_.library.favorites.insert(state_.navigation.selected_game_id);
        save_profile();
    } else if (action_name == "hide") {
        if (state_.library.hidden.contains(state_.navigation.selected_game_id)) state_.library.hidden.erase(state_.navigation.selected_game_id);
        else state_.library.hidden.insert(state_.navigation.selected_game_id);
        close_modal();
        const auto remaining = visible_games();
        if (remaining.empty()) state_.navigation.focus_key = state_.library.query.empty() ? "library-empty" : "clear-search";
        else {
            state_.navigation.selected_index = std::min(state_.navigation.selected_index, static_cast<int>(remaining.size()) - 1);
            state_.navigation.selected_game_id = remaining[static_cast<std::size_t>(state_.navigation.selected_index)]->id;
            state_.navigation.focus_key = game_key(state_.navigation.selected_game_id);
        }
    } else if (action_name == "context") open_modal({"context", "Game actions", "details-more", {"favorite", "collection", "note", "hide", "verify", "uninstall", "logs", "cancel"}, 0, false});
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
            const auto existing = std::find_if(state_.jobs.begin(), state_.jobs.end(), [this](const Job& job) { return job.game_id == state_.navigation.selected_game_id && job.state != JobState::Cancelled; });
            if (existing == state_.jobs.end()) {
                const auto required = assessment.required_bytes.value_or(0);
                state_.jobs.push_back({"job-" + std::to_string(state_.jobs.size() + 1), state_.navigation.selected_game_id, "install", static_cast<int>(state_.jobs.size()), JobStage::Download, JobState::Queued, 0, *assessment.download_bytes, std::nullopt, std::nullopt, {}, JobStage::Download, selected_volume_id_, 0, required, true});
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
            if (job->kind != "uninstall")
                for (auto& other : state_.jobs) if (&other != job && other.kind != "uninstall" && (other.state == JobState::Downloading || other.state == JobState::Verifying)) other.state = JobState::Queued;
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
        state_.jobs.push_back({"verify-" + std::to_string(state_.navigation.selected_game_id), state_.navigation.selected_game_id, "verify", 0, JobStage::Verify, JobState::Verifying, 0, 0, std::nullopt, std::nullopt, {}, JobStage::Verify, {}, 0, 0, false});
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
        open_modal({"confirmation", std::move(title), state_.navigation.focus_key, {"cancel", "confirm-destructive"}, 0, true});
    } else if (action_name == "confirm-destructive") {
        if (pending_destructive_action_.rfind("job-cancel-", 0) == 0) {
            if (auto* job = find_job(state_, pending_destructive_action_.substr(11))) { job->state = JobState::Cancelled; job->reservation_held = false; }
            reconcile_storage(state_);
        }
        else if (pending_destructive_action_ == "uninstall") {
            if (const auto* installation = find_installation(state_, state_.navigation.selected_game_id)) {
                state_.jobs.push_back({"uninstall-" + std::to_string(state_.navigation.selected_game_id), state_.navigation.selected_game_id, "uninstall", static_cast<int>(state_.jobs.size()), JobStage::Uninstall, JobState::Verifying, 0, installation->installed_bytes, std::nullopt, std::nullopt, {}, JobStage::Uninstall, installation->volume_id, 0, 0, false});
                state_.navigation.route = Route::Downloads;
                state_.navigation.focus_key = "job-uninstall-" + std::to_string(state_.navigation.selected_game_id);
            }
        }
        else if (pending_destructive_action_ == "signout-confirm") { state_.account.authentication = AuthState::SignedOut; state_.account.cache_available = false; state_.library = {}; clear_profile_customizations(state_); state_.navigation.route = Route::Startup; }
        else if (pending_destructive_action_ == "reset-confirm") { const auto installs = state_.installations; state_.library = {}; state_.installations = installs; clear_profile_customizations(state_); state_.reduced_motion = false; }
        else if (pending_destructive_action_ == "quit-game") { state_.session = SessionState::Stopping; state_.navigation.route = Route::Session; }
        else if (pending_destructive_action_ == "force-stop") { state_.session = SessionState::Idle; state_.navigation.route = Route::Details; state_.navigation.detail_id = state_.session_game_id; }
        else if (pending_destructive_action_ == "quit-confirm") state_.quit_requested = true;
        else if (pending_destructive_action_.rfind("collection-delete-", 0) == 0) {
            const auto id = pending_destructive_action_.substr(18);
            state_.library.collections.erase(std::remove_if(state_.library.collections.begin(), state_.library.collections.end(), [&id](const Collection& value) { return value.id == id; }), state_.library.collections.end());
        }
        state_.navigation.modals.clear();
        pending_destructive_action_.clear();
    } else if (action_name.rfind("sort-", 0) == 0) {
        state_.library.sort = action_name.substr(5); close_modal(); save_profile();
    } else if (action_name.rfind("filter-", 0) == 0) {
        const auto filter = action_name.substr(7);
        if (state_.library.filters.contains(filter)) state_.library.filters.erase(filter); else state_.library.filters.insert(filter);
        close_modal(); save_profile();
    } else if (action_name == "filters-reset") {
        state_.library.filters.clear(); state_.library.sort = "name"; close_modal(); save_profile();
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
    else if (action_name == "logs") open_modal({"logs", "Operation logs", state_.navigation.focus_key, {"logs-back"}, 0, false});
    else if (action_name == "logs-back") close_modal();
    else if (action_name == "controller-test") open_modal({"controller", "Controller diagnostics", state_.navigation.focus_key, {"cancel"}, 0, false});
    else if (action_name == "settings-runtime") open_modal({"runtime", "Runtime status — simulated", state_.navigation.focus_key, {"runtime-retry", "cancel"}, 0, false});
    else if (action_name == "runtime-retry") { state_.runtime = RuntimeState::Checking; state_.session_error.clear(); }
    else if (action_name == "qr-fixture-controls") open_modal({"qr-controls", "QR fixture controls", state_.navigation.focus_key,
        {"inject-qr-approved", "inject-qr-expired", "inject-qr-failure", "inject-qr-denied", "advance-clock", "cancel"}, 0, false});
    else if (action_name == "simulation-controls") open_modal({"simulation-controls", "Simulation controls", state_.navigation.focus_key,
        {"advance-clock", "inject-download-failed", "inject-verification-failed", "inject-uninstall-failed", "inject-storage-disconnect", "inject-storage-reconnect", "inject-controller-disconnect", "inject-controller-reconnect", "inject-launch-failure", "inject-crash", "cancel"}, 0, false});
    else if (action_name == "advance-clock") tick(1);
    else if (action_name.rfind("inject-", 0) == 0) {
        const auto injected = action_name.substr(7);
        if (state_.navigation.modals.back().id == "qr-controls") close_modal();
        action("inject:" + injected);
    }
    else if (action_name == "notices") {
        std::vector<std::string> actions;
        for (const auto& notice : state_.notices) if (!notice.action.empty()) actions.push_back(notice.action);
        actions.push_back("cancel"); open_modal({"notices", "Warnings", state_.navigation.focus_key, std::move(actions), 0, false});
    }
    else if (action_name == "storage-retry") action("inject:storage-reconnect");
    else if (action_name == "retry-save") { state_.persistence_available = true; save_profile(); }
    else if (action_name == "account") open_modal({"account", "Account", state_.navigation.focus_key, {"signout-confirm", "cancel"}, 0, false});
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
        if (remove) state_.library.collections.erase(found); else found->pinned = !found->pinned;
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
        const auto existing = std::find_if(state_.jobs.begin(), state_.jobs.end(), [id](const Job& value) { return value.game_id == id && value.state != JobState::Cancelled; });
        if (existing == state_.jobs.end()) {
            state_.jobs.push_back({"job-" + std::to_string(state_.jobs.size() + 1), id, "install", static_cast<int>(state_.jobs.size()), JobStage::Download, JobState::Queued, 0, *assessment.download_bytes, std::nullopt, std::nullopt, {}, JobStage::Download, volume_id, 0, assessment.required_bytes.value_or(0), true});
            reconcile_storage(state_);
        }
        if (state_.persistence_available) save_profile();
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
        if (job->kind != "uninstall")
            for (auto& other : state_.jobs) if (&other != job && other.kind != "uninstall" && (other.state == JobState::Downloading || other.state == JobState::Verifying)) other.state = JobState::Queued;
        job->stage = job->retry_stage; job->state = job->stage == JobStage::Download ? JobState::Downloading : JobState::Verifying; job->failure.clear(); save_profile(); rebuild_view(); return true;
    }
    if (logical_action == "reset") {
        const auto installs = state_.installations; state_.library = {}; state_.installations = installs; clear_profile_customizations(state_); state_.reduced_motion = false; save_profile(); rebuild_view(); return true;
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
        if (details) { state_.navigation.detail_origin = state_.navigation.route; state_.navigation.route = Route::Details; state_.navigation.detail_id = id; state_.navigation.focus_key = find_installation(state_, id) ? "details-play" : "details-install"; }
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
        rebuild_view(); return true;
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
        if (!state_.editor.active && state_.navigation.route == Route::Library && state_.navigation.modals.empty())
            open_modal({"filters", "Sort and filters", state_.navigation.focus_key,
                        {"sort-name", "sort-recent-played", "sort-recent-added", "sort-playtime", "sort-install-size",
                         "filter-installed", "filter-genre-adventure", "filter-controller-full", "filter-compat-playable",
                         "filters-reset", "cancel"}, 0, false});
    }
    else if (logical_action == "options") {
        if (!state_.editor.active && state_.navigation.route == Route::Details && state_.navigation.modals.empty()) open_modal({"context", "Game actions", "details-more", {"favorite", "collection", "note", "hide", "verify", "uninstall", "logs", "cancel"}, 0, false});
        else if (!state_.editor.active && state_.navigation.route == Route::Library && !state_.navigation.modals.empty()) {}
        else if (!state_.editor.active && state_.navigation.route == Route::Library && state_.navigation.selected_game_id != 0 && state_.navigation.modals.empty()
                 && state_.navigation.focus_key.rfind("game-", 0) == 0) open_modal({"context", "Game actions", state_.navigation.focus_key, {"favorite", "collection", "note", "hide", "cancel"}, 0, false});
        else if (!state_.editor.active && state_.navigation.route == Route::Downloads && state_.navigation.modals.empty()) activate();
    } else if (logical_action == "square") {
        if (state_.editor.active) {
            if (state_.editor.caret > 0) { const auto prior = previous_caret(state_.editor.value, state_.editor.caret); state_.editor.value.erase(prior, state_.editor.caret - prior); state_.editor.caret = prior; }
        } else if (state_.navigation.modals.empty() && (state_.navigation.route == Route::Library || state_.navigation.route == Route::Details) && state_.navigation.selected_game_id != 0) {
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
            for (auto& job : state_.jobs) if (job.volume_id == "external" && (job.state == JobState::Downloading || job.state == JobState::Verifying)) { job.state = JobState::Interrupted; job.failure = "Storage disconnected"; }
            state_.notices.push_back({"storage", "USB fixture library disconnected", "storage-retry", true, 0});
        } else if (injected == "storage-reconnect") {
            if (auto* volume = find_volume(state_, "external")) volume->available = true;
            state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(), [](const Notice& notice) { return notice.id == "storage"; }), state_.notices.end());
        } else if (injected == "controller-disconnected") {
            state_.controller_connected = false;
            state_.notices.push_back({"controller", "Controller disconnected", "controller-test", true, 0});
        } else if (injected == "controller-reconnected") {
            state_.controller_connected = true;
            state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(), [](const Notice& notice) { return notice.id == "controller"; }), state_.notices.end());
        } else if (injected == "download-failed" || injected == "verification-failed") {
            if (auto* job = find_job(state_, "job-1")) { job->state = JobState::Failed; job->retry_stage = injected == "verification-failed" ? JobStage::Verify : JobStage::Download; job->stage = job->retry_stage; job->failure = injected == "verification-failed" ? "Verification failed" : "Download failed"; }
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
        else if (injected == "late-approval") {
            if (state_.account.authentication != AuthState::Cancelled && challenge_generation_ != cancelled_challenge_generation_) state_.account.authentication = AuthState::Syncing;
        } else return scenario(injected);
        if (state_.persistence_available) save_profile();
        rebuild_view(); return true;
    }
    else { last_error_ = "Unknown action: " + logical_action; return false; }
    rebuild_view();
    if (state_.persistence_available) save_profile();
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
    for (auto& job : state_.jobs) {
        if (job.kind != "uninstall" || job.state != JobState::Verifying) continue;
        ++job.stage_ticks;
        if (job.stage_ticks >= 2) {
            job.state = JobState::Downloaded;
            job.completed_bytes = job.total_bytes;
            reconcile_storage(state_);
            state_.notices.push_back({"job-complete-" + job.id, "Uninstall complete; saves preserved", "open-game", false, 5});
        }
    }
    Job* active = nullptr;
    for (auto& job : state_.jobs) if (job.kind != "uninstall" && (job.state == JobState::Downloading || job.state == JobState::Verifying)) { active = &job; break; }
    if (!active) {
        auto queued = std::min_element(state_.jobs.begin(), state_.jobs.end(), [](const Job& left, const Job& right) {
            const int left_order = left.state == JobState::Queued ? left.queue_order : std::numeric_limits<int>::max();
            const int right_order = right.state == JobState::Queued ? right.queue_order : std::numeric_limits<int>::max();
            return left_order < right_order;
        });
        if (queued != state_.jobs.end() && queued->state == JobState::Queued) queued->state = JobState::Downloading;
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
            const auto message = active->kind == "verify" ? "Verification complete" : "Download complete";
            state_.notices.push_back({"job-complete-" + active->id, message, "open-game", false, 5});
        }
    }
}

void Simulation::tick(int ticks) {
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
                state_.library_syncing = false;
                state_.navigation.route = Route::Home;
                state_.navigation.focus_key = game_key(state_.navigation.selected_game_id);
            }
            if (state_.session == SessionState::Preparing) state_.session = SessionState::Launching;
            else if (state_.session == SessionState::Launching) state_.session = SessionState::Running;
            else if (state_.session == SessionState::Stopping) { state_.session = SessionState::Idle; state_.navigation.route = Route::Details; state_.navigation.detail_id = state_.session_game_id; }
            if (state_.runtime == RuntimeState::Checking) state_.runtime = RuntimeState::Ready;
            for (auto& game : state_.games) if (game.art_pending) game.art_pending = false;
            for (auto& notice : state_.notices) if (!notice.persistent && notice.remaining_ticks > 0) --notice.remaining_ticks;
            state_.notices.erase(std::remove_if(state_.notices.begin(), state_.notices.end(), [](const Notice& notice) { return !notice.persistent && notice.remaining_ticks == 0; }), state_.notices.end());
        }
    }
    if (ticks > 0 && state_.persistence_available) save_profile();
    rebuild_view();
}

std::string Simulation::snapshot() const {
    StringBuilder output;
    const auto visible = visible_games();
    output << "{\"simulation\":true,\"clock\":" << state_.clock_ticks << ",\"route\":\""
           << route_name(state_.navigation.route) << "\",\"surface\":\"" << json::escape(view_.surface)
           << "\",\"focus\":\"" << json::escape(state_.navigation.focus_key) << "\",\"selected_game_id\":"
           << state_.navigation.selected_game_id << ",\"account\":\"" << auth_name(state_.account.authentication)
           << "\",\"detail_id\":";
    if (state_.navigation.detail_id) output << *state_.navigation.detail_id; else output << "null";
    output << ",\"query\":\"" << json::escape(state_.library.query) << "\",\"scope\":\""
           << json::escape(state_.library.scope) << "\",\"sort\":\"" << json::escape(state_.library.sort)
           << "\",\"visible_count\":" << visible.size() << ",\"materialized_row_count\":" << view_.rows.size()
           << ",\"modal_depth\":" << state_.navigation.modals.size()
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
