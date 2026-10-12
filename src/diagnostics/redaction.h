#pragma once
#include "steam/service.h"
#include <string>
namespace playden::diagnostics {
// Whitelist stable labels, not arbitrary transport/protocol error text.
const char* AuthMessage(steam::AuthError error);
std::string AuthRecord(const steam::SteamEvent& event);
}
