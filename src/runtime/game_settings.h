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
  bool environment = false;  // prospero-win [environment] section (upstream U1, not released)
  bool registry = false;     // Playden can edit the stopped bottle's user.reg (decision D2)
  bool input_tuning = false;  // prospero-win [input] stick_deadzone, rumble, trigger_deadzone (U4, not released)
  bool input_wheel = false;   // prospero-win [input] wheel, with Wine patch 0745 (U4, not built yet)
  bool audio_volume = false;  // prospero-win [audio] volume (U4, not released)
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

// Whether the sheet lists a setting at all: rows from a later roadmap phase appear only
// once the kit can apply them (rows gated by the game, such as graphics mode, stay listed
// and disabled with their reason). Button rows live under Button mapping.
bool SettingShown(const SettingDefinition& definition, const KitCapabilities& kit);

// The 17 controller buttons prospero-win binds, as setting ids "button.<name>".
const std::vector<std::string>& ProsperoButtons();

struct LaunchEntry {
  std::string id;
  std::string label;
  std::string executable;         // relative to the install root, '/' or '\' separated
  std::string working_directory;  // relative to the install root; empty = executable's folder
  std::string arguments;          // this entry's own arguments, after the recipe's
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
  std::string detected;  // generated recipes: what the game's files showed (GameDefaults::summary)
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
  std::uint64_t revision = 0;
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
// The recipe as JSON that ParseGameRecipe reads back.
std::string SerializeGameRecipe(const GameRecipe& recipe);

// Games without a shipped recipe get one generated from Steam's Windows launch options.
// Its id is GeneratedRecipeId(app_id); it starts the first launch entry with the game's own
// arguments, takes graphics and engine arguments from the game's files (game_probe.h) and
// locks nothing. With graphics "dxvk", BuildEffectiveSettings adds kDxvkOverrides.
std::string GeneratedRecipeId(std::uint32_t app_id);
bool IsGeneratedRecipeId(std::string_view id);
// Builds that recipe from Steam's launch options (default first). Entries whose executable is
// not a game-relative .exe are skipped. The architecture comes from the first usable entry's
// PE header under install_root; entries built for the other architecture are skipped, and so
// are programs the console cannot run (SteamStub, .NET, Direct3D 12 only). An Unreal root
// program is replaced by its *-Shipping.exe. On failure, reason is a short sentence for the
// player.
bool GenerateGameRecipe(std::uint32_t app_id, const std::string& name, const std::vector<LaunchEntry>& launches,
                        const std::string& install_root, GameRecipe* recipe, std::string* reason);
std::string SerializeGameSettingsRecord(const GameSettingsRecord& record);

// Deletes the DXVK pipeline cache for the launch entry's executable (<install_root>/
// <exe stem>.dxvk-cache, next to where DXVK writes it from the working directory) and
// nothing else. Missing cache is success. Call only while the game is not running.
bool ClearShaderCache(const LaunchEntry& launch, const std::string& install_root, std::string* reason);

// One value Playden owns in the bottle's user.reg (HKEY_CURRENT_USER). An empty data
// removes the value, so the game inherits Wine's default again.
struct RegistryValue {
  std::string key;   // e.g. Software\Wine\AppDefaults\hl2.exe (single backslashes)
  std::string name;  // e.g. Version
  std::string data;  // Wine's text form: "\"win7\"" or "dword:00000078"; empty removes
};

// The registry values the effective settings ask for: Windows version for the launch
// executable (AppDefaults) and text scale (Wine's LogPixels). Empty when the kit cannot
// edit the bottle.
std::vector<RegistryValue> RenderRegistryValues(const EffectiveSettings& settings);

// Applies values to Wine's user.reg text, keeping every other line. Sections Playden adds
// carry the given Unix time. Returns false for text that is not a Wine registry file.
bool ApplyRegistryValues(std::string* user_reg, const std::vector<RegistryValue>& values, std::int64_t unix_time,
                         std::string* reason);

// Edits <prefix>/user.reg in place: copies it to user.reg.playden-backup first, then replaces
// it atomically. Call only while the bottle is stopped (no wineserver).
bool ApplyRegistrySettings(const EffectiveSettings& settings, const std::string& prefix_dir, std::int64_t unix_time,
                           std::string* reason);

// A prospero-win-profiles sidecar (sidecars/<id>.json, schema_version 1) as a preset for
// the game it describes: "Community · <name>", its newest console check as the hint, and
// only the settings this build knows and accepts. Returns false for other schema versions.
struct CommunityPreset {
  SettingsPreset preset;
  std::uint32_t steam_appid = 0;  // 0 when the sidecar names none
  std::string profile_id;
  std::string rating;             // newest check: works, playable or broken
  std::vector<std::string> requires_prospero_win;
};
bool ParseProfileSidecar(std::string_view json, CommunityPreset* community, std::string* reason);

// Whether the kit's runtime has every newer profile feature the sidecar declares in
// requires.prospero_win: the kit capability names without "settings-" (profile-environment,
// input-tuning, input-wheel, audio-volume; see prospero-win-profiles SIDECARS.md). Other entries
// (pull requests, versions) are not checked.
bool CommunityPresetSupported(const CommunityPreset& community, const KitCapabilities& kit);

// --- Integration contract (roadmap S4, integration Tasks 4-6 and 8) ---

// Capability names a runtime kit manifest may list for settings ("capabilities", sorted).
// The kit trust config should allow exactly these besides its own runtime names.
inline constexpr const char* kKitDxvkConfig = "settings-dxvk-config";        // DXVK reads dxvk.conf
inline constexpr const char* kKitDxvkPresent = "settings-dxvk-present";      // VSync off honored
inline constexpr const char* kKitEnvironment = "settings-profile-environment";  // [environment]
inline constexpr const char* kKitInputTuning = "settings-input-tuning";      // stick/trigger dead zones, rumble
inline constexpr const char* kKitInputWheel = "settings-input-wheel";        // [input] wheel
inline constexpr const char* kKitAudioVolume = "settings-audio-volume";      // [audio] volume
const std::vector<std::string>& SettingsKitCapabilityNames();

// Settings gates from a verified kit's capabilities. bottle_access: the adapter can edit the
// stopped bottle's user.reg (decision D2); it is not a kit property.
KitCapabilities KitCapabilitiesFromManifest(const std::vector<std::string>& capabilities, bool bottle_access);

// Everything one game's settings put on disk for its next launch.
struct SettingsLaunchPlan {
  std::uint32_t app_id = 0;
  std::string recipe_id;
  std::string recipe_version;
  std::string profile_id;
  std::string profile_bytes;             // full prospero profile, checked for the kit's features
  std::uint64_t settings_revision = 0;   // GameSettingsRecord::revision it was built from
  std::string launch_entry_id;
  // Input preset files the profile references ([input] preset), as <name>.input and bytes.
  std::vector<std::pair<std::string, std::string>> input_presets;
  std::string dxvk_config;               // empty: no Playden dxvk.conf (an old one is removed)
  std::vector<RegistryValue> registry;   // empty unless the kit has bottle access
  LaunchEntry launch_entry;             // owned snapshot; recipe reloads cannot change the plan
};

struct SettingsLaunchInput {
  const GameRecipe* recipe = nullptr;
  const SettingsPreset* preset = nullptr;  // exact record preset; empty record accepts null or empty playden-default
  GameSettingsRecord record;
  KitCapabilities kit;
  ProfileTarget target;     // profile id and console path of the game files
  std::string runtime_dir;  // bundled settings data; input presets are read from <runtime_dir>/input
};

// Builds the plan, or false with a reason (invalid override, missing input preset, a profile
// the kit's runtime would refuse). Pure apart from reading the input preset files.
bool BuildSettingsLaunchPlan(const SettingsLaunchInput& input, SettingsLaunchPlan* plan, std::string* reason);

// Writes the plan's game-folder and bottle files: dxvk.conf in the launch working folder
// (never replacing one Playden did not write) and the registry values in <prefix_dir>/user.reg
// (with a backup). If the registry edit fails, dxvk.conf is put back; should that rewrite itself
// fail, the folder keeps the new dxvk.conf and the call still returns false. The profile is not written here: the integration publishes profile_bytes
// through its journaled profile/descriptor publication. Call only while the title is stopped.
// Writes include file/directory durability barriers and preserve file modes. This helper
// is not a cross-directory transaction: the owner must journal apply/publication and
// block readiness on failure/interruption until reconciled. A failed config rollback
// returns a distinct reconciliation diagnostic; false does not promise unchanged files.
bool ApplySettingsFiles(const SettingsLaunchPlan& plan, const std::string& install_root, const std::string& prefix_dir,
                        std::int64_t unix_time, std::string* reason);

// Per-game records in one small JSON file ({"version":1,"games":{"<app id>":record}}).
// A missing file loads as no records. Saving replaces the file atomically.
bool LoadGameSettingsFile(const std::string& path, std::map<std::uint32_t, GameSettingsRecord>* records,
                          std::string* reason);
bool SaveGameSettingsFile(const std::string& path, const std::map<std::uint32_t, GameSettingsRecord>& records,
                          std::string* reason);
// One-time import from the legacy approved state tree into an already-created
// private settings root. The owned legacy file must be 0600/0644; the private
// destination must be absent and is published as 0600. The source is preserved.
bool MigrateLegacyGameSettingsFile(const std::string& legacy_path,
                                   const std::string& private_path,
                                   std::string* reason);

// Loads <runtime_dir>/presets.json and the recipes listed in <runtime_dir>/recipes/recipes.lst.
// The console title cannot list folders, so recipes are found through the list file.
bool LoadSettingsData(const std::string& runtime_dir, std::vector<SettingsPreset>* presets,
                      std::vector<GameRecipe>* recipes, std::string* reason);

}  // namespace playden::runtime
