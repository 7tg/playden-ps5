#pragma once

#include "runtime/prospero_profile.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace playden::runtime {

// Per-game settings: the catalogue the settings sheet is drawn from, the recipe and preset
// data that supply defaults, and the effective values that become the prospero profile and
// dxvk.conf. See docs/engineering/game-settings-roadmap.md.
//
// Resolution order for each setting:
//   Playden default -> recipe default -> preset -> player override,
// except that a setting the recipe locks always keeps the recipe value. An absent override
// means "inherit"; an explicit value, including "off", is never treated as absent.

enum class SettingTier { kBasic, kCompatibility, kAdvanced };
enum class ApplyMode { kNextLaunch, kGameClosed, kPreparesBottle };
enum class SettingKind { kChoice, kText };
enum class ValueSource { kPlayden, kRecipe, kPreset, kUser };

// What the runtime kit can apply beyond the prospero profile.
struct KitCapabilities {
  bool dxvk_config = false;   // dxvk.conf in the game folder is read (roadmap step S0)
  bool dxvk_present = false;  // the display honors DXVK's present interval (VSync off)
  bool dxvk_unverified = false;  // frame latency and reported VRAM, not yet checked on a console
  bool environment = false;  // prospero-win [environment] section (upstream U1)
  bool registry = false;     // stopped-bottle registry overlays (upstream U2)
};

struct SettingChoice {
  std::string value;
  std::string label;
  std::string explanation;
};

struct SettingDefinition {
  std::string id;
  std::string label;
  std::string effect;       // one-line description under the name
  std::string also_called;  // technical names: profile keys, DXVK options
  SettingTier tier = SettingTier::kBasic;
  int phase = 1;            // roadmap phase that ships it
  ApplyMode apply = ApplyMode::kNextLaunch;
  SettingKind kind = SettingKind::kChoice;
  std::vector<SettingChoice> choices;  // empty for text and for launch_option (from the recipe)
  std::string playden_default;         // empty means "not written"
};

const std::vector<SettingDefinition>& SettingsCatalogue();
const SettingDefinition* FindSetting(std::string_view id);

// The 17 controller buttons prospero-win binds, as setting ids "button.<name>".
const std::vector<std::string>& ProsperoButtons();

struct LaunchEntry {
  std::string id;
  std::string label;
  std::string executable;         // relative to the install root, '/' or '\' separated
  std::string working_directory;  // relative to the install root; empty = executable's folder
};

struct GameRecipe {
  std::string id;
  int version = 0;
  std::uint32_t app_id = 0;
  std::string name;
  std::vector<LaunchEntry> launch_entries;  // the first is the default
  // Arguments the game needs. "{resolution}" is replaced by resolution_arguments with
  // {w} and {h} filled from the Resolution setting. Player arguments are appended after.
  std::string arguments;
  std::string resolution_arguments;
  std::string dll_overrides;  // always written first; the player can only add entries
  std::string architecture = "pe32";
  std::string runtime = "wine-wow64";
  std::string prefix = "game";
  std::string graphics = "auto";
  std::map<std::string, std::string> defaults;  // setting id -> value
  std::set<std::string> locked;                 // setting ids the player cannot change
};

struct SettingsPreset {
  std::string id;
  std::string name;
  std::string try_when;
  std::string short_hint;
  std::map<std::string, std::string> settings;  // only the settings it changes
};

// What Playden persists per game.
struct GameSettingsRecord {
  std::string preset_id;  // empty = Playden default
  std::map<std::string, std::string> overrides;
  std::uint32_t revision = 0;
};

inline bool operator==(const GameSettingsRecord& left, const GameSettingsRecord& right) {
  return left.preset_id == right.preset_id && left.overrides == right.overrides && left.revision == right.revision;
}
inline bool operator!=(const GameSettingsRecord& left, const GameSettingsRecord& right) { return !(left == right); }

struct EffectiveValue {
  std::string value;  // empty = not written
  ValueSource source = ValueSource::kPlayden;
  bool locked = false;
  bool available = true;   // false: the row is disabled and the value is not applied
  std::string reason;      // why it is unavailable or locked
};

struct EffectiveSettings {
  std::map<std::string, EffectiveValue> values;
  const LaunchEntry* launch = nullptr;  // points into the recipe
  std::string arguments;                // full argument string written to the profile
  std::string dll_overrides;
  std::string graphics;
};

struct SettingsError {
  std::string setting_id;
  std::string message;
};

bool BuildEffectiveSettings(const GameRecipe& recipe, const SettingsPreset* preset,
                            const std::map<std::string, std::string>& overrides, const KitCapabilities& kit,
                            EffectiveSettings* settings, SettingsError* error);

// Checks one player value for one setting; used by the UI before saving an override.
bool ValidateSettingValue(const GameRecipe& recipe, std::string_view id, std::string_view value, std::string* reason);

struct ProfileTarget {
  std::string id;            // prospero profile id, also the file stem
  std::string install_root;  // console path of the game files
};

// The full prospero profile: [application], [display], [input] and [debug].
bool RenderProsperoProfile(const GameRecipe& recipe, const EffectiveSettings& settings, const ProfileTarget& target,
                           ProsperoProfile* profile, std::string* reason);

// dxvk.conf for the game folder; empty when no DXVK option differs from DXVK's default.
std::string RenderDxvkConfig(const EffectiveSettings& settings);

// Writes what the next launch reads: <prospero_root>/profiles/<id>.profile (listed in
// profiles.lst) and <install_root>/dxvk.conf. An empty config removes Playden's dxvk.conf.
// A dxvk.conf Playden did not write (no Playden header) is never replaced or removed; the
// call fails with a reason instead. Call only while the game is not running.
bool ApplyGameSettings(const GameRecipe& recipe, const EffectiveSettings& settings, const ProfileTarget& target,
                       const std::string& prospero_root, std::string* reason);

// JSON data files: runtime/recipes/<id>.json and runtime/presets.json.
bool ParseGameRecipe(std::string_view json, GameRecipe* recipe, std::string* reason);
bool ParseSettingsPresets(std::string_view json, std::vector<SettingsPreset>* presets, std::string* reason);
bool ParseGameSettingsRecord(std::string_view json, GameSettingsRecord* record, std::string* reason);
std::string SerializeGameSettingsRecord(const GameSettingsRecord& record);

// Per-game records in one small JSON file ({"version":1,"games":{"<app id>":record}}).
// A missing file loads as no records. Saving replaces the file atomically.
bool LoadGameSettingsFile(const std::string& path, std::map<std::uint32_t, GameSettingsRecord>* records,
                          std::string* reason);
bool SaveGameSettingsFile(const std::string& path, const std::map<std::uint32_t, GameSettingsRecord>& records,
                          std::string* reason);

// Loads <runtime_dir>/presets.json and the recipes listed in <runtime_dir>/recipes/recipes.lst.
// The console title cannot list folders, so recipes are found through the list file.
bool LoadSettingsData(const std::string& runtime_dir, std::vector<SettingsPreset>* presets,
                      std::vector<GameRecipe>* recipes, std::string* reason);

}  // namespace playden::runtime
