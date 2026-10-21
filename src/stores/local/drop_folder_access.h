#pragma once
// drop_folder_access.h - Opens the files of a game copied over FTP into the drop folder.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
//
// FTP writes as root: a game in /data/playden-import/<entry> has root-owned folders (0755) and
// files (0644), which the app user can read but a game can't rewrite (a shipped .ini, a save
// next to the program). Root can't bypass permissions on the console, but it can chmod what it
// owns, so the walk opens the entry's folders to 0777 and its files to 0666 (an executable keeps
// its x bits). It never changes owners and never leaves the entry:
// - only /data/playden-import/<one folder name> is walked; any other path is refused;
// - every step goes through platform/directory_walk.h, which never follows a link; calls the
//   console doesn't support (ENOSYS, EINVAL) refuse the walk, with no path fallback as root;
// - a link is left as it is, but one that leads outside the entry fails the walk, naming it. On
//   the console, where a title can't read a link (readlink is a libkernel_sys-only import), every
//   link fails the walk;
// - files with more than one link are left alone (their content may be another file's);
// - root is raised in short bursts (as_root: ps5::WithRoot, the process-wide root lock): only to
//   open an entry the app user can't, and to chmod a batch of root-owned entries. seteuid is per
//   process, so a file another thread creates while root is raised would belong to root.
// Resumable and idempotent: an interrupted walk is simply run again, and on an open tree it
// changes nothing and raises root not at all.
#include "runtime/writable_folders.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

namespace playden::stores::local {

inline constexpr const char* kDropFolder = "/data/playden-import";

struct DropFolderAccessOptions {
  runtime::WritableFoldersAsRoot as_root;  // empty: root-owned entries can't be opened
  std::function<void(const std::string&)> log;
  // Prepended to the console paths walked (a host test's namespace); messages name console paths.
  std::filesystem::path namespace_root = "/";
  // Entries this user owns are changed without root. Default: the effective UID at the call.
  std::optional<uid_t> app_uid;
  // Root-owned entries changed in one raise.
  std::size_t batch = 64;
};

struct DropFolderAccessResult {
  bool ok = false;
  std::string message;  // a sentence naming the folder, when not ok
  std::size_t folders = 0, files = 0;
  std::size_t changed = 0;      // modes changed
  std::size_t hard_linked = 0;  // files skipped: more than one link
  std::size_t links = 0;        // links left as they are (all inside the entry)
  std::size_t raises = 0;       // as_root calls
};

// Whether `console_path` is /data/playden-import/<name>, with one plain folder name.
bool IsDropFolderEntry(const std::string& console_path);

// Walks the entry (a console path, see IsDropFolderEntry). Run with no root held by this thread.
DropFolderAccessResult OpenDropFolderEntry(const std::string& entry, const DropFolderAccessOptions& options);

// Deleting a drop-folder game's files once a copy of it elsewhere is in Playden, so it isn't stored
// twice. Only the entry `name` (one plain folder name) directly inside `drop_folder` (the drop
// folder as this process opens it: /data/playden-import on the console) is walked, through
// platform/directory_walk.h, never following a link: a link is never followed nor deleted, and
// neither is a file `planned` doesn't name (relative to the entry, '/' separators, with the file's
// lstat: what the copy took, unchanged since). A folder still holding something then stays, and the
// result says so. Root is raised (as_root)
// only for one removal the app user may not do (FTP leaves root-owned folders). Run with no root
// held by this thread.
struct DropFolderRemoval {
  bool ok = false;      // the entry is gone
  std::string message;  // for the log, when not ok
  std::size_t files = 0, folders = 0;  // removed
  std::size_t kept = 0;                // entries left: links, files the copy didn't take
  std::size_t raises = 0;
};
using DropFolderPlanned = std::function<bool(const std::string& relative, const struct stat& info)>;
DropFolderRemoval RemoveDropFolderEntry(const std::string& drop_folder, const std::string& name,
                                        const DropFolderPlanned& planned,
                                        const runtime::WritableFoldersAsRoot& as_root,
                                        const std::function<void(const std::string&)>& log = {});

// OpenDropFolderEntry as a callback: the empty string, or the failure's sentence.
using DropFolderOpener = std::function<std::string(const std::string& entry)>;
DropFolderOpener MakeDropFolderOpener(DropFolderAccessOptions options);

}  // namespace playden::stores::local
