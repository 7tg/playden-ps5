#pragma once
namespace playden::ps5 {
// Explicit developer-only probe. Returns true when its one-shot request was consumed;
// caller must park instead of starting normal UI after the root view changed.
bool RunTitleContractProbe();
}
