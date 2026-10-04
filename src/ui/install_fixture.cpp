#include "install_fixture.h"
#include <RmlUi/Core.h>
#include <algorithm>
namespace playden {
namespace {
void Text(Rml::ElementDocument* document, const char* id, const std::string& value) {
    if (auto* element = document->GetElementById(id)) {
        element->SetInnerRML("");
        element->AppendChild(document->CreateTextNode(value));
    }
}
}
void InstallFixture::SetEnabled(bool enabled) { enabled_ = enabled; if (!enabled) { queued_ = false; progress_ = 0.f; } }
void InstallFixture::Begin(GameId game, std::string title) {
    if (!enabled_) return;
    game_ = game;
    title_ = std::move(title);
    queued_ = false;
    progress_ = 0.f;
}
void InstallFixture::Confirm() { if (enabled_ && game_) queued_ = true; }
void InstallFixture::Tick(float dt) {
    if (enabled_ && queued_ && dt > 0.f) progress_ = std::min(1.f, progress_ + dt * 0.04f);
}
void InstallFixture::PopulateInstall(Rml::ElementDocument* document) const {
    if (!document) return;
    Text(document, "install-title", title_.empty() ? "Install performance fixture" : "Install " + title_);
    Text(document, "install-state", queued_ ? "Synthetic queue is active." : "Choose the synthetic internal storage target.");
    if (auto* queue = document->GetElementById("view-queue")) queue->SetProperty("display", queued_ ? "block" : "none");
}
void InstallFixture::PopulateConfirmation(Rml::ElementDocument* document) const {
    if (document) Text(document, "confirmation-game", title_);
}
void InstallFixture::PopulateQueue(Rml::ElementDocument* document) const {
    if (!document) return;
    Text(document, "queue-game", title_.empty() ? "No synthetic job" : title_);
    Text(document, "queue-progress", std::to_string(ProgressPercent()) + "% — simulated transfer");
    if (auto* bar = document->GetElementById("queue-progress-bar"))
        bar->SetProperty("width", std::to_string(ProgressPercent()) + "%");
}
int InstallFixture::ProgressPercent() const { return static_cast<int>(progress_ * 100.f); }
}
