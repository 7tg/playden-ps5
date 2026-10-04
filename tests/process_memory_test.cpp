#include "platform/ps5/process_memory.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {

static_assert(static_cast<std::uint8_t>(playden::ps5::ProcessMemoryError::kNone) == 0);
static_assert(static_cast<std::uint8_t>(
                  playden::ps5::ProcessMemoryError::kProcessIdUnavailable) == 1);
static_assert(static_cast<std::uint8_t>(
                  playden::ps5::ProcessMemoryError::kQueryUnavailable) == 2);
static_assert(static_cast<std::uint8_t>(
                  playden::ps5::ProcessMemoryError::kInvalidResponse) == 3);
static_assert(static_cast<std::uint8_t>(playden::ps5::ProcessMemoryError::kOverflow) ==
              4);
static_assert(static_cast<std::uint8_t>(
                  playden::ps5::ProcessMemoryError::kSizeQueryUnavailable) == 5);
static_assert(static_cast<std::uint8_t>(
                  playden::ps5::ProcessMemoryError::kUnsupportedRecordLayout) == 6);

void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", description);
    std::exit(1);
  }
}

void ConvertsResidentPagesToBytes() {
  const auto result = playden::ps5::detail::ProcessResidentBytesFromPages(321, 16384);
  Check(result.available(), "valid page count is available");
  Check(result.error == playden::ps5::ProcessMemoryError::kNone,
        "valid conversion has no error");
  Check(result.native_error == 0, "valid conversion has no native error");
  Check(result.required_record_bytes == 0,
        "conversion alone has no process-record size diagnostic");
  Check(result.returned_record_bytes == 0,
        "conversion alone has no returned process record");
  Check(result.bytes == 5259264, "resident pages use the supplied platform page size");
}

void RejectsAnInvalidPageSize() {
  const auto result = playden::ps5::detail::ProcessResidentBytesFromPages(1, 0);
  Check(!result.available(), "zero page size is unavailable");
  Check(result.error == playden::ps5::ProcessMemoryError::kInvalidResponse,
        "zero page size reports an invalid response");
  Check(result.bytes == 0, "invalid response does not publish bytes");
}

void RejectsZeroResidentPages() {
  const auto result = playden::ps5::detail::ProcessResidentBytesFromPages(0, 16384);
  Check(!result.available(), "zero resident pages are unavailable");
  Check(result.error == playden::ps5::ProcessMemoryError::kInvalidResponse,
        "zero resident pages report an invalid response");
  Check(result.bytes == 0, "zero resident pages do not publish bytes");
}

void RejectsByteCountOverflow() {
  const std::uint64_t pages = std::numeric_limits<std::uint64_t>::max() / 16384 + 1;
  const auto result =
      playden::ps5::detail::ProcessResidentBytesFromPages(pages, 16384);
  Check(!result.available(), "overflow is unavailable");
  Check(result.error == playden::ps5::ProcessMemoryError::kOverflow,
        "overflow is reported explicitly");
  Check(result.bytes == 0, "overflow does not publish wrapped bytes");
}

void RejectsAnUnknownNativeRecordLayoutWithoutPublishingRss() {
  const auto result = playden::ps5::detail::UnsupportedProcessRecordLayout(1096);
  Check(!result.available(), "unknown process-record layout is unavailable");
  Check(result.error ==
            playden::ps5::ProcessMemoryError::kUnsupportedRecordLayout,
        "unknown process-record layout has a distinct error");
  Check(result.bytes == 0, "unknown process-record layout does not publish RSS");
  Check(result.native_error == 0,
        "successful size query has no invented native error");
  Check(result.required_record_bytes == 1096,
        "unknown process-record layout exposes the advertised size");
  Check(result.returned_record_bytes == 0,
        "unknown process-record layout is rejected before fetch");
}

void HostReportsPlatformQueryUnavailable() {
#if !defined(__PROSPERO__)
  const auto result = playden::ps5::ReadProcessResidentBytes();
  Check(!result.available(), "non-PS5 query is unavailable");
  Check(result.error == playden::ps5::ProcessMemoryError::kQueryUnavailable,
        "non-PS5 query reports its unavailable stage");
  Check(result.native_error == 0, "non-PS5 query has no invented native error");
  Check(result.required_record_bytes == 0,
        "non-PS5 query has no invented required size");
  Check(result.returned_record_bytes == 0,
        "non-PS5 query has no invented returned size");
  Check(result.bytes == 0, "unavailable query does not publish bytes");
#endif
}

}  // namespace

int main() {
  ConvertsResidentPagesToBytes();
  RejectsAnInvalidPageSize();
  RejectsZeroResidentPages();
  RejectsByteCountOverflow();
  RejectsAnUnknownNativeRecordLayoutWithoutPublishingRss();
  HostReportsPlatformQueryUnavailable();
  std::puts("process_memory_test passed");
}
