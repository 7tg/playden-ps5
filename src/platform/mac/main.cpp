#include "app/application.h"
#include "diagnostics/frame_stats.h"

#include <rmlfocus/input.h>
#include <rmlfocus/platform_sdl.h>

#include <RmlUi/Core.h>
#include <RmlUi_Include_GL3.h>
#include <RmlUi_Platform_SDL.h>
#include <RmlUi_Renderer_GL3.h>
#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <sstream>
#include <vector>

#include <mach/mach.h>

namespace {

std::uint64_t MonotonicNanoseconds()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::uint64_t ResidentBytes()
{
	mach_task_basic_info_data_t info{};
	mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
	if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
		reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0;
	return info.resident_size;
}

bool LoadFonts(const std::string& assets)
{
	bool loaded = true;
	for (const char* name : {"Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-SemiBold.ttf", "Inter-Bold.ttf"})
		loaded = Rml::LoadFontFace(assets + "/fonts/" + name) && loaded;
	return loaded;
}

bool WritePpm(const std::string& path, int width, int height)
{
	std::vector<unsigned char> rgba(std::size_t(width) * height * 4);
	glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
	FILE* file = std::fopen(path.c_str(), "wb");
	if (!file) return false;
	std::fprintf(file, "P6\n%d %d\n255\n", width, height);
	for (int y = height - 1; y >= 0; --y) {
		const unsigned char* row = rgba.data() + std::size_t(y) * width * 4;
		for (int x = 0; x < width; ++x) std::fwrite(row + x * 4, 1, 3, file);
	}
	return std::fclose(file) == 0;
}

bool ParseCommand(const std::string& name, rf::Command& command)
{
	if (name == "Up") command = rf::Command::Up;
	else if (name == "Down") command = rf::Command::Down;
	else if (name == "Left") command = rf::Command::Left;
	else if (name == "Right") command = rf::Command::Right;
	else if (name == "Cross") command = rf::Command::Select;
	else if (name == "Circle") command = rf::Command::Back;
	else if (name == "Triangle") command = rf::Command::Secondary;
	else if (name == "L1") command = rf::Command::PreviousTab;
	else if (name == "R1") command = rf::Command::NextTab;
	else return false;
	return true;
}

} // namespace

int main(int argc, char** argv)
{
	double run_for_seconds = 0.0;
	std::string shot;
	std::vector<rf::Command> scripted_commands;
	for (int index = 1; index < argc; index += 2) {
		if (index + 1 >= argc) {
			std::fprintf(stderr, "usage: %s [--run-for-seconds N] [--shot image.ppm] [--keys R1,Cross]\n", argv[0]);
			return 2;
		}
		const std::string option = argv[index];
		if (option == "--run-for-seconds") run_for_seconds = std::atof(argv[index + 1]);
		else if (option == "--shot") shot = argv[index + 1];
		else if (option == "--keys") {
			std::stringstream names(argv[index + 1]);
			for (std::string name; std::getline(names, name, ',');) {
				rf::Command command{};
				if (!ParseCommand(name, command)) {
					std::fprintf(stderr, "unknown scripted key: %s\n", name.c_str());
					return 2;
				}
				scripted_commands.push_back(command);
			}
		}
		else {
			std::fprintf(stderr, "usage: %s [--run-for-seconds N] [--shot image.ppm] [--keys R1,Cross]\n", argv[0]);
			return 2;
		}
	}
	if (!shot.empty() && run_for_seconds <= 0.0) run_for_seconds = 1.0;
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
		std::fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_Window* window = SDL_CreateWindow("Playden", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1920, 1080,
		SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
	SDL_GLContext gl = window ? SDL_GL_CreateContext(window) : nullptr;
	Rml::String gl_message;
	if (!window || !gl || !RmlGL3::Initialize(&gl_message)) {
		std::fprintf(stderr, "OpenGL init failed: %s %s\n", SDL_GetError(), gl_message.c_str());
		if (gl) SDL_GL_DeleteContext(gl);
		if (window) SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	SDL_GL_SetSwapInterval(1);

	int width = 0, height = 0;
	SDL_GL_GetDrawableSize(window, &width, &height);
	auto renderer = std::make_unique<RenderInterface_GL3>();
	renderer->SetViewport(width, height);
	SystemInterface_SDL system(window);
	Rml::SetSystemInterface(&system);
	Rml::SetRenderInterface(renderer.get());
	const bool rml_initialized = Rml::Initialise();
	if (!rml_initialized || !LoadFonts(PLAYDEN_ASSETS_DIR)) {
		std::fprintf(stderr, "RmlUi or font initialization failed\n");
		if (rml_initialized) Rml::Shutdown();
		renderer.reset();
		RmlGL3::Shutdown();
		SDL_GL_DeleteContext(gl);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	Rml::Context* context = Rml::CreateContext("playden", {width, height});
	if (!context) {
		std::fprintf(stderr, "Could not create the Playden RmlUi context\n");
		Rml::Shutdown();
		renderer.reset();
		RmlGL3::Shutdown();
		SDL_GL_DeleteContext(gl);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}
	context->SetDensityIndependentPixelRatio(height / 1080.f);

	char* preference_path = SDL_GetPrefPath("Playden", "Launcher");
	const std::string storage = preference_path ? preference_path : PLAYDEN_STORAGE_DIR;
	SDL_free(preference_path);
	auto app = std::make_unique<playden::Application>(context, PLAYDEN_ASSETS_DIR, storage);
	if (!app->Initialize()) {
		std::fprintf(stderr, "Playden application initialization failed\n");
		app->Shutdown();
		context->Update();
		app.reset();
		Rml::RemoveContext("playden");
		Rml::Shutdown();
		renderer.reset();
		RmlGL3::Shutdown();
		SDL_GL_DeleteContext(gl);
		SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	playden::diagnostics::FrameStats stats;
	SDL_DisplayMode mode{};
	const int display = SDL_GetWindowDisplayIndex(window);
	const bool mode_available = display >= 0 && SDL_GetCurrentDisplayMode(display, &mode) == 0 && mode.refresh_rate > 0;
	stats.SetDisplayConfig({
		playden::diagnostics::Dimensions::Measured(width, height),
		playden::diagnostics::Dimensions::Measured(width, height),
		mode_available ? playden::diagnostics::RefreshConfig::Measured(mode.refresh_rate)
			: playden::diagnostics::RefreshConfig{},
	});
	stats.SetRevisionMetadata({PLAYDEN_REVISION, PLAYDEN_RMLFOCUS_REVISION, "macOS-host"});

	rf::SdlPlatform platform;
	rf::Input input;
	double previous = platform.Time();
	const double started = previous;
	bool running = true;
	bool shot_written = false;
	std::size_t scripted_index = 0;
	while (running && !app->ExitRequested()) {
		const std::uint64_t frame_time = MonotonicNanoseconds();
		stats.RecordFrameStart(frame_time);
		SDL_Event event{};
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) running = false;
			platform.Event(event);
			if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
				SDL_GL_GetDrawableSize(window, &width, &height);
				renderer->SetViewport(width, height);
				context->SetDimensions({width, height});
				context->SetDensityIndependentPixelRatio(height / 1080.f);
				stats.SetDisplayConfig({
					playden::diagnostics::Dimensions::Measured(width, height),
					playden::diagnostics::Dimensions::Measured(width, height),
					mode_available ? playden::diagnostics::RefreshConfig::Measured(mode.refresh_rate)
						: playden::diagnostics::RefreshConfig{},
				});
			}
		}
		if (platform.TakeInputReset()) input.Reset();
		const double now = platform.Time();
		if (run_for_seconds > 0.0 && now - started >= run_for_seconds) running = false;
		if (scripted_index < scripted_commands.size() && now - started >= 0.5 * (scripted_index + 1)) {
			const rf::Command command = scripted_commands[scripted_index++];
				app->Handle({command, true, false});
				app->Handle({command, false, false});
		}
		for (const rf::InputEvent& event : input.Update(platform.PollInput(), now)) app->Handle(event);
		app->Tick(static_cast<float>(std::min(now - previous, 0.1)));
		previous = now;
		context->Update();
		renderer->Clear();
		renderer->BeginFrame();
		context->Render();
		renderer->EndFrame();
		const double shot_at = std::max(0.5, run_for_seconds - 0.25);
		if (!shot.empty() && !shot_written && now - started >= shot_at) {
			shot_written = WritePpm(shot, width, height);
			if (!shot_written) std::fprintf(stderr, "Could not write screenshot: %s\n", shot.c_str());
		}
		SDL_GL_SwapWindow(window);

		const std::uint64_t resident_bytes = ResidentBytes();
		if (resident_bytes > 0) stats.RecordMemory(resident_bytes);
		const std::uint64_t after_frame = MonotonicNanoseconds();
		if (stats.RecordDue(after_frame))
			std::fprintf(stderr, "%s\n", playden::diagnostics::FrameStats::FormatRecord(stats.TakeRecord(after_frame)).c_str());
	}

	platform.Close();
	app->Shutdown();
	context->Update(); // Release unloaded documents while Shell's event listener is still alive.
	app.reset();
	Rml::RemoveContext("playden");
	Rml::Shutdown();
	renderer.reset();
	RmlGL3::Shutdown();
	SDL_GL_DeleteContext(gl);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return !shot.empty() && !shot_written ? 1 : 0;
}
