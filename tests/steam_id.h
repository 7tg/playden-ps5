#pragma once
#include "library/store.h"

#include <cstdint>

// Tests name Steam games by their app ID.
inline playden::GameId SteamId(std::uint32_t app) { return playden::GameId::Steam(app); }
