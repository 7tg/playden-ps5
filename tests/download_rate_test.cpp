#include "steam/download_rate.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace playden::steam;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #value); std::exit(1); } } while(false)
int main() {
  DownloadRate rate;
  CHECK(!rate.Estimate(0, 0, 100000000).bytes_per_second);
  rate.Record(9000000000ull, 0); // resumed bytes seed the baseline; they were not transferred now
  CHECK(!rate.Estimate(0, 9000000000ull, 9100000000ull).bytes_per_second);
  rate.Record(9002000000ull, 1000);
  auto result = rate.Estimate(1000, 9002000000ull, 9100000000ull);
  CHECK(result.bytes_per_second == 2000000 && result.eta_seconds == 49);
  rate.Record(9004000000ull, 2000);
  CHECK(rate.Estimate(2000, 9004000000ull, 9100000000ull).bytes_per_second == 2000000);
  // Time alone changes the estimate, even when no callback arrives during a stall.
  CHECK(rate.Estimate(4000, 9004000000ull, 9100000000ull).bytes_per_second == 1000000);
  result = rate.Estimate(7000, 9004000000ull, 9100000000ull);
  CHECK(result.bytes_per_second == 0 && !result.eta_seconds);
  rate.Reset();
  rate.Record(9004000000ull, 8000);
  CHECK(!rate.Estimate(8500, 9004000000ull, 9100000000ull).bytes_per_second);
  rate.Record(9005000000ull, 9000);
  CHECK(rate.Estimate(9000, 9005000000ull, 0).bytes_per_second == 1000000);
  CHECK(!rate.Estimate(9000, 9005000000ull, 0).eta_seconds);
  CHECK(rate.Estimate(9000, 9005000000ull, 9005000000ull).eta_seconds == 0);
  rate.Record(5, 10000); // regressing progress starts a new baseline
  CHECK(!rate.Estimate(10000, 5, 10).bytes_per_second);
  rate.Record(6, 11000);
  CHECK(rate.Estimate(11000, 6, std::numeric_limits<std::uint64_t>::max()).eta_seconds == std::numeric_limits<int>::max());
  rate.Record(10, 500); // a changed clock resets too
  CHECK(!rate.Estimate(500, 10, 20).bytes_per_second);
}
