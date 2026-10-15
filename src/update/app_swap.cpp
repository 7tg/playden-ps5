#include "update/app_swap.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>

#include "update/file_ops.h"

namespace playden::update {
namespace {

void Log(const LogLine& log, const std::string& line) {
  if (log) log(line);
}

bool Move(const std::string& from, const std::string& to, const LogLine& log) {
  if (rename(from.c_str(), to.c_str()) == 0) return true;
  Log(log, "swap: rename " + from + " -> " + to + " failed errno=" + std::to_string(errno));
  return false;
}

bool Kept(const std::string& name) {
  const auto& kept = KeptAppEntries();
  return std::find(kept.begin(), kept.end(), name) != kept.end();
}

// Moves source's entries into target, stashing target's same-named entries in stash.
SwapResult Exchange(const std::string& target, const std::string& source, const std::string& stash,
                    const LogLine& log) {
  SwapResult result;
  std::vector<std::string> names;
  for (const auto& name : ListFolder(source))
    if (!Kept(name)) names.push_back(name);
  if (names.empty()) {
    result.error = "nothing to move";
    return result;
  }
  if (Exists(stash)) {
    result.error = "backup folder already exists";
    return result;
  }
  if (!MakeFolders(stash)) {
    result.error = "cannot create backup folder";
    return result;
  }
  // One entry at a time (aside, then in), with eboot.bin last: an interruption leaves at most one
  // entry missing, and the old executable stays until everything else is in place, so the version
  // that starts tells whether the swap finished (UndoSwap puts an unfinished one back).
  std::stable_partition(names.begin(), names.end(), [](const std::string& name) { return name != "eboot.bin"; });
  struct Step {
    std::string name;
    bool stashed = false;
    bool moved = false;
  };
  std::vector<Step> steps;
  auto undo = [&](std::string reason) {
    for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
      if (it->moved) Move(target + "/" + it->name, source + "/" + it->name, log);
      if (it->stashed) Move(stash + "/" + it->name, target + "/" + it->name, log);
    }
    result.ok = false;
    result.moved = 0;
    result.error = std::move(reason);
    Log(log, "swap undone: " + result.error);
    return result;
  };
  for (const auto& name : names) {
    steps.push_back({name});
    if (Exists(target + "/" + name)) {
      if (!Move(target + "/" + name, stash + "/" + name, log)) return undo("cannot move " + name + " aside");
      steps.back().stashed = true;
    }
    if (!Move(source + "/" + name, target + "/" + name, log)) return undo("cannot move " + name + " in");
    steps.back().moved = true;
  }
  result.ok = true;
  result.moved = static_cast<int>(steps.size());
  return result;
}

bool CopyFile(const std::string& from, const std::string& to, const LogLine& log) {
  const int in = open(from.c_str(), O_RDONLY);
  if (in < 0) return false;
  const std::string temporary = to + ".playden-new";
  const int out = open(temporary.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0666);
  if (out < 0) {
    close(in);
    Log(log, "refresh: cannot create " + temporary + " errno=" + std::to_string(errno));
    return false;
  }
  auto buffer = std::make_unique<char[]>(1 << 16);
  bool ok = true;
  ssize_t n;
  while (ok && (n = read(in, buffer.get(), 1 << 16)) > 0) {
    ssize_t written = 0;
    while (written < n) {
      const ssize_t w = write(out, buffer.get() + written, static_cast<std::size_t>(n - written));
      if (w <= 0) { ok = false; break; }
      written += w;
    }
  }
  if (n < 0) ok = false;
  fchmod(out, 0666);
  close(in);
  close(out);
  if (!ok || rename(temporary.c_str(), to.c_str()) != 0) {
    unlink(temporary.c_str());
    Log(log, "refresh: cannot replace " + to);
    return false;
  }
  return true;
}

}  // namespace

const std::vector<std::string>& KeptAppEntries() {
  static const std::vector<std::string> kept = {"logs", "playden-data", "dev", "games", "playden-hui-comparison"};
  return kept;
}

SwapResult SwapApp(const std::string& installed, const std::string& staged, const std::string& backup,
                   const LogLine& log) {
  if (!IsFolder(installed) || !Exists(staged + "/eboot.bin") || !IsFolder(staged + "/sce_sys")) {
    SwapResult result;
    result.error = "staged app is incomplete";
    return result;
  }
  return Exchange(installed, staged, backup, log);
}

SwapResult RestoreApp(const std::string& installed, const std::string& backup, const std::string& displaced,
                      const LogLine& log) {
  if (!Exists(backup + "/eboot.bin")) {
    SwapResult result;
    result.error = "backup is incomplete";
    return result;
  }
  return Exchange(installed, backup, displaced, log);
}

bool UndoSwap(const std::string& installed, const std::string& source, const std::string& stash, const LogLine& log) {
  bool ok = MakeFolders(source);  // removed after a finished swap
  for (const auto& name : ListFolder(stash)) {
    const std::string here = installed + "/" + name;
    if (Exists(here)) {
      if (Exists(source + "/" + name) || !Move(here, source + "/" + name, log)) {
        ok = false;
        continue;
      }
    }
    if (!Move(stash + "/" + name, here, log)) ok = false;
  }
  Log(log, ok ? "unfinished swap undone" : "unfinished swap could not be fully undone");
  return ok;
}

bool RefreshRegisteredCopies(const std::string& sce_sys, const std::string& appmeta, const std::string& app,
                             const LogLine& log) {
  static const char* files[] = {"param.json", "icon0.png", "pic0.dds", "pic1.dds", "snd0.at9"};
  bool ok = true;
  for (const char* name : files) {
    const std::string source = sce_sys + "/" + name;
    if (!Exists(source)) continue;
    if (IsFolder(appmeta)) ok &= CopyFile(source, appmeta + "/" + name, log);
    if (IsFolder(app + "/sce_sys")) ok &= CopyFile(source, app + "/sce_sys/" + name, log);
  }
  if (IsFolder(app) && Exists(sce_sys + "/icon0.png")) ok &= CopyFile(sce_sys + "/icon0.png", app + "/icon0.png", log);
  return ok;
}

}  // namespace playden::update
