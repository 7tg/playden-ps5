#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace playden {

// Where a game comes from. The values are persisted: never renumber them.
enum class Store : std::uint8_t { kSteam = 1, kGog = 2, kEpic = 3, kLocal = 4 };

struct StoreTraits {
  Store store;
  std::string_view key;   // "steam", "local": key prefix and file names
  std::string_view name;  // "Steam", "Imported": UI and title suffix
  bool needs_account;
  bool cloud_saves;
  bool downloads;         // Local copies instead
};
const StoreTraits& Traits(Store store);
// The store named by Traits(store).key in a file ("local"). Only stores whose games can be
// stored are accepted: Local for now (Steam is never written, it is the default).
std::optional<Store> ParseStoreKey(std::string_view key);

// A game's identity across stores. Steam values are canonical decimal app IDs (1..UINT32_MAX, no leading zero);
// Local values are lower-case UUIDs. A default-constructed GameId is invalid.
struct GameId {
  Store store = Store::kSteam;
  std::string value;

  bool valid() const;
  // Steam only: the boundary into Steam code and every u32-keyed format.
  std::optional<std::uint32_t> steam_app() const;

  static GameId Steam(std::uint32_t app);
  static GameId Local(std::string uuid);
};

bool operator==(const GameId& left, const GameId& right);
bool operator!=(const GameId& left, const GameId& right);
// Store order, then Steam numerically, others by value.
bool operator<(const GameId& left, const GameId& right);

// Row and file key: Steam "440", Local "local-<uuid>". ParseKey accepts only those two forms.
std::string ToKey(const GameId& id);
std::optional<GameId> ParseKey(std::string_view key);

// Derived identifiers, [a-z0-9-] and at most 64 bytes.
std::string ToInstallId(const GameId& id);  // "steam-440", "local-<uuid>"
std::optional<GameId> ParseInstallId(std::string_view install_id);
std::string GeneratedRecipeId(const GameId& id);  // "steam-440-auto", "local-<uuid>-auto"
std::string ProfileId(const GameId& id);          // "playden-steam-440", "playden-local-<uuid>"

}  // namespace playden

namespace std {
template <>
struct hash<playden::GameId> {
  std::size_t operator()(const playden::GameId& id) const noexcept {
    const auto value = std::hash<std::string>{}(id.value);
    return value ^ (static_cast<std::size_t>(id.store) + 0x9e3779b9u + (value << 6) + (value >> 2));
  }
};
}  // namespace std
