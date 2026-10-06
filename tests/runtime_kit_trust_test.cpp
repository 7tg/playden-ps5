#include "runtime/kit_admission.h"
#include "runtime/kit_trust.h"
#include "steam/backend/json.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace playden::runtime;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x); std::exit(1); } } while (0)

using Json = playden::steam::json::Value;
const Json& Get(const Json& value, const std::string& key) {
  for (const auto& [name, child] : value.object()) if (name == key) return child;
  std::fprintf(stderr, "missing key %s\n", key.c_str()); std::exit(1);
}

// The compiled trust anchors must agree with the dependency lock, and the
// locally packaged development kit (when present) must be admitted by them.
int main(int argc, char** argv) {
  CHECK(argc == 3);
  std::ifstream in(argv[1]);
  const std::string text{std::istreambuf_iterator<char>(in), {}};
  playden::steam::json::Limits limits; limits.max_bytes = 1 << 20; limits.max_depth = 16;
  limits.max_values = 1 << 16; limits.max_string_bytes = 4096;
  const auto lock = playden::steam::json::Parse(text, limits);
  CHECK(lock && lock->is_object());
  const auto& kit = Get(*lock, "prospero_kit");
  const auto field = [&](const char* name) { return Get(kit, name).text(); };
  const auto trust = DevelopmentKitTrust("13.60");
  CHECK(field("kit_id") == kDevelopmentKitId && field("console_root") == kDevelopmentKitRoot);
  CHECK(field("manifest_sha256") == trust.manifest_sha256);
  CHECK(field("wrapper_revision") == trust.sources.at("wrapper").at("revision"));
  CHECK(Get(Get(*lock, "prospero_win"), "revision").text() == field("wrapper_revision"));
  const auto& pin = Get(Get(kit, "title_pins"), "FAKE78001");
  CHECK(Get(pin, "helper_sha256").text() == trust.title_pins.at("FAKE78001").helper_sha256);
  CHECK(Get(pin, "protocol_sha256").text() == trust.title_pins.at("FAKE78001").protocol_sha256);
  std::vector<std::string> capabilities;
  for (const auto& item : Get(kit, "capabilities").array()) capabilities.push_back(item.text());
  CHECK(capabilities == trust.allowed_capabilities);

  const std::filesystem::path root = argv[2];
  if (!std::filesystem::exists(root / "manifest.json")) {
    std::puts("kit trust matches the lock; packaged kit absent, admission skipped");
    return 0;
  }
  const auto admitted = VerifyInstalledKit(std::filesystem::canonical(root), trust);
  if (admitted.status != KitManifestStatus::kVerified) std::fprintf(stderr, "admission: %s\n", admitted.reason.c_str());
  CHECK(admitted.status == KitManifestStatus::kVerified && admitted.view);
  CHECK(admitted.view->kit_id == kDevelopmentKitId);
  CHECK(admitted.available_variant_ids == std::vector<std::string>{"hl2-fake78001-v1"});
  // Unknown or other firmware is refused.
  CHECK(VerifyInstalledKit(std::filesystem::canonical(root), DevelopmentKitTrust("")).status != KitManifestStatus::kVerified);
  CHECK(VerifyInstalledKit(std::filesystem::canonical(root), DevelopmentKitTrust("12.02")).status != KitManifestStatus::kVerified);
  std::puts("development kit admitted by its independent trust anchors");
  return 0;
}
