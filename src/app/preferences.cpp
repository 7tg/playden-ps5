#include "app/preferences.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace playden {
namespace {

constexpr char kPreferencesPath[] = "preferences.conf";

std::string ToString(const Bytes& bytes) {
  return std::string(bytes.begin(), bytes.end());
}

// Version 8: one "key=value" line per field, in this order, each ending in a newline.
constexpr const char* kVersionEightKeys[] = {"reduce_motion", "title_store_suffix", "cloud_saves_auto",
                                            "steam_sign_in_password", "downloads_slow_network", "steam_stay_signed_in",
                                            "beta_updates", "download_server"};

bool ParseFlag(const std::string& value, bool* flag) {
  if (value != "0" && value != "1") return false;
  *flag = value == "1";
  return true;
}

bool ValidDownloadServer(const std::string& host) {
  if (host.empty() || host.size() > 253) return false;
  return std::all_of(host.begin(), host.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.';
  });
}

bool ParseVersionEight(const std::string& serialized, Preferences* preferences) {
  constexpr std::string_view kHeader = "version=8\n";
  if (serialized.compare(0, kHeader.size(), kHeader) != 0 || serialized.back() != '\n') return false;
  std::vector<std::string> values;
  std::size_t start = kHeader.size();
  for (const char* key : kVersionEightKeys) {
    const std::string prefix = std::string(key) + "=";
    const auto end = serialized.find('\n', start);
    if (end == std::string::npos || serialized.compare(start, prefix.size(), prefix) != 0) return false;
    values.push_back(serialized.substr(start + prefix.size(), end - start - prefix.size()));
    start = end + 1;
  }
  if (start != serialized.size()) return false;
  Preferences parsed;
  if (!ParseFlag(values[0], &parsed.reduce_motion) || !ParseFlag(values[1], &parsed.title_store_suffix) ||
      !ParseFlag(values[2], &parsed.cloud_saves_auto) || !ParseFlag(values[3], &parsed.steam_sign_in_password) ||
      !ParseFlag(values[4], &parsed.downloads_slow_network) || !ParseFlag(values[5], &parsed.steam_stay_signed_in) ||
      !ParseFlag(values[6], &parsed.beta_updates))
    return false;
  // A host the current rule refuses is dropped without resetting unrelated preferences.
  if (ValidDownloadServer(values[7])) parsed.download_server = values[7];
  *preferences = std::move(parsed);
  return true;
}

}  // namespace

PreferencesStore::PreferencesStore(Storage& storage) : storage_(storage) {}

PreferencesLoadResult PreferencesStore::Load() const {
  const auto ready=storage_.EnsureRootDirectory();
  if(!ready.ok()) return {Preferences{},PreferencesLoadStatus::kDefaultReadError,"Preference storage is unavailable."};
  const auto read = storage_.Read(kPreferencesPath);
  if (!read.ok()) {
    if (read.error() == StorageError::kNotFound) {
      return PreferencesLoadResult{Preferences{},
                                   PreferencesLoadStatus::kDefaultMissing,
                                   std::string()};
    }
    return PreferencesLoadResult{
        Preferences{}, PreferencesLoadStatus::kDefaultReadError,
        "Preferences could not be read; defaults restored: " + read.message()};
  }

  std::string serialized = ToString(read.value());
  Preferences eight;
  if (ParseVersionEight(serialized, &eight))
    return PreferencesLoadResult{std::move(eight), PreferencesLoadStatus::kLoaded, std::string()};
  bool beta_updates = false;
  if (serialized.rfind("version=7\n", 0) == 0) {
    const auto suffix = serialized.rfind("beta_updates=");
    if (suffix == std::string::npos || (serialized.substr(suffix) != "beta_updates=0\n" &&
                                      serialized.substr(suffix) != "beta_updates=1\n"))
      return {Preferences{}, PreferencesLoadStatus::kDefaultCorrupt, "Preferences could not be read; defaults restored."};
    beta_updates = serialized.substr(suffix) == "beta_updates=1\n";
    serialized.erase(suffix);
    serialized.replace(0, 9, "version=6");
  }
  // Version 1 holds only reduce_motion; version 2 adds title_store_suffix; version 3 cloud_saves_auto;
  // version 4 steam_sign_in_password; version 5 downloads_slow_network; version 6 steam_stay_signed_in
  // (on in files older than that). Version 7 appends beta_updates (off in older files); version 8 (above)
  // adds the download server (Automatic in older files).
  for (const int motion : {0, 1}) {
    if (serialized == std::string("version=1\nreduce_motion=") + char('0' + motion) + "\n") {
      Preferences preferences;
      preferences.reduce_motion = motion == 1;
      return PreferencesLoadResult{preferences, PreferencesLoadStatus::kLoaded, std::string()};
    }
    for (const int suffix : {0, 1})
      if (serialized == std::string("version=2\nreduce_motion=") + char('0' + motion) +
                            "\ntitle_store_suffix=" + char('0' + suffix) + "\n") {
        Preferences preferences;
        preferences.reduce_motion = motion == 1;
        preferences.title_store_suffix = suffix == 1;
        return PreferencesLoadResult{preferences, PreferencesLoadStatus::kLoaded, std::string()};
      }
    for (const int suffix : {0, 1})
      for (const int cloud : {0, 1})
        if (serialized == std::string("version=3\nreduce_motion=") + char('0' + motion) +
                              "\ntitle_store_suffix=" + char('0' + suffix) + "\ncloud_saves_auto=" + char('0' + cloud) + "\n") {
          Preferences preferences;
          preferences.reduce_motion = motion == 1;
          preferences.title_store_suffix = suffix == 1;
          preferences.cloud_saves_auto = cloud == 1;
          return PreferencesLoadResult{preferences, PreferencesLoadStatus::kLoaded, std::string()};
        }
    for (const int suffix : {0, 1})
      for (const int cloud : {0, 1})
        for (const int password : {0, 1})
          if (serialized == std::string("version=4\nreduce_motion=") + char('0' + motion) +
                                "\ntitle_store_suffix=" + char('0' + suffix) + "\ncloud_saves_auto=" + char('0' + cloud) +
                                "\nsteam_sign_in_password=" + char('0' + password) + "\n") {
            Preferences preferences;
            preferences.reduce_motion = motion == 1;
            preferences.title_store_suffix = suffix == 1;
            preferences.cloud_saves_auto = cloud == 1;
            preferences.steam_sign_in_password = password == 1;
            return PreferencesLoadResult{preferences, PreferencesLoadStatus::kLoaded, std::string()};
          }
    for (const int suffix : {0, 1})
      for (const int cloud : {0, 1})
        for (const int password : {0, 1})
          for (const int slow : {0, 1})
            if (serialized == std::string("version=5\nreduce_motion=") + char('0' + motion) +
                                  "\ntitle_store_suffix=" + char('0' + suffix) + "\ncloud_saves_auto=" + char('0' + cloud) +
                                  "\nsteam_sign_in_password=" + char('0' + password) +
                                  "\ndownloads_slow_network=" + char('0' + slow) + "\n") {
              Preferences preferences;
              preferences.reduce_motion = motion == 1;
              preferences.title_store_suffix = suffix == 1;
              preferences.cloud_saves_auto = cloud == 1;
              preferences.steam_sign_in_password = password == 1;
              preferences.downloads_slow_network = slow == 1;
              return PreferencesLoadResult{preferences, PreferencesLoadStatus::kLoaded, std::string()};
            }
    for (const int suffix : {0, 1})
      for (const int cloud : {0, 1})
        for (const int password : {0, 1})
          for (const int slow : {0, 1})
            for (const int stay : {0, 1})
              if (serialized == std::string("version=6\nreduce_motion=") + char('0' + motion) +
                                    "\ntitle_store_suffix=" + char('0' + suffix) + "\ncloud_saves_auto=" + char('0' + cloud) +
                                    "\nsteam_sign_in_password=" + char('0' + password) +
                                    "\ndownloads_slow_network=" + char('0' + slow) +
                                    "\nsteam_stay_signed_in=" + char('0' + stay) + "\n") {
                Preferences preferences;
                preferences.reduce_motion = motion == 1;
                preferences.title_store_suffix = suffix == 1;
                preferences.cloud_saves_auto = cloud == 1;
                preferences.steam_sign_in_password = password == 1;
                preferences.downloads_slow_network = slow == 1;
                preferences.steam_stay_signed_in = stay == 1;
                preferences.beta_updates = beta_updates;
                return PreferencesLoadResult{preferences, PreferencesLoadStatus::kLoaded, std::string()};
              }
  }
  return PreferencesLoadResult{
      Preferences{}, PreferencesLoadStatus::kDefaultCorrupt,
      "Preferences file is corrupt; defaults restored."};
}

Result<void> PreferencesStore::Save(const Preferences& preferences) const {
  const auto ready=storage_.EnsureRootDirectory();
  if(!ready.ok()) return ready;
  // Never write a host that cannot be read back, including embedded newlines.
  const std::string server = ValidDownloadServer(preferences.download_server) ? preferences.download_server : std::string();
  const std::string text = std::string("version=8\nreduce_motion=") + (preferences.reduce_motion ? "1" : "0") +
                           "\ntitle_store_suffix=" + (preferences.title_store_suffix ? "1" : "0") +
                           "\ncloud_saves_auto=" + (preferences.cloud_saves_auto ? "1" : "0") +
                           "\nsteam_sign_in_password=" + (preferences.steam_sign_in_password ? "1" : "0") +
                           "\ndownloads_slow_network=" + (preferences.downloads_slow_network ? "1" : "0") +
                           "\nsteam_stay_signed_in=" + (preferences.steam_stay_signed_in ? "1" : "0") +
                           "\nbeta_updates=" + (preferences.beta_updates ? "1" : "0") +
                           "\ndownload_server=" + server + "\n";
  return storage_.WriteAtomic(kPreferencesPath, Bytes(text.begin(), text.end()));
}

}  // namespace playden
