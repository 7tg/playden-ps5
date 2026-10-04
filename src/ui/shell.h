#pragma once
#include <rmlfocus/rml_router.h>
#include <functional>
#include <memory>
#include <string>

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
    bool ExitRequested() const { return exit_requested_; }
    std::string CurrentRoute() const;
    std::string FocusedKey() const;
    Rml::ElementDocument* Document() const;
    std::function<void(bool)> preference_changed;
private:
    void ProcessEvent(Rml::Event& event) override;
    void Refresh();
    Rml::Context* context_;
    std::string assets_root_;
    std::unique_ptr<rf::RmlRouter> router_;
    bool reduce_motion_ = false;
    bool exit_requested_ = false;
    std::string diagnostic_;
};
}
