// Native-policy tests: direct *at imports fail; the separately named gateway works.
#include <dlfcn.h>
#include <cerrno>
int playden_test_gateway_error = 0;
#include <fcntl.h>
#include <sys/stat.h>
extern "C" int __wrap_openat(int parent, const char* name, int flags, ...) {
  if (playden_test_gateway_error) { errno = playden_test_gateway_error; return -1; }
  using OpenAt = int (*)(int, const char*, int, ...);
  static const auto open_at = reinterpret_cast<OpenAt>(dlsym(RTLD_NEXT, "openat"));
  return open_at(parent, name, flags);
}
extern "C" int __wrap_mkdirat(int parent, const char* name, mode_t mode) {
  if (playden_test_gateway_error) { errno = playden_test_gateway_error; return -1; }
  using MkdirAt = int (*)(int, const char*, mode_t);
  static const auto mkdir_at = reinterpret_cast<MkdirAt>(dlsym(RTLD_NEXT, "mkdirat"));
  return mkdir_at(parent, name, mode);
}
