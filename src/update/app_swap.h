#pragma once
// app_swap.h - Replaces the installed app folder's entries with a staged copy, with undo.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include <functional>
#include <string>
#include <vector>

namespace playden::update {

// Top-level entries that hold the user's data; a swap never moves them.
const std::vector<std::string>& KeptAppEntries();

struct SwapResult {
  bool ok = false;
  std::string error;
  int moved = 0;  // entries moved in
};

using LogLine = std::function<void(const std::string&)>;

// Moves every staged top-level entry (except KeptAppEntries) into installed, one at a time and
// eboot.bin last. The installed entry of the same name moves to backup first; backup must not
// exist yet (it is created). The installed folder itself stays where it is. If any rename fails,
// the renames done so far are undone.
// Staged must hold eboot.bin and sce_sys.
SwapResult SwapApp(const std::string& installed, const std::string& staged, const std::string& backup,
                   const LogLine& log);

// Puts backup's entries back into installed (the installed entries of the same name move to
// displaced, which must not exist). Used to go back to the previous version.
SwapResult RestoreApp(const std::string& installed, const std::string& backup, const std::string& displaced,
                      const LogLine& log);

// After an interrupted SwapApp or RestoreApp (the process died part way): puts stash's entries
// back into installed, moving the ones that replaced them back to source. Entries that were new in
// the swap stay. Returns false when a rename failed.
bool UndoSwap(const std::string& installed, const std::string& source, const std::string& stash, const LogLine& log);

// Copies the Home-screen files from a new sce_sys folder over the copies that ShadowMount Plus
// made at registration (/user/appmeta/<id>/ and /user/app/<id>/sce_sys/, plus /user/app/<id>/icon0.png).
// Missing destinations are skipped. Returns false when a copy failed.
bool RefreshRegisteredCopies(const std::string& sce_sys, const std::string& appmeta, const std::string& app,
                             const LogLine& log);

}  // namespace playden::update
