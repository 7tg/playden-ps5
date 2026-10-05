// Developer-only bridge to the existing ShadowMount loopback service.
// It never installs a payload service, restarts ShadowMount, or removes a title.
#include "platform/ps5/title_registry.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr const char* kTitle = "FAKE78001";
constexpr const char* kRoot = "/data/playden/development/FAKE78001";
constexpr const char* kOwner = "Playden per-game wrapper acceptance FAKE78001\n";
constexpr std::size_t kMaxResponse = 1024 * 1024;

bool Marker(const char* path) {
  struct stat info{};
  if (lstat(path, &info) || !S_ISREG(info.st_mode)) return false;
  FILE* file = std::fopen(path, "rb");
  if (!file) return false;
  char buffer[128]{};
  const auto count = std::fread(buffer, 1, sizeof(buffer), file);
  const bool clean = !std::ferror(file);
  std::fclose(file);
  return clean && count == std::strlen(kOwner) &&
         std::memcmp(buffer, kOwner, count) == 0;
}

std::optional<std::string> Post(FILE* log, const std::string& route,
                                const std::string& body) {
  const int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd < 0) return std::nullopt;
  timeval timeout{5, 0};
  if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
      setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout))) {
    close(socket_fd); return std::nullopt;
  }
  sockaddr_in address{};
#if defined(__FreeBSD__)
  address.sin_len = sizeof(address);
#endif
  address.sin_family = AF_INET;
  address.sin_port = htons(10101);
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  if (connect(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address))) {
    std::fprintf(log, "route=%s connect_errno=%d\n", route.c_str(), errno);
    close(socket_fd); return std::nullopt;
  }
  const std::string request = "POST " + route + " HTTP/1.0\r\n"
      "Host: 127.0.0.1:10101\r\nContent-Type: application/json\r\n"
      "Connection: close\r\nContent-Length: " + std::to_string(body.size()) +
      "\r\n\r\n" + body;
  std::size_t sent = 0;
  while (sent < request.size()) {
    const auto count = send(socket_fd, request.data() + sent, request.size() - sent, 0);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) { close(socket_fd); return std::nullopt; }
    sent += static_cast<std::size_t>(count);
  }
  std::string response;
  char buffer[4096];
  for (;;) {
    const auto count = recv(socket_fd, buffer, sizeof(buffer), 0);
    if (count < 0 && errno == EINTR) continue;
    if (count < 0) { close(socket_fd); return std::nullopt; }
    if (count == 0) break;
    if (static_cast<std::size_t>(count) > kMaxResponse - response.size()) {
      close(socket_fd); return std::nullopt;
    }
    response.append(buffer, static_cast<std::size_t>(count));
  }
  close(socket_fd);
  const auto separator = response.find("\r\n\r\n");
  unsigned http_status = 0;
  if (separator == std::string::npos ||
      std::sscanf(response.c_str(), "HTTP/%*u.%*u %u", &http_status) != 1 ||
      http_status != 200) return std::nullopt;
  // HTTP/1.0 requests avoid chunked encoding; reject unsupported encoding.
  auto headers = response.substr(0, separator);
  for (char& c : headers) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  if (headers.find("transfer-encoding:") != std::string::npos) return std::nullopt;
  auto payload = response.substr(separator + 4);
  std::fprintf(log, "route=%s http=%u body_bytes=%zu\n", route.c_str(), http_status, payload.size());
  std::fflush(log);
  return payload;
}
}  // namespace

int main() {
  if (!Marker("/data/playden/development/FAKE78001/owner.txt")) return 1;
  struct stat root{};
  if (lstat(kRoot, &root) || !S_ISDIR(root.st_mode)) return 1;
  const int log_fd = open("/data/playden/development/FAKE78001/probe.log",
                          O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644);
  if (log_fd < 0) return 1;
  struct stat log_info{};
  if (fstat(log_fd, &log_info) || !S_ISREG(log_info.st_mode)) {
    close(log_fd); return 1;
  }
  FILE* log = fdopen(log_fd, "a");
  if (!log) { close(log_fd); return 1; }
  std::fprintf(log, "begin pid=%d\n", getpid());
  const int directory = open(kRoot, O_RDONLY | O_DIRECTORY);
  const int synced = directory >= 0 ? fsync(directory) : -1;
  const int sync_error = synced ? errno : 0;
  if (directory >= 0) close(directory);
  const int random = open("/dev/urandom", O_RDONLY);
  unsigned char bytes[16];
  const auto random_count = random >= 0 ? read(random, bytes, sizeof(bytes)) : -1;
  if (random >= 0) close(random);
  std::fprintf(log, "payload_directory_fsync=%d errno=%d random_bytes=%ld\n",
               synced, sync_error, static_cast<long>(random_count));
  playden::ps5::TitleRegistry registry(
      [log](const auto& route, const auto& body) { return Post(log, route, body); });
  const auto before = registry.Query(kTitle);
  std::fprintf(log, "query title=%s state=%d path=%s\n",
               kTitle, int(before.state), before.path.c_str());
  int result = 0;
  if (Marker("/data/playden/development/FAKE78001/register.request")) {
    struct stat native{};
    const bool native_absent = lstat("/user/app/FAKE78001", &native) < 0 && errno == ENOENT;
    const bool owned = Marker("/data/homebrew/FAKE78001/playden-acceptance-owner.txt");
    if (before.state != playden::ps5::RegistrationState::kAbsent || !native_absent || !owned) {
      std::fprintf(log, "registration refused native_absent=%d source_owned=%d\n", native_absent, owned);
      result = 1;
    } else {
      const auto registered = registry.Register(kTitle);
      std::fprintf(log, "register accepted=%d code=%u stage=%s\n",
                   registered.ok, registered.code, registered.stage.c_str());
      result = registered.ok ? 0 : 1;
    }
    // Consume only our exact request marker, never arbitrary files.
    unlink("/data/playden/development/FAKE78001/register.request");
  }
  std::fprintf(log, "end result=%d\n", result);
  if (std::fclose(log)) return 1;
  return result;
}
