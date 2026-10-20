#include "library/store.h"

#include <charconv>
#include <system_error>
#include <utility>

namespace playden {
namespace {

constexpr StoreTraits kSteamTraits{Store::kSteam, "steam", "Steam", true, true, true};
constexpr StoreTraits kGogTraits{Store::kGog, "gog", "GOG", true, false, true};
constexpr StoreTraits kEpicTraits{Store::kEpic, "epic", "Epic", true, false, true};
constexpr StoreTraits kLocalTraits{Store::kLocal, "local", "Imported", false, false, false};

constexpr std::string_view kLocalPrefix = "local-";

std::optional<std::uint32_t> ParseSteamValue(std::string_view text) {
  if (text.empty() || text.front() == '0') return std::nullopt;
  std::uint32_t value = 0;
  const auto* end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, value);
  if (result.ec != std::errc{} || result.ptr != end || value == 0) return std::nullopt;
  return value;
}

bool IsLowerHex(char character) {
  return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
}

// 8-4-4-4-12 lower-case hex.
bool IsLocalValue(std::string_view text) {
  if (text.size() != 36) return false;
  for (std::size_t index = 0; index < text.size(); ++index) {
    const bool dash = index == 8 || index == 13 || index == 18 || index == 23;
    if (dash ? text[index] != '-' : !IsLowerHex(text[index])) return false;
  }
  return true;
}

}  // namespace

const StoreTraits& Traits(Store store) {
  switch (store) {
    case Store::kSteam: return kSteamTraits;
    case Store::kGog: return kGogTraits;
    case Store::kEpic: return kEpicTraits;
    case Store::kLocal: return kLocalTraits;
  }
  return kSteamTraits;
}

std::optional<Store> ParseStoreKey(std::string_view key) {
  if (key == kLocalTraits.key) return Store::kLocal;
  return std::nullopt;
}

bool GameId::valid() const {
  switch (store) {
    case Store::kSteam: return ParseSteamValue(value).has_value();
    case Store::kLocal: return IsLocalValue(value);
    case Store::kGog:
    case Store::kEpic: return false;  // not implemented yet
  }
  return false;
}

std::optional<std::uint32_t> GameId::steam_app() const {
  if (store != Store::kSteam) return std::nullopt;
  return ParseSteamValue(value);
}

GameId GameId::Steam(std::uint32_t app) {
  if (app == 0) return {};
  return {Store::kSteam, std::to_string(app)};
}

GameId GameId::Local(std::string uuid) { return {Store::kLocal, std::move(uuid)}; }

bool operator==(const GameId& left, const GameId& right) {
  return left.store == right.store && left.value == right.value;
}

bool operator!=(const GameId& left, const GameId& right) { return !(left == right); }

bool operator<(const GameId& left, const GameId& right) {
  if (left.store != right.store) return left.store < right.store;
  // Canonical decimals: the shorter number is the smaller one.
  if (left.store == Store::kSteam && left.value.size() != right.value.size())
    return left.value.size() < right.value.size();
  return left.value < right.value;
}

std::string ToKey(const GameId& id) {
  if (id.store == Store::kLocal) return std::string(kLocalPrefix) + id.value;
  return id.value;
}

std::optional<GameId> ParseKey(std::string_view key) {
  if (key.substr(0, kLocalPrefix.size()) == kLocalPrefix) {
    const auto value = key.substr(kLocalPrefix.size());
    if (!IsLocalValue(value)) return std::nullopt;
    return GameId::Local(std::string(value));
  }
  if (const auto app = ParseSteamValue(key)) return GameId::Steam(*app);
  return std::nullopt;
}

std::string ToInstallId(const GameId& id) {
  if (id.store == Store::kSteam) return "steam-" + id.value;
  return ToKey(id);
}

std::optional<GameId> ParseInstallId(std::string_view install_id) {
  constexpr std::string_view kSteamPrefix = "steam-";
  if (install_id.substr(0, kSteamPrefix.size()) == kSteamPrefix) {
    const auto app = ParseSteamValue(install_id.substr(kSteamPrefix.size()));
    if (!app) return std::nullopt;
    return GameId::Steam(*app);
  }
  if (install_id.substr(0, kLocalPrefix.size()) == kLocalPrefix) return ParseKey(install_id);
  return std::nullopt;
}

std::string GeneratedRecipeId(const GameId& id) { return ToInstallId(id) + "-auto"; }

std::string ProfileId(const GameId& id) { return "playden-" + ToInstallId(id); }

}  // namespace playden
