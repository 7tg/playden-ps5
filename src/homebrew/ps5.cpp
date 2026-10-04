// ps5.cpp - Direct Hui comparison lifecycle and controller adapter.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#include "homebrew/render.hpp"
#include "homebrew/simulation.hpp"
#include "core/input.hpp"
#include "platform/ps5/display_egl.hpp"
#include "platform/ps5/pad.hpp"
#include "platform/ps5/system.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
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
        // Candidate-local state is in the title sandbox. Fixture volume labels
        // never become writable paths or trigger actual installation work.
        mkdir("/app0/playden-hui-comparison", 0777);
        playden::homebrew::Simulation simulation("/app0/fixtures/ui-comparison", "/app0/playden-hui-comparison/profile.json");
        playden::homebrew::Render renderer("/app0/assets");
        hui::ps5::Pad pad;
        pad.open();
        hui::InputTracker tracker;
        std::array<hui::PadSample, 64> samples{};
        bool presented = false;
        bool was_connected = true;
        for (;;) {
            const auto now = hui::sys::monotonic_us();
            const auto count = pad.read(samples);
            const auto input = tracker.update(std::span<const hui::PadSample>(samples.data(), count), now);
            if (input.connected != was_connected) {
                simulation.action(input.connected ? "inject:controller-reconnect" : "inject:controller-disconnect");
                was_connected = input.connected;
            }
            dispatch(simulation, input);
            if (simulation.state().quit_requested) break;
            renderer.draw(simulation, display.width(), display.height());
            if (!display.swap()) break;
            if (!presented) { hui::sys::hide_splash_screen(); presented = true; }
            // Work advances only through the documented manual fixture clock.
            pad.tick(1.0F / 60.0F);
        }
    }
    display.close();
    hui::sys::quit();
}
