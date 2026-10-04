#include "library/model.h"
#include "ui/library.h"

#include <RmlUi/Core.h>
#include <rmlfocus/images.h>
#include <rmlfocus/rml_collection.h>
#include <rmlfocus/rml_image.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
class NullRender final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 1; }
    void ReleaseTexture(Rml::TextureHandle) override {}
};
void Check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
std::vector<unsigned char> Png() {
    static const unsigned char bytes[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,1,8,6,0,0,0,244,34,127,138,0,0,0,17,73,68,65,84,120,156,99,60,145,98,212,192,192,192,192,0,0,12,186,1,224,195,81,84,107,0,0,0,0,73,69,78,68,174,66,96,130};
    return {bytes, bytes + sizeof(bytes)};
}
}

int main(int argc, char** argv) {
    Check(argc == 2, "asset root argument");
    NullRender render;
    Rml::SetRenderInterface(&render);
    Check(Rml::Initialise(), "RmlUi initializes");
    Check(Rml::LoadFontFace(std::string(argv[1]) + "/fonts/Inter-Regular.ttf"), "regular fixture font loads");
    Check(Rml::LoadFontFace(std::string(argv[1]) + "/fonts/Inter-Bold.ttf"), "bold fixture font loads");
    rf::RegisterCollections();
    std::mutex mutex;
    std::condition_variable cv;
    bool old_started = false;
    bool release_old = false;
    rf::ImageLimits limits;
    limits.worker_count = 2;
    limits.max_queued = 8;
    rf::Images images(limits, [&](const std::string& source, size_t, std::vector<unsigned char>& output, std::string& error) {
        if (source.find("old.png") != std::string::npos) {
            std::unique_lock<std::mutex> lock(mutex);
            old_started = true;
            cv.notify_all();
            cv.wait(lock, [&] { return release_old; });
            output = Png();
            return true;
        }
        error = "synthetic artwork failure";
        return false;
    });
    rf::RegisterImageElement(images);
    auto* context = Rml::CreateContext("artwork-binding", {3840, 2160});
    context->SetDensityIndependentPixelRatio(2.f);
    auto* document = context->LoadDocument(std::string(argv[1]) + "/ui/library.rml");
    Check(document != nullptr, "library document loads");
    document->Show();
    playden::LibraryView library;
    library.Bind(document);
    library.SetGames({{42, "Recycled tile", "old.png", "", ""}});
    context->Update();
    {
        std::unique_lock<std::mutex> lock(mutex);
        Check(cv.wait_for(lock, std::chrono::seconds(2), [&] { return old_started; }), "old artwork request begins");
    }
    library.SetGames({{42, "Recycled tile", "new-missing.png", "", ""}});
    auto* image = document->QuerySelector("rf-image");
    Check(image != nullptr, "materialized tile owns an rf-image subscription");
    for (int i = 0; i < 300 && image->GetAttribute<Rml::String>("data-state", "") != "error"; ++i) {
        images.Pump(8);
        context->Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Check(image->GetAttribute<Rml::String>("data-state", "") == "error", "current failed artwork shows stable fallback state");
    {
        std::lock_guard<std::mutex> lock(mutex);
        release_old = true;
    }
    cv.notify_all();
    for (int i = 0; i < 100; ++i) {
        images.Pump(8);
        context->Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Check(image->GetAttribute<Rml::String>("data-state", "") == "error",
        "late old artwork completion cannot replace the tile's current source");
    Check(images.Stats().queued <= limits.max_queued, "artwork request queue respects its configured bound");
    Rml::RemoveContext("artwork-binding");
    Rml::Shutdown();
    std::puts("artwork_binding_test passed");
}
