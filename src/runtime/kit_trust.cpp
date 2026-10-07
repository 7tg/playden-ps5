#include "runtime/kit_trust.h"

namespace playden::runtime {

KitManifestTrustedConfig DevelopmentKitTrust(const std::string& current_firmware) {
  KitManifestTrustedConfig trust;
  trust.manifest_sha256 = "b730337f29199c25bac33f22af580509704eae291d7c6597a37667800dfc4ad7";
  trust.current_firmware = current_firmware;
  trust.sources = {
      {"wrapper", {{"revision", "231fb4c35a11f8df0586f70fec3bf3cb5cd79dad"},
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
  // FAKE78003-FAKE78010: same e9f2cfc helper procedure, only the title differs (2026-10-07).
  trust.title_pins["FAKE78003"] = {"451a5f223b3f2360f9df5bb7b2cae2df90eb9c2c98223f6784ea82f4fe2606c8",
                                   "bb02c4aa814eaba7a7a423a31b29ff41f786212c2953678cf29434e85fa0f869"};
  trust.title_pins["FAKE78004"] = {"21ae2769aa1e2c77972c6bec0b4b27e62bff824c25f57640e52020efb9e8d02e",
                                   "bb02c4aa814eaba7a7a423a31b29ff41f786212c2953678cf29434e85fa0f869"};
  trust.title_pins["FAKE78005"] = {"19293f8b3795e5b5a5e5bf45452c2ab4e1dc61477aac74f6208accfc777ee4b8",
                                   "bb02c4aa814eaba7a7a423a31b29ff41f786212c2953678cf29434e85fa0f869"};
  trust.title_pins["FAKE78006"] = {"004f67349b8678bc7c6ffb5abc0530b190420a555b604e351e29a6cb4baa21d6",
                                   "bb02c4aa814eaba7a7a423a31b29ff41f786212c2953678cf29434e85fa0f869"};
  trust.title_pins["FAKE78007"] = {"3f464c2ef7c83e5ada6f66b09b236520945fad146b77b81eac82f92537790043",
                                   "bb02c4aa814eaba7a7a423a31b29ff41f786212c2953678cf29434e85fa0f869"};
  trust.title_pins["FAKE78008"] = {"bc4bd449639ac46dc8d2f7595990636512c1f23d896026bb3d5785ac78a35d68",
                                   "bb02c4aa814eaba7a7a423a31b29ff41f786212c2953678cf29434e85fa0f869"};
  trust.title_pins["FAKE78009"] = {"4fd5e80a788901074fb699d4b472470816e5f9cb302bad148442c8eaae77e735",
                                   "bb02c4aa814eaba7a7a423a31b29ff41f786212c2953678cf29434e85fa0f869"};
  trust.title_pins["FAKE78010"] = {"30cb8eddd908e42c682dd49885f7a8d2436043a806ad557c1268da78fe311c3d",
                                   "bb02c4aa814eaba7a7a423a31b29ff41f786212c2953678cf29434e85fa0f869"};
  // The publication guard capability stays untrusted until native qualification.
  trust.allowed_capabilities = {"dxvk", "offline_bottle", "per_game_title", "wow64_i386"};
  return trust;
}

}  // namespace playden::runtime
