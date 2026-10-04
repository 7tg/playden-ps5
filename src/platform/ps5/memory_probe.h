#pragma once

#include <cstddef>
#include <cstdint>

namespace playden::ps5 {

// Experimental diagnostics only. A successful sample does not make the
// undocumented 1096-byte process record a supported production ABI.
enum class MemoryProbeStatus : std::uint32_t {
  kOk = 0,
  kProcessIdUnavailable = 1,
  kParentProcessIdUnavailable = 2,
  kSizeQueryFailed = 3,
  kUnexpectedRecordSize = 4,
  kFetchFailed = 5,
  kReturnedRecordSizeMismatch = 6,
  kEmbeddedRecordSizeMismatch = 7,
  kProcessIdMismatch = 8,
  kParentProcessIdMismatch = 9,
  kPageSizeMismatch = 10,
  kResidentPagesInvalid = 11,
  kByteCountOverflow = 12,
  kResidentExceedsVirtual = 13,
  kMapFailed = 14,
  kUnmapFailed = 15,
  kInvalidRecordStorage = 16,
};

struct MemoryProbeSample {
  MemoryProbeStatus status = MemoryProbeStatus::kInvalidRecordStorage;
  std::int32_t native_error = 0;
  std::uint64_t required_record_bytes = 0;
  std::uint64_t returned_record_bytes = 0;
  std::uint64_t virtual_bytes = 0;
  std::uint64_t resident_pages = 0;
  std::uint64_t resident_bytes = 0;
  std::uint64_t page_bytes = 0;
};

// Runs three bounded experimental samples: baseline, after touching a 32 MiB
// anonymous mapping, and after unmapping it. Output contains numeric fields
// only and deliberately omits process identifiers.
void RunMemoryProbe();

namespace detail {

// Parses only the independently reviewed prefix of the observed 1096-byte
// record. Kept platform-neutral for host validation.
MemoryProbeSample ParseMemoryProbeRecord(const std::uint8_t* record,
                                         std::size_t record_bytes,
                                         std::int32_t expected_process_id,
                                         std::int32_t expected_parent_process_id,
                                         std::uint64_t page_bytes);

}  // namespace detail
}  // namespace playden::ps5
