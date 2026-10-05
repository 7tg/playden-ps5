#include "runtime/game_settings.h"

#include "homebrew/json.hpp"
#include "steam/backend/app_info.h"
#include "steam/backend/json.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <limits>
#include <optional>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace playden::runtime {
namespace {

namespace json = homebrew::json;
namespace storage_json = steam::json;

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
  list.push_back(Choice("volume", "Volume", "How loud this game plays, without changing the console's volume.",
                        "[audio] volume", T::kBasic,
                        {{"100", "100%", ""}, {"75", "75%", ""}, {"50", "50%", ""}, {"25", "25%", ""},
                         {"0", "Muted", ""}},
                        "100", 4));
  list.push_back(Choice("performance_overlay", "Performance overlay", "Frame rate in the corner of the screen.",
                        "DXVK HUD · [display] show_fps", T::kBasic,
                        {{"off", "Off", ""}, {"on", "On", "Shows frames per second."}}, "off"));
  list.push_back(Choice("overlay_detail", "Overlay detail", "What the performance overlay shows.", "dxvk.hud",
                        T::kBasic,
                        {{"fps", "Frame rate", ""},
                         {"frametimes", "Frame rate and frame times", "Adds a graph of how long each frame took."},
                         {"full", "Everything", "Also memory, GPU load and the DXVK version."}},
                        "fps", 2));
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
  list.push_back(Choice("stick_deadzone", "Stick dead zone",
                        "How far a stick moves before the game sees it. Raise it if the camera drifts.",
                        "[input] stick_deadzone", T::kCompatibility,
                        {{"0", "Off", "The game's own dead zone only."}, {"5", "5%", ""}, {"10", "10%", ""},
                         {"15", "15%", ""}, {"20", "20%", ""}, {"30", "30%", "For a badly drifting stick."}},
                        "0", 4));
  list.push_back(Choice("rumble", "Rumble", "How strongly the controller vibrates.", "[input] rumble",
                        T::kCompatibility,
                        {{"100", "Full", ""}, {"75", "75%", ""}, {"50", "50%", ""}, {"25", "25%", ""},
                         {"0", "Off", ""}},
                        "100", 4));
  list.push_back(Choice("trigger_deadzone", "Trigger dead zone",
                        "How far L2 and R2 travel before the game sees them. Raise it for worn triggers.",
                        "[input] trigger_deadzone", T::kCompatibility,
                        {{"0", "Off", ""}, {"5", "5%", ""}, {"10", "10%", ""}, {"20", "20%", ""}, {"30", "30%", ""}},
                        "0", 4));
  list.push_back(Choice("wheel_stick", "Mouse wheel stick", "Scroll the mouse wheel by pushing a stick up or down.",
                        "[input] wheel", T::kCompatibility,
                        {{"none", "None", ""}, {"left_stick", "Left stick", ""}, {"right_stick", "Right stick", ""}},
                        "none", 4));
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
                        "dxgi.maxDeviceMemory · d3d9.maxAvailableMemory", T::kCompatibility,
                        {{"game", "DXVK default", ""}, {"2048", "2 GB", ""}, {"4096", "4 GB", ""}}, "game", 2));
  list.push_back(Choice("windows_version", "Windows version", "Which Windows the game is told it runs on.",
                        "winver · registry", T::kCompatibility,
                        {{"win10", "Windows 10", ""}, {"win81", "Windows 8.1", ""}, {"win7", "Windows 7", ""},
                         {"winxp", "Windows XP", ""}},
                        "win10", 3, ApplyMode::kGameClosed));
  list.push_back(Choice("text_scale", "Text scale", "Size of Windows text and dialogs. Shared by every game in the same bottle.",
                        "LogPixels · DPI", T::kCompatibility,
                        {{"96", "100%", ""}, {"120", "125%", ""}, {"144", "150%", ""}}, "96", 3, ApplyMode::kGameClosed));
  list.push_back(Choice("logging", "Logging", "How much detail goes into the game's log file.", "WINEDEBUG",
                        T::kCompatibility,
                        {{"normal", "Normal", "Errors and loaded modules."},
                         {"verbose", "Verbose", "Also warnings and exceptions. Larger logs."}},
                        "normal"));
  // Advanced (T3).
  {
    auto environment = Text("environment", "Environment variables",
                            "Extra variables for the game, separated by ';', like WINE_CPU_TOPOLOGY=4:0,1,2,3.",
                            "[environment]", T::kAdvanced);
    environment.phase = 2;
    list.push_back(std::move(environment));
  }
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
  bool quoted = false;
  for (const char value : text) {
    if (value == ' ' && !quoted && (out.empty() || out.back() == ' ')) continue;
    out.push_back(value);
    if (value == '"') quoted = !quoted;
  }
  while (!out.empty() && out.back() == ' ' && !quoted) out.pop_back();
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
    // Checked on the console (results/20261005-dxvk-conf-probe): the cap works, DXVK applies
    // latency and VRAM, VSync off does not change the 60 Hz pacing.
    if (id == "vsync" && !kit.dxvk_present) return "The PS5 display paces frames at 60 Hz; VSync cannot be turned off yet.";
  }
  if ((id == "windows_version" || id == "text_scale") && !kit.registry)
    return "Needs access to the game's bottle, which comes with the Play button.";
  if (id == "environment" && !kit.environment) return "Needs a prospero-win update that reads [environment].";
  if (id == "wheel_stick" && !kit.input_wheel) return "Needs a prospero-win update that turns the mouse wheel.";
  if (id == "volume" && !kit.audio_volume) return "Needs a prospero-win update with per-game volume.";
  if (id == "stick_deadzone" || id == "rumble" || id == "trigger_deadzone") {
    if (!kit.input_tuning) return "Needs a prospero-win update with controller tuning.";
    const auto controller = values.find("controller");
    if (controller == values.end() || controller->second.value != "xinput") return "Only with the Xbox controller.";
  }
  if (id == "performance_overlay" && graphics == "gdi") return "Only available with DXVK or OpenGL.";
  if (id == "overlay_detail") {
    if (!dxvk) return "Only available with DXVK.";
    if (!kit.dxvk_config) return "Needs a runtime that reads dxvk.conf.";
    const auto overlay = values.find("performance_overlay");
    if (overlay == values.end() || overlay->second.value != "on") return "Turn the performance overlay on first.";
  }
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

bool SettingShown(const SettingDefinition& definition, const KitCapabilities& kit) {
  if (definition.id.rfind("button.", 0) == 0) return false;
  if (definition.id == "environment") return kit.environment;
  if (definition.phase == 2) return kit.dxvk_config;
  if (definition.phase == 3) return kit.registry;
  if (definition.phase == 4) {
    if (definition.id == "wheel_stick") return kit.input_wheel;
    if (definition.id == "volume") return kit.audio_volume;
    return kit.input_tuning;
  }
  return true;
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
  if (id == "environment") {
    if (value.empty()) return true;
    std::size_t count = 0;
    std::set<std::string> names;
    std::size_t start = 0;
    while (start <= value.size()) {
      const auto end = std::min(value.find(';', start), value.size());
      auto entry = value.substr(start, end - start);
      start = end + 1;
      while (!entry.empty() && entry.front() == ' ') entry.remove_prefix(1);
      while (!entry.empty() && entry.back() == ' ') entry.remove_suffix(1);
      if (entry.empty()) continue;
      const auto equals = entry.find('=');
      if (equals == std::string_view::npos) return fail("each entry needs NAME=value");
      auto name = entry.substr(0, equals);
      auto item = entry.substr(equals + 1);
      while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
      while (!item.empty() && item.front() == ' ') item.remove_prefix(1);
      if (!IsProsperoEnvironmentEntry(name, item)) return fail(std::string(name) + " is not an allowed variable");
      if (!names.insert(std::string(name)).second) return fail(std::string(name) + " appears twice");
      if (++count > kProsperoMaxEnvironmentEntries) return fail("at most 8 variables");
    }
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
  // With a kit that reads dxvk.conf, a DXVK game's overlay is dxvk.hud there; the title's
  // own DXVK_HUD=fps would override it, so show_fps stays false.
  const bool hud_in_config = settings.values.at("overlay_detail").available;
  text += std::string("show_fps = ") + (hud_in_config ? "false" : on("performance_overlay")) + "\n";
  if (settings.graphics == "opengl") {
    text += "refresh = " + value("refresh") + "\n";
    text += std::string("opengl_thread = ") + on("opengl_thread") + "\n";
  }

  text += "\n[input]\n";
  if (value("controller_preset") != "none") text += "preset = " + value("controller_preset") + "\n";
  text += "mode = " + value("controller") + "\n";
  text += "mouse = " + value("pointer_stick") + "\n";
  if (settings.values.at("pointer_speed").available) text += "mouse_speed = " + value("pointer_speed") + "\n";
  // Written only when they differ from the runtime's defaults (no dead zone, full rumble).
  const bool tuned_deadzone = settings.values.at("stick_deadzone").available && value("stick_deadzone") != "0";
  const bool tuned_rumble = settings.values.at("rumble").available && value("rumble") != "100";
  const bool tuned_triggers = settings.values.at("trigger_deadzone").available && value("trigger_deadzone") != "0";
  const bool wheel = settings.values.at("wheel_stick").available && value("wheel_stick") != "none";
  if (tuned_deadzone) text += "stick_deadzone = " + value("stick_deadzone") + "\n";
  if (tuned_rumble) text += "rumble = " + value("rumble") + "\n";
  if (tuned_triggers) text += "trigger_deadzone = " + value("trigger_deadzone") + "\n";
  if (wheel) text += "wheel = " + value("wheel_stick") + "\n";
  for (const auto& button : ProsperoButtons()) {
    const auto& binding = settings.values.at("button." + button).value;
    if (!binding.empty()) text += button + " = " + binding + "\n";
  }

  const bool audio_section = settings.values.at("volume").available && value("volume") != "100";
  if (audio_section) text += "\n[audio]\nvolume = " + value("volume") + "\n";

  if (value("logging") == "verbose") text += std::string("\n[debug]\nwinedebug = ") + kVerboseWinedebug + "\n";

  const auto& environment = settings.values.at("environment");
  const bool environment_section = environment.available && !environment.value.empty();
  if (environment_section) {
    text += "\n[environment]\n";
    std::size_t start = 0;
    const auto& entries = environment.value;
    while (start <= entries.size()) {
      const auto end = std::min(entries.find(';', start), entries.size());
      std::string entry = entries.substr(start, end - start);
      start = end + 1;
      const auto equals = entry.find('=');
      if (equals == std::string::npos) continue;
      std::string name = entry.substr(0, equals), item = entry.substr(equals + 1);
      while (!name.empty() && name.front() == ' ') name.erase(0, 1);
      while (!name.empty() && name.back() == ' ') name.pop_back();
      while (!item.empty() && item.front() == ' ') item.erase(0, 1);
      while (!item.empty() && item.back() == ' ') item.pop_back();
      text += name + " = " + item + "\n";
    }
  }

  std::string why;
  ProsperoProfileFeatures features;
  features.environment_section = environment_section;
  features.input_tuning = tuned_deadzone || tuned_rumble || tuned_triggers;
  features.input_wheel = wheel;
  features.audio_section = audio_section;
  if (!CheckProsperoProfile(text, &why, features)) return fail("generated profile is invalid: " + why);
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
  const auto& detail = option("overlay_detail");
  if (detail.available) {
    const char* elements = detail.value == "full" ? "fps,frametimes,memory,gpuload,version"
                           : detail.value == "frametimes" ? "fps,frametimes" : "fps";
    text += std::string("dxvk.hud = ") + elements + "\n";
  }
  const auto& vram = option("reported_vram");
  if (vram.available && vram.value != "game")
    text += "dxgi.maxDeviceMemory = " + vram.value + "\nd3d9.maxAvailableMemory = " + vram.value + "\n";
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

bool ExactUnsigned(const json::Value* value, std::uint64_t maximum, std::uint64_t* out, bool allow_zero = true) {
  const auto number = value ? value->number() : std::optional<double>{};
  if (!number || !std::isfinite(*number) || *number < 0 || (!allow_zero && *number == 0) ||
      *number > static_cast<double>(maximum) || std::floor(*number) != *number)
    return false;
  *out = static_cast<std::uint64_t>(*number);
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
  std::uint64_t parsed_version = 0;
  std::uint64_t parsed_app = 0;
  if (!StringField(*root, "recipe_id", &out.id) ||
      !ExactUnsigned(version, static_cast<std::uint64_t>(std::numeric_limits<int>::max()), &parsed_version, false) ||
      !ExactUnsigned(app, std::numeric_limits<std::uint32_t>::max(), &parsed_app, false))
    return fail("recipe needs recipe_id, recipe_version and app_id");
  out.version = static_cast<int>(parsed_version);
  out.app_id = static_cast<std::uint32_t>(parsed_app);
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

namespace {

bool RecordFromValue(const storage_json::Value& root, GameSettingsRecord* out) {
  if (!root.is_object()) return false;
  if (const auto* preset = root.Find("preset")) {
    if (!preset->is_string()) return false;
    out->preset_id = preset->text();
  }
  if (const auto* overrides = root.Find("overrides")) {
    if (!overrides->is_object()) return false;
    for (const auto& [key, item] : overrides->object()) {
      if (!item.is_string()) return false;
      out->overrides[key] = item.text();
    }
  }
  if (const auto* revision = root.Find("revision")) {
    const auto parsed = revision->AsUint64();
    if (!parsed) return false;
    out->revision = *parsed;
  }
  // Settings this build does not know are dropped so an older record still loads.
  for (auto it = out->overrides.begin(); it != out->overrides.end();)
    it = FindSetting(it->first) ? std::next(it) : out->overrides.erase(it);
  return true;
}

}  // namespace

bool ParseGameSettingsRecord(std::string_view text, GameSettingsRecord* record, std::string* reason) {
  const auto root = storage_json::Parse(text);
  GameSettingsRecord out;
  if (!root || !RecordFromValue(*root, &out)) {
    if (reason) *reason = "settings record is malformed";
    return false;
  }
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

int OpenSafeDirectoryPath(const std::string& path) {
  if (path.empty()) return -1;
  const bool absolute = path.front() == '/';
  int directory = ::open(absolute ? "/" : ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (directory < 0) return -1;
  for (std::size_t start = absolute ? 1 : 0; start < path.size();) {
    const auto slash = path.find('/', start);
    const auto component = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    if (!component.empty()) {
      if (component == "." || component == "..") { ::close(directory); return -1; }
      const int next = ::openat(directory, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
      ::close(directory);
      if (next < 0) return -1;
      directory = next;
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  return directory;
}

int OpenSafeParent(const std::string& path, std::string* name) {
  const auto slash = path.rfind('/');
  *name = slash == std::string::npos ? path : path.substr(slash + 1);
  if (name->empty() || *name == "." || *name == "..") { errno = EINVAL; return -1; }
  if (slash == std::string::npos) return ::open(".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  return OpenSafeDirectoryPath(slash == 0 ? "/" : path.substr(0, slash));
}

int OpenLaunchDirectory(const LaunchEntry& launch, const std::string& install_root) {
  const auto executable = steam::RelativeGamePath(launch.executable);
  if (!executable) return -1;
  std::string working;
  if (launch.working_directory.empty()) {
    const auto slash = executable->rfind('/');
    working = slash == std::string::npos ? std::string() : executable->substr(0, slash);
  } else {
    const auto relative = steam::RelativeGamePath(launch.working_directory, true);
    if (!relative) return -1;
    working = *relative;
  }
  int directory = OpenSafeDirectoryPath(install_root);
  if (directory < 0) return -1;
  for (std::size_t start = 0; start < working.size();) {
    const auto slash = working.find('/', start);
    const auto component = working.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    const int next = ::openat(directory, component.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    ::close(directory);
    if (next < 0) return -1;
    directory = next;
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  return directory;
}

enum class RelativeRead { kMissing, kPresent, kError };

RelativeRead ReadRegularAt(int directory, const char* name, std::size_t maximum, std::string* text,
                           mode_t* mode = nullptr) {
  const int file = ::openat(directory, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if (file < 0) return errno == ENOENT ? RelativeRead::kMissing : RelativeRead::kError;
  struct stat status {};
  if (::fstat(file, &status) != 0 || !S_ISREG(status.st_mode)) { ::close(file); return RelativeRead::kError; }
  if (mode) *mode = status.st_mode & 0777;
  text->clear();
  char buffer[4096];
  while (true) {
    const auto count = ::read(file, buffer, sizeof(buffer));
    if (count < 0) { ::close(file); return RelativeRead::kError; }
    if (count == 0) break;
    if (text->size() + static_cast<std::size_t>(count) > maximum) {
      ::close(file);
      return RelativeRead::kError;
    }
    text->append(buffer, static_cast<std::size_t>(count));
  }
  if (::close(file) != 0) return RelativeRead::kError;
  return RelativeRead::kPresent;
}

bool WriteAtomicAt(int directory, const char* name, const std::string& text,
                   std::optional<mode_t> mode_override = std::nullopt) {
  mode_t mode = mode_override.value_or(0644);
  const int existing = ::openat(directory, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
  if (existing >= 0) {
    struct stat status {};
    const bool ordinary = ::fstat(existing, &status) == 0 && S_ISREG(status.st_mode);
    const bool closed = ::close(existing) == 0;
    if (!ordinary || !closed) return false;
    if (!mode_override) mode = status.st_mode & 0777;
  } else if (errno != ENOENT) return false;
  static std::atomic<std::uint64_t> sequence{0};
  const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
  std::string temporary;
  int file = -1;
  for (unsigned attempt = 0; attempt < 8; ++attempt) {
    const auto serial = sequence.fetch_add(1, std::memory_order_relaxed);
    temporary = std::string(name) + ".playden-tmp-" + std::to_string(static_cast<long long>(::getpid())) + "-" +
                std::to_string(static_cast<long long>(tick)) + "-" + std::to_string(serial);
    file = ::openat(directory, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, mode);
    if (file >= 0) break;
    if (errno != EEXIST) return false;
  }
  if (file < 0) return false;
  if (::fchmod(file, mode) != 0) {
    ::close(file);
    ::unlinkat(directory, temporary.c_str(), 0);
    return false;
  }
  std::size_t written = 0;
  while (written < text.size()) {
    const auto count = ::write(file, text.data() + written, text.size() - written);
    if (count <= 0) break;
    written += static_cast<std::size_t>(count);
  }
  const bool synced = written == text.size() && ::fsync(file) == 0;
  const bool closed = ::close(file) == 0;
  if (!synced || !closed || ::renameat(directory, temporary.c_str(), directory, name) != 0) {
    ::unlinkat(directory, temporary.c_str(), 0);
    return false;
  }
  return ::fsync(directory) == 0;
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
  if (!settings.launch) return fail("no launch option");

  // Check the launch working folder before writing anything, so a refusal leaves both files as they were.
  const int config_directory = OpenLaunchDirectory(*settings.launch, target.install_root);
  if (config_directory < 0) return fail("launch working folder is not a safe directory");
  std::string existing;
  const auto read_config = ReadRegularAt(config_directory, "dxvk.conf", 256u * 1024u, &existing);
  if (read_config == RelativeRead::kError) {
    ::close(config_directory);
    return fail("cannot read dxvk.conf");
  }
  const bool has_config = read_config == RelativeRead::kPresent;
  if (has_config && existing.compare(0, std::char_traits<char>::length(kDxvkHeader), kDxvkHeader) != 0)
  {
    ::close(config_directory);
    return fail("the game folder has its own dxvk.conf; Playden will not replace it");
  }

  if (!PublishProsperoProfile(prospero_root, profile, reason)) { ::close(config_directory); return false; }
  const auto config = RenderDxvkConfig(settings);
  if (config.empty()) {
    if (has_config && ::unlinkat(config_directory, "dxvk.conf", 0) != 0) {
      ::close(config_directory);
      return fail("cannot remove dxvk.conf");
    }
    ::close(config_directory);
    return true;
  }
  if (has_config && existing == config) { ::close(config_directory); return true; }
  const bool written = WriteAtomicAt(config_directory, "dxvk.conf", config);
  ::close(config_directory);
  if (!written) return fail("cannot write dxvk.conf");
  return true;
}

bool LoadGameSettingsFile(const std::string& path, std::map<std::uint32_t, GameSettingsRecord>* records,
                          std::string* reason) {
  records->clear();
  std::string text;
  std::string name;
  const int directory = OpenSafeParent(path, &name);
  if (directory < 0) {
    if (errno == ENOENT) return true;
    if (reason) *reason = "cannot read game settings";
    return false;
  }
  const auto read = ReadRegularAt(directory, name.c_str(), storage_json::Limits{}.max_bytes, &text);
  ::close(directory);
  if (read == RelativeRead::kMissing) return true;
  if (read == RelativeRead::kError) {
    if (reason) *reason = "cannot read game settings";
    return false;
  }
  const auto root = storage_json::Parse(text);
  const auto* version = root ? root->Find("version") : nullptr;
  const auto* games = root ? root->Find("games") : nullptr;
  if (!root || !root->is_object() || !version || version->AsUint64() != 1 || !games || !games->is_object()) {
    if (reason) *reason = "game settings file is malformed";
    return false;
  }
  for (const auto& [key, value] : games->object()) {
    std::uint32_t app_id = 0;
    const auto parsed = std::from_chars(key.data(), key.data() + key.size(), app_id);
    GameSettingsRecord record;
    if (key.empty() || parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size() || app_id == 0 ||
        !RecordFromValue(value, &record))
      continue;
    (*records)[app_id] = std::move(record);
  }
  return true;
}

bool SaveGameSettingsFile(const std::string& path, const std::map<std::uint32_t, GameSettingsRecord>& records,
                          std::string* reason) {
  std::string text = "{\"version\":1,\"games\":{";
  bool first = true;
  for (const auto& [app_id, record] : records) {
    if (record.preset_id.empty() && record.overrides.empty() && record.revision == 0) continue;
    if (!first) text += ",";
    first = false;
    text += "\"" + std::to_string(app_id) + "\":" + SerializeGameSettingsRecord(record);
  }
  text += "}}\n";
  std::string name;
  const int directory = OpenSafeParent(path, &name);
  if (directory < 0) {
    if (reason) *reason = "cannot write game settings";
    return false;
  }
  struct stat status {};
  const int inspected = ::fstatat(directory, name.c_str(), &status, AT_SYMLINK_NOFOLLOW);
  if (inspected == 0 && !S_ISREG(status.st_mode)) {
    ::close(directory);
    if (reason) *reason = "cannot write game settings";
    return false;
  }
  if (inspected != 0 && errno != ENOENT) {
    ::close(directory);
    if (reason) *reason = "cannot write game settings";
    return false;
  }
  const bool written = WriteAtomicAt(directory, name.c_str(), text);
  ::close(directory);
  if (!written) {
    if (reason) *reason = "cannot write game settings";
    return false;
  }
  return true;
}

bool ParseProfileSidecar(std::string_view text, CommunityPreset* community, std::string* reason) {
  const auto fail = [&](std::string why) {
    if (reason) *reason = std::move(why);
    return false;
  };
  const auto root = json::parse(text);
  const auto* version = root ? root->get("schema_version") : nullptr;
  if (!root || !root->object() || !version || version->number() != 1.0) return fail("not a schema 1 sidecar");
  CommunityPreset out;
  const auto* game = root->get("game");
  std::string name;
  if (!StringField(*root, "profile", &out.profile_id) || !game || !StringField(*game, "name", &name))
    return fail("sidecar needs profile and game.name");
  if (const auto* app = game->get("steam_appid")) {
    std::uint64_t parsed_app = 0;
    if (!ExactUnsigned(app, std::numeric_limits<std::uint32_t>::max(), &parsed_app, false))
      return fail("sidecar steam_appid is invalid");
    out.steam_appid = static_cast<std::uint32_t>(parsed_app);
  }
  if (const auto* requires_field = root->get("requires")) {
    if (const auto* prs = requires_field->get("prospero_win"); prs && prs->array())
      for (const auto& item : *prs->array())
        if (item.string()) out.requires_prospero_win.push_back(*item.string());
  }
  const auto* checks = root->get("checks");
  if (!checks || !checks->array() || checks->array()->empty()) return fail("sidecar has no console check");
  const auto& newest = checks->array()->back();
  std::string date, firmware, notes;
  StringField(newest, "rating", &out.rating, false);
  StringField(newest, "date", &date, false);
  StringField(newest, "firmware", &firmware, false);
  StringField(newest, "notes", &notes, false);
  out.preset.id = "community-" + out.profile_id;
  out.preset.name = "Community · " + name;
  out.preset.try_when = notes;
  out.preset.short_hint = out.rating + (firmware.empty() ? "" : " on " + firmware) + (date.empty() ? "" : ", " + date);
  std::map<std::string, std::string> settings;
  if (!StringMap(root->get("settings"), &settings)) return fail("settings must map ids to strings");
  // Keep what a preset may set and this build accepts; skip the rest rather than refuse.
  for (const auto& [id, value] : settings) {
    const auto* definition = FindSetting(id);
    const bool button = id.compare(0, 7, "button.") == 0;
    if (!definition || id == "launch_option" || id == "graphics" || (definition->kind == SettingKind::kText && !button))
      continue;
    if (ValidateSettingValue(GameRecipe{}, id, value, nullptr)) out.preset.settings[id] = value;
  }
  *community = std::move(out);
  return true;
}

bool ClearShaderCache(const LaunchEntry& launch, const std::string& install_root, std::string* reason) {
  const auto fail = [&](std::string why) {
    if (reason) *reason = std::move(why);
    return false;
  };
  const auto executable = steam::RelativeGamePath(launch.executable);
  if (!executable) return fail("launch executable is not a game path");
  auto stem = executable->substr(executable->rfind('/') == std::string::npos ? 0 : executable->rfind('/') + 1);
  const auto lower = [](char value) { return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value; };
  if (stem.size() > 4 && lower(stem[stem.size() - 4]) == '.' && lower(stem[stem.size() - 3]) == 'e' &&
      lower(stem[stem.size() - 2]) == 'x' && lower(stem[stem.size() - 1]) == 'e')
    stem.resize(stem.size() - 4);
  const int directory = OpenLaunchDirectory(launch, install_root);
  if (directory < 0) return fail("launch working folder is not a safe directory");
  const auto name = stem + ".dxvk-cache";
  struct stat status {};
  if (::fstatat(directory, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0) {
    const int error = errno;
    ::close(directory);
    if (error == ENOENT) return true;
    return fail("cannot inspect the shader cache");
  }
  if (!S_ISREG(status.st_mode)) {
    ::close(directory);
    return fail("shader cache is not a regular file");
  }
  const bool removed = ::unlinkat(directory, name.c_str(), 0) == 0;
  ::close(directory);
  if (!removed) return fail("cannot delete the shader cache");
  return true;
}

std::vector<RegistryValue> RenderRegistryValues(const EffectiveSettings& settings) {
  std::vector<RegistryValue> values;
  const auto& version = settings.values.at("windows_version");
  if (version.available && settings.launch) {
    auto executable = settings.launch->executable;
    std::replace(executable.begin(), executable.end(), '\\', '/');
    const auto slash = executable.rfind('/');
    const auto exe = slash == std::string::npos ? executable : executable.substr(slash + 1);
    // Windows 10 is Wine's default here, so it removes the per-game value.
    values.push_back({"Software\\Wine\\AppDefaults\\" + exe, "Version",
                      version.value == "win10" ? std::string() : "\"" + version.value + "\""});
  }
  const auto& scale = settings.values.at("text_scale");
  if (scale.available) {
    // Wine reads the DPI from Control Panel\Desktop (winecfg's slider writes it there);
    // Software\Wine\Fonts LogPixels is only its font cache marker, rewritten at start.
    // 96 is Wine's default, so 100% removes the value.
    char dword[16];
    std::snprintf(dword, sizeof(dword), "dword:%08lx", std::strtoul(scale.value.c_str(), nullptr, 10));
    values.push_back({"Control Panel\\Desktop", "LogPixels", scale.value == "96" ? std::string() : dword});
  }
  return values;
}

namespace {

std::string RegistryHeader(const std::string& key) {
  std::string escaped;
  for (const char c : key) escaped += c == '\\' ? std::string("\\\\") : std::string(1, c);
  return "[" + escaped + "]";
}

char AsciiLower(char value) {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

bool RegistryEqual(std::string_view left, std::string_view right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index)
    if (AsciiLower(left[index]) != AsciiLower(right[index])) return false;
  return true;
}

std::string RegistryIdentity(const RegistryValue& value) {
  auto identity = RegistryHeader(value.key) + "\n" + value.name;
  std::transform(identity.begin(), identity.end(), identity.begin(), AsciiLower);
  return identity;
}

}  // namespace

bool ApplyRegistryValues(std::string* user_reg, const std::vector<RegistryValue>& values, std::int64_t unix_time,
                         std::string* reason) {
  const auto fail = [&](std::string why) {
    if (reason) *reason = std::move(why);
    return false;
  };
  if (user_reg->compare(0, 23, "WINE REGISTRY Version 2") != 0) return fail("not a Wine registry file");
  std::string text = *user_reg;
  std::set<std::string> identities;
  for (const auto& value : values) {
    if (value.key.empty() || value.name.empty() || value.name.find('"') != std::string::npos ||
        HasControl(value.key) || HasControl(value.name) || HasControl(value.data))
      return fail("bad registry value");
    if (!identities.insert(RegistryIdentity(value)).second) return fail("duplicate registry value");
  }
  for (const auto& value : values) {
    const auto header = RegistryHeader(value.key);
    const auto line_prefix = "\"" + value.name + "\"=";
    std::size_t section = std::string::npos;
    std::size_t body = std::string::npos;
    std::size_t end = text.size();
    std::size_t section_count = 0;
    for (std::size_t at = 0; at < text.size();) {
      const auto newline = text.find('\n', at);
      const auto next = newline == std::string::npos ? text.size() : newline + 1;
      auto content_end = newline == std::string::npos ? text.size() : newline;
      if (content_end > at && text[content_end - 1] == '\r') --content_end;
      const std::string_view line(text.data() + at, content_end - at);
      if (line.size() >= header.size() && RegistryEqual(line.substr(0, header.size()), header)) {
        const auto suffix = line.substr(header.size());
        bool valid_suffix = suffix.empty();
        if (!suffix.empty() && suffix.front() == ' ') {
          valid_suffix = suffix.size() > 1 &&
                         std::all_of(suffix.begin() + 1, suffix.end(), [](char c) { return c >= '0' && c <= '9'; });
        }
        if (!valid_suffix) return fail("malformed registry section");
        if (++section_count > 1) return fail("duplicate registry section");
        section = at;
        body = next;
      } else if (section_count == 1 && end == text.size() && at >= body && !line.empty() && line.front() == '[') {
        end = at;
      }
      if (newline == std::string::npos) break;
      at = next;
    }
    if (section == std::string::npos) {
      if (value.data.empty()) continue;
      const std::string newline = text.find("\r\n") == std::string::npos ? "\n" : "\r\n";
      if (!text.empty() && text.back() != '\n') text += '\n';
      text += newline + header + " " + std::to_string(unix_time) + newline + line_prefix + value.data + newline;
      continue;
    }
    std::size_t line = std::string::npos;
    std::size_t assignment_count = 0;
    for (auto at = body; at < end;) {
      const auto newline = text.find('\n', at);
      const auto line_end = std::min(newline, end);
      if (line_end - at >= line_prefix.size() &&
          RegistryEqual(std::string_view(text).substr(at, line_prefix.size()), line_prefix)) {
        line = at;
        if (++assignment_count > 1) return fail("duplicate registry assignment");
      }
      if (newline == std::string::npos || newline >= end) break;
      at = newline + 1;
    }
    if (line != std::string::npos) {
      auto line_end = text.find('\n', line);
      const auto terminator = line_end == std::string::npos ? std::string() :
                              (line_end > line && text[line_end - 1] == '\r' ? "\r\n" : "\n");
      line_end = line_end == std::string::npos ? text.size() : line_end + 1;
      const auto existing_prefix = text.substr(line, line_prefix.size());
      text.replace(line, line_end - line,
                   value.data.empty() ? std::string() : existing_prefix + value.data + terminator);
    } else if (!value.data.empty()) {
      const std::string newline = text.find("\r\n") == std::string::npos ? "\n" : "\r\n";
      // After the section's last value, before the blank line that separates sections.
      while (end > body && text[end - 1] == '\n') {
        auto blank = end - 1;
        if (blank > body && text[blank - 1] == '\r') --blank;
        if (blank == body || text[blank - 1] != '\n') break;
        end = blank;
      }
      std::string insertion;
      if (end > body && text[end - 1] != '\n') insertion += newline;
      insertion += line_prefix + value.data + newline;
      text.insert(end, insertion);
    }
  }
  *user_reg = std::move(text);
  return true;
}

static bool ApplyRegistryFileValues(const std::vector<RegistryValue>& values, const std::string& prefix_dir,
                                    std::int64_t unix_time, std::string* reason) {
  const auto fail = [&](std::string why) {
    if (reason) *reason = std::move(why);
    return false;
  };
  if (values.empty()) return true;
  const int directory = OpenSafeDirectoryPath(prefix_dir);
  if (directory < 0) return fail("cannot read the bottle's user.reg");
  std::string original;
  constexpr std::size_t kMaxRegistryBytes = 32u * 1024u * 1024u;
  mode_t original_mode = 0600;
  if (ReadRegularAt(directory, "user.reg", kMaxRegistryBytes, &original, &original_mode) != RelativeRead::kPresent) {
    ::close(directory);
    return fail("cannot read the bottle's user.reg");
  }
  std::string edited = original;
  if (!ApplyRegistryValues(&edited, values, unix_time, reason)) { ::close(directory); return false; }
  if (edited == original) { ::close(directory); return true; }
  if (!WriteAtomicAt(directory, "user.reg.playden-backup", original, original_mode)) {
    ::close(directory);
    return fail("cannot back up user.reg");
  }
  if (!WriteAtomicAt(directory, "user.reg", edited)) {
    ::close(directory);
    return fail("cannot write user.reg");
  }
  ::close(directory);
  return true;
}

bool ApplyRegistrySettings(const EffectiveSettings& settings, const std::string& prefix_dir, std::int64_t unix_time,
                           std::string* reason) {
  return ApplyRegistryFileValues(RenderRegistryValues(settings), prefix_dir, unix_time, reason);
}

}  // namespace playden::runtime

namespace playden::runtime {

const std::vector<std::string>& SettingsKitCapabilityNames() {
  // Sorted, as a kit manifest lists them.
  static const std::vector<std::string> names = {kKitAudioVolume, kKitDxvkConfig,  kKitDxvkPresent,
                                                 kKitInputTuning, kKitInputWheel,  kKitEnvironment};
  return names;
}

KitCapabilities KitCapabilitiesFromManifest(const std::vector<std::string>& capabilities, bool bottle_access) {
  const auto has = [&](const char* name) {
    return std::find(capabilities.begin(), capabilities.end(), name) != capabilities.end();
  };
  KitCapabilities kit;
  kit.dxvk_config = has(kKitDxvkConfig);
  kit.dxvk_present = has(kKitDxvkPresent);
  kit.environment = has(kKitEnvironment);
  kit.input_tuning = has(kKitInputTuning);
  kit.input_wheel = has(kKitInputWheel);
  kit.audio_volume = has(kKitAudioVolume);
  kit.registry = bottle_access;
  return kit;
}

bool BuildSettingsLaunchPlan(const SettingsLaunchInput& input, SettingsLaunchPlan* plan, std::string* reason) {
  const auto fail = [&](std::string why) {
    if (reason) *reason = std::move(why);
    return false;
  };
  if (!input.recipe || !plan) return fail("no recipe or plan output");
  if (input.record.preset_id.empty()) {
    if (input.preset && (input.preset->id != "playden-default" || !input.preset->settings.empty()))
      return fail("default record does not match the supplied preset");
  } else if (!input.preset || input.preset->id != input.record.preset_id) {
    return fail("record preset is missing or does not match the supplied preset");
  }
  SettingsLaunchPlan out;
  out.app_id = input.recipe->app_id;
  out.recipe_id = input.recipe->id;
  out.recipe_version = std::to_string(input.recipe->version);
  EffectiveSettings settings;
  SettingsError error;
  if (!BuildEffectiveSettings(*input.recipe, input.preset, input.record.overrides, input.kit, &settings, &error))
    return fail(error.setting_id.empty() ? error.message : error.setting_id + ": " + error.message);
  ProsperoProfile profile;
  if (!RenderProsperoProfile(*input.recipe, settings, input.target, &profile, reason)) return false;
  out.profile_id = profile.id;
  out.profile_bytes = std::move(profile.text);
  out.settings_revision = input.record.revision;
  if (!settings.launch) return fail("no launch option");
  out.launch_entry = *settings.launch;
  out.launch_entry_id = out.launch_entry.id;

  // Every preset the profile names must ship with it, or the runtime refuses the profile.
  const auto& preset = settings.values.at("controller_preset").value;
  if (!preset.empty() && preset != "none") {
    if (preset.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") != std::string::npos)
      return fail("controller preset name is not valid");
    const int directory = OpenSafeDirectoryPath(input.runtime_dir + "/input");
    if (directory < 0) return fail("controller preset " + preset + " is not available");
    std::string bytes;
    const auto file = preset + ".input";
    const auto read = ReadRegularAt(directory, file.c_str(), kProsperoMaxProfileBytes, &bytes);
    ::close(directory);
    if (read != RelativeRead::kPresent || bytes.find("[input]") == std::string::npos)
      return fail("controller preset " + preset + " is not available");
    out.input_presets.emplace_back(file, std::move(bytes));
  }
  out.dxvk_config = RenderDxvkConfig(settings);
  out.registry = RenderRegistryValues(settings);
  *plan = std::move(out);
  return true;
}

bool ApplySettingsFiles(const SettingsLaunchPlan& plan, const std::string& install_root, const std::string& prefix_dir,
                        std::int64_t unix_time, std::string* reason) {
  const auto fail = [&](std::string why) {
    if (reason) *reason = std::move(why);
    return false;
  };
  if (plan.launch_entry.id.empty() || plan.launch_entry.id != plan.launch_entry_id)
    return fail("no matching launch option");
  const int directory = OpenLaunchDirectory(plan.launch_entry, install_root);
  if (directory < 0) return fail("launch working folder is not a safe directory");
  std::string existing;
  mode_t existing_mode = 0644;
  const auto read = ReadRegularAt(directory, "dxvk.conf", 256u * 1024u, &existing, &existing_mode);
  const bool has_config = read == RelativeRead::kPresent;
  const auto close_fail = [&](std::string why) {
    ::close(directory);
    return fail(std::move(why));
  };
  if (read == RelativeRead::kError) return close_fail("cannot read dxvk.conf");
  if (has_config && existing.compare(0, std::char_traits<char>::length(kDxvkHeader), kDxvkHeader) != 0)
    return close_fail("the game folder has its own dxvk.conf; Playden will not replace it");
  // This is a stopped-title file operation, not a cross-directory transaction.
  // Restore config bytes/mode on registry failure and surface failed rollback.
  // The integration owner journals readiness and reconciles any interrupted apply.
  bool changed = false;
  if (plan.dxvk_config.empty()) {
    if (has_config) {
      if (::unlinkat(directory, "dxvk.conf", 0) != 0) return close_fail("cannot remove dxvk.conf");
      if (::fsync(directory) != 0) return close_fail("cannot sync dxvk.conf removal; reconciliation required");
      changed = true;
    }
  } else if (!has_config || existing != plan.dxvk_config) {
    if (!WriteAtomicAt(directory, "dxvk.conf", plan.dxvk_config))
      return close_fail("cannot commit dxvk.conf; reconciliation required");
    changed = true;
  }
  if (!plan.registry.empty() && !ApplyRegistryFileValues(plan.registry, prefix_dir, unix_time, reason)) {
    const auto registry_reason = reason ? *reason : std::string("registry apply failed");
    if (changed) {
      const bool restored = has_config
          ? WriteAtomicAt(directory, "dxvk.conf", existing, existing_mode)
          : (::unlinkat(directory, "dxvk.conf", 0) == 0 && ::fsync(directory) == 0);
      if (!restored)
        return close_fail("settings rollback failed; reconciliation required: " + registry_reason);
    }
    return close_fail("settings apply failed; reconciliation required: " + registry_reason);
  }
  ::close(directory);
  return true;
}

}  // namespace playden::runtime

namespace playden::runtime {

bool CommunityPresetSupported(const CommunityPreset& community, const KitCapabilities& kit) {
  for (const auto& feature : community.requires_prospero_win) {
    if ((feature == "profile-environment" && !kit.environment) || (feature == "input-tuning" && !kit.input_tuning) ||
        (feature == "input-wheel" && !kit.input_wheel) || (feature == "audio-volume" && !kit.audio_volume))
      return false;
  }
  return true;
}

}  // namespace playden::runtime
