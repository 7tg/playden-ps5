#include "runtime/game_settings.h"

#include "homebrew/json.hpp"
#include "steam/backend/app_info.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace playden::runtime {
namespace {

namespace json = homebrew::json;

SettingDefinition Choice(std::string id, std::string label, std::string effect, std::string also_called,
                         SettingTier tier, std::vector<SettingChoice> choices, std::string playden_default,
                         int phase = 1, ApplyMode apply = ApplyMode::kNextLaunch) {
  SettingDefinition definition;
  definition.id = std::move(id);
  definition.label = std::move(label);
  definition.effect = std::move(effect);
  definition.also_called = std::move(also_called);
  definition.tier = tier;
  definition.phase = phase;
  definition.apply = apply;
  definition.choices = std::move(choices);
  definition.playden_default = std::move(playden_default);
  return definition;
}

SettingDefinition Text(std::string id, std::string label, std::string effect, std::string also_called,
                       SettingTier tier) {
  auto definition = Choice(std::move(id), std::move(label), std::move(effect), std::move(also_called), tier, {}, "");
  definition.kind = SettingKind::kText;
  return definition;
}

std::vector<SettingDefinition> BuildCatalogue() {
  using T = SettingTier;
  std::vector<SettingDefinition> list;
  // Settings (T1).
  list.push_back(Choice("launch_option", "Launch option", "Which part of the game starts when you press Play.",
                        "executable", T::kBasic, {}, ""));
  list.push_back(Choice("resolution", "Resolution", "Size of the game's screen. Higher is sharper and slower.",
                        "[display] desktop", T::kBasic,
                        {{"1280x720", "1280×720", "Smoother frame rate in heavy scenes. Softer on a 4K TV."},
                         {"1920x1080", "1920×1080", "Full HD."},
                         {"2560x1440", "2560×1440", "Sharper image. May drop below 60 fps."}},
                        "1920x1080"));
  list.push_back(Choice("controller", "Controller", "How the game sees your DualSense.", "[input] mode", T::kBasic,
                        {{"xinput", "Xbox controller", "For games with controller support."},
                         {"keyboard", "Keyboard and mouse mapping", "Buttons send keys and mouse clicks."}},
                        "xinput"));
  list.push_back(Choice("controller_preset", "Controller preset", "Ready-made button layout for this kind of game.",
                        "[input] preset", T::kBasic,
                        {{"none", "None", "Only the buttons set here."},
                         {"gamepad", "Gamepad", "Keyboard keys on the usual gamepad positions."},
                         {"mouse", "Mouse", "Right stick moves the pointer; Cross and Circle click."}},
                        "none"));
  list.push_back(Choice("performance_overlay", "Performance overlay", "Frame rate in the corner of the screen.",
                        "DXVK HUD · [display] show_fps", T::kBasic,
                        {{"off", "Off", ""}, {"on", "On", "Shows frames per second."}}, "off"));
  list.push_back(Text("extra_arguments", "Extra arguments", "Added after the arguments the recipe needs.",
                      "[application] arguments", T::kBasic));
  list.push_back(Choice("frame_limit", "Frame limit", "Caps the frame rate for steadier motion and less heat.",
                        "dxgi.maxFrameRate · d3d9.maxFrameRate", T::kBasic,
                        {{"off", "Off", "No cap."}, {"30", "30", ""}, {"60", "60", ""}}, "off", 2));
  list.push_back(Choice("vsync", "VSync", "Waits for the TV before showing a frame. Stops tearing, adds some delay.",
                        "dxgi.syncInterval · d3d9.presentInterval", T::kBasic,
                        {{"game", "Game default", "The game's own setting."},
                         {"on", "On", ""},
                         {"off", "Off", ""}},
                        "game", 2));
  // More settings (T2).
  static const std::map<std::string, std::string> button_names = {
      {"cross", "Cross"},   {"circle", "Circle"}, {"square", "Square"},   {"triangle", "Triangle"},
      {"l1", "L1"},         {"r1", "R1"},         {"l2", "L2"},           {"r2", "R2"},
      {"l3", "L3"},         {"r3", "R3"},         {"up", "D-pad up"},     {"down", "D-pad down"},
      {"left", "D-pad left"}, {"right", "D-pad right"}, {"options", "Options"}, {"create", "Create"},
      {"touchpad", "Touchpad"}};
  for (const auto& button : ProsperoButtons()) {
    auto definition = Text("button." + button, button_names.at(button), "Key or mouse button this button sends.",
                           "[input] " + button, T::kCompatibility);
    list.push_back(std::move(definition));
  }
  list.push_back(Choice("pointer_stick", "Pointer stick", "Move the mouse pointer with a stick, for menus.",
                        "[input] mouse", T::kCompatibility,
                        {{"none", "None", ""}, {"left_stick", "Left stick", ""}, {"right_stick", "Right stick", ""}},
                        "none"));
  list.push_back(Choice("pointer_speed", "Pointer speed", "How fast the pointer moves, in pixels per second.",
                        "[input] mouse_speed", T::kCompatibility,
                        {{"600", "600", ""}, {"900", "900", ""}, {"1200", "1200", ""}, {"1800", "1800", ""},
                         {"2400", "2400", ""}},
                        "1200"));
  list.push_back(Choice("scaling", "Scaling", "How the game's screen fills the TV.", "[display] scaling",
                        T::kCompatibility,
                        {{"fit", "Fit", "Keeps the shape; black bars if needed."},
                         {"integer", "Integer", "Whole-number steps; sharp pixels."},
                         {"stretch", "Stretch", "Fills the screen."}},
                        "fit"));
  list.push_back(Choice("window_view", "Window view", "Show only game windows, or the whole Windows desktop.",
                        "[display] view", T::kCompatibility,
                        {{"window", "Game windows", ""}, {"desktop", "Entire desktop", ""}}, "window"));
  list.push_back(Choice("refresh", "120 Hz output", "Sends 120 frames per second to the TV when it supports it.",
                        "[display] refresh", T::kCompatibility, {{"60", "60 Hz", ""}, {"120", "120 Hz", ""}}, "60"));
  list.push_back(Choice("opengl_thread", "OpenGL worker thread", "Runs OpenGL work on a second thread.",
                        "[display] opengl_thread", T::kCompatibility, {{"off", "Off", ""}, {"on", "On", ""}}, "off"));
  list.push_back(Choice("graphics", "Graphics", "How Direct3D is drawn on the PS5 GPU.", "[application] graphics",
                        T::kCompatibility,
                        {{"auto", "Automatic", ""}, {"gdi", "GDI", "2D only."}, {"dxvk", "DXVK", "Direct3D 9-11 on Vulkan."},
                         {"opengl", "OpenGL", ""}},
                        "auto"));
  list.push_back(Choice("frame_latency", "Frame latency", "How many frames can wait in the queue.",
                        "dxgi.maxFrameLatency · d3d9.maxFrameLatency", T::kCompatibility,
                        {{"game", "Game default", ""}, {"1", "1", "Lowest delay."}, {"2", "2", ""}, {"3", "3", ""}},
                        "game", 2));
  list.push_back(Choice("reported_vram", "Reported video memory", "How much VRAM the game is told it has. Does not add memory.",
                        "dxgi.maxDeviceMemory", T::kCompatibility,
                        {{"game", "DXVK default", ""}, {"2048", "2 GB", ""}, {"4096", "4 GB", ""}}, "game", 2));
  list.push_back(Choice("windows_version", "Windows version", "Which Windows the game is told it runs on.",
                        "winver · registry", T::kCompatibility,
                        {{"win10", "Windows 10", ""}, {"win81", "Windows 8.1", ""}, {"win7", "Windows 7", ""},
                         {"winxp", "Windows XP", ""}},
                        "win10", 3, ApplyMode::kGameClosed));
  list.push_back(Choice("logging", "Logging", "How much detail goes into the game's log file.", "WINEDEBUG",
                        T::kCompatibility,
                        {{"normal", "Normal", "Errors and loaded modules."},
                         {"verbose", "Verbose", "Also warnings and exceptions. Larger logs."}},
                        "normal"));
  // Advanced (T3).
  list.push_back(Text("library_overrides", "Library overrides", "Use the game's own copy of a Windows library.",
                      "WINEDLLOVERRIDES · [application] dll_overrides", T::kAdvanced));
  return list;
}

constexpr const char* kVerboseWinedebug = "err+all,warn+all,fixme-all,+loaddll,+process,+seh";

std::string Backslashes(std::string text) {
  std::replace(text.begin(), text.end(), '/', '\\');
  return text;
}

bool HasControl(std::string_view text) {
  return std::any_of(text.begin(), text.end(), [](char value) {
    const auto byte = static_cast<unsigned char>(value);
    return byte < 0x20 || byte == 0x7f;
  });
}

void ReplaceAll(std::string* text, std::string_view from, std::string_view to) {
  for (auto at = text->find(from); at != std::string::npos; at = text->find(from, at + to.size()))
    text->replace(at, from.size(), to);
}

std::string CollapseSpaces(const std::string& text) {
  std::string out;
  for (const char value : text) {
    if (value == ' ' && (out.empty() || out.back() == ' ')) continue;
    out.push_back(value);
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

const LaunchEntry* FindEntry(const GameRecipe& recipe, std::string_view id) {
  for (const auto& entry : recipe.launch_entries)
    if (entry.id == id) return &entry;
  return nullptr;
}

std::string Unavailable(const SettingDefinition& definition, const std::string& graphics, const KitCapabilities& kit,
                        const std::map<std::string, EffectiveValue>& values) {
  const auto& id = definition.id;
  const bool dxvk = graphics == "dxvk";
  if (id == "frame_limit" || id == "vsync" || id == "frame_latency" || id == "reported_vram") {
    if (!dxvk) return "Only available with DXVK.";
    if (!kit.dxvk_config) return "Needs a runtime that reads dxvk.conf.";
    // Checked on the console (results/20261005-dxvk-conf-probe): the cap works, VSync off
    // does not change the 60 Hz pacing, latency and VRAM have no visible effect yet.
    if (id == "vsync" && !kit.dxvk_present) return "The PS5 display paces frames at 60 Hz; VSync cannot be turned off yet.";
    if ((id == "frame_latency" || id == "reported_vram") && !kit.dxvk_unverified) return "Not checked on the console yet.";
  }
  if (id == "windows_version" && !kit.registry) return "Needs a runtime update.";
  if (id == "performance_overlay" && graphics == "gdi") return "Only available with DXVK or OpenGL.";
  if (id == "scaling" && dxvk) return "Not available with DXVK output yet.";
  if ((id == "refresh" || id == "opengl_thread") && graphics != "opengl") return "Only available with OpenGL.";
  if (id == "pointer_speed") {
    const auto stick = values.find("pointer_stick");
    if (stick == values.end() || stick->second.value == "none") return "Choose a pointer stick first.";
  }
  return {};
}

}  // namespace

const std::vector<std::string>& ProsperoButtons() {
  static const std::vector<std::string> buttons = {"cross", "circle", "square", "triangle", "l1",    "r1",
                                                   "l2",    "r2",     "l3",     "r3",       "up",    "down",
                                                   "left",  "right",  "options", "create",  "touchpad"};
  return buttons;
}

const std::vector<SettingDefinition>& SettingsCatalogue() {
  static const std::vector<SettingDefinition> catalogue = BuildCatalogue();
  return catalogue;
}

const SettingDefinition* FindSetting(std::string_view id) {
  for (const auto& definition : SettingsCatalogue())
    if (definition.id == id) return &definition;
  return nullptr;
}

bool ValidateSettingValue(const GameRecipe& recipe, std::string_view id, std::string_view value, std::string* reason) {
  const auto fail = [&](std::string why) {
    if (reason) *reason = std::move(why);
    return false;
  };
  const auto* definition = FindSetting(id);
  if (!definition) return fail("unknown setting");
  if (HasControl(value)) return fail("contains a control character");
  if (id == "launch_option") return FindEntry(recipe, value) ? true : fail("not a launch option of this game");
  if (definition->kind == SettingKind::kChoice) {
    for (const auto& choice : definition->choices)
      if (choice.value == value) return true;
    return fail("not one of the choices");
  }
  if (id == "extra_arguments") {
    if (value.size() > kProsperoMaxArgumentBytes) return fail("too long");
    if (value.find_first_of("{}") != std::string_view::npos) return fail("braces are not allowed");
    if (!SplitProsperoArguments(value, nullptr)) return fail("unclosed quote or too many words");
    return true;
  }
  if (id == "library_overrides") {
    if (value.empty()) return true;
    return IsProsperoDllOverrides(value) ? true : fail("not a list like name=n;other=n,b");
  }
  if (id.substr(0, 7) == "button.") return IsProsperoBinding(value) ? true : fail("not a key, mouse button or none");
  return fail("unknown setting");
}

bool BuildEffectiveSettings(const GameRecipe& recipe, const SettingsPreset* preset,
                            const std::map<std::string, std::string>& overrides, const KitCapabilities& kit,
                            EffectiveSettings* settings, SettingsError* error) {
  const auto fail = [&](std::string id, std::string message) {
    if (error) *error = {std::move(id), std::move(message)};
    return false;
  };
  if (recipe.launch_entries.empty()) return fail("launch_option", "the recipe has no launch entry");

  EffectiveSettings out;
  for (const auto& definition : SettingsCatalogue()) {
    EffectiveValue value;
    value.value = definition.id == "launch_option" ? recipe.launch_entries.front().id : definition.playden_default;
    const auto layer = [&](const std::map<std::string, std::string>& source, ValueSource kind) {
      const auto found = source.find(definition.id);
      if (found == source.end()) return true;
      std::string reason;
      if (!ValidateSettingValue(recipe, definition.id, found->second, &reason)) return false;
      value.value = found->second;
      value.source = kind;
      return true;
    };
    if (!layer(recipe.defaults, ValueSource::kRecipe)) return fail(definition.id, "recipe default is invalid");
    value.locked = definition.id == "graphics" || recipe.locked.count(definition.id) > 0;
    if (definition.id == "graphics") value.value = recipe.graphics, value.source = ValueSource::kRecipe;
    if (value.locked) {
      value.reason = "Required by recipe";
    } else {
      if (preset && !layer(preset->settings, ValueSource::kPreset))
        return fail(definition.id, "preset value is invalid");
      // A player value that no longer validates (for example a launch entry the recipe
      // dropped) is ignored rather than blocking the launch.
      layer(overrides, ValueSource::kUser);
    }
    out.values[definition.id] = std::move(value);
  }
  out.graphics = out.values["graphics"].value;

  // Availability depends on graphics, kit and other values; unavailable rows fall back to
  // the Playden default so a hidden setting never takes effect.
  for (const auto& definition : SettingsCatalogue()) {
    auto& value = out.values[definition.id];
    const auto reason = Unavailable(definition, out.graphics, kit, out.values);
    if (reason.empty()) continue;
    value.available = false;
    value.reason = reason;
    value.value = definition.playden_default;
  }

  out.launch = FindEntry(recipe, out.values["launch_option"].value);
  if (!out.launch) return fail("launch_option", "launch option not found");

  std::string arguments = recipe.arguments;
  std::string resolution_arguments = recipe.resolution_arguments;
  const auto& resolution = out.values["resolution"].value;
  const auto x = resolution.find('x');
  ReplaceAll(&resolution_arguments, "{w}", resolution.substr(0, x));
  ReplaceAll(&resolution_arguments, "{h}", x == std::string::npos ? "" : resolution.substr(x + 1));
  ReplaceAll(&arguments, "{resolution}", resolution_arguments);
  const auto& extra = out.values["extra_arguments"].value;
  if (!extra.empty()) arguments += " " + extra;
  out.arguments = CollapseSpaces(arguments);
  if (out.arguments.size() > kProsperoMaxArgumentBytes || !SplitProsperoArguments(out.arguments, nullptr))
    return fail("extra_arguments", "arguments are too long or have too many words");

  out.dll_overrides = recipe.dll_overrides;
  const auto& extra_overrides = out.values["library_overrides"].value;
  if (!extra_overrides.empty())
    out.dll_overrides = out.dll_overrides.empty() ? extra_overrides : out.dll_overrides + ";" + extra_overrides;
  if (!out.dll_overrides.empty() && !IsProsperoDllOverrides(out.dll_overrides))
    return fail("library_overrides", "library overrides are too long");

  *settings = std::move(out);
  return true;
}

bool RenderProsperoProfile(const GameRecipe& recipe, const EffectiveSettings& settings, const ProfileTarget& target,
                           ProsperoProfile* profile, std::string* reason) {
  const auto fail = [&](std::string why) {
    if (reason) *reason = std::move(why);
    return false;
  };
  if (!settings.launch) return fail("no launch option");
  const auto root = ProsperoWindowsPath(target.install_root);
  if (!root) return fail("install folder cannot be mapped for Wine");
  const auto executable = steam::RelativeGamePath(settings.launch->executable);
  if (!executable) return fail("launch executable is not a game path");
  std::string working;
  if (settings.launch->working_directory.empty()) {
    const auto slash = executable->rfind('/');
    working = slash == std::string::npos ? std::string() : executable->substr(0, slash);
  } else {
    const auto relative = steam::RelativeGamePath(settings.launch->working_directory, true);
    if (!relative) return fail("launch working folder is not a game path");
    working = *relative;
  }
  const auto value = [&](const char* id) -> const std::string& { return settings.values.at(id).value; };
  const auto on = [&](const char* id) { return value(id) == "on" ? "true" : "false"; };

  std::string name = recipe.name.empty() ? target.id : recipe.name;
  if (name.size() > kProsperoMaxNameBytes) name.resize(kProsperoMaxNameBytes);

  std::string text = "; Generated by Playden from recipe " + recipe.id + " v" + std::to_string(recipe.version) +
                     ". Edit settings in Playden.\n[application]\n";
  text += "id = " + target.id + "\n";
  text += "name = " + name + "\n";
  text += "executable = " + *root + "\\" + Backslashes(*executable) + "\n";
  text += "working_directory = " + (working.empty() ? *root : *root + "\\" + Backslashes(working)) + "\n";
  if (!settings.arguments.empty()) text += "arguments = " + settings.arguments + "\n";
  if (!settings.dll_overrides.empty()) text += "dll_overrides = " + settings.dll_overrides + "\n";
  text += "prefix = " + recipe.prefix + "\n";
  text += "runtime = " + recipe.runtime + "\n";
  text += "architecture = " + recipe.architecture + "\n";
  text += "graphics = " + settings.graphics + "\n";

  text += "\n[display]\n";
  text += "desktop = " + value("resolution") + "\n";
  if (settings.values.at("scaling").available) text += "scaling = " + value("scaling") + "\n";
  text += "view = " + value("window_view") + "\n";
  text += std::string("show_fps = ") + on("performance_overlay") + "\n";
  if (settings.graphics == "opengl") {
    text += "refresh = " + value("refresh") + "\n";
    text += std::string("opengl_thread = ") + on("opengl_thread") + "\n";
  }

  text += "\n[input]\n";
  if (value("controller_preset") != "none") text += "preset = " + value("controller_preset") + "\n";
  text += "mode = " + value("controller") + "\n";
  text += "mouse = " + value("pointer_stick") + "\n";
  if (settings.values.at("pointer_speed").available) text += "mouse_speed = " + value("pointer_speed") + "\n";
  for (const auto& button : ProsperoButtons()) {
    const auto& binding = settings.values.at("button." + button).value;
    if (!binding.empty()) text += button + " = " + binding + "\n";
  }

  if (value("logging") == "verbose") text += std::string("\n[debug]\nwinedebug = ") + kVerboseWinedebug + "\n";

  std::string why;
  if (!CheckProsperoProfile(text, &why)) return fail("generated profile is invalid: " + why);
  *profile = {target.id, std::move(text)};
  return true;
}

constexpr const char* kDxvkHeader = "# Generated by Playden from this game's settings.";

std::string RenderDxvkConfig(const EffectiveSettings& settings) {
  const auto option = [&](const char* id) -> const EffectiveValue& { return settings.values.at(id); };
  std::string text;
  const auto& limit = option("frame_limit");
  if (limit.available && limit.value != "off")
    text += "dxgi.maxFrameRate = " + limit.value + "\nd3d9.maxFrameRate = " + limit.value + "\n";
  const auto& vsync = option("vsync");
  if (vsync.available && vsync.value != "game") {
    const char* interval = vsync.value == "on" ? "1" : "0";
    text += std::string("dxgi.syncInterval = ") + interval + "\nd3d9.presentInterval = " + interval + "\n";
  }
  const auto& latency = option("frame_latency");
  if (latency.available && latency.value != "game")
    text += "dxgi.maxFrameLatency = " + latency.value + "\nd3d9.maxFrameLatency = " + latency.value + "\n";
  const auto& vram = option("reported_vram");
  if (vram.available && vram.value != "game") text += "dxgi.maxDeviceMemory = " + vram.value + "\n";
  if (text.empty()) return text;
  return std::string(kDxvkHeader) + " Changes here are replaced on the next launch.\n" + text;
}

namespace {

bool StringField(const json::Value& object, const char* key, std::string* out, bool required = true) {
  const auto* field = object.get(key);
  if (!field) return !required;
  const auto* text = field->string();
  if (!text) return false;
  *out = *text;
  return true;
}

bool StringMap(const json::Value* value, std::map<std::string, std::string>* out) {
  if (!value) return true;
  const auto* object = value->object();
  if (!object) return false;
  for (const auto& [key, item] : *object) {
    const auto* text = item.string();
    if (!text) return false;
    (*out)[key] = *text;
  }
  return true;
}

}  // namespace

bool ParseGameRecipe(std::string_view text, GameRecipe* recipe, std::string* reason) {
  const auto fail = [&](std::string why) {
    if (reason) *reason = std::move(why);
    return false;
  };
  const auto root = json::parse(text);
  if (!root || !root->object()) return fail("recipe is not a JSON object");
  GameRecipe out;
  const auto* version = root->get("recipe_version");
  const auto* app = root->get("app_id");
  if (!StringField(*root, "recipe_id", &out.id) || !version || !version->number() || !app || !app->number())
    return fail("recipe needs recipe_id, recipe_version and app_id");
  out.version = static_cast<int>(*version->number());
  out.app_id = static_cast<std::uint32_t>(*app->number());
  if (!StringField(*root, "name", &out.name, false) || !StringField(*root, "arguments", &out.arguments, false) ||
      !StringField(*root, "resolution_arguments", &out.resolution_arguments, false) ||
      !StringField(*root, "dll_overrides", &out.dll_overrides, false) ||
      !StringField(*root, "architecture", &out.architecture, false) ||
      !StringField(*root, "runtime", &out.runtime, false) || !StringField(*root, "prefix", &out.prefix, false) ||
      !StringField(*root, "graphics", &out.graphics, false))
    return fail("recipe has a field of the wrong type");
  const auto* entries = root->get("launch_entries");
  if (!entries || !entries->array() || entries->array()->empty()) return fail("recipe needs launch_entries");
  for (const auto& item : *entries->array()) {
    LaunchEntry entry;
    if (!item.object() || !StringField(item, "id", &entry.id) || !StringField(item, "label", &entry.label) ||
        !StringField(item, "executable", &entry.executable) ||
        !StringField(item, "working_directory", &entry.working_directory, false))
      return fail("launch entry needs id, label and executable");
    out.launch_entries.push_back(std::move(entry));
  }
  if (!StringMap(root->get("defaults"), &out.defaults)) return fail("defaults must map setting ids to strings");
  if (const auto* locked = root->get("locked")) {
    if (!locked->array()) return fail("locked must be a list");
    for (const auto& item : *locked->array()) {
      if (!item.string() || !FindSetting(*item.string())) return fail("locked names an unknown setting");
      out.locked.insert(*item.string());
    }
  }
  if (out.dll_overrides.size() && !IsProsperoDllOverrides(out.dll_overrides)) return fail("recipe dll_overrides are invalid");
  if (out.architecture != "pe32" && out.architecture != "pe64") return fail("architecture must be pe32 or pe64");
  for (const auto& [id, value] : out.defaults) {
    std::string why;
    if (!ValidateSettingValue(out, id, value, &why)) return fail("default " + id + ": " + why);
  }
  if (!ValidateSettingValue(out, "graphics", out.graphics, nullptr)) return fail("unknown graphics mode");
  if (HasControl(out.arguments) || HasControl(out.resolution_arguments)) return fail("arguments contain a control character");
  *recipe = std::move(out);
  return true;
}

bool ParseSettingsPresets(std::string_view text, std::vector<SettingsPreset>* presets, std::string* reason) {
  const auto fail = [&](std::string why) {
    if (reason) *reason = std::move(why);
    return false;
  };
  const auto root = json::parse(text);
  const auto* list = root ? root->get("presets") : nullptr;
  if (!list || !list->array()) return fail("presets file needs a presets list");
  std::vector<SettingsPreset> out;
  for (const auto& item : *list->array()) {
    SettingsPreset preset;
    if (!item.object() || !StringField(item, "id", &preset.id) || !StringField(item, "name", &preset.name) ||
        !StringField(item, "try_when", &preset.try_when, false) ||
        !StringField(item, "short_hint", &preset.short_hint, false) || !StringMap(item.get("settings"), &preset.settings))
      return fail("preset needs id, name and string settings");
    for (const auto& [id, value] : preset.settings) {
      // Presets bundle choices and button bindings; launch entries, graphics and free text
      // belong to the recipe or the player.
      const auto* definition = FindSetting(id);
      const bool button = id.compare(0, 7, "button.") == 0;
      if (!definition || id == "launch_option" || id == "graphics" ||
          (definition->kind == SettingKind::kText && !button))
        return fail("preset " + preset.id + " sets unsupported setting " + id);
      std::string why;
      if (!ValidateSettingValue(GameRecipe{}, id, value, &why)) return fail("preset " + preset.id + " " + id + ": " + why);
    }
    out.push_back(std::move(preset));
  }
  *presets = std::move(out);
  return true;
}

bool ParseGameSettingsRecord(std::string_view text, GameSettingsRecord* record, std::string* reason) {
  const auto root = json::parse(text);
  GameSettingsRecord out;
  const auto* revision = root ? root->get("revision") : nullptr;
  if (!root || !root->object() || !StringField(*root, "preset", &out.preset_id, false) ||
      !StringMap(root->get("overrides"), &out.overrides) || (revision && !revision->number())) {
    if (reason) *reason = "settings record is malformed";
    return false;
  }
  if (revision) out.revision = static_cast<std::uint32_t>(*revision->number());
  // Settings this build does not know are dropped so an older record still loads.
  for (auto it = out.overrides.begin(); it != out.overrides.end();)
    it = FindSetting(it->first) ? std::next(it) : out.overrides.erase(it);
  *record = std::move(out);
  return true;
}

std::string SerializeGameSettingsRecord(const GameSettingsRecord& record) {
  std::string text = "{\"preset\":\"" + json::escape(record.preset_id) + "\",\"revision\":" +
                     std::to_string(record.revision) + ",\"overrides\":{";
  bool first = true;
  for (const auto& [id, value] : record.overrides) {
    if (!first) text += ",";
    first = false;
    text += "\"" + json::escape(id) + "\":\"" + json::escape(value) + "\"";
  }
  return text + "}}";
}

namespace {

bool ReadSmallFile(const std::string& path, std::string* text) {
  FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return false;
  text->clear();
  char buffer[4096];
  std::size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0 && text->size() < 256 * 1024) text->append(buffer, read);
  std::fclose(file);
  return true;
}

}  // namespace

bool LoadSettingsData(const std::string& runtime_dir, std::vector<SettingsPreset>* presets,
                      std::vector<GameRecipe>* recipes, std::string* reason) {
  const auto fail = [&](std::string why) {
    if (reason) *reason = std::move(why);
    return false;
  };
  std::string text;
  if (!ReadSmallFile(runtime_dir + "/presets.json", &text)) return fail("cannot read presets.json");
  if (!ParseSettingsPresets(text, presets, reason)) return false;
  recipes->clear();
  if (!ReadSmallFile(runtime_dir + "/recipes/recipes.lst", &text)) return fail("cannot read recipes.lst");
  std::size_t start = 0;
  while (start < text.size()) {
    const auto end = std::min(text.find('\n', start), text.size());
    std::string name = text.substr(start, end - start);
    start = end + 1;
    while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
    if (name.empty() || name.front() == '#') continue;
    if (name.find('/') != std::string::npos || name.find("..") != std::string::npos) return fail("bad recipe name " + name);
    std::string recipe_text;
    GameRecipe recipe;
    std::string why;
    if (!ReadSmallFile(runtime_dir + "/recipes/" + name, &recipe_text)) return fail("cannot read recipe " + name);
    if (!ParseGameRecipe(recipe_text, &recipe, &why)) return fail(name + ": " + why);
    recipes->push_back(std::move(recipe));
  }
  return true;
}

bool ApplyGameSettings(const GameRecipe& recipe, const EffectiveSettings& settings, const ProfileTarget& target,
                       const std::string& prospero_root, std::string* reason) {
  const auto fail = [&](std::string why) {
    if (reason) *reason = std::move(why);
    return false;
  };
  ProsperoProfile profile;
  if (!RenderProsperoProfile(recipe, settings, target, &profile, reason)) return false;

  // Check the game folder before writing anything, so a refusal leaves both files as they were.
  const auto config_path = target.install_root + "/dxvk.conf";
  std::string existing;
  const bool has_config = ReadSmallFile(config_path, &existing);
  if (has_config && existing.compare(0, std::char_traits<char>::length(kDxvkHeader), kDxvkHeader) != 0)
    return fail("the game folder has its own dxvk.conf; Playden will not replace it");

  if (!PublishProsperoProfile(prospero_root, profile, reason)) return false;
  const auto config = RenderDxvkConfig(settings);
  if (config.empty()) {
    if (has_config && std::remove(config_path.c_str()) != 0) return fail("cannot remove dxvk.conf");
    return true;
  }
  if (has_config && existing == config) return true;
  const auto temporary = config_path + ".tmp";
  FILE* file = std::fopen(temporary.c_str(), "wb");
  if (!file) return fail("cannot write dxvk.conf");
  const bool written = std::fwrite(config.data(), 1, config.size(), file) == config.size();
  if (std::fclose(file) != 0 || !written || std::rename(temporary.c_str(), config_path.c_str()) != 0) {
    std::remove(temporary.c_str());
    return fail("cannot write dxvk.conf");
  }
  return true;
}

}  // namespace playden::runtime
