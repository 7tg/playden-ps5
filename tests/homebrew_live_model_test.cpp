// homebrew_live_model_test.cpp - Live homebrew model boundary tests.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT

#include "homebrew/simulation.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

using playden::homebrew::AccountUpdate;
using playden::homebrew::AuthState;
using playden::homebrew::ModelMode;
using playden::homebrew::Route;
using playden::homebrew::ServiceAction;
using playden::homebrew::Simulation;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void activate(Simulation& model, const std::string& key) {
    check(model.action("focus:" + key), "requested live row must exist");
    check(model.action("cross"), "requested live row must activate");
}

bool has_action(const std::vector<ServiceAction>& actions, ServiceAction expected) {
    return std::find(actions.begin(), actions.end(), expected) != actions.end();
}

Simulation make_live(const std::filesystem::path& base) {
    return Simulation(base / "fixtures-that-do-not-exist", base / "missing" / "profile.json", ModelMode::Live);
}

void live_starts_clean_and_never_reads_or_writes_fixtures(const std::filesystem::path& base) {
    auto model = make_live(base);
    const auto& state = model.state();
    check(!state.simulation_mode && state.account.authentication == AuthState::SignedOut,
          "live model starts signed out and identifies itself as live");
    check(state.games.empty() && state.jobs.empty() && state.installations.empty() && state.volumes.empty(),
          "live model does not inherit fixture library or operation state");
    check(state.runtime == playden::homebrew::RuntimeState::Unavailable && state.clock_ticks == 0,
          "live model starts without synthetic runtime readiness or fixture time");
    check(!model.view().show_simulation_label && model.snapshot().find("\"simulation\":false") != std::string::npos,
          "live presentation and diagnostics do not claim simulation mode");
    check(model.last_error().empty(), "missing fixture and profile paths are not consulted in live mode");

    const auto clock = state.clock_ticks;
    check(!model.scenario("default"), "scenario API is unavailable in live mode");
    check(!model.action("scenario:qr-approved") && !model.action("inject:qr-approved")
              && !model.action("route:library") && !model.action("install:1:external")
              && !model.action("runtime-retry"),
          "synthetic scenario, injection, route, install and runtime actions are unavailable live");
    model.tick(100);
    check(model.state().clock_ticks == clock && model.state().account.authentication == AuthState::SignedOut,
          "manual ticks cannot advance live authentication or fixture time");
    check(!std::filesystem::exists(base / "missing" / "profile.json"),
          "live interactions do not create the comparison profile");

    model.SetControllerConnected(false);
    check(!model.state().controller_connected, "production controller connection state has an explicit setter");
}

void live_dispatches_bounded_service_effects_and_replaces_challenges(const std::filesystem::path& base) {
    auto model = make_live(base);
    activate(model, "startup-connect");
    auto actions = model.TakeServiceActions();
    check(actions.size() == 1 && actions.front() == ServiceAction::BeginQr,
          "connect dispatches one QR begin effect");
    check(model.TakeServiceActions().empty(), "taking service actions drains the queue");

    model.UpdateAccount({AuthState::Waiting, {}, {}, "https://example.invalid/first-secret", {}, false});
    check(model.state().account.challenge_url == "https://example.invalid/first-secret",
          "external challenge is accepted for presentation");
    model.UpdateAccount({AuthState::Waiting, {}, {}, "https://example.invalid/replacement-secret", {}, false});
    check(model.state().account.challenge_url == "https://example.invalid/replacement-secret",
          "new external challenge replaces the prior challenge");
    check(model.snapshot().find("replacement-secret") == std::string::npos,
          "diagnostic snapshot omits the private challenge URL");
    model.UpdateAccount({AuthState::Expired, {}, {}, {}, "Code expired", false});
    check(model.state().account.challenge_url.empty() && model.view().subtitle == "Code expired",
          "external update can clear the challenge and expose a static recovery error");

    activate(model, "qr-refresh");
    model.action("circle");
    actions = model.TakeServiceActions();
    check(actions.size() == 2 && actions[0] == ServiceAction::BeginQr && actions[1] == ServiceAction::CancelQr,
          "retry and cancel dispatch ordered service effects without synthetic approval");

    for (int index = 0; index < 40; ++index) {
        activate(model, "startup-connect");
        model.action("circle");
    }
    actions = model.TakeServiceActions();
    check(actions.size() == 32, "pending service effects are bounded to 32 entries");
}

void failed_signout_preserves_account_until_external_clear(const std::filesystem::path& base) {
    auto model = make_live(base);
    model.UpdateAccount({AuthState::SignedIn, "76561198000000001", "Synthetic Tester", {}, {}, false});
    check(model.state().navigation.route == Route::Home && !model.state().account.cache_available,
          "authentication success does not invent a delivered cache");
    check(model.snapshot().find("76561198000000001") == std::string::npos
              && model.snapshot().find("Synthetic Tester") == std::string::npos,
          "live snapshot omits account identity");

    model.action("r1");
    model.action("r1");
    model.action("r1");
    check(model.state().navigation.route == Route::Settings, "controller navigation reaches account settings");
    activate(model, "settings-signout");
    activate(model, "confirm-destructive");
    auto actions = model.TakeServiceActions();
    check(has_action(actions, ServiceAction::SignOut) && model.state().account.authentication == AuthState::SigningOut,
          "confirmed signout dispatches a barrier and waits for the service result");
    check(model.state().account.id == "76561198000000001",
          "pending signout does not clear account state before durable service completion");

    model.UpdateAccount({AuthState::Failed, "76561198000000001", "Synthetic Tester", {}, "Sign out failed", false});
    check(model.state().account.authentication == AuthState::Failed
              && model.state().account.id == "76561198000000001"
              && model.state().account.error == "Sign out failed"
              && model.view().subtitle == "Sign out failed",
          "failed signout preserves the account and shows the external error without fake success");

    model.UpdateAccount({AuthState::SignedOut, {}, {}, {}, {}, true});
    check(model.state().account.id.empty() && model.state().account.display_name.empty()
              && model.state().games.empty() && model.state().navigation.modals.empty()
              && model.state().navigation.route == Route::Startup,
          "confirmed external account clear removes account-specific state and restores safe startup focus");
}

void account_commands_dispatch_refresh_and_switch_barriers(const std::filesystem::path& base) {
    auto refresh = make_live(base);
    refresh.UpdateAccount({AuthState::SignedIn, "76561198000000002", "Refresh Tester", {}, {}, false});
    refresh.action("r1"); refresh.action("r1"); refresh.action("r1");
    activate(refresh, "settings-account");
    activate(refresh, "account-sync");
    auto actions = refresh.TakeServiceActions();
    check(actions.size() == 1 && actions.front() == ServiceAction::RefreshLibrary
              && refresh.state().account.authentication == AuthState::SignedIn
              && refresh.state().library_loading && refresh.state().library_syncing,
          "account sync dispatches refresh and remains pending external completion");

    auto switching = make_live(base);
    switching.UpdateAccount({AuthState::SignedIn, "76561198000000003", "Switch Tester", {}, {}, false});
    switching.action("r1"); switching.action("r1"); switching.action("r1");
    activate(switching, "settings-account");
    activate(switching, "account-switch");
    activate(switching, "confirm-destructive");
    actions = switching.TakeServiceActions();
    check(actions.size() == 1 && actions.front() == ServiceAction::SwitchAccount
              && switching.state().account.authentication == AuthState::SigningOut
              && switching.state().account.id == "76561198000000003",
          "confirmed account switch dispatches a barrier without prematurely clearing identity");
}

void settings_survive_account_restore_and_cache_action_is_honest(const std::filesystem::path& base) {
    std::filesystem::create_directories(base);
    std::filesystem::permissions(base, std::filesystem::perms::owner_all);
    const auto file = base / "game-settings.json";
    playden::runtime::GameSettingsRecord stored;
    stored.revision = 3;
    stored.overrides["resolution"] = "1280x720";
    std::string reason;
    check(playden::runtime::SaveGameSettingsFile(file.string(), {{220, stored}}, &reason), "seed live settings");
    auto model = make_live(base);
    model.SetGameSettingsFile(file);
    model.UpdateAccount({AuthState::Restoring, {}, {}, {}, {}, true});
    check(model.state().game_settings.count(220) && model.state().game_settings.at(220) == stored,
          "initial account restore retains device game settings");
    model.UpdateAccount({AuthState::SignedOut, {}, {}, {}, {}, true});
    model.SetWallClock(100);
    std::map<std::uint32_t, playden::runtime::GameSettingsRecord> loaded;
    check(playden::runtime::LoadGameSettingsFile(file.string(), &loaded, &reason) && loaded.at(220) == stored,
          "sign-out does not erase settings from disk");
    model.UpdateAccount({AuthState::SignedIn, "account", "Owner", {}, {}, false});
    playden::GameSummary game;
    game.id = 220;
    game.title = "Half-Life 2";
    check(model.UpdateLibrary({{game}, false, {}, false, true, 100}), "settings game library delivered");
    {
        std::ofstream corrupt(file);
        corrupt << "malformed settings";
    }
    model.SetGameSettingsFile(file);
    check(model.state().game_settings.count(220) && model.state().game_settings.at(220) == stored,
          "failed settings reload preserves the current records");
    model.action("r1");
    std::ifstream unchanged(file);
    std::string contents;
    std::getline(unchanged, contents);
    check(contents == "malformed settings", "load failure blocks destructive automatic save");
    check(model.action("details:220"), "open settings game details");
    check(std::none_of(model.view().rows.begin(), model.view().rows.end(),
                       [](const auto& row) { return row.key == "details-settings"; }),
          "live settings wait for the real launch and maintenance bridge");
}

} // namespace

int main() {
    const auto base = std::filesystem::canonical(std::filesystem::temp_directory_path())
        / ("playden-homebrew-live-model-test-" + std::to_string(getpid()));
    std::error_code error;
    std::filesystem::remove_all(base, error);

    settings_survive_account_restore_and_cache_action_is_honest(base);
    live_starts_clean_and_never_reads_or_writes_fixtures(base);
    live_dispatches_bounded_service_effects_and_replaces_challenges(base);
    failed_signout_preserves_account_until_external_clear(base);
    account_commands_dispatch_refresh_and_switch_barriers(base);

    std::filesystem::remove_all(base, error);
    std::cout << "homebrew_live_model_test passed\n";
    return 0;
}
