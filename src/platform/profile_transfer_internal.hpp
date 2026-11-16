#pragma once
// Shared by profile_transfer.cpp and profile_transfer_apply.cpp only.
#include "profile_transfer.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace profile_transfer::transfer_detail {
// A secret file of the backup (secure.bin): `protect` = it was this user's protected file and is
// protected again on import; else written as it was.
struct SecureFile {
    Part part = Part::keys;
    std::filesystem::path relative;
    bool protect = false;
    std::string data;
};
std::string entryPath(const std::filesystem::path& relative);
std::filesystem::path fromEntry(std::string_view name);
std::optional<std::string> readFile(const std::filesystem::path& file, uint64_t limit);
bool regularFile(const std::filesystem::path& path);
std::string systemName();
std::vector<std::filesystem::path> collect(const std::filesystem::path& config, Part part);
bool wholeSecret(Part part);
bool maybeProtected(const std::filesystem::path& relative);
std::string packSecure(const std::vector<SecureFile>& files);
std::optional<std::vector<SecureFile>> unpackSecure(std::string_view bytes);
}
