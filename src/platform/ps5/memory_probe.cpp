#include "platform/ps5/memory_probe.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>

#if defined(__PROSPERO__)
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <unistd.h>
#endif

namespace playden::ps5 {
namespace {

constexpr std::size_t kObservedRecordBytes = 1096;
constexpr std::uint64_t kExpectedPageBytes = 16384;
constexpr std::size_t kStructSizeOffset = 0;
constexpr std::size_t kProcessIdOffset = 72;
constexpr std::size_t kParentProcessIdOffset = 76;
constexpr std::size_t kVirtualBytesOffset = 256;
constexpr std::size_t kResidentPagesOffset = 264;
#if defined(__PROSPERO__)
constexpr std::size_t kMappingBytes = 32 * 1024 * 1024;
constexpr unsigned int kSettleMicroseconds = 100000;
static_assert(sizeof(pid_t) == sizeof(std::int32_t));
static_assert(kResidentPagesOffset + sizeof(std::int64_t) <=
              kObservedRecordBytes);
#endif

template <typename T>
T ReadScalar(const std::uint8_t* record, std::size_t offset) {
  T value{};
  std::memcpy(&value, record + offset, sizeof(value));
  return value;
}

#if defined(__PROSPERO__)
void LogSample(std::uint32_t phase, const MemoryProbeSample& sample,
               std::uint64_t mapping_bytes, std::int32_t delay_error) {
  std::printf(
      "{\"memory_probe\":1,\"phase\":%u,\"status\":%u,"
      "\"native_error\":%d,\"required_record_bytes\":%llu,"
      "\"returned_record_bytes\":%llu,\"virtual_bytes\":%llu,"
      "\"resident_pages\":%llu,\"resident_bytes\":%llu,"
      "\"page_bytes\":%llu,\"mapping_bytes\":%llu,"
      "\"delay_error\":%d}\n",
      static_cast<unsigned>(phase), static_cast<unsigned>(sample.status),
      sample.native_error,
      static_cast<unsigned long long>(sample.required_record_bytes),
      static_cast<unsigned long long>(sample.returned_record_bytes),
      static_cast<unsigned long long>(sample.virtual_bytes),
      static_cast<unsigned long long>(sample.resident_pages),
      static_cast<unsigned long long>(sample.resident_bytes),
      static_cast<unsigned long long>(sample.page_bytes),
      static_cast<unsigned long long>(mapping_bytes), delay_error);
  std::fflush(stdout);
}

MemoryProbeSample SampleCurrentProcess() {
  MemoryProbeSample sample;

  errno = 0;
  const pid_t process_id = getpid();
  if (process_id <= 0) {
    sample.status = MemoryProbeStatus::kProcessIdUnavailable;
    sample.native_error = errno;
    return sample;
  }

  errno = 0;
  const pid_t parent_process_id = getppid();
  if (parent_process_id < 0) {
    sample.status = MemoryProbeStatus::kParentProcessIdUnavailable;
    sample.native_error = errno;
    return sample;
  }

  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, process_id};
  std::size_t required_bytes = 0;
  errno = 0;
  if (sysctl(mib, 4, nullptr, &required_bytes, nullptr, 0) != 0) {
    sample.status = MemoryProbeStatus::kSizeQueryFailed;
    sample.native_error = errno;
    sample.required_record_bytes = required_bytes;
    return sample;
  }
  sample.required_record_bytes = required_bytes;
  if (required_bytes != kObservedRecordBytes) {
    sample.status = MemoryProbeStatus::kUnexpectedRecordSize;
    return sample;
  }

  std::array<std::uint8_t, kObservedRecordBytes> record{};
  std::size_t returned_bytes = record.size();
  errno = 0;
  if (sysctl(mib, 4, record.data(), &returned_bytes, nullptr, 0) != 0) {
    sample.status = MemoryProbeStatus::kFetchFailed;
    sample.native_error = errno;
    sample.returned_record_bytes = returned_bytes;
    return sample;
  }
  sample.returned_record_bytes = returned_bytes;
  if (returned_bytes != record.size()) {
    sample.status = MemoryProbeStatus::kReturnedRecordSizeMismatch;
    return sample;
  }

  const int runtime_page_bytes = getpagesize();
  sample = detail::ParseMemoryProbeRecord(
      record.data(), returned_bytes, static_cast<std::int32_t>(process_id),
      static_cast<std::int32_t>(parent_process_id),
      runtime_page_bytes > 0 ? static_cast<std::uint64_t>(runtime_page_bytes)
                             : 0);
  sample.required_record_bytes = required_bytes;
  sample.returned_record_bytes = returned_bytes;
  return sample;
}

std::int32_t Settle() {
  errno = 0;
  if (usleep(kSettleMicroseconds) == 0) return 0;
  return errno;
}
#endif

}  // namespace

namespace detail {

MemoryProbeSample ParseMemoryProbeRecord(
    const std::uint8_t* record, std::size_t record_bytes,
    std::int32_t expected_process_id, std::int32_t expected_parent_process_id,
    std::uint64_t page_bytes) {
  MemoryProbeSample sample;
  sample.returned_record_bytes = record_bytes;
  sample.page_bytes = page_bytes;
  if (record == nullptr) {
    sample.status = MemoryProbeStatus::kInvalidRecordStorage;
    return sample;
  }
  if (record_bytes != kObservedRecordBytes) {
    sample.status = MemoryProbeStatus::kUnexpectedRecordSize;
    return sample;
  }
  if (page_bytes != kExpectedPageBytes) {
    sample.status = MemoryProbeStatus::kPageSizeMismatch;
    return sample;
  }

  const auto embedded_bytes = ReadScalar<std::int32_t>(record, kStructSizeOffset);
  if (embedded_bytes != static_cast<std::int32_t>(record_bytes)) {
    sample.status = MemoryProbeStatus::kEmbeddedRecordSizeMismatch;
    return sample;
  }
  if (ReadScalar<std::int32_t>(record, kProcessIdOffset) !=
      expected_process_id) {
    sample.status = MemoryProbeStatus::kProcessIdMismatch;
    return sample;
  }
  if (ReadScalar<std::int32_t>(record, kParentProcessIdOffset) !=
      expected_parent_process_id) {
    sample.status = MemoryProbeStatus::kParentProcessIdMismatch;
    return sample;
  }

  const auto resident_pages =
      ReadScalar<std::int64_t>(record, kResidentPagesOffset);
  if (resident_pages <= 0) {
    sample.status = MemoryProbeStatus::kResidentPagesInvalid;
    return sample;
  }
  const auto unsigned_pages = static_cast<std::uint64_t>(resident_pages);
  if (unsigned_pages >
      std::numeric_limits<std::uint64_t>::max() / page_bytes) {
    sample.status = MemoryProbeStatus::kByteCountOverflow;
    return sample;
  }

  const std::uint64_t virtual_bytes =
      ReadScalar<std::uint64_t>(record, kVirtualBytesOffset);
  const std::uint64_t resident_bytes = unsigned_pages * page_bytes;
  if (resident_bytes > virtual_bytes) {
    sample.status = MemoryProbeStatus::kResidentExceedsVirtual;
    return sample;
  }

  sample.status = MemoryProbeStatus::kOk;
  sample.virtual_bytes = virtual_bytes;
  sample.resident_pages = unsigned_pages;
  sample.resident_bytes = resident_bytes;
  return sample;
}

}  // namespace detail

void RunMemoryProbe() {
#if defined(__PROSPERO__)
  LogSample(0, SampleCurrentProcess(), 0, 0);

  errno = 0;
  void* mapping = mmap(nullptr, kMappingBytes, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANON, -1, 0);
  if (mapping == MAP_FAILED) {
    MemoryProbeSample failed;
    failed.status = MemoryProbeStatus::kMapFailed;
    failed.native_error = errno;
    LogSample(1, failed, kMappingBytes, 0);
    LogSample(2, failed, 0, 0);
    return;
  }

  auto* bytes = static_cast<volatile std::uint8_t*>(mapping);
  for (std::size_t offset = 0; offset < kMappingBytes;
       offset += kExpectedPageBytes) {
    bytes[offset] = static_cast<std::uint8_t>(offset / kExpectedPageBytes);
  }
  const std::int32_t touched_delay_error = Settle();
  LogSample(1, SampleCurrentProcess(), kMappingBytes, touched_delay_error);

  errno = 0;
  if (munmap(mapping, kMappingBytes) != 0) {
    MemoryProbeSample failed;
    failed.status = MemoryProbeStatus::kUnmapFailed;
    failed.native_error = errno;
    LogSample(2, failed, kMappingBytes, 0);
    return;
  }

  const std::int32_t unmapped_delay_error = Settle();
  LogSample(2, SampleCurrentProcess(), 0, unmapped_delay_error);
#endif
}

}  // namespace playden::ps5
