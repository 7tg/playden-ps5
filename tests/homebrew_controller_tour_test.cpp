// Controller-only traversal catches actions exposed only through replay setters.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "homebrew/simulation.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace playden::homebrew;
static void require(bool okay, const char* message) {
    if (!okay) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void press(Simulation& simulation, const char* key) {
    require(simulation.action(key), key);
}
static void choose(Simulation& simulation, const std::string& key) {
    const auto& rows = simulation.view().rows;
    const auto target = std::find_if(rows.begin(), rows.end(), [&](const ViewRow& row) { return row.key == key; });
    if (target == rows.end()) { std::fprintf(stderr, "Unreachable controller row %s in %s\n", key.c_str(), simulation.view().surface.c_str()); std::exit(1); }
    const int index = static_cast<int>(std::distance(rows.begin(), target));
    for (int count = 0; count < 100 && simulation.view().focused_row != index; ++count)
        press(simulation, simulation.view().focused_row < index ? "down" : "up");
    require(simulation.state().navigation.focus_key == key, "navigation reaches requested visible row");
    press(simulation, "cross");
}

int main(int argc, char** argv) {
    require(argc == 2, "fixture argument");
    const auto profile = std::filesystem::temp_directory_path() / "playden-hui-controller-tour-test.json";
    std::error_code error;
    std::filesystem::remove(profile, error);
    Simulation simulation(argv[1], profile);
    // Enter a Home shelf, visit details, then restore the actual shelf child.
    press(simulation, "right");
    const auto home_id = simulation.state().navigation.selected_game_id;
    const auto shelf = simulation.state().navigation.home_shelf;
    press(simulation, "cross");
    press(simulation, "circle");
    require(simulation.state().navigation.route == Route::Home &&
            simulation.state().navigation.selected_game_id == home_id &&
            simulation.state().navigation.home_shelf == shelf, "Home detail return restores shelf identity");
    press(simulation, "r1");
    press(simulation, "cross");
    const auto game = simulation.state().navigation.selected_game_id;
    choose(simulation, "details-settings");
    choose(simulation, "edit-arguments");
    simulation.text("é星ر");
    press(simulation, "square");
    require(simulation.state().editor.value == "é星", "controller backspace preserves complete UTF-8 codepoints");
    press(simulation, "circle");
    require(simulation.state().navigation.focus_key == "edit-arguments", "keyboard Cancel restores exact modal opener");
    press(simulation, "circle");
    choose(simulation, "details-more");
    choose(simulation, "note");
    choose(simulation, "rating-playable");
    require(simulation.state().library.ratings.at(game) == Compatibility::Playable, "rating reachable through controller");
    require(simulation.state().navigation.focus_key == "note", "rating picker restores its context opener");
    choose(simulation, "collection");
    choose(simulation, "collection-toggle-collection-1");
    choose(simulation, "collection-pin-collection-1");
    choose(simulation, "collection-delete-collection-1");
    require(simulation.state().navigation.focus_key == "cancel", "collection deletion defaults to Cancel");
    choose(simulation, "confirm-destructive");
    require(simulation.state().games.size() == 24 && simulation.state().library.collections.empty(),
            "controller deletes only collection organization");
    // Leave stacked menus and reach account destruction through normal sections.
    while (!simulation.state().navigation.modals.empty()) press(simulation, "circle");
    press(simulation, "circle");
    press(simulation, "r1");
    press(simulation, "r1");
    choose(simulation, "settings-signout");
    choose(simulation, "confirm-destructive");
    Simulation restored(argv[1], profile);
    require(restored.state().account.authentication == AuthState::SignedOut &&
            restored.visible_games().empty(), "controller sign-out remains signed out after restart");
    std::filesystem::remove(profile, error);
    std::puts("PASS: Home return, UTF-8/editor opener, rating/collection controller reachability, sign-out restart");
}
