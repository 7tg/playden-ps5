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

const playden::homebrew::ViewRow* find_row(const Simulation& model, const std::string& key) {
    const auto found = std::find_if(model.view().rows.begin(), model.view().rows.end(),
                                    [&key](const auto& row) { return row.key == key; });
    return found == model.view().rows.end() ? nullptr : &*found;
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
                         {"usb0", "USB drive", false, std::nullopt, std::nullopt}}, "internal");
    model.UpdateInstalls({{43, JobState::Downloading, 100, 400, {}, "m2"}},
                         {{42, 600, "internal", true}, {44, 0, "usb0", false}});

    const auto& volumes = model.state().volumes;
    check(volumes.size() == 3 && model.state().default_volume_id == "internal", "volumes and default projected");
    check(!volumes[0].capacity_known && volumes[0].games_bytes == 600 && volumes[0].used_bytes == 600
              && volumes[0].reserved_bytes == 0, "a drive without a space report counts finished installs");
    check(volumes[1].capacity_known && volumes[1].capacity_bytes == 2'000 && volumes[1].used_bytes == 1'500
              && volumes[1].reserved_bytes == 300, "a reported drive uses its own figures plus the queue's remaining bytes");
    check(!volumes[2].available, "an absent drive stays listed as unavailable");

    // Settings shows the default and changes it through the install service.
    model.action("r1"); model.action("r1"); model.action("r1");
    const auto* storage = find_row(model, "settings-storage");
    check(storage && storage->enabled && storage->value == "Console storage", "Settings shows the default storage");
    activate(model, "settings-storage");
    const auto* usb = find_row(model, "volume-usb0");
    check(has_row(model, "volume-internal") && has_row(model, "volume-m2") && usb && !usb->enabled,
          "the picker lists every drive and disables an absent one");
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

    // Details show where a game is and where a new install goes.
    check(model.action("details:42"), "installed game opens");
    const auto* installed = find_row(model, "details-installed");
    check(installed && installed->title == "Installed" && installed->subtitle == "On Console storage", "an installed game names its drive");
    check(model.action("details:44"), "game on an absent drive opens");
    installed = find_row(model, "details-installed");
    check(installed && installed->title == "Storage disconnected" && installed->subtitle == "Connect USB drive to use this game",
          "a game on an absent drive says so even without a known size");
    check(model.action("details:45") && model.UpdateAppDetails(45, 1'000, 2'000, true), "new game details");
    const auto* install = find_row(model, "details-install");
    check(install && install->subtitle == "To M.2 SSD", "Install names the default drive when there is a choice");
}

const playden::homebrew::Notice* find_notice(const Simulation& model, const std::string& id) {
    const auto found = std::find_if(model.state().notices.begin(), model.state().notices.end(),
                                    [&id](const auto& notice) { return notice.id == id; });
    return found == model.state().notices.end() ? nullptr : &*found;
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
    check(failed && failed->failure && failed->message == "Download failed"
              && failed->detail == "Failing · Steam refused the download.", "a new failure is announced with its reason");
    model.UpdateInstalls({{43, JobState::Failed, 5, 100, "Steam refused the download.", "internal"}},
                         {{42, 100, "internal", true}, {44, 500, "internal", true}});
    check(std::count_if(model.state().notices.begin(), model.state().notices.end(),
                        [](const auto& notice) { return notice.id == "install-43"; }) == 1,
          "a failure that persists is not announced twice");

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
    check(find_notice(model, "install-43") != nullptr, "failures stay up longer");
    model.SetWallClock(1'060);
    check(!find_notice(model, "install-43"), "failures expire too");

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
    model.action("r1"); model.action("r1"); model.action("r1");
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
    std::filesystem::remove_all(directory);
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
    live_install_volumes();
    live_install_notifications();
    live_log_viewer();
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
