#include "steam/local_session_repository.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
using namespace playden;
int main() {
  const auto base=std::filesystem::temp_directory_path()/("playden-local-session-"+std::to_string(getpid()));
  std::filesystem::remove_all(base); std::filesystem::create_directories(base);
  const auto root=base/"data";
  LocalSessionRepository repo(root.string());
  assert(!std::filesystem::exists(root)); // Construction does no filesystem IO.
  auto result=repo.Load(); assert(result.ok() && !result.value());
  assert(std::filesystem::is_directory(root));
  assert(!std::filesystem::exists(root/"session-storage.probe"));
  StoredSession session{123,"synthetic-account","synthetic-refresh-secret","synthetic-access-secret",{}};
  assert(repo.Save(session).ok());
  struct stat info{}; assert(stat((root/"session.v1").c_str(),&info)==0 && (info.st_mode&0777)==0600);
  LocalSessionRepository restarted(root.string());
  result=restarted.Load(); assert(result.ok() && result.value()->steam_id==123);
  assert(restarted.Clear().ok() && !repo.Load().value());
  const auto bad=base/"file"; std::ofstream(bad)<<"not a directory";
  std::string diagnostic;
  LocalSessionRepository unavailable(bad.string(), [&](const std::string& text) { diagnostic = text; });
  assert(!unavailable.Load().ok());
  assert(diagnostic.find("stat") != std::string::npos);
  assert(!unavailable.Save(session).ok());
  const auto missing=base/"missing"/"child";
  LocalSessionRepository absent(missing.string()); assert(!absent.Load().ok());
  assert(absent.Clear().ok());
  assert(unavailable.Load().message().find("synthetic-refresh-secret")==std::string::npos);
  std::filesystem::remove_all(base);
}
