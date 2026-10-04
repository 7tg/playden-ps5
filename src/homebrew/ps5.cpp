// ps5.cpp - Direct Hui comparison lifecycle and controller adapter.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "homebrew/render.hpp"
#include "homebrew/simulation.hpp"
#include "homebrew/controller.hpp"
#include "homebrew/input_adapter.hpp"
#include "core/input.hpp"
#include "steam/backend/auth_client.h"
#include "steam/local_session_repository.h"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"

#include <array>
#include <cstdio>
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
        playden::steam::AuthClient auth("/app0/trust/cacert.pem");
        playden::LocalSessionRepository sessions("/app0/playden-data");
        playden::homebrew::Controller services(simulation, auth, sessions);
#endif
        playden::homebrew::Render renderer("/app0/assets");
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
            if (!presented) { hui::sys::hide_splash_screen(); presented = true; }
            // Work advances only through the documented manual fixture clock.
            pad.tick(1.0F / 60.0F);
        }
#if !PLAYDEN_UI_SIMULATION
        services.Shutdown();
#endif
    }
    display.close();
    hui::sys::quit();
}
