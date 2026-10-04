#include "library/model.h"
#include "ui/library.h"
#include "ui/shell.h"

#include <RmlUi/Core.h>
#include <rmlfocus/rml_collection.h>

#include <cstdio>
#include <cstdlib>
#include <string>
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

std::vector<playden::GameSummary> Games(std::size_t count) {
    std::vector<playden::GameSummary> games;
    games.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        games.push_back({static_cast<playden::GameId>(1000 + index), "Fixture " + std::to_string(index),
            "missing-" + std::to_string(index) + ".png", "Synthetic description", "Fixture Studio"});
    return games;
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
    auto* context = Rml::CreateContext("library-navigation", {3840, 2160});
    Check(context != nullptr, "4K context creates");
    context->SetDensityIndependentPixelRatio(2.f);

    auto* document = context->LoadDocument(std::string(argv[1]) + "/ui/library.rml");
    Check(document != nullptr, "library document loads");
    document->Show();
    playden::LibraryView library;
    library.Bind(document);
    auto games = Games(1000);
    library.SetGames(games);
    context->Update();
    Check(library.LogicalCount() == 1000, "fixture keeps 1,000 logical records");
    Check(library.MaterializedCount() <= 50, "library DOM and artwork subscriptions stay bounded");
    Check(library.FocusGame(1500), "off-window game focuses by stable ID");
    const float prior_scroll = library.ScrollTarget();

    std::vector<playden::GameSummary> filtered(games.begin() + 400, games.begin() + 650);
    library.SetGames(filtered);
    Check(library.SelectedGame() == 1500, "filter snapshot preserves selected stable ID");
    Check(library.ScrollTarget() != 0.f, "filter snapshot keeps selected game scrolled into view");
    filtered.erase(filtered.begin() + 100);
    library.SetGames(filtered);
    Check(library.SelectedGame() == 1501, "removing focused game selects its next neighbor");
    Check(library.ScrollTarget() != prior_scroll, "reconciled snapshot recomputes a valid scroll target");
    document->Close();
    context->Update();

    {
        playden::Shell shell(context, argv[1]);
        shell.SetGames(games);
        shell.SetDeveloperFixtures(true);
        Check(shell.Initialize(), "fixture shell initializes");
        auto tick = [&] { shell.Tick(1.f / 60.f); context->Update(); };
        auto press = [&](rf::Command command) {
            shell.Handle({command, true, false});
            shell.Handle({command, false, false});
            tick();
        };
        press(rf::Command::NextTab);
        Check(shell.CurrentRoute() == "library", "library tab opens");
        Check(shell.FocusGame(1500), "shell focuses deep library record");
        const auto selected = shell.SelectedGame();
        const float selected_scroll = shell.LibraryScrollTarget();
        press(rf::Command::Select);
        Check(shell.CurrentRoute() == "details", "game selection opens details");
        press(rf::Command::Select);
        Check(shell.CurrentRoute() == "install", "fixture install opens as a sheet");
        press(rf::Command::Select);
        Check(shell.CurrentRoute() == "confirmation", "fixture install opens nested confirmation");
        press(rf::Command::Back);
        Check(shell.CurrentRoute() == "install" && shell.FocusedKey() == "install-continue",
            "dismissing confirmation restores its exact opener");
        press(rf::Command::Select);
        Check(shell.CurrentRoute() == "confirmation", "confirmation reopens");
        press(rf::Command::Select);
        Check(shell.CurrentRoute() == "install", "confirming returns to install sheet");
        press(rf::Command::Right);
        Check(shell.FocusedKey() == "view-queue", "confirmed fixture exposes queue action");
        press(rf::Command::Select);
        Check(shell.CurrentRoute() == "queue", "fixture queue opens");
        const std::string first_progress = shell.Document()->GetElementById("queue-progress")->GetInnerRML();
        for (int frame = 0; frame < 120; ++frame) tick();
        Check(shell.Document()->GetElementById("queue-progress")->GetInnerRML() != first_progress,
            "fixture queue visibly changes while frames advance");
        press(rf::Command::Back);
        Check(shell.CurrentRoute() == "install" && shell.FocusedKey() == "view-queue",
            "dismissing queue restores its exact opener");
        press(rf::Command::Back);
        Check(shell.CurrentRoute() == "details" && shell.FocusedKey() == "details-install",
            "dismissing install restores its exact opener");
        press(rf::Command::Back);
        Check(shell.CurrentRoute() == "library" && shell.SelectedGame() == selected,
            "returning from details restores selected ID and collection position");
        Check(shell.LibraryScrollTarget() == selected_scroll,
            "returning from nested details and sheets restores the exact collection scroll target");
        shell.Shutdown();
    }
    {
        playden::Shell shell(context, argv[1]);
        shell.SetGames(games);
        Check(shell.Initialize(), "normal shell initializes with game data");
        auto press = [&](rf::Command command) {
            shell.Handle({command, true, false});
            shell.Handle({command, false, false});
            shell.Tick(1.f / 60.f);
            context->Update();
        };
        press(rf::Command::NextTab);
        press(rf::Command::Select);
        Check(shell.CurrentRoute() == "details" && shell.FocusedKey() == "details-back",
            "normal build details omit fixture install from focus navigation");
        Check(shell.Document()->GetElementById("details-install")->GetComputedValues().display() == Rml::Style::Display::None,
            "normal mode keeps synthetic install action unavailable");
        shell.Shutdown();
    }
    {
        playden::Shell shell(context, argv[1]);
        Check(shell.Initialize(), "shell can initialize before an asynchronous library arrives");
        shell.SetGames(games);
        shell.Handle({rf::Command::NextTab, true, false});
        shell.Handle({rf::Command::NextTab, false, false});
        shell.Tick(1.f / 60.f);
        context->Update();
        Check(shell.CurrentRoute() == "library" && shell.FocusedKey() == playden::LibraryView::FocusKey(games.front().id),
            "late library population updates the tab default to the first stable game ID");
        shell.Shutdown();
    }

    Rml::RemoveContext("library-navigation");
    Rml::Shutdown();
    std::puts("library_navigation_test passed");
}
