#pragma once
// release.h - GitHub release metadata and Playden's update manifest (playden-update.json).
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "update/version.h"

namespace playden::update {

inline constexpr const char* kUpdateManifestAsset = "playden-update.json";

struct ReleaseAsset {
  std::string name;
  std::string url;     // browser_download_url
  std::uint64_t size = 0;
  std::string sha256;  // from GitHub's "digest" ("sha256:<hex>"); empty when GitHub gave none
};

struct Release {
  std::string tag;
  std::string page;  // html_url: the release's GitHub page, when it is one
  Version version;
  bool draft = false;
  bool prerelease = false;
  std::vector<ReleaseAsset> assets;
  const ReleaseAsset* Find(std::string_view name) const;
};

// The JSON of GET /repos/<owner>/<repo>/releases/latest. The tag must be a version ("v0.3.0").
std::optional<Release> ParseRelease(std::string_view json, std::string* error);
// The JSON of GET /repos/<owner>/<repo>/releases: newest first, releases whose tag isn't a version left out.
std::optional<std::vector<Release>> ParseReleases(std::string_view json, std::string* error);

struct KitPart {
  std::string kit_id;           // stays the same across revisions ("hl2-fw1360-dev1")
  std::string revision;         // "r9"
  std::string manifest_sha256;  // the kit's manifest.json SHA-256
  std::string asset;            // release asset name
  std::string prefix;           // folder inside the zip ("playden-kit-0.12/hl2-fw1360-dev1/")
};

struct UpdateManifest {
  Version app_version;
  std::string title_id;
  Version min_from;   // oldest installed version that can update to this one
  std::string app_asset;
  std::string app_prefix;
  KitPart kit;
  std::string notes;  // short plain text
};

// playden-update.json, format 1. Names, folders and prefixes are checked for safety.
std::optional<UpdateManifest> ParseUpdateManifest(std::string_view json, std::string* error);

bool SafeFolderName(std::string_view name);
bool IsSha256(std::string_view text);

}  // namespace playden::update
