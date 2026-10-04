#pragma once
#include "library/model.h"
#include <string>
namespace Rml { class ElementDocument; }
namespace playden {
class InstallFixture {
public:
    void SetEnabled(bool enabled);
    bool Enabled() const { return enabled_; }
    void Begin(GameId game, std::string title);
    void Confirm();
    void Tick(float dt);
    void PopulateInstall(Rml::ElementDocument* document) const;
    void PopulateConfirmation(Rml::ElementDocument* document) const;
    void PopulateQueue(Rml::ElementDocument* document) const;
    int ProgressPercent() const;
private:
    bool enabled_ = false;
    bool queued_ = false;
    GameId game_ = 0;
    std::string title_;
    float progress_ = 0.f;
};
}
