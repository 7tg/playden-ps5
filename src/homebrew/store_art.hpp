#pragma once
// store_art.hpp - A game's artwork addresses, from its store.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "library/store.h"

#include <string>

namespace playden::homebrew {

struct Game;

// The store's art for a game: its cover (600x900), library hero and logo. Steam games get the
// Steam CDN addresses (the artwork backend's CanonicalKey accepts exactly these); games from
// other stores have none yet, and an empty address means no art (the tile shows the title).
std::string CoverUrl(const GameId& game);
std::string HeroUrl(const GameId& game);
std::string LogoUrl(const GameId& game);
std::string CoverUrl(const Game& game);
std::string HeroUrl(const Game& game);
std::string LogoUrl(const Game& game);

}  // namespace playden::homebrew
