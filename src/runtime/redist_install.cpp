#include "runtime/redist_install.h"

#include "platform/managed_directory.h"
#include "runtime/artifact_hash.h"
#include "runtime/game_settings.h"
#include "runtime/msi_package.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <map>
#include <set>
#include <unistd.h>

namespace playden::runtime {
namespace {

using platform::DirectoryPolicy;
using platform::ManagedDirectory;

constexpr std::size_t kMaxPackageBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxScripts = 32;
constexpr int kMaxDepth = 4;
constexpr const char* kStateFile = ".playden-redist";
constexpr const char* kEnvironmentKey = "System\\CurrentControlSet\\Control\\Session Manager\\Environment";

std::string Lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char value) { return static_cast<char>(value >= 'A' && value <= 'Z' ? value + 32 : value); });
  return out;
}

bool ReadFile(const std::filesystem::path& path, std::size_t maximum, std::string* text) {
  text->clear();
  const int file = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (file < 0) return false;
  bool ok = true;
  // Large packages: read straight into the string, not through a stack buffer (small worker stacks).
  for (std::size_t chunk = 1u << 20;;) {
    const auto size = text->size();
    text->resize(size + chunk);
    const auto count = ::read(file, &(*text)[size], chunk);
    if (count < 0 && errno == EINTR) {
      text->resize(size);
      continue;
    }
    if (count < 0) { ok = false; text->resize(size); break; }
    text->resize(size + static_cast<std::size_t>(count));
    if (count == 0) break;
    if (text->size() > maximum) { ok = false; break; }
  }
  ::close(file);
  return ok;
}

// Folders where Steam games keep their redistributables, looked into below the game's top level.
bool RedistFolder(const std::string& name) {
  static const std::set<std::string> kNames = {
      "_commonredist", "commonredist", "redist", "redists", "_redist", "redistributables", "redistributable",
      "support", "__installer", "_installer", "installer", "installers", "prerequisites", "prereqs", "physx",
      "directx", "vcredist", "dotnet"};
  return kNames.count(Lower(name)) != 0;
}

bool ScriptName(const std::string& name) {
  const auto lower = Lower(name);
  return lower.rfind("installscript", 0) == 0 && lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".vdf") == 0;
}

void FindScripts(const std::filesystem::path& folder, const std::string& relative, int depth,
                 std::vector<std::string>* scripts) {
  std::error_code error;
  std::vector<std::pair<std::string, bool>> children;
  for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
    std::error_code type_error;
    if (it->is_symlink(type_error)) continue;
    children.emplace_back(it->path().filename().string(), it->is_directory(type_error));
  }
  std::sort(children.begin(), children.end());
  for (const auto& [name, directory] : children) {
    if (scripts->size() >= kMaxScripts) return;
    const auto path = relative.empty() ? name : relative + "/" + name;
    if (!directory && ScriptName(name) && depth > 0) scripts->push_back(path);
    else if (directory && depth < kMaxDepth && (depth > 0 || RedistFolder(name)))
      FindScripts(folder / name, path, depth + 1, scripts);
  }
}

// Splits a command line into arguments (double quotes group, as Windows does for msiexec).
std::vector<std::string> Arguments(std::string_view command) {
  std::vector<std::string> out;
  std::string current;
  bool quoted = false, any = false;
  for (const char value : command) {
    if (value == '"') {
      quoted = !quoted;
      any = true;
    } else if ((value == ' ' || value == '\t') && !quoted) {
      if (any) out.push_back(current);
      current.clear();
      any = false;
    } else {
      current += value;
      any = true;
    }
  }
  if (any) out.push_back(current);
  return out;
}

std::string FileName(std::string path) {
  std::replace(path.begin(), path.end(), '/', '\\');
  const auto slash = path.rfind('\\');
  return Lower(slash == std::string::npos ? path : path.substr(slash + 1));
}

// A Windows path inside the game folder, relative to it with '/' separators.
bool InsideGame(const std::string& windows_path, const std::string& install_dir, std::string* relative) {
  auto base = install_dir;
  while (!base.empty() && base.back() == '\\') base.pop_back();
  if (windows_path.size() <= base.size() + 1 || Lower(windows_path.substr(0, base.size())) != Lower(base) ||
      windows_path[base.size()] != '\\')
    return false;
  auto rest = windows_path.substr(base.size() + 1);
  std::replace(rest.begin(), rest.end(), '\\', '/');
  if (rest.empty() || rest.find("..") != std::string::npos) return false;
  *relative = rest;
  return true;
}

// Opens (creating when asked) a folder below root, matching existing names without regard to
// case as Windows does, so a package's "Program Files (x86)" lands in the bottle's own.
std::optional<ManagedDirectory> OpenPath(const std::filesystem::path& root, const std::string& relative, bool create) {
  auto directory = ManagedDirectory::Open(root, DirectoryPolicy::kApprovedTree);
  if (!directory) return std::nullopt;
  for (std::size_t start = 0; start < relative.size();) {
    const auto slash = relative.find('/', start);
    auto name = relative.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    start = slash == std::string::npos ? relative.size() : slash + 1;
    if (name.empty()) continue;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory->path(), error), end; !error && it != end; it.increment(error)) {
      const auto existing = it->path().filename().string();
      if (Lower(existing) == Lower(name)) {
        name = existing;
        break;
      }
    }
    auto next = directory->OpenChild(name, DirectoryPolicy::kApprovedTree, create);
    if (!next) return std::nullopt;
    directory = std::move(next);
  }
  return directory;
}

bool WriteFileAt(const ManagedDirectory& directory, const std::string& name, std::string_view data) {
  struct stat existing {};
  std::string target = name;
  if (!directory.InspectChild(name, &existing)) {
    if (errno != ENOENT) return false;
    // Reuse a differently cased file of the same name rather than adding a second one.
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory.path(), error), end; !error && it != end; it.increment(error))
      if (Lower(it->path().filename().string()) == Lower(name)) target = it->path().filename().string();
  } else if (!S_ISREG(existing.st_mode)) {
    return false;
  }
  const auto temporary = target + ".playden-tmp-" + std::to_string(static_cast<long long>(::getpid())) + "-" +
                         std::to_string(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
  const int file = directory.CreateExclusiveFile(temporary, 0644);
  if (file < 0) return false;
  std::size_t written = 0;
  while (written < data.size()) {
    const auto count = ::write(file, data.data() + written, data.size() - written);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) break;
    written += static_cast<std::size_t>(count);
  }
  const bool closed = ::close(file) == 0;
  if (written != data.size() || !closed || !directory.RenameChild(temporary, directory, target, true)) {
    directory.RemoveRegularFile(temporary);
    return false;
  }
  return true;
}

// The machine PATH as system.reg holds it (REG_EXPAND_SZ, Wine's text form), or Wine's default.
std::string MachinePath(const std::filesystem::path& prefix) {
  static const std::string kDefault =
      "%SystemRoot%\\system32;%SystemRoot%;%SystemRoot%\\system32\\wbem;%SystemRoot%\\system32\\WindowsPowershell\\v1.0";
  std::string text;
  if (!ReadFile(prefix / "system.reg", 64u * 1024u * 1024u, &text)) return kDefault;
  const auto header = Lower("[System\\\\CurrentControlSet\\\\Control\\\\Session Manager\\\\Environment]");
  const auto lower = Lower(text);
  auto at = lower.find(header);
  if (at == std::string::npos) return kDefault;
  const auto section_end = lower.find("\n[", at + 1);
  at = lower.find("\n\"path\"=", at);
  if (at == std::string::npos || (section_end != std::string::npos && at > section_end)) return kDefault;
  at = text.find('=', at) + 1;
  if (text.compare(at, 7, "str(2):") == 0) at += 7;
  if (at >= text.size() || text[at] != '"') return kDefault;
  std::string value;
  for (++at; at < text.size() && text[at] != '"' && text[at] != '\n'; ++at) {
    if (text[at] == '\\' && at + 1 < text.size()) ++at;
    value += text[at];
  }
  return value;
}

std::string WineString(std::string_view text) {
  std::string out = "\"";
  for (const char value : text) {
    if (value == '\\' || value == '"') out += '\\';
    out += value;
  }
  return out + "\"";
}

std::map<std::string, std::string> InstalledPackages(const std::filesystem::path& prefix) {
  std::map<std::string, std::string> out;
  std::string text;
  if (!ReadFile(prefix / kStateFile, 1024 * 1024, &text)) return out;
  for (std::size_t at = 0; at < text.size();) {
    auto end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    const auto line = text.substr(at, end - at);
    at = end + 1;
    const auto space = line.find(' ');
    if (space == 64) out[line.substr(0, space)] = line.substr(space + 1);
  }
  return out;
}

bool RecordPackage(const std::filesystem::path& prefix, const std::string& sha256, const std::string& package) {
  auto packages = InstalledPackages(prefix);
  packages[sha256] = package;
  std::string text;
  for (const auto& [hash, name] : packages) text += hash + " " + name + "\n";
  auto directory = ManagedDirectory::Open(prefix, DirectoryPolicy::kApprovedTree);
  return directory && WriteFileAt(*directory, kStateFile, text);
}

// Installs one package; returns the number of files written, or a reason in *reason.
bool InstallPackage(const std::filesystem::path& package_path, const std::filesystem::path& prefix,
                    const RedistEntry& entry, std::int64_t unix_time, std::size_t* files, std::string* reason) {
  std::string package;
  if (!ReadFile(package_path, kMaxPackageBytes, &package)) {
    *reason = "the package could not be read";
    return false;
  }
  MsiInstallPlan plan;
  if (!ReadMsiPackage(package, &plan, reason)) return false;
  std::map<std::string, std::vector<const MsiFile*>> wanted;  // member → files
  for (const auto& file : plan.files) wanted[file.member].push_back(&file);
  const auto drive = prefix / "drive_c";
  for (std::size_t index = 0; index < plan.cabinets.size(); ++index) {
    const auto& cabinet = plan.cabinets[index];
    std::string bytes;
    if (cabinet.embedded) {
      auto stream = ReadMsiStream(package, cabinet.name);
      if (!stream) {
        *reason = "the package's cabinet " + cabinet.name + " is missing";
        return false;
      }
      bytes = std::move(*stream);
    } else if (cabinet.name.find('/') != std::string::npos || cabinet.name.find('\\') != std::string::npos ||
               !ReadFile(package_path.parent_path() / cabinet.name, kMaxPackageBytes, &bytes)) {
      *reason = "the cabinet " + cabinet.name + " beside the package could not be read";
      return false;
    }
    std::string failure;
    const bool ok = ExtractCabinet(bytes, [&](const std::string& member, std::string_view data) {
      const auto found = wanted.find(member);
      if (found == wanted.end()) return true;
      for (const auto* file : found->second) {
        if (file->cabinet != index) continue;
        const auto slash = file->target.rfind('/');
        const auto folder = slash == std::string::npos ? std::string() : file->target.substr(0, slash);
        const auto name = slash == std::string::npos ? file->target : file->target.substr(slash + 1);
        const auto directory = OpenPath(drive, folder, true);
        if (!directory || !WriteFileAt(*directory, name, data)) {
          failure = "could not write C:\\" + file->target;
          return false;
        }
        ++*files;
      }
      return true;
    }, reason);
    if (!ok) {
      if (!failure.empty()) *reason = failure;
      return false;
    }
  }
  auto machine = plan.machine;
  if (!plan.path_prepend.empty() || !plan.path_append.empty()) {
    const auto current = MachinePath(prefix);
    const auto extended = ExtendPath(current, plan.path_prepend, plan.path_append);
    if (extended != current) machine.push_back({kEnvironmentKey, "PATH", "str(2):" + WineString(extended)});
  }
  // Steam marks the entry as run under its HasRunKey (a DWORD named after the entry).
  const auto run_key = Lower(entry.has_run_key);
  if (run_key.rfind("hkey_local_machine\\software\\", 0) == 0 && run_key.rfind("hkey_local_machine\\software\\wow6432node\\", 0) != 0)
    machine.push_back({"Software\\Wow6432Node\\" + entry.has_run_key.substr(28), entry.name, "dword:00000001"});
  for (const auto& [hive, values] : {std::pair<const char*, const std::vector<RegistryValue>*>{"system.reg", &machine},
                                     std::pair<const char*, const std::vector<RegistryValue>*>{"user.reg", &plan.user}}) {
    if (!values->empty() && !ApplyBottleRegistryValues(*values, prefix.string(), unix_time, hive, reason)) return false;
  }
  return true;
}

}  // namespace

RedistAction ClassifyRedist(const RedistEntry& entry, const std::string& install_dir, std::string* package) {
  const auto program = FileName(entry.process);
  if (program == "msiexec" || program == "msiexec.exe") {
    const auto arguments = Arguments(entry.command);
    for (std::size_t index = 0; index + 1 < arguments.size(); ++index) {
      const auto flag = Lower(arguments[index]);
      if ((flag == "/i" || flag == "-i" || flag == "/package" || flag == "-package") &&
          Lower(arguments[index + 1]).size() > 4 &&
          Lower(arguments[index + 1]).compare(arguments[index + 1].size() - 4, 4, ".msi") == 0 &&
          InsideGame(arguments[index + 1], install_dir, package))
        return RedistAction::kMsi;
    }
    return RedistAction::kNotRun;
  }
  if (program.size() > 4 && program.compare(program.size() - 4, 4, ".msi") == 0 &&
      InsideGame(entry.process, install_dir, package))
    return RedistAction::kMsi;
  // Installers whose DLLs and frameworks Wine and the kit already provide.
  static constexpr const char* kCovered[] = {"vcredist", "vc_redist", "dxsetup", "dxwebsetup", "d3d11install",
                                             "dotnetfx", "dotnetfx35", "ndp", "netfx", "xnafx"};
  for (const auto* prefix : kCovered)
    if (program.rfind(prefix, 0) == 0) return RedistAction::kCovered;
  return RedistAction::kNotRun;
}

std::string ExtendPath(const std::string& current, const std::vector<std::string>& prepend,
                       const std::vector<std::string>& append) {
  std::vector<std::string> entries;
  for (std::size_t at = 0; at <= current.size();) {
    auto end = current.find(';', at);
    if (end == std::string::npos) end = current.size();
    if (end > at) entries.push_back(current.substr(at, end - at));
    at = end + 1;
  }
  const auto listed = [&](const std::string& folder) {
    auto wanted = Lower(folder);
    while (!wanted.empty() && wanted.back() == '\\') wanted.pop_back();
    return std::any_of(entries.begin(), entries.end(), [&](const std::string& existing) {
      auto have = Lower(existing);
      while (!have.empty() && have.back() == '\\') have.pop_back();
      return have == wanted;
    });
  };
  std::vector<std::string> front;
  for (const auto& folder : prepend)
    if (!listed(folder)) front.push_back(folder);
  entries.insert(entries.begin(), front.begin(), front.end());
  for (const auto& folder : append)
    if (!listed(folder)) entries.push_back(folder);
  std::string out;
  for (const auto& folder : entries) out += (out.empty() ? "" : ";") + folder;
  return out;
}

RedistInstallResult InstallRedistributables(const std::string& game_root, const std::string& install_dir,
                                            const std::string& prefix_dir, std::int64_t unix_time) {
  RedistInstallResult result;
  std::vector<std::string> scripts;
  {
    // The top level first (its scripts' registry values are ApplyInstallScripts' job too).
    std::error_code error;
    std::vector<std::string> top;
    for (std::filesystem::directory_iterator it(game_root, error), end; !error && it != end; it.increment(error)) {
      std::error_code type_error;
      if (ScriptName(it->path().filename().string()) && it->is_regular_file(type_error) && !it->is_symlink(type_error))
        top.push_back(it->path().filename().string());
    }
    std::sort(top.begin(), top.end());
    scripts = top;
    FindScripts(game_root, std::string(), 0, &scripts);
    if (scripts.size() > kMaxScripts) scripts.resize(kMaxScripts);
  }
  const std::filesystem::path prefix(prefix_dir);
  auto installed = InstalledPackages(prefix);
  std::set<std::string> seen;  // a package two scripts run is installed once
  for (const auto& script : scripts) {
    std::string text, reason;
    std::vector<RedistEntry> entries;
    if (!ReadInstallScript((std::filesystem::path(game_root) / script).string(), &text) ||
        !ParseRunProcess(text, install_dir, &entries, &reason))
      continue;
    for (const auto& entry : entries) {
      RedistOutcome outcome;
      outcome.script = script;
      outcome.name = entry.name;
      std::string package;
      const auto action = ClassifyRedist(entry, install_dir, &package);
      if (action != RedistAction::kMsi) {
        outcome.result = action == RedistAction::kCovered ? "covered" : "not-run";
        outcome.detail = FileName(entry.process);
        result.outcomes.push_back(std::move(outcome));
        continue;
      }
      outcome.detail = package;
      const auto path = std::filesystem::path(game_root) / package;
      const auto hash = HashArtifactFile(path);
      if (!hash) {
        outcome.result = "failed";
        outcome.detail = package + ": the package could not be read";
        result.outcomes.push_back(std::move(outcome));
        continue;
      }
      if (installed.count(*hash) || !seen.insert(*hash).second) {
        outcome.result = "already";
        result.outcomes.push_back(std::move(outcome));
        continue;
      }
      std::string failure;
      if (!InstallPackage(path, prefix, entry, unix_time, &outcome.files, &failure)) {
        outcome.result = "failed";
        outcome.detail = package + ": " + failure;
      } else if (!RecordPackage(prefix, *hash, package)) {
        outcome.result = "failed";
        outcome.detail = package + ": installed, but could not be recorded";
      } else {
        outcome.result = "installed";
        installed[*hash] = package;
      }
      result.outcomes.push_back(std::move(outcome));
    }
  }
  return result;
}

}  // namespace playden::runtime
