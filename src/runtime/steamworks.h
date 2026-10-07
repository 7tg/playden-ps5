#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace playden::runtime {

// Steamworks emulation for games without a Steam client on the console: the game's
// steam_api.dll / steam_api64.dll is replaced by gbe_fork's (shipped unmodified in
// <runtime_dir>/steamworks, pinned below), the original is kept as <dll>.orig, and
// steam_settings/ beside it tells the emulator who plays and what they own. Every call is
// idempotent and must run while the game is stopped. Links are never followed.
inline constexpr const char* kSteamApi32Sha256 = "4cc9dfcab8b4df0db507fa91d0acec6e58693fd4edd78dc80d39bda8072d08ed";
inline constexpr const char* kSteamApi64Sha256 = "513430ac8b869b0eed258d9e5feba5176a35446b3b61a63d01ab1d87683cdd1b";

// Where the emulator keeps ISteamRemoteStorage files: <local_save_path>/<app id>/remote, which
// is where Playden's Cloud mapping looks (steam/backend/cloud_mapping.h kEmulatorUserData).
inline constexpr const char* kSteamworksSavePath = "C:\\Program Files (x86)\\Steam\\userdata\\0";

// What the download recorded beside the game (SteamworksFactsFile): installed depots and the
// DLC this account owns.
inline constexpr const char* kSteamworksFactsFile = ".playden-steam";  // "depots=1,2\ndlc=3\n"

struct SteamworksIdentity {
  std::uint32_t app_id = 0;
  std::string account_name;  // empty: "Playden"
  std::uint64_t steam_id = 0;
  std::string language = "english";
  std::vector<std::uint32_t> owned_dlc;
  std::vector<std::uint32_t> depots;  // empty: no depots.txt
};

struct SteamworksResult {
  bool ok = false;
  std::string message;               // on failure: a sentence for the player
  std::vector<std::string> changed;  // DLL paths (relative) replaced or restored by this call
  std::vector<std::string> found;    // every steam_api DLL found (relative)
};

// steam_api.dll / steam_api64.dll below install_root (any case, at most 10 folders deep, no
// links, no hidden folders), relative with '/' separators, sorted.
std::vector<std::string> FindSteamApi(const std::string& install_root);

// Swaps every found DLL for the emulator and writes its steam_settings/. A DLL that already is
// the emulator needs its .orig; a DLL that is neither (a newer original from an update)
// becomes the new .orig. No steam_api DLL: ok with nothing found.
SteamworksResult EmulateSteamworks(const std::string& install_root, const std::string& emulator_dir,
                                   const SteamworksIdentity& identity);

// Puts each .orig back over a swapped DLL; .orig and steam_settings/ stay.
SteamworksResult RestoreSteamworks(const std::string& install_root);

// Interface versions a steam_api DLL asks for, as gbe_fork's generate_interfaces finds them.
std::vector<std::string> SteamInterfaces(std::string_view dll_bytes);

// The facts file the download writes, and its reader (missing or unreadable: false).
std::string SerializeSteamworksFacts(const std::vector<std::uint32_t>& depots, const std::vector<std::uint32_t>& owned_dlc);
bool WriteSteamworksFacts(const std::string& install_root, const std::vector<std::uint32_t>& depots,
                          const std::vector<std::uint32_t>& owned_dlc);
bool ReadSteamworksFacts(const std::string& install_root, SteamworksIdentity* identity);

}  // namespace playden::runtime
