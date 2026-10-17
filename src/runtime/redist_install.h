#pragma once

#include "runtime/install_script.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace playden::runtime {

// Redistributables a game's Steam install scripts run on its first start ("Run Process"
// entries: PhysX, Visual C++, DirectX, .NET). The console can't start their installers, so:
// Windows Installer packages are installed by Playden itself (runtime/msi_package.h), installers
// whose DLLs Wine provides are skipped, and other programs are skipped and logged.

enum class RedistAction { kMsi, kCovered, kNotRun };

// What Playden does with an entry. For kMsi, package is the package's path inside the game
// folder ('/' separators), found from `msiexec /i <package>` or a process that is the package.
RedistAction ClassifyRedist(const RedistEntry& entry, const std::string& install_dir, std::string* package);

struct RedistOutcome {
  std::string script;   // the install script, relative to the game folder
  std::string name;
  std::string result;   // "installed", "already", "covered", "not-run", "failed"
  std::string detail;   // the package, program or failure reason
  std::size_t files = 0;
};

struct RedistInstallResult {
  std::vector<RedistOutcome> outcomes;
};

// Finds install scripts at the top of game_root and in its redistributable folders
// (_CommonRedist, Redist, Support, __Installer…, up to four folders deep), and installs their
// Windows Installer packages into the stopped bottle at prefix_dir. A package installed before
// (the same SHA-256, listed in <prefix>/.playden-redist) is not installed again. A package that
// fails is reported and left out; the others still install.
RedistInstallResult InstallRedistributables(const std::string& game_root, const std::string& install_dir,
                                            const std::string& prefix_dir, std::int64_t unix_time);

// Adds folders to a PATH value: prepend in order before the existing entries, append after
// them, each only when not already listed (case-insensitive).
std::string ExtendPath(const std::string& current, const std::vector<std::string>& prepend,
                       const std::vector<std::string>& append);

}  // namespace playden::runtime
