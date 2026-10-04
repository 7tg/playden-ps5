// homebrew_library_model_test.cpp - Real library projection tests.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT

#include "homebrew/simulation.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

using playden::GameSummary;
using playden::homebrew::AuthState;
using playden::homebrew::LibraryUpdate;
using playden::homebrew::ModelMode;
using playden::homebrew::Route;
using playden::homebrew::RowKind;
using playden::homebrew::ServiceAction;
using playden::homebrew::Simulation;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

Simulation make_live() {
    return Simulation("fixtures-not-used", "profile-not-used.json", ModelMode::Live);
}

GameSummary game(playden::GameId id, std::string title, std::string art = {},
                 std::string description = {}) {
    GameSummary result;
    result.id = id;
    result.title = std::move(title);
    result.artwork_source = std::move(art);
    result.description = std::move(description);
    return result;
}

const playden::homebrew::Game* find_game(const Simulation& model, playden::GameId id) {
    const auto found = std::find_if(model.state().games.begin(), model.state().games.end(),
                                    [id](const auto& item) { return item.id == id; });
    return found == model.state().games.end() ? nullptr : &*found;
}

bool has_row(const Simulation& model, const std::string& key) {
    return std::any_of(model.view().rows.begin(), model.view().rows.end(),
                       [&key](const auto& row) { return row.key == key; });
}

void activate(Simulation& model, const std::string& key) {
    check(model.action("focus:" + key), "requested row exists");
    check(model.action("cross"), "requested row activates");
}

void projects_only_owned_facts_and_delivery_status() {
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "account", "Owner", {}, {}, false});

    auto summary = game(10, "Known title", "https://cdn.invalid/10.jpg", "Known description");
    summary.developer = "Not projected by this boundary";
    summary.playtime_minutes = 91;
    summary.last_played_unix_seconds = 4'102'444'800ULL;
    check(model.UpdateLibrary({{summary}, false, {}, false, true, 1234}),
          "valid successful live snapshot is accepted");

    const auto* projected = find_game(model, 10);
    check(projected != nullptr && projected->title == "Known title"
              && projected->description == "Known description"
              && projected->source == "Steam"
              && projected->artwork_source == "https://cdn.invalid/10.jpg",
          "shared identity, text, artwork and known storefront are projected");
    check(projected->playtime_minutes == 91 && projected->last_played == 4'102'444'800LL,
          "owned play history is projected without narrowing valid timestamps");
    check(projected->genres.empty() && projected->controller_support.empty()
              && !projected->download_bytes && !projected->install_bytes
              && !projected->added_at && !projected->art_pending && !projected->art_failed,
          "projection does not invent install, controller, metadata or artwork state");
    check(model.state().account.authentication == AuthState::SignedIn
              && model.state().account.cache_available
              && model.state().account.last_sync == 1234
              && !model.state().library_loading && !model.state().library_syncing
              && !model.state().library_offline && model.state().account.error.empty(),
          "successful delivery updates library status without repurposing authentication");
}

void loading_and_failure_preserve_last_good_snapshot() {
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "account", "Owner", {}, {}, false});
    check(model.UpdateLibrary({{game(1, "One"), game(2, "Two")}, false, {}, false, true, 100}),
          "initial snapshot accepted");

    check(model.UpdateLibrary({{}, true, {}, false, true, std::nullopt}),
          "loading delivery status accepted");
    check(model.state().games.size() == 2 && model.state().library_loading
              && model.state().library_syncing && model.state().account.last_sync == 100,
          "loading preserves games and prior retrieval time");

    check(model.UpdateLibrary({{}, false, "Refresh failed", true, true, std::nullopt}),
          "failure delivery status accepted");
    check(model.state().games.size() == 2 && !model.state().library_loading
              && !model.state().library_syncing && model.state().library_offline
              && model.state().account.error == "Refresh failed",
          "failed refresh keeps the last good catalog and exposes stale error state");

    check(model.UpdateLibrary({{}, false, {}, false, true, 200}),
          "genuine successful empty snapshot accepted");
    check(model.state().games.empty() && model.state().account.last_sync == 200
              && model.state().account.error.empty() && !model.state().library_offline,
          "successful empty ownership replaces stale games and clears failure state");

    check(model.UpdateLibrary({{game(3, "Three")}, false, {}, false, true, 201})
              && model.action("details:3"),
          "details can be open before an empty ownership result");
    check(model.UpdateLibrary({{}, false, {}, false, true, 202})
              && model.state().navigation.route == Route::Home
              && !model.state().navigation.detail_id
              && model.state().navigation.selected_game_id == 0
              && has_row(model, "home-library"),
          "empty ownership leaves a removed details record for its empty origin surface");
}

void rejects_invalid_snapshots_transactionally_and_leaves_simulation_alone() {
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "account", "Owner", {}, {}, false});
    check(model.UpdateLibrary({{game(5, "Five")}, false, {}, false, true, 50}),
          "baseline snapshot accepted");

    const auto assert_unchanged = [&model](const char* message) {
        check(model.state().games.size() == 1 && model.state().games.front().id == 5
                  && model.state().account.last_sync == 50
                  && model.state().account.cache_available
                  && model.state().account.error.empty() && !model.state().library_offline,
              message);
    };
    check(!model.UpdateLibrary({{game(0, "Zero")}, false, "must not leak", true, false, 60}),
          "zero app ID is rejected");
    assert_unchanged("zero-ID rejection is transactional");
    check(!model.UpdateLibrary({{game(6, "")}, false, {}, false, false, 60}),
          "empty title is rejected");
    assert_unchanged("empty-title rejection is transactional");
    check(!model.UpdateLibrary({{game(6, "First"), game(6, "Duplicate")}, false, {}, false, false, 60}),
          "duplicate app ID is rejected");
    assert_unchanged("duplicate rejection is transactional");

    Simulation simulation("fixtures-not-used", "profile-not-used.json", ModelMode::Simulation);
    const auto prior_games = simulation.state().games.size();
    check(!simulation.UpdateLibrary({{game(88, "Live only")}, false, {}, false, true, 90})
              && simulation.state().games.size() == prior_games,
          "production library API cannot mutate deterministic simulation state");
}

void preserves_identity_and_scroll_then_chooses_a_stable_neighbor() {
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "account", "Owner", {}, {}, false});
    std::vector<GameSummary> initial;
    for (playden::GameId id = 1; id <= 30; ++id)
        initial.push_back(game(id, "Game " + (id < 10 ? std::string("0") : std::string{}) + std::to_string(id)));
    check(model.UpdateLibrary({initial, false, {}, false, true, 1}), "large snapshot accepted");
    check(model.action("r1") && model.state().navigation.route == Route::Library,
          "navigation enters library");
    check(model.action("select:13") && model.action("down"), "selection moves into a scrolled row");
    const auto selected_id = model.state().navigation.selected_game_id;
    const auto scroll = model.state().navigation.scroll_offsets.at("library-grid");
    check(selected_id == 19 && scroll == 3, "test starts with stable identity and nonzero scroll");

    auto reordered = initial;
    for (auto& item : reordered)
        if (item.id == selected_id) item.title = "AAA selected moved";
    std::reverse(reordered.begin(), reordered.end());
    check(model.UpdateLibrary({reordered, false, {}, false, true, 2}), "reordered snapshot accepted");
    check(model.state().navigation.selected_game_id == selected_id
              && model.state().navigation.focus_key == "game-19"
              && model.state().navigation.scroll_offsets.at("library-grid") == scroll,
          "refresh preserves selected Steam ID, focus and scroll across reorder");

    const int prior_index = model.state().navigation.selected_index;
    reordered.erase(std::remove_if(reordered.begin(), reordered.end(), [selected_id](const auto& item) {
        return item.id == selected_id;
    }), reordered.end());
    check(model.UpdateLibrary({reordered, false, {}, false, true, 3}), "removal snapshot accepted");
    const auto visible = model.visible_games();
    const int expected_index = std::min(prior_index, static_cast<int>(visible.size()) - 1);
    check(!visible.empty() && model.state().navigation.selected_index == expected_index
              && model.state().navigation.selected_game_id == visible[static_cast<std::size_t>(expected_index)]->id
              && model.state().navigation.focus_key == "game-" + std::to_string(model.state().navigation.selected_game_id)
              && model.state().navigation.scroll_offsets.at("library-grid") == scroll,
          "removed selection chooses the item at the prior visible index and preserves scroll");

    const auto detail_id = model.state().navigation.selected_game_id;
    check(model.action("details:" + std::to_string(detail_id))
              && model.action("focus:details-more"),
          "selected neighbor opens with a stable details focus");
    auto detail_refresh = reordered;
    for (auto& item : detail_refresh) if (item.id == detail_id) item.title = "AAA detail moved";
    check(model.UpdateLibrary({detail_refresh, false, {}, false, true, 4}),
          "details refresh accepted");
    check(model.state().navigation.detail_id == detail_id
              && model.state().navigation.selected_game_id == detail_id
              && model.state().navigation.focus_key == "details-more"
              && model.state().navigation.scroll_offsets.at("library-grid") == scroll,
          "details refresh preserves game identity, action focus and scroll by Steam ID");
}

void live_details_keep_personal_edits_and_block_fixture_operations() {
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "account", "Owner", {}, {}, false});
    check(model.UpdateLibrary({{game(7, "Seven")}, false, {}, false, true, 1}),
          "live game delivered");
    check(model.action("select:7") && model.action("square")
              && model.action("note:7:personal note")
              && model.action("collection:create:Personal")
              && model.action("collection:add:collection-1:7"),
          "favorites, notes and collections remain editable in live mode");
    check(model.UpdateLibrary({{game(8, "Eight"), game(7, "Seven renamed")}, false, {}, false, true, 2}),
          "follow-up live snapshot accepted");
    check(model.state().library.favorites.contains(7)
              && model.state().library.notes.at(7) == "personal note"
              && model.state().library.collections.front().members.contains(7),
          "refresh preserves account-scoped personal annotations");

    check(model.action("details:7"), "live details open");
    check(has_row(model, "details-favorite")
              && !has_row(model, "details-install") && !has_row(model, "details-play")
              && !has_row(model, "details-settings") && !has_row(model, "details-simulate-launch"),
          "live details expose personal actions without fixture install/session actions");
    const auto focused = model.view().focused_row;
    check(focused >= 0 && model.view().rows[static_cast<std::size_t>(focused)].enabled
              && model.view().rows[static_cast<std::size_t>(focused)].key == "details-favorite",
          "live details enter on a visible enabled favorite action");
    check(model.action("focus:details-more") && model.action("cross"), "personal context opens");
    check(has_row(model, "favorite") && has_row(model, "collection") && has_row(model, "note")
              && has_row(model, "hide") && !has_row(model, "verify") && !has_row(model, "logs"),
          "live context retains personal actions and hides fixture operations");
    const auto uninstall = std::find_if(model.view().rows.begin(), model.view().rows.end(),
                                        [](const auto& row) { return row.key == "uninstall"; });
    check(uninstall != model.view().rows.end() && !uninstall->enabled, "live Uninstall is disabled until installed");
}

void live_install_requests_and_projection() {
    auto model=make_live();
    model.UpdateAccount({AuthState::SignedIn,"123","Synthetic",{},{},true});
    check(model.UpdateLibrary({{game(42,"Synthetic")},false,{},false,false,{}}),"library loaded");
    check(model.action("details:42"), "details open");
    check(has_row(model, "details-checking"), "install state waits for Steam details");
    check(model.UpdateAppDetails(42, 300'000'000, 600'000'000, true), "app details apply");
    check(has_row(model, "details-install"), "an installable game offers Install");
    activate(model, "details-install");
    auto requests = model.TakeInstallRequests();
    check(requests.size() == 1 && requests[0].kind == playden::homebrew::InstallRequestKind::Install && requests[0].game_id == 42,
          "Install sends one request");
    model.UpdateInstalls({{42, playden::homebrew::JobState::Downloading, 50, 200, {}}}, {});
    check(has_row(model, "details-download") && !has_row(model, "details-install"), "a running job replaces Install");
    activate(model, "details-download");
    check(model.state().navigation.route == playden::homebrew::Route::Downloads, "the download row opens Downloads");
    activate(model, "job-42");
    activate(model, "job-pause-42");
    requests = model.TakeInstallRequests();
    check(requests.size() == 1 && requests[0].kind == playden::homebrew::InstallRequestKind::Pause, "Pause goes to the service");
    model.UpdateInstalls({}, {{42, 600'000'000}});
    check(model.action("details:42") && has_row(model, "details-installed"), "a finished install shows Installed");
    check(model.action("focus:details-more") && model.action("cross"), "context opens");
    activate(model, "uninstall");
    activate(model, "confirm-destructive");
    requests = model.TakeInstallRequests();
    check(requests.size() == 1 && requests[0].kind == playden::homebrew::InstallRequestKind::Uninstall && requests[0].game_id == 42,
          "Uninstall confirms and goes to the service");
    model.UpdateAppDetails(42, std::nullopt, std::nullopt, false);
    model.UpdateInstalls({}, {});
    check(model.action("details:42") && has_row(model, "details-unavailable"), "a game without Windows content says so");
}

void refresh_actions_preserve_authentication_and_wall_clock_is_external() {
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "account", "Owner", {}, {}, false});
    check(model.UpdateLibrary({{game(1, "One")}, false, {}, false, true, 100}),
          "baseline for refresh action accepted");
    model.action("r1"); model.action("r1"); model.action("r1");
    activate(model, "settings-account");
    activate(model, "account-sync");
    const auto actions = model.TakeServiceActions();
    check(actions.size() == 1 && actions.front() == ServiceAction::RefreshLibrary
              && model.state().account.authentication == AuthState::SignedIn
              && model.state().library_syncing && model.state().library_loading,
          "visible refresh action starts library delivery without changing account authentication");
    check(model.UpdateLibrary({{}, true, {}, false, true, std::nullopt})
              && model.state().account.authentication == AuthState::SignedIn,
          "loading update retains authenticated account state");
    check(model.UpdateLibrary({{game(1, "One refreshed")}, false, {}, false, true, 101})
              && model.state().account.authentication == AuthState::SignedIn
              && !model.state().library_syncing && !model.state().library_loading,
          "terminal library update clears delivery status without restoring authentication");

    auto offline = make_live();
    offline.UpdateAccount({AuthState::Offline, "account", "Owner", {}, {}, false});
    check(offline.action("refresh-library")
              && offline.state().account.authentication == AuthState::Offline
              && offline.state().library_syncing && offline.state().library_loading,
          "direct refresh preserves offline authentication state");

    check(model.state().clock_ticks == 0, "live model starts without invented wall time");
    model.SetWallClock(4'102'444'900LL);
    check(model.state().clock_ticks == 4'102'444'900LL,
          "controller can inject current Unix time into live presentation");
    model.SetWallClock(4'102'444'900LL);
    check(model.state().clock_ticks == 4'102'444'900LL,
          "repeating wall time is an idempotent update");

    Simulation simulation("fixtures-not-used", "profile-not-used.json", ModelMode::Simulation);
    const auto fixture_clock = simulation.state().clock_ticks;
    simulation.SetWallClock(1);
    check(simulation.state().clock_ticks == fixture_clock,
          "external wall clock cannot alter deterministic simulation time");
}

void live_filters_only_use_available_facts() {
    auto model=make_live();
    model.UpdateAccount({AuthState::SignedIn,"123","Synthetic",{},{},true});
    check(model.UpdateLibrary({{game(42,"Synthetic")},false,{},false,false,{}}),"library loaded");
    check(model.state().games.front().folded_title=="synthetic","production delivery caches Unicode key once");
    check(model.action("r1") && model.action("triangle"),"live filter modal opens");
    const auto& actions=model.state().navigation.modals.back().action_keys;
    for(const auto& unavailable:{"sort-recent-added","sort-install-size","filter-installed","filter-genre-adventure","filter-controller-full"})
        check(std::find(actions.begin(),actions.end(),unavailable)==actions.end(),"unknown facts are not filter choices");
    check(std::find(actions.begin(),actions.end(),"filter-compat-playable")!=actions.end(),"personal compatibility remains filterable");

    // A CM acquisition date makes "Recently added" a real fact, sorted newest first.
    auto dated=make_live();
    dated.UpdateAccount({AuthState::SignedIn,"123","Synthetic",{},{},true});
    auto older=game(42,"Alpha"),newer=game(43,"Beta"),undated=game(44,"Gamma");
    older.added_at_unix_seconds=1400000000;newer.added_at_unix_seconds=1500000000;
    check(dated.UpdateLibrary({{older,newer,undated},false,{},false,false,{}}),"dated library loaded");
    check(dated.state().games.front().added_at==1400000000,"acquisition date projected");
    check(dated.action("r1") && dated.action("triangle"),"dated filter modal opens");
    const auto& dated_actions=dated.state().navigation.modals.back().action_keys;
    check(std::find(dated_actions.begin(),dated_actions.end(),"sort-recent-added")!=dated_actions.end(),"recently added offered when dates exist");
    activate(dated,"sort-recent-added");
    check(dated.state().library.sort=="recent-added","recently added sort selected");
    const auto ordered=dated.visible_games();
    check(ordered.size()==3 && ordered[0]->id==43 && ordered[1]->id==42 && ordered[2]->id==44,
          "newest acquisition first, undated last");
}

} // namespace

int main() {
    live_install_requests_and_projection();
    live_filters_only_use_available_facts();
    projects_only_owned_facts_and_delivery_status();
    loading_and_failure_preserve_last_good_snapshot();
    rejects_invalid_snapshots_transactionally_and_leaves_simulation_alone();
    preserves_identity_and_scroll_then_chooses_a_stable_neighbor();
    live_details_keep_personal_edits_and_block_fixture_operations();
    refresh_actions_preserve_authentication_and_wall_clock_is_external();
    std::cout << "homebrew_library_model_test passed\n";
    return 0;
}
