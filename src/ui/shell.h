#pragma once
#include "details.h"
#include "install_fixture.h"
#include "library.h"
#include <rmlfocus/images.h>
#include <rmlfocus/rml_router.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace playden {
class Shell final : private Rml::EventListener {
public:
    Shell(Rml::Context* context, std::string assets_root);
    ~Shell() override;
    bool Initialize();
    void Handle(const rf::InputEvent& event);
    void Tick(float dt);
    void Shutdown();
    void SetReduceMotion(bool enabled);
    void SetDiagnostic(const std::string& message);
    void SetGames(const std::vector<GameSummary>& games);
    void SetDeveloperFixtures(bool enabled);
    bool FocusGame(GameId id);
    GameId SelectedGame() const;
    std::size_t LibraryLogicalCount() const;
    std::size_t LibraryMaterializedCount() const;
    float LibraryScrollTarget() const;
    rf::ImageStats ImageStats() const;
    bool ExitRequested() const { return exit_requested_; }
    std::string CurrentRoute() const;
    std::string FocusedKey() const;
    Rml::ElementDocument* Document() const;
    std::function<void(bool)> preference_changed;
private:
    void ProcessEvent(Rml::Event& event) override;
    Rml::ElementDocument* LoadRoute(const rf::Route& route);
    void Populate(Rml::ElementDocument* document, const rf::Route& route);
    void ConfigureCollectionNavigation();
    static std::string EventKey(Rml::Element* element);
    void Refresh();
    Rml::Context* context_;
    std::string assets_root_;
    std::unique_ptr<rf::RmlRouter> router_;
    rf::Images images_;
    LibraryView library_;
    DetailsView details_;
    InstallFixture install_fixture_;
    std::vector<GameSummary> games_;
    GameId selected_game_ = 0;
    bool reduce_motion_ = false;
    bool exit_requested_ = false;
    std::string diagnostic_;
};
}
