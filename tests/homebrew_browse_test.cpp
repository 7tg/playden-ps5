// homebrew_browse_test.cpp - Catalog projection and browse navigation tests.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT

#include "homebrew/browse.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace playden::homebrew;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::filesystem::path temp_profile()
{
    const auto path = std::filesystem::temp_directory_path() / "playden-homebrew-browse-test.json";
    std::error_code error;
    std::filesystem::remove(path, error);
    return path;
}

State fixture_state(const std::filesystem::path& fixtures)
{
    Simulation simulation(fixtures, temp_profile());
    check(simulation.scenario("default"), "default fixture scenario loads");
    return simulation.state();
}

void filtering_uses_real_fixture(const std::filesystem::path& fixtures)
{
    State state = fixture_state(fixtures);
    check(filtered_games(state).size() == 23, "All omits the fixture's hidden game");

    state.library.scope = "installed";
    check(filtered_games(state).size() == 5, "Installed scope uses available installations");
    state.library.scope = "favorites";
    check(filtered_games(state).size() == 2, "Favorites scope uses persisted favorite IDs");
    state.library.scope = "hidden";
    check(filtered_games(state).size() == 1 && filtered_games(state).front()->id == 900024,
          "Hidden scope exposes only hidden games");
    state.library.scope = "collection-1";
    check(filtered_games(state).size() == 3, "collection scope uses its exact membership");

    state.library.scope = "all";
    state.library.query = "night orchard";
    check(filtered_games(state).size() == 1 && filtered_games(state).front()->id == 900010,
          "query matching is case insensitive");
    state.library.query.clear();
    state.library.filters = {"installed"};
    check(filtered_games(state).size() == 5, "installed filter composes with All");
    state.library.filters = {"genre-adventure"};
    check(filtered_games(state).size() == 6, "genre token matches the fixture model");
    state.library.filters = {"controller-full"};
    check(filtered_games(state).size() == 8, "controller token matches the fixture model");
    state.library.ratings[900001] = Compatibility::Works;
    state.library.ratings[900002] = Compatibility::Playable;
    state.library.ratings[900003] = Compatibility::Broken;
    state.library.filters = {"compat-playable"};
    check(filtered_games(state).size() == 2, "compatibility filter accepts Works and Playable only");

    state.library.filters.clear();
    state.account.authentication = AuthState::SignedOut;
    state.account.cache_available = false;
    check(filtered_games(state).empty(), "signed out without cache has no browsable catalog");
    state.account.cache_available = true;
    check(filtered_games(state).size() == 23, "signed out cached catalog remains browsable");
}

template <typename Projection>
void check_unknown_last(State state, std::string sort, Projection projection, const char* message)
{
    state.library.sort = std::move(sort);
    state.games.front().*projection = std::nullopt;
    const auto games = filtered_games(state);
    bool saw_unknown = false;
    bool known_after_unknown = false;
    for (const Game* game : games)
    {
        if (!(game->*projection).has_value())
            saw_unknown = true;
        else if (saw_unknown)
            known_after_unknown = true;
    }
    check(saw_unknown && !known_after_unknown, message);
}

void all_sorts_keep_unknown_values_last(const std::filesystem::path& fixtures)
{
    State state = fixture_state(fixtures);
    state.library.sort = "name";
    const auto named = filtered_games(state);
    check(named.front()->title == "A Very Long Journey Beyond the Last Observatory: Director’s Synthetic Edition",
          "name sort is ascending");
    check_unknown_last(state, "recent-played", &Game::last_played,
                       "recently played keeps an unknown value last");
    check_unknown_last(state, "recent-added", &Game::added_at,
                       "recently added keeps an unknown value last");
    check_unknown_last(state, "playtime", &Game::playtime_minutes,
                       "playtime keeps an unknown value last");
    check_unknown_last(state, "install-size", &Game::install_bytes,
                       "install size keeps an unknown value last");
}

void shelves_are_nonempty_and_restore_children(const std::filesystem::path& fixtures)
{
    State state = fixture_state(fixtures);
    const auto projected = shelves(state);
    check(projected.size() == 5, "fixture builds four standard shelves and one pinned shelf");
    for (const Shelf& shelf : projected)
        check(!shelf.game_ids.empty(), "empty Home shelves are omitted");
    check(projected[0].key == "continue" && projected[1].key == "downloading" &&
              projected[2].key == "installed" && projected[3].key == "favorites" &&
              projected[4].key == "collection-collection-1",
          "Home shelf order is stable");

    state.navigation.route = Route::Home;
    state.navigation.home_shelf = 0;
    state.navigation.home_shelf_focus[projected[0].key] = 2;
    check(move_browser(state, 0, 1), "Home consumes vertical movement");
    check(state.navigation.home_shelf == 1, "down moves to the next shelf");
    move_browser(state, 1, 0);
    check(state.navigation.home_shelf_focus[projected[1].key] == 1,
          "horizontal movement updates only the active shelf child");
    move_browser(state, 0, 1);
    move_browser(state, 0, -1);
    check(state.navigation.home_shelf == 1 &&
              state.navigation.home_shelf_focus[projected[1].key] == 1,
          "returning to a Home shelf restores its child");
}

void library_grid_and_rail_edges(const std::filesystem::path& fixtures)
{
    State state = fixture_state(fixtures);
    state.navigation.route = Route::Library;
    const auto games = filtered_games(state);
    check(games.size() > 12, "fixture has multiple six-column rows");

    state.navigation.selected_index = 5;
    state.navigation.selected_game_id = games[5]->id;
    move_browser(state, 1, 0);
    check(state.navigation.selected_index == 5, "right clamps at the end of a grid row");
    move_browser(state, 0, 1);
    check(state.navigation.selected_index == 11, "down preserves the six-column position");
    move_browser(state, 0, 20);
    check(state.navigation.selected_index == static_cast<int>(games.size()) - 1,
          "vertical movement clamps within a short final row");

    state.navigation.selected_index = 6;
    state.navigation.selected_game_id = games[6]->id;
    move_browser(state, -1, 0);
    check(state.navigation.library_rail_focused && state.navigation.focus_key == "rail-all",
          "left from the first column enters the rail at the active scope");
    move_browser(state, 0, 99);
    const auto scopes = library_rail_scopes(state);
    check(scopes.back() == "new-collection", "rail exposes the new collection action");
    check(state.navigation.focus_key == "rail-" + scopes.back(), "rail movement clamps at its final scope");
    state.navigation.focus_key = "rail-all";
    move_browser(state, 1, 0);
    check(!state.navigation.library_rail_focused && state.navigation.selected_game_id == games[6]->id,
          "right restores the prior grid game after rail focus");

    state.library.query = "no fixture game matches this";
    state.navigation.library_rail_focused = false;
    state.navigation.focus_key = "clear-search";
    move_browser(state, -1, 0);
    check(state.navigation.library_rail_focused && state.navigation.focus_key == "rail-all",
          "an empty result grid can still move left into the rail");
}

void utf8_caret_boundaries()
{
    std::string text = "Aé星ر";
    check(previous_caret(text, text.size()) == 6, "previous caret crosses one Arabic code point");
    check(previous_caret(text, 6) == 3, "previous caret crosses one CJK code point");
    check(previous_caret(text, 2) == 1, "previous caret repairs an interior byte position");
    check(next_caret(text, 1) == 3, "next caret crosses one accented code point");
    check(next_caret(text, 2) == 3, "next caret repairs an interior byte position");
    const std::size_t caret = 6;
    const std::size_t prior = previous_caret(text, caret);
    text.erase(prior, caret - prior);
    check(text == "Aéر", "UTF-8 backspace range removes one complete code point");
}

} // namespace

int main(int argc, char** argv)
{
    check(argc == 2, "fixture directory argument is required");
    const std::filesystem::path fixtures(argv[1]);
    filtering_uses_real_fixture(fixtures);
    all_sorts_keep_unknown_values_last(fixtures);
    shelves_are_nonempty_and_restore_children(fixtures);
    library_grid_and_rail_edges(fixtures);
    utf8_caret_boundaries();
    std::cout << "homebrew browse tests passed\n";
    return 0;
}
