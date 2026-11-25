// Linux/other: protected_file.hpp's read/write (Windows uses DPAPI inline).
#include "protected_file.hpp"
#include "user_config.hpp"

#include <libsecret/secret.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sys/stat.h>

#include <array>
#include <cstring>
#include <mutex>

namespace protected_file {
namespace {
// File layout: magic, 12-byte nonce, ciphertext, 16-byte GCM tag.
constexpr std::string_view magic = "LMP1";
constexpr size_t key_size = 32, nonce_size = 12, tag_size = 16;
using Key = std::array<uint8_t, key_size>;

const SecretSchema* schema() {
    static const SecretSchema value{"io.github.launchme.FileKey", SECRET_SCHEMA_NONE,
        {{"purpose", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    return &value;
}

std::string hex(const Key& key) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (const uint8_t byte : key) { text += digits[byte >> 4]; text += digits[byte & 15]; }
    return text;
}
std::optional<Key> parseHex(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1);
    if (text.size() != key_size * 2) return std::nullopt;
    Key key{};
    for (size_t i = 0; i < key_size; ++i) {
        const auto nibble = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
        const int high = nibble(text[i * 2]), low = nibble(text[i * 2 + 1]);
        if (high < 0 || low < 0) return std::nullopt;
        key[i] = static_cast<uint8_t>(high << 4 | low);
    }
    return key;
}

// The desktop keyring, when one answers; nullopt otherwise (headless sessions, no daemon).
std::optional<Key> keyringKey(bool create) {
    GError* error = nullptr;
    gchar* stored = secret_password_lookup_sync(schema(), nullptr, &error, "purpose", "protected-files", nullptr);
    if (error) { g_error_free(error); return std::nullopt; }
    if (stored) {
        auto key = parseHex(stored);
        secret_password_free(stored);
        return key;
    }
    if (!create) return std::nullopt;
    Key key{};
    if (RAND_bytes(key.data(), static_cast<int>(key.size())) != 1) return std::nullopt;
    const auto text = hex(key);
    const bool saved = secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT, "Launchme file key", text.c_str(),
                                                  nullptr, &error, "purpose", "protected-files", nullptr);
    if (error) g_error_free(error);
    return saved ? std::optional<Key>(key) : std::nullopt;
}

// No keyring: a key readable by this user only, in the data folder.
std::optional<Key> fileKey(bool create) {
    const auto file = userConfigDirectory() / L"protected.key";
    {
        std::ifstream input(file, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (auto key = parseHex(text)) return key;
    }
    if (!create) return std::nullopt;
    Key key{};
    if (RAND_bytes(key.data(), static_cast<int>(key.size())) != 1) return std::nullopt;
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    // Created 0600 itself: no umask change, which is process-wide while other threads create files.
    if (!atomic_file::writeDurable(file, hex(key) + "\n", 0600)) return std::nullopt;
    ::chmod(file.c_str(), 0600); // A file left by an older version with wider rights.
    return key;
}

std::optional<Key> key(bool create) {
    static std::mutex lock;
    static std::optional<Key> cached;
    std::lock_guard guard(lock);
    if (cached) return cached;
    // A key already in either place wins, so a keyring that appears later does not orphan files.
    if (auto found = keyringKey(false)) return cached = found;
    if (auto found = fileKey(false)) return cached = found;
    if (!create) return std::nullopt;
    if (auto made = keyringKey(true)) return cached = made;
    return cached = fileKey(true);
}

struct Cipher {
    EVP_CIPHER_CTX* value = EVP_CIPHER_CTX_new();
    ~Cipher() { EVP_CIPHER_CTX_free(value); }
};
}

std::optional<std::string> read(const std::filesystem::path& file, size_t limit) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > static_cast<std::streamoff>(limit)) return {};
    if (input.tellg() == 0) return std::string{};
    std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg())); input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
    if (bytes.size() < magic.size() + nonce_size + tag_size || std::memcmp(bytes.data(), magic.data(), magic.size()) != 0) return {};
    const auto secret = key(false);
    if (!secret) return {};
    const uint8_t* nonce = bytes.data() + magic.size();
    const uint8_t* body = nonce + nonce_size;
    const size_t body_size = bytes.size() - magic.size() - nonce_size - tag_size;
    const uint8_t* tag = body + body_size;
    Cipher cipher;
    std::string plain(body_size, '\0');
    int length = 0, final_length = 0;
    const bool ok = cipher.value &&
        EVP_DecryptInit_ex(cipher.value, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(cipher.value, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce_size), nullptr) == 1 &&
        EVP_DecryptInit_ex(cipher.value, nullptr, nullptr, secret->data(), nonce) == 1 &&
        EVP_DecryptUpdate(cipher.value, reinterpret_cast<uint8_t*>(plain.data()), &length, body, static_cast<int>(body_size)) == 1 &&
        EVP_CIPHER_CTX_ctrl(cipher.value, EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag_size), const_cast<uint8_t*>(tag)) == 1 &&
        EVP_DecryptFinal_ex(cipher.value, reinterpret_cast<uint8_t*>(plain.data()) + length, &final_length) == 1;
    if (!ok) { OPENSSL_cleanse(plain.data(), plain.size()); return {}; }
    plain.resize(static_cast<size_t>(length + final_length));
    return plain;
}

bool write(const std::filesystem::path& file, std::string_view text, const wchar_t*) {
    if (text.size() > static_cast<size_t>(INT32_MAX) - 64) return false;
    const auto secret = key(true);
    if (!secret) return false;
    std::vector<uint8_t> bytes(magic.size() + nonce_size + text.size() + tag_size);
    std::memcpy(bytes.data(), magic.data(), magic.size());
    uint8_t* nonce = bytes.data() + magic.size();
    uint8_t* body = nonce + nonce_size;
    if (RAND_bytes(nonce, static_cast<int>(nonce_size)) != 1) return false;
    Cipher cipher;
    int length = 0, final_length = 0;
    const bool ok = cipher.value &&
        EVP_EncryptInit_ex(cipher.value, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(cipher.value, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce_size), nullptr) == 1 &&
        EVP_EncryptInit_ex(cipher.value, nullptr, nullptr, secret->data(), nonce) == 1 &&
        EVP_EncryptUpdate(cipher.value, body, &length, reinterpret_cast<const uint8_t*>(text.data()), static_cast<int>(text.size())) == 1 &&
        EVP_EncryptFinal_ex(cipher.value, body + length, &final_length) == 1 &&
        EVP_CIPHER_CTX_ctrl(cipher.value, EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag_size), body + text.size()) == 1;
    if (!ok) return false;
    // Durable (a key the user typed once is not lost to a power cut) and readable by this user only.
    return atomic_file::writeDurable(file, std::span<const uint8_t>(bytes), 0600);
}
}
