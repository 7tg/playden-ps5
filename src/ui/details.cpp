#include "details.h"
#include <RmlUi/Core.h>
namespace playden {
namespace {
void Text(Rml::ElementDocument* document, const char* id, const std::string& value) {
    if (auto* element = document->GetElementById(id)) {
        element->SetInnerRML("");
        element->AppendChild(document->CreateTextNode(value));
    }
}
}
void DetailsView::Populate(Rml::ElementDocument* document, const GameSummary* game, bool fixtures) const {
    if (!document) return;
    Text(document, "details-title", game ? game->title : "Unavailable game");
    Text(document, "details-developer", game && !game->developer.empty() ? game->developer : "Developer unavailable");
    Text(document, "details-description", game && !game->description.empty() ? game->description : "No description is available.");
    if (auto* art = document->GetElementById("details-art")) {
        if (game && !game->artwork_source.empty()) art->SetAttribute("src", game->artwork_source);
        else art->SetAttribute("src", "missing-artwork://none");
    }
    if (auto* action = document->GetElementById("details-install")) action->SetProperty("display", fixtures ? "block" : "none");
    if (auto* notice = document->GetElementById("fixture-notice")) notice->SetProperty("display", fixtures ? "block" : "none");
}
}
