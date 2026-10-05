#include "platform/ps5/native_startup.h"
#include "platform/ps5/native_access_posix.h"
#include "platform/ps5/display_egl.hpp"
#include "steam/backend/native_network.h"

#include <cerrno>
#include <cstdint>
#include <time.h>
#include <unistd.h>

extern "C" int sceUserServiceInitialize(const void*);
#if PLAYDEN_NATIVE_ACCESS_SOURCE
extern "C" {
#include "pw_lapy_elevation.h"
}
#endif

namespace playden::ps5 {
namespace {
struct StartupContext {
  hui::ps5::Display& display;
  std::string title_id;
  NativeAccessPosixBackend* filesystem = nullptr;
  std::string source_identity;
};

int InitializeUser(void* data) {
  auto& context = *static_cast<StartupContext*>(data);
  const auto result = static_cast<std::uint32_t>(sceUserServiceInitialize(nullptr));
  if (result != 0 && result != 0x80960003U) return static_cast<int>(result);
  const auto ops = context.filesystem->Ops();
  ops.read_source_identity(ops.context, "/app0", &context.source_identity);
  return 0;
}

int PrepareGraphics(void* data) {
  auto& display = static_cast<StartupContext*>(data)->display;
  return display.width() > 0 || display.open(3840, 2160) ? 0 : EIO;
}

int RequestData(void* data) {
#if PLAYDEN_NATIVE_ACCESS_SOURCE
  auto& context = *static_cast<StartupContext*>(data);
  if (!steam::internal::InitializeNativeNetwork()) return ENETDOWN;
  const auto ops = context.filesystem->Ops();
  std::string helper;
  for (const auto& root : {std::string("/app0"), "/data/homebrew/" + context.title_id}) {
    std::string identity;
    if (!context.source_identity.empty() &&
        ops.read_source_identity(ops.context, root.c_str(), &identity) &&
        identity == context.source_identity &&
        ops.path_kind(ops.context, (root + "/lapy.elf").c_str()) == NativePathKind::kFile) {
      helper = root + "/lapy.elf";
      break;
    }
  }
  if (helper.empty()) return ENOENT;
  if (pw_lapy_elevation_request_at(static_cast<std::int32_t>(getpid()), helper.c_str()) != 0)
    return errno ? errno : EIO;
  // The helper may still be settling the process after its cooperative reply.
  struct timespec delay{1, 0};
  while (nanosleep(&delay, &delay) != 0)
    if (errno != EINTR) return errno ? errno : EIO;
  return 0;
#else
  (void)data;
  return ENOSYS;
#endif
}
}

NativeAccessResult PrepareNativeStartup(hui::ps5::Display& display,
                                       const std::string& title_id) {
  StartupContext context{display, title_id, nullptr, {}};
  NativeAccessPosixBackend filesystem(title_id,
      {&context, InitializeUser, PrepareGraphics, RequestData});
  context.filesystem = &filesystem;
  NativeAccessOptions options;
  options.title_id = title_id;
  NativeAccessBootstrap bootstrap(filesystem.Ops(), options);
  return bootstrap.Prepare();
}
}
