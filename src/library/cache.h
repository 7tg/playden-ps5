#pragma once
#include "library/model.h"
#include "platform/storage.h"
#include <vector>

namespace playden {
struct LibrarySnapshot {
    std::uint32_t schema_version = 2;
    std::uint64_t retrieved_at_unix_seconds = 0;
    std::vector<GameSummary> games;
};
class LibraryCache {
public:
    explicit LibraryCache(Storage& storage) : storage_(storage) {}
    Result<LibrarySnapshot> Load(const std::string& account_id) const;
    Result<void> Save(const std::string& account_id, const LibrarySnapshot& snapshot) const;
private:
    Storage& storage_;
};
}
