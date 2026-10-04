// browse.hpp - Pure catalog projection and controller navigation helpers.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#pragma once

#include "homebrew/simulation.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace playden::homebrew {

struct Shelf {
    std::string key;
    std::string title;
    std::vector<GameId> game_ids;
};

[[nodiscard]] std::vector<const Game*> filtered_games(const State& state);
[[nodiscard]] std::vector<Shelf> shelves(const State& state);
[[nodiscard]] std::vector<std::string> library_rail_scopes(const State& state);

// Moves only Home and Library browse focus. Returning true means the route
// consumed the direction even when focus was already against an edge.
bool move_browser(State& state, int delta_x, int delta_y);

[[nodiscard]] std::size_t previous_caret(std::string_view utf8, std::size_t caret) noexcept;
[[nodiscard]] std::size_t next_caret(std::string_view utf8, std::size_t caret) noexcept;

} // namespace playden::homebrew
