// Playden - macOS SDL/OpenGL host for the direct homebrew UI candidate.
// Copyright (C) 2026 Leo Lobato
// SPDX-License-Identifier: GPL-3.0-or-later

#include "homebrew/render.hpp"
#include "homebrew/simulation.hpp"

#include "core/input.hpp"
#include "gfx/gl.hpp"
#include "gfx/gl_program.hpp"

#include <SDL.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

struct Options {
    std::string scenario;
    std::filesystem::path profile = PLAYDEN_HOMEBREW_DEFAULT_PROFILE;
    std::filesystem::path replay;
    std::filesystem::path capture;
    std::filesystem::path snapshot;
    int width = 1920;
    int height = 1080;
    bool headless = false;
};

void usage(const char* executable) {
    std::fprintf(stderr,
                 "usage: %s [--scenario ID] [--profile PATH] [--replay PATH]\n"
                 "          [--capture PATH.png] [--snapshot PATH|-] [--headless]\n"
                 "          [--width PX] [--height PX]\n",
                 executable);
}

bool positive_integer(std::string_view text, int* value) {
    int parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed <= 0 ||
        parsed > 16384)
        return false;
    *value = parsed;
    return true;
}

std::optional<Options> parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option(argv[index]);
        if (option == "--headless") {
            options.headless = true;
            continue;
        }
        if (option == "--help") {
            usage(argv[0]);
            std::exit(0);
        }
        if (index + 1 >= argc) {
            usage(argv[0]);
            return std::nullopt;
        }
        const char* argument = argv[++index];
        if (option == "--scenario")
            options.scenario = argument;
        else if (option == "--profile")
            options.profile = argument;
        else if (option == "--replay")
            options.replay = argument;
        else if (option == "--capture")
            options.capture = argument;
        else if (option == "--snapshot")
            options.snapshot = argument;
        else if (option == "--width") {
            if (!positive_integer(argument, &options.width)) return std::nullopt;
        } else if (option == "--height") {
            if (!positive_integer(argument, &options.height)) return std::nullopt;
        } else {
            usage(argv[0]);
            return std::nullopt;
        }
    }
    if (!options.capture.empty()) options.headless = true;
    return options;
}

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool apply_replay(playden::homebrew::Simulation& simulation, const std::filesystem::path& path) {
    if (path.empty()) return true;
    std::ifstream input(path);
    if (!input) {
        std::fprintf(stderr, "could not read replay: %s\n", path.c_str());
        return false;
    }
    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(std::move(line));
        if (line.empty() || line[0] == '#') continue;
        const auto separator = line.find_first_of(" \t");
        const std::string command = line.substr(0, separator);
        const std::string argument =
            separator == std::string::npos ? std::string{} : trim(line.substr(separator + 1));
        bool accepted = true;
        if (command == "scenario")
            accepted = !argument.empty() && simulation.scenario(argument);
        else if (command == "action")
            accepted = !argument.empty() && simulation.action(argument);
        else if (command == "text")
            simulation.text(argument);
        else if (command == "tick") {
            int ticks = 1;
            accepted = argument.empty() || positive_integer(argument, &ticks);
            if (accepted) simulation.tick(ticks);
        } else {
            accepted = false;
        }
        if (!accepted) {
            std::fprintf(stderr, "%s:%d: invalid replay command: %s%s%s\n", path.c_str(),
                         line_number, command.c_str(), argument.empty() ? "" : " ",
                         argument.c_str());
            if (!simulation.last_error().empty())
                std::fprintf(stderr, "%s\n", simulation.last_error().c_str());
            return false;
        }
    }
    return true;
}

bool write_snapshot(const playden::homebrew::Simulation& simulation,
                    const std::filesystem::path& path) {
    if (path.empty()) return true;
    const std::string snapshot = simulation.snapshot() + "\n";
    if (path == "-") return std::fwrite(snapshot.data(), 1, snapshot.size(), stdout) == snapshot.size();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << snapshot;
    return output.good();
}

std::uint32_t navigation_bit(SDL_Keycode key) {
    switch (key) {
    case SDLK_UP: return hui::pad_bits::kUp;
    case SDLK_DOWN: return hui::pad_bits::kDown;
    case SDLK_LEFT: return hui::pad_bits::kLeft;
    case SDLK_RIGHT: return hui::pad_bits::kRight;
    default: return 0;
    }
}

const char* keyboard_action(const SDL_KeyboardEvent& event, bool editing) {
    if (event.repeat != 0) return nullptr;
    if (navigation_bit(event.keysym.sym) != 0) return nullptr;
    if (editing) {
        if (event.keysym.sym == SDLK_RETURN) return "cross";
        if (event.keysym.sym == SDLK_ESCAPE) return "circle";
        if (event.keysym.sym == SDLK_BACKSPACE) return "square";
        return nullptr;
    }
    switch (event.keysym.sym) {
    case SDLK_RETURN:
    case SDLK_SPACE: return "cross";
    case SDLK_ESCAPE: return "circle";
    case SDLK_BACKSPACE: return "square";
    case SDLK_PAGEUP: return "l2";
    case SDLK_PAGEDOWN: return "r2";
    case SDLK_TAB: return (event.keysym.mod & KMOD_SHIFT) != 0 ? "l1" : "r1";
    case SDLK_q: return "l1";
    case SDLK_e: return "r1";
    case SDLK_f: return "triangle";
    case SDLK_x: return "square";
    case SDLK_s:
    case SDLK_SLASH: return "search";
    case SDLK_o: return "options";
    default: return nullptr;
    }
}

std::uint8_t axis_byte(Sint16 value) {
    return static_cast<std::uint8_t>(
        (static_cast<std::int32_t>(value) + 32768) * 255 / 65535);
}

hui::PadSample controller_sample(SDL_GameController* controller, std::uint64_t now_us) {
    hui::PadSample sample;
    sample.timestamp_us = now_us;
    sample.connected = controller != nullptr && SDL_GameControllerGetAttached(controller);
    if (!sample.connected) return sample;
    const auto button = [&](SDL_GameControllerButton name, std::uint32_t bit) {
        if (SDL_GameControllerGetButton(controller, name) != 0) sample.buttons |= bit;
    };
    button(SDL_CONTROLLER_BUTTON_DPAD_UP, hui::pad_bits::kUp);
    button(SDL_CONTROLLER_BUTTON_DPAD_DOWN, hui::pad_bits::kDown);
    button(SDL_CONTROLLER_BUTTON_DPAD_LEFT, hui::pad_bits::kLeft);
    button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, hui::pad_bits::kRight);
    button(SDL_CONTROLLER_BUTTON_A, hui::pad_bits::kCross);
    button(SDL_CONTROLLER_BUTTON_B, hui::pad_bits::kCircle);
    button(SDL_CONTROLLER_BUTTON_X, hui::pad_bits::kSquare);
    button(SDL_CONTROLLER_BUTTON_Y, hui::pad_bits::kTriangle);
    button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, hui::pad_bits::kL1);
    button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, hui::pad_bits::kR1);
    button(SDL_CONTROLLER_BUTTON_START, hui::pad_bits::kOptions);
    button(SDL_CONTROLLER_BUTTON_LEFTSTICK, hui::pad_bits::kL3);
    button(SDL_CONTROLLER_BUTTON_RIGHTSTICK, hui::pad_bits::kR3);
#if SDL_VERSION_ATLEAST(2, 0, 14)
    button(SDL_CONTROLLER_BUTTON_TOUCHPAD, hui::pad_bits::kTouchpad);
#endif
    sample.left_x = axis_byte(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX));
    sample.left_y = axis_byte(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY));
    sample.right_x = axis_byte(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX));
    sample.right_y = axis_byte(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY));
    sample.l2 = static_cast<std::uint8_t>(
        std::max<Sint16>(0, SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT)) /
        128);
    sample.r2 = static_cast<std::uint8_t>(
        std::max<Sint16>(0, SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) /
        128);
    return sample;
}

const char* action_name(hui::Action action) {
    switch (action) {
    case hui::Action::confirm: return "cross";
    case hui::Action::back: return "circle";
    case hui::Action::north: return "triangle";
    case hui::Action::west: return "square";
    case hui::Action::page_prev: return "l1";
    case hui::Action::page_next: return "r1";
    case hui::Action::jump_prev: return "l2";
    case hui::Action::jump_next: return "r2";
    case hui::Action::menu: return "options";
    case hui::Action::touch: return "search";
    default: return nullptr;
    }
}

void apply_input(playden::homebrew::Simulation& simulation, const hui::InputFrame& frame) {
    switch (frame.nav) {
    case hui::Direction::up: simulation.action("up"); break;
    case hui::Direction::down: simulation.action("down"); break;
    case hui::Direction::left: simulation.action("left"); break;
    case hui::Direction::right: simulation.action("right"); break;
    case hui::Direction::none: break;
    }
    for (unsigned index = static_cast<unsigned>(hui::Action::confirm);
         index < static_cast<unsigned>(hui::Action::count); ++index) {
        const auto action = static_cast<hui::Action>(index);
        if (frame.is_pressed(action)) {
            if (const char* name = action_name(action)) simulation.action(name);
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    const auto options = parse_options(argc, argv);
    if (!options) return 2;

    playden::homebrew::Simulation simulation(PLAYDEN_HOMEBREW_FIXTURES_DIR, options->profile);
    if (!options->scenario.empty() && !simulation.scenario(options->scenario)) {
        std::fprintf(stderr, "%s\n", simulation.last_error().c_str());
        return 2;
    }
    if (!apply_replay(simulation, options->replay)) return 2;

    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    const Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                         (options->headless ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
    SDL_Window* window = SDL_CreateWindow("Playden — ps5-homebrew-ui", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, options->width, options->height,
                                          flags);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!window || !context) {
        std::fprintf(stderr, "OpenGL 4.1 context creation failed: %s\n", SDL_GetError());
        if (context) SDL_GL_DeleteContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_SetSwapInterval(options->headless ? 0 : 1);
    hui::gfx::set_glsl_prefix("#version 410 core\n");

    auto render = std::make_unique<playden::homebrew::Render>(PLAYDEN_HOMEBREW_ASSETS_DIR);
    SDL_StartTextInput();
    SDL_GameController* controller = nullptr;
    for (int index = 0; index < SDL_NumJoysticks() && !controller; ++index)
        if (SDL_IsGameController(index)) controller = SDL_GameControllerOpen(index);
    hui::InputTracker controller_input;
    hui::InputTracker keyboard_input;
    std::uint32_t keyboard_buttons = 0;
    int width = options->width;
    int height = options->height;
    bool running = true;
    bool rendered_once = false;

    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            else if (event.type == SDL_WINDOWEVENT &&
                     event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED && !options->headless) {
                width = event.window.data1;
                height = event.window.data2;
            } else if (event.type == SDL_KEYDOWN) {
                keyboard_buttons |= navigation_bit(event.key.keysym.sym);
                if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_t &&
                    !simulation.state().editor.active)
                    simulation.tick();
                else if (const char* action =
                             keyboard_action(event.key, simulation.state().editor.active))
                    simulation.action(action);
            } else if (event.type == SDL_KEYUP) {
                keyboard_buttons &= ~navigation_bit(event.key.keysym.sym);
            } else if (event.type == SDL_CONTROLLERDEVICEADDED && !controller) {
                controller = SDL_GameControllerOpen(event.cdevice.which);
            } else if (event.type == SDL_CONTROLLERDEVICEREMOVED && controller &&
                       SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)) ==
                           event.cdevice.which) {
                SDL_GameControllerClose(controller);
                controller = nullptr;
            } else if (event.type == SDL_TEXTINPUT) {
                simulation.text(event.text.text);
            }
        }

        const auto now_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        const hui::PadSample pad = controller_sample(controller, now_us);
        apply_input(simulation, controller_input.update(std::span(&pad, 1), now_us));
        hui::PadSample keys;
        keys.connected = true;
        keys.buttons = keyboard_buttons;
        keys.timestamp_us = now_us;
        apply_input(simulation, keyboard_input.update(std::span(&keys, 1), now_us));
        render->draw(simulation, width, height);
        rendered_once = true;

        if (!options->capture.empty()) {
            glFinish();
            if (!render->capture_png(options->capture, width, height)) {
                std::fprintf(stderr, "PNG capture failed: %s\n", options->capture.c_str());
                running = false;
                rendered_once = false;
                break;
            }
        }
        const GLenum gl_error = glGetError();
        if (gl_error != GL_NO_ERROR) {
            std::fprintf(stderr, "OpenGL error after frame: 0x%x\n", gl_error);
            running = false;
            rendered_once = false;
            break;
        }
        SDL_GL_SwapWindow(window);
        if (options->headless) running = false;
    }

    const bool snapshot_written = write_snapshot(simulation, options->snapshot);
    if (controller) SDL_GameControllerClose(controller);
    SDL_StopTextInput();
    render.reset();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rendered_once && snapshot_written ? 0 : 1;
}
