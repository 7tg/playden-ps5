#pragma once

#include "runtime/game_settings.h"
#include "runtime/package_installer.h"

namespace playden::runtime {

// Fills the settings-owned fields of a profile publication from a launch plan: the profile
// id and bytes, the settings revision and every input preset the profile references. The
// owner adapter keeps the identity fields (install, title, runtime bundle, recipe) and its
// own parser validation (profile_references_validated).
inline void FillProfilePublication(const SettingsLaunchPlan& plan, ProfilePublication* publication) {
  publication->profile_id = plan.profile_id;
  publication->profile_bytes = plan.profile_bytes;
  publication->settings_revision = plan.settings_revision;
  publication->required_input_presets.clear();
  publication->input_presets.clear();
  for (const auto& [filename, bytes] : plan.input_presets) {
    publication->required_input_presets.push_back(filename);  // "<name>.input", as the installer checks
    publication->input_presets.push_back({filename, bytes});
  }
}

}  // namespace playden::runtime
