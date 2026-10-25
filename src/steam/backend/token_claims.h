#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace playden::steam {

struct TokenClaims {
  std::uint64_t steam_id = 0;
  std::uint64_t expires_at_unix_seconds = 0;
  std::uint64_t issued_at_unix_seconds = 0;  // 0: no "iat" claim
  // Steam's "per" claim: 1 for a session kept across restarts, 0 for one that is not. Nothing: no claim.
  std::optional<std::uint64_t> persistence;
};

// Inspects local JWT claims only. This does not validate a JWT signature or establish trust.
std::optional<TokenClaims> InspectTokenClaims(std::string_view token);

}  // namespace playden::steam
