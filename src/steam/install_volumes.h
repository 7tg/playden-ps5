#pragma once

#include "steam/install_service.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace playden::steam {

// A drive Playden may install to. `mount` must exist for the candidate to be offered;
// `games_root` is created under it.
struct VolumeCandidate {
  std::string id;
  std::string label;
  std::string mount;
  std::string games_root;
};

// Candidates whose mount exists, with `available` set when Playden could create games_root and
// write a probe file there. The first candidate is the primary volume and is always returned,
// marked available (its folder is created on first install).
std::vector<InstallVolume> DiscoverInstallVolumes(const std::vector<VolumeCandidate>& candidates);

// Candidate drives for the PS5 title: console storage under /app0, then extended storage and
// USB drives if the title can see them.
std::vector<VolumeCandidate> ConsoleVolumeCandidates();

struct VolumeSpace {
  std::uint64_t total_bytes = 0;
  std::uint64_t free_bytes = 0;
};

// Total and free bytes for the drive holding `path`, or nothing when the platform cannot report
// them. On the PS5 this always returns nothing: statfs/statvfs terminate the title there.
std::optional<VolumeSpace> QueryVolumeSpace(const std::string& path);

}  // namespace playden::steam
