#include "diagnostics/frame_stats.h"

#include <cassert>
#include <clocale>
#include <cstdint>
#include <string>

namespace {

using playden::diagnostics::Dimensions;
using playden::diagnostics::DisplayConfig;
using playden::diagnostics::FrameStats;
using playden::diagnostics::FrameStatsConfig;
using playden::diagnostics::RefreshConfig;
using playden::diagnostics::RevisionMetadata;

constexpr std::uint64_t kMillisecond = 1000000;

void TestBoundedIntervalsAndPercentiles() {
  FrameStats stats(FrameStatsConfig{4, 100 * kMillisecond});

  stats.RecordFrameStart(0);
  stats.RecordFrameStart(10 * kMillisecond);
  stats.RecordFrameStart(30 * kMillisecond);
  stats.RecordFrameStart(60 * kMillisecond);
  stats.RecordFrameStart(100 * kMillisecond);
  stats.RecordFrameStart(150 * kMillisecond);

  assert(stats.RecordDue(99 * kMillisecond) == false);
  assert(stats.RecordDue(100 * kMillisecond) == true);

  const auto record = stats.TakeRecord(150 * kMillisecond);
  assert(record.interval_count == 4);
  assert(record.dropped_interval_count == 1);
  assert(record.frame_intervals_ms.size() == 4);
  assert(record.frame_intervals_ms[0] == 20.0);
  assert(record.frame_intervals_ms[3] == 50.0);
  assert(record.p50_ms == 30.0);
  assert(record.p95_ms == 50.0);
  assert(record.p99_ms == 50.0);
  assert(record.max_ms == 50.0);

  const auto empty_record = stats.TakeRecord(200 * kMillisecond);
  assert(empty_record.interval_count == 0);
  assert(empty_record.has_frame_timing == false);
}

void TestPeriodicWindowResetsWithoutLosingFrameBaseline() {
  FrameStats stats(FrameStatsConfig{8, 50 * kMillisecond});
  stats.RecordFrameStart(10 * kMillisecond);
  stats.RecordFrameStart(30 * kMillisecond);
  const auto first = stats.TakeRecord(60 * kMillisecond);
  assert(first.frame_intervals_ms.size() == 1);
  assert(first.frame_intervals_ms[0] == 20.0);

  stats.RecordFrameStart(70 * kMillisecond);
  const auto second = stats.TakeRecord(110 * kMillisecond);
  assert(second.frame_intervals_ms.size() == 1);
  assert(second.frame_intervals_ms[0] == 40.0);
}

void TestUnknownPlatformValuesStayUnknown() {
  FrameStats stats;
  stats.RecordFrameStart(1);
  stats.RecordFrameStart(16000001);

  const auto record = stats.TakeRecord(20000000);
  assert(record.memory_available == false);
  assert(record.display.surface.available == false);
  assert(record.display.render_target.available == false);
  assert(record.display.refresh.available == false);

  const std::string json = FrameStats::FormatRecord(record);
  assert(json.find("\"memory_used_bytes\":null") != std::string::npos);
  assert(json.find("\"render_width\":null") != std::string::npos);
  assert(json.find("\"surface_width\":null") != std::string::npos);
  assert(json.find("\"refresh_hz\":null") != std::string::npos);
  assert(json.find("\"missed_presentations\":null") != std::string::npos);
  assert(json.find("\"missed_presentations_source\":\"unknown\"") !=
         std::string::npos);
  assert(json.find("estimated") == std::string::npos);
}

void TestMeasuredPlatformValuesAndRevisionsAreRecorded() {
  FrameStats stats;
  DisplayConfig display;
  display.surface = Dimensions::Measured(3840, 2160);
  display.render_target = Dimensions::Measured(3840, 2160);
  display.refresh = RefreshConfig::Measured(59.94);
  stats.SetDisplayConfig(display);
  stats.SetRevisionMetadata(
      RevisionMetadata{"build-\"abc\"", "rmlfocus-dep", "ps5-test"});
  stats.RecordMemory(123456789);
  stats.RecordMemory(123456999);

  const auto record = stats.TakeRecord(42);
  assert(record.memory_available);
  assert(record.memory_used_bytes == 123456999);
  assert(record.memory_peak_bytes == 123456999);
  assert(record.memory_sample_count == 2);

  const std::string json = FrameStats::FormatRecord(record);
  assert(json.find("\"surface_width\":3840") != std::string::npos);
  assert(json.find("\"render_height\":2160") != std::string::npos);
  assert(json.find("\"refresh_hz\":59.940") != std::string::npos);
  assert(json.find("\"memory_used_bytes\":123456999") != std::string::npos);
  assert(json.find("\"memory_peak_bytes\":123456999") != std::string::npos);
  assert(json.find("\"build_revision\":\"build-\\\"abc\\\"\"") !=
         std::string::npos);
  assert(json.find("\"dependency_revision\":\"rmlfocus-dep\"") !=
         std::string::npos);
  assert(json.find("\"environment\":\"ps5-test\"") != std::string::npos);
}

void TestNonMonotonicFrameStartsAreRejectedExplicitly() {
  FrameStats stats;
  stats.RecordFrameStart(100);
  stats.RecordFrameStart(90);
  stats.RecordFrameStart(110);

  const auto record = stats.TakeRecord(200);
  assert(record.invalid_timestamp_count == 1);
  assert(record.frame_intervals_ms.size() == 1);
  assert(record.frame_intervals_ms[0] == 0.000010);
}

void TestJsonNumbersIgnoreTheProcessNumericLocale() {
  const char* previous_locale_name = std::setlocale(LC_NUMERIC, nullptr);
  assert(previous_locale_name != nullptr);
  const std::string previous_locale(previous_locale_name);
  assert(std::setlocale(LC_NUMERIC, "de_DE.UTF-8") != nullptr);

  FrameStats stats;
  DisplayConfig display;
  display.refresh = RefreshConfig::Measured(59.94);
  stats.SetDisplayConfig(display);
  stats.RecordFrameStart(0);
  stats.RecordFrameStart(16670000);
  const std::string json = FrameStats::FormatRecord(stats.TakeRecord(16670000));

  assert(std::setlocale(LC_NUMERIC, previous_locale.c_str()) != nullptr);
  assert(json.find("\"frame_intervals_ms\":[16.670]") != std::string::npos);
  assert(json.find("\"refresh_hz\":59.940") != std::string::npos);
}

}  // namespace

int main() {
  TestBoundedIntervalsAndPercentiles();
  TestPeriodicWindowResetsWithoutLosingFrameBaseline();
  TestUnknownPlatformValuesStayUnknown();
  TestMeasuredPlatformValuesAndRevisionsAreRecorded();
  TestNonMonotonicFrameStartsAreRejectedExplicitly();
  TestJsonNumbersIgnoreTheProcessNumericLocale();
  return 0;
}
