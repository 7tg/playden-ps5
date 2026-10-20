// homebrew_library_model_test.cpp - Real library projection tests.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT

#include "homebrew/simulation.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

using playden::GameSummary;
using playden::homebrew::AuthState;
using playden::homebrew::CloudRequestKind;
using playden::homebrew::CloudStatus;
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

bool has_legend(const Simulation& model, const std::string& legend) {
    const auto& legends = model.view().legends;
    return std::find(legends.begin(), legends.end(), legend) != legends.end();
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
              && std::none_of(model.view().rows.begin(), model.view().rows.end(),
                              [](const auto& row) { return row.key == "details-install" && row.enabled; })
              && !has_row(model, "details-play")
              && !has_row(model, "details-simulate-launch"),
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

const playden::homebrew::ViewRow* find_row(const Simulation& model, const std::string& key) {
    const auto found = std::find_if(model.view().rows.begin(), model.view().rows.end(),
                                    [&key](const auto& row) { return row.key == key; });
    return found == model.view().rows.end() ? nullptr : &*found;
}

void live_install_requests_and_projection() {
    auto model=make_live();
    model.UpdateAccount({AuthState::SignedIn,"123","Synthetic",{},{},true});
    check(model.UpdateLibrary({{game(42,"Synthetic")},false,{},false,false,{}}),"library loaded");
    check(model.action("details:42"), "details open");
    check(has_row(model, "details-checking"), "install state waits for Steam details");
    {
        const auto* waiting = find_row(model, "details-install");
        check(waiting && !waiting->enabled && waiting->value == "…", "Install stays in place, disabled, while Steam answers");
    }
    check(model.UpdateAppDetails(42, 300'000'000, 600'000'000, true), "app details apply");
    check(has_row(model, "details-install"), "an installable game offers Install");
    check(model.state().navigation.focus_key == "details-install",
          "focus moves from Favorite to Install once Steam's details arrive");
    model.UpdateVolumes({{"internal", "Console storage", true, 2'000'000'000, 1'000'000'000}}, "internal");
    check(find_row(model, "details-install")->value == "600 MB",
          "Install shows the install size (not the download size), the one size a game has");
    check(find_row(model, "details-install")->subtitle == "To Console storage · 1.0 GB free",
          "Install names the destination and its free space");
    activate(model, "details-install");
    auto requests = model.TakeInstallRequests();
    check(model.view().surface != "install", "a game that fits installs without a confirmation sheet");
    check(requests.size() == 1 && requests[0].kind == playden::homebrew::InstallRequestKind::Install && requests[0].game_id == 42,
          "Install sends one request");
    model.UpdateInstalls({{42, playden::homebrew::JobState::Downloading, 50, 200, {}}}, {});
    check(has_row(model, "details-download") && !has_row(model, "details-install"), "a running job replaces Install");
    check(model.state().navigation.focus_key == "details-download", "focus follows Install to the download");
    activate(model, "details-download");
    check(model.state().navigation.route == playden::homebrew::Route::Downloads, "the download row opens Downloads");
    activate(model, "job-42");
    activate(model, "job-pause-42");
    requests = model.TakeInstallRequests();
    check(requests.size() == 1 && requests[0].kind == playden::homebrew::InstallRequestKind::Pause, "Pause goes to the service");
    model.UpdateInstalls({}, {{42, 600'000'000}});
    check(model.action("details:42") && has_row(model, "details-setup"), "a finished install shows setup readiness");
    check(!has_row(model, "details-runtime"), "no runtime row before the runtime host reports");
    model.UpdateRuntime(playden::homebrew::RuntimeState::Ready,
                        {{42, "Not prepared", "Makes a private bottle and its own PS5 title", true, false}});
    check(model.action("details:42") && has_row(model, "details-play"), "an installed game offers Play");
    const auto* play = find_row(model, "details-play");
    check(play && play->enabled && play->action == "runtime-play", "Play is offered before the game is prepared");
    activate(model, "details-play");
    const auto plays = model.TakeRuntimePlayRequests();
    check(plays.size() == 1 && plays[0] == 42, "Play on an unprepared game goes to the controller, which prepares first");
    check(find_row(model, "details-setup") && find_row(model, "details-setup")->value == "Not set up",
          "setup readiness appears as a fact");
    model.RequestRuntimePrepare(42);
    check(model.TakeRuntimePrepareRequests().size() == 1, "Prepare can be queued without a button press");
    model.UpdateRuntime(playden::homebrew::RuntimeState::Ready, {{42, "Preparing", "Copying the bottle", false, false}});
    check(model.action("details:42"), "details reopen");
    check(find_row(model, "details-play")->progress == 0.0F,
          "a step without a known fraction shows no bar");
    {
        playden::homebrew::LiveRuntimeGame copying{42, "Preparing", "Copying the Windows environment", false, false};
        copying.progress = 0.42F;
        model.UpdateRuntime(playden::homebrew::RuntimeState::Ready, {copying});
        check(find_row(model, "details-play")->progress == 0.42F,
              "a copy with a known fraction shows it on Play");
        model.UpdateRuntime(playden::homebrew::RuntimeState::Ready, {{42, "Preparing", "Copying the bottle", false, false}});
    }
    check(model.state().installations.size() == 1 && !model.state().installations[0].runtime_ready, "preparing is not ready");
    model.UpdateRuntime(playden::homebrew::RuntimeState::Ready, {{42, "Ready", "FAKE78001", false, true}});
    model.UpdateInstalls({}, {{42, 600'000'000}});
    check(model.state().installations[0].runtime_ready, "readiness survives install refreshes");
    check(model.action("details:42"), "details reopen");
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

void live_install_volumes() {
    using playden::homebrew::InstallRequestKind;
    using playden::homebrew::JobState;
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Installed"), game(43, "Downloading"), game(44, "On USB"), game(45, "New")},
                               false, {}, false, false, {}}), "library loaded");
    model.UpdateVolumes({{"internal", "Console storage", true, std::nullopt, std::nullopt},
                         {"m2", "M.2 SSD", true, 2'000, 500},
                         {"usb0", "USB drive", false, std::nullopt, std::nullopt},
                         {"ext0", "Extended storage", true, 4'000, 3'000, false}}, "internal");
    model.UpdateAppDetails(43, 400, 800, true);
    model.UpdateInstalls({{43, JobState::Downloading, 100, 400, {}, "m2"}},
                         {{42, 600, "internal", true}, {44, 0, "usb0", false}});

    const auto& volumes = model.state().volumes;
    check(volumes.size() == 4 && model.state().default_volume_id == "internal", "volumes and default projected");
    check(!volumes[0].capacity_known && volumes[0].games_bytes == 600 && volumes[0].used_bytes == 600
              && volumes[0].reserved_bytes == 0, "a drive without a space report counts finished installs");
    check(volumes[1].capacity_known && volumes[1].capacity_bytes == 2'000 && volumes[1].used_bytes == 1'500
              && volumes[1].reserved_bytes == 800, "a reported drive uses its own figures plus full install reservations");
    check(!volumes[2].available, "an absent drive stays listed as unavailable");
    check(volumes[3].available && !volumes[3].writable && volumes[3].capacity_known && volumes[3].capacity_bytes == 4'000,
          "an attached drive Playden can't write to keeps its space report");

    // Settings shows the default and changes it through the install service.
    model.action("l1");
    const auto* storage = find_row(model, "settings-storage");
    check(storage && storage->enabled && storage->value == "Console storage", "Settings shows the default storage");
    activate(model, "settings-storage");
    const auto* usb = find_row(model, "volume-usb0");
    check(has_row(model, "volume-internal") && has_row(model, "volume-m2") && usb && !usb->enabled,
          "the picker lists every drive and disables an absent one");
    const auto* locked = find_row(model, "volume-ext0");
    check(locked && !locked->enabled && locked->subtitle == "No write access" && usb->subtitle == "Disconnected",
          "an attached drive Playden can't write to is told apart from an absent one");
    const auto* m2 = find_row(model, "volume-m2");
    check(m2 && m2->value.find("free") != std::string::npos, "a reported drive shows its free space");
    activate(model, "volume-m2");
    auto requests = model.TakeInstallRequests();
    check(requests.size() == 1 && requests[0].kind == InstallRequestKind::SetDefaultVolume && requests[0].volume_id == "m2",
          "choosing a drive asks the install service to use it");
    check(model.state().default_volume_id == "m2", "the choice shows immediately");
    activate(model, "settings-storage");
    activate(model, "volume-m2");
    check(model.TakeInstallRequests().empty(), "choosing the current default sends nothing");
    const auto unsaved = [&] {
        const auto& modals = model.state().navigation.modals;
        return std::count_if(modals.begin(), modals.end(), [](const auto& modal) { return modal.title == "Install drive not saved"; });
    };
    const std::vector<playden::homebrew::LiveVolume> live_volumes{{"internal", "Console storage", true, std::nullopt, std::nullopt},
                                               {"m2", "M.2 SSD", true, 2'000, 500},
                                               {"usb0", "USB drive", false, std::nullopt, std::nullopt},
                                               {"ext0", "Extended storage", true, 4'000, 3'000, false}};
    model.UpdateVolumes(live_volumes, "m2", false);
    check(unsaved() == 1, "a drive choice that couldn't be saved is said");
    model.UpdateVolumes(live_volumes, "m2", false);
    check(unsaved() == 1, "and said once, not on every refresh");
    model.action("circle");

    // Details show where a game is and where a new install goes.
    check(model.action("details:42"), "installed game opens");
    const auto* installed = find_row(model, "details-installed");
    check(!installed, "installed size and location are shown only in facts");
    check(model.action("details:44"), "game on an absent drive opens");
    installed = find_row(model, "details-installed");
    check(installed && installed->title == "Storage disconnected" && installed->subtitle == "Connect USB drive to use this game",
          "a game on an absent drive says so even without a known size");
    check(model.action("details:45") && model.UpdateAppDetails(45, 1'000, 2'000, true), "new game details");
    const auto* install = find_row(model, "details-install");
    check(install && install->subtitle.find("To M.2 SSD · ") == 0 && install->subtitle.find(" free") != std::string::npos &&
              install->subtitle.find("reservation") == std::string::npos, "Install names the default drive and its free space");
}

const playden::homebrew::Notice* find_notice(const Simulation& model, const std::string& id) {
    const auto found = std::find_if(model.state().notices.begin(), model.state().notices.end(),
                                    [&id](const auto& notice) { return notice.id == id; });
    return found == model.state().notices.end() ? nullptr : &*found;
}

void live_install_confirmation_checks_space_and_unknowns() {
    using playden::homebrew::JobState;
    using playden::homebrew::InstallRequestKind;
    constexpr std::uint64_t margin = 64ULL * 1024 * 1024;
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "New"), game(43, "Queued")}, false, {}, false, false, {}}), "library loaded");
    model.UpdateAppDetails(42, 100, 600, true);
    model.UpdateAppDetails(43, 100, 700, true);
    model.UpdateVolumes({{"internal", "Console storage", true, margin + 2'000, margin + 599},
                         {"m2", "M.2 SSD", true, margin + 2'000, margin + 1'000}}, "internal");
    check(model.action("details:42"), "details open");

    // A game that does not fit the default drive opens the sheet, which explains and cannot confirm.
    activate(model, "details-install");
    check(model.view().surface == "install" && !find_row(model, "install-confirm")->enabled,
          "a game that does not fit opens the install sheet without a usable Install");
    check(model.view().subtitle.find("Not enough space") != std::string::npos, "the sheet says why");
    check(model.TakeInstallRequests().empty(), "nothing is queued while it does not fit");

    // Another drive is for this install only: the default stays.
    activate(model, "storage-picker");
    check(model.view().title == "Install to" && model.view().subtitle.find("For this game only") == 0,
          "the picker says the choice is for this game only");
    activate(model, "volume-m2");
    check(model.view().surface == "install" && find_row(model, "install-confirm")->enabled,
          "a drive with room enables Install");
    check(model.view().subtitle.find("To M.2 SSD (this game only)") == 0 &&
              model.view().subtitle.find("Other games still install to Console storage") != std::string::npos,
          "the sheet names the one-off drive and the unchanged default");
    check(model.TakeInstallRequests().empty() && model.state().default_volume_id == "internal",
          "choosing a drive for one install does not change the default");
    activate(model, "install-confirm");
    auto requests = model.TakeInstallRequests();
    check(requests.size() == 1 && requests[0].kind == InstallRequestKind::Install && requests[0].game_id == 42 &&
              requests[0].volume_id == "m2", "the install goes to the chosen drive");

    // Reservations: the rest of each download's estimate, less what it already holds on the drive.
    model.UpdateInstalls({{43, JobState::Downloading, 90, 100, {}, "m2"}}, {});
    check(model.state().volumes[1].reserved_bytes == 700, "a download that has reported nothing reserves its estimate");
    model.UpdateInstalls({{43, JobState::Downloading, 90, 100, {}, "m2", {}, {}, 300}}, {});
    check(model.state().volumes[1].reserved_bytes == 400, "bytes a download already holds on the drive are not reserved twice");
    model.UpdateInstalls({{43, JobState::Downloading, 90, 100, {}, "m2", {}, {}, 900}}, {});
    check(model.state().volumes[1].reserved_bytes == 0, "a preallocated download reserves nothing more");
    model.UpdateInstalls({{43, JobState::Failed, 90, 100, {}, "m2"}}, {});
    check(model.state().volumes[1].reserved_bytes == 0, "failed downloads release live reservations");
    model.UpdateInstalls({{43, JobState::Cancelled, 90, 100, {}, "m2"}}, {});
    check(model.state().volumes[1].reserved_bytes == 0, "cancelled downloads hold no reservation");
    model.UpdateInstalls({{43, JobState::Downloaded, 100, 100, {}, "m2"}}, {});
    check(model.state().volumes[1].reserved_bytes == 0, "completed downloads hold no reservation");
    model.UpdateInstalls({}, {});

    // A game that fits installs at once, to the default drive.
    model.UpdateVolumes({{"internal", "Console storage", true, margin + 2'000, margin + 1'000}}, "internal");
    activate(model, "details-install");
    requests = model.TakeInstallRequests();
    check(model.view().surface != "install" && requests.size() == 1 && requests[0].volume_id.empty(),
          "a game that fits installs without the sheet");

    // An unknown size installs at once when the drive has the margin; the downloader checks it.
    model.UpdateAppDetails(42, std::nullopt, std::nullopt, true);
    check(find_row(model, "details-install")->value == "Size unknown", "unknown size never displays zero");
    activate(model, "details-install");
    check(model.TakeInstallRequests().size() == 1, "unknown size installs with a pre-download check");
    model.UpdateVolumes({{"internal", "Console storage", true, margin + 2'000, 0}}, "internal");
    activate(model, "details-install");
    check(model.view().surface == "install" && !find_row(model, "install-confirm")->enabled,
          "unknown size still needs the known download safety margin");
    activate(model, "cancel");

    // A drive whose free space cannot be read blocks; a console that cannot read it at all does not.
    model.UpdateVolumes({{"internal", "Console storage", true, std::nullopt, std::nullopt}}, "internal");
    activate(model, "details-install");
    check(!find_row(model, "install-confirm")->enabled &&
              model.view().subtitle.find("can't be read") != std::string::npos,
          "unknown capacity is honestly unavailable and blocks confirmation");
    check(model.TakeInstallRequests().empty(), "unknown capacity sends no install request");
    activate(model, "cancel");
    model.SetFreeSpaceSupported(false);
    model.UpdateVolumes({{"internal", "Console storage", true, std::nullopt, std::nullopt}}, "internal");
    activate(model, "details-install");
    check(model.TakeInstallRequests().size() == 1, "a console that cannot read free space still installs, as before");
}

void live_install_notifications() {
    using playden::homebrew::JobState;
    auto model = make_live();
    model.SetWallClock(1'000);
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Finishing"), game(43, "Failing"), game(44, "Removed")}, false, {}, false, false, {}}),
          "library loaded");

    // The first snapshot is restored state: a failed job from before restart is not announced.
    model.UpdateInstalls({{42, JobState::Downloading, 10, 100, {}, "internal"}, {43, JobState::Failed, 0, 100, "Old failure", "internal"}},
                         {{44, 500, "internal", true}});
    check(model.state().notices.empty(), "restored history raises no notification");

    // A job that finishes into an install, and a job that newly fails.
    model.UpdateInstalls({{43, JobState::Downloading, 5, 100, {}, "internal"}}, {{42, 100, "internal", true}, {44, 500, "internal", true}});
    model.UpdateInstalls({{43, JobState::Failed, 5, 100, "Steam refused the download.", "internal"}},
                         {{42, 100, "internal", true}, {44, 500, "internal", true}});
    const auto* done = find_notice(model, "install-42");
    check(done && !done->persistent && !done->failure && done->message == "Download complete" && done->detail == "Finishing",
          "a finished download is announced with the game's name");
    const auto* failed = find_notice(model, "install-43");
    check(failed && failed->failure && !failed->persistent && failed->message == "Download failed" &&
              failed->detail == "Failing · Steam refused the download." && model.state().navigation.modals.empty(),
          "a new failure is announced with its reason in a corner, not a dialog");
    model.UpdateInstalls({{43, JobState::Failed, 5, 100, "Steam refused the download.", "internal"}},
                         {{42, 100, "internal", true}, {44, 500, "internal", true}});
    check(std::count_if(model.state().notices.begin(), model.state().notices.end(),
                        [](const auto& notice) { return notice.id == "install-43"; }) == 1 &&
              model.state().navigation.modals.empty(),
          "a failure that persists is not announced twice");

    // The More menu's Favorite says what it will do and closes the menu.
    check(model.action("details:44") && model.action("focus:details-more") && model.action("cross"), "menu opens");
    check(find_row(model, "favorite") && find_row(model, "favorite")->title == "Add to Favorites", "menu offers to add");
    activate(model, "favorite");
    check(model.state().navigation.modals.empty() && model.state().library.favorites.contains(44),
          "Favorite from the menu marks the game and closes the menu");
    check(model.action("focus:details-more") && model.action("cross") &&
              find_row(model, "favorite")->title == "Remove from Favorites", "menu offers to remove a favorite");
    activate(model, "favorite");
    check(!model.state().library.favorites.contains(44), "removing works the same way");

    // Uninstall: only a removal the player asked for is announced.
    check(model.action("details:44") && model.action("focus:details-more") && model.action("cross"), "context opens");
    activate(model, "uninstall");
    activate(model, "confirm-destructive");
    model.TakeInstallRequests();
    model.UpdateInstalls({{43, JobState::Failed, 5, 100, "Steam refused the download.", "internal"}}, {{42, 100, "internal", true}});
    const auto* removed = find_notice(model, "install-44");
    check(removed && removed->message == "Game uninstalled" && removed->detail == "Removed", "an uninstall is announced");

    // Notifications count down only while visible and then disappear.
    check(model.action("details:42") && model.action("focus:details-more") && model.action("cross")
              && !model.state().navigation.modals.empty(), "a dialog opens");
    model.SetWallClock(1'030);
    check(find_notice(model, "install-42") != nullptr, "notifications wait behind a dialog");
    check(model.action("circle"), "the dialog closes");
    model.SetWallClock(1'033);
    check(find_notice(model, "install-42") != nullptr, "a visible notification lasts a few seconds");
    model.SetWallClock(1'040);
    check(!find_notice(model, "install-42") && !find_notice(model, "install-44"), "visible notifications expire");

    // Controller: a lasting warning while disconnected, a short notice on reconnect.
    model.SetControllerConnected(false);
    const auto* lost = find_notice(model, "controller");
    check(lost && lost->persistent && lost->failure, "a disconnected controller raises a lasting warning");
    model.SetControllerConnected(true);
    check(!find_notice(model, "controller") && find_notice(model, "controller-connected"),
          "reconnecting clears the warning and says so briefly");
}

void live_log_viewer() {
    using playden::homebrew::InstallRequestKind;
    using playden::homebrew::JobState;
    using playden::homebrew::LogState;
    const auto directory = std::filesystem::temp_directory_path() / ("playden-model-logs-" + std::to_string(::getpid()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory / "launch.log");  // unreadable as a file
    {
        std::ofstream log(directory / "install.log");
        for (int index = 0; index < 12; ++index)
            log << "2026-10-05T07:00:" << (10 + index) << "Z app=" << (index % 2 ? 43 : 42) << " step=trace result=r" << index << " detail=\n";
    }
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Fine"), game(43, "Broken")}, false, {}, false, false, {}}), "library loaded");
    model.UpdateInstalls({{43, JobState::Failed, 0, 100, "Download failed.", "internal"}}, {});

    // Without a log folder the Settings entry is unavailable.
    model.action("l1");
    const auto* logs_row = find_row(model, "settings-logs");
    check(logs_row && !logs_row->enabled, "no log folder: Logs is unavailable");
    model.SetLogDirectory(directory);
    logs_row = find_row(model, "settings-logs");
    check(logs_row && logs_row->enabled, "with a log folder: Logs opens");

    activate(model, "settings-logs");
    check(has_row(model, "log-install") && has_row(model, "log-auth") && has_row(model, "log-launch"), "the log picker lists each log");
    activate(model, "log-install");
    check(model.state().log_state == LogState::Ready && model.state().log_lines.size() == 12, "the install log loads every line");
    check(model.state().log_offset == 4 && has_row(model, "log-line-11") && !has_row(model, "log-line-3"),
          "the log opens at its newest lines");
    check(model.state().log_context.find("12 lines") != std::string::npos, "the log says how many lines it shows");
    check(has_row(model, "logs-refresh") && !has_row(model, "job-retry-43"), "a general log offers Refresh, not Retry");
    activate(model, "logs-back");

    activate(model, "log-auth");
    check(model.state().log_state == LogState::Ready && model.state().log_lines.empty() && has_row(model, "logs-empty"),
          "a log that does not exist yet is empty, not an error");
    activate(model, "logs-back");
    activate(model, "log-launch");
    check(model.state().log_state == LogState::Failed && has_row(model, "logs-error"), "an unreadable log reports a failure");
    activate(model, "logs-back");
    model.action("circle");

    // From a failed download: that game's lines only, with Retry.
    model.action("l1");
    model.action("l1");
    check(model.state().navigation.route == Route::Downloads, "Downloads open");
    activate(model, "job-43");
    activate(model, "logs");
    check(model.state().log_lines.size() == 6, "a game's log keeps only that game's lines");
    check(std::all_of(model.state().log_lines.begin(), model.state().log_lines.end(),
                      [](const std::string& line) { return line.find("app=43 ") != std::string::npos; }), "only app 43 lines");
    check(model.state().log_context.rfind("Broken · ", 0) == 0, "the game's log names the game");
    check(has_row(model, "job-retry-43"), "a failed download's log offers Retry");
    {
        std::ofstream log(directory / "install.log", std::ios::app);
        log << "2026-10-05T07:01:00Z app=43 step=install result=network detail=\n";
    }
    activate(model, "logs-refresh");
    check(model.state().log_lines.size() == 7, "Refresh reads new lines");
    activate(model, "job-retry-43");
    const auto requests = model.TakeInstallRequests();
    check(requests.size() == 1 && requests[0].kind == InstallRequestKind::Resume && requests[0].game_id == 43,
          "Retry from the log resumes the download");
    check(model.state().navigation.modals.empty(), "Retry closes the log and the job's actions");

    // Resume all puts every paused download back in the queue.
    model.UpdateInstalls({{43, JobState::Paused, 10, 100, {}}, {44, JobState::Paused, 0, 0, {}},
                          {45, JobState::Downloading, 5, 50, {}}}, {});
    activate(model, "job-45");
    check(has_row(model, "job-resume-all"), "a running download's actions offer Resume all while others wait");
    activate(model, "job-resume-all");
    const auto resumed = model.TakeInstallRequests();
    check(resumed.size() == 2 && resumed[0].kind == InstallRequestKind::Resume && resumed[0].game_id == 43 &&
              resumed[1].kind == InstallRequestKind::Resume && resumed[1].game_id == 44,
          "Resume all resumes each paused download");
    check(model.state().navigation.modals.empty(), "Resume all closes the actions");
    model.UpdateInstalls({{43, JobState::Paused, 10, 100, {}}, {45, JobState::Downloading, 5, 50, {}}}, {});
    activate(model, "job-43");
    check(!has_row(model, "job-resume-all"), "a single paused download offers only its own Resume");
    model.action("circle");
    std::filesystem::remove_all(directory);
}

void live_cloud_saves() {
    using playden::homebrew::CloudRequestKind;
    using playden::homebrew::CloudStatus;
    auto model = make_live();
    model.SetWallClock(10 * 86'400);
    model.UpdateAccount({AuthState::SignedIn, "123", "Player", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Saved Game"), game(43, "Not Installed")}, false, {}, false, false, {}}), "library loaded");
    model.UpdateInstalls({}, {{42, 100, "internal", true}});

    // A runtime that can't start is said on every screen, with the reason in Settings (#64).
    model.UpdateRuntimeProblem("managed runtime data root is inaccessible");
    check(std::any_of(model.state().notices.begin(), model.state().notices.end(),
                      [](const auto& notice) { return notice.id == "runtime" && notice.persistent; }),
          "a runtime that can't start shows a lasting notice");
    check(model.state().runtime_problem.find("/data/playden") != std::string::npos, "the reason says what to fix");
    model.UpdateRuntimeProblem("");
    check(model.state().runtime_problem.empty() &&
              std::none_of(model.state().notices.begin(), model.state().notices.end(),
                           [](const auto& notice) { return notice.id == "runtime"; }),
          "the notice goes once the runtime starts");

    // Settings turns automatic Cloud saves on and off; it is off by default (a fresh install never
    // changes Cloud saves unasked, #69) and saved with the preferences.
    check(!model.state().cloud_saves_auto && !model.ExportPreferences().cloud_saves_auto, "automatic Cloud saves start off");
    model.action("l1");
    const auto* automatic = find_row(model, "settings-cloud-saves");
    check(automatic && !automatic->selected, "Settings shows automatic Cloud saves off");
    activate(model, "settings-cloud-saves");
    check(model.state().cloud_saves_auto && model.ExportPreferences().cloud_saves_auto, "the toggle turns it on");
    activate(model, "settings-cloud-saves");
    check(!model.state().cloud_saves_auto, "and off again");
    activate(model, "settings-cloud-saves");
    check(model.state().cloud_saves_auto, "and on for what follows");

    check(model.action("details:43") && model.UpdateAppDetails(43, 1, 2, true) && !has_row(model, "details-cloud"),
          "a game that is not installed has no Cloud saves row");
    check(model.action("details:42"), "installed game opens");
    const auto* saves = find_row(model, "details-cloud");
    check(saves && saves->enabled && saves->action == "cloud-sync" && saves->value == "Sync", "an installed game offers Cloud saves");
    activate(model, "details-cloud");
    auto requests = model.TakeCloudRequests();
    check(requests.size() == 1 && requests[0].kind == CloudRequestKind::Sync && requests[0].game_id == 42, "Cloud saves asks for a sync");

    model.UpdateCloud({{42, CloudStatus::Checking, "Checking Steam Cloud…"}});
    saves = find_row(model, "details-cloud");
    check(saves && !saves->enabled && saves->value == "Checking…", "a running sync cannot be started twice");
    check(saves->kind == playden::homebrew::RowKind::Action, "Cloud saves stays a button while it works");
    model.UpdateCloud({{42, CloudStatus::Synced, "2 saves downloaded"}});
    saves = find_row(model, "details-cloud");
    check(saves && saves->enabled && saves->value == "Synced" && saves->subtitle == "2 saves downloaded", "a finished sync says what moved");
    const auto* synced = find_notice(model, "cloud-42");
    check(synced && synced->message == "Cloud saves synced" && synced->detail == "Saved Game · 2 saves downloaded", "a sync is announced");

    // A conflict opens a dialog describing both sides; the choice goes to the service.
    model.UpdateCloud({{42, CloudStatus::Conflict, "Saves here and in Steam Cloud both changed.", 2, 3, 2'048, 4'096,
                        9 * 86'400, 10 * 86'400}});
    check(find_notice(model, "cloud-42")->message == "Cloud saves need a choice", "a pending choice is announced");
    saves = find_row(model, "details-cloud");
    check(saves && saves->action == "cloud-choose" && saves->value == "Choose", "a conflict asks for a choice");
    activate(model, "details-cloud");
    const auto& modal = model.state().navigation.modals.back();
    check(modal.id == "confirmation" && modal.body.find("Steam Cloud: 3 files") != std::string::npos
              && modal.body.find("This console: 2 files") != std::string::npos
              && modal.body.find("changed yesterday") != std::string::npos,
          "the choice dialog describes both sides");
    check(has_row(model, "cloud-use-cloud") && has_row(model, "cloud-use-local"), "both choices are offered");
    activate(model, "cancel");
    model.UpdateCloud({{42, CloudStatus::Conflict, "Saves here and in Steam Cloud both changed.", 0, 1, 0, 4'096, 0, 10 * 86'400}});
    activate(model, "details-cloud");
    const auto& empty_side = model.state().navigation.modals.back().body;
    check(empty_side.find("Steam Cloud: 1 file, ") != std::string::npos && empty_side.find("This console: no saves.") != std::string::npos,
          "a side without saves says so, and one file is not \"1 files\"");
    activate(model, "cancel");
    model.UpdateCloud({{42, CloudStatus::Conflict, "Saves here and in Steam Cloud both changed.", 2, 3, 2'048, 4'096,
                        9 * 86'400, 10 * 86'400}});
    activate(model, "details-cloud");
    activate(model, "cloud-use-local");
    requests = model.TakeCloudRequests();
    check(requests.size() == 1 && requests[0].kind == CloudRequestKind::UseLocal && model.state().navigation.modals.empty(),
          "choosing local saves goes to the service and closes the dialog");

    // Uploads for an account Playden has not synced before need its confirmation.
    model.UpdateCloud({{42, CloudStatus::NeedsAccount, "Confirm this Steam account."}});
    activate(model, "details-cloud");
    check(model.state().navigation.modals.back().body.find("Player") != std::string::npos && has_row(model, "cloud-confirm-account"),
          "the account dialog names the signed-in account");
    activate(model, "cloud-confirm-account");
    requests = model.TakeCloudRequests();
    check(requests.size() == 1 && requests[0].kind == CloudRequestKind::ConfirmAccount, "confirming goes to the service");

    model.UpdateCloud({{42, CloudStatus::Failed, "Steam is not reachable. Try again."}});
    check(!model.state().navigation.modals.empty() && model.state().navigation.modals.back().title == "Cloud sync failed",
          "a sync failure waits in a dialog");
    activate(model, "alert-ok");
    check(find_row(model, "details-cloud")->value == "Retry", "a failure can be retried");
}

void refresh_actions_preserve_authentication_and_wall_clock_is_external() {
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "account", "Owner", {}, {}, false});
    check(model.UpdateLibrary({{game(1, "One")}, false, {}, false, true, 100}),
          "baseline for refresh action accepted");
    model.action("l1");
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

void live_empty_filter_keeps_the_signed_in_account() {
    // The Playable filter keeps only games the player rated; with no ratings the grid is empty,
    // and its only row must undo the filter rather than start a new Steam sign-in.
    auto model=make_live();
    model.UpdateAccount({AuthState::SignedIn,"123","Synthetic",{},{},true});
    check(model.UpdateLibrary({{game(42,"Synthetic")},false,{},false,false,{}}),"library loaded");
    model.TakeServiceActions();
    check(model.action("r1") && model.action("triangle"),"live filter modal opens");
    activate(model,"filter-compat-playable");
    check(model.visible_games().empty(),"no rated game passes the Playable filter");
    const auto* empty=find_row(model,"library-empty");
    check(empty!=nullptr && empty->action=="filters-reset" && empty->title=="Reset filters",
          "a filtered-out library offers Reset filters, not Connect Steam");
    activate(model,"library-empty");
    const auto actions=model.TakeServiceActions();
    check(std::find(actions.begin(),actions.end(),ServiceAction::BeginQr)==actions.end() &&
          model.state().account.authentication==AuthState::SignedIn,"resetting filters keeps the Steam sign-in");
    check(model.state().library.filters.empty() && model.visible_games().size()==1 &&
          model.state().navigation.focus_key=="game-42","reset filters shows the library again");

    // An empty scope offers the whole library back.
    check(model.action("left") && model.action("down") && model.state().navigation.focus_key=="rail-favorites" &&
          model.action("cross"),"Favorites scope opens");
    check(model.visible_games().empty(),"no favorites yet");
    empty=find_row(model,"library-empty");
    check(empty!=nullptr && empty->action=="library-show-all","an empty scope offers Show all games");
    activate(model,"library-empty");
    check(model.state().library.scope=="all" && model.visible_games().size()==1 &&
          model.state().account.authentication==AuthState::SignedIn,"Show all games returns to every game");

    // Without a catalog there is nothing to show yet, so Connect Steam stays.
    auto signed_out=make_live();
    check(signed_out.action("r1"),"library opens before sign-in");
    empty=find_row(signed_out,"library-empty");
    check(empty==nullptr || empty->action=="connect","a missing catalog still offers Connect Steam");
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

// Sort and filters offers Refresh library: it asks the controller for Steam's current list and the
// Library stays on screen. While a refresh runs, the row waits.
void live_sort_sheet_refreshes_the_library() {
    auto model=make_live();
    model.UpdateAccount({AuthState::SignedIn,"123","Synthetic",{},{},true});
    check(model.UpdateLibrary({{game(42,"Synthetic")},false,{},false,false,{}}),"library loaded");
    model.TakeServiceActions();
    check(model.action("r1") && model.action("triangle"),"live filter modal opens");
    const auto* row=find_row(model,"refresh-library");
    check(row!=nullptr && row->title=="Refresh library" && row->enabled,"the sheet offers Refresh library");
    activate(model,"refresh-library");
    const auto actions=model.TakeServiceActions();
    check(actions.size()==1 && actions.front()==ServiceAction::RefreshLibrary,"Refresh library asks for a refresh");
    check(model.state().navigation.modals.empty() && model.state().navigation.route==Route::Library &&
          model.state().library_syncing && model.state().library_loading &&
          model.state().account.authentication==AuthState::SignedIn,"the Library stays while it refreshes");
    check(model.action("triangle"),"filter modal opens while refreshing");
    row=find_row(model,"refresh-library");
    check(row!=nullptr && !row->enabled,"a refresh in progress is not started again");
    check(model.action("focus:refresh-library") && model.action("cross") && model.TakeServiceActions().empty(),
          "the disabled row asks for nothing");
}

// "More" on Continue playing shows the Library most recently played first, for that visit only: the
// sort the player chose stays saved and comes back when the Library is opened again.
void continue_playing_more_keeps_the_chosen_sort() {
    auto model=make_live();
    model.UpdateAccount({AuthState::SignedIn,"123","Synthetic",{},{},true});
    std::vector<GameSummary> games;
    for(playden::GameId id=1;id<=25;++id) {
        auto summary=game(id,"Game "+std::to_string(100+id));
        summary.last_played_unix_seconds=1'700'000'000ULL+id;
        summary.playtime_minutes=id%2 ? 1000+id : id;
        games.push_back(std::move(summary));
    }
    check(model.UpdateLibrary({std::move(games),false,{},false,false,{}}),"played library loaded");
    check(model.action("r1") && model.action("triangle"),"live filter modal opens");
    activate(model,"sort-playtime");
    check(model.state().library.sort=="playtime" && model.visible_games().front()->id==25,"playtime sort chosen");
    check(model.action("l1") && model.state().navigation.route==Route::Home,"back to Home");
    for(int step=0;step<30 && model.state().navigation.focus_key!="shelf-continue-more";++step) model.action("right");
    check(model.state().navigation.focus_key=="shelf-continue-more","More on Continue playing focused");
    check(model.action("cross") && model.state().navigation.route==Route::Library,"More opens the Library");
    const auto more=model.visible_games();
    check(more.size()==25 && more.front()->id==25 && more[1]->id==24,"More lists the most recently played first");
    check(model.state().library.sort=="playtime" && model.ExportOrganization().sort=="playtime",
          "More does not replace the saved sort");
    check(model.action("details:24") && model.action("circle") && model.state().navigation.route==Route::Library &&
          model.visible_games()[1]->id==24,"a game opened from More returns to the same order");
    check(model.action("l1") && model.action("r1") && model.state().navigation.route==Route::Library,"Library opened again");
    const auto again=model.visible_games();
    check(again.front()->id==25 && again[1]->id==23,"the chosen playtime sort is back");

    // Choosing a sort in the More view keeps it.
    check(model.action("l1"),"back to Home");
    for(int step=0;step<30 && model.state().navigation.focus_key!="shelf-continue-more";++step) model.action("right");
    check(model.action("cross") && model.action("triangle"),"sort sheet from the More view");
    activate(model,"sort-name");
    check(model.state().library.sort=="name" && model.visible_games().front()->id==1,"a chosen sort replaces the More order");
    check(model.action("l1") && model.action("r1") && model.state().library.sort=="name" &&
          model.visible_games().front()->id==1,"and stays chosen");
}

void live_uninstall_removes_title_and_keeps_saves() {
    using playden::homebrew::InstallRequestKind;
    using playden::homebrew::LiveRuntimeGame;
    using playden::homebrew::RowKind;
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Synthetic"), game(43, "No title")}, false, {}, false, false, {}}), "library loaded");
    model.UpdateAppDetails(42, 300, 600, true);
    model.UpdateInstalls({}, {{42, 600'000'000}, {43, 100}});
    LiveRuntimeGame ready;
    ready.game_id = 42; ready.status = "Ready"; ready.ready = true; ready.can_play = true; ready.has_title = true;
    LiveRuntimeGame plain;
    plain.game_id = 43; plain.status = "Not prepared"; plain.can_prepare = true;
    model.UpdateRuntime(playden::homebrew::RuntimeState::Ready, {ready, plain});

    // A prepared game: the dialog says the PS5 title goes too and offers deleting saves, off by default.
    check(model.action("details:42") && model.action("focus:details-more") && model.action("cross"), "context opens");
    activate(model, "uninstall");
    const auto& modal = model.state().navigation.modals.back();
    check(modal.body.find("PS5 title are removed") != std::string::npos, "the dialog names the PS5 title");
    check(modal.body.find("Steam Cloud saves are synced first") != std::string::npos, "Cloud is synced first");
    const auto* toggle = find_row(model, "uninstall-delete-saves");
    check(toggle && toggle->kind == RowKind::Toggle && !toggle->selected, "deleting saves is a choice, off by default");
    activate(model, "uninstall-delete-saves");
    toggle = find_row(model, "uninstall-delete-saves");
    check(toggle && toggle->selected && !model.state().navigation.modals.empty(), "the choice toggles in the open dialog");
    check(model.state().navigation.modals.back().body.find("saves and settings on this console are deleted") != std::string::npos &&
          model.state().navigation.modals.back().body.find("are kept") == std::string::npos,
          "the dialog says the saves go when the choice is on");
    activate(model, "confirm-destructive");
    auto requests = model.TakeInstallRequests();
    check(requests.size() == 1 && requests[0].kind == InstallRequestKind::Uninstall && requests[0].game_id == 42 &&
              requests[0].delete_saves, "Uninstall carries the saves choice");

    // While it runs, Play waits; an interrupted removal offers Finish removing.
    ready.uninstall_status = "Removing PS5 title…"; ready.can_play = false;
    model.UpdateRuntime(playden::homebrew::RuntimeState::Ready, {ready, plain});
    check(model.action("details:42"), "details reopen");
    const auto* play = find_row(model, "details-play");
    check(play && !play->enabled && play->value == "Removing PS5 title…", "Play shows the uninstall in progress");
    check(find_row(model, "details-setup") && find_row(model, "details-setup")->value == "Removing",
          "active uninstall replaces Ready with Removing in setup facts");
    check(!has_row(model, "details-hint"), "active uninstall has no interrupted setup hint");
    LiveRuntimeGame stuck;
    stuck.game_id = 42; stuck.status = "Removal incomplete"; stuck.can_prepare = true; stuck.has_title = true;
    stuck.detail = "The console has not confirmed that the PS5 title is gone. Retry checks again.";
    model.UpdateRuntime(playden::homebrew::RuntimeState::Ready, {stuck, plain});
    check(model.action("details:42"), "details reopen");
    const auto* retry = find_row(model, "details-play");
    check(retry && retry->title == "Finish removing" && retry->enabled && retry->action == "runtime-remove-retry",
          "an unfinished removal offers Finish removing");
    activate(model, "details-play");
    requests = model.TakeInstallRequests();
    check(requests.size() == 1 && requests[0].kind == InstallRequestKind::Uninstall && !requests[0].delete_saves,
          "Retry continues the uninstall with the saved choice");
    model.NotifyUninstallStopped(42, "The PS5 title could not be removed.");
    check(!model.state().navigation.modals.empty() && model.state().navigation.modals.back().title == "Uninstall stopped" &&
              model.state().navigation.modals.back().action_keys == std::vector<std::string>{"alert-ok"},
          "a stopped uninstall says why in a dialog with OK");
    activate(model, "alert-ok");
    check(model.state().navigation.modals.empty(), "OK dismisses the failure dialog");
    model.NotifyPlayStopped(42, "This game's program has Steam DRM that Playden can't unpack on the PS5 yet.");
    model.NotifyPlayStopped(42, "This game's program has Steam DRM that Playden can't unpack on the PS5 yet.");
    check(model.state().navigation.modals.size() == 1 && model.state().navigation.modals.back().title == "Can't play" &&
              model.state().navigation.modals.back().body.find("Steam DRM") != std::string::npos,
          "a refused Play says why, once however often it is pressed");
    activate(model, "alert-ok");
    check(model.state().navigation.modals.empty(), "a repeated refusal is not asked twice");

    // A game without a PS5 title and without Cloud: no saves choice; its saves exist only here.
    model.UpdateCloud({{43, playden::homebrew::CloudStatus::Unsupported, "No Steam Cloud saves"}});
    check(model.action("details:43") && model.action("focus:details-more") && model.action("cross"), "context opens");
    activate(model, "uninstall");
    check(!has_row(model, "uninstall-delete-saves"), "no saves choice without a PS5 title");
    check(model.state().navigation.modals.back().body.find("exist only on this console") != std::string::npos,
          "without Cloud the dialog says the saves exist only here");
    activate(model, "cancel");
    while (!model.state().navigation.modals.empty()) model.action("circle");

    // An uninstalled game with kept saves: Details shows them and can delete them.
    model.UpdateInstalls({}, {{43, 100}});
    model.UpdateKeptBottles({{42, "Synthetic", 1760000000, 5'000'000}});
    check(model.action("details:42"), "details open");
    const auto* kept = find_row(model, "details-kept");
    check(kept && kept->title == "Saves from a previous install" && !kept->value.empty(), "kept saves are shown with their size");
    check(model.state().navigation.focus_key != "details-kept-delete", "Delete saves is never focused by default");
    {
        // While Steam is still checked there is no Install yet: focus still avoids Delete saves.
        model.UpdateInstalls({}, {});
        model.UpdateKeptBottles({{42, "Synthetic", 1760000000, 5'000'000}, {43, "No title", 1760000000, 1'000'000}});
        check(model.action("details:43") && has_row(model, "details-checking") && has_row(model, "details-kept-delete"),
              "kept saves while Steam is checked");
        check(model.state().navigation.focus_key != "details-kept-delete", "a second Cross cannot land on Delete saves");
        model.UpdateInstalls({}, {{43, 100}});
        model.UpdateKeptBottles({{42, "Synthetic", 1760000000, 5'000'000}});
        check(model.action("details:42"), "details reopen");
    }
    check(has_row(model, "details-install"), "Install brings them back");
    activate(model, "details-kept-delete");
    check(model.state().navigation.modals.back().title == "Delete kept saves?", "deleting asks first");
    activate(model, "confirm-destructive");
    auto deletes = model.TakeKeptDeleteRequests();
    check(deletes.size() == 1 && deletes[0] == 42, "the confirmed deletion goes to the runtime");

    // Settings shows kept game data as one row that opens the manager.
    model.UpdateKeptBottles({{42, "Synthetic", 1760000000, 5'000'000}, {43, "", 1760000000, 3'000'000},
                             {44, "Third", 0, 1'000'000}});
    model.action("l1");
    check(model.state().navigation.route == playden::homebrew::Route::Settings, "Settings opens");
    check(!has_row(model, "settings-kept-42"), "kept games are not listed one by one in Settings");
    const auto* entry = find_row(model, "settings-kept");
    check(entry && entry->title == "Kept game data" && entry->value.find("3 games · ") == 0, "the row counts kept games and size");
    activate(model, "settings-kept");
    check(model.state().navigation.modals.back().id == "kept-data", "the row opens the kept game data manager");
    const auto* named = find_row(model, "kept-toggle-43");
    check(named && named->title == "No title" && named->kind == RowKind::Toggle && !named->selected,
          "a game without a kept name uses the library's, unchecked");
    check(find_row(model, "kept-toggle-42")->subtitle == "Kept 2025-10-09", "each game shows when it was kept");
    check(!find_row(model, "kept-selection-delete")->enabled, "nothing to delete until a game is checked");
    activate(model, "kept-toggle-43");
    check(find_row(model, "kept-toggle-43")->selected && !model.state().navigation.modals.empty(), "checking stays in the manager");
    check(find_row(model, "kept-selection-delete")->value.find("1 game · ") == 0, "Delete names how many");
    activate(model, "kept-select-all");
    check(find_row(model, "kept-toggle-42")->selected && find_row(model, "kept-toggle-44")->selected, "Select all checks every game");
    check(find_row(model, "kept-select-all")->title == "Clear selection", "with all checked it clears instead");
    activate(model, "kept-select-all");
    check(!find_row(model, "kept-toggle-42")->selected, "Clear selection unchecks every game");
    activate(model, "kept-toggle-42");
    activate(model, "kept-toggle-44");
    activate(model, "kept-selection-delete");
    const auto& confirm = model.state().navigation.modals.back();
    check(confirm.title == "Delete kept game data?" && confirm.body.find("2 games (6 MB)") != std::string::npos,
          "the confirmation names how many games and how much space");
    activate(model, "cancel");
    check(model.TakeKeptDeleteRequests().empty() && model.state().navigation.modals.back().id == "kept-data",
          "cancelling deletes nothing and returns to the manager");
    activate(model, "kept-selection-delete");
    activate(model, "confirm-destructive");
    deletes = model.TakeKeptDeleteRequests();
    check(deletes == std::vector<playden::homebrew::GameId>{42, 44}, "the confirmed games go to the runtime");
    check(model.state().navigation.modals.empty() && model.state().navigation.focus_key == "settings-kept",
          "the manager closes and focus returns to its row");

    // Kept data changes while the manager is open: it lists what is left, and closes when nothing is.
    activate(model, "settings-kept");
    model.UpdateKeptBottles({{43, "", 1760000000, 3'000'000}});
    check(!has_row(model, "kept-toggle-42") && has_row(model, "kept-toggle-43"), "the open manager follows the kept list");
    model.UpdateKeptBottles({});
    check(model.state().navigation.modals.empty(), "the manager closes when nothing is kept");
    entry = find_row(model, "settings-kept");
    check(entry && entry->value == "None" && !entry->enabled, "nothing kept says so");
    check(model.state().navigation.focus_key == "settings-kept", "focus stays on the row");
}

std::vector<std::string> installed_order(const Simulation& model) {
    std::vector<std::string> order;
    for (const auto& key : model.state().navigation.modals.back().action_keys)
        if (key.rfind("installed-toggle-", 0) == 0) order.push_back(key.substr(17));
    return order;
}

void live_storage_section_and_installed_games() {
    using playden::homebrew::InstallRequestKind;
    using playden::homebrew::JobState;
    using playden::homebrew::LiveInstallJob;
    using playden::homebrew::LiveRuntimeGame;
    using playden::homebrew::RuntimeState;
    constexpr std::int64_t now = 1'800'000'000;
    constexpr std::int64_t day = 86'400;
    constexpr std::uint64_t gb = 1'000'000'000;
    auto model = make_live();
    model.SetWallClock(now);
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    model.UpdateVolumes({{"internal", "Console storage", true, 800 * gb, 500 * gb}}, "internal");

    // Nothing installed: the Storage rows say so, and leftovers stay hidden.
    model.action("l1");
    check(model.state().navigation.route == Route::Settings, "Settings opens");
    const auto section_of = [&model](const std::string& key) {
        const auto* row = find_row(model, key);
        return row ? row->section : std::string("missing");
    };
    for (const auto* key : {"settings-storage", "settings-installed", "settings-kept", "settings-runtime"})
        check(section_of(key) == "Storage", "the storage rows are in the Storage section");
    check(section_of("settings-title-store") == "Library" && section_of("settings-cloud-saves") == "Library",
          "Library keeps its two toggles");
    check(find_row(model, "settings-storage")->action == "storage-picker", "Games storage opens the drive picker");
    std::vector<std::string> order;
    for (const auto& row : model.view().rows) if (row.section == "Storage") order.push_back(row.key);
    check(order == std::vector<std::string>{"settings-storage", "settings-installed", "settings-kept",
                                            "settings-runtime", "settings-downloads-slow"},
          "Storage lists its rows in order, without the hidden cleanup row");
    // Downloads for slow networks (#29): off by default, a toggle saved with the preferences.
    check(!find_row(model, "settings-downloads-slow")->selected && !model.ExportPreferences().downloads_slow_network,
          "standard downloads by default");
    activate(model, "settings-downloads-slow");
    check(find_row(model, "settings-downloads-slow")->selected && model.ExportPreferences().downloads_slow_network,
          "the toggle turns downloads for slow networks on");
    activate(model, "settings-downloads-slow");
    check(!model.ExportPreferences().downloads_slow_network, "and off again");
    std::vector<std::string> sections;
    for (const auto& row : model.view().rows)
        if (sections.empty() || sections.back() != row.section) sections.push_back(row.section);
    check(sections == std::vector<std::string>{"Account", "Library", "Storage", "Display", "About", "Quit Playden"},
          "the rail order is Account, Library, Storage, Display, About, Quit");
    const auto* empty = find_row(model, "settings-installed");
    check(empty && !empty->enabled && empty->value == "None", "nothing installed says None");
    check(find_row(model, "settings-kept")->value == "None" && !has_row(model, "settings-runtime-cleanup"),
          "kept data says None and the cleanup row is hidden");
    model.SetRuntimeLeftovers(1);
    order.clear();
    for (const auto& row : model.view().rows) if (row.section == "Storage") order.push_back(row.key);
    check(order == std::vector<std::string>{"settings-storage", "settings-installed", "settings-kept",
                                            "settings-runtime-cleanup", "settings-runtime", "settings-downloads-slow"},
          "unused settings data sits before Runtime status when there is some");
    model.SetRuntimeLeftovers(0);
    // The Settings drive picker changes the default, not one install's drive.
    activate(model, "settings-storage");
    check(model.state().navigation.modals.back().id == "storage" &&
              model.state().navigation.modals.back().title == "Choose storage", "Games storage opens the default picker");
    model.action("circle");
    // Up and Down stay in the section; the rail moves to another one.
    check(model.action("focus:settings-storage"), "Games storage is focused");
    model.action("up");
    check(model.state().navigation.focus_key == "settings-storage" &&
              model.view().rows[static_cast<std::size_t>(model.view().focused_row)].section == "Storage",
          "Up on Storage's first row does not leave the section");
    if (model.state().navigation.tabs_focused) model.action("down");
    check(model.action("focus:settings-downloads-slow"), "Downloads for slow networks is focused");
    model.action("down");
    check(model.state().navigation.focus_key == "settings-downloads-slow", "Down on Storage's last row stays on it");
    model.action("left");
    model.action("down");
    model.action("right");
    check(model.state().navigation.focus_key == "settings-motion", "the rail moves to the next section");

    const auto played = [&](playden::GameId id, std::string title, std::optional<std::int64_t> days) {
        auto summary = game(id, std::move(title));
        if (days) summary.last_played_unix_seconds = static_cast<std::uint64_t>(now - *days * day);
        return summary;
    };
    check(model.UpdateLibrary({{played(10, "Alpha", 10), played(11, "Beta", std::nullopt), played(12, "Gamma", 100),
                                played(13, "Delta", 1), played(14, "Echo", 50), played(15, "Foxtrot", 30),
                                played(16, "Golf", 20)}, false, {}, false, false, {}}), "library loaded");
    model.UpdateInstalls({LiveInstallJob{15, JobState::Queued, 0, gb, {}, "internal"}},
                         {{10, 5 * gb, "internal"}, {11, 20 * gb, "internal"}, {12, 5 * gb, "internal"},
                          {13, 1 * gb, "internal"}, {14, 2 * gb, "internal"}, {15, 3 * gb, "internal"},
                          {16, 4 * gb, "internal"}});
    LiveRuntimeGame titled; titled.game_id = 10; titled.status = "Ready"; titled.ready = true; titled.has_title = true;
    LiveRuntimeGame running; running.game_id = 14; running.status = "Ready"; running.ready = true; running.play_status = "Playing";
    LiveRuntimeGame preparing; preparing.game_id = 16; preparing.status = "Preparing";
    model.UpdateRuntime(RuntimeState::Ready, {titled, running, preparing});
    const auto* entry = find_row(model, "settings-installed");
    check(entry && entry->enabled && entry->value == "7 games · 40.0 GB", "the row counts installed games and size");

    // Largest first, ties by title; then never played first and the longest unplayed after.
    activate(model, "settings-installed");
    check(model.state().navigation.modals.back().id == "installed-games", "the row opens the installed games list");
    check(model.state().navigation.focus_key == "installed-toggle-11", "focus starts on the first game");
    check(installed_order(model) == std::vector<std::string>{"11", "10", "12", "16", "15", "14", "13"}, "sorted by size");
    check(find_row(model, "installed-sort")->value == "Size", "the sort row names the order");
    const auto* alpha = find_row(model, "installed-toggle-10");
    check(alpha && alpha->kind == RowKind::Toggle && alpha->title == "Alpha" && alpha->value == "5.0 GB" &&
              alpha->subtitle == "Played 10 days ago" && alpha->enabled && !alpha->selected,
          "a game row shows its size and when it was last played");
    check(find_row(model, "installed-toggle-11")->subtitle == "Never played", "a game never played says so");
    activate(model, "installed-sort");
    check(find_row(model, "installed-sort")->value == "Last played" && model.state().navigation.focus_key == "installed-sort",
          "Sort switches to last played and keeps the focus");
    check(installed_order(model) == std::vector<std::string>{"11", "12", "14", "15", "16", "10", "13"},
          "never played first, then the oldest");
    activate(model, "installed-sort");

    // Games that can't go now say why and can't be checked.
    const auto* echo = find_row(model, "installed-toggle-14");
    const auto* foxtrot = find_row(model, "installed-toggle-15");
    const auto* golf = find_row(model, "installed-toggle-16");
    check(echo && !echo->enabled && echo->subtitle == "Playing", "a running game is blocked");
    check(foxtrot && !foxtrot->enabled && foxtrot->subtitle == "In Downloads", "a game with a download is blocked");
    check(golf && !golf->enabled && golf->subtitle == "Busy", "a game being prepared is blocked");
    activate(model, "installed-toggle-14");
    check(!find_row(model, "installed-toggle-14")->selected, "a blocked game can't be checked");
    check(!find_row(model, "installed-selection-uninstall")->enabled, "nothing to uninstall until a game is checked");
    activate(model, "installed-select-all");
    for (const auto* key : {"installed-toggle-10", "installed-toggle-11", "installed-toggle-12", "installed-toggle-13"})
        check(find_row(model, key)->selected, "Select all checks every game that can go");
    for (const auto* key : {"installed-toggle-14", "installed-toggle-15", "installed-toggle-16"})
        check(!find_row(model, key)->selected, "Select all skips blocked games");
    check(find_row(model, "installed-select-all")->title == "Clear selection", "with all checked it clears instead");
    check(model.view().footer == "4 selected · 31.0 GB", "the footer names how many and how much");
    // A game that starts running leaves the selection.
    running.game_id = 13;
    model.UpdateRuntime(RuntimeState::Ready, {titled, running, preparing});
    check(!find_row(model, "installed-toggle-13")->selected && find_row(model, "installed-toggle-13")->subtitle == "Playing",
          "a game that becomes blocked leaves the selection");
    running.game_id = 14;
    model.UpdateRuntime(RuntimeState::Ready, {titled, running, preparing});
    activate(model, "installed-select-all");
    activate(model, "installed-select-all");
    check(!find_row(model, "installed-toggle-10")->selected, "Clear selection unchecks every game");

    // A batch of three: the confirmation names them and the space, with the saves choice.
    activate(model, "installed-toggle-10");
    activate(model, "installed-toggle-11");
    activate(model, "installed-toggle-12");
    activate(model, "installed-selection-uninstall");
    const auto& confirm = model.state().navigation.modals.back();
    check(confirm.destructive && confirm.title == "Uninstall 3 games?", "the confirmation counts the games");
    check(confirm.body.find("Beta, Alpha and Gamma. Frees about 30.0 GB.") == 0, "it names them and the space freed");
    check(confirm.body.find("Steam Cloud saves are synced first. If that fails for a game, that game stays installed and its "
                            "row says why.") != std::string::npos, "Cloud is synced first, and a failure keeps that game");
    const auto* saves = find_row(model, "uninstall-delete-saves");
    check(saves && !saves->selected, "deleting saves is a choice, off by default");
    activate(model, "uninstall-delete-saves");
    check(model.state().navigation.modals.back().body.find("saves and settings on this console are deleted") != std::string::npos,
          "the dialog says the saves go when the choice is on");
    activate(model, "cancel");
    check(model.TakeInstallRequests().empty() && model.state().navigation.modals.back().id == "installed-games",
          "Cancel uninstalls nothing and returns to the list");
    activate(model, "installed-selection-uninstall");
    activate(model, "confirm-destructive");
    auto requests = model.TakeInstallRequests();
    check(requests.size() == 3 && requests[0].game_id == 11 && requests[1].game_id == 10 && requests[2].game_id == 12 &&
              std::all_of(requests.begin(), requests.end(), [](const auto& request) {
                  return request.kind == InstallRequestKind::Uninstall && !request.delete_saves;
              }),
          "each checked game is uninstalled, keeping its saves");
    check(model.state().navigation.modals.size() == 1 && model.state().navigation.modals.back().id == "installed-games",
          "the list stays open as the progress view");
    // While the batch runs the list shows only its games, in the batch's order, as status rows.
    check(model.state().navigation.modals.back().action_keys ==
              std::vector<std::string>{"installed-step-11", "installed-step-10", "installed-step-12", "installed-close"},
          "the batch view lists only the batch's games, then Close");
    check(!has_row(model, "installed-sort") && !has_row(model, "installed-select-all") &&
              !has_row(model, "installed-toggle-13"), "Sort, Select all and the other games are hidden");
    check(model.state().navigation.focus_key == "installed-step-11", "focus is on the batch's first game");
    check(model.view().footer == "Uninstalling 1 of 3…", "the footer says how far the batch is");
    check(model.view().subtitle == "Uninstalling the selected games.", "the header says what the list shows");
    const auto* waiting = find_row(model, "installed-step-12");
    check(waiting && waiting->kind == RowKind::Status && waiting->subtitle == "Waiting" && waiting->value == "5.0 GB",
          "a game not started yet waits, without a checkbox");
    const auto batch_notices = model.state().notices.size();
    check(!model.action("triangle") || model.TakeInstallRequests().empty(), "Triangle starts nothing in the batch view");
    titled.uninstall_status = "Syncing saves…";
    model.UpdateRuntime(RuntimeState::Ready, {titled, running, preparing});
    check(find_row(model, "installed-step-10")->subtitle == "Syncing saves…", "a row shows its game's uninstall step");

    // One stops on Cloud: its row says why, without a notice.
    const auto notices = batch_notices;
    model.NotifyUninstallStopped(12, "Cloud saves could not be synced.");
    check(model.state().notices.size() == notices && model.state().navigation.modals.back().id == "installed-games",
          "a stop in the batch posts no notice");
    const auto* stopped = find_row(model, "installed-step-12");
    check(stopped && stopped->subtitle == "Stopped: Cloud saves could not be synced." && stopped->tone == "problem",
          "the stopped game shows the reason");
    check(model.view().footer == "Uninstalling 2 of 3…", "a stop counts as done");
    check(!has_row(model, "installed-show-all"), "Show all games waits for the batch to finish");

    // The other two finish: their rows say so and the totals count only them.
    titled.uninstall_status.clear();
    model.UpdateRuntime(RuntimeState::Ready, {running, preparing});
    model.UpdateInstalls({LiveInstallJob{15, JobState::Queued, 0, gb, {}, "internal"}},
                         {{12, 5 * gb, "internal"}, {13, 1 * gb, "internal"}, {14, 2 * gb, "internal"},
                          {15, 3 * gb, "internal"}, {16, 4 * gb, "internal"}});
    check(model.state().notices.size() == notices, "the batch posts no Game uninstalled notices");
    const auto* gone = find_row(model, "installed-step-11");
    check(gone && gone->subtitle == "Uninstalled" && gone->tone == "good" && gone->value == "20.0 GB", "an uninstalled game says so");
    check(model.view().footer == "2 uninstalled · about 25.0 GB freed · 1 stopped", "the totals count freed space and stops");
    check(model.state().navigation.modals.back().action_keys.size() == 5 && has_row(model, "installed-show-all") &&
              find_row(model, "installed-show-all")->title == "Show all games",
          "a finished batch offers Show all games beside Close");
    check(model.view().subtitle == "Finished uninstalling.", "the header says the batch is done");
    check(model.view().legends == std::vector<std::string>{"Circle Close"}, "a status row offers no Cross");
    check(model.action("focus:installed-show-all"), "Show all games is focused");
    check(model.view().legends == std::vector<std::string>{"Cross Choose", "Circle Close"}, "a footer button offers Cross");

    // Show all games returns to the installed games; the stopped one keeps its reason and can go again.
    activate(model, "installed-show-all");
    check(installed_order(model) == std::vector<std::string>{"12", "16", "15", "14", "13"},
          "the full list shows what is installed now, without the uninstalled games");
    check(model.state().navigation.focus_key == "installed-toggle-12", "focus is on the first game");
    check(find_row(model, "installed-toggle-12")->enabled &&
              find_row(model, "installed-toggle-12")->subtitle == "Stopped: Cloud saves could not be synced.",
          "the stopped game says why and can be chosen");
    activate(model, "installed-toggle-12");
    check(model.view().footer == "1 selected · 5.0 GB", "the footer sums the selection");
    activate(model, "installed-selection-uninstall");
    check(model.state().navigation.modals.back().title == "Uninstall 1 game?", "the retry asks for the one game");
    activate(model, "confirm-destructive");
    requests = model.TakeInstallRequests();
    check(requests.size() == 1 && requests[0].game_id == 12, "the stopped game is uninstalled again");
    check(model.view().footer == "Uninstalling 1 of 1…" && find_row(model, "installed-step-12")->subtitle == "Waiting",
          "the retry is a batch of its own");
    model.UpdateInstalls({LiveInstallJob{15, JobState::Queued, 0, gb, {}, "internal"}},
                         {{13, 1 * gb, "internal"}, {14, 2 * gb, "internal"}, {15, 3 * gb, "internal"},
                          {16, 4 * gb, "internal"}});
    check(model.view().footer == "1 uninstalled · about 5.0 GB freed", "the retry's totals");
    activate(model, "installed-show-all");
    check(!has_row(model, "installed-toggle-12"), "the uninstalled game leaves the list");

    // The footer is reached from the last game and left again; Left/Right move between its buttons.
    check(model.action("focus:installed-toggle-13"), "the last game is focused");
    model.action("down");
    check(model.state().navigation.focus_key == "installed-selection-uninstall", "Down from the last game reaches the footer");
    model.action("right");
    check(model.state().navigation.focus_key == "installed-close", "Right moves to Close");
    model.action("down");
    check(model.state().navigation.focus_key == "installed-close", "Down in the footer stays");
    model.action("up");
    check(model.state().navigation.focus_key == "installed-toggle-13", "Up returns to the game the focus left");
    check(model.action("focus:installed-toggle-16"), "a game mid-list is focused");
    model.action("up");
    model.action("up");
    check(model.state().navigation.focus_key == "installed-sort", "Up from the first game reaches the pinned Sort row");

    // Triangle is Uninstall selected from anywhere in the list, and nothing without a selection.
    check(model.action("focus:installed-toggle-16"), "a game is focused");
    model.action("triangle");
    check(model.state().navigation.modals.back().id == "installed-games" && model.TakeInstallRequests().empty(),
          "Triangle does nothing with nothing checked");
    activate(model, "installed-toggle-13");
    check(model.action("focus:installed-toggle-16"), "a game is focused");
    model.action("triangle");
    check(model.state().navigation.modals.back().title == "Uninstall 1 game?", "Triangle opens the confirmation");
    activate(model, "cancel");
    check(model.state().navigation.focus_key == "installed-toggle-16", "cancelling returns to the row the player was on");
    activate(model, "installed-toggle-13");

    // Every checked game became blocked while the dialog was open: nothing goes, and the list says so.
    model.action("circle");
    activate(model, "settings-installed");
    activate(model, "installed-toggle-13");
    activate(model, "installed-selection-uninstall");
    running.game_id = 13;
    model.UpdateRuntime(RuntimeState::Ready, {running, preparing});
    activate(model, "confirm-destructive");
    check(model.TakeInstallRequests().empty() && model.view().subtitle == "None of the selected games can be uninstalled now.",
          "without a batch the list says nothing could go");

    // Closing the list ends the batch: later stops are notices again.
    const auto* close = find_row(model, "installed-close");
    check(close && close->title == "Close", "the list closes with Close");
    activate(model, "installed-close");
    check(model.state().navigation.modals.empty(), "the list closes");
    check(find_row(model, "settings-installed")->value == "4 games · 10.0 GB", "the Settings row follows");
    model.NotifyUninstallStopped(13, "Cloud saves could not be synced.");
    check(!model.state().navigation.modals.empty() && model.state().navigation.modals.back().title == "Uninstall stopped",
          "after the list closes a stop is told as before");
    model.action("circle");

    // Without a batch the list follows the installs, and closes when none is left.
    model.UpdateRuntime(RuntimeState::Ready, {});
    model.UpdateVolumes({{"internal", "Console storage", true, 800 * gb, 500 * gb}, {"usb0", "USB drive", true, {}, {}}},
                        "internal");
    model.UpdateInstalls({}, {{12, 5 * gb, "internal"}, {13, 1 * gb, "usb0"}});
    activate(model, "settings-installed");
    check(installed_order(model) == std::vector<std::string>{"12", "13"}, "the list shows what is installed now");
    check(find_row(model, "installed-toggle-13")->subtitle == "USB drive · Played yesterday" &&
              find_row(model, "installed-toggle-12")->subtitle == "Console storage · Played 100 days ago",
          "with games on two drives each row names its drive");
    check(model.action("focus:installed-toggle-13"), "the last game is focused");
    model.UpdateInstalls({}, {{12, 5 * gb, "internal"}});
    check(installed_order(model) == std::vector<std::string>{"12"}, "the list follows an uninstall that finished");
    check(model.state().navigation.focus_key == "installed-toggle-12", "focus moves to the nearest game, not the footer");
    model.UpdateInstalls({}, {});
    check(model.state().navigation.modals.empty(), "the list closes when nothing is installed");
    entry = find_row(model, "settings-installed");
    check(entry && entry->value == "None" && !entry->enabled, "nothing installed says None");
}

void live_settings_focus_stays_near_a_removed_row() {
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Synthetic")}, false, {}, false, false, {}}), "library loaded");
    model.SetRuntimeLeftovers(2);
    model.UpdateVolumes({{"internal", "Console storage", true, {}, {}}}, "internal");
    model.action("l1");
    activate(model, "settings-runtime-cleanup");
    check(!model.TakeRuntimeCleanupRequest() && !model.state().navigation.modals.empty() &&
              model.state().navigation.modals.back().destructive, "cleanup asks before deleting");
    activate(model, "cancel");
    check(!model.TakeRuntimeCleanupRequest() && has_row(model, "settings-runtime-cleanup"), "Cancel keeps the data");
    activate(model, "settings-runtime-cleanup");
    activate(model, "runtime-cleanup-confirm");
    check(model.TakeRuntimeCleanupRequest(), "cleanup goes to the runtime");
    model.SetRuntimeLeftovers(0);
    const auto& rows = model.view().rows;
    const auto focused = rows[static_cast<std::size_t>(model.view().focused_row)];
    check(!has_row(model, "settings-runtime-cleanup"), "the cleaned row is gone");
    check(focused.section == "Storage" && !focused.rail_only && !model.state().navigation.settings_rail_focused,
          "focus moves to the nearest row in the same section, not the rail");
    check(focused.enabled, "a row that can be chosen is preferred");
}

void preparation_steps_read_as_player_words() {
    using playden::homebrew::PreparationStepWords;
    for (const char* step : {"stage-title", "copy-template", "validate-artifacts", "publish-profile", "query-native",
                             "register", "await-native", "capture-settings", "something-new"}) {
        const auto words = PreparationStepWords(step);
        check(!words.empty() && words.find('-') == std::string::npos, "every step reads as words, never its identifier");
    }
    check(PreparationStepWords("copy-template") == "Copying the Windows environment", "the template copy is named");
    check(PreparationStepWords("await-native") == "Waiting for the PS5", "registration waits for the PS5");
    check(PreparationStepWords("").empty(), "no step, no words");
    using playden::homebrew::PreparationProgressWords;
    check(PreparationProgressWords(false, "") == "Step 1 of 6 · Reserving a PS5 title", "before its record, the title is reserved");
    check(PreparationProgressWords(true, "copy-template") == "Step 3 of 6 · Copying the Windows environment",
          "the template copy is step 3");
    check(PreparationProgressWords(true, "await-native") == "Step 6 of 6 · Waiting for the PS5", "registration is the last step");
    check(PreparationProgressWords(true, "something-new") == "Preparing", "an unknown step has no number");
}

void live_details_focus_moves_to_play_once_enabled() {
    using playden::homebrew::LiveRuntimeGame;
    using playden::homebrew::RuntimeState;
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Synthetic")}, false, {}, false, false, {}}), "library loaded");
    model.UpdateAppDetails(42, 300, 600, true);
    model.UpdateInstalls({}, {{42, 600'000'000}});
    check(model.action("details:42"), "details open");
    // The runtime is still checking: no Play yet, focus waits on the first enabled action.
    check(model.state().navigation.focus_key != "details-play", "no Play while the runtime checks");
    LiveRuntimeGame checking; checking.game_id = 42; checking.status = "Checking";
    model.UpdateRuntime(RuntimeState::Checking, {checking});
    check(model.state().navigation.focus_key == "details-cloud", "a disabled Play leaves focus on Cloud saves for now");
    LiveRuntimeGame ready; ready.game_id = 42; ready.status = "Ready"; ready.ready = true; ready.can_play = true; ready.has_title = true;
    model.UpdateRuntime(RuntimeState::Ready, {ready});
    check(model.state().navigation.focus_key == "details-play", "focus moves to Play once it is enabled");

    // Once the player moves, Play becoming enabled leaves focus where they put it.
    model.UpdateRuntime(RuntimeState::Checking, {checking});
    check(model.action("details:42"), "details reopen");
    check(model.action("right"), "the player moves");
    const auto chosen = model.state().navigation.focus_key;
    model.UpdateRuntime(RuntimeState::Ready, {ready});
    check(model.state().navigation.focus_key == chosen, "focus stays where the player moved it");

    // Left and Right step between the drawn buttons only: the install line and the PS5 title note are not stops.
    check(model.action("focus:details-play"), "focus Play");
    check(model.action("left") && model.state().navigation.focus_key == "details-play", "Left from Play stays on Play");
    std::vector<std::string> visited{model.state().navigation.focus_key};
    for (int step = 0; step < 6; ++step) {
        check(model.action("right"), "move right");
        if (model.state().navigation.focus_key != visited.back()) visited.push_back(model.state().navigation.focus_key);
    }
    for (const auto& key : visited) {
        const auto* item = find_row(model, key);
        check(item && (item->kind == playden::homebrew::RowKind::Action || item->kind == playden::homebrew::RowKind::Toggle),
              "every stop is a button");
    }
    check(std::find(visited.begin(), visited.end(), "details-runtime") == visited.end() &&
              std::find(visited.begin(), visited.end(), "details-installed") == visited.end(),
          "notes are skipped");
}

// Settings › Sync Cloud saves automatically, on (it starts off).
void sync_automatically(Simulation& model) {
    playden::PreferencesLoadResult loaded;
    loaded.preferences.cloud_saves_auto = true;
    check(model.ApplyPreferences(model.BeginPreferencesLoad(), loaded) && model.state().cloud_saves_auto,
          "automatic Cloud saves on");
}

void live_ready_to_play_quits_to_home() {
    using playden::homebrew::LiveRuntimeGame;
    using playden::homebrew::RuntimeState;
    auto model = make_live();
    sync_automatically(model);
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Synthetic"), game(43, "Other")}, false, {}, false, false, {}}), "library loaded");
    model.UpdateAppDetails(42, 300, 600, true);
    model.UpdateInstalls({}, {{42, 600'000'000}, {43, 100}});
    LiveRuntimeGame waiting; waiting.game_id = 42; waiting.status = "Waiting for the PS5"; waiting.awaiting_registration = true;
    waiting.can_prepare = true; waiting.has_title = true;
    waiting.detail = "Waiting for the PS5, which adds the game to Home when Playden quits";
    model.UpdateRuntime(RuntimeState::Ready, {waiting});

    // Details: Finish setup quits to Home without asking; SETUP explains the pending registration.
    check(model.action("details:42"), "details open");
    const auto* play = find_row(model, "details-play");
    check(play && play->enabled && play->action == "quit-to-home" && play->title == "Finish setup" && play->value == "Quits to Home", "Finish setup explains quitting to Home");
    check(find_row(model, "details-setup")->value == "Waiting for the PS5", "setup says the title waits for the PS5");
    check(model.state().navigation.focus_key == "details-play", "focus rests on Play");
    // Its saves sync first (#50): Play waits in a dialog, then quits once they are in step.
    model.UpdateCloud({{42, CloudStatus::UpToDate, "Saves are up to date"}});
    activate(model, "details-play");
    auto requests = model.TakeCloudRequests();
    check(requests.size() == 1 && requests[0].kind == CloudRequestKind::Sync && requests[0].game_id == 42, "Play syncs saves first");
    check(!model.state().quit_requested && model.state().navigation.modals.back().title == "Syncing Cloud saves…",
          "Play waits for the sync in a dialog");
    model.UpdateCloud({{42, CloudStatus::UpToDate, "Saves are up to date"}});
    check(!model.state().quit_requested, "a result that is not the sync's answer does not count");
    model.NotifyCloudAnswered(43, CloudStatus::Synced);
    check(!model.state().quit_requested, "another game's answer does not count");
    model.UpdateCloud({{42, CloudStatus::Synced, "1 save downloaded"}});
    model.NotifyCloudAnswered(42, CloudStatus::Synced);
    check(model.state().quit_requested && model.state().navigation.modals.empty(), "Play quits to Home once saves are in step");
    // A choice or a failure lets the player play without syncing.
    auto choosing = make_live();
    sync_automatically(choosing);
    choosing.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(choosing.UpdateLibrary({{game(42, "Synthetic")}, false, {}, false, false, {}}), "library loaded");
    choosing.UpdateInstalls({}, {{42, 600'000'000}});
    choosing.UpdateRuntime(RuntimeState::Ready, {waiting});
    check(choosing.action("details:42"), "details open");
    activate(choosing, "details-play");
    choosing.UpdateCloud({{42, CloudStatus::Conflict, "Both changed."}});
    choosing.NotifyCloudAnswered(42, CloudStatus::Conflict);
    check(!choosing.state().quit_requested && choosing.state().navigation.modals.back().title == "Cloud saves need a choice" &&
              has_row(choosing, "saves-quit-anyway"),
          "a conflict asks before quitting");
    activate(choosing, "saves-quit-anyway");
    check(choosing.state().quit_requested, "Play without syncing quits");
    // The console refusing the sync (the title may run) does not leave Play waiting.
    auto refused = make_live();
    sync_automatically(refused);
    refused.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(refused.UpdateLibrary({{game(42, "Synthetic")}, false, {}, false, false, {}}), "library loaded");
    refused.UpdateInstalls({}, {{42, 600'000'000}});
    refused.UpdateRuntime(RuntimeState::Ready, {waiting});
    check(refused.action("details:42"), "details open");
    activate(refused, "details-play");
    refused.NotifyCloudRefused(42);
    check(!refused.state().quit_requested && refused.state().navigation.modals.back().title == "The game may still be running" &&
              has_row(refused, "saves-quit-anyway"),
          "a refused sync says why and offers Play without syncing");
    // With automatic sync off, Play quits without moving saves.
    auto manual = make_live();
    manual.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(manual.UpdateLibrary({{game(42, "Synthetic")}, false, {}, false, false, {}}), "library loaded");
    manual.UpdateInstalls({}, {{42, 600'000'000}});
    manual.UpdateRuntime(RuntimeState::Ready, {waiting});
    manual.UpdateCloud({{42, CloudStatus::UpToDate, "Saves are up to date"}});
    check(manual.action("details:42"), "details open");
    activate(manual, "details-play");
    check(manual.TakeCloudRequests().empty() && manual.state().quit_requested, "automatic sync off: Play quits at once");
    // A game without Steam Cloud quits at once.
    auto local = make_live();
    local.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(local.UpdateLibrary({{game(42, "Synthetic")}, false, {}, false, false, {}}), "library loaded");
    local.UpdateInstalls({}, {{42, 600'000'000}});
    local.UpdateRuntime(RuntimeState::Ready, {waiting});
    local.UpdateCloud({{42, CloudStatus::Unsupported, "This game does not use Steam Cloud."}});
    check(local.action("details:42"), "details open");
    activate(local, "details-play");
    check(local.state().quit_requested && local.state().navigation.modals.empty(), "Play quits to Home without a dialog");

    // The prompt: shown when nothing else is open, once per game, Play quits, Later closes.
    auto prompted = make_live();
    sync_automatically(prompted);
    prompted.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(prompted.UpdateLibrary({{game(42, "Synthetic"), game(43, "Other")}, false, {}, false, false, {}}), "library loaded");
    prompted.UpdateInstalls({}, {{42, 600'000'000}, {43, 100}});
    LiveRuntimeGame other = waiting; other.game_id = 43;
    prompted.UpdateRuntime(RuntimeState::Ready, {waiting, other});
    prompted.action("l1");
    activate(prompted, "settings-about");  // a sheet is open: the prompt waits
    const auto open_before = prompted.state().navigation.modals.size();
    prompted.NotifyReadyToPlay(42);
    prompted.NotifyReadyToPlay(42);
    check(prompted.state().navigation.modals.size() == open_before, "the prompt waits for the open sheet");
    check(prompted.action("circle"), "the sheet closes");
    check(prompted.state().navigation.modals.empty(), "the prompt waits for its game's page");
    check(prompted.action("details:42"), "the announced game's page opens");
    check(!prompted.state().navigation.modals.empty(), "then the prompt shows");
    const auto& prompt = prompted.state().navigation.modals.back();
    check(prompt.title == "Synthetic is ready to play" && prompt.action_keys[static_cast<std::size_t>(prompt.focused_action)] == "quit-to-home",
          "the prompt names the game and focuses Play");
    check(find_row(prompted, "quit-to-home")->title == "Play" && find_row(prompted, "ready-later")->title == "Later", "Play or Later");
    activate(prompted, "ready-later");
    check(prompted.state().navigation.modals.empty() && !prompted.state().quit_requested, "Later closes it, once per game");
    // A game that stopped waiting (the check found it registered) is not announced.
    prompted.NotifyReadyToPlay(43);
    check(prompted.state().navigation.modals.empty(), "a background game does not interrupt another game's page");
    check(prompted.action("details:43"), "the other game's page opens");
    check(!prompted.state().navigation.modals.empty(), "another game is announced");
    activate(prompted, "quit-to-home");
    requests = prompted.TakeCloudRequests();
    check(requests.size() == 1 && requests[0].game_id == 43, "Play in the prompt syncs the announced game");
    prompted.NotifyCloudAnswered(43, CloudStatus::Synced);
    check(prompted.state().quit_requested, "Play in the prompt quits to Home");
    // Quitting would cut off another game's preparation: the quit waits for it in a dialog.
    auto busy = make_live();
    busy.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(busy.UpdateLibrary({{game(42, "Synthetic"), game(43, "Other")}, false, {}, false, false, {}}), "library loaded");
    busy.UpdateInstalls({}, {{42, 600'000'000}, {43, 100}});
    LiveRuntimeGame preparing; preparing.game_id = 43; preparing.status = "Preparing";
    busy.UpdateRuntime(RuntimeState::Ready, {waiting, preparing});
    busy.UpdateCloud({{42, CloudStatus::Unsupported, "This game does not use Steam Cloud."}});
    check(busy.action("details:42"), "details open");
    activate(busy, "details-play");
    check(!busy.state().quit_requested && busy.state().quit_waiting &&
              busy.state().navigation.modals.back().title == "Waiting for Other",
          "Play waits for the other game's preparation");
    LiveRuntimeGame prepared = preparing; prepared.status = "Ready"; prepared.ready = true;
    busy.UpdateRuntime(RuntimeState::Ready, {waiting, preparing});
    check(!busy.state().quit_requested, "still waiting while it prepares");
    busy.UpdateRuntime(RuntimeState::Ready, {waiting, prepared});
    check(busy.state().quit_requested && !busy.state().quit_waiting && busy.state().navigation.modals.empty(),
          "Playden quits once the preparation is done");
    auto cancelled = make_live();
    cancelled.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(cancelled.UpdateLibrary({{game(42, "Synthetic"), game(43, "Other")}, false, {}, false, false, {}}), "library loaded");
    cancelled.UpdateInstalls({}, {{42, 600'000'000}, {43, 100}});
    cancelled.UpdateRuntime(RuntimeState::Ready, {waiting, preparing});
    cancelled.UpdateCloud({{42, CloudStatus::Unsupported, "This game does not use Steam Cloud."}});
    check(cancelled.action("details:42"), "details open");
    activate(cancelled, "details-play");
    activate(cancelled, "cancel");
    cancelled.UpdateRuntime(RuntimeState::Ready, {waiting, prepared});
    check(!cancelled.state().quit_requested && !cancelled.state().quit_waiting, "Cancel keeps Playden open");
    // A game held behind another game's preparation says what it waits for.
    auto held = make_live();
    held.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(held.UpdateLibrary({{game(42, "Synthetic"), game(43, "Other")}, false, {}, false, false, {}}), "library loaded");
    held.UpdateInstalls({}, {{42, 600'000'000}, {43, 100}});
    LiveRuntimeGame recovering; recovering.game_id = 42; recovering.status = "Needs recovery"; recovering.has_title = true;
    held.UpdateRuntime(RuntimeState::Ready, {recovering, preparing});
    check(held.action("details:42"), "details open");
    const auto* held_play = find_row(held, "details-play");
    check(held_play && !held_play->enabled && held_play->value == "Waiting" &&
              held_play->subtitle.empty(),
          "a held setup action waits without adding a second status line");
    auto dropped = make_live();
    dropped.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(dropped.UpdateLibrary({{game(42, "Synthetic")}, false, {}, false, false, {}}), "library loaded");
    dropped.UpdateInstalls({}, {{42, 600'000'000}});
    LiveRuntimeGame ready; ready.game_id = 42; ready.status = "Ready"; ready.ready = true; ready.can_play = true;
    dropped.UpdateRuntime(RuntimeState::Ready, {ready});
    dropped.NotifyReadyToPlay(42);
    check(dropped.state().navigation.modals.empty(), "a Ready game needs no prompt");
}

void live_ready_prompt_matches_the_open_game() {
    using playden::homebrew::LiveRuntimeGame;
    using playden::homebrew::RuntimeState;
    auto model = make_live();
    sync_automatically(model);
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(1684930, "CULTIC"), game(548430, "Deep Rock Galactic")}, false, {}, false, false, {}}),
          "library loaded");
    model.UpdateInstalls({}, {{1684930, 100}, {548430, 100}});
    LiveRuntimeGame cultic; cultic.game_id = 1684930; cultic.awaiting_registration = true; cultic.has_title = true;
    auto deep_rock = cultic; deep_rock.game_id = 548430;
    model.UpdateRuntime(RuntimeState::Ready, {cultic, deep_rock});
    check(model.action("details:1684930"), "CULTIC page opens");
    model.NotifyReadyToPlay(548430);
    check(model.state().navigation.modals.empty(), "Deep Rock Galactic readiness stays queued on CULTIC's page");
    model.NotifyReadyToPlay(1684930);
    check(model.state().navigation.modals.size() == 1 &&
              model.state().navigation.modals.back().title == "CULTIC is ready to play",
          "the current game's readiness is not blocked by an earlier background game");
    activate(model, "ready-later");
    check(model.state().navigation.modals.empty(), "Later does not expose a background game's prompt");
    check(model.action("details:548430"), "Deep Rock Galactic page opens");
    check(model.state().navigation.modals.size() == 1 &&
              model.state().navigation.modals.back().title == "Deep Rock Galactic is ready to play",
          "the deferred prompt names the game on its own page");
    activate(model, "quit-to-home");
    const auto requests = model.TakeCloudRequests();
    check(requests.size() == 1 && requests[0].game_id == 548430, "Play syncs the game named in the prompt");
}

// A setup waiting behind another game's says so: the button goes quiet and the page names the game ahead.
void live_queued_setup_names_the_game_ahead() {
    using playden::homebrew::RuntimeState;
    using playden::homebrew::LiveRuntimeGame;
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Synthetic"), game(43, "Ahead")}, false, {}, false, false, {}}), "library loaded");
    model.UpdateInstalls({}, {{42, 600'000'000}, {43, 700'000'000}});
    LiveRuntimeGame queued; queued.game_id = 42; queued.status = "Needs recovery"; queued.has_title = true;
    queued.queued = true;
    LiveRuntimeGame ahead; ahead.game_id = 43; ahead.status = "Preparing"; ahead.detail = "Step 3 of 6 · Copying";
    model.UpdateRuntime(RuntimeState::Ready, {queued, ahead});
    check(model.action("details:42"), "details open");
    const auto* play = find_row(model, "details-play");
    check(play && play->title == "Finish setup" && play->value == "Queued" && !play->enabled && play->action.empty(),
          "a queued setup can't be asked for again and says it is queued");
    const auto* setup = find_row(model, "details-setup");
    check(setup && setup->value == "Waiting for Ahead" && setup->tone == "busy", "setup names the game ahead");
    const auto* hint = find_row(model, "details-hint");
    check(hint && hint->title == "Setup starts when Ahead is ready." && hint->tone == "busy", "hint says when it starts");
}

void live_play_does_the_next_useful_thing() {
    using playden::homebrew::RuntimeState;
    using playden::homebrew::LiveRuntimeGame;
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Synthetic")}, false, {}, false, false, {}}), "library loaded");
    model.UpdateInstalls({}, {{42, 600'000'000}});
    struct Case {
        const char* status; const char* label; const char* value; const char* hint;
        const char* setup; const char* tone; const char* action;
    };
    const std::vector<Case> cases{
        {"Ready", "Play", "", "", "Ready", "good", "runtime-play"},
        {"Not prepared", "Play", "Sets up first", "", "Not set up", "quiet", "runtime-play"},
        {"Kit update available", "Play", "Kit update first", "", "Kit update available", "busy", "runtime-play"},
        {"Needs repair", "Play", "Repairs first", "Some of Synthetic's setup files changed.", "Needs repair", "attention", "runtime-play"},
        {"Needs recovery", "Finish setup", "", "Setup stopped early.", "Interrupted", "attention", "runtime-prepare"},
        {"Waiting for the PS5", "Finish setup", "Quits to Home", "Quit Playden and Synthetic appears on the PS5 Home screen.", "Waiting for the PS5", "busy", "quit-to-home"},
        {"Failed", "Finish setup", "", "Setup failed.", "Failed", "problem", "runtime-prepare"},
        {"Registration pending", "Check again", "", "The PS5 hasn't confirmed Synthetic yet.", "Not confirmed", "attention", "runtime-prepare"},
        {"Removal incomplete", "Finish removing", "", "Removing Synthetic from the PS5 Home screen stopped partway.", "Removing", "attention", "runtime-remove-retry"},
        {"No kit", "Open Settings", "", "Playden needs its compatibility kit to set up games.", "No kit", "problem", ""},
    };
    for (const auto& expected : cases) {
        LiveRuntimeGame state; state.game_id = 42; state.status = expected.status;
        state.detail = "raw technical detail for bug reports";
        state.ready = state.status == "Ready"; state.can_play = state.ready;
        state.can_prepare = !state.ready && state.status != "No kit";
        state.has_title = state.status != "No kit" && state.status != "Not prepared";
        state.awaiting_registration = state.status == "Waiting for the PS5";
        model.UpdateRuntime(RuntimeState::Ready, {state});
        check(model.action("details:42"), "details open");
        const auto* play = find_row(model, "details-play");
        check(play && play->enabled && play->title == expected.label && play->value == expected.value,
              (state.status + ": main action names the next step").c_str());
        check(play->subtitle.empty(), "main action does not repeat the hint or technical detail");
        const auto* hint = find_row(model, "details-hint");
        if (std::string(expected.hint).empty()) check(!hint, "ready and routine setup have no attention hint");
        else check(hint && hint->title == expected.hint && hint->tone == expected.tone && !hint->enabled,
                   (state.status + ": one plain, coloured hint").c_str());
        const auto* setup = find_row(model, "details-setup");
        check(setup && setup->value == expected.setup && setup->tone == expected.tone && !setup->enabled,
              (state.status + ": setup fact states readiness").c_str());
        check(!has_row(model, "details-runtime") && !has_row(model, "details-installed") &&
                  !has_row(model, "details-update-runtime"), "details avoid duplicated facts and separate kit update action");
        if (state.status == "No kit") {
            activate(model, "details-play");
            check(model.state().navigation.route == Route::Settings, "missing kit opens Settings");
            check(model.TakeRuntimePlayRequests().empty() && model.TakeRuntimePrepareRequests().empty(),
                  "missing kit does not queue an impossible preparation");
            check(model.state().navigation.focus_key == "about-kit", "missing kit opens Compatibility kit in About");
            while (!model.state().navigation.modals.empty()) check(model.action("circle"), "close kit information");
        } else {
            check(play->action == expected.action, "redesigned action preserves existing behavior");
            if (play->action == "runtime-play" || play->action == "runtime-prepare") {
                const bool starts = play->action == "runtime-play";
                activate(model, "details-play");
                check((starts ? model.TakeRuntimePlayRequests() : model.TakeRuntimePrepareRequests()) ==
                          std::vector<playden::GameId>{42}, "action requests the selected game");
            }
        }
    }
    // Raw failures must not leak into the page, even when they contain paths or jargon.
    for (const std::string status : {"Needs recovery", "Failed"}) {
        for (const auto& [detail, expected] : std::vector<std::pair<std::string, std::string>>{
                 {"insufficient verified free space for package copy", status == "Failed"
                     ? "Setup failed: console storage was full." : "Setup stopped early: console storage was full."},
                 {"kit source is missing, linked, or has wrong mode", "The compatibility kit is damaged. Reinstall it."},
                 {"unknown failure /private/technical/path", status == "Failed" ? "Setup failed." : "Setup stopped early."},
                 {"", status == "Failed" ? "Setup failed." : "Setup stopped early."}}) {
            LiveRuntimeGame state; state.game_id = 42; state.status = status; state.detail = detail; state.can_prepare = true;
            model.UpdateRuntime(RuntimeState::Ready, {state});
            check(model.action("details:42"), "failed details open");
            const auto* hint = find_row(model, "details-hint");
            check(hint && hint->title == expected, (status + ": " + detail + " -> " + (hint ? hint->title : "missing hint")).c_str());
            check(model.state().runtime_games.at(42).detail == detail, "raw failure remains available for diagnostics");
        }
    }
    for (const std::string status : {"Needs recovery", "Failed"}) {
        for (const std::string code : {"space-unavailable", "kit-source", "kit-invalid-mode", "unknown-code"}) {
            LiveRuntimeGame state; state.game_id = 42; state.status = status; state.can_prepare = true;
            state.detail = "technical failure context"; state.failure_code = code;
            model.UpdateRuntime(RuntimeState::Ready, {state});
            const std::string expected = code == "space-unavailable"
                ? (status == "Failed" ? "Setup failed: console storage was full." : "Setup stopped early: console storage was full.")
                : code.starts_with("kit-") ? "The compatibility kit is damaged. Reinstall it."
                : status == "Failed" ? "Setup failed." : "Setup stopped early.";
            check(find_row(model, "details-hint") && find_row(model, "details-hint")->title == expected,
                  "recorded failure code determines the plain hint");
        }
    }
    LiveRuntimeGame preparing; preparing.game_id = 42; preparing.status = "Preparing";
    preparing.detail = "Step 3 of 6 · Copying the Windows environment";
    model.UpdateRuntime(RuntimeState::Ready, {preparing});
    check(find_row(model, "details-play") && !find_row(model, "details-play")->enabled, "preparing cannot start again");
    check(find_row(model, "details-setup") && find_row(model, "details-setup")->value == preparing.detail,
          "setup fact uses the existing preparation progress words");
    for (const auto& [runtime_state, status] : std::vector<std::pair<RuntimeState, std::string>>{
             {RuntimeState::Checking, "Checking"}, {RuntimeState::Unavailable, "Unavailable"}}) {
        LiveRuntimeGame transient; transient.game_id = 42; transient.status = status;
        model.UpdateRuntime(runtime_state, {transient});
        check(find_row(model, "details-setup") && find_row(model, "details-setup")->value == status,
              "transient runtime availability has a setup fact");
        check(!find_row(model, "details-play")->enabled && !has_row(model, "details-hint"),
              "transient runtime availability does not offer an impossible action or attention hint");
    }
    model.UpdateInstalls({}, {});
    check(!has_row(model, "details-setup") && !has_row(model, "details-hint"), "uninstalled games have no setup facts or hints");
}

void live_launch_failure_remains_visible_without_a_details_status_line() {
    using playden::homebrew::LiveRuntimeGame;
    using playden::homebrew::RuntimeState;
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Synthetic")}, false, {}, false, false, {}}), "library loaded");
    model.UpdateInstalls({}, {{42, 600'000'000}});
    LiveRuntimeGame ready; ready.game_id = 42; ready.status = "Ready"; ready.ready = true; ready.can_play = true;
    model.UpdateRuntime(RuntimeState::Ready, {ready});
    check(model.action("details:42"), "details open");
    ready.play_detail = "Could not start: technical /private/launch/path";
    model.UpdateRuntime(RuntimeState::Ready, {ready});
    check(model.state().navigation.modals.size() == 1 && model.state().navigation.modals.back().title == "Can't play",
          "a failed launch remains visible after removing the details subtitle");
    check(model.state().navigation.modals.back().body.find("/private/launch/path") == std::string::npos,
          "launch failure notice keeps technical paths out of player copy");
    activate(model, "alert-ok");
    model.UpdateRuntime(RuntimeState::Ready, {ready});
    check(model.state().navigation.modals.empty(), "an unchanged launch refusal is not repeated on refresh");
    check(!has_row(model, "details-hint"), "a ready game's launch refusal does not invent a setup hint");
    activate(model, "details-play");
    check(model.TakeRuntimePlayRequests() == std::vector<playden::GameId>{42}, "retry requests another play");
    model.UpdateRuntime(RuntimeState::Ready, {ready});
    check(model.state().navigation.modals.size() == 1, "a fresh attempt can report the same refusal again");
    activate(model, "alert-ok");
    ready.play_detail = "Cloud saves could not be checked. Press Play again to start without syncing.";
    model.UpdateRuntime(RuntimeState::Ready, {ready});
    check(model.state().navigation.modals.size() == 1 &&
              model.state().navigation.modals.back().body.find(ready.play_detail) != std::string::npos,
          "Cloud retry guidance remains reachable after removing the details subtitle");
}

void live_store_details_fill_the_description() {
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Synthetic")}, false, {}, false, false, {}}), "library loaded");
    model.UpdateStoreDetails(42, "A synthetic game.", {"Action", "Indie"}, "full");
    const auto& games = model.state().games;
    check(games[0].description == "A synthetic game." && games[0].genres.size() == 2 && games[0].controller_support == "full",
          "the store fills the description, genres and controller support");
    check(model.UpdateLibrary({{game(42, "Synthetic")}, false, {}, false, false, {}}), "library refreshed");
    check(model.state().games[0].description == "A synthetic game.", "a library refresh keeps the store description");
}

} // namespace

const playden::homebrew::ViewRow* saves_row(const Simulation& model, const std::string& key) {
    for (const auto& row : model.view().rows) if (row.key == key) return &row;
    return nullptr;
}

// Games never uploaded to this Steam account are confirmed together; choices stay per game.
void live_saves_upload_all() {
    using playden::homebrew::LiveCloudGame;
    using playden::homebrew::LiveSavesJob;
    using playden::homebrew::SavesJobKind;
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Alpha"), game(43, "Beta"), game(44, "Gamma")}, false, {}, false, false, {}}),
          "upload all: library loaded");
    model.UpdateInstalls({}, {{42, 600'000'000}, {43, 100'000'000}, {44, 100'000'000}});
    model.action("l1");
    model.action("l1");
    model.TakeCloudRequests();

    LiveCloudGame conflict{42, CloudStatus::Conflict, "Both changed."};
    LiveCloudGame synced{43, CloudStatus::Synced, "1 save uploaded"};
    model.UpdateCloud({conflict, synced});
    check(!saves_row(model, "saves-upload-all") && !has_legend(model, "Square Upload all"),
          "upload all: hidden while no game waits for the account");

    LiveCloudGame first{43, CloudStatus::NeedsAccount, "Confirm this Steam account."};
    LiveCloudGame second{44, CloudStatus::NeedsAccount, "Confirm this Steam account."};
    model.UpdateCloud({first, second});
    check(saves_row(model, "saves-upload-all") && saves_row(model, "saves-upload-all")->value == "2" &&
              has_legend(model, "Square Upload all"),
          "upload all: offered with the number of games");
    check(model.action("focus:saves-filter-4"), "upload all: focus the last filter");
    model.action("right");
    check(model.state().navigation.focus_key == "saves-upload-all", "upload all: sits between the chips and Sync all");
    model.action("right");
    check(model.state().navigation.focus_key == "saves-sync-all", "upload all: Sync all follows it");

    // One confirmation, then one request per waiting game; the conflict is left for the player.
    model.action("square");
    check(!model.state().navigation.modals.empty() &&
              model.state().navigation.modals.back().title == "Upload all saves to this Steam account?" &&
              model.state().navigation.modals.back().body.find("2 games' saves for Synthetic") != std::string::npos,
          "upload all: Square asks once for every game");
    check(model.TakeCloudRequests().empty(), "upload all: nothing is sent before the confirmation");
    activate(model, "cloud-confirm-account-all");
    auto requests = model.TakeCloudRequests();
    check(requests.size() == 2 && requests[0].kind == CloudRequestKind::ConfirmAccount && requests[0].game_id == 43 &&
              requests[1].kind == CloudRequestKind::ConfirmAccount && requests[1].game_id == 44,
          "upload all: confirms each waiting game");
    check(model.state().navigation.modals.empty(), "upload all: the dialog closes");

    // Not while Sync all runs.
    model.UpdateSavesJob({SavesJobKind::SyncAll, 0, 3, 42, false});
    check(!saves_row(model, "saves-upload-all"), "upload all: hidden while Sync all runs");
    model.action("square");
    check(model.state().navigation.modals.empty(), "upload all: Square does nothing while Sync all runs");
}

void live_saves_tab() {
    using playden::homebrew::LiveCloudGame;
    using playden::homebrew::LiveSavesJob;
    using playden::homebrew::SavesJobKind;
    auto model = make_live();
    model.SetWallClock(1'800'000'000);
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Alpha"), game(43, "Beta"), game(44, "Gamma")}, false, {}, false, false, {}}),
          "library loaded");
    model.UpdateInstalls({}, {{42, 600'000'000}, {43, 100'000'000}});
    model.UpdateKeptBottles({{44, "Gamma", 1'700'000'000, 1'900'000'000}});

    // The tab sits between Downloads and Settings; opening it asks for a quick check.
    model.action("l1");
    model.action("l1");
    check(model.state().navigation.route == Route::Saves, "Saves is the tab before Settings");
    auto requests = model.TakeCloudRequests();
    check(requests.size() == 1 && requests[0].kind == CloudRequestKind::QuickCheck, "opening Saves checks what may have changed");

    LiveCloudGame first{42, CloudStatus::Conflict, "Both changed.", 2, 3, 900'000, 1'200'000, 1'799'000'000, 1'799'900'000};
    first.first_sync = true;
    LiveCloudGame synced{43, CloudStatus::Synced, "1 save uploaded", 4, 4, 312'000, 312'000};
    synced.last_sync = 1'800'000'000 - 7200;
    LiveCloudGame kept{44, CloudStatus::UpToDate, "Saves are up to date", 6, 6, 420'000, 420'000};
    kept.background = true;
    model.UpdateCloud({first, synced, kept});
    check(saves_row(model, "save-42") && saves_row(model, "save-42")->value == "Choose saves" &&
              saves_row(model, "save-42")->subtitle == "First sync · both sides already have saves",
          "a first sync with saves on both sides asks for a choice");
    check(saves_row(model, "save-43")->value == "In sync" && saves_row(model, "save-43")->badge == "Synced 2 h ago" &&
              saves_row(model, "save-43")->aside == "312 KB",
          "a synced game says when");
    check(saves_row(model, "save-44")->subtitle == "Not installed", "kept data of an uninstalled game is listed");
    check(saves_row(model, "saves-filter-1")->value == "1" && saves_row(model, "saves-filter-4")->value == "1", "filters count");

    // Chips and Sync all form one row above the list.
    check(model.action("focus:saves-filter-0"), "focus the first filter");
    model.action("down");
    check(model.state().navigation.focus_key == "save-42", "Down enters the list");
    model.action("up");
    check(model.state().navigation.focus_key == "saves-filter-0", "Up returns to the current filter");
    activate(model, "saves-filter-1");
    check(saves_row(model, "save-42") && !saves_row(model, "save-43") && !saves_row(model, "save-44"), "Needs you shows only choices");
    activate(model, "saves-filter-0");

    // A choice found again by a later check is not announced again.
    LiveCloudGame checking_again = first;
    checking_again.status = CloudStatus::Checking;
    model.UpdateCloud({checking_again});
    const auto notices_before = model.state().notices.size();
    model.UpdateCloud({first});
    check(model.state().notices.size() == notices_before, "a known choice is not announced again");

    // Sync all, and Stop while it runs; a finished Sync all says what happened.
    check(model.action("triangle"), "Triangle works on Saves");
    requests = model.TakeCloudRequests();
    check(requests.size() == 1 && requests[0].kind == CloudRequestKind::SyncAll, "Triangle starts Sync all");
    model.UpdateSavesJob({SavesJobKind::SyncAll, 1, 3, 43, false});
    check(saves_row(model, "saves-sync-all")->title == "Stop" && model.view().subtitle.find("Syncing 2 of 3") == 0,
          "a running Sync all shows its progress and Stop");
    activate(model, "saves-sync-all");
    requests = model.TakeCloudRequests();
    check(requests.size() == 1 && requests[0].kind == CloudRequestKind::StopJob, "Stop stops the job");
    model.UpdateSavesJob({});
    check(saves_row(model, "saves-sync-all")->title == "Sync all", "Sync all comes back");
    bool announced = false;
    for (const auto& notice : model.state().notices) announced |= notice.id == "saves-sync-all";
    check(announced, "a finished Sync all is announced");

    // A background failure never opens a dialog; one the player asked for does.
    LiveCloudGame failed{43, CloudStatus::Failed, "Steam is not reachable. Try again."};
    failed.background = true;
    model.UpdateCloud({failed});
    check(model.state().navigation.modals.empty() && saves_row(model, "save-43")->value == "Couldn't sync",
          "a background failure shows only in the list");

    // A game's page: the choice goes to the service; Circle returns to its row.
    activate(model, "save-42");
    check(model.state().navigation.route == Route::SaveDetails && has_row(model, "saves-use-cloud") && has_row(model, "saves-use-local"),
          "a first sync offers both sides");
    activate(model, "saves-use-cloud");
    requests = model.TakeCloudRequests();
    check(requests.size() == 1 && requests[0].kind == CloudRequestKind::UseCloud && requests[0].game_id == 42, "the choice is sent");
    model.action("circle");
    check(model.state().navigation.route == Route::Saves && model.state().navigation.focus_key == "save-42", "Circle returns to the row");

    // Deleting an installed game's saves asks first.
    model.UpdateCloud({synced});
    activate(model, "save-43");
    activate(model, "saves-delete");
    check(model.state().navigation.modals.back().title == "Delete saves on this console?" &&
              model.state().navigation.modals.back().body.find("Steam Cloud keeps its copy") != std::string::npos,
          "deleting saves asks and says Cloud keeps a copy");
    activate(model, "saves-delete-confirm");
    requests = model.TakeCloudRequests();
    check(requests.size() == 1 && requests[0].kind == CloudRequestKind::RemoveLocal && requests[0].game_id == 43, "delete is sent");
    model.action("circle");

    // An uninstalled game's leftovers: removing them frees the kept data.
    activate(model, "save-44");
    check(!has_row(model, "saves-delete") && has_row(model, "saves-leftover"), "an uninstalled game offers Remove leftover data");
    activate(model, "saves-leftover");
    check(model.state().navigation.modals.back().body.find("1.9 GB") != std::string::npos, "the size is shown first");
    activate(model, "saves-leftover-confirm");
    requests = model.TakeCloudRequests();
    check(requests.size() == 1 && requests[0].kind == CloudRequestKind::RemoveLeftover && requests[0].game_id == 44 &&
              model.state().navigation.route == Route::Saves,
          "removal is sent and the list comes back");

    // While a game is checked, its actions wait.
    LiveCloudGame queued = synced;
    queued.queued = true;
    model.UpdateCloud({queued});
    activate(model, "save-43");
    check(saves_row(model, "saves-sync") && !saves_row(model, "saves-sync")->enabled, "actions wait while the game is checked");

    // The game page's More menu leads to the same page.
    check(model.action("details:43"), "details open");
    activate(model, "details-more");
    activate(model, "manage-saves");
    check(model.state().navigation.route == Route::SaveDetails && model.state().navigation.save_id == 43 &&
              model.state().navigation.modals.empty(),
          "More > Manage saves opens the game's saves");
}

void live_completed_downloads_follow_the_queue() {
    using namespace playden::homebrew;
    auto model = make_live();
    model.SetWallClock(100'000);
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Finished"), game(43, "Downloading"), game(45, "Earlier"), game(46, "Removed")},
                               false, {}, false, false, {}}), "library loaded");
    // 46 is listed by the service but no longer installed; 43 is downloading again.
    model.UpdateInstalls({{43, JobState::Downloading, 10, 100, {}, "internal"}},
                         {{42, 100, "internal", true}, {43, 50, "internal", true}, {45, 300, "m2", true}},
                         {{42, 100, "internal", 99'880}, {45, 300, "m2", 92'800}, {43, 50, "internal", 99'000}, {46, 9, "internal", 99'990}});
    const auto& jobs = model.state().jobs;
    check(jobs.size() == 3 && jobs[0].game_id == 43 && jobs[0].state == JobState::Downloading, "the queue comes first");
    check(jobs[1].game_id == 42 && jobs[1].state == JobState::Downloaded && jobs[1].finished_at == 99'880 &&
              jobs[1].completed_bytes == 100 && jobs[1].total_bytes == 100 && jobs[2].game_id == 45 && jobs[2].volume_id == "m2",
          "finished downloads follow it, newest first, without games downloading again or uninstalled");
    check(model.state().volumes.empty() || std::all_of(model.state().volumes.begin(), model.state().volumes.end(),
                                                       [](const Volume& volume) { return volume.reserved_bytes == 0 || volume.id == "internal"; }),
          "finished downloads reserve no space");

    model.action("r1");
    model.action("r1");
    check(model.state().navigation.route == Route::Downloads, "Downloads open");
    check(has_row(model, "job-43") && has_row(model, "job-42") && has_row(model, "job-45"), "every entry has a row");
    activate(model, "job-42");
    check(has_row(model, "job-open-42") && has_row(model, "job-dismiss-42") && has_row(model, "job-clear-completed") &&
              !has_row(model, "job-cancel-42"), "a finished download offers Open, Dismiss and Clear completed");
    activate(model, "job-dismiss-42");
    auto requests = model.TakeInstallRequests();
    check(requests.size() == 1 && requests[0].kind == InstallRequestKind::DismissCompleted && requests[0].game_id == 42,
          "Dismiss tells the download service");
    check(!has_row(model, "job-42") && has_row(model, "job-45"), "the dismissed entry goes at once");

    activate(model, "job-45");
    check(!has_row(model, "job-clear-completed"), "one finished download needs no Clear completed");
    model.action("circle");
    model.UpdateInstalls({}, {{42, 100, "internal", true}, {43, 50, "internal", true}, {45, 300, "m2", true}},
                         {{43, 50, "internal", 99'950}, {45, 300, "m2", 92'800}});
    activate(model, "job-43");
    activate(model, "job-clear-completed");
    requests = model.TakeInstallRequests();
    check(requests.size() == 1 && requests[0].kind == InstallRequestKind::ClearCompleted, "Clear completed tells the service");
    check(std::none_of(model.state().jobs.begin(), model.state().jobs.end(),
                       [](const Job& job) { return job.state == JobState::Downloaded; }) && has_row(model, "downloads-empty"),
          "Clear completed empties the list");
}

void game_collections_sheet_adds_and_removes_the_game() {
    using namespace playden::homebrew;
    auto model = make_live();
    model.UpdateAccount({AuthState::SignedIn, "123", "Synthetic", {}, {}, true});
    check(model.UpdateLibrary({{game(42, "Metro"), game(43, "Other")}, false, {}, false, false, {}}), "library loaded");
    check(model.action("details:42") && model.action("focus:details-more") && model.action("cross"), "game actions open");
    activate(model, "collection");
    check(model.state().navigation.modals.back().id == "collections" && has_row(model, "collection-new") &&
              !has_row(model, "collection-manage") && find_row(model, "collection-new")->title == "New collection",
          "with no collections the sheet offers New collection");

    activate(model, "collection-new");
    model.text("To test");
    check(model.action("done"), "the name is entered");
    const auto& collections = model.state().library.collections;
    check(collections.size() == 1 && collections[0].name == "To test" && collections[0].members.contains(42),
          "a collection made from a game's sheet holds that game");
    const auto toggle = "collection-toggle-" + collections[0].id;
    const auto* row = find_row(model, toggle);
    check(row && row->title == "To test" && row->selected && row->value == "1 game" &&
              model.state().navigation.focus_key == toggle && has_row(model, "collection-manage"),
          "the new collection shows in the open sheet, checked and focused");

    check(model.action("cross"), "the checkbox toggles");
    check(!model.state().library.collections[0].members.contains(42) && !find_row(model, toggle)->selected &&
              model.state().navigation.modals.back().id == "collections",
          "unchecking removes the game and keeps the sheet open");

    activate(model, "collection-manage");
    const auto id = model.state().library.collections[0].id;
    check(find_row(model, "collection-rename-" + id)->title == "Rename \u201cTo test\u201d" &&
              find_row(model, "collection-pin-" + id) && find_row(model, "collection-delete-" + id)->dangerous,
          "Manage collections renames, pins and deletes each collection");
    activate(model, "collection-rename-" + id);
    model.text(" later");
    check(model.action("done"), "the new name is entered");
    check(model.state().library.collections[0].name == "To test later" &&
              find_row(model, "collection-rename-" + id)->title == "Rename \u201cTo test later\u201d",
          "a rename shows in the open sheet");
}

void live_download_card_keeps_queue_order() {
    using namespace playden::homebrew;
    auto model = make_live();
    // The download retried a moment ago waits in the queue; a failed one behind it must not take the card.
    model.UpdateInstalls({{42, JobState::Queued, 0, 100, {}}, {43, JobState::Failed, 0, 100, "Could not reach Steam."}}, {});
    model.action("r1");
    model.action("r1");
    check(model.state().navigation.route == Route::Downloads, "Downloads open");
    check(!model.view().rows.empty() && model.view().rows[0].key == "job-42" && model.view().rows[1].key == "job-43",
          "the first download in the queue keeps the top spot ahead of a failed one");
    model.UpdateInstalls({{42, JobState::Queued, 0, 100, {}}, {43, JobState::Downloading, 5, 100, {}}}, {});
    check(model.view().rows[0].key == "job-43", "a running download takes the top spot");
}

void live_download_reorder_reaches_service() {
    using namespace playden::homebrew;
    auto model = make_live();
    model.UpdateInstalls({{43, JobState::Downloading, 10, 100, {}},
                         {44, JobState::Queued, 0, 100, {}},
                         {45, JobState::Queued, 0, 100, {}}}, {});
    model.action("r1");
    model.action("r1");
    check(model.state().navigation.route == Route::Downloads, "Downloads open for reordering");
    activate(model, "job-44");
    activate(model, "job-down-44");
    auto requests = model.TakeInstallRequests();
    check(requests.size() == 1 && requests[0].game_id == 44 && requests[0].kind == InstallRequestKind::MoveDown,
          "Move down sends the order change to the download service");
    activate(model, "job-45");
    activate(model, "job-up-45");
    requests = model.TakeInstallRequests();
    check(requests.size() == 1 && requests[0].game_id == 45 && requests[0].kind == InstallRequestKind::MoveUp,
          "Move up sends the order change to the download service");
}

void live_sign_in_on_full_storage_offers_uninstall() {
    auto model = make_live();
    constexpr std::uint64_t gb = 1'000'000'000;
    model.UpdateVolumes({{"internal", "Console storage", true, 800 * gb, 0}}, "internal");
    model.UpdateInstalls({}, {{12, 5 * gb, "internal"}, {13, 1 * gb, "internal"}});
    playden::homebrew::AccountUpdate full;
    full.authentication = AuthState::Failed;
    full.error = "Playden can't save your sign-in.";
    full.storage_full = true;
    model.UpdateAccount(full);
    check(model.view().title == "Console storage is full", "a sign-in that can't be saved says storage is full");
    const auto* installed = find_row(model, "signin-installed");
    check(installed && installed->enabled && installed->value == "2 games · 6.0 GB" && has_row(model, "qr-retry"),
          "it offers the installed games and Retry");
    check(model.state().navigation.focus_key == "signin-installed", "the installed games are focused");
    activate(model, "signin-installed");
    check(!model.state().navigation.modals.empty() && model.state().navigation.modals.back().title == "Installed games",
          "the list opens to uninstall from");
    model.UpdateAccount(full);
    check(!model.state().navigation.modals.empty() && model.state().navigation.modals.back().title == "Installed games",
          "the retry every few seconds leaves the open list alone");
    model.action("circle");

    playden::homebrew::AccountUpdate busy;
    busy.authentication = AuthState::Failed;
    busy.error = "Account storage is unavailable. Retrying your saved sign-in…";
    model.UpdateAccount(busy);
    check(model.view().title == "Connect Steam" && !has_row(model, "signin-installed"),
          "other storage trouble keeps the usual screen");
}

int main() {
    live_launch_failure_remains_visible_without_a_details_status_line();
    live_download_reorder_reaches_service();
    live_play_does_the_next_useful_thing();
    live_queued_setup_names_the_game_ahead();
    live_store_details_fill_the_description();
    live_ready_to_play_quits_to_home();
    live_ready_prompt_matches_the_open_game();
    live_details_focus_moves_to_play_once_enabled();
    live_settings_focus_stays_near_a_removed_row();
    live_sign_in_on_full_storage_offers_uninstall();
    live_storage_section_and_installed_games();
    preparation_steps_read_as_player_words();
    live_install_requests_and_projection();
    live_install_volumes();
    live_uninstall_removes_title_and_keeps_saves();
    live_install_confirmation_checks_space_and_unknowns();
    live_install_notifications();
    live_completed_downloads_follow_the_queue();
    live_download_card_keeps_queue_order();
    game_collections_sheet_adds_and_removes_the_game();
    live_log_viewer();
    live_cloud_saves();
    live_saves_tab();
    live_saves_upload_all();
    live_filters_only_use_available_facts();
    live_sort_sheet_refreshes_the_library();
    continue_playing_more_keeps_the_chosen_sort();
    live_empty_filter_keeps_the_signed_in_account();
    projects_only_owned_facts_and_delivery_status();
    loading_and_failure_preserve_last_good_snapshot();
    rejects_invalid_snapshots_transactionally_and_leaves_simulation_alone();
    preserves_identity_and_scroll_then_chooses_a_stable_neighbor();
    live_details_keep_personal_edits_and_block_fixture_operations();
    refresh_actions_preserve_authentication_and_wall_clock_is_external();
    std::cout << "homebrew_library_model_test passed\n";
    return 0;
}
