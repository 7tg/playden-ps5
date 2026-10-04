#pragma once
#include <cstdint>
#include <string>

namespace playden {
using GameId = std::uint32_t; // Steam app ID, stable across refreshes and account sessions.

struct GameSummary {
    GameId id = 0;
    std::string title;
    std::string artwork_source;
    std::string description;
    std::string developer;
};
} // namespace playden
