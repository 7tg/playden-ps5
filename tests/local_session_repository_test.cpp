#include "steam/local_session_repository.h"
#include <cassert>
#include <atomic>
#include <thread>
#include <vector>
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
  // Authentication, library, details, installs and Cloud each own a repository
  // for the same root. Their first reads must not race on the storage probe.
  std::atomic<bool> start{false};
  std::atomic<int> failed_loads{0};
  std::vector<std::thread> readers;
  for (int worker=0;worker<5;++worker) readers.emplace_back([&] {
    while (!start.load()) std::this_thread::yield();
    for (int attempt=0;attempt<100;++attempt) {
      LocalSessionRepository concurrent(root.string());
      const auto loaded=concurrent.Load();
      if (!loaded.ok() || !loaded.value() || loaded.value()->steam_id!=123) ++failed_loads;
    }
  });
  start=true;
  for (auto& reader:readers) reader.join();
  assert(failed_loads==0 && "concurrent repositories must restore the saved session");
  for (const auto& entry:std::filesystem::directory_iterator(root))
    assert(entry.path().filename()=="session.v1" && "storage probes must be removed");
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

  // "Stay signed in" off: the session lives in the vault every repository of the run shares.
  const auto vault_root=base/"vault";
  std::filesystem::create_directories(vault_root);
  const auto saved_file=vault_root/"session.v1";
  {
    LocalSessionRepository writer(vault_root.string());
    assert(writer.Save(session).ok() && std::filesystem::exists(saved_file));
  }
  SessionVault vault(false);
  LocalSessionRepository auth(vault_root.string(),{},&vault);
  LocalSessionRepository library(vault_root.string(),{},&vault);
  result=library.Load(); assert(result.ok() && !result.value() && "off never reads session.v1");
  StoredSession other{456,"synthetic-other","synthetic-other-refresh","synthetic-other-access",7u};
  assert(auth.Save(other).ok());
  assert(!std::filesystem::exists(saved_file) && "off deletes session.v1 and writes none");
  result=library.Load(); assert(result.ok() && result.value()->steam_id==456 && result.value()->cell_id==7u);
  {
    // A new process (a new vault) finds nothing.
    SessionVault rebooted(false);
    LocalSessionRepository after(vault_root.string(),{},&rebooted);
    assert(after.Load().ok() && !after.Load().value());
  }
  // Turning it on saves the session the vault holds; the file then serves new runs.
  assert(auth.SetPersist(true).ok() && vault.persist() && std::filesystem::exists(saved_file));
  {
    LocalSessionRepository fresh(vault_root.string());
    result=fresh.Load(); assert(result.ok() && result.value() && result.value()->steam_id==456);
  }
  // On: Save writes the file, as without a vault.
  assert(auth.Save(session).ok());
  result=LocalSessionRepository(vault_root.string()).Load(); assert(result.ok() && result.value()->steam_id==123);
  result=library.Load(); assert(result.ok() && result.value()->steam_id==123);
  // Off again while signed in: the saved session moves into memory and the file goes.
  assert(library.SetPersist(false).ok() && !vault.persist() && !std::filesystem::exists(saved_file));
  result=auth.Load(); assert(result.ok() && result.value() && result.value()->steam_id==123);
  // Sign-out clears memory and disk, whichever repository asks.
  { LocalSessionRepository writer(vault_root.string()); assert(writer.Save(other).ok()); }
  assert(library.Clear().ok() && !std::filesystem::exists(saved_file));
  result=auth.Load(); assert(result.ok() && !result.value());
  // Off and session.v1 can't be deleted: the sign-in fails and nothing stays in memory.
  {
    LocalSessionRepository writer(vault_root.string()); assert(writer.Save(session).ok());
    chmod(vault_root.c_str(),0500);
    if (unlink(saved_file.c_str())!=0) {  // root ignores the mode; nothing to check then
      assert(!auth.Save(other).ok());
      result=auth.Load(); assert(result.ok() && !result.value());
    }
    chmod(vault_root.c_str(),0700);
  }
  // Many workers read and write the shared vault at once.
  std::atomic<bool> go{false};
  std::atomic<int> bad_loads{0};
  std::vector<std::thread> workers;
  for (int worker=0;worker<4;++worker) workers.emplace_back([&,worker] {
    LocalSessionRepository mine(vault_root.string(),{},&vault);
    while (!go.load()) std::this_thread::yield();
    for (int attempt=0;attempt<200;++attempt) {
      if (worker==0) { assert(mine.Save(attempt%2 ? session : other).ok()); continue; }
      const auto loaded=mine.Load();
      if (!loaded.ok() || (loaded.value() && loaded.value()->steam_id!=123 && loaded.value()->steam_id!=456)) ++bad_loads;
    }
  });
  go=true;
  for (auto& worker:workers) worker.join();
  assert(bad_loads==0 && !std::filesystem::exists(saved_file));
  std::filesystem::remove_all(base);
}
