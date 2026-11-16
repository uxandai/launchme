#include "password_crypto.hpp"
#include "system.hpp"
#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <openssl/evp.h>
#endif
#include <cstring>

namespace password_crypto {
namespace {
constexpr char magic[8] = {'L', 'M', 'S', 'E', 'A', 'L', '1', '\0'};
constexpr uint32_t max_iterations = 10000000;
void wipe(void* data, size_t size) {
    volatile auto* bytes = static_cast<volatile uint8_t*>(data);
    while (size--) *bytes++ = 0;
}
struct Key {
    std::array<uint8_t, 32> bytes{};
    ~Key() { wipe(bytes.data(), bytes.size()); }
};
bool derive(std::string_view password, const uint8_t* salt, uint32_t iterations, Key& key) {
    const auto derived = pbkdf2Sha256(password, salt, salt_size, iterations, key.bytes.size());
    if (!derived) return false;
    std::memcpy(key.bytes.data(), derived->data(), key.bytes.size());
    return true;
}
}

std::optional<std::vector<uint8_t>> pbkdf2Sha256(std::string_view password, const uint8_t* salt, size_t salt_length, uint32_t iterations, size_t length) {
    if (!iterations || !length || length > 1024) return std::nullopt;
    std::vector<uint8_t> output(length);
#ifdef _WIN32
    if (!BCRYPT_SUCCESS(BCryptDeriveKeyPBKDF2(BCRYPT_HMAC_SHA256_ALG_HANDLE, reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
                                              static_cast<ULONG>(password.size()), const_cast<PUCHAR>(salt), static_cast<ULONG>(salt_length),
                                              iterations, output.data(), static_cast<ULONG>(length), 0))) return std::nullopt;
#else
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt, static_cast<int>(salt_length), static_cast<int>(iterations),
                          EVP_sha256(), static_cast<int>(length), output.data()) != 1) return std::nullopt;
#endif
    return output;
}

std::optional<std::string> aesGcmEncrypt(const uint8_t* key, const uint8_t* nonce, std::string_view plain) {
    std::string out(plain.size() + tag_size, '\0');
    auto* cipher = reinterpret_cast<uint8_t*>(out.data());
    uint8_t* tag = cipher + plain.size();
#ifdef _WIN32
    BCRYPT_KEY_HANDLE handle = nullptr;
    if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(BCRYPT_AES_GCM_ALG_HANDLE, &handle, nullptr, 0, const_cast<PUCHAR>(key), 32, 0))) return std::nullopt;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce); info.cbNonce = static_cast<ULONG>(nonce_size);
    info.pbTag = tag; info.cbTag = static_cast<ULONG>(tag_size);
    ULONG written = 0;
    const bool ok = BCRYPT_SUCCESS(BCryptEncrypt(handle, reinterpret_cast<PUCHAR>(const_cast<char*>(plain.data())), static_cast<ULONG>(plain.size()),
                                                 &info, nullptr, 0, cipher, static_cast<ULONG>(plain.size()), &written, 0)) && written == plain.size();
    BCryptDestroyKey(handle);
    if (!ok) return std::nullopt;
#else
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    int length = 0, final_length = 0;
    const bool ok = context && EVP_EncryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce_size), nullptr) == 1 &&
        EVP_EncryptInit_ex(context, nullptr, nullptr, key, nonce) == 1 &&
        (plain.empty() || EVP_EncryptUpdate(context, cipher, &length, reinterpret_cast<const uint8_t*>(plain.data()), static_cast<int>(plain.size())) == 1) &&
        EVP_EncryptFinal_ex(context, cipher + length, &final_length) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag_size), tag) == 1;
    EVP_CIPHER_CTX_free(context);
    if (!ok) return std::nullopt;
#endif
    return out;
}

std::optional<std::string> aesGcmDecrypt(const uint8_t* key, const uint8_t* nonce, std::string_view cipher_and_tag) {
    if (cipher_and_tag.size() < tag_size) return std::nullopt;
    const size_t size = cipher_and_tag.size() - tag_size;
    const auto* cipher = reinterpret_cast<const uint8_t*>(cipher_and_tag.data());
    const uint8_t* tag = cipher + size;
    std::string plain(size, '\0');
#ifdef _WIN32
    BCRYPT_KEY_HANDLE handle = nullptr;
    if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(BCRYPT_AES_GCM_ALG_HANDLE, &handle, nullptr, 0, const_cast<PUCHAR>(key), 32, 0))) return std::nullopt;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce); info.cbNonce = static_cast<ULONG>(nonce_size);
    info.pbTag = const_cast<PUCHAR>(tag); info.cbTag = static_cast<ULONG>(tag_size);
    ULONG written = 0;
    const bool ok = BCRYPT_SUCCESS(BCryptDecrypt(handle, const_cast<PUCHAR>(cipher), static_cast<ULONG>(size), &info, nullptr, 0,
                                                 reinterpret_cast<PUCHAR>(plain.data()), static_cast<ULONG>(size), &written, 0)) && written == size;
    BCryptDestroyKey(handle);
#else
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    int length = 0, final_length = 0;
    const bool ok = context && EVP_DecryptInit_ex(context, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce_size), nullptr) == 1 &&
        EVP_DecryptInit_ex(context, nullptr, nullptr, key, nonce) == 1 &&
        (size == 0 || EVP_DecryptUpdate(context, reinterpret_cast<uint8_t*>(plain.data()), &length, cipher, static_cast<int>(size)) == 1) &&
        EVP_CIPHER_CTX_ctrl(context, EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag_size), const_cast<uint8_t*>(tag)) == 1 &&
        EVP_DecryptFinal_ex(context, reinterpret_cast<uint8_t*>(plain.data()) + length, &final_length) == 1;
    EVP_CIPHER_CTX_free(context);
#endif
    if (!ok) { wipe(plain.data(), plain.size()); return std::nullopt; }
    return plain;
}

bool sealed(std::string_view bytes) {
    return bytes.size() >= header_size + tag_size && std::memcmp(bytes.data(), magic, sizeof(magic)) == 0;
}

std::optional<std::string> sealWith(std::string_view password, std::string_view plain, uint32_t iterations,
                                    const std::array<uint8_t, salt_size>& salt, const std::array<uint8_t, nonce_size>& nonce) {
    if (password.empty() || !iterations || iterations > max_iterations) return std::nullopt;
    Key key;
    if (!derive(password, salt.data(), iterations, key)) return std::nullopt;
    auto body = aesGcmEncrypt(key.bytes.data(), nonce.data(), plain);
    if (!body) return std::nullopt;
    std::string out(magic, sizeof(magic));
    for (int shift = 0; shift < 32; shift += 8) out += static_cast<char>((iterations >> shift) & 0xFF);
    out.append(reinterpret_cast<const char*>(salt.data()), salt.size());
    out.append(reinterpret_cast<const char*>(nonce.data()), nonce.size());
    out += *body;
    return out;
}

std::optional<std::string> seal(std::string_view password, std::string_view plain, uint32_t iterations) {
    std::array<uint8_t, salt_size> salt{};
    std::array<uint8_t, nonce_size> nonce{};
    if (!system_info::randomBytes(salt.data(), salt.size()) || !system_info::randomBytes(nonce.data(), nonce.size())) return std::nullopt;
    return sealWith(password, plain, iterations, salt, nonce);
}

std::optional<std::string> open(std::string_view password, std::string_view bytes) {
    if (password.empty() || !sealed(bytes)) return std::nullopt;
    const auto* data = reinterpret_cast<const uint8_t*>(bytes.data());
    uint32_t iterations = 0;
    for (int i = 0; i < 4; ++i) iterations |= static_cast<uint32_t>(data[8 + i]) << (8 * i);
    if (!iterations || iterations > max_iterations) return std::nullopt;
    const uint8_t* salt = data + 12;
    const uint8_t* nonce = salt + salt_size;
    Key key;
    if (!derive(password, salt, iterations, key)) return std::nullopt;
    return aesGcmDecrypt(key.bytes.data(), nonce, bytes.substr(header_size));
}
}
