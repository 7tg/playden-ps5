#include "steam/backend/password_crypto.h"

#include <mbedtls/base64.h>
#include <mbedtls/bignum.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/rsa.h>

#include <algorithm>
#include <vector>

namespace playden::steam {
namespace {
constexpr std::size_t kMaxModulusHex = 1024;  // 4096 bits
constexpr std::size_t kMaxExponentHex = 16;

bool Hex(const std::string& value, std::size_t limit) {
  return !value.empty() && value.size() <= limit &&
         std::all_of(value.begin(), value.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
         });
}

// The contexts are freed on every path; the padding's random bytes come from the same entropy
// source and DRBG the TLS stack uses.
struct Contexts {
  mbedtls_rsa_context rsa;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_mpi n, e;
  Contexts() {
    mbedtls_rsa_init(&rsa);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_mpi_init(&n);
    mbedtls_mpi_init(&e);
  }
  ~Contexts() {
    mbedtls_mpi_free(&e);
    mbedtls_mpi_free(&n);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_rsa_free(&rsa);
  }
  Contexts(const Contexts&) = delete;
  Contexts& operator=(const Contexts&) = delete;
};
}  // namespace

std::optional<std::string> EncryptPassword(const std::string& modulus_hex, const std::string& exponent_hex,
                                           const std::string& password) {
  if (!Hex(modulus_hex, kMaxModulusHex) || !Hex(exponent_hex, kMaxExponentHex)) return std::nullopt;
  Contexts c;
  if (mbedtls_mpi_read_string(&c.n, 16, modulus_hex.c_str()) != 0 ||
      mbedtls_mpi_read_string(&c.e, 16, exponent_hex.c_str()) != 0 ||
      mbedtls_rsa_import(&c.rsa, &c.n, nullptr, nullptr, nullptr, &c.e) != 0 ||
      mbedtls_rsa_complete(&c.rsa) != 0 || mbedtls_rsa_check_pubkey(&c.rsa) != 0)
    return std::nullopt;
  const std::size_t size = mbedtls_rsa_get_len(&c.rsa);
  // PKCS#1 v1.5 needs 11 bytes of padding.
  if (password.size() + 11 > size) return std::nullopt;
  static const unsigned char kPersonal[] = "playden-password";
  if (mbedtls_ctr_drbg_seed(&c.drbg, mbedtls_entropy_func, &c.entropy, kPersonal, sizeof(kPersonal) - 1) != 0)
    return std::nullopt;
  std::vector<unsigned char> encrypted(size);
  if (mbedtls_rsa_pkcs1_encrypt(&c.rsa, mbedtls_ctr_drbg_random, &c.drbg, password.size(),
                                reinterpret_cast<const unsigned char*>(password.data()), encrypted.data()) != 0)
    return std::nullopt;
  std::size_t written = 0;
  std::string encoded(4 * ((size + 2) / 3) + 1, '\0');
  if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(encoded.data()), encoded.size(), &written,
                            encrypted.data(), encrypted.size()) != 0)
    return std::nullopt;
  encoded.resize(written);
  return encoded;
}

}  // namespace playden::steam
