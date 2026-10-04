#include "platform/ps5/memory_probe.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

using playden::ps5::MemoryProbeStatus;

void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", description);
    std::exit(1);
  }
}

template <typename T>
void Put(std::array<std::uint8_t, 1096>& record, std::size_t offset,
         T value) {
  std::memcpy(record.data() + offset, &value, sizeof(value));
}

std::array<std::uint8_t, 1096> ValidRecord() {
  std::array<std::uint8_t, 1096> record{};
  Put<std::int32_t>(record, 0, 1096);
  Put<std::int32_t>(record, 72, 41);
  Put<std::int32_t>(record, 76, 7);
  Put<std::uint64_t>(record, 256, 256ULL * 1024ULL * 1024ULL);
  Put<std::int64_t>(record, 264, 4096);
  return record;
}

void ParsesTheValidatedPrefix() {
  const auto record = ValidRecord();
  const auto sample = playden::ps5::detail::ParseMemoryProbeRecord(
      record.data(), record.size(), 41, 7, 16384);
  Check(sample.status == MemoryProbeStatus::kOk, "valid prefix is accepted");
  Check(sample.virtual_bytes == 268435456, "virtual byte field is preserved");
  Check(sample.resident_pages == 4096, "resident page field is preserved");
  Check(sample.resident_bytes == 67108864, "resident pages use 16 KiB pages");
  Check(sample.page_bytes == 16384, "validated page size is reported");
}

void RejectsUnknownRecordSize() {
  const auto record = ValidRecord();
  const auto sample = playden::ps5::detail::ParseMemoryProbeRecord(
      record.data(), 1088, 41, 7, 16384);
  Check(sample.status == MemoryProbeStatus::kUnexpectedRecordSize,
        "only the observed 1096-byte record is parsed");
}

void RejectsMismatchedIdentityWithoutPublishingMemory() {
  auto record = ValidRecord();
  Put<std::int32_t>(record, 72, 42);
  auto sample = playden::ps5::detail::ParseMemoryProbeRecord(
      record.data(), record.size(), 41, 7, 16384);
  Check(sample.status == MemoryProbeStatus::kProcessIdMismatch,
        "mismatched process id is rejected");
  Check(sample.resident_bytes == 0, "rejected identity publishes no bytes");

  record = ValidRecord();
  Put<std::int32_t>(record, 76, 8);
  sample = playden::ps5::detail::ParseMemoryProbeRecord(
      record.data(), record.size(), 41, 7, 16384);
  Check(sample.status == MemoryProbeStatus::kParentProcessIdMismatch,
        "mismatched parent process id is rejected");
}

void RejectsInvalidMemoryRelationships() {
  auto record = ValidRecord();
  Put<std::int64_t>(record, 264, -1);
  auto sample = playden::ps5::detail::ParseMemoryProbeRecord(
      record.data(), record.size(), 41, 7, 16384);
  Check(sample.status == MemoryProbeStatus::kResidentPagesInvalid,
        "negative resident pages are rejected");

  record = ValidRecord();
  Put<std::int64_t>(record, 264, std::numeric_limits<std::int64_t>::max());
  sample = playden::ps5::detail::ParseMemoryProbeRecord(
      record.data(), record.size(), 41, 7, 16384);
  Check(sample.status == MemoryProbeStatus::kByteCountOverflow,
        "resident-byte overflow is rejected");

  record = ValidRecord();
  Put<std::uint64_t>(record, 256, 1024);
  sample = playden::ps5::detail::ParseMemoryProbeRecord(
      record.data(), record.size(), 41, 7, 16384);
  Check(sample.status == MemoryProbeStatus::kResidentExceedsVirtual,
        "resident bytes cannot exceed virtual bytes");
}

void RejectsWrongPageSizeAndEmbeddedSize() {
  auto record = ValidRecord();
  auto sample = playden::ps5::detail::ParseMemoryProbeRecord(
      record.data(), record.size(), 41, 7, 4096);
  Check(sample.status == MemoryProbeStatus::kPageSizeMismatch,
        "probe requires the observed PS5 16 KiB page size");

  Put<std::int32_t>(record, 0, 1088);
  sample = playden::ps5::detail::ParseMemoryProbeRecord(
      record.data(), record.size(), 41, 7, 16384);
  Check(sample.status == MemoryProbeStatus::kEmbeddedRecordSizeMismatch,
        "embedded record size must match fetched storage");
}

}  // namespace

int main() {
  ParsesTheValidatedPrefix();
  RejectsUnknownRecordSize();
  RejectsMismatchedIdentityWithoutPublishingMemory();
  RejectsInvalidMemoryRelationships();
  RejectsWrongPageSizeAndEmbeddedSize();
  std::puts("memory_probe_test passed");
}
