#pragma once
#include "library/store.h"
#include "steam/app_details_service.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace playden::stores {

// What a store says about installing one game: sizes, whether it installs here, its launch options.
using GameDetails = steam::AppDetails;

// One store's game details, fetched off the presentation thread for the games the player opens.
// Requests for another store's games are ignored.
class DetailsSource {
 public:
  virtual ~DetailsSource() = default;
  virtual Store store() const = 0;
  virtual void Request(const GameId& id, std::uint64_t generation) = 0;
  // Asks again for a game already answered (its install changed); by default, as Request.
  virtual void Refresh(const GameId& id, std::uint64_t generation) { Request(id, generation); }
  virtual void Invalidate(std::uint64_t generation) = 0;
  virtual std::vector<GameDetails> Pump(std::size_t maximum) = 0;
  virtual void Shutdown() = 0;
};

}  // namespace playden::stores
