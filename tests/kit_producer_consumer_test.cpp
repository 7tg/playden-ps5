#include "runtime/artifact_hash.h"
#include "runtime/kit_admission.h"
#include <cstdio>

// Fixture-only trust anchors independent of the producer's emitted manifest.
// These identify synthetic test bytes and must never enter the production lock.
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  using namespace playden::runtime;
  KitManifestTrustedConfig trust;
  trust.current_firmware = "13.60";
  trust.manifest_sha256 = argv[2];
  const std::string wrapper = "54caac176e86c60c7c407972583b491f67838ed9";
  trust.sources = {
      {"wrapper", {{"revision", wrapper}, {"baseline_revision", wrapper}}},
      {"wine", {{"revision", "e16ec8f3a19dec4a1162e7554ab73ce41f83bdb9"}}},
      {"helper", {{"revision", "e9f2cfc2c3913e42fdf83d74291e270b041ff0d9"},
                  {"base_revision", "611fa5f7ed46cdde7094b764168390ab5c4e89f8"}}},
      {"foundation", {{"revision", "9c0b994a048521af6fb84c73ded364504fe250e9"}}},
      {"sdk", {{"version", "0.42"}}},
      {"dxvk", {{"version", "2.6.2"},
                {"archive_sha256", "17761876556afd55736cb895d184f5a1c55d43350f1b1e3b129f8d28706d7992"}}}};
  trust.allowed_capabilities = {"per_game_title", "offline_bottle", "wow64_i386", "dxvk"};
  const auto helper = HashArtifactBytes(std::string("\x7f" "ELFhelper"));
  const auto protocol = HashArtifactBytes("protocol-v1");
  if (!helper || !protocol) return 2;
  trust.title_pins["FAKE78001"] = {*helper, *protocol};
  const auto result = VerifyInstalledKit(argv[1], trust);
  if (result.status != KitManifestStatus::kVerified) {
    std::fprintf(stderr, "admission rejected: %s\n", result.reason.c_str());
    return 1;
  }
  if (!result.view || result.available_variant_ids.size() != 1 ||
      result.available_variant_ids.front() != "hl2-fa78001-v1") return 2;
  std::puts("verified emitted kit manifest with production SHA-256 admission");
  return 0;
}
