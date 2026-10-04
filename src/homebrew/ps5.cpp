// ps5.cpp - Direct Hui comparison lifecycle and controller adapter.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "homebrew/render.hpp"
#include "homebrew/artwork_backend.hpp"
#include "homebrew/simulation.hpp"
#include "homebrew/controller.hpp"
#include "homebrew/input_adapter.hpp"
#include "core/input.hpp"
#include "steam/backend/auth_client.h"
#include "steam/backend/entitled_library.h"
#include "steam/backend/library_client.h"
#include "steam/local_session_repository.h"
#include "library/cache.h"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"

#include <array>
#include <cstdio>
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
struct PlaydenLncAppParam {
    uint32_t size;
    uint32_t user_id;
    uint32_t app_options;
    uint64_t crash_report;
    uint32_t check_flag;  // 2 = skip the system update check
};
uint32_t sceLncUtilLaunchApp(const char* title_id, const char* argv[], PlaydenLncAppParam* param);
int sceUserServiceGetForegroundUser(uint32_t* user_id);
}

namespace {
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
    std::vector<const char*> argv;
    for (std::size_t index = 1; index < words.size(); ++index) argv.push_back(words[index].c_str());
    argv.push_back(nullptr);
    uint32_t user = 0;
    const int user_result = sceUserServiceGetForegroundUser(&user);
    PlaydenLncAppParam param{sizeof(PlaydenLncAppParam), user, 0, 0, 2};
    mkdir("/app0/logs", 0777);
    if (FILE* log = std::fopen("/app0/logs/launch.log", "a")) {
        std::fprintf(log, "launch_request title=%s args=%zu user_result=%d\n", words[0].c_str(), words.size() - 1, user_result);
        std::fclose(log);
    }
    const uint32_t result = sceLncUtilLaunchApp(words[0].c_str(), argv.data(), &param);
    if (FILE* log = std::fopen("/app0/logs/launch.log", "a")) {
        std::fprintf(log, "launch_result title=%s result=0x%08x\n", words[0].c_str(), result);
        std::fclose(log);
    }
}
}  // namespace

int main() {
    hui::ps5::Display display;
    if (!display.open(3840, 2160)) { hui::sys::log("Playden Hui display failed"); hui::sys::park(); }
    {
#if PLAYDEN_UI_SIMULATION
        mkdir("/app0/playden-hui-comparison", 0777);
        playden::homebrew::Simulation simulation(
            "/app0/fixtures/ui-comparison", "/app0/playden-hui-comparison/profile.json",
            playden::homebrew::ModelMode::Simulation);
#else
        playden::homebrew::Simulation simulation({}, {}, playden::homebrew::ModelMode::Live);
        playden::Storage auth_storage("/app0/playden-data");
        playden::steam::CmClient auth_cm("/app0/trust/cacert.pem", auth_storage);
        playden::steam::AuthClient auth("/app0/trust/cacert.pem");
        auth.SetCmRenewal([&auth_cm](const playden::StoredSession& stored, const std::atomic<bool>& cancel) {
            return playden::steam::RenewThroughCm(auth_cm, stored, cancel);
        });
        // Sanitized: method, HTTP status and X-EResult number; never tokens or account identity.
        auth.SetFailureReport([](const char* method, long http_status, int eresult) {
            mkdir("/app0/logs", 0777);
            if (FILE* log = std::fopen("/app0/logs/auth.log", "a")) {
                std::fprintf(log, "auth_failure method=%s http=%ld eresult=%d\n", method, http_status, eresult);
                std::fclose(log);
            }
        });
        playden::LocalSessionRepository sessions("/app0/playden-data");
        playden::steam::LibraryClient owned_library("/app0/trust/cacert.pem");
        playden::LocalSessionRepository library_sessions("/app0/playden-data");
        playden::Storage library_storage("/app0/playden-data");
        playden::steam::CmClient cm_client("/app0/trust/cacert.pem", library_storage);
        playden::steam::EntitledLibraryBackend library_backend(owned_library, cm_client);
        playden::LibraryCache library_cache(library_storage);
        playden::LibraryService library_service(library_backend,library_sessions,&library_cache);
        playden::PreferencesStore preferences(library_storage);
        playden::OrganizationStore organization(library_storage);
        playden::app::PersistenceService persistence(preferences,organization);
        playden::homebrew::Controller services(simulation, auth, sessions,&library_service,&persistence);
#endif
#if PLAYDEN_UI_SIMULATION
        playden::homebrew::Render renderer("/app0/assets");
#else
        playden::homebrew::HttpArtworkBackend artwork_backend("/app0/trust/cacert.pem");
        playden::homebrew::ArtworkService artwork(artwork_backend);
        playden::homebrew::Render renderer("/app0/assets", &artwork);
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
                run_launch_request();
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
