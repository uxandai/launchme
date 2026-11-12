#pragma once

#include "binary_kv.hpp" // Steam's binary KeyValues (appcache/stats): exposed for tests.
#include "../net/http.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Achievements of one Steam game for the signed-in account (Szczegóły → Osiągnięcia).
struct SteamAchievement {
    std::string id;                 // API name.
    std::wstring name, description;
    std::wstring icon;              // https URL: colour icon when unlocked, grey one (when known) while locked.
    bool achieved = false, hidden = false;
    uint64_t unlocked_at = 0;       // Unix time; 0 while locked.
    float global_percent = -1;      // Share of all players who have it; -1 unknown.
    std::wstring gray_icon;         // Schemas only: the locked icon (empty: unknown, or `icon` is already it).
};
struct SteamAchievements {
    std::vector<SteamAchievement> items;
    uint32_t unlocked = 0, total = 0;
    bool complete = false;          // Every achievement listed; otherwise the client's cached part.
};
using SteamHttp = std::function<HttpResponse(const std::wstring&, const std::wstring&, size_t)>;

// The Steam client's own achievement files (appcache/stats, no network): the game's schema
// (UserGameStatsSchema_<AppID>.bin: every achievement, names in all languages, icons) and the
// account's state (UserGameStats_<account>_<AppID>.bin: unlock bits and times). Global shares
// come from the game page cache where it has them. Empty when the client has no schema.
SteamAchievements readLocalAchievements(const std::filesystem::path& steam_root, uint64_t steam_id, uint32_t app_id, bool english);
// The client's schema alone (every achievement locked, no shares): the names, descriptions and
// icons of a game whose unlocks come from elsewhere (local_achievements.hpp).
SteamAchievements readSteamSchema(const std::filesystem::path& steam_root, uint32_t app_id, bool english);
// Only the counts of readLocalAchievements: {unlocked, total}, total 0 without a schema.
std::pair<uint32_t, uint32_t> localAchievementCounts(const std::filesystem::path& steam_root, uint64_t steam_id, uint32_t app_id);

// The Steam client's game page cache (userdata/<id>/config/librarycache/<AppID>.json): the counts
// and the achievements it keeps there (the unlocked ones and some of the rest). No network.
SteamAchievements readCachedAchievements(const std::filesystem::path& steam_root, uint64_t steam_id, uint32_t app_id);

// Public share of players per achievement (Steam Web API, no key); empty on failure.
std::vector<std::pair<std::string, float>> fetchGlobalPercentages(uint32_t app_id, const SteamHttp& get = httpGet);
void applyGlobalPercentages(SteamAchievements& list, const std::vector<std::pair<std::string, float>>& percentages);

// Steam Web API with the user's key: the game's schema (names, descriptions, icons in the UI
// language), the account's unlocks and the global percentages. Empty items on any failure
// (no key, private game details, no network); the caller keeps the local list then.
SteamAchievements fetchSteamAchievements(const std::wstring& key, uint64_t steam_id, uint32_t app_id,
                                         bool english, const SteamHttp& get = httpGetPrivate);

// GetSchemaForGame alone with the user's key (no account state): every achievement locked, the
// colour icon in `icon` and the grey one in `gray_icon`. Empty on any failure.
SteamAchievements fetchSteamSchema(const std::wstring& key, uint32_t app_id, bool english, const SteamHttp& get = httpGetPrivate);

// Unlocked first (newest first), then locked (most common first), hidden locked ones last.
void sortAchievements(std::vector<SteamAchievement>& items);

// Icon file name inside the achievements cache folder, taken from the URL's last segment
// (Steam uses hex hashes); empty when the URL does not look like a Steam image.
std::wstring achievementIconFile(const std::wstring& url);
