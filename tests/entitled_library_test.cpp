#include "steam/backend/entitled_library.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <map>
#include <string>
#include <vector>
#include <thread>

using namespace playden;
using namespace playden::steam;
using namespace std::chrono_literals;

namespace {

struct Owned final : LibraryBackend {
  LibraryResult result{LibraryError::kNone, {{10, "Ten", {}, {}, {}}, {20, "Twenty", {}, {}, {}}}, false};
  LibraryResult Fetch(const StoredSession&, const std::atomic<bool>&) override { return result; }
};

struct Acquisitions final : AcquisitionSource {
  AcquisitionResult result{CmError::kNone, {{10, 1500000000u}, {99, 1400000000u}}};
  bool block_until_stopped = false;
  std::atomic<bool> saw_stop{false};
  std::map<std::uint32_t, std::string> app_info;
  std::vector<std::vector<std::uint32_t>> app_info_requests;
  AppInfoBuffers FetchAppInfo(const StoredSession&, const std::vector<std::uint32_t>& ids,
                              const std::atomic<bool>&) override {
    app_info_requests.push_back(ids);
    AppInfoBuffers result{CmError::kNone, {}};
    for (const auto id : ids)
      if (const auto found = app_info.find(id); found != app_info.end()) result.buffers[id] = found->second;
    return result;
  }
  AcquisitionResult FetchAcquisitionDates(const StoredSession&, const std::atomic<bool>& cancel) override {
    while (block_until_stopped) {
      if (cancel) {
        saw_stop = true;
        return {CmError::kCanceled, {}};
      }
      std::this_thread::sleep_for(1ms);
    }
    return result;
  }
};

StoredSession Session() { return StoredSession{123, "synthetic", "refresh", "access", {}}; }

void DatesMergeIntoOwnedGames() {
  Owned owned;
  Acquisitions acquisitions;
  EntitledLibraryBackend backend(owned, acquisitions);
  std::atomic<bool> cancel{false};
  const auto result = backend.Fetch(Session(), cancel);
  assert(result.error == LibraryError::kNone && result.games.size() == 2);
  assert(result.games[0].added_at_unix_seconds == 1500000000u);
  // App 99 is licensed but not in the owned list; app 20 has no license date.
  assert(!result.games[1].added_at_unix_seconds);
}

void CmFailureIsNeverFatal() {
  Owned owned;
  Acquisitions acquisitions;
  acquisitions.result = {CmError::kRejected, {}};
  EntitledLibraryBackend backend(owned, acquisitions);
  std::atomic<bool> cancel{false};
  const auto result = backend.Fetch(Session(), cancel);
  assert(result.error == LibraryError::kNone && result.games.size() == 2);
  assert(!result.games[0].added_at_unix_seconds && !result.games[1].added_at_unix_seconds);
  assert(backend.last_acquisition_error() == CmError::kRejected);
}

void OwnedFailureStopsTheCmSession() {
  Owned owned;
  owned.result = {LibraryError::kCredentialsRevoked, {}, false};
  Acquisitions acquisitions;
  acquisitions.block_until_stopped = true;
  EntitledLibraryBackend backend(owned, acquisitions);
  std::atomic<bool> cancel{false};
  const auto start = std::chrono::steady_clock::now();
  const auto result = backend.Fetch(Session(), cancel);
  assert(result.error == LibraryError::kCredentialsRevoked && acquisitions.saw_stop);
  assert(std::chrono::steady_clock::now() - start < 1s);
}

void CancelReachesARunningCmSession() {
  Owned owned;
  Acquisitions acquisitions;
  acquisitions.block_until_stopped = true;
  EntitledLibraryBackend backend(owned, acquisitions);
  std::atomic<bool> cancel{false};
  std::thread canceler([&cancel] {
    std::this_thread::sleep_for(30ms);
    cancel = true;
  });
  const auto result = backend.Fetch(Session(), cancel);
  canceler.join();
  assert(acquisitions.saw_stop && result.error == LibraryError::kNone);
  assert(!result.games[0].added_at_unix_seconds);
}

std::string AppInfoText(std::uint32_t id, const std::string& name, const std::string& type) {
  return "\"appinfo\"\n{\n\t\"appid\"\t\"" + std::to_string(id) + "\"\n\t\"common\"\n\t{\n\t\t\"name\"\t\"" + name +
         "\"\n\t\t\"type\"\t\"" + type + "\"\n\t\t\"icon\"\t\"abc123\"\n\t}\n}\n";
}

// A free-to-play game added with "Add to library" and never played: GetOwnedGames leaves it out,
// its free license names it. Packages with a listed game, DLC and tools add nothing.
void LicensedFreeGamesAreAdded() {
  Owned owned;
  Acquisitions acquisitions;
  acquisitions.result.acquired_at[2767030] = 1700000000u;
  acquisitions.result.package_apps = {{0, {480}},               // Steam's basic free package
                                      {100, {10, 11}},          // a bought game and its DLC
                                      {200, {2767030}},         // the free game
                                      {300, {30}},              // DLC bought on its own
                                      {400, {40}}};             // an app Steam doesn't describe
  acquisitions.app_info = {{2767030, AppInfoText(2767030, "Synthetic Rivals", "Game")},
                           {30, AppInfoText(30, "Synthetic DLC", "DLC")},
                           {480, AppInfoText(480, "Spacewar", "Game")}};
  EntitledLibraryBackend backend(owned, acquisitions);
  std::atomic<bool> cancel{false};
  auto result = backend.Fetch(Session(), cancel);
  assert(result.error == LibraryError::kNone && result.games.size() == 3);
  const auto& added = result.games.back();
  assert(added.id == 2767030 && added.title == "Synthetic Rivals" && added.added_at_unix_seconds == 1700000000u);
  assert(added.artwork_source == "https://media.steampowered.com/steamcommunity/public/images/apps/2767030/abc123.jpg");
  assert(acquisitions.app_info_requests.size() == 1);
  assert((acquisitions.app_info_requests[0] == std::vector<std::uint32_t>{30, 40, 2767030}));

  // A refresh asks only about apps it hasn't seen this run.
  acquisitions.result.package_apps[500] = {50};
  acquisitions.app_info[50] = AppInfoText(50, "Synthetic New", "game");
  result = backend.Fetch(Session(), cancel);
  assert(result.games.size() == 4 && std::any_of(result.games.begin(), result.games.end(),
                                                  [](const auto& game) { return game.title == "Synthetic New"; }));
  assert(acquisitions.app_info_requests.size() == 2 &&
         (acquisitions.app_info_requests[1] == std::vector<std::uint32_t>{50}));
  result = backend.Fetch(Session(), cancel);
  assert(result.games.size() == 4 && acquisitions.app_info_requests.size() == 2);
}

}  // namespace

int main() {
  LicensedFreeGamesAreAdded();
  DatesMergeIntoOwnedGames();
  CmFailureIsNeverFatal();
  OwnedFailureStopsTheCmSession();
  CancelReachesARunningCmSession();
}
