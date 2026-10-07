#include "runtime/game_probe.h"
#include "runtime/game_settings.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

#define CHECK(cond)                                                                     \
  do {                                                                                  \
    if (!(cond)) {                                                                      \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);     \
      std::exit(1);                                                                     \
    }                                                                                   \
  } while (0)

using namespace playden::runtime;
namespace fs = std::filesystem;

namespace {

struct PeSpec {
  bool pe64 = true;
  std::vector<std::string> imports;
  bool clr = false;
  bool bind_entry = false;   // entry point inside a ".bind" section (SteamStub)
  std::string text;          // bytes placed in the code section (names loaded at run time)
  std::size_t data_at = 0x1400;  // file offset of the section holding the import table
};

void Put32(std::string* bytes, std::size_t at, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) (*bytes)[at + i] = static_cast<char>((value >> (8 * i)) & 0xff);
}
void Put16(std::string* bytes, std::size_t at, std::uint16_t value) {
  (*bytes)[at] = static_cast<char>(value & 0xff);
  (*bytes)[at + 1] = static_cast<char>(value >> 8);
}

void WritePe(const fs::path& path, const PeSpec& spec) {
  fs::create_directories(path.parent_path());
  const std::size_t optional_size = spec.pe64 ? 0xf0 : 0xe0;
  std::string bytes(spec.data_at + 0x1000, '\0');
  bytes[0] = 'M'; bytes[1] = 'Z'; Put32(&bytes, 0x3c, 0x40);
  bytes[0x40] = 'P'; bytes[0x41] = 'E';
  Put16(&bytes, 0x44, spec.pe64 ? 0x8664 : 0x014c);
  Put16(&bytes, 0x46, 2);
  Put16(&bytes, 0x54, static_cast<std::uint16_t>(optional_size));
  const std::size_t optional = 0x58;
  Put16(&bytes, optional, spec.pe64 ? 0x20b : 0x10b);
  Put32(&bytes, optional + 16, 0x1010);  // entry point, in the first section
  const std::size_t directories = optional + (spec.pe64 ? 112 : 96);
  if (!spec.imports.empty()) Put32(&bytes, directories + 8, 0x2000);
  if (spec.clr) Put32(&bytes, directories + 14 * 8, 0x2800);
  const std::size_t table = optional + optional_size;
  const auto section = [&](std::size_t row, const char* name, std::uint32_t address, std::uint32_t raw) {
    for (std::size_t i = 0; name[i]; ++i) bytes[table + row * 40 + i] = name[i];
    Put32(&bytes, table + row * 40 + 8, 0x1000);
    Put32(&bytes, table + row * 40 + 12, address);
    Put32(&bytes, table + row * 40 + 16, 0x1000);
    Put32(&bytes, table + row * 40 + 20, raw);
  };
  section(0, spec.bind_entry ? ".bind" : ".text", 0x1000, 0x400);
  section(1, ".rdata", 0x2000, static_cast<std::uint32_t>(spec.data_at));
  bytes.replace(0x400, spec.text.size(), spec.text);
  std::uint32_t name_rva = 0x2400;
  for (std::size_t i = 0; i < spec.imports.size(); ++i) {
    Put32(&bytes, spec.data_at + i * 20 + 12, name_rva);
    bytes.replace(spec.data_at + (name_rva - 0x2000), spec.imports[i].size(), spec.imports[i]);
    name_rva += static_cast<std::uint32_t>(spec.imports[i].size() + 1);
  }
  std::ofstream(path, std::ios::binary) << bytes;
}

std::string Wide(const std::string& text) {
  std::string out;
  for (char value : text) out += value, out += '\0';
  return out;
}

void Touch(const fs::path& path) {
  fs::create_directories(path.parent_path());
  std::ofstream(path) << "x";
}

}  // namespace

int main() {
  const auto base = fs::temp_directory_path() / ("playden-game-probe-" + std::to_string(::getpid()));
  fs::remove_all(base);
  int case_number = 0;
  const auto fresh = [&] { return base / std::to_string(++case_number); };

  {  // Imported Direct3D 9, with the import table far into a large program.
    const auto root = fresh();
    WritePe(root / "Game.exe", {false, {"KERNEL32.dll", "d3d9.dll"}, false, false, "", 9u * 1024u * 1024u});
    const auto probe = ProbeGame(root.string(), "Game.exe");
    CHECK(probe.apis.count("d3d9") && probe.engine.empty() && !probe.dotnet && !probe.steam_stub);
    const auto defaults = DefaultsFor(probe);
    CHECK(defaults.graphics == "dxvk" && defaults.arguments.empty() && defaults.refusal.empty());
    CHECK(defaults.summary == "d3d9 graphics=dxvk");
  }
  {  // Unity loads Direct3D at run time (wide strings); D3D11 is forced.
    const auto root = fresh();
    WritePe(root / "Balls.exe", {true, {"UnityPlayer.dll", "KERNEL32.dll"}});
    WritePe(root / "UnityPlayer.dll", {true, {"KERNEL32.dll"}, false, false, Wide("d3d11.dll") + Wide("D3D12.dll")});
    WritePe(root / "GameAssembly.dll", {true, {"KERNEL32.dll"}});
    fs::create_directories(root / "Balls_Data");
    const auto probe = ProbeGame(root.string(), "Balls.exe");
    CHECK(probe.engine == "unity" && probe.il2cpp && probe.apis.count("d3d11") && probe.apis.count("d3d12"));
    const auto defaults = DefaultsFor(probe);
    CHECK(defaults.graphics == "dxvk" && defaults.arguments == "-force-d3d11 {resolution}");
    CHECK(defaults.resolution_arguments == "-screen-width {w} -screen-height {h}");
    CHECK(defaults.summary == "unity-il2cpp,d3d11,d3d12 graphics=dxvk");
  }
  {  // A linked UnityPlayer.dll is not followed.
    const auto root = fresh();
    WritePe(root / "Game.exe", {true, {"KERNEL32.dll"}});
    WritePe(base / "elsewhere.dll", {true, {}, false, false, "d3d11.dll"});
    fs::create_symlink(base / "elsewhere.dll", root / "UnityPlayer.dll");
    const auto probe = ProbeGame(root.string(), "Game.exe");
    CHECK(probe.engine.empty() && probe.apis.empty() && DefaultsFor(probe).graphics == "auto");
  }
  {  // GameMaker names D3D11 in upper case.
    const auto root = fresh();
    WritePe(root / "Risk.exe", {true, {"KERNEL32.dll"}, false, false, "D3D11.DLL"});
    Touch(root / "data.win");
    const auto probe = ProbeGame(root.string(), "Risk.exe");
    CHECK(probe.engine == "gamemaker" && probe.apis.count("d3d11"));
    CHECK(DefaultsFor(probe).graphics == "dxvk");
  }
  {  // Programs the console cannot run are refused with a reason.
    const auto root = fresh();
    WritePe(root / "Stub.exe", {false, {"d3d9.dll"}, false, true});
    WritePe(root / "Net.exe", {false, {"mscoree.dll"}});
    WritePe(root / "Clr.exe", {true, {"KERNEL32.dll"}, true});
    WritePe(root / "Twelve.exe", {true, {"d3d12.dll"}});
    CHECK(DefaultsFor(ProbeGame(root.string(), "Stub.exe")).refusal.find("Steam DRM") != std::string::npos);
    CHECK(DefaultsFor(ProbeGame(root.string(), "Net.exe")).refusal.find(".NET") != std::string::npos);
    CHECK(DefaultsFor(ProbeGame(root.string(), "Clr.exe")).refusal.find(".NET") != std::string::npos);
    CHECK(DefaultsFor(ProbeGame(root.string(), "Twelve.exe")).refusal.find("Direct3D 12") != std::string::npos);
  }
  {  // OpenGL and plain Windows programs.
    const auto root = fresh();
    WritePe(root / "Gl.exe", {false, {"OPENGL32.dll"}});
    WritePe(root / "Plain.exe", {false, {"USER32.dll", "ddraw.dll"}});
    CHECK(DefaultsFor(ProbeGame(root.string(), "Gl.exe")).graphics == "opengl");
    const auto plain = ProbeGame(root.string(), "Plain.exe");
    CHECK(plain.apis.count("ddraw") && DefaultsFor(plain).graphics == "auto");
  }
  {  // Godot and Source.
    const auto root = fresh();
    WritePe(root / "win/garagerally.exe", {true, {"KERNEL32.dll"}, false, false, "vulkan-1.dll"});
    Touch(root / "win/garagerally.pck");
    const auto godot = DefaultsFor(ProbeGame(root.string(), "win/garagerally.exe"));
    CHECK(godot.graphics == "opengl" && godot.arguments == "--rendering-driver opengl3");
    WritePe(root / "hl2.exe", {false, {"KERNEL32.dll"}});
    WritePe(root / "bin/engine.dll", {false, {"d3d9.dll"}});
    const auto source = DefaultsFor(ProbeGame(root.string(), "hl2.exe"));
    CHECK(source.graphics == "dxvk" && source.resolution_arguments == "-w {w} -h {h}");
  }
  {  // An Unreal root program is replaced by its Shipping program, which starts in its own folder.
    const auto root = fresh();
    WritePe(root / "Game.exe", {true, {"KERNEL32.dll"}});
    fs::create_directories(root / "Engine/Binaries/Win64");
    WritePe(root / "Game/Binaries/Win64/Game-Win64-Shipping.exe", {true, {"d3d11.dll", "d3d12.dll"}});
    Touch(root / "EasyAntiCheat/settings.json");
    const auto probe = ProbeGame(root.string(), "Game.exe");
    CHECK(probe.engine == "unreal" && probe.unreal_program == "Game/Binaries/Win64/Game-Win64-Shipping.exe");
    CHECK(probe.anticheat && DefaultsFor(probe).arguments == "-dx11");
    GameRecipe recipe;
    std::string reason;
    CHECK(GenerateGameRecipe(9001, "Unreal", {{"", "", "Game.exe", "", "-nosplash"}}, root.string(), &recipe, &reason));
    CHECK(recipe.launch_entries[0].executable == "Game/Binaries/Win64/Game-Win64-Shipping.exe");
    CHECK(recipe.launch_entries[0].working_directory == "Game/Binaries/Win64");
    CHECK(recipe.launch_entries[0].arguments == "-nosplash" && recipe.arguments == "-dx11" && recipe.graphics == "dxvk");
    CHECK(recipe.detected == "unreal,anticheat graphics=dxvk");
  }
  {  // Generated recipes: DXVK through overrides that follow the graphics value; graphics is open.
    const auto root = fresh();
    WritePe(root / "Balls.exe", {true, {"UnityPlayer.dll"}});
    WritePe(root / "UnityPlayer.dll", {true, {}, false, false, "d3d11.dll"});
    GameRecipe recipe;
    std::string reason;
    CHECK(GenerateGameRecipe(2062430, "BALL x PIT", {{"", "", "Balls.exe", "", ""}}, root.string(), &recipe, &reason));
    CHECK(recipe.graphics == "dxvk" && recipe.dll_overrides.empty() && recipe.locked.empty());
    GameRecipe reread;
    CHECK(ParseGameRecipe(SerializeGameRecipe(recipe), &reread, &reason) && reread.detected == recipe.detected);
    EffectiveSettings settings;
    SettingsError error;
    CHECK(BuildEffectiveSettings(recipe, nullptr, {{"library_overrides", "winmm=n"}}, KitCapabilities{}, &settings, &error));
    CHECK(!settings.values.at("graphics").locked && settings.graphics == "dxvk");
    CHECK(settings.dll_overrides == std::string(kDxvkOverrides) + ";winmm=n");
    CHECK(settings.arguments == "-force-d3d11 -screen-width 1920 -screen-height 1080");
    CHECK(BuildEffectiveSettings(recipe, nullptr, {{"graphics", "auto"}}, KitCapabilities{}, &settings, &error));
    CHECK(settings.graphics == "auto" && settings.dll_overrides.empty());
    // Every program refused: the first reason is the player's.
    WritePe(root / "Stub.exe", {false, {"d3d9.dll"}, false, true});
    CHECK(!GenerateGameRecipe(1, "Stub", {{"", "", "Stub.exe", "", ""}}, root.string(), &recipe, &reason));
    CHECK(reason.find("Steam DRM") != std::string::npos);
  }

  // The player's words for what was found.
  CHECK(DetectedLabel("unity-il2cpp,d3d11,d3d12 graphics=dxvk") == "Unity \xC2\xB7 Direct3D 11");
  CHECK(DetectedLabel("d3d9 graphics=dxvk") == "Direct3D 9");
  CHECK(DetectedLabel("godot,opengl,vulkan graphics=opengl") == "Godot \xC2\xB7 OpenGL");
  CHECK(DetectedLabel("unknown graphics=auto").empty() && DetectedLabel("").empty());

  fs::remove_all(base);
  std::puts("game probe tests passed");
}
