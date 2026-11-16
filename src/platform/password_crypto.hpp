#pragma once
// Data sealed with a password, readable on any computer and system (unlike protected_file.hpp, which
// binds to this user): PBKDF2-HMAC-SHA256 derives an AES-256-GCM key. CNG on Windows, OpenSSL elsewhere;
// both produce the same bytes. Layout: "LMSEAL1\0", iterations (u32 LE), salt (16), nonce (12), ciphertext, tag (16).
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace password_crypto {
inline constexpr uint32_t default_iterations = 600000;
inline constexpr size_t salt_size = 16, nonce_size = 12, tag_size = 16, header_size = 8 + 4 + salt_size + nonce_size;

// nullopt when the system's crypto fails (or the password is empty).
std::optional<std::string> seal(std::string_view password, std::string_view plain, uint32_t iterations = default_iterations);
// Same, with a given salt and nonce (tests: fixed vectors).
std::optional<std::string> sealWith(std::string_view password, std::string_view plain, uint32_t iterations,
                                    const std::array<uint8_t, salt_size>& salt, const std::array<uint8_t, nonce_size>& nonce);
// nullopt: a wrong password, or data that was changed or is not sealed.
std::optional<std::string> open(std::string_view password, std::string_view sealed);
bool sealed(std::string_view bytes);

// The building blocks (tests: RFC 7914 and NIST GCM vectors).
std::optional<std::vector<uint8_t>> pbkdf2Sha256(std::string_view password, const uint8_t* salt, size_t salt_length, uint32_t iterations, size_t length);
// Ciphertext followed by the 16-byte tag.
std::optional<std::string> aesGcmEncrypt(const uint8_t* key, const uint8_t* nonce, std::string_view plain);
std::optional<std::string> aesGcmDecrypt(const uint8_t* key, const uint8_t* nonce, std::string_view cipher_and_tag);
}
