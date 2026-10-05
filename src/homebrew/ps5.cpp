// ps5.cpp - Direct Hui comparison lifecycle and controller adapter.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "homebrew/render.hpp"
#include "homebrew/artwork_backend.hpp"
#include "homebrew/simulation.hpp"
#include "homebrew/controller.hpp"
#include "steam/install_volumes.h"
#include "app/diagnostic_log.h"
#include "platform/ps5/title_runtime.h"
#include "platform/ps5/native_startup.h"
#if PLAYDEN_TITLE_CONTRACT_PROBE
#include "platform/ps5/title_contract_probe.h"
#endif
#include "homebrew/input_adapter.hpp"
#include "core/input.hpp"
#include "steam/backend/auth_client.h"
#include "steam/backend/cm_app_details.h"
#include "steam/backend/entitled_library.h"
#include "steam/backend/steam_install_runner.h"
#include "steam/backend/steam_cloud_runner.h"
#include "steam/backend/library_client.h"
#include "steam/local_session_repository.h"
#include "library/cache.h"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sys/stat.h>

extern "C" void playden_release_splash(void);
// The native signer requires every referenced export to resolve, including
// the kit's optional weak splash callback. Use the launcher's held splash.
extern "C" void hui_release_splash(void) { playden_release_splash(); }

namespace {
void dispatch(playden::homebrew::Simulation& simulation, const hui::InputFrame& input) {
    switch (input.nav) {
        case hui::Direction::up: simulation.action("up"); break;
        case hui::Direction::down: simulation.action("down"); break;
        case hui::Direction::left: simulation.action("left"); break;
        case hui::Direction::right: simulation.action("right"); break;
        default: break;
    }
    const std::array<std::pair<hui::Action, const char*>, 10> actions{{
        {hui::Action::confirm, "cross"}, {hui::Action::back, "circle"},
        {hui::Action::page_prev, "l1"}, {hui::Action::page_next, "r1"},
        {hui::Action::jump_prev, "l2"}, {hui::Action::jump_next, "r2"},
        {hui::Action::menu, "options"}, {hui::Action::north, "triangle"},
        {hui::Action::west, "square"}, {hui::Action::touch, "search"}
    }};
    // Dispatch one activation edge per frame so opening a modal cannot also
    // activate its default control through another buffered physical edge.
    for (const auto& [action, name] : actions) {
        if (input.is_pressed(action)) { simulation.action(name); break; }
    }
}
}

// Title-to-title launch, as Itemzflow does: the target receives argv as its arguments.
extern "C" {
int sceLncUtilInitialize(void);
uint32_t sceLncUtilLaunchApp(const char* title_id, const char* argv[], playden::ps5::LaunchParameters* param);
uint32_t sceSystemServiceLaunchApp(const char* title_id, const char* argv[], void* param);
int sceSysmoduleLoadModuleInternal(uint32_t id);
int sceSysmoduleGetModuleHandleInternal(uint32_t id, int32_t* handle);
int sceKernelDlsym(int32_t handle, const char* name, void** address);
int sceUserServiceGetForegroundUser(uint32_t* user_id);
}

namespace {
std::string title_root = "/app0";
std::string log_root = "/app0/logs";
#if PLAYDEN_TITLE_CONTRACT_PROBE
int trace_module_load(uint32_t id) {
    const int rc = sceSysmoduleLoadModuleInternal(id);
    char line[160]; std::snprintf(line, sizeof(line), "module_load id=%08x result=%08x", id, unsigned(rc));
    playden::app::AppendLogLine(log_root, "launch.log", line); return rc;
}
int trace_module_handle(uint32_t id, int32_t* handle) {
    const int rc = sceSysmoduleGetModuleHandleInternal(id, handle);
    char line[160]; std::snprintf(line, sizeof(line), "module_handle id=%08x result=%08x handle=%d", id, unsigned(rc), *handle);
    playden::app::AppendLogLine(log_root, "launch.log", line); return rc;
}
int trace_module_resolve(int32_t handle, const char* name, void** address) {
    int rc = sceKernelDlsym(handle, name, address);
    char line[160]; std::snprintf(line, sizeof(line), "module_resolve name=%s result=%08x address=%p", name, unsigned(rc), *address);
    playden::app::AppendLogLine(log_root, "launch.log", line);
    return rc;
}
// Development probe for the runtime handoff: when /app0/dev/launch-request.txt holds
// "<TITLE_ID> [arg ...]", it is consumed once and that title is launched with the arguments.
// The result is appended to /app0/logs/launch.log. Nothing in the UI triggers this.
void run_launch_request() {
    const char* request_path = "/app0/dev/launch-request.txt";
    FILE* request = std::fopen(request_path, "r");
    if (!request) return;
    char line[512] = {};
    const bool read = std::fgets(line, sizeof(line), request) != nullptr;
    std::fclose(request);
    std::remove(request_path);
    if (!read) return;
    std::vector<std::string> words;
    for (char* word = std::strtok(line, " \t\r\n"); word && words.size() < 9; word = std::strtok(nullptr, " \t\r\n"))
        words.emplace_back(word);
    if (words.empty()) return;
    char line_text[160];
    std::snprintf(line_text, sizeof(line_text), "launch_request title=%s args=%zu", words[0].c_str(), words.size() - 1);
    playden::app::AppendLogLine(log_root, "launch.log", line_text);
    std::snprintf(line_text, sizeof(line_text), "launch_provider initialize=%p foreground=%p launch=%p",
                  reinterpret_cast<void*>(sceLncUtilInitialize), reinterpret_cast<void*>(sceUserServiceGetForegroundUser),
                  reinterpret_cast<void*>(sceLncUtilLaunchApp));
    playden::app::AppendLogLine(log_root, "launch.log", line_text);
    auto api = playden::ps5::ResolveTitleApi(
        {trace_module_load, trace_module_handle, trace_module_resolve}, sceUserServiceGetForegroundUser);
    if (!api.launch && !api.public_launch) {
        api.public_launch = sceSystemServiceLaunchApp;
        api.foreground_user = sceUserServiceGetForegroundUser;
    }
    std::snprintf(line_text, sizeof(line_text), "launch_resolved initialize=%p launch=%p public=%p",
                  reinterpret_cast<void*>(api.initialize), reinterpret_cast<void*>(api.launch), reinterpret_cast<void*>(api.public_launch));
    playden::app::AppendLogLine(log_root, "launch.log", line_text);
    playden::ps5::TitleBackend backend(api);
    const auto result = backend.Launch(words[0], {words.begin() + 1, words.end()});
    std::snprintf(line_text, sizeof(line_text), "launch_result title=%s stage=%s accepted=%d result=0x%08x", words[0].c_str(),
                  result.stage.c_str(), result.ok, result.code);
    playden::app::AppendLogLine(log_root, "launch.log", line_text);
}
#endif
// Development aid for unattended console runs: /app0/dev/install-request.txt holding one Steam
// app ID queues that install once. Nothing in the UI triggers it.
std::uint32_t take_install_request() {
    const std::string request_path = title_root + "/dev/install-request.txt";
    FILE* request = std::fopen(request_path.c_str(), "r");
    if (!request) return 0;
    char line[64] = {};
    const bool read = std::fgets(line, sizeof(line), request) != nullptr;
    std::fclose(request);
    std::remove(request_path.c_str());
    return read ? static_cast<std::uint32_t>(std::strtoul(line, nullptr, 10)) : 0;
}
}  // namespace

int main() {
#if PLAYDEN_TITLE_CONTRACT_PROBE
    if (playden::ps5::RunTitleContractProbe()) hui::sys::park();
#endif
    hui::ps5::Display display;
    const auto native_access = playden::ps5::PrepareNativeStartup(PLAYDEN_PS5_TITLE_ID);
    if (display.width() <= 0 && !display.open(3840, 2160)) {
        hui::sys::log("Playden Hui display failed"); hui::sys::park();
    }
    if (!native_access.launcher_ready) {
        hui::sys::log("Playden launcher paths unavailable: %s", native_access.error.c_str());
        hui::sys::park();
    }
    const auto& paths = native_access.paths;
    title_root = paths.title_root;
    log_root = paths.log_root;
    if (!native_access.runtime_ready)
        hui::sys::log("Playden native runtime unavailable: %s", native_access.error.c_str());
    {
#if PLAYDEN_UI_SIMULATION
        mkdir((paths.title_root + "/playden-hui-comparison").c_str(), 0777);
        playden::homebrew::Simulation simulation(
            paths.title_root + "/fixtures/ui-comparison", paths.title_root + "/playden-hui-comparison/profile.json",
            playden::homebrew::ModelMode::Simulation);
#else
        playden::homebrew::Simulation simulation({}, {}, playden::homebrew::ModelMode::Live);
#endif
        {
            std::vector<playden::runtime::SettingsPreset> presets;
            std::vector<playden::runtime::GameRecipe> recipes;
            std::string reason;
            // The kit's DXVK reads dxvk.conf from the game folder (results/20261005-dxvk-conf-probe).
            playden::runtime::KitCapabilities kit;
            kit.dxvk_config = true;
            simulation.SetKitCapabilities(kit);
            if (playden::runtime::LoadSettingsData(paths.title_root + "/runtime", &presets, &recipes, &reason))
                simulation.SetSettingsData(std::move(presets), std::move(recipes));
            else
                playden::app::AppendLogLine(paths.log_root, "launch.log", "game settings data unavailable: " + reason);
        }
#if !PLAYDEN_UI_SIMULATION
        simulation.SetGameSettingsFile(paths.steam_state_root + "/game-settings.json");
        playden::Storage auth_storage(paths.steam_state_root);
        playden::steam::CmClient auth_cm(paths.trust_store, auth_storage);
        playden::steam::AuthClient auth(paths.trust_store);
        auth.SetCmRenewal([&auth_cm](const playden::StoredSession& stored, const std::atomic<bool>& cancel) {
            return playden::steam::RenewThroughCm(auth_cm, stored, cancel);
        });
        // Sanitized: method, HTTP status and X-EResult number; never tokens or account identity.
        auth.SetFailureReport([](const char* method, long http_status, int eresult) {
            char line[160];
            std::snprintf(line, sizeof(line), "auth_failure method=%s http=%ld eresult=%d", method, http_status, eresult);
            playden::app::AppendLogLine(log_root, "auth.log", line);
        });
        playden::LocalSessionRepository sessions(paths.steam_state_root, [](const std::string& diagnostic) {
            playden::app::AppendLogLine(log_root, "storage.log", diagnostic);
        });
        playden::steam::LibraryClient owned_library(paths.trust_store);
        playden::LocalSessionRepository library_sessions(paths.steam_state_root);
        playden::Storage library_storage(paths.steam_state_root);
        playden::steam::CmClient cm_client(paths.trust_store, library_storage);
        playden::steam::EntitledLibraryBackend library_backend(owned_library, cm_client);
        playden::LibraryCache library_cache(library_storage);
        playden::LibraryService library_service(library_backend,library_sessions,&library_cache);
        playden::PreferencesStore preferences(library_storage);
        playden::OrganizationStore organization(library_storage);
        playden::app::PersistenceService persistence(preferences,organization);
        playden::steam::CmClient details_cm(paths.trust_store, library_storage);
        playden::steam::CmAppDetailsSource details_source(details_cm);
        playden::LocalSessionRepository details_sessions(paths.steam_state_root);
        playden::steam::AppDetailsService app_details(details_source, details_sessions);
        playden::steam::CmClient install_cm(paths.trust_store, library_storage);
        playden::steam::CdnClient install_cdn(paths.trust_store);
        playden::steam::SteamInstallRunner install_runner(install_cm, install_cdn);
        install_runner.SetLog([](playden::GameId app, const char* step, const char* category, const char* detail) {
            char line[320];
            std::snprintf(line, sizeof(line), "app=%u step=%s result=%s detail=%s", app, step, category, detail);
            playden::app::AppendLogLine(log_root, "install.log", line);
        });
        playden::LocalSessionRepository install_sessions(paths.steam_state_root);
        playden::steam::InstallService installs(install_runner, install_sessions,
            playden::steam::DiscoverInstallVolumes(playden::steam::ConsoleVolumeCandidates()));
        playden::steam::CmClient cloud_cm(paths.trust_store, library_storage);
        playden::steam::HttpClient cloud_http(paths.trust_store);
        playden::steam::SteamCloudRunner cloud_runner(cloud_cm, cloud_http);
        cloud_runner.SetLog([](playden::GameId app, const char* event, const std::string& message) {
            playden::app::AppendLogLine(log_root, "cloud.log", "app=" + std::to_string(app) + " event=" + event + " " + message);
        });
        playden::LocalSessionRepository cloud_sessions(paths.steam_state_root);
        playden::steam::CloudService cloud(cloud_runner, cloud_sessions);
        simulation.SetLogDirectory(paths.log_root);
        playden::homebrew::Controller services(simulation, auth, sessions,&library_service,&persistence,&app_details,&installs,&cloud);
#endif
#if PLAYDEN_UI_SIMULATION
        playden::homebrew::Render renderer(paths.assets_root);
#else
        playden::homebrew::HttpArtworkBackend artwork_backend(paths.trust_store);
        playden::homebrew::ArtworkService artwork(artwork_backend);
        playden::homebrew::Render renderer(paths.assets_root, &artwork);
#endif
        hui::ps5::Pad pad;
        pad.open();
        hui::InputTracker tracker;
        std::array<hui::PadSample, 64> samples{};
        bool presented = false;
        bool was_connected = true;
        std::uint64_t last_render_us = 0;
        for (;;) {
            const auto now = hui::sys::monotonic_us();
            const auto count = pad.read(samples);
            const auto input = tracker.update(std::span<const hui::PadSample>(samples.data(), count), now);
            if (input.connected != was_connected) {
                simulation.SetControllerConnected(input.connected);
                was_connected = input.connected;
            }
            dispatch(simulation, input);
            simulation.controller_sample(playden::homebrew::diagnostic_sample(input));
#if !PLAYDEN_UI_SIMULATION
            services.Tick();
#endif
            if (simulation.state().quit_requested) break;
            const float frame_dt = last_render_us == 0 ? 1.0F / 60.0F :
                static_cast<float>(now - last_render_us) / 1000000.0F;
            last_render_us = now;
            renderer.draw(simulation, display.width(), display.height(), frame_dt);
            if (!display.swap()) break;
            if (!presented) {
                hui::sys::hide_splash_screen();
                presented = true;
#if PLAYDEN_TITLE_CONTRACT_PROBE
                run_launch_request();
#endif
#if !PLAYDEN_UI_SIMULATION
                if (const auto app = take_install_request()) installs.Install(app);
#endif
            }
            // Work advances only through the documented manual fixture clock.
            pad.tick(1.0F / 60.0F);
        }
#if !PLAYDEN_UI_SIMULATION
        services.Shutdown();
        artwork.Shutdown();
#endif
    }
    display.close();
    hui::sys::quit();
}
