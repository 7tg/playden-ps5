#include "platform/ps5/title_registry.h"
#include "homebrew/json.hpp"
#include <cerrno>
#include <cmath>
#include <limits>

namespace playden::ps5 {
namespace {
namespace json = homebrew::json;
bool Flag(const json::Value& v, const char* key) {
  const auto* value = v.get(key);
  return value && value->boolean().value_or(false);
}
std::string Text(const json::Value& v, const char* key) {
  const auto* value = v.get(key);
  return value && value->string() ? *value->string() : std::string();
}
bool Success(const json::Value& v) {
  const auto* status = v.get("status");
  return status && status->number() == 0;
}
std::string SourceBody(const std::string& title) { return "{\"path\":\"/data/homebrew/" + title + "\"}"; }
}
bool IsPlaydenGameTitle(const std::string& title) {
  return ValidTitleId(title) && title.compare(0, 6, "FAKE78") == 0;
}
TitleRegistration TitleRegistry::Query(const std::string& title) {
  if (!ValidTitleId(title) || !transport_) return {};
  const auto bytes = transport_("/api/v1/games", "{}");
  if (!bytes || bytes->size() > 1024*1024) return {};
  const auto v = json::Parser(*bytes).parse();
  if (!v || !Success(*v)) return {};
  const auto* games = v->get("games");
  if (!games || !games->array()) return {};
  for (const auto& game : *games->array()) {
    if (Text(game, "title_id") != title) continue;
    const auto path = Text(game, "path");
    if (path != "/data/homebrew/" + title || !Flag(game, "managed"))
      return {RegistrationState::kCollision, path};
    return {Flag(game, "installed") && Flag(game, "source_available") && Text(game, "source_type") == "folder"
                ? RegistrationState::kReady : RegistrationState::kPending, path};
  }
  return {RegistrationState::kAbsent, {}};
}
TitleResult TitleRegistry::Post(const std::string& route, const std::string& body) {
  if (!transport_) return {false, ENOSYS, "shadowmount"};
  const auto bytes = transport_(route, body);
  if (!bytes || bytes->size() > 1024*1024) return {false, EIO, "shadowmount"};
  const auto v = json::Parser(*bytes).parse();
  const auto* status = v ? v->get("status") : nullptr;
  const auto code = status ? status->number() : std::nullopt;
  if (!code || *code < 0 || *code > std::numeric_limits<std::uint32_t>::max() || std::floor(*code) != *code)
    return {false, EPROTO, "shadowmount"};
  return {*code == 0, static_cast<std::uint32_t>(*code), "shadowmount"};
}
TitleResult TitleRegistry::Register(const std::string& title) {
  if (!IsPlaydenGameTitle(title)) return {false, EINVAL, "title-id"};
  const auto existing = Query(title);
  if (existing.state == RegistrationState::kUnavailable) return {false, EIO, "query"};
  if (existing.state == RegistrationState::kCollision) return {false, EEXIST, "collision"};
  if (existing.state == RegistrationState::kReady) return {true, 0, "registered"};
  auto result = Post("/api/v1/manual/add", SourceBody(title));
  return result.ok ? Post("/api/v1/scan", "{}") : result;
}
TitleResult TitleRegistry::RemoveManualSource(const std::string& title) {
  if (!IsPlaydenGameTitle(title)) return {false, EINVAL, "title-id"};
  return Post("/api/v1/manual/remove", SourceBody(title));
}
TitleResult TitleRegistry::Unregister(const std::string& title) {
  if (!IsPlaydenGameTitle(title)) return {false, EINVAL, "title-id"};
  return Post("/api/v1/games/uninstall", "{\"title_id\":\"" + title + "\"}");
}
}  // namespace playden::ps5
