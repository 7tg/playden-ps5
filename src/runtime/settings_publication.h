#pragma once

#include "runtime/game_settings.h"
#include "runtime/package_installer.h"

namespace playden::runtime {

// Fills the settings-owned fields of a profile publication from a launch plan: the profile
// id and bytes, the settings revision and every input preset the profile references. The
// recipe identity comes from the snapshot, so stale plans cannot adopt a newer recipe.
// The owner compares plan.app_id/launch_entry to its install record and keeps the
// remaining identity fields (install, title, runtime bundle) and its
// own parser validation (profile_references_validated).
inline void FillProfilePublication(const SettingsLaunchPlan& plan, ProfilePublication* publication) {
  publication->recipe_id = plan.recipe_id;
  publication->recipe_version = plan.recipe_version;
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
