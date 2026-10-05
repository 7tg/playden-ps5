#pragma once
#include "platform/ps5/native_access.h"
namespace playden::ps5 {
// Explicit developer-only probe. Returns true when its one-shot request was consumed;
// call after bootstrap, then park instead of entering the normal UI.
bool RunTitleContractProbe(const NativeAccessResult& access);
}
