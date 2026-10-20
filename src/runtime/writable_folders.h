#pragma once
// writable_folders.h - Checks (and fixes) the folders Playden writes into before it writes.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace playden::runtime {

// Runs work with root raised (ps5::WithRoot on the console); false when root could not be raised
// or the work failed. Empty: nothing can be fixed.
using WritableFoldersAsRoot = std::function<bool(const std::function<bool()>& work)>;

// Folders made over FTP belong to root and are often 0755, so the app user can't write into them.
// For each existing folder (missing ones and links are skipped), a probe file is created and
// removed as the app user. Only fixed, approved shared roots may be repaired as root (root owns them, so no
// permission bypass is needed) and probed again. Returns an empty string when every folder can be
// written, or a message naming the first folder that still can't. Each fix and failure is logged.
std::string EnsureWritableFolders(const std::vector<std::filesystem::path>& folders,
                                  const WritableFoldersAsRoot& as_root,
                                  const std::function<void(const std::string&)>& log,
                                  const std::filesystem::path& namespace_root = "/");

// Single-threaded startup, after the grant and before the first runtime write check.
std::string PrepareRuntimeRoot(const std::filesystem::path& root, const WritableFoldersAsRoot& as_root,
                               const std::function<void(const std::string&)>& log,
                               const std::filesystem::path& namespace_root = "/");

}  // namespace playden::runtime
