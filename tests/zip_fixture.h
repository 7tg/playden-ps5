#pragma once
// zip_fixture.h - Writes small stored (uncompressed) zips for the self-update tests.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <string>
#include <vector>

#include "miniz.h"

namespace zip_fixture {

struct Entry {
  std::string name;
  std::string data;
  unsigned mode = 0100644;   // unix type and permission bits
  std::uint16_t flags = 0;   // bit 0 = encrypted
  bool unix_made = true;
  bool corrupt_crc = false;
};

inline void Put16(std::string& out, unsigned value) {
  out += static_cast<char>(value & 0xff);
  out += static_cast<char>((value >> 8) & 0xff);
}
inline void Put32(std::string& out, std::uint32_t value) {
  Put16(out, value & 0xffff);
  Put16(out, value >> 16);
}

inline std::string Build(const std::vector<Entry>& entries) {
  std::string out, central;
  for (const auto& entry : entries) {
    std::uint32_t crc = static_cast<std::uint32_t>(
        mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(entry.data.data()), entry.data.size()));
    if (entry.corrupt_crc) crc ^= 1;
    const auto offset = static_cast<std::uint32_t>(out.size());
    Put32(out, 0x04034b50); Put16(out, 20); Put16(out, entry.flags); Put16(out, 0); Put16(out, 0); Put16(out, 0x21);
    Put32(out, crc); Put32(out, entry.data.size()); Put32(out, entry.data.size());
    Put16(out, entry.name.size()); Put16(out, 0);
    out += entry.name;
    out += entry.data;
    Put32(central, 0x02014b50); Put16(central, (entry.unix_made ? 3u << 8 : 0u) | 20); Put16(central, 20);
    Put16(central, entry.flags); Put16(central, 0); Put16(central, 0); Put16(central, 0x21);
    Put32(central, crc); Put32(central, entry.data.size()); Put32(central, entry.data.size());
    Put16(central, entry.name.size()); Put16(central, 0); Put16(central, 0); Put16(central, 0); Put16(central, 0);
    Put32(central, entry.unix_made ? static_cast<std::uint32_t>(entry.mode) << 16 : 0); Put32(central, offset);
    central += entry.name;
  }
  const auto central_offset = static_cast<std::uint32_t>(out.size());
  out += central;
  Put32(out, 0x06054b50); Put16(out, 0); Put16(out, 0); Put16(out, entries.size()); Put16(out, entries.size());
  Put32(out, central.size()); Put32(out, central_offset); Put16(out, 0);
  return out;
}

inline bool Write(const std::string& path, const std::string& bytes) {
  FILE* file = std::fopen(path.c_str(), "wb");
  if (!file) return false;
  const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
  return std::fclose(file) == 0 && ok;
}

inline std::string TempFolder(const char* tag) {
  std::string pattern = std::string("/tmp/playden-") + tag + "-XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  const char* made = mkdtemp(buffer.data());
  return made ? made : "";
}

}  // namespace zip_fixture
