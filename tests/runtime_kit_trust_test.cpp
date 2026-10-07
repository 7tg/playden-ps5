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

// The kit this build was tested with must agree with the dependency lock, and the
// locally packaged development kit (when present) must be admitted and recognised as it.
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
  const auto config = DevelopmentKitAdmission("13.60");
  CHECK(field("kit_id") == kDevelopmentKitId && field("console_root") == kDevelopmentKitRoot);
  CHECK(field("manifest_sha256") == config.expected_manifest_sha256);
  std::vector<std::string> capabilities;
  for (const auto& item : Get(kit, "capabilities").array()) capabilities.push_back(item.text());
  CHECK(capabilities == config.known_capabilities);

  const std::filesystem::path root = argv[2];
  if (!std::filesystem::exists(root / "manifest.json")) {
    std::puts("expected kit matches the lock; packaged kit absent, admission skipped");
    return 0;
  }
  const auto admitted = VerifyInstalledKit(std::filesystem::canonical(root), config);
  if (admitted.status != KitManifestStatus::kVerified) std::fprintf(stderr, "admission: %s\n", admitted.reason.c_str());
  CHECK(admitted.status == KitManifestStatus::kVerified && admitted.view);
  CHECK(admitted.view->kit_id == kDevelopmentKitId);
  CHECK(admitted.manifest_sha256 == config.expected_manifest_sha256 && admitted.ignored_capabilities.empty());
  std::vector<std::string> variants;
  for (int i = 1; i <= 10; ++i) variants.push_back("hl2-fake780" + std::string(i < 10 ? "0" : "") + std::to_string(i) + "-v1");
  CHECK(admitted.available_variant_ids == variants);
  // Unknown or other firmware is refused.
  CHECK(VerifyInstalledKit(std::filesystem::canonical(root), DevelopmentKitAdmission("")).status != KitManifestStatus::kVerified);
  CHECK(VerifyInstalledKit(std::filesystem::canonical(root), DevelopmentKitAdmission("12.02")).status != KitManifestStatus::kVerified);
  std::puts("development kit admitted and recognised as the expected kit");
  return 0;
}
