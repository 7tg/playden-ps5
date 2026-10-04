#pragma once

#include "library/model.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Rml { class ElementDocument; }
namespace rf { class CollectionElement; }

namespace playden {
class LibraryView {
public:
    void Bind(Rml::ElementDocument* document);
    void SetGames(const std::vector<GameSummary>& games);
    bool FocusGame(GameId id);
    GameId SelectedGame() const;
    const GameSummary* Find(GameId id) const;
    std::size_t LogicalCount() const;
    std::size_t MaterializedCount() const;
    float ScrollTarget() const;

    static std::string FocusKey(GameId id);

private:
    void Populate();
    void CaptureSelection();
    Rml::ElementDocument* document_ = nullptr;
    rf::CollectionElement* collection_ = nullptr;
    std::vector<GameSummary> games_;
    GameId selected_ = 0;
};
}
