#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace playden::diagnostics {

struct Dimensions {
  bool available = false;
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  static Dimensions Unknown();
  static Dimensions Measured(std::uint32_t width, std::uint32_t height);
};

struct RefreshConfig {
  bool available = false;
  double hz = 0.0;

  static RefreshConfig Unknown();
  static RefreshConfig Measured(double hz);
};

struct DisplayConfig {
  Dimensions surface;
  Dimensions render_target;
  RefreshConfig refresh;
};

struct RevisionMetadata {
  std::string build_revision;
  std::string dependency_revision;
  std::string environment;
};

struct FrameStatsConfig {
  std::size_t interval_capacity = 2048;
  std::uint64_t record_period_ns = 5000000000ULL;
};

struct FrameRecord {
  std::uint64_t record_start_ns = 0;
  std::uint64_t record_end_ns = 0;
  std::uint64_t total_frame_start_count = 0;
  std::size_t interval_count = 0;
  std::uint64_t dropped_interval_count = 0;
  std::uint64_t invalid_timestamp_count = 0;
  bool has_frame_timing = false;
  double p50_ms = 0.0;
  double p95_ms = 0.0;
  double p99_ms = 0.0;
  double max_ms = 0.0;
  std::vector<double> frame_intervals_ms;

  bool memory_available = false;
  std::uint64_t memory_used_bytes = 0;
  std::uint64_t memory_peak_bytes = 0;
  std::uint64_t memory_sample_count = 0;

  DisplayConfig display;
  RevisionMetadata revisions;
};

class FrameStats {
 public:
  static constexpr std::size_t kMaximumIntervalCapacity = 4096;

  explicit FrameStats(FrameStatsConfig config = {});

  // The platform supplies a monotonic frame-start timestamp. The interval
  // between consecutive starts is measured; render or present duration is not
  // inferred from it.
  void RecordFrameStart(std::uint64_t monotonic_ns);

  // The platform owns process-memory measurement and supplies the value here.
  void RecordMemory(std::uint64_t used_bytes);
  void SetDisplayConfig(const DisplayConfig& display);
  void SetRevisionMetadata(const RevisionMetadata& revisions);

  bool RecordDue(std::uint64_t monotonic_ns) const;
  FrameRecord TakeRecord(std::uint64_t monotonic_ns);

  // Produces one JSON object without relying on iostream or locale support.
  // Missed presentations are explicitly unknown because frame-start timing
  // cannot establish hardware presentation outcomes.
  static std::string FormatRecord(const FrameRecord& record);

 private:
  FrameStatsConfig config_;
  std::vector<std::uint64_t> intervals_ns_;
  std::size_t next_interval_ = 0;
  std::uint64_t dropped_interval_count_ = 0;
  std::uint64_t invalid_timestamp_count_ = 0;
  std::uint64_t total_frame_start_count_ = 0;

  bool has_previous_frame_start_ = false;
  std::uint64_t previous_frame_start_ns_ = 0;
  bool has_record_start_ = false;
  std::uint64_t record_start_ns_ = 0;

  bool memory_available_ = false;
  std::uint64_t memory_used_bytes_ = 0;
  std::uint64_t memory_peak_bytes_ = 0;
  std::uint64_t memory_sample_count_ = 0;

  DisplayConfig display_;
  RevisionMetadata revisions_;
};

}  // namespace playden::diagnostics
