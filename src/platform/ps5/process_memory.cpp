#include "platform/ps5/process_memory.h"

#include <cerrno>
#include <limits>

#if defined(__PROSPERO__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <unistd.h>

#include <cstddef>
#endif

namespace playden::ps5 {
namespace {

constexpr ProcessMemoryResult Failure(ProcessMemoryError error,
                                      std::int32_t native_error = 0,
                                      std::uint64_t required_record_bytes = 0,
                                      std::uint64_t returned_record_bytes = 0) {
  return {0, error, native_error, required_record_bytes, returned_record_bytes};
}

}  // namespace

namespace detail {

ProcessMemoryResult ProcessResidentBytesFromPages(std::uint64_t resident_pages,
                                                  std::uint64_t page_bytes) {
  if (resident_pages == 0 || page_bytes == 0)
    return Failure(ProcessMemoryError::kInvalidResponse);
  if (resident_pages > std::numeric_limits<std::uint64_t>::max() / page_bytes)
    return Failure(ProcessMemoryError::kOverflow);
  return {resident_pages * page_bytes, ProcessMemoryError::kNone, 0, 0, 0};
}

ProcessMemoryResult UnsupportedProcessRecordLayout(
    std::uint64_t required_record_bytes) {
  return Failure(ProcessMemoryError::kUnsupportedRecordLayout, 0,
                 required_record_bytes);
}

}  // namespace detail

ProcessMemoryResult ReadProcessResidentBytes() {
#if defined(__PROSPERO__)
  // The v0.43 payload SDK is pinned by playden-ps5/toolchain.lock.json. These
  // checks turn a header/layout drift into a build failure instead of reading
  // a different field as RSS.
  static_assert(sizeof(kinfo_proc) == KINFO_PROC_SIZE);
  static_assert(sizeof(kinfo_proc) == 1088);
  static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));
  static_assert(offsetof(kinfo_proc, ki_structsize) == 0);
  static_assert(offsetof(kinfo_proc, ki_pid) == 72);
  static_assert(offsetof(kinfo_proc, ki_rssize) == 264);
  static_assert(sizeof(((kinfo_proc*)nullptr)->ki_rssize) == sizeof(std::int64_t));
  static_assert(offsetof(kinfo_proc, ki_rssize) +
                    sizeof(((kinfo_proc*)nullptr)->ki_rssize) <=
                sizeof(kinfo_proc));

  errno = 0;
  const pid_t process_id = getpid();
  if (process_id <= 0)
    return Failure(ProcessMemoryError::kProcessIdUnavailable, errno);

  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, process_id};

  // Query the kernel's record size before providing storage. The pinned SDK's
  // own process-list sample uses this two-call contract. Only the exact ABI
  // described by the pinned headers is safe to interpret as kinfo_proc.
  std::size_t required_bytes = 0;
  errno = 0;
  if (sysctl(mib, 4, nullptr, &required_bytes, nullptr, 0) != 0) {
    const int query_error = errno;
    return Failure(ProcessMemoryError::kSizeQueryUnavailable, query_error,
                   static_cast<std::uint64_t>(required_bytes));
  }
  if (required_bytes == 0)
    return Failure(ProcessMemoryError::kInvalidResponse);
  if (required_bytes != sizeof(kinfo_proc))
    return detail::UnsupportedProcessRecordLayout(
        static_cast<std::uint64_t>(required_bytes));

  kinfo_proc info = {};
  std::size_t returned_bytes = required_bytes;
  errno = 0;
  if (sysctl(mib, 4, &info, &returned_bytes, nullptr, 0) != 0) {
    const int query_error = errno;
    return Failure(ProcessMemoryError::kQueryUnavailable, query_error,
                   static_cast<std::uint64_t>(required_bytes),
                   static_cast<std::uint64_t>(returned_bytes));
  }
  if (returned_bytes != sizeof(kinfo_proc))
    return Failure(ProcessMemoryError::kInvalidResponse, 0,
                   static_cast<std::uint64_t>(required_bytes),
                   static_cast<std::uint64_t>(returned_bytes));

  // Requiring the complete expected record prevents a compatible prefix from
  // masking an ABI change. ki_layout is reserved, so no value is assumed.
  if (info.ki_structsize != static_cast<int>(sizeof(kinfo_proc)) ||
      info.ki_pid != process_id || info.ki_rssize <= 0)
    return Failure(ProcessMemoryError::kInvalidResponse, 0,
                   static_cast<std::uint64_t>(required_bytes),
                   static_cast<std::uint64_t>(returned_bytes));

  const int page_bytes = getpagesize();
  if (page_bytes <= 0 || page_bytes != PAGE_SIZE)
    return Failure(ProcessMemoryError::kInvalidResponse, 0,
                   static_cast<std::uint64_t>(required_bytes),
                   static_cast<std::uint64_t>(returned_bytes));

  ProcessMemoryResult result = detail::ProcessResidentBytesFromPages(
      static_cast<std::uint64_t>(info.ki_rssize),
      static_cast<std::uint64_t>(page_bytes));
  result.required_record_bytes = static_cast<std::uint64_t>(required_bytes);
  result.returned_record_bytes = static_cast<std::uint64_t>(returned_bytes);
  return result;
#else
  return Failure(ProcessMemoryError::kQueryUnavailable);
#endif
}

}  // namespace playden::ps5
