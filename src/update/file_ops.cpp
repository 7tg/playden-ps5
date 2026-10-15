#include "update/file_ops.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace playden::update {

bool MakeFolders(const std::string& path) {
  for (std::size_t i = 1; i <= path.size(); ++i) {
    if (i != path.size() && path[i] != '/') continue;
    const std::string part = path.substr(0, i);
    if (mkdir(part.c_str(), 0777) != 0 && errno != EEXIST) return false;
  }
  return IsFolder(path);
}

bool MakeParents(const std::string& path) {
  const auto slash = path.rfind('/');
  return slash == std::string::npos || slash == 0 || MakeFolders(path.substr(0, slash));
}

bool Exists(const std::string& path) {
  struct stat st{};
  return lstat(path.c_str(), &st) == 0;
}

bool IsFolder(const std::string& path) {
  struct stat st{};
  return lstat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::vector<std::string> ListFolder(const std::string& path) {
  std::vector<std::string> names;
  DIR* folder = opendir(path.c_str());
  if (!folder) return names;
  while (const dirent* entry = readdir(folder)) {
    if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
    names.emplace_back(entry->d_name);
  }
  closedir(folder);
  std::sort(names.begin(), names.end());
  return names;
}

std::uint64_t RemoveTree(const std::string& path) {
  struct stat st{};
  if (lstat(path.c_str(), &st) != 0) return 0;
  std::uint64_t removed = 0;
  if (S_ISDIR(st.st_mode)) {
    for (const auto& name : ListFolder(path)) removed += RemoveTree(path + "/" + name);
    if (rmdir(path.c_str()) == 0) ++removed;
  } else if (unlink(path.c_str()) == 0) {
    ++removed;
  }
  return removed;
}

bool WriteFileAtomically(const std::string& path, const std::string& text) {
  const std::string temporary = path + ".tmp";
  const int fd = open(temporary.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0666);
  if (fd < 0) return false;
  std::size_t written = 0;
  while (written < text.size()) {
    const ssize_t n = write(fd, text.data() + written, text.size() - written);
    if (n <= 0) { close(fd); unlink(temporary.c_str()); return false; }
    written += static_cast<std::size_t>(n);
  }
  fchmod(fd, 0666);
  const bool synced = fsync(fd) == 0;
  close(fd);
  if (!synced || rename(temporary.c_str(), path.c_str()) != 0) { unlink(temporary.c_str()); return false; }
  return true;
}

bool ReadSmallFile(const std::string& path, std::string* text, std::size_t limit) {
  const int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) return false;
  text->clear();
  char buffer[4096];
  ssize_t n;
  while ((n = read(fd, buffer, sizeof buffer)) > 0) {
    if (text->size() + static_cast<std::size_t>(n) > limit) { close(fd); return false; }
    text->append(buffer, static_cast<std::size_t>(n));
  }
  close(fd);
  return n == 0;
}

}  // namespace playden::update
