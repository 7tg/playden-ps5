#include "library.h"

#include <RmlUi/Core.h>
#include <rmlfocus/rml_collection.h>

#include <cstdlib>
#include <utility>

namespace playden {
namespace {
std::string Escape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '\"': out += "&quot;"; break;
        case '\'': out += "&#39;"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

GameId IdFromKey(const std::string& key) {
    static const std::string prefix = "game-";
    if (key.compare(0, prefix.size(), prefix) != 0) return 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(key.c_str() + prefix.size(), &end, 10);
    return end && *end == '\0' ? static_cast<GameId>(parsed) : 0;
}
}

void LibraryView::Bind(Rml::ElementDocument* document) {
    CaptureSelection();
    document_ = document;
    collection_ = document ? dynamic_cast<rf::CollectionElement*>(document->GetElementById("library-grid")) : nullptr;
    Populate();
}

void LibraryView::SetGames(const std::vector<GameSummary>& games) {
    CaptureSelection();
    games_ = games;
    Populate();
}

void LibraryView::Populate() {
    if (!collection_) return;
    if (auto* empty = document_->GetElementById("library-empty")) empty->SetProperty("display", games_.empty() ? "block" : "none");
    collection_->SetProperty("display", games_.empty() ? "none" : "block");
    std::vector<rf::CollectionItem> items;
    items.reserve(games_.size());
    for (const GameSummary& game : games_) {
        std::string art;
        if (game.artwork_source.empty())
            art = "<div class='game-art game-art-missing'><span>NO ART</span></div>";
        else
            art = "<rf-image class='game-art' src='" + Escape(game.artwork_source) + "' fit='cover'/>";
        items.push_back({FocusKey(game.id), "<div class='game-tile rf-material'>" + art +
            "<span class='game-title lines-2'>" + Escape(game.title) + "</span></div>"});
    }
    collection_->SetItems(std::move(items));
    if (selected_ && collection_->FocusKey(FocusKey(selected_))) return;
    selected_ = IdFromKey(collection_->Model().FocusedKey());
}

bool LibraryView::FocusGame(GameId id) {
    if (!collection_ || !collection_->FocusKey(FocusKey(id))) return false;
    selected_ = id;
    return true;
}

void LibraryView::CaptureSelection() {
    if (!collection_) return;
    const GameId id = IdFromKey(collection_->Model().FocusedKey());
    if (id) selected_ = id;
}

GameId LibraryView::SelectedGame() const {
    if (collection_) {
        const GameId id = IdFromKey(collection_->Model().FocusedKey());
        if (id) return id;
    }
    return selected_;
}

const GameSummary* LibraryView::Find(GameId id) const {
    for (const GameSummary& game : games_) if (game.id == id) return &game;
    return nullptr;
}

std::size_t LibraryView::LogicalCount() const { return collection_ ? collection_->Model().Size() : games_.size(); }
std::size_t LibraryView::MaterializedCount() const { return collection_ ? collection_->MaterializedCount() : 0; }
float LibraryView::ScrollTarget() const { return collection_ ? collection_->Model().ScrollTarget() : 0.f; }
std::string LibraryView::FocusKey(GameId id) { return "game-" + std::to_string(id); }
}
