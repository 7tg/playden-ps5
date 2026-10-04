#pragma once

#include <cstdint>

namespace playden::ps5 {

enum class ProcessMemoryError : std::uint8_t {
  kNone = 0,
  kProcessIdUnavailable = 1,
  kQueryUnavailable = 2,
  kInvalidResponse = 3,
  kOverflow = 4,
  kSizeQueryUnavailable = 5,
  kUnsupportedRecordLayout = 6,
};

struct ProcessMemoryResult {
  std::uint64_t bytes = 0;
  ProcessMemoryError error = ProcessMemoryError::kQueryUnavailable;
  // errno captured immediately after the failed platform call. Zero means the
  // platform did not supply a meaningful native error code.
  std::int32_t native_error = 0;
  // Size advertised by the read-only sysctl query, and the size reported by
  // the subsequent fetch. A fetch is not attempted for an unknown ABI size.
  std::uint64_t required_record_bytes = 0;
  std::uint64_t returned_record_bytes = 0;

  constexpr bool available() const { return error == ProcessMemoryError::kNone; }
};

// Returns the process's current resident set from the platform process table.
// An unavailable or invalid platform response is never substituted with heap
// usage or peak resident memory.
ProcessMemoryResult ReadProcessResidentBytes();

namespace detail {

// Kept integer-only so conversion and overflow behavior can be tested on the
// host without exposing the platform's kinfo_proc ABI.
ProcessMemoryResult ProcessResidentBytesFromPages(std::uint64_t resident_pages,
                                                  std::uint64_t page_bytes);

// Records an ABI size mismatch without fetching or interpreting the unknown
// process-record layout.
ProcessMemoryResult UnsupportedProcessRecordLayout(
    std::uint64_t required_record_bytes);

}  // namespace detail
}  // namespace playden::ps5
