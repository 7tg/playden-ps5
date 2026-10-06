#include "runtime/kit_trust.h"

namespace playden::runtime {

KitManifestTrustedConfig DevelopmentKitTrust(const std::string& current_firmware) {
  KitManifestTrustedConfig trust;
  trust.manifest_sha256 = "b54c4b44592e25f70e0c525cd8af755ef6c0b88d3d8e210872e1a01679168cee";
  trust.current_firmware = current_firmware;
  trust.sources = {
      {"wrapper", {{"revision", "9f314314ae6973c12f47c99b546d13d6ec002598"},
                   {"baseline_revision", "54caac176e86c60c7c407972583b491f67838ed9"}}},
      {"wine", {{"revision", "e16ec8f3a19dec4a1162e7554ab73ce41f83bdb9"}}},
      {"helper", {{"revision", "e9f2cfc2c3913e42fdf83d74291e270b041ff0d9"},
                  {"base_revision", "611fa5f7ed46cdde7094b764168390ab5c4e89f8"}}},
      {"foundation", {{"revision", "9c0b994a048521af6fb84c73ded364504fe250e9"}}},
      {"sdk", {{"version", "0.42"}}},
      {"dxvk", {{"version", "2.6.2"},
                {"archive_sha256", "17761876556afd55736cb895d184f5a1c55d43350f1b1e3b129f8d28706d7992"}}}};
  trust.title_pins["FAKE78001"] = {"5608915201204dfcd1846b01dd2a9e60d03aa390c69b1dc61f3b150566bf1935",
                                   "bb02c4aa814eaba7a7a423a31b29ff41f786212c2953678cf29434e85fa0f869"};
  trust.title_pins["FAKE78002"] = {"e2449875f8b22a8c595a0d40c02a57c706e0661054986a4d96076c763deaafb2",
                                   "bb02c4aa814eaba7a7a423a31b29ff41f786212c2953678cf29434e85fa0f869"};
  // The publication guard capability stays untrusted until native qualification.
  trust.allowed_capabilities = {"dxvk", "offline_bottle", "per_game_title", "wow64_i386"};
  return trust;
}

}  // namespace playden::runtime
