#pragma once

#include "platform/storage.h"

#include <string>

namespace playden {

struct Preferences {
  bool reduce_motion = false;
};

enum class PreferencesLoadStatus {
  kLoaded,
  kDefaultMissing,
  kDefaultCorrupt,
  kDefaultReadError,
};

struct PreferencesLoadResult {
  Preferences preferences;
  PreferencesLoadStatus status = PreferencesLoadStatus::kLoaded;
  std::string diagnostic;
};

class PreferencesStore {
 public:
  explicit PreferencesStore(Storage& storage);

  PreferencesLoadResult Load() const;
  Result<void> Save(const Preferences& preferences) const;

 private:
  Storage& storage_;
};

}  // namespace playden
