#pragma once
// file_ops.h - Folder helpers for the self-update (POSIX only; the PS5 title has no umask, link or statfs).
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include <cstdint>
#include <string>
#include <vector>

namespace playden::update {

// Creates every missing folder of path (not path's last part when it names a file) with mode 0777.
bool MakeParents(const std::string& path);
bool MakeFolders(const std::string& path);
// Removes path and everything below it. Returns the number of entries removed; a missing path is 0.
std::uint64_t RemoveTree(const std::string& path);
// Names in a folder, without "." and "..", sorted. Empty when it can't be read.
std::vector<std::string> ListFolder(const std::string& path);
bool Exists(const std::string& path);
bool IsFolder(const std::string& path);
// Writes text to path through a temporary file and a rename.
bool WriteFileAtomically(const std::string& path, const std::string& text);
bool ReadSmallFile(const std::string& path, std::string* text, std::size_t limit = 1 << 20);

}  // namespace playden::update
