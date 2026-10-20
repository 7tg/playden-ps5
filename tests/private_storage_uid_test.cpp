#include "platform/storage.h"
#include "platform/ps5/title_allow_list.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

// Model the title's process-wide credentials without requiring host root.
static std::atomic<uid_t> effective_uid{getuid()};
extern "C" uid_t geteuid() { return effective_uid.load(); }
extern "C" int seteuid(uid_t user) { effective_uid.store(user); return 0; }
static std::atomic<bool> wrong_owner{false};
static std::atomic<unsigned> owner_retries{0};
extern "C" int fstat(int fd, struct stat* info) {
  using Stat = int (*)(int, struct stat*);
  static const auto real_stat = reinterpret_cast<Stat>(dlsym(RTLD_NEXT, "fstat"));
  const int result = real_stat(fd, info);
  if (result == 0 && info->st_size > 0 && wrong_owner.exchange(false)) {
    info->st_uid = getuid() + 1;
    ++owner_retries;
  }
  return result;
}
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #x); std::exit(1); } } while (0)

int main() {
  char scratch[] = "/tmp/playden-private-uid-XXXXXX";
  CHECK(mkdtemp(scratch));
  const auto root = std::filesystem::canonical(scratch);
  playden::Storage storage(root.string());
  std::promise<void> raised, release, saving;
  auto release_root = release.get_future();
  auto root_work = std::async(std::launch::async, [&] {
    return playden::ps5::WithRoot([&] {
      CHECK(geteuid() == 0);
      raised.set_value();
      release_root.wait();
      return true;
    });
  });
  raised.get_future().wait();
  auto save = std::async(std::launch::async, [&] {
    saving.set_value();
    return storage.WriteAtomicPrivate("session.v1", {'n', 'e', 'w'});
  });
  saving.get_future().wait();
  const bool blocked = save.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout;
  const bool absent = !std::filesystem::exists(root / "session.v1.tmp") && !std::filesystem::exists(root / "session.v1");
  release.set_value();
  CHECK(root_work.get());
  CHECK(save.get().ok());
  CHECK(blocked && absent && geteuid() == getuid());
  struct stat info{};
  CHECK(stat((root / "session.v1").c_str(), &info) == 0 && info.st_uid == geteuid() && (info.st_mode & 07777) == 0600);
  CHECK(storage.Read("session.v1").value() == playden::Bytes({'n', 'e', 'w'}));
  wrong_owner = true;
  CHECK(storage.WriteAtomicPrivate("session.v1", {'r', 'e', 't', 'r', 'y'}).ok());
  CHECK(owner_retries == 1 && !wrong_owner);
  CHECK(storage.Read("session.v1").value() == playden::Bytes({'r', 'e', 't', 'r', 'y'}));
  CHECK(!std::filesystem::exists(root / "session.v1.tmp"));
  std::filesystem::remove_all(root);
}
