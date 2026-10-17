#pragma once

#include "steam/secret.h"

#include <optional>
#include <string>

namespace playden::steam {

// Encrypts an account password with the RSA key Steam returns from GetPasswordRSAPublicKey (hex
// modulus and exponent), PKCS#1 v1.5 as the Steam client does, and returns it base64-encoded.
// Returns nothing when the key is malformed or encryption fails.
std::optional<std::string> EncryptPassword(const std::string& modulus_hex, const std::string& exponent_hex,
                                           const std::string& password);

}  // namespace playden::steam
