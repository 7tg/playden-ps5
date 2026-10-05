#pragma once

#include "steam/backend/app_info.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace playden::runtime {

// prospero-win (PPSA99995) launches a Windows game from a strict INI profile listed in
// profiles/profiles.lst under its data root. These helpers draft that profile for a game
// Playden installed, following the format of prospero-win's src/pw_app_profile.h; limits
// mirror its parser so a drafted profile is never rejected for length.
enum class PeArchitecture { kPe32, kPe64 };

// Reads the machine field of a PE image header (i386 or AMD64 only).
std::optional<PeArchitecture> ParsePeArchitecture(const std::uint8_t* bytes, std::size_t size);
std::optional<PeArchitecture> ReadPeArchitecture(const std::string& path);

struct ProsperoGame {
  std::uint32_t app_id = 0;
  std::string name;
  // Absolute console path of the install, as prospero-win sees it (mapped through Z:).
  std::string install_root;
  steam::AppLaunch launch;
  PeArchitecture architecture = PeArchitecture::kPe32;
  // Shared Wine prefix the game runs in; built on a PC and pushed to prospero-win.
  std::string prefix = "playden";
};

struct ProsperoProfile {
  std::string id;         // also the file name stem: <id>.profile
  std::string text;
};

constexpr std::size_t kProsperoMaxProfileBytes = 8192;
constexpr std::size_t kProsperoMaxIdBytes = 64;
constexpr std::size_t kProsperoMaxNameBytes = 96;
constexpr std::size_t kProsperoMaxPathBytes = 259;
constexpr std::size_t kProsperoMaxArgumentBytes = 512;
constexpr std::size_t kProsperoMaxCatalogEntries = 16;
constexpr std::size_t kProsperoMaxDllOverrideBytes = 256;
constexpr std::size_t kProsperoMaxArgumentWords = 32;
constexpr std::size_t kProsperoMaxWinedebugBytes = 127;

// "/data/x/games/220" -> "Z:\data\x\games\220"; prospero-win maps Z: to the console root.
std::optional<std::string> ProsperoWindowsPath(std::string_view host_path);

// Splits arguments the way prospero-win's launcher does: spaces and tabs separate words,
// double quotes group a word and are removed, there is no escape syntax. Fails on an
// unclosed quote or more than kProsperoMaxArgumentWords words.
bool SplitProsperoArguments(std::string_view arguments, std::vector<std::string>* words);

// A button binding prospero-win accepts: a named key, a letter or digit, F1-F24,
// vk:0xNN, mouse_left/right/middle or none.
bool IsProsperoBinding(std::string_view value);

// Wine DLL override syntax limited to what prospero-win accepts: name[,name]=mode entries
// separated by ';', mode one of n, b, d, n,b, b,n or empty.
bool IsProsperoDllOverrides(std::string_view value);

// Applies the rules of prospero-win's profile parsers (and prospero-win-profiles'
// tools/check_profiles.py): strict sections and keys, value vocabularies and limits.
bool CheckProsperoProfile(std::string_view text, std::string* reason);

std::string ProsperoProfileId(std::uint32_t app_id);

// Returns false with a short reason when the launch cannot be expressed as a profile.
bool DraftProsperoProfile(const ProsperoGame& game, ProsperoProfile* profile, std::string* reason);

// Writes <root>/profiles/<id>.profile and adds it to profiles.lst (at most 16 entries,
// existing entries and comments kept). Returns false with a short reason on failure.
bool PublishProsperoProfile(const std::string& prospero_root, const ProsperoProfile& profile,
                            std::string* reason);

}  // namespace playden::runtime
