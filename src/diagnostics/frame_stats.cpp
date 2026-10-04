#include "diagnostics/frame_stats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace playden::diagnostics {
namespace {

constexpr double kNanosecondsPerMillisecond = 1000000.0;

void AppendUnsigned(std::string& output, std::uint64_t value) {
  char buffer[32];
  const int length = std::snprintf(buffer, sizeof(buffer), "%llu",
                                   static_cast<unsigned long long>(value));
  if (length > 0) {
    output.append(buffer, static_cast<std::size_t>(length));
  }
}

void AppendSize(std::string& output, std::size_t value) {
  AppendUnsigned(output, static_cast<std::uint64_t>(value));
}

void AppendDouble(std::string& output, double value) {
  constexpr std::uint64_t kScale = 1000;
  const long double scaled =
      static_cast<long double>(value) * static_cast<long double>(kScale) + 0.5L;
  if (!std::isfinite(value) || value < 0.0 ||
      scaled >= static_cast<long double>(
                    std::numeric_limits<std::uint64_t>::max())) {
    output.append("null");
    return;
  }

  const auto scaled_integer = static_cast<std::uint64_t>(scaled);
  AppendUnsigned(output, scaled_integer / kScale);
  output.push_back('.');
  const std::uint64_t fraction = scaled_integer % kScale;
  output.push_back(static_cast<char>('0' + (fraction / 100) % 10));
  output.push_back(static_cast<char>('0' + (fraction / 10) % 10));
  output.push_back(static_cast<char>('0' + fraction % 10));
}

void AppendJsonString(std::string& output, const std::string& value) {
  static constexpr char kHex[] = "0123456789abcdef";
  output.push_back('"');
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    switch (character) {
      case '"':
        output.append("\\\"");
        break;
      case '\\':
        output.append("\\\\");
        break;
      case '\b':
        output.append("\\b");
        break;
      case '\f':
        output.append("\\f");
        break;
      case '\n':
        output.append("\\n");
        break;
      case '\r':
        output.append("\\r");
        break;
      case '\t':
        output.append("\\t");
        break;
      default:
        if (character < 0x20) {
          output.append("\\u00");
          output.push_back(kHex[(character >> 4U) & 0x0fU]);
          output.push_back(kHex[character & 0x0fU]);
        } else {
          output.push_back(static_cast<char>(character));
        }
    }
  }
  output.push_back('"');
}

void AppendNullableString(std::string& output, const std::string& value) {
  if (value.empty()) {
    output.append("null");
  } else {
    AppendJsonString(output, value);
  }
}

double Percentile(const std::vector<std::uint64_t>& sorted, double fraction) {
  const double rank = std::ceil(fraction * static_cast<double>(sorted.size()));
  std::size_t index = static_cast<std::size_t>(rank);
  if (index == 0) {
    index = 1;
  }
  return static_cast<double>(sorted[index - 1]) /
         kNanosecondsPerMillisecond;
}

void AppendDimensions(std::string& output, const char* width_name,
                      const char* height_name, const char* source_name,
                      const Dimensions& dimensions) {
  output.append(",\"");
  output.append(width_name);
  output.append("\":");
  if (dimensions.available) {
    AppendUnsigned(output, dimensions.width);
  } else {
    output.append("null");
  }
  output.append(",\"");
  output.append(height_name);
  output.append("\":");
  if (dimensions.available) {
    AppendUnsigned(output, dimensions.height);
  } else {
    output.append("null");
  }
  output.append(",\"");
  output.append(source_name);
  output.append("\":\"");
  output.append(dimensions.available ? "measured" : "unknown");
  output.push_back('"');
}

}  // namespace

Dimensions Dimensions::Unknown() { return {}; }

Dimensions Dimensions::Measured(std::uint32_t width, std::uint32_t height) {
  if (width == 0 || height == 0) {
    return Unknown();
  }
  return Dimensions{true, width, height};
}

RefreshConfig RefreshConfig::Unknown() { return {}; }

RefreshConfig RefreshConfig::Measured(double hz) {
  if (!std::isfinite(hz) || hz <= 0.0) {
    return Unknown();
  }
  return RefreshConfig{true, hz};
}

FrameStats::FrameStats(FrameStatsConfig config) : config_(config) {
  if (config_.interval_capacity == 0) {
    config_.interval_capacity = 1;
  } else if (config_.interval_capacity > kMaximumIntervalCapacity) {
    config_.interval_capacity = kMaximumIntervalCapacity;
  }
  intervals_ns_.reserve(config_.interval_capacity);
}

void FrameStats::RecordFrameStart(std::uint64_t monotonic_ns) {
  if (!has_record_start_) {
    has_record_start_ = true;
    record_start_ns_ = monotonic_ns;
  }

  ++total_frame_start_count_;
  if (!has_previous_frame_start_) {
    has_previous_frame_start_ = true;
    previous_frame_start_ns_ = monotonic_ns;
    return;
  }

  if (monotonic_ns <= previous_frame_start_ns_) {
    ++invalid_timestamp_count_;
    return;
  }

  const std::uint64_t interval = monotonic_ns - previous_frame_start_ns_;
  previous_frame_start_ns_ = monotonic_ns;
  if (intervals_ns_.size() < config_.interval_capacity) {
    intervals_ns_.push_back(interval);
    return;
  }

  intervals_ns_[next_interval_] = interval;
  next_interval_ = (next_interval_ + 1) % config_.interval_capacity;
  ++dropped_interval_count_;
}

void FrameStats::RecordMemory(std::uint64_t used_bytes) {
  memory_available_ = true;
  memory_used_bytes_ = used_bytes;
  if (memory_sample_count_ == 0 || used_bytes > memory_peak_bytes_) {
    memory_peak_bytes_ = used_bytes;
  }
  ++memory_sample_count_;
}

void FrameStats::SetDisplayConfig(const DisplayConfig& display) {
  display_ = display;
}

void FrameStats::SetRevisionMetadata(const RevisionMetadata& revisions) {
  revisions_ = revisions;
}

bool FrameStats::RecordDue(std::uint64_t monotonic_ns) const {
  if (!has_record_start_ || monotonic_ns < record_start_ns_) {
    return false;
  }
  return monotonic_ns - record_start_ns_ >= config_.record_period_ns;
}

FrameRecord FrameStats::TakeRecord(std::uint64_t monotonic_ns) {
  FrameRecord record;
  record.record_start_ns = has_record_start_ ? record_start_ns_ : monotonic_ns;
  record.record_end_ns = monotonic_ns;
  record.total_frame_start_count = total_frame_start_count_;
  record.interval_count = intervals_ns_.size();
  record.dropped_interval_count = dropped_interval_count_;
  record.invalid_timestamp_count = invalid_timestamp_count_;
  record.memory_available = memory_available_;
  record.memory_used_bytes = memory_used_bytes_;
  record.memory_peak_bytes = memory_peak_bytes_;
  record.memory_sample_count = memory_sample_count_;
  record.display = display_;
  record.revisions = revisions_;

  record.frame_intervals_ms.reserve(intervals_ns_.size());
  const bool wrapped = intervals_ns_.size() == config_.interval_capacity &&
                       dropped_interval_count_ > 0;
  const std::size_t first = wrapped ? next_interval_ : 0;
  for (std::size_t offset = 0; offset < intervals_ns_.size(); ++offset) {
    const std::size_t index = (first + offset) % intervals_ns_.size();
    record.frame_intervals_ms.push_back(
        static_cast<double>(intervals_ns_[index]) /
        kNanosecondsPerMillisecond);
  }

  if (!intervals_ns_.empty()) {
    std::vector<std::uint64_t> sorted(intervals_ns_);
    std::sort(sorted.begin(), sorted.end());
    record.has_frame_timing = true;
    record.p50_ms = Percentile(sorted, 0.50);
    record.p95_ms = Percentile(sorted, 0.95);
    record.p99_ms = Percentile(sorted, 0.99);
    record.max_ms = static_cast<double>(sorted.back()) /
                    kNanosecondsPerMillisecond;
  }

  intervals_ns_.clear();
  next_interval_ = 0;
  dropped_interval_count_ = 0;
  invalid_timestamp_count_ = 0;
  has_record_start_ = true;
  record_start_ns_ = monotonic_ns;
  memory_available_ = false;
  memory_used_bytes_ = 0;
  memory_peak_bytes_ = 0;
  memory_sample_count_ = 0;
  return record;
}

std::string FrameStats::FormatRecord(const FrameRecord& record) {
  std::string output;
  output.reserve(512 + record.frame_intervals_ms.size() * 10);
  output.append("{\"record_start_ns\":");
  AppendUnsigned(output, record.record_start_ns);
  output.append(",\"record_end_ns\":");
  AppendUnsigned(output, record.record_end_ns);
  output.append(",\"total_frame_start_count\":");
  AppendUnsigned(output, record.total_frame_start_count);
  output.append(",\"interval_count\":");
  AppendSize(output, record.interval_count);
  output.append(",\"dropped_interval_count\":");
  AppendUnsigned(output, record.dropped_interval_count);
  output.append(",\"invalid_timestamp_count\":");
  AppendUnsigned(output, record.invalid_timestamp_count);

  output.append(",\"frame_intervals_ms\":[");
  for (std::size_t index = 0; index < record.frame_intervals_ms.size();
       ++index) {
    if (index != 0) {
      output.push_back(',');
    }
    AppendDouble(output, record.frame_intervals_ms[index]);
  }
  output.push_back(']');

  const auto append_timing = [&output, &record](const char* name,
                                                double value) {
    output.append(",\"");
    output.append(name);
    output.append("\":");
    if (record.has_frame_timing) {
      AppendDouble(output, value);
    } else {
      output.append("null");
    }
  };
  append_timing("p50_ms", record.p50_ms);
  append_timing("p95_ms", record.p95_ms);
  append_timing("p99_ms", record.p99_ms);
  append_timing("max_ms", record.max_ms);

  output.append(",\"memory_used_bytes\":");
  if (record.memory_available) {
    AppendUnsigned(output, record.memory_used_bytes);
  } else {
    output.append("null");
  }
  output.append(",\"memory_peak_bytes\":");
  if (record.memory_available) {
    AppendUnsigned(output, record.memory_peak_bytes);
  } else {
    output.append("null");
  }
  output.append(",\"memory_sample_count\":");
  AppendUnsigned(output, record.memory_sample_count);
  output.append(",\"memory_source\":\"");
  output.append(record.memory_available ? "measured" : "unknown");
  output.push_back('"');

  AppendDimensions(output, "surface_width", "surface_height",
                   "surface_dimensions_source", record.display.surface);
  AppendDimensions(output, "render_width", "render_height",
                   "render_dimensions_source", record.display.render_target);
  output.append(",\"refresh_hz\":");
  if (record.display.refresh.available) {
    AppendDouble(output, record.display.refresh.hz);
  } else {
    output.append("null");
  }
  output.append(",\"refresh_source\":\"");
  output.append(record.display.refresh.available ? "configured" : "unknown");
  output.push_back('"');

  output.append(",\"missed_presentations\":null");
  output.append(",\"missed_presentations_source\":\"unknown\"");
  output.append(",\"build_revision\":");
  AppendNullableString(output, record.revisions.build_revision);
  output.append(",\"dependency_revision\":");
  AppendNullableString(output, record.revisions.dependency_revision);
  output.append(",\"environment\":");
  AppendNullableString(output, record.revisions.environment);
  output.push_back('}');
  return output;
}

}  // namespace playden::diagnostics
