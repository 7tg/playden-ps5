#pragma once
#include <string_view>
namespace playden::ps5 {
// Developer-only acceptance fixture identity; never a production ownership proof.
bool ValidateBootstrapProbeSource(std::string_view owner, std::string_view descriptor,
                                  std::string_view param);
}
