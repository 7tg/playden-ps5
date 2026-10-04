#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace playden::steam {

inline constexpr std::size_t kMaxGunzipBytes = 4 * 1024 * 1024;

// Decodes one RFC 1952 gzip member whose uncompressed size is declared in advance
// (CMsgMulti.size_unzipped). Returns false on any malformed, truncated or oversized input.
bool Gunzip(const std::uint8_t* data, std::size_t size, std::size_t expected_size,
            std::vector<std::uint8_t>* out);

}  // namespace playden::steam
