#include "runtime/prospero_profile.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace playden::runtime {
namespace {

bool HasControl(std::string_view text) {
  for (const unsigned char value : text)
    if (value < 0x20 || value == 0x7f) return true;
  return false;
}

std::string Backslashes(std::string text) {
  for (auto& value : text)
    if (value == '/') value = '\\';
  return text;
}

// "/data/x/games/220" -> "Z:\data\x\games\220"; prospero-win maps Z: to the console root.
std::optional<std::string> HostToWindows(std::string_view root) {
  while (root.size() > 1 && root.back() == '/') root.remove_suffix(1);
  if (root.size() < 2 || root.front() != '/' || HasControl(root) ||
      root.find_first_of(":\\") != std::string_view::npos)
    return std::nullopt;
  std::size_t start = 1;
  while (start <= root.size()) {
    const auto end = std::min(root.find('/', start), root.size());
    const auto segment = root.substr(start, end - start);
    if (segment.empty() || segment == "." || segment == "..") return std::nullopt;
    start = end + 1;
  }
  return "Z:" + Backslashes(std::string(root));
}

// Cuts at a UTF-8 boundary so a long title stays valid text.
std::string Truncate(std::string text, std::size_t limit) {
  if (text.size() <= limit) return text;
  std::size_t cut = limit;
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
  text.resize(cut);
  return text;
}

std::string CleanName(std::string_view raw, std::uint32_t app_id) {
  std::string name;
  for (const unsigned char value : raw) name.push_back(value < 0x20 || value == 0x7f ? ' ' : static_cast<char>(value));
  name = Truncate(std::move(name), kProsperoMaxNameBytes);
  const auto first = name.find_first_not_of(' ');
  if (first == std::string::npos) return "Steam app " + std::to_string(app_id);
  name = name.substr(first, name.find_last_not_of(' ') - first + 1);
  return name;
}

bool WriteAtomically(const std::string& path, const std::string& text) {
  const auto temporary = path + ".tmp";
  FILE* file = std::fopen(temporary.c_str(), "w");
  if (!file) return false;
  const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
  if (std::fclose(file) != 0 || !written) {
    std::remove(temporary.c_str());
    return false;
  }
  return std::rename(temporary.c_str(), path.c_str()) == 0;
}

std::string_view Trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
  return text;
}

}  // namespace

std::optional<PeArchitecture> ParsePeArchitecture(const std::uint8_t* bytes, std::size_t size) {
  if (!bytes || size < 0x40 || bytes[0] != 'M' || bytes[1] != 'Z') return std::nullopt;
  const std::uint32_t offset = bytes[0x3c] | bytes[0x3d] << 8 | bytes[0x3e] << 16 |
                               static_cast<std::uint32_t>(bytes[0x3f]) << 24;
  if (offset > size || size - offset < 6) return std::nullopt;
  const auto* header = bytes + offset;
  if (header[0] != 'P' || header[1] != 'E' || header[2] != 0 || header[3] != 0) return std::nullopt;
  const unsigned machine = header[4] | header[5] << 8;
  if (machine == 0x014c) return PeArchitecture::kPe32;
  if (machine == 0x8664) return PeArchitecture::kPe64;
  return std::nullopt;
}

std::optional<PeArchitecture> ReadPeArchitecture(const std::string& path) {
  FILE* file = std::fopen(path.c_str(), "rb");
  if (!file) return std::nullopt;
  // The PE header follows the DOS stub, normally within the first few hundred bytes.
  std::vector<std::uint8_t> head(4096);
  head.resize(std::fread(head.data(), 1, head.size(), file));
  std::fclose(file);
  return ParsePeArchitecture(head.data(), head.size());
}

std::string ProsperoProfileId(std::uint32_t app_id) { return "playden-steam-" + std::to_string(app_id); }

bool DraftProsperoProfile(const ProsperoGame& game, ProsperoProfile* profile, std::string* reason) {
  const auto fail = [&](const char* why) {
    if (reason) *reason = why;
    return false;
  };
  if (game.app_id == 0) return fail("missing app id");
  const auto root = HostToWindows(game.install_root);
  if (!root) return fail("install folder cannot be mapped for Wine");
  const auto executable = steam::RelativeGamePath(game.launch.executable);
  if (!executable) return fail("launch executable is not a game path");
  std::string working;
  if (game.launch.working_directory.empty()) {
    const auto slash = executable->rfind('/');
    working = slash == std::string::npos ? std::string() : executable->substr(0, slash);
  } else {
    const auto relative = steam::RelativeGamePath(game.launch.working_directory, true);
    if (!relative) return fail("launch working folder is not a game path");
    working = *relative;
  }
  if (HasControl(game.launch.arguments) || !steam::ParseWindowsArguments(game.launch.arguments))
    return fail("launch arguments are malformed");
  if (game.launch.arguments.size() > kProsperoMaxArgumentBytes) return fail("launch arguments are too long");

  const auto executable_path = *root + "\\" + Backslashes(*executable);
  const auto working_path = working.empty() ? *root : *root + "\\" + Backslashes(working);
  if (executable_path.size() > kProsperoMaxPathBytes || working_path.size() > kProsperoMaxPathBytes)
    return fail("install path is too long for prospero-win");
  if (game.prefix.empty() || game.prefix.size() > kProsperoMaxIdBytes ||
      game.prefix.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_") != std::string::npos ||
      game.prefix.front() == '-' || game.prefix.front() == '_')
    return fail("invalid prefix name");

  ProsperoProfile drafted;
  drafted.id = ProsperoProfileId(game.app_id);
  std::string text = "; Drafted by Playden from Steam launch data.\n[application]\n";
  text += "id = " + drafted.id + "\n";
  text += "name = " + CleanName(game.name, game.app_id) + "\n";
  text += "executable = " + executable_path + "\n";
  text += "working_directory = " + working_path + "\n";
  if (!game.launch.arguments.empty()) text += "arguments = " + game.launch.arguments + "\n";
  text += "prefix = " + game.prefix + "\n";
  text += "runtime = wine-wow64\n";
  text += std::string("architecture = ") + (game.architecture == PeArchitecture::kPe64 ? "pe64" : "pe32") + "\n";
  text += "graphics = auto\n";
  if (text.size() > kProsperoMaxProfileBytes) return fail("profile is too large");
  drafted.text = std::move(text);
  *profile = std::move(drafted);
  return true;
}

bool PublishProsperoProfile(const std::string& prospero_root, const ProsperoProfile& profile, std::string* reason) {
  const auto fail = [&](const char* why) {
    if (reason) *reason = why;
    return false;
  };
  const auto directory = prospero_root + "/profiles";
  mkdir(prospero_root.c_str(), 0777);
  mkdir(directory.c_str(), 0777);
  const auto entry = profile.id + ".profile";
  if (!WriteAtomically(directory + "/" + entry, profile.text)) return fail("cannot write profile");

  const auto catalog_path = directory + "/profiles.lst";
  std::string catalog;
  if (FILE* file = std::fopen(catalog_path.c_str(), "r")) {
    char buffer[1024];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0 && catalog.size() < 64 * 1024)
      catalog.append(buffer, read);
    std::fclose(file);
  }
  std::size_t entries = 0;
  std::size_t start = 0;
  while (start < catalog.size()) {
    const auto end = std::min(catalog.find('\n', start), catalog.size());
    const auto line = Trim(std::string_view(catalog).substr(start, end - start));
    if (!line.empty() && line.front() != '#' && line.front() != ';') {
      if (line == entry) return true;
      ++entries;
    }
    start = end + 1;
  }
  if (entries >= kProsperoMaxCatalogEntries) return fail("prospero-win profile list is full");
  if (!catalog.empty() && catalog.back() != '\n') catalog += '\n';
  catalog += entry + "\n";
  if (!WriteAtomically(catalog_path, catalog)) return fail("cannot update profile list");
  return true;
}

}  // namespace playden::runtime
