#include "steam/backend/password_crypto.h"

#include <mbedtls/base64.h>
#include <mbedtls/bignum.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/rsa.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using playden::steam::EncryptPassword;
using playden::steam::ZeroizeSecret;

namespace {
void Check(bool condition, const char* message) {
  if (!condition) { std::fprintf(stderr, "password_crypto_test: %s\n", message); std::exit(1); }
}

std::string HexOf(const mbedtls_mpi& value) {
  char buffer[1100];
  std::size_t written = 0;
  Check(mbedtls_mpi_write_string(&value, 16, buffer, sizeof(buffer), &written) == 0, "hex export");
  return std::string(buffer);
}
}  // namespace

int main() {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_rsa_context rsa;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&drbg);
  mbedtls_rsa_init(&rsa);
  Check(mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, nullptr, 0) == 0, "seed");
  Check(mbedtls_rsa_gen_key(&rsa, mbedtls_ctr_drbg_random, &drbg, 2048, 65537) == 0, "key generation");
  mbedtls_mpi n, e;
  mbedtls_mpi_init(&n);
  mbedtls_mpi_init(&e);
  Check(mbedtls_rsa_export(&rsa, &n, nullptr, nullptr, nullptr, &e) == 0, "export");
  const auto modulus = HexOf(n), exponent = HexOf(e);

  // Round trip: the private key recovers the password.
  const std::string password = "correct horse battery staple ✓";
  const auto encrypted = EncryptPassword(modulus, exponent, password);
  Check(encrypted.has_value() && !encrypted->empty(), "encryption failed");
  std::vector<unsigned char> raw(512);
  std::size_t raw_size = 0;
  Check(mbedtls_base64_decode(raw.data(), raw.size(), &raw_size,
                              reinterpret_cast<const unsigned char*>(encrypted->data()), encrypted->size()) == 0 &&
            raw_size == 256, "not base64 of one RSA block");
  unsigned char plain[256];
  std::size_t plain_size = 0;
  Check(mbedtls_rsa_pkcs1_decrypt(&rsa, mbedtls_ctr_drbg_random, &drbg, &plain_size, raw.data(), plain,
                                  sizeof(plain)) == 0, "decryption failed");
  Check(std::string(reinterpret_cast<char*>(plain), plain_size) == password, "round trip mismatch");
  // Lowercase hex, as Steam sends it, works too.
  std::string lower = modulus;
  for (auto& c : lower) if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
  Check(EncryptPassword(lower, "010001", password).has_value(), "lowercase hex refused");
  // Padding is random: two encryptions differ.
  Check(*EncryptPassword(modulus, exponent, password) != *encrypted, "padding not random");

  // Bad keys fail cleanly.
  Check(!EncryptPassword("", exponent, password), "empty modulus accepted");
  Check(!EncryptPassword("zz" + modulus, exponent, password), "non-hex modulus accepted");
  Check(!EncryptPassword("-" + modulus, exponent, password), "signed modulus accepted");
  Check(!EncryptPassword(modulus, "", password), "empty exponent accepted");
  Check(!EncryptPassword("0f", "03", password), "tiny modulus accepted");
  Check(!EncryptPassword(modulus, exponent, std::string(300, 'x')), "oversized password accepted");

  std::string secret = "hunter2";
  ZeroizeSecret(secret);
  Check(secret.empty(), "secret not cleared");

  mbedtls_mpi_free(&e);
  mbedtls_mpi_free(&n);
  mbedtls_rsa_free(&rsa);
  mbedtls_ctr_drbg_free(&drbg);
  mbedtls_entropy_free(&entropy);
  std::puts("password_crypto_test passed");
  return 0;
}
