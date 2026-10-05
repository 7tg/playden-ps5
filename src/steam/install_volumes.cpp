#include "steam/install_volumes.h"

#include <sys/stat.h>
#if !PLAYDEN_PLATFORM_PS5
#include <sys/statvfs.h>
#endif

#include <cstdio>

namespace playden::steam {
namespace {

bool IsDirectory(const std::string& path) {
  struct stat info {};
  return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

// mkdir -p for an absolute path; existing components are fine.
bool MakeDirectories(const std::string& path) {
  if (path.empty()) return false;
  for (std::size_t slash = path.find('/', 1); slash != std::string::npos; slash = path.find('/', slash + 1)) {
    const auto prefix = path.substr(0, slash);
    if (!IsDirectory(prefix) && mkdir(prefix.c_str(), 0777) != 0 && !IsDirectory(prefix)) return false;
  }
  return IsDirectory(path) || mkdir(path.c_str(), 0777) == 0 || IsDirectory(path);
}

bool CanWrite(const std::string& directory) {
  const auto probe = directory + "/.playden-probe";
  FILE* file = std::fopen(probe.c_str(), "w");
  if (!file) return false;
  const bool written = std::fputs("ok\n", file) >= 0;
  const bool closed = std::fclose(file) == 0;
  std::remove(probe.c_str());
  return written && closed;
}

}  // namespace

std::vector<InstallVolume> DiscoverInstallVolumes(const std::vector<VolumeCandidate>& candidates) {
  std::vector<InstallVolume> volumes;
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    const auto& candidate = candidates[index];
    if (index == 0) {
      volumes.push_back({candidate.id, candidate.label, candidate.games_root, true});
      continue;
    }
    if (!IsDirectory(candidate.mount)) continue;
    const bool available = MakeDirectories(candidate.games_root) && CanWrite(candidate.games_root);
    volumes.push_back({candidate.id, candidate.label, candidate.games_root, available});
  }
  return volumes;
}

std::vector<VolumeCandidate> ConsoleVolumeCandidates(const std::string& title_root) {
  return {
      {InstallService::kPrimaryVolumeId, "Console storage", title_root, title_root + "/games"},
      {"m2", "M.2 SSD", "/mnt/ext1", "/mnt/ext1/playden/games"},
      {"extended", "Extended storage", "/mnt/ext0", "/mnt/ext0/playden/games"},
      {"usb0", "USB drive", "/mnt/usb0", "/mnt/usb0/playden/games"},
      {"usb1", "USB drive 2", "/mnt/usb1", "/mnt/usb1/playden/games"},
  };
}

std::optional<VolumeSpace> QueryVolumeSpace(const std::string& path) {
#if PLAYDEN_PLATFORM_PS5
  (void)path;
  return std::nullopt;
#else
  struct statvfs info {};
  if (statvfs(path.c_str(), &info) != 0) return std::nullopt;
  const auto unit = static_cast<std::uint64_t>(info.f_frsize ? info.f_frsize : info.f_bsize);
  return VolumeSpace{static_cast<std::uint64_t>(info.f_blocks) * unit, static_cast<std::uint64_t>(info.f_bavail) * unit};
#endif
}

}  // namespace playden::steam
