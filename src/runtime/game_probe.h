#pragma once

#include <set>
#include <string>

namespace playden::runtime {

// What a Windows game's files say about how to start it on the console. Read only and
// bounded: symbolic links are never followed, folders are walked a few levels deep and at
// most kProbeScanBytes of a program are scanned.
struct GameProbe {
  std::string engine;  // "unity", "unreal", "source", "gamemaker", "godot", "love", or empty
  bool il2cpp = false;  // Unity with GameAssembly.dll
  bool legacy_unity = false;  // Unity before 2017.2: the player is built into the program
  // Graphics libraries the program or its engine imports or names: "d3d8", "d3d9", "d3d10",
  // "d3d11", "dxgi", "d3d12", "opengl", "vulkan", "ddraw".
  std::set<std::string> apis;
  bool dotnet = false;      // a .NET program (needs Mono, which the runtime lacks)
  bool steam_stub = false;  // SteamStub DRM: the entry point is in a ".bind" section
  // A writable section shared between processes whose pages the console can't map
  // (16 KB pages; GameMaker's runner has one, ".mydata").
  bool shared_section = false;
  bool anticheat = false;   // an EasyAntiCheat or BattlEye folder in the game
  // For an Unreal game started through its root program (which starts the real one as a
  // child process, which the console cannot do): the *-Shipping.exe to start instead,
  // relative to the install root, as spelled on disk.
  std::string unreal_program;
};

inline constexpr unsigned long long kProbeScanBytes = 256ull * 1024ull * 1024ull;

// executable is relative to install_root with '/' separators, as spelled on disk.
GameProbe ProbeGame(const std::string& install_root, const std::string& executable);

// How a game without a shipped recipe starts, from its probe.
struct GameDefaults {
  std::string graphics = "auto";  // auto, dxvk or opengl
  std::string arguments;          // recipe arguments; may hold "{resolution}"
  std::string resolution_arguments;
  std::string refusal;  // non-empty: the console cannot run this program; a sentence for the player
  std::string summary;  // e.g. "unity,d3d11 graphics=dxvk", for logs and the settings sheet
};
GameDefaults DefaultsFor(const GameProbe& probe);

// The player's words for a GameDefaults::summary: "Unity · Direct3D 11", "OpenGL"; empty
// when nothing was recognised.
std::string DetectedLabel(const std::string& summary);

// The DLL overrides that make a game use the bottle's DXVK instead of Wine's Direct3D.
inline constexpr const char* kDxvkOverrides = "d3d8,d3d9,d3d10core,d3d11,dxgi=n";

}  // namespace playden::runtime
