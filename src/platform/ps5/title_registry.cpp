#include "platform/ps5/title_registry.h"
#include "steam/backend/json.h"
#include <cerrno>
#include <limits>

namespace playden::ps5 {
namespace {
namespace json = steam::json;
std::optional<bool> Flag(const json::Value& v, const char* key) {
  const auto* value = v.Find(key);
  return value && value->is_bool() ? std::optional<bool>(value->boolean()) : std::nullopt;
}
std::string Text(const json::Value& v, const char* key) {
  const auto* value = v.Find(key);
  return value && value->is_string() ? value->text() : std::string();
}
bool Success(const json::Value& v) {
  const auto* status = v.Find("status");
  return status && status->AsUint64() == 0;
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
  const auto v = json::Parse(*bytes, {1024*1024, 16, 65536, 4096});
  if (!v || !Success(*v)) return {};
  const auto* games = v->Find("games");
  if (!games || !games->is_array()) return {};
  TitleRegistration result{RegistrationState::kAbsent, {}};
  unsigned matches = 0;
  for (const auto& game : games->array()) {
    const auto id = Text(game, "title_id");
    if (!ValidTitleId(id)) return {}; // Incomplete inventory cannot imply absence.
    if (id != title) continue;
    const auto path = Text(game, "path");
    if (++matches > 1) return {RegistrationState::kCollision, path};
    if (path.empty()) return {};
    const auto managed = Flag(game, "managed");
    if (!managed) return {};
    if (path != "/data/homebrew/" + title || !*managed)
      result = {RegistrationState::kCollision, path};
    else {
      const auto installed = Flag(game, "installed"), available = Flag(game, "source_available");
      const auto type = Text(game, "source_type");
      if (!installed || !available || type.empty()) return {};
      result = {*installed && *available && type == "folder" ? RegistrationState::kReady : RegistrationState::kPending, path};
    }
  }
  return result;
}

TitleResult TitleRegistry::Post(const std::string& route, const std::string& body) {
  if (!transport_) return {false, ENOSYS, "shadowmount"};
  const auto bytes = transport_(route, body);
  if (!bytes || bytes->size() > 1024*1024) return {false, EIO, "shadowmount"};
  const auto v = json::Parse(*bytes, {1024*1024, 16, 65536, 4096});
  const auto* status = v ? v->Find("status") : nullptr;
  const auto code = status ? status->AsUint64() : std::nullopt;
  if (!code || *code > std::numeric_limits<std::uint32_t>::max())
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
