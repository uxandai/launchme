#pragma once
// The profile's background read (src/app/profile.cpp): the Steam account's picture from the
// client's avatar cache, the achievements Steam's own files say were unlocked (only games whose
// stats file changed since the last read) and the icons of the latest unlocks (cached under
// achievements\, fetched once). The UI tests answer it themselves.
#include "../library/achievement_history.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct ProfileScanRequest {
    std::filesystem::path steam_root, icon_cache;
    uint64_t steam_id = 0;
    bool english = false;
    std::vector<std::pair<std::string, uint32_t>> steam_games; // The library's Steam games: key, AppID.
    std::map<std::string, std::wstring> names;                  // Game key → name, for the history.
    std::map<std::string, uint64_t> scanned;                    // achievement_history::History::scanned.
    // The icons wanted: URL → the achievements\ subfolder its game's icons are kept in.
    std::vector<std::pair<std::wstring, std::wstring>> icons;
};
struct ProfileGameUnlocks {
    std::string key;
    std::wstring name;
    std::vector<achievement_history::Unlock> unlocks;
    achievement_history::Progress progress;
};
struct ProfileScanResult {
    std::vector<uint8_t> avatar;
    std::vector<ProfileGameUnlocks> games;
    std::map<std::string, uint64_t> scanned;
    std::map<std::wstring, std::vector<uint8_t>> icons; // URL → picture.
};
using ProfileReader = std::function<ProfileScanResult(const ProfileScanRequest&, const std::function<bool()>& cancelled)>;
std::vector<uint8_t> readSteamAvatar(const std::filesystem::path& steam_root, uint64_t steam_id);
ProfileScanResult readProfile(const ProfileScanRequest& request, const std::function<bool()>& cancelled);
