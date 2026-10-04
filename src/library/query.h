#pragma once
#include "library/model.h"
#include <vector>

namespace playden {
enum class LibrarySort { TitleAscending, TitleDescending, AppId };
struct LibraryQuery {
    std::string text;
    std::string developer; // Empty means all; exact case-folded developer label otherwise.
};
// Unicode case folding, then bytewise UTF-8 order; deliberately not locale collation.
// Only supplied ownership records are queried. No install/playability state is inferred.
std::vector<GameId> QueryLibrary(const std::vector<GameSummary>& games,
                                const LibraryQuery& query = {},
                                LibrarySort sort = LibrarySort::TitleAscending);
} // namespace playden
