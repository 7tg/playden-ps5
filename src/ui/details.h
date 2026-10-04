#pragma once
#include "library/model.h"
namespace Rml { class ElementDocument; }
namespace playden {
class DetailsView {
public:
    void Populate(Rml::ElementDocument* document, const GameSummary* game, bool developer_fixtures) const;
};
}
