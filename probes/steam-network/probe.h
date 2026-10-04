#pragma once

namespace playden::steam_probe {

// Runs the credential-free networking gate and writes sanitized JSON lines to stdout.
// ca_bundle must name a readable PEM bundle. Returns zero only when every check passes.
int Run(const char* ca_bundle);

}  // namespace playden::steam_probe
