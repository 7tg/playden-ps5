#include "library/store.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <set>

using namespace playden;

static void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", description);
    std::exit(1);
  }
}

int main() {
  constexpr const char* kUuid = "1b4e28ba-2fa1-11d2-883f-0016d3cca427";

  Check(!GameId{}.valid(), "a default GameId is invalid");
  Check(!GameId::Steam(0).valid() && GameId::Steam(0) == GameId{}, "Steam app 0 is invalid");
  Check(GameId::Steam(440).valid() && GameId::Steam(440).value == "440", "Steam value is canonical decimal");
  Check(GameId::Steam(440).steam_app() == 440u, "steam_app returns the app ID");
  Check(!GameId::Local(kUuid).steam_app(), "a Local game has no Steam app");
  Check(GameId::Local(kUuid).valid(), "a lower-case UUID is a valid Local value");
  Check(!GameId::Local("1B4E28BA-2FA1-11D2-883F-0016D3CCA427").valid(), "an upper-case UUID is not a valid Local value");
  Check(!(GameId{Store::kSteam, "0440"}).valid(), "a Steam value with a leading zero is invalid");
  Check(!(GameId{Store::kGog, "1"}).valid(), "GOG is not implemented yet");

  const auto max = GameId::Steam(std::numeric_limits<std::uint32_t>::max());
  Check(ToKey(max) == "4294967295" && ParseKey("4294967295") == max, "UINT32_MAX round trips");
  Check(ToKey(GameId::Steam(440)) == "440", "Steam key is the bare app ID");
  Check(ParseKey("440") == GameId::Steam(440), "bare digits parse as Steam");
  Check(ToKey(GameId::Local(kUuid)) == std::string("local-") + kUuid, "Local key has the local- prefix");
  Check(ParseKey(std::string("local-") + kUuid) == GameId::Local(kUuid), "Local key round trips");

  for (const char* rejected : {"", "0", "0440", "4294967296", "99999999999999999999", "+1", "-1", " 440", "440 ",
                               "440x", "local-", "local-1B4E28BA-2FA1-11D2-883F-0016D3CCA427",
                               "local-1b4e28ba2fa111d2883f0016d3cca427", "local-1b4e28ba-2fa1-11d2-883f-0016d3cca42",
                               "local-1b4e28ba-2fa1-11d2-883f-0016d3cca427-", "gog-1", "steam-440", "LOCAL-x"}) {
    if (ParseKey(rejected)) {
      std::fprintf(stderr, "FAIL: ParseKey accepted \"%s\"\n", rejected);
      return 1;
    }
  }

  Check(GameId::Steam(9) < GameId::Steam(10), "Steam orders numerically");
  Check(!(GameId::Steam(10) < GameId::Steam(9)), "Steam numeric order is strict");
  Check(GameId::Steam(4294967295u) < GameId::Local(kUuid), "Steam sorts before Local");
  const std::set<GameId> ordered{GameId::Steam(100), GameId::Steam(9), GameId::Steam(20)};
  Check(*ordered.begin() == GameId::Steam(9) && *ordered.rbegin() == GameId::Steam(100), "set order is numeric");
  Check(GameId::Steam(440) != GameId::Steam(441) && !(GameId::Steam(440) != GameId::Steam(440)), "inequality");
  Check(GameId{Store::kSteam, "1"} != GameId{Store::kLocal, "1"}, "the store is part of the identity");

  const std::hash<GameId> hash;
  Check(hash(GameId::Steam(440)) == hash(*ParseKey("440")), "equal Steam ids hash equally");
  Check(hash(GameId::Local(kUuid)) == hash(*ParseKey(std::string("local-") + kUuid)), "equal Local ids hash equally");

  // Steam derived IDs keep today's exact text.
  const auto steam = GameId::Steam(440);
  Check(ToInstallId(steam) == "steam-440", "Steam install ID");
  Check(ParseInstallId("steam-440") == steam, "Steam install ID parses");
  Check(GeneratedRecipeId(steam) == "steam-440-auto", "Steam generated recipe ID");
  Check(ProfileId(steam) == "playden-steam-440", "Steam profile ID");
  Check(!ParseInstallId("steam-0440") && !ParseInstallId("steam-") && !ParseInstallId("440") && !ParseInstallId("gog-1"),
        "malformed install IDs are rejected");

  const auto local = GameId::Local(kUuid);
  Check(ToInstallId(local) == std::string("local-") + kUuid, "Local install ID");
  Check(ParseInstallId(ToInstallId(local)) == local, "Local install ID parses");
  Check(GeneratedRecipeId(local) == std::string("local-") + kUuid + "-auto", "Local generated recipe ID");
  Check(ProfileId(local) == std::string("playden-local-") + kUuid, "Local profile ID");

  for (const auto& id : {max, local}) {
    for (const auto& derived : {ToKey(id), ToInstallId(id), GeneratedRecipeId(id), ProfileId(id)}) {
      bool allowed = derived.size() <= 64;
      for (const char character : derived)
        allowed = allowed && ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
                              character == '-');
      if (!allowed) {
        std::fprintf(stderr, "FAIL: derived ID \"%s\" is not [a-z0-9-] within 64 bytes\n", derived.c_str());
        return 1;
      }
    }
    Check(ToKey(id).size() <= 48, "keys fit in 48 bytes");
  }

  Check(Traits(Store::kSteam).key == "steam" && Traits(Store::kSteam).name == "Steam" &&
            Traits(Store::kSteam).needs_account && Traits(Store::kSteam).cloud_saves && Traits(Store::kSteam).downloads,
        "Steam traits");
  Check(Traits(Store::kLocal).key == "local" && Traits(Store::kLocal).name == "Imported" &&
            !Traits(Store::kLocal).needs_account && !Traits(Store::kLocal).cloud_saves && !Traits(Store::kLocal).downloads,
        "Local traits");
  Check(Traits(Store::kGog).key == "gog" && Traits(Store::kGog).name == "GOG" && Traits(Store::kGog).needs_account &&
            !Traits(Store::kGog).cloud_saves && Traits(Store::kGog).downloads,
        "GOG traits");
  Check(Traits(Store::kEpic).key == "epic" && Traits(Store::kEpic).name == "Epic" && Traits(Store::kEpic).needs_account &&
            !Traits(Store::kEpic).cloud_saves && Traits(Store::kEpic).downloads,
        "Epic traits");
  Check(static_cast<int>(Store::kSteam) == 1 && static_cast<int>(Store::kGog) == 2 &&
            static_cast<int>(Store::kEpic) == 3 && static_cast<int>(Store::kLocal) == 4,
        "persisted store values");

  std::puts("library_store: ok");
  return 0;
}
