#include "platform/ps5/title_probe_contract.h"
#include "steam/backend/json.h"
namespace playden::ps5 {
namespace {
bool text(const steam::json::Value& object, const char* key, std::string_view expected) {
  const auto* value = object.Find(key);
  return value && value->is_string() && value->text() == expected;
}
bool number(const steam::json::Value& object, const char* key, std::uint64_t expected) {
  const auto* value = object.Find(key);
  return value && value->AsUint64() == expected;
}
}
bool ValidateBootstrapProbeSource(std::string_view owner, std::string_view descriptor,
                                  std::string_view param) {
  const steam::json::Limits limits{65536, 8, 256, 4096};
  const auto a = steam::json::Parse(owner, limits);
  const auto b = steam::json::Parse(descriptor, limits);
  const auto c = steam::json::Parse(param, limits);
  if (!a || !b || !c || !a->is_object() || !b->is_object() || !c->is_object()) return false;
  return number(*a, "schema", 1) &&
      text(*a, "install_id", "acceptance-hl2-fresh-v16") &&
      text(*a, "title_id", "FAKE78001") &&
      text(*a, "purpose", "isolated bootstrap acceptance") &&
      number(*b, "schema_version", 1) && number(*b, "app_id", 220) &&
      text(*b, "install_id", "acceptance-hl2-fresh-v16") &&
      text(*b, "title_id", "FAKE78001") &&
      text(*b, "data_root", "/data/playden/games/acceptance-hl2-fresh-v16") &&
      text(*b, "default_profile_id", "game") &&
      text(*c, "titleId", "FAKE78001") &&
      text(*c, "contentId", "UP9000-FAKE78001_00-PROSPEROWIN00001");
}
}
