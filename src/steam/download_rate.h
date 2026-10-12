#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
namespace playden::steam {
struct DownloadEstimate {
  std::optional<std::uint64_t> bytes_per_second;
  std::optional<int> eta_seconds;
};
// Six fixed buckets cover a recent five-second window; all times are steady-clock milliseconds.
class DownloadRate {
 public:
  void Reset() { *this = DownloadRate{}; }
  void Record(std::uint64_t done, std::int64_t now) {
    if (now < 0) { Reset(); return; }
    if (!started_ || now < last_ms_ || done < last_done_) {
      Reset(); started_ = true; start_ms_ = now; last_ms_ = now; last_done_ = done;
      return; // A journal's resumed bytes are the baseline, never new traffic.
    }
    const auto second = now / 1000;
    auto& bucket = buckets_[second % buckets_.size()];
    if (bucket.second != second) bucket = {second, 0};
    bucket.bytes += done - last_done_;
    last_ms_ = now; last_done_ = done;
  }
  DownloadEstimate Estimate(std::int64_t now, std::uint64_t done, std::uint64_t total) const {
    if (!started_ || now < last_ms_ || now - start_ms_ < 1000) return {};
    std::uint64_t bytes = 0;
    for (const auto& bucket : buckets_)
      if (bucket.second >= 0 && now >= bucket.second * 1000 && now - bucket.second * 1000 < 5000)
        bytes += bucket.bytes;
    const auto elapsed = std::min<std::int64_t>(5000, now - start_ms_);
    const long double per_second = static_cast<long double>(bytes) * 1000 / elapsed;
    DownloadEstimate result;
    result.bytes_per_second = per_second >= std::numeric_limits<std::uint64_t>::max()
        ? std::numeric_limits<std::uint64_t>::max() : static_cast<std::uint64_t>(per_second);
    if (total && *result.bytes_per_second) {
      const auto remaining = total > done ? total - done : 0;
      const auto seconds = remaining ? (remaining - 1) / *result.bytes_per_second + 1 : 0;
      result.eta_seconds = static_cast<int>(std::min<std::uint64_t>(seconds, std::numeric_limits<int>::max()));
    }
    return result;
  }
 private:
  struct Bucket { std::int64_t second = -1; std::uint64_t bytes = 0; };
  std::array<Bucket, 6> buckets_{};
  bool started_ = false;
  std::int64_t start_ms_ = 0, last_ms_ = 0;
  std::uint64_t last_done_ = 0;
};
}
