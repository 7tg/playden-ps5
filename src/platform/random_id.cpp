#include "platform/random_id.h"

#include <cstddef>
#include <cstdio>

#include <fcntl.h>
#include <unistd.h>

namespace playden {

std::optional<std::string> RandomUuid() {
  // Not std::random_device: on the console it goes through libc arc4random, which may abort.
  unsigned char bytes[16];
  const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return std::nullopt;
  std::size_t read_bytes = 0;
  while (read_bytes < sizeof(bytes)) {
    const auto count = ::read(fd, bytes + read_bytes, sizeof(bytes) - read_bytes);
    if (count <= 0) { ::close(fd); return std::nullopt; }
    read_bytes += static_cast<std::size_t>(count);
  }
  ::close(fd);
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);  // version 4
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);  // RFC 4122 variant
  char text[37];
  std::snprintf(text, sizeof(text), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9],
                bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
  return std::string(text);
}

}  // namespace playden
