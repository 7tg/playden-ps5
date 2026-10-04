#include "app/application.h"
#include "diagnostics/frame_stats.h"
#include "platform.h"
#include "platform/storage.h"
#include "platform/ps5/process_memory.h"
#include "title_shims.h"

#include <rmlfocus/input.h>

#include <RmlUi/Core.h>
#include <RmlUi_Renderer_GL3.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "gl_ps5.h"
#include <ps5_opengl_display_modes.h>

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cerrno>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

extern "C" {
int sceSystemServiceLoadExec(const char* path, char* const argv[]);
int sceUserServiceInitialize(const void* params);
int scePadInit(void);
std::size_t ps5_opengl_heap_live_bytes(void);
}

namespace {

FILE* log_file;
void Log(const char* format, ...)
{
	if (!log_file) {
		mkdir("/app0/logs", 0777);
		log_file = std::fopen("/app0/logs/playden.log", "w");
	}
	if (!log_file) return;
	va_list arguments;
	va_start(arguments, format);
	std::vfprintf(log_file, format, arguments);
	va_end(arguments);
	std::fputc('\n', log_file);
	std::fflush(log_file);
}

std::uint64_t NowNanoseconds()
{
	timespec value{};
	clock_gettime(CLOCK_MONOTONIC, &value);
	return std::uint64_t(value.tv_sec) * 1'000'000'000ull + value.tv_nsec;
}

double NowSeconds() { return NowNanoseconds() / 1e9; }

class SystemInterface final : public Rml::SystemInterface {
public:
	double GetElapsedTime() override { return NowSeconds() - start_; }
	bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
	{
		Log("[rmlui %d] %s", int(type), message.c_str());
		return true;
	}
private:
	double start_ = NowSeconds();
};

bool CaptureFramebuffer(int width, int height)
{
    // A failed attempt must not leave an old or partial image at the final path.
    if (std::remove("/app0/logs/framebuffer.ppm") != 0 && errno != ENOENT) return false;
    if (std::remove("/app0/logs/framebuffer.ppm.tmp") != 0 && errno != ENOENT) return false;
    if(width <= 0 || height <= 0 || width > 8192 || height > 8192)return false;
    std::vector<unsigned char> rgba(std::size_t(width)*height*4);
    glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
    if(glGetError()!=GL_NO_ERROR)return false;
    FILE* file=std::fopen("/app0/logs/framebuffer.ppm.tmp","wb");
    if(!file)return false;
    bool okay=std::fprintf(file,"P6\n%d %d\n255\n",width,height)>0;
    std::vector<unsigned char> rgb(std::size_t(width)*3);
    for(int y=height-1;okay&&y>=0;--y){
        for(int x=0;x<width;++x)for(int channel=0;channel<3;++channel)
            rgb[std::size_t(x)*3+channel]=rgba[(std::size_t(y)*width+x)*4+channel];
        okay=std::fwrite(rgb.data(),1,rgb.size(),file)==rgb.size();
    }
    const bool closed=std::fclose(file)==0;
    if(!closed||!okay){std::remove("/app0/logs/framebuffer.ppm.tmp");return false;}
    if(std::rename("/app0/logs/framebuffer.ppm.tmp","/app0/logs/framebuffer.ppm")!=0){
        std::remove("/app0/logs/framebuffer.ppm.tmp");return false;
    }
    return true;
}

bool LoadFonts()
{
	bool loaded = true;
	for (const char* name : {"Inter-Regular.ttf", "Inter-Medium.ttf", "Inter-SemiBold.ttf", "Inter-Bold.ttf"})
		loaded = Rml::LoadFontFace(std::string("/app0/assets/fonts/") + name) && loaded;
	return loaded;
}

std::string ProbeStorage()
{
	const std::string root = "/app0/playden-data";
	if (mkdir(root.c_str(), 0777) != 0 && errno != EEXIST) {
		Log("storage probe: mkdir failed errno=%d; persistence unavailable", errno);
		return {};
	}
	playden::Storage storage(root);
	const playden::Bytes expected = {'p', 'l', 'a', 'y', 'd', 'e', 'n'};
	const auto write = storage.WriteAtomic("storage.probe", expected);
	if (!write.ok()) {
		Log("storage probe: write failed (%s); persistence unavailable", write.message().c_str());
		return {};
	}
	const auto read = storage.Read("storage.probe");
	std::remove((root + "/storage.probe").c_str());
	if (!read.ok() || read.value() != expected) {
		Log("storage probe: readback failed; persistence unavailable");
		return {};
	}
	Log("storage probe: atomic write/read succeeded at %s", root.c_str());
	return root;
}

int Run()
{
	static const EGLint config_attributes[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24,
		EGL_STENCIL_SIZE, 8, EGL_NONE};
	static const EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 4, EGL_CONTEXT_MINOR_VERSION_KHR, 6,
		EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE};
	EGLConfig egl_config = nullptr;
	EGLint major = 0, minor = 0, count = 0, width = 0, height = 0;
	EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (display != EGL_NO_DISPLAY) {
		eglSetDisplayModePS5(display, 3840, 2160);
		eglSetDisplayRefreshPS5(display, 60);
	}
	if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) || !eglBindAPI(EGL_OPENGL_API) ||
		!eglChooseConfig(display, config_attributes, &egl_config, 1, &count) || count != 1) {
		Log("egl initialization failed 0x%x", eglGetError());
		return 1;
	}
	EGLSurface surface = eglCreateWindowSurface(display, egl_config, (EGLNativeWindowType)0, nullptr);
	EGLContext gl_context = eglCreateContext(display, egl_config, EGL_NO_CONTEXT, context_attributes);
	if (surface == EGL_NO_SURFACE || gl_context == EGL_NO_CONTEXT ||
		!eglMakeCurrent(display, surface, surface, gl_context) ||
		!eglQuerySurface(display, surface, EGL_WIDTH, &width) || !eglQuerySurface(display, surface, EGL_HEIGHT, &height)) {
		Log("egl surface/context failed 0x%x", eglGetError());
		return 1;
	}
	Log("display: GL %s surface=%dx%d requested=3840x2160@60", reinterpret_cast<const char*>(glGetString(GL_VERSION)), width, height);

	Rml::String renderer_message;
	if (!RmlGL3::Initialize(&renderer_message)) { Log("renderer init failed: %s", renderer_message.c_str()); return 1; }
	SystemInterface system;
	auto renderer = std::make_unique<RenderInterface_GL3>();
	if (!*renderer) { Log("RenderInterface_GL3 failed"); return 1; }
	renderer->SetViewport(width, height);
	Rml::SetSystemInterface(&system);
	Rml::SetRenderInterface(renderer.get());
	if (!Rml::Initialise() || !LoadFonts()) { Log("RmlUi or font initialization failed"); return 1; }
	Rml::Context* context = Rml::CreateContext("playden", {width, height});
	if (!context) { Log("context creation failed"); return 1; }
	context->SetDensityIndependentPixelRatio(height / 1080.f);

	// Enable persistence only after a runtime atomic write/read probe. A failed probe leaves an empty root,
	// which produces a visible storage diagnostic without touching an assumed /data path.
	const std::string storage = ProbeStorage();
	playden::Application app(context, "/app0/assets", storage);
	#ifdef PLAYDEN_DEVELOPER_FIXTURES
 const bool developer_fixtures = true;
#else
 const bool developer_fixtures = false;
#endif
 if (!app.Initialize(developer_fixtures)) { Log("application initialization failed"); return 1; }

	playden::diagnostics::FrameStats stats;
	stats.SetDisplayConfig({playden::diagnostics::Dimensions::Measured(width, height),
		playden::diagnostics::Dimensions::Measured(width, height),
		playden::diagnostics::RefreshConfig::Unknown()});
	stats.SetRevisionMetadata({PLAYDEN_REVISION, PLAYDEN_RMLFOCUS_REVISION,
		"PS5-native-title; memory=process_resident_bytes"});
	Log("diagnostics: memory_source=process_resident_bytes (unavailable samples stay unknown)");

	sceUserServiceInitialize(nullptr);
	scePadInit();
	playden::ps5::PadPlatform platform(NowSeconds, Log);
	rf::Input input;
	double previous = NowSeconds();
 const double capture_started = previous;
 bool capture_done = false;
	double options_down = -1.0;
	bool first_frame = true;
 double next_memory_sample = 0;
	while (!app.ExitRequested()) {
		const std::uint64_t frame_time = NowNanoseconds();
		stats.RecordFrameStart(frame_time);
		const double now = NowSeconds();
		if (platform.TakeInputReset()) input.Reset();
		for (const rf::InputEvent& event : input.Update(platform.PollInput(), now)) app.Handle(event);
		const bool options = platform.OptionsHeld();
		if (options && options_down < 0) options_down = now;
		if (!options) options_down = -1.0;
		if (options_down >= 0 && now - options_down >= 2.0) break;
		app.Tick(static_cast<float>(std::min(now - previous, 0.1)));
		previous = now;
		context->Update();
		renderer->Clear();
		renderer->BeginFrame();
		context->Render();
		renderer->EndFrame();
        if(PLAYDEN_CAPTURE_AFTER_SECONDS > 0 && !capture_done && now-capture_started >= PLAYDEN_CAPTURE_AFTER_SECONDS){
            capture_done=true;
            GLint viewport[4]{};glGetIntegerv(GL_VIEWPORT,viewport);
            const bool captured=CaptureFramebuffer(width,height);
            Log("framebuffer capture path=%s success=%d dimensions=%dx%d viewport=%d,%d,%d,%d expected_bytes=%llu; capture run is not performance evidence",
                captured ? "/app0/logs/framebuffer.ppm" : "none",captured,width,height,viewport[0],viewport[1],viewport[2],viewport[3],
                3ull*width*height+9+std::to_string(width).size()+std::to_string(height).size());
        }
		eglSwapBuffers(display, surface);
		if (first_frame) {
			first_frame = false;
			EGLint scanout_width = 0, scanout_height = 0, refresh_hz = 0;
			if (eglGetDisplayModePS5(display, &scanout_width, &scanout_height, &refresh_hz) && refresh_hz > 0) {
				stats.SetDisplayConfig({playden::diagnostics::Dimensions::Measured(width, height),
					playden::diagnostics::Dimensions::Measured(width, height),
					playden::diagnostics::RefreshConfig::Measured(refresh_hz)});
				Log("display accepted: scanout=%dx%d refresh=%d surface=%dx%d", scanout_width, scanout_height,
					refresh_hz, width, height);
			}
			playden_release_splash();
		}
		const std::uint64_t after_frame = NowNanoseconds();
		if (now >= next_memory_sample) {
            next_memory_sample = now + 1.0;
            const auto memory = playden::ps5::ReadProcessResidentBytes();
            if (memory.available()) stats.RecordMemory(memory.bytes);
            else Log("resident memory unavailable error=%d native_error=%d required_record_bytes=%llu returned_record_bytes=%llu", int(memory.error),memory.native_error,
                static_cast<unsigned long long>(memory.required_record_bytes), static_cast<unsigned long long>(memory.returned_record_bytes));
        }
		if (stats.RecordDue(after_frame)) Log("%s", playden::diagnostics::FrameStats::FormatRecord(stats.TakeRecord(after_frame)).c_str());
	}

	app.Shutdown();
	Rml::RemoveContext("playden");
	Rml::Shutdown();
	renderer.reset();
	eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(display, gl_context);
	eglDestroySurface(display, surface);
	eglTerminate(display);
	return 0;
}

} // namespace

int main()
{
	const int status = Run();
	Log("exit status=%d", status);
	std::fflush(nullptr);
	sceSystemServiceLoadExec("exit", nullptr);
	for (;;) {}
}
