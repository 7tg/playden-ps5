#include "steam/install_volumes.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

using namespace playden::steam;

#define CHECK(condition)                                                                  \
  do {                                                                                    \
    if (!(condition)) {                                                                   \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
      std::exit(1);                                                                       \
    }                                                                                     \
  } while (false)

int main() {
  const auto base = std::filesystem::temp_directory_path() / ("playden-volumes-" + std::to_string(::getpid()));
  std::filesystem::remove_all(base);
  std::filesystem::create_directories(base / "app0");
  std::filesystem::create_directories(base / "ext1");
  std::filesystem::create_directories(base / "locked");

  const std::vector<VolumeCandidate> candidates = {
      {"internal", "Console storage", (base / "app0").string(), (base / "app0" / "games").string()},
      {"m2", "M.2 SSD", (base / "ext1").string(), (base / "ext1" / "playden" / "games").string()},
      {"usb0", "USB drive", (base / "absent").string(), (base / "absent" / "games").string()},
      {"locked", "Read-only drive", (base / "locked").string(), (base / "locked" / "playden" / "games").string()},
  };
  std::filesystem::permissions(base / "locked", std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);

  const auto volumes = DiscoverInstallVolumes(candidates);
  // The primary is always offered; a missing mount is skipped; an unwritable one is offered unavailable.
  CHECK(volumes.size() == 3);
  CHECK(volumes[0].id == "internal" && volumes[0].available);
  CHECK(volumes[1].id == "m2" && volumes[1].available);
  CHECK(std::filesystem::is_directory(base / "ext1" / "playden" / "games"));
  CHECK(!std::filesystem::exists(base / "ext1" / "playden" / "games" / ".playden-probe"));
  CHECK(volumes[2].id == "locked");
  if (::geteuid() != 0) CHECK(!volumes[2].available);  // root ignores the permission bits

  // A folder on the same filesystem as its parent is an empty mount point, not a drive:
  // it is skipped and nothing is created inside it.
  std::filesystem::create_directories(base / "unmounted");
  const auto unmounted = DiscoverInstallVolumes({
      candidates[0],
      {"extended", "Extended storage", (base / "unmounted").string(), (base / "unmounted" / "playden" / "games").string(), true},
  });
  CHECK(unmounted.size() == 1 && unmounted[0].id == "internal");
  CHECK(!std::filesystem::exists(base / "unmounted" / "playden"));
  // The filesystem root is a mount point on any host, so a required mount point there is offered.
  const auto mounted = DiscoverInstallVolumes({candidates[0], {"root", "Root", "/", (base / "root-games").string(), true}});
  CHECK(mounted.size() == 2 && mounted[1].id == "root");

  // The console candidates start with console storage under /app0; drives must be mounted.
  const auto console = ConsoleVolumeCandidates();
  CHECK(!console.empty() && console[0].id == InstallService::kPrimaryVolumeId && console[0].games_root == "/app0/games");
  CHECK(!console[0].require_mount_point);
  for (std::size_t index = 1; index < console.size(); ++index) CHECK(console[index].require_mount_point);

  const auto space = QueryVolumeSpace(base.string());
  CHECK(space && space->total_bytes > 0 && space->free_bytes <= space->total_bytes);
  CHECK(!QueryVolumeSpace((base / "absent").string()));

  std::filesystem::permissions(base / "locked", std::filesystem::perms::owner_all);
  std::filesystem::remove_all(base);
  std::puts("install volumes tests passed");
}
