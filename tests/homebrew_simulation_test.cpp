// homebrew_simulation_test.cpp - Deterministic Playden UI simulation acceptance tests.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT

#include "homebrew/simulation.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::filesystem::path temp_profile() {
    auto path = std::filesystem::temp_directory_path() / "playden-homebrew-simulation-test.json";
    std::error_code error;
    std::filesystem::remove(path, error);
    return path;
}

void t01_authentication_ignores_late_approval(const std::filesystem::path& fixtures,
                                               const std::filesystem::path& profile) {
    playden::homebrew::Simulation simulation(fixtures, profile);
    simulation.scenario("signed-out");
    check(simulation.state().navigation.route == playden::homebrew::Route::Startup,
          "T01 signed-out scenario starts at first run");
    simulation.action("cross");
    check(simulation.state().account.authentication == playden::homebrew::AuthState::Waiting,
          "T01 Connect Steam opens a waiting challenge");
    simulation.scenario("qr-expired");
    check(simulation.state().account.authentication == playden::homebrew::AuthState::Expired,
          "T01 injected QR expiry is visible");
    simulation.action("cross");
    simulation.scenario("qr-approved");
    simulation.tick(1);
    check(simulation.state().navigation.route == playden::homebrew::Route::Home,
          "T01 approved refreshed challenge reaches Home");
    simulation.scenario("signed-out");
    simulation.action("cross");
    simulation.action("circle");
    simulation.scenario("late-approval");
    simulation.tick(5);
    check(simulation.state().account.authentication == playden::homebrew::AuthState::Cancelled,
          "T01 late approval cannot revive a cancelled challenge");
}

void t02_focus_and_nested_modal_restoration(const std::filesystem::path& fixtures,
                                             const std::filesystem::path& profile) {
    playden::homebrew::Simulation simulation(fixtures, profile);
    simulation.scenario("default");
    simulation.action("r1");
    simulation.action("down");
    simulation.action("down");
    const auto selected = simulation.state().navigation.selected_game_id;
    const auto scroll = simulation.state().navigation.scroll_offsets.at("library-grid");
    simulation.action("cross");
    simulation.action("options");
    check(simulation.state().navigation.modals.size() == 1, "T02 More opens one modal");
    simulation.action("circle");
    check(simulation.state().navigation.focus_key == "details-more", "T02 closing More restores its opener");
    simulation.action("circle");
    check(simulation.state().navigation.selected_game_id == selected,
          "T02 returning to Library restores selected game identity");
    check(simulation.state().navigation.scroll_offsets.at("library-grid") == scroll,
          "T02 returning to Library restores exact viewport");
}

void public_contract_and_snapshot(const std::filesystem::path& fixtures,
                                  const std::filesystem::path& profile) {
    playden::homebrew::Simulation simulation(fixtures, profile);
    simulation.scenario("default");
    check(simulation.state().games.size() == 24, "curated fixture exposes all 24 games");
    check(!simulation.view().rows.empty(), "active surface exposes reusable rows");
    check(simulation.snapshot().find("\"simulation\":true") != std::string::npos,
          "snapshot marks the state as a UI simulation");
}

void activate(playden::homebrew::Simulation& simulation, const std::string& key) {
    check(simulation.action("focus:" + key), "test focus key exists");
    check(simulation.action("cross"), "focused action activates");
}

void t03_search_filters_and_focus_reconciliation(const std::filesystem::path& fixtures,
                                                 const std::filesystem::path& profile) {
    playden::homebrew::Simulation simulation(fixtures, profile);
    simulation.action("route:library");
    simulation.action("search");
    simulation.text("temporary");
    simulation.action("circle");
    check(simulation.state().library.query.empty(), "T03 cancelling search restores the original query");
    simulation.action("search");
    simulation.text("Night Orchard");
    simulation.action("done");
    check(simulation.visible_games().size() == 1 && simulation.visible_games().front()->id == 900010,
          "T03 committed search updates results and count");
    activate(simulation, "game-900010");
    simulation.action("circle");
    simulation.action("options");
    activate(simulation, "hide");
    check(simulation.visible_games().empty() && simulation.state().navigation.focus_key == "clear-search",
          "T03 removing the focused result reconciles focus to Clear search");
    activate(simulation, "clear-search");
    check(!simulation.visible_games().empty(), "T03 Clear search recovers results");
    simulation.action("triangle");
    activate(simulation, "sort-playtime");
    simulation.action("triangle");
    activate(simulation, "filter-controller-full");
    simulation.action("triangle");
    activate(simulation, "filters-reset");
    check(simulation.state().library.filters.empty() && simulation.state().library.sort == "name",
          "T03 reset clears every filter and restores name sort");
}

void t04_organization_persists_without_deleting_games(const std::filesystem::path& fixtures,
                                                       const std::filesystem::path& profile) {
    playden::homebrew::Simulation simulation(fixtures, profile);
    simulation.action("route:library");
    simulation.action("select:900012");
    simulation.action("square");
    check(simulation.state().library.favorites.contains(900012), "T04 favorite toggles by stable ID");
    check(simulation.action("collection:create:Night games"), "T04 collection create action is available");
    check(simulation.action("collection:add:collection-2:900012"), "T04 collection membership changes");
    check(simulation.action("collection:rename:collection-2:After dark"), "T04 collection rename works");
    check(simulation.action("collection:pin:collection-2"), "T04 collection pin works");
    check(simulation.action("rate:900012:playable"), "T04 local compatibility rating works");
    check(simulation.action("note:900012:Synthetic note"), "T04 note edit works");
    playden::homebrew::Simulation organized(fixtures, profile);
    check(organized.state().library.collections.size() == 2 && organized.state().library.collections.back().pinned
              && organized.state().library.collections.back().members.contains(900012),
          "T04 collection name, pin, and membership survive restart");
    check(organized.state().library.ratings.at(900012) == playden::homebrew::Compatibility::Playable
              && organized.state().library.notes.at(900012) == "Synthetic note",
          "T04 rating and note survive restart");
    check(simulation.action("collection:delete:collection-2"), "T04 collection delete works");
    check(simulation.state().games.size() == 24, "T04 deleting a collection never deletes a game");
    playden::homebrew::Simulation restored(fixtures, profile);
    check(restored.state().library.favorites.contains(900012), "T04 favorites survive restart");
}

void t05_install_rechecks_storage_and_submits_once(const std::filesystem::path& fixtures,
                                                   const std::filesystem::path& profile) {
    playden::homebrew::Simulation simulation(fixtures, profile);
    simulation.action("details:900012");
    activate(simulation, "details-install");
    activate(simulation, "storage-picker");
    activate(simulation, "volume-external");
    simulation.action("inject:storage-disconnect");
    activate(simulation, "install-confirm");
    check(simulation.state().jobs.size() == 3, "T05 disconnect before confirmation blocks install");
    simulation.action("inject:storage-reconnect");
    activate(simulation, "install-confirm");
    check(simulation.state().jobs.size() == 4, "T05 successful confirmation creates one job");
    check(simulation.action("install:900012:external"), "T05 duplicate install submission is accepted idempotently");
    check(simulation.state().jobs.size() == 4, "T05 duplicate confirmation cannot create a second job");
}

void t06_queue_transitions_are_single_active_and_safe(const std::filesystem::path& fixtures,
                                                      const std::filesystem::path& profile) {
    playden::homebrew::Simulation simulation(fixtures, profile);
    simulation.action("route:downloads");
    activate(simulation, "job-job-1");
    activate(simulation, "job-pause-job-1");
    simulation.tick(1);
    activate(simulation, "job-job-1");
    activate(simulation, "job-resume-job-1");
    int active = 0;
    for (const auto& job : simulation.state().jobs)
        if (job.state == playden::homebrew::JobState::Downloading || job.state == playden::homebrew::JobState::Verifying) ++active;
    check(active == 1, "T06 resume preserves the one-active-transfer invariant");
    check(simulation.action("job:move-up:job-3"), "T06 queued jobs can move up");
    check(simulation.action("job:cancel:job-2"), "T06 cancel opens safe confirmation");
    check(simulation.state().navigation.modals.back().focused_action == 0, "T06 destructive cancel defaults to decline");
    simulation.action("circle");
    check(simulation.state().jobs[1].state != playden::homebrew::JobState::Cancelled, "T06 declining cancel leaves the job intact");
    simulation.action("job:fail:job-1:download");
    check(simulation.action("job:retry:job-1"), "T06 failure retry is reachable");
    check(simulation.state().jobs.front().state == playden::homebrew::JobState::Downloading,
          "T06 retry resumes the failed stage");
}

void t07_interruption_and_verification_recovery(const std::filesystem::path& fixtures,
                                                const std::filesystem::path& profile) {
    playden::homebrew::Simulation simulation(fixtures, profile);
    simulation.action("inject:storage-disconnect");
    playden::homebrew::Simulation restored(fixtures, profile);
    check(restored.state().jobs.front().state == playden::homebrew::JobState::Interrupted,
          "T07 interrupted work survives restart without invented completion");
    restored.action("inject:storage-reconnect");
    check(restored.action("job:retry:job-1"), "T07 interrupted download can recover");
    restored.action("inject:verification-failed");
    check(restored.state().jobs.front().retry_stage == playden::homebrew::JobStage::Verify,
          "T07 verification failure preserves its retry stage");
    restored.scenario("runtime-unavailable");
    check(!restored.state().installations.front().runtime_ready,
          "T07 downloaded/installed remains distinct from runtime readiness");
}

void t08_nested_settings_restore_openers(const std::filesystem::path& fixtures,
                                         const std::filesystem::path& profile) {
    playden::homebrew::Simulation simulation(fixtures, profile);
    simulation.action("details:900001");
    activate(simulation, "details-settings");
    activate(simulation, "resolution-picker");
    activate(simulation, "resolution-3840x2160");
    check(simulation.state().navigation.focus_key == "resolution-picker", "T08 picker restores its opener");
    activate(simulation, "edit-arguments");
    simulation.text("--fixture");
    simulation.action("circle");
    check(simulation.state().profiles[1].launch_arguments.empty(), "T08 cancelling text edit discards changes");
    activate(simulation, "edit-arguments");
    simulation.text("--fixture");
    simulation.action("done");
    check(simulation.state().profiles[1].launch_arguments == "--fixture", "T08 committing text edit saves the override");
    activate(simulation, "profile-picker");
    activate(simulation, "profile-custom");
    check(simulation.state().selected_profiles.at(900001) == "custom", "T08 profile selection is scoped to the game");
}

void t09_uninstall_and_reset_preserve_declared_data(const std::filesystem::path& fixtures,
                                                    const std::filesystem::path& profile) {
    playden::homebrew::Simulation simulation(fixtures, profile);
    simulation.action("details:900001");
    simulation.action("options");
    activate(simulation, "uninstall");
    check(simulation.state().navigation.modals.back().focused_action == 0, "T09 uninstall defaults to Cancel");
    simulation.action("circle");
    simulation.action("circle");
    simulation.action("options");
    activate(simulation, "uninstall");
    activate(simulation, "confirm-destructive");
    check(simulation.state().installations.size() == 4, "T09 uninstall removes only the selected installation");
    check(simulation.action("reset"), "T09 reset is available");
    check(simulation.state().installations.size() == 4 && simulation.state().installations.front().saves_present,
          "T09 reset preserves fixture installs and saves");
}

void t10_session_failure_running_stop_and_crash(const std::filesystem::path& fixtures,
                                                const std::filesystem::path& profile) {
    playden::homebrew::Simulation simulation(fixtures, profile);
    simulation.scenario("launch-failure");
    activate(simulation, "session-retry");
    simulation.tick(2);
    check(simulation.state().session == playden::homebrew::SessionState::Running, "T10 launch retry reaches running");
    check(simulation.action("session:quit"), "T10 session quit is controller reachable");
    simulation.action("inject:stop-failure");
    check(simulation.state().session == playden::homebrew::SessionState::Failed, "T10 stop failure is recoverable");
    simulation.action("inject:crash");
    activate(simulation, "session-logs");
    simulation.action("circle");
    check(simulation.state().navigation.route == playden::homebrew::Route::Details
              && simulation.state().navigation.detail_id == 900001,
          "T10 crash logs return to prior game details");
}

void t11_preferences_failure_and_controller_modal_recovery(const std::filesystem::path& fixtures,
                                                            const std::filesystem::path& profile) {
    playden::homebrew::Simulation simulation(fixtures, profile);
    simulation.action("route:settings");
    activate(simulation, "settings-motion");
    playden::homebrew::Simulation restored(fixtures, profile);
    check(restored.state().reduced_motion, "T11 reduced motion persists across restart");
    restored.scenario("save-failure");
    restored.action("route:library");
    restored.action("triangle");
    const auto focus = restored.state().navigation.focus_key;
    restored.action("inject:controller-disconnect");
    restored.action("inject:controller-reconnect");
    check(restored.view().surface == "filters" && restored.state().navigation.focus_key == focus,
          "T11 controller reconnect retains modal and visible focus");
}

void t12_stress_dataset_keeps_logical_focus(const std::filesystem::path& fixtures,
                                            const std::filesystem::path& profile) {
    playden::homebrew::Simulation simulation(fixtures, profile);
    simulation.scenario("stress");
    check(simulation.state().games.size() == 1000, "T12 stress scenario loads all 1,000 logical games");
    simulation.action("select:100500");
    const auto focused = simulation.state().navigation.selected_game_id;
    simulation.tick(3);
    check(simulation.state().navigation.selected_game_id == focused,
          "T12 asynchronous art/job updates never steal stable game focus");
}

} // namespace

int main(int argc, char** argv) {
    check(argc == 2, "fixture directory argument is required");
    const std::filesystem::path fixtures(argv[1]);
    const auto profile = temp_profile();
    auto clean = [&] { std::error_code error; std::filesystem::remove(profile, error); };
    clean();
    t01_authentication_ignores_late_approval(fixtures, profile);
    clean();
    t02_focus_and_nested_modal_restoration(fixtures, profile);
    clean();
    t03_search_filters_and_focus_reconciliation(fixtures, profile);
    clean();
    t04_organization_persists_without_deleting_games(fixtures, profile);
    clean();
    t05_install_rechecks_storage_and_submits_once(fixtures, profile);
    clean();
    t06_queue_transitions_are_single_active_and_safe(fixtures, profile);
    clean();
    t07_interruption_and_verification_recovery(fixtures, profile);
    clean();
    t08_nested_settings_restore_openers(fixtures, profile);
    clean();
    t09_uninstall_and_reset_preserve_declared_data(fixtures, profile);
    clean();
    t10_session_failure_running_stop_and_crash(fixtures, profile);
    clean();
    t11_preferences_failure_and_controller_modal_recovery(fixtures, profile);
    clean();
    t12_stress_dataset_keeps_logical_focus(fixtures, profile);
    public_contract_and_snapshot(fixtures, profile);
    std::error_code error;
    std::filesystem::remove(profile, error);
    std::cout << "homebrew_simulation_test passed\n";
}
