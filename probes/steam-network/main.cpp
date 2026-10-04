#include <curl/curl.h>

#include "probe.h"

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
extern "C" {
int sceNetInit(void);
int sceNetTerm(void);
int sceNetPoolCreate(const char* name, int size, int flags);
int sceNetPoolDestroy(int pool_id);
int* sceNetErrnoLoc(void);
int sceNetRecv(int socket, void* data, std::size_t size, int flags);
int sceNetSetsockopt(int socket, int level, int option, const void* value,
                     std::uint32_t size);
int sceNetResolverCreate(const char* name, int pool_id, int flags);
int sceNetResolverDestroy(int resolver_id);
int sceNetResolverStartNtoa(int resolver_id, const char* hostname, in_addr_t* address,
                            int timeout, int retries, int flags);
}
#endif

namespace {
using Clock = std::chrono::steady_clock;

struct Buffer {
  std::string bytes;
  bool overflow = false;
};

size_t Write(void* data, size_t size, size_t count, void* context) {
  constexpr size_t kLimit = 1024 * 1024;
  auto& output = *static_cast<Buffer*>(context);
  if (count != 0 && size > kLimit / count) {
    output.overflow = true;
    return 0;
  }
  const size_t length = size * count;
  if (length > kLimit - output.bytes.size()) {
    output.overflow = true;
    return 0;
  }
  output.bytes.append(static_cast<const char*>(data), length);
  return length;
}

void Common(CURL* curl, const char* ca_bundle, long timeout_ms) {
  curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_PROXY, "");
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "PlaydenSteamNetworkProbe/1");
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
}

long long Milliseconds(Clock::time_point start) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

void Result(const char* probe, bool pass, CURLcode code, long long elapsed_ms,
            const char* detail = nullptr) {
  std::printf("{\"probe\":\"%s\",\"status\":\"%s\",\"curl_code\":%d,\"elapsed_ms\":%lld",
              probe, pass ? "pass" : "fail", static_cast<int>(code), elapsed_ms);
  if (detail) std::printf(",\"detail\":\"%s\"", detail);
  std::puts("}");
  std::fflush(stdout);
}

void Phase(const char* name) {
  std::printf("{\"probe\":\"phase\",\"name\":\"%s\"}\n", name);
  std::fflush(stdout);
}

struct CurlTrace {
  bool resolving = false;
  bool trying = false;
  bool connected = false;
  bool header_prepared = false;
  bool request_sent = false;
  bool response_received = false;
  bool timed_out = false;
};

void TraceOnce(bool& emitted, const char* phase) {
  if (emitted) return;
  emitted = true;
  Phase(phase);
}

int CurlDebug(CURL*, curl_infotype type, char* bytes, size_t size, void* context) {
  auto& trace = *static_cast<CurlTrace*>(context);
  if (type == CURLINFO_HEADER_OUT) {
    TraceOnce(trace.header_prepared, "curl_header_prepared");
  } else if (type == CURLINFO_HEADER_IN || type == CURLINFO_DATA_IN) {
    TraceOnce(trace.response_received, "curl_response_received");
  } else if (type == CURLINFO_TEXT) {
    const std::string_view message(bytes, size);
    if (message.find("Could not resolve") != std::string_view::npos ||
        message.find("Host ") != std::string_view::npos)
      TraceOnce(trace.resolving, "curl_resolution_state");
    if (message.find("Trying ") != std::string_view::npos)
      TraceOnce(trace.trying, "curl_connect_trying");
    if (message.find("Connected to ") != std::string_view::npos ||
        message.find("Established connection") != std::string_view::npos)
      TraceOnce(trace.connected, "curl_connected");
    if (message.find("Request completely sent off") != std::string_view::npos)
      TraceOnce(trace.request_sent, "curl_request_sent");
    if (message.find("timed out") != std::string_view::npos)
      TraceOnce(trace.timed_out, "curl_timeout_reported");
  }
  return 0;
}

#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
std::atomic<unsigned long long> allocation_calls{0};
std::atomic<unsigned long long> allocation_failures{0};
std::atomic<unsigned long long> allocation_requested_bytes{0};

void RecordAllocation(const void* result, std::size_t bytes) {
  allocation_calls.fetch_add(1, std::memory_order_relaxed);
  allocation_requested_bytes.fetch_add(bytes, std::memory_order_relaxed);
  if (!result && bytes) allocation_failures.fetch_add(1, std::memory_order_relaxed);
}

void* CurlMalloc(std::size_t size) {
  void* result = std::malloc(size);
  RecordAllocation(result, size);
  return result;
}

void CurlFree(void* address) { std::free(address); }

void* CurlRealloc(void* address, std::size_t size) {
  void* result = std::realloc(address, size);
  RecordAllocation(result, size);
  return result;
}

void* CurlCalloc(std::size_t count, std::size_t size) {
  void* result = std::calloc(count, size);
  RecordAllocation(result, count <= SIZE_MAX / (size ? size : 1) ? count * size : SIZE_MAX);
  return result;
}

char* CurlStrdup(const char* value) {
  const std::size_t size = std::strlen(value) + 1;
  char* result = static_cast<char*>(std::malloc(size));
  if (result) std::memcpy(result, value, size);
  RecordAllocation(result, size);
  return result;
}

void AllocationStats(const char* phase) {
  std::printf("{\"probe\":\"curl_allocations\",\"phase\":\"%s\",\"calls\":%llu,"
              "\"failed\":%llu,\"requested_bytes\":%llu}\n",
              phase, allocation_calls.load(std::memory_order_relaxed),
              allocation_failures.load(std::memory_order_relaxed),
              allocation_requested_bytes.load(std::memory_order_relaxed));
  std::fflush(stdout);
}

class NativeResolver {
 public:
  int Initialize() {
    pool_ = sceNetPoolCreate("playden-steam-probe", 0x4000, 0);
    if (pool_ < 0) return pool_;
    resolver_ = sceNetResolverCreate("playden-steam-probe", pool_, 0);
    return resolver_ < 0 ? resolver_ : 0;
  }

  int Resolve(const std::string& hostname, std::string& numeric_address) {
    // The PS4/PS5 form of this API takes microseconds (the public Vita form has the same
    // six-argument signature). Bound each attempt to five seconds and allow one retry.
    constexpr int kTimeoutMicroseconds = 5'000'000;
    constexpr int kRetries = 1;
    in_addr_t address = 0;
    const int result = sceNetResolverStartNtoa(resolver_, hostname.c_str(), &address,
                                               kTimeoutMicroseconds, kRetries, 0);
    if (result < 0) return result;
    char buffer[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &address, buffer, sizeof(buffer))) return -1;
    numeric_address = buffer;
    return 0;
  }

  bool Attach(CURL* curl, const std::string& hostname, const std::string& port,
              curl_slist*& entries) {
    std::string address;
    if (Resolve(hostname, address) != 0) return false;
    const std::string entry = hostname + ":" + port + ":" + address;
    curl_slist* appended = curl_slist_append(entries, entry.c_str());
    if (!appended) return false;
    entries = appended;
    return curl_easy_setopt(curl, CURLOPT_RESOLVE, entries) == CURLE_OK;
  }

  void Shutdown() {
    if (resolver_ >= 0) sceNetResolverDestroy(resolver_);
    if (pool_ >= 0) sceNetPoolDestroy(pool_);
    resolver_ = -1;
    pool_ = -1;
  }

  ~NativeResolver() { Shutdown(); }

 private:
  int pool_ = -1;
  int resolver_ = -1;
};

bool SplitEndpoint(const std::string& endpoint, std::string& hostname, std::string& port) {
  const std::size_t colon = endpoint.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= endpoint.size()) return false;
  hostname = endpoint.substr(0, colon);
  port = endpoint.substr(colon + 1);
  return hostname.find(':') == std::string::npos;
}
#endif

std::vector<std::string> ParseEndpoints(std::string_view json) {
  constexpr std::string_view key = "\"endpoint\"";
  std::vector<std::string> result;
  size_t cursor = 0;
  while (result.size() < 4 && (cursor = json.find(key, cursor)) != std::string_view::npos) {
    cursor = json.find(':', cursor + key.size());
    if (cursor == std::string_view::npos) break;
    cursor = json.find('"', cursor + 1);
    if (cursor == std::string_view::npos) break;
    const size_t end = json.find('"', ++cursor);
    if (end == std::string_view::npos) break;
    const auto endpoint = json.substr(cursor, end - cursor);
    const size_t colon = endpoint.rfind(':');
    bool safe = colon != std::string_view::npos && colon > 0 && colon + 1 < endpoint.size();
    for (char c : endpoint) safe &= (c == '.' || c == '-' || c == ':' ||
                                      (c >= '0' && c <= '9') ||
                                      (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
    if (safe) result.emplace_back(endpoint);
    cursor = end + 1;
  }
  return result;
}

class SilentServer {
 public:
  bool Start() {
    listener_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0) return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener_, 1) != 0) return false;
    socklen_t length = sizeof(address);
    if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) != 0) return false;
    port_ = ntohs(address.sin_port);
    thread_ = std::thread([this] {
      int client = -1;
      while (!stop_.load()) {
        pollfd descriptor{listener_, POLLIN, 0};
        const int ready = poll(&descriptor, 1, 50);
        if (ready > 0 && (descriptor.revents & POLLIN)) {
          client = accept(listener_, nullptr, nullptr);
          break;
        }
      }
      if (client >= 0) {
        while (!stop_.load() && !release_client_.load())
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        close(client);
      }
    });
    return true;
  }
  ~SilentServer() {
    stop_.store(true);
    if (listener_ >= 0) shutdown(listener_, SHUT_RDWR);
    if (thread_.joinable()) thread_.join();
    if (listener_ >= 0) close(listener_);
  }
  unsigned port() const { return port_; }
  void ReleaseClient() { release_client_.store(true); }

 private:
  int listener_ = -1;
  unsigned port_ = 0;
  std::atomic<bool> stop_{false};
  std::atomic<bool> release_client_{false};
  std::thread thread_;
};

#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
bool NativeSocketPollProbe() {
  // PS5 libSceNet shifts the Vita/PS4 SOL_SOCKET extension block by 0x100: the
  // independently exercised send/receive timeout options are 0x1105/0x1106 rather than
  // 0x1005/0x1006. This makes the corresponding native nonblocking option 0x1200.
  constexpr int kSceNetSolSocket = 0xffff;
  constexpr int kSceNetSoNonblock = 0x1200;
  SilentServer server;
  if (!server.Start()) {
    std::puts("{\"probe\":\"native_socket_poll\",\"status\":\"fail\","
              "\"stage\":\"server\"}");
    std::fflush(stdout);
    return false;
  }
  const int client = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(server.port());
  const int connect_result = client < 0
                                 ? -1
                                 : connect(client, reinterpret_cast<sockaddr*>(&address),
                                           sizeof(address));
  pollfd descriptor{client, POLLIN, 0};
  const auto start = Clock::now();
  const int poll_result = connect_result == 0 ? poll(&descriptor, 1, 20) : -1;
  const long long elapsed = Milliseconds(start);
  const int flags_before = client >= 0 ? fcntl(client, F_GETFL, 0) : -1;
  errno = 0;
  const int set_nonblock_result =
      flags_before >= 0 ? fcntl(client, F_SETFL, flags_before | O_NONBLOCK) : -1;
  const int set_nonblock_error = set_nonblock_result < 0 ? errno : 0;
  const int flags_after = client >= 0 ? fcntl(client, F_GETFL, 0) : -1;
  int ioctl_nonblock = 1;
  errno = 0;
  const int ioctl_nonblock_result =
      client >= 0 ? ioctl(client, FIONBIO, &ioctl_nonblock) : -1;
  const int ioctl_nonblock_error = ioctl_nonblock_result < 0 ? errno : 0;
  int native_nonblock = 1;
  const int native_nonblock_result =
      client >= 0 ? sceNetSetsockopt(client, kSceNetSolSocket, kSceNetSoNonblock,
                                     &native_nonblock, sizeof(native_nonblock))
                  : -1;
  const int native_nonblock_error =
      native_nonblock_result < 0 && sceNetErrnoLoc() ? *sceNetErrnoLoc() : 0;
  std::thread release_thread([&server] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    server.ReleaseClient();
  });
  unsigned char byte = 0;
  const auto native_recv_start = Clock::now();
  const int native_recv_result = client >= 0 ? sceNetRecv(client, &byte, 1, 0) : -1;
  const int native_recv_error =
      native_recv_result < 0 && sceNetErrnoLoc() ? *sceNetErrnoLoc() : 0;
  const long long native_recv_elapsed = Milliseconds(native_recv_start);
  const auto recv_start = Clock::now();
  const ssize_t recv_result = client >= 0 ? recv(client, &byte, 1, 0) : -1;
  const int recv_error = recv_result < 0 ? errno : 0;
  const long long recv_elapsed = Milliseconds(recv_start);
  release_thread.join();
  if (client >= 0) close(client);
  const bool nonblocking = native_nonblock_result == 0 &&
                           native_recv_result < 0 && native_recv_error == EWOULDBLOCK &&
                           native_recv_elapsed < 20 && recv_result < 0 &&
                           (recv_error == EAGAIN || recv_error == EWOULDBLOCK) &&
                           recv_elapsed < 20;
  const bool pass = connect_result == 0 && poll_result == 0 && elapsed >= 10 && elapsed < 500 &&
                    nonblocking;
  std::printf("{\"probe\":\"native_socket_poll\",\"status\":\"%s\","
              "\"connect_result\":%d,\"poll_result\":%d,\"revents\":%d,"
              "\"elapsed_ms\":%lld,\"flags_before\":%d,\"set_nonblock_result\":%d,"
              "\"set_nonblock_error\":%d,\"flags_after\":%d,"
              "\"ioctl_nonblock_result\":%d,\"ioctl_nonblock_error\":%d,"
              "\"native_nonblock_result\":%d,\"native_nonblock_error\":%d,"
              "\"native_recv_result\":%d,\"native_recv_error\":%d,"
              "\"native_recv_elapsed_ms\":%lld,"
              "\"recv_result\":%lld,\"recv_error\":%d,"
              "\"recv_elapsed_ms\":%lld}\n",
              pass ? "pass" : "fail", connect_result, poll_result,
              static_cast<int>(descriptor.revents), elapsed, flags_before, set_nonblock_result,
              set_nonblock_error, flags_after, ioctl_nonblock_result, ioctl_nonblock_error,
              native_nonblock_result, native_nonblock_error, native_recv_result,
              native_recv_error, native_recv_elapsed,
              static_cast<long long>(recv_result), recv_error, recv_elapsed);
  std::fflush(stdout);
  return pass;
}
#endif

CURLcode SilentRequest(const char* ca_bundle, long timeout_ms, long cancel_after_ms,
                       long long& elapsed_ms) {
  SilentServer server;
  if (!server.Start()) return CURLE_FAILED_INIT;
  Phase(cancel_after_ms <= 0 ? "timeout_server_ready" : "cancel_server_ready");
  // Keep timeout/cancellation independent of TLS entropy and certificate setup. Verified Steam
  // HTTPS and WSS are exercised separately below; this local server isolates curl's socket poll.
  const std::string url = "http://127.0.0.1:" + std::to_string(server.port()) + "/";
  CURL* curl = curl_easy_init();
  if (!curl) return CURLE_FAILED_INIT;
  Phase(cancel_after_ms <= 0 ? "timeout_easy_ready" : "cancel_easy_ready");
  CurlTrace trace;
  Common(curl, ca_bundle, timeout_ms);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, CurlDebug);
  curl_easy_setopt(curl, CURLOPT_DEBUGDATA, &trace);
  const auto start = Clock::now();
  CURLcode code = CURLE_OK;
  if (cancel_after_ms <= 0) {
    Phase("timeout_perform_begin");
    code = curl_easy_perform(curl);
    Phase("timeout_perform_end");
  } else {
    Phase("cancel_multi_begin");
    CURLM* multi = curl_multi_init();
    if (!multi) {
      curl_easy_cleanup(curl);
      return CURLE_FAILED_INIT;
    }
    Phase("cancel_multi_ready");
    curl_multi_add_handle(multi, curl);
    int running = 0;
    unsigned iteration = 0;
    do {
      std::printf("{\"probe\":\"multi_step\",\"iteration\":%u,"
                  "\"step\":\"perform_begin\"}\n", iteration);
      std::fflush(stdout);
      const CURLMcode multi_code = curl_multi_perform(multi, &running);
      std::printf("{\"probe\":\"multi_step\",\"iteration\":%u,"
                  "\"step\":\"perform_end\",\"multi_code\":%d,\"running\":%d}\n",
                  iteration, static_cast<int>(multi_code), running);
      std::fflush(stdout);
      if (multi_code != CURLM_OK) {
        code = CURLE_FAILED_INIT;
        break;
      }
      if (!running) {
        int messages = 0;
        if (CURLMsg* message = curl_multi_info_read(multi, &messages)) code = message->data.result;
        break;
      }
      if (Milliseconds(start) >= cancel_after_ms) {
        code = CURLE_ABORTED_BY_CALLBACK;
        break;
      }
      std::printf("{\"probe\":\"multi_step\",\"iteration\":%u,"
                  "\"step\":\"poll_begin\"}\n", iteration);
      std::fflush(stdout);
      const CURLMcode poll_code = curl_multi_poll(multi, nullptr, 0, 20, nullptr);
      std::printf("{\"probe\":\"multi_step\",\"iteration\":%u,"
                  "\"step\":\"poll_end\",\"multi_code\":%d}\n",
                  iteration, static_cast<int>(poll_code));
      std::fflush(stdout);
      if (poll_code != CURLM_OK) {
        code = CURLE_FAILED_INIT;
        break;
      }
      ++iteration;
    } while (true);
    curl_multi_remove_handle(multi, curl);
    curl_multi_cleanup(multi);
    Phase("cancel_multi_end");
  }
  elapsed_ms = Milliseconds(start);
  curl_easy_cleanup(curl);
  return code;
}

bool SendClientHello(CURL* socket, long long& elapsed_ms, CURLcode& code) {
  // EMsg ClientHello (9805), protobuf flag, empty CMsgProtoBufHeader, then
  // CMsgClientHello { protocol_version: 65580 }.
  constexpr unsigned char frame[] = {0x4d, 0x26, 0x00, 0x80, 0x00, 0x00,
                                     0x00, 0x00, 0x08, 0xac, 0x80, 0x04};
  constexpr long long kSendDeadlineMs = 1000;
  size_t offset = 0;
  const auto start = Clock::now();
  code = CURLE_OK;
  while (offset < sizeof(frame) && Milliseconds(start) < kSendDeadlineMs) {
    size_t sent = 0;
    code = curl_ws_send(socket, frame + offset, sizeof(frame) - offset, &sent, 0,
                        CURLWS_BINARY);
    offset += sent;
    if (code != CURLE_OK && code != CURLE_AGAIN) break;
    if (offset < sizeof(frame) && (code == CURLE_AGAIN || sent == 0))
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    code = CURLE_OK;
  }
  elapsed_ms = Milliseconds(start);
  if (offset != sizeof(frame) && code == CURLE_OK) code = CURLE_OPERATION_TIMEDOUT;
  return code == CURLE_OK && offset == sizeof(frame);
}

bool VerifyPeerLiveness(CURL* socket, long long& elapsed_ms, CURLcode& code) {
  constexpr unsigned char nonce[] = {'p', 'l', 'a', 'y', 'd', 'e', 'n', '1'};
  constexpr long long kDeadlineMs = 2000;
  const auto start = Clock::now();
  size_t offset = 0;
  code = CURLE_OK;
  while (offset < sizeof(nonce) && Milliseconds(start) < kDeadlineMs) {
    size_t sent = 0;
    code = curl_ws_send(socket, nonce + offset, sizeof(nonce) - offset, &sent, 0, CURLWS_PING);
    offset += sent;
    if (code != CURLE_OK && code != CURLE_AGAIN) break;
    if (offset < sizeof(nonce)) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    code = CURLE_OK;
  }
  if (offset != sizeof(nonce)) {
    elapsed_ms = Milliseconds(start);
    if (code == CURLE_OK || code == CURLE_AGAIN) code = CURLE_OPERATION_TIMEDOUT;
    return false;
  }

  unsigned char received[256];
  while (Milliseconds(start) < kDeadlineMs) {
    size_t received_count = 0;
    const curl_ws_frame* metadata = nullptr;
    code = curl_ws_recv(socket, received, sizeof(received), &received_count, &metadata);
    if (code == CURLE_AGAIN) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    if (code != CURLE_OK || !metadata || (metadata->flags & CURLWS_CLOSE)) break;
    if ((metadata->flags & CURLWS_PONG) && metadata->offset == 0 && metadata->bytesleft == 0 &&
        received_count == sizeof(nonce) && std::memcmp(received, nonce, sizeof(nonce)) == 0) {
      elapsed_ms = Milliseconds(start);
      return true;
    }
  }
  elapsed_ms = Milliseconds(start);
  if (code == CURLE_OK || code == CURLE_AGAIN) code = CURLE_OPERATION_TIMEDOUT;
  return false;
}
}  // namespace

int playden::steam_probe::Run(const char* ca_bundle) {
  Phase("run_enter");
  if (!ca_bundle || !*ca_bundle) return 2;
#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
  // The payload libc's POSIX resolver and sockets adapt to libSceNet. The native service must be
  // initialized before getaddrinfo; calling the POSIX adapter first crashes inside the title.
  Phase("sce_net_init_begin");
  const int net_init = sceNetInit();
  std::printf("{\"probe\":\"sce_net_init\",\"status\":\"%s\",\"native_code\":%d}\n",
              net_init == 0 ? "pass" : "fail", net_init);
  std::fflush(stdout);
  if (net_init != 0) return 2;
  NativeResolver resolver;
  const int resolver_init = resolver.Initialize();
  std::printf("{\"probe\":\"native_resolver_init\",\"status\":\"%s\",\"native_code\":%d}\n",
              resolver_init == 0 ? "pass" : "fail", resolver_init);
  std::fflush(stdout);
  if (resolver_init != 0) {
    resolver.Shutdown();
    sceNetTerm();
    return 2;
  }
  Phase("native_socket_poll_begin");
  if (!NativeSocketPollProbe()) {
    resolver.Shutdown();
    const int net_term = sceNetTerm();
    std::printf("{\"probe\":\"sce_net_term\",\"status\":\"%s\",\"native_code\":%d}\n",
                net_term == 0 ? "pass" : "fail", net_term);
    std::puts("{\"probe\":\"complete\",\"status\":\"fail\"}");
    std::fflush(stdout);
    return 1;
  }
#endif
  Phase("curl_global_init_begin");
#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
  const CURLcode global_init = curl_global_init_mem(CURL_GLOBAL_DEFAULT, CurlMalloc, CurlFree,
                                                    CurlRealloc, CurlStrdup, CurlCalloc);
#else
  const CURLcode global_init = curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
  std::printf("{\"probe\":\"curl_global_init\",\"status\":\"%s\",\"curl_code\":%d}\n",
              global_init == CURLE_OK ? "pass" : "fail", static_cast<int>(global_init));
  std::fflush(stdout);
  if (global_init != CURLE_OK) {
#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
    AllocationStats("global_init_failure");
    resolver.Shutdown();
    sceNetTerm();
#endif
    return 2;
  }

  bool all = true;
  Phase("dns_begin");
  const auto dns_start = Clock::now();
#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
  std::string api_address;
  const int dns = resolver.Resolve("api.steampowered.com", api_address);
  const bool dns_ok = dns == 0 && !api_address.empty();
#else
  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* addresses = nullptr;
  const int dns = getaddrinfo("api.steampowered.com", "443", &hints, &addresses);
  const bool dns_ok = dns == 0 && addresses;
#endif
  std::printf("{\"probe\":\"dns\",\"status\":\"%s\",\"resolver_code\":%d,\"elapsed_ms\":%lld}\n",
              dns_ok ? "pass" : "fail", dns, Milliseconds(dns_start));
  std::fflush(stdout);
#ifndef PLAYDEN_STEAM_NETWORK_PROBE_PS5
  if (addresses) freeaddrinfo(addresses);
#endif
  all &= dns_ok;

#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
  Phase("native_poll_sleep_begin");
  timespec monotonic_before{};
  timespec monotonic_after{};
  timeval wall_before{};
  timeval wall_after{};
  const int monotonic_before_result = clock_gettime(CLOCK_MONOTONIC, &monotonic_before);
  const int wall_before_result = gettimeofday(&wall_before, nullptr);
  const auto poll_start = Clock::now();
  const int poll_result = poll(nullptr, 0, 20);
  const long long poll_elapsed = Milliseconds(poll_start);
  const int monotonic_after_result = clock_gettime(CLOCK_MONOTONIC, &monotonic_after);
  const int wall_after_result = gettimeofday(&wall_after, nullptr);
  const long long monotonic_elapsed =
      (monotonic_after.tv_sec - monotonic_before.tv_sec) * 1000LL +
      (monotonic_after.tv_nsec - monotonic_before.tv_nsec) / 1'000'000LL;
  const long long wall_elapsed = (wall_after.tv_sec - wall_before.tv_sec) * 1000LL +
                                 (wall_after.tv_usec - wall_before.tv_usec) / 1000LL;
  const bool poll_ok = poll_result == 0 && poll_elapsed >= 10 && poll_elapsed < 500;
  std::printf("{\"probe\":\"native_poll_sleep\",\"status\":\"%s\","
              "\"poll_result\":%d,\"steady_elapsed_ms\":%lld,"
              "\"monotonic_before_result\":%d,\"monotonic_after_result\":%d,"
              "\"monotonic_elapsed_ms\":%lld,\"wall_before_result\":%d,"
              "\"wall_after_result\":%d,\"wall_elapsed_ms\":%lld}\n",
              poll_ok ? "pass" : "fail", poll_result, poll_elapsed,
              monotonic_before_result, monotonic_after_result, monotonic_elapsed,
              wall_before_result, wall_after_result, wall_elapsed);
  std::fflush(stdout);
  all &= poll_ok;
#endif

  long long elapsed = 0;
  CURLcode code = SilentRequest(ca_bundle, 3000, 120, elapsed);
  const bool cancel_ok = code == CURLE_ABORTED_BY_CALLBACK && elapsed >= 80 && elapsed < 1000;
  Result("cancellation", cancel_ok, code, elapsed);
#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
  AllocationStats("after_cancellation");
#endif
  all &= cancel_ok;

  code = SilentRequest(ca_bundle, 300, 0, elapsed);
  const bool timeout_ok = code == CURLE_OPERATION_TIMEDOUT && elapsed >= 200 && elapsed < 1500;
  Result("timeout", timeout_ok, code, elapsed);
#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
  AllocationStats("after_timeout");
#endif
  all &= timeout_ok;

  Buffer directory;
  CURL* http = curl_easy_init();
#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
  curl_slist* http_resolve = nullptr;
  bool http_resolution_ok = true;
#endif
  long status = 0;
  long verify = -1;
  const auto https_start = Clock::now();
  if (http) {
    Common(http, ca_bundle, 10000);
#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
    if (!resolver.Attach(http, "api.steampowered.com", "443", http_resolve)) {
      code = CURLE_COULDNT_RESOLVE_HOST;
      http_resolution_ok = false;
      curl_easy_cleanup(http);
      http = nullptr;
    }
#endif
  }
  if (http) {
    curl_easy_setopt(http, CURLOPT_URL,
                     "https://api.steampowered.com/ISteamDirectory/GetCMListForConnect/v1/"
                     "?cmtype=websockets&maxcount=4&format=json");
    curl_easy_setopt(http, CURLOPT_WRITEFUNCTION, Write);
    curl_easy_setopt(http, CURLOPT_WRITEDATA, &directory);
    code = curl_easy_perform(http);
    curl_easy_getinfo(http, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(http, CURLINFO_SSL_VERIFYRESULT, &verify);
    curl_easy_cleanup(http);
  } else {
#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
    if (http_resolution_ok) code = CURLE_FAILED_INIT;
#else
    code = CURLE_FAILED_INIT;
#endif
  }
#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
  curl_slist_free_all(http_resolve);
#endif
  auto endpoints = ParseEndpoints(directory.bytes);
  const bool https_ok = code == CURLE_OK && status == 200 && verify == 0 && !directory.overflow &&
                        !endpoints.empty();
  std::printf("{\"probe\":\"verified_https_cm_directory\",\"status\":\"%s\","
              "\"curl_code\":%d,\"http_status\":%ld,\"verify_result\":%ld,"
              "\"endpoint_count\":%zu,\"elapsed_ms\":%lld}\n",
              https_ok ? "pass" : "fail", static_cast<int>(code), status, verify,
              endpoints.size(), Milliseconds(https_start));
  std::fflush(stdout);
  all &= https_ok;
#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
  AllocationStats("after_https");
#endif

  bool websocket_ok = false;
  bool hello_ok = false;
  bool persistent_ok = false;
  for (const auto& endpoint : endpoints) {
    CURL* websocket = curl_easy_init();
    if (!websocket) break;
#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
    curl_slist* websocket_resolve = nullptr;
    std::string websocket_host;
    std::string websocket_port;
    if (!SplitEndpoint(endpoint, websocket_host, websocket_port) ||
        !resolver.Attach(websocket, websocket_host, websocket_port, websocket_resolve)) {
      curl_slist_free_all(websocket_resolve);
      curl_easy_cleanup(websocket);
      continue;
    }
#endif
    const std::string url = "wss://" + endpoint + "/cmsocket/";
    Common(websocket, ca_bundle, 10000);
    curl_easy_setopt(websocket, CURLOPT_URL, url.c_str());
    curl_easy_setopt(websocket, CURLOPT_CONNECT_ONLY, 2L);
    const auto ws_start = Clock::now();
    code = curl_easy_perform(websocket);
    curl_easy_getinfo(websocket, CURLINFO_SSL_VERIFYRESULT, &verify);
    websocket_ok = code == CURLE_OK && verify == 0;
    Result("verified_wss_cm_upgrade", websocket_ok, code, Milliseconds(ws_start));
    if (websocket_ok) {
      hello_ok = SendClientHello(websocket, elapsed, code);
      Result("cm_client_hello", hello_ok, code, elapsed);
      if (hello_ok) {
        // ClientHello does not promise an anonymous protobuf reply. A matching WebSocket PONG is
        // peer-observable proof that the upgraded CM transport remains live. No payload is logged.
        persistent_ok = VerifyPeerLiveness(websocket, elapsed, code);
        Result("cm_transport_persistent", persistent_ok, code, elapsed);
      }
    }
    curl_easy_cleanup(websocket);
#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
    curl_slist_free_all(websocket_resolve);
#endif
    if (websocket_ok && hello_ok && persistent_ok) break;
  }
  if (endpoints.empty()) Result("verified_wss_cm_upgrade", false, CURLE_COULDNT_RESOLVE_HOST, 0,
                                "no_sanitized_endpoint");
  all &= websocket_ok && hello_ok && persistent_ok;

  curl_global_cleanup();
#ifdef PLAYDEN_STEAM_NETWORK_PROBE_PS5
  resolver.Shutdown();
  const int net_term = sceNetTerm();
  std::printf("{\"probe\":\"sce_net_term\",\"status\":\"%s\",\"native_code\":%d}\n",
              net_term == 0 ? "pass" : "fail", net_term);
  std::fflush(stdout);
  all &= net_term == 0;
#endif
  std::printf("{\"probe\":\"complete\",\"status\":\"%s\"}\n", all ? "pass" : "fail");
  std::fflush(stdout);
  return all ? 0 : 1;
}

#ifndef PLAYDEN_STEAM_NETWORK_PROBE_NO_MAIN
int main(int argc, char** argv) {
  const char* ca_bundle = nullptr;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--ca-bundle") == 0) ca_bundle = argv[++i];
  }
  if (!ca_bundle) {
    std::fputs("usage: playden-steam-network-probe --ca-bundle PATH\n", stderr);
    return 2;
  }
  return playden::steam_probe::Run(ca_bundle);
}
#endif
