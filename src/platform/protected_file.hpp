#pragma once
// Files only this user can read back: DPAPI on Windows. Elsewhere AES-256-GCM with a key kept in
// the desktop keyring (Secret Service), or, without one, in a 0600 file in the data folder
// (protected_file.cpp).
#include "atomic_file.hpp"
#ifdef _WIN32
#include <wincrypt.h>
#endif
#include <optional>
#include <vector>

namespace protected_file {
#ifndef _WIN32
std::optional<std::string> read(const std::filesystem::path& file, size_t limit);
bool write(const std::filesystem::path& file, std::string_view text, const wchar_t* description);
#else
inline std::optional<std::string> read(const std::filesystem::path& file, size_t limit) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() < 0 || input.tellg() > static_cast<std::streamoff>(limit)) return {};
    if (input.tellg() == 0) return std::string{};
    std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg())); input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) return {};
    DATA_BLOB encrypted{static_cast<DWORD>(bytes.size()), bytes.data()}, plain{};
    if (!CryptUnprotectData(&encrypted, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain)) return {};
    std::string result(reinterpret_cast<const char*>(plain.pbData), plain.cbData);
    SecureZeroMemory(plain.pbData, plain.cbData); LocalFree(plain.pbData);
    return result;
}
inline bool write(const std::filesystem::path& file, std::string_view text, const wchar_t* description) {
    if (text.size() > MAXDWORD) return false;
    DATA_BLOB plain{static_cast<DWORD>(text.size()), reinterpret_cast<BYTE*>(const_cast<char*>(text.data()))}, encrypted{};
    if (!CryptProtectData(&plain, description, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted)) return false;
    const bool saved = atomic_file::writeDurable(file, std::span<const uint8_t>(encrypted.pbData, encrypted.cbData)); // Flushed before the rename.
    LocalFree(encrypted.pbData); return saved;
}
#endif
}
