#pragma once
// title_icon.hpp - A game's PS5 title icon from its store cover art.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "homebrew/artwork.hpp"

#include <optional>
#include <string>

namespace playden::homebrew {

inline constexpr std::uint32_t kTitleIconSize = 512;

// The centred square of the cover, scaled to 512x512 by area averaging and
// encoded as an opaque RGB PNG. Nothing for an empty or malformed image.
std::optional<std::string> MakeTitleIcon(const ArtworkImage& cover);

}  // namespace playden::homebrew
