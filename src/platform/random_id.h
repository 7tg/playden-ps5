#pragma once

#include <optional>
#include <string>

namespace playden {

// A random (version 4) UUID in lower-case canonical text (8-4-4-4-12 hex digits) from /dev/urandom,
// or nothing when it can't be read.
std::optional<std::string> RandomUuid();

}  // namespace playden
