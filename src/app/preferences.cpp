#include "app/preferences.h"

#include <string>

namespace playden {
namespace {

constexpr char kPreferencesPath[] = "preferences.conf";
constexpr char kReduceMotionEnabled[] = "version=1\nreduce_motion=1\n";
constexpr char kReduceMotionDisabled[] = "version=1\nreduce_motion=0\n";

std::string ToString(const Bytes& bytes) {
  return std::string(bytes.begin(), bytes.end());
}

Bytes ToBytes(const char* text) {
  const std::string value(text);
  return Bytes(value.begin(), value.end());
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

  const std::string serialized = ToString(read.value());
  if (serialized == kReduceMotionEnabled) {
    Preferences preferences;
    preferences.reduce_motion = true;
    return PreferencesLoadResult{preferences, PreferencesLoadStatus::kLoaded,
                                 std::string()};
  }
  if (serialized == kReduceMotionDisabled) {
    return PreferencesLoadResult{Preferences{}, PreferencesLoadStatus::kLoaded,
                                 std::string()};
  }
  return PreferencesLoadResult{
      Preferences{}, PreferencesLoadStatus::kDefaultCorrupt,
      "Preferences file is corrupt; defaults restored."};
}

Result<void> PreferencesStore::Save(const Preferences& preferences) const {
  const auto ready=storage_.EnsureRootDirectory();
  if(!ready.ok()) return ready;
  return storage_.WriteAtomic(
      kPreferencesPath,
      ToBytes(preferences.reduce_motion ? kReduceMotionEnabled
                                        : kReduceMotionDisabled));
}

}  // namespace playden
