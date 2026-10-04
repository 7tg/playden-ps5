// Host event regression tests exercise the actual SDL adapter without a GL context.
// Copyright (c) 2026 Playden contributors
// SPDX-License-Identifier: MIT
#define main playden_host_program
#include "../src/homebrew/host.cpp"
#undef main
#include <cstdlib>
#include <cstring>

static void require(bool okay, const char* message) {
    if (!okay) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main(int argc, char** argv) {
    require(argc == 2, "fixture directory argument");
    const auto profile = std::filesystem::temp_directory_path() / "playden-hui-host-input-test.json";
    std::error_code error;
    std::filesystem::remove(profile, error);
    playden::homebrew::Simulation simulation(argv[1], profile);
    simulation.action("r1");
    bool suppress = false;
    SDL_Event event{};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_SLASH;
    text_key_event(simulation, event, suppress);
    require(simulation.state().editor.active, "slash opens search");
    event.type = SDL_TEXTINPUT;
    std::strcpy(event.text.text, "/");
    text_key_event(simulation, event, suppress);
    require(simulation.state().editor.value.empty(), "search-opening slash is consumed");
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_f;
    text_key_event(simulation, event, suppress);
    event.type = SDL_TEXTINPUT;
    std::strcpy(event.text.text, "f");
    text_key_event(simulation, event, suppress);
    require(simulation.state().editor.value == "f" && simulation.state().navigation.modals.empty(),
            "printable editing key inserts instead of opening Filters");
    simulation.action("circle");
    hui::InputFrame input;
    input.pressed = hui::action_bit(hui::Action::confirm) | hui::action_bit(hui::Action::menu);
    apply_input(simulation, input);
    require(simulation.state().navigation.route == playden::homebrew::Route::Details &&
            simulation.state().navigation.modals.empty(), "simultaneous Cross/Options applies one edge");
    std::filesystem::remove(profile, error);
    std::puts("PASS: SDL Search gesture, printable edit isolation, simultaneous controller activation");
}
