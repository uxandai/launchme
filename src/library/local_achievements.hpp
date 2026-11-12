#pragma once

#include "steam_achievements.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Local achievements of DRM-free games: the unlocks a game keeps in files on this computer, next
// to it or in a shared save folder, with no store account behind them. The game folder gives the
// game's Steam AppID (steam_appid.txt, or RealAppId= in an .ini beside the game) and where the saves
// are. Two save formats:
// - JSON: {"ACH_NAME": {"earned": true, "earned_time": 1747279005}, ...} (achievements.json), in
//   the game folder (local_save.txt, local_save_path=) or under %APPDATA%\GSE Saves\<AppID>\.
// - INI: [ACH_NAME] achieved=true timestamp=1747279005 (Stats\Achievements.ini), in the game
//   folder or under Public Documents\OnlineFix\<AppID>\.
// Names come from the game's own schema (steam_settings\achievements.json, icons beside it in
// achievement_images\ or img\), else the Steam client's (readSteamSchema), else the Web API with the
// user's key; the caller lays the unlocks over the schema it has (overlay).
namespace local_achievements {
enum class Format : uint8_t { none, json, ini };

// The shared save folders' parents: Public Documents and the roaming AppData folder.
struct Places {
    std::filesystem::path public_documents, roaming_app_data;
};
Places systemPlaces(); // This computer's (Windows); empty paths elsewhere.

struct Found {
    Format format = Format::none;
    uint32_t app_id = 0;
    std::filesystem::path state;          // The unlocks file; empty while the game has saved none yet.
    std::filesystem::path steam_settings; // The folder holding the game's schema; empty: none.
    explicit operator bool() const { return format != Format::none && (app_id || !state.empty()); }
};
// Looks through the game folder (a few levels, a bounded number of entries) and the shared save
// folders. Nothing when the folder shows no local achievements.
Found find(const std::filesystem::path& game_folder, const Places& places);

struct Unlock {
    std::string id;
    bool achieved = false;
    uint64_t time = 0; // Unix seconds; 0 unknown.
};
std::vector<Unlock> parseJson(std::string_view json);
std::vector<Unlock> parseIni(std::string_view ini);
std::vector<Unlock> readUnlocks(const Found& found);

// The game's steam_settings\achievements.json: every achievement locked; icons are pictures on this
// computer (achievement_images\, img\ or the path the schema names), the grey one in gray_icon.
SteamAchievements parseSchema(std::string_view json, const std::filesystem::path& steam_settings, bool english);
SteamAchievements readSchema(const Found& found, bool english);

// The unlocks laid over a schema (any source). Without a schema each unlock is an item named after
// its id; the list is complete only when the saves list locked ones too (INI saves keep earned only).
SteamAchievements overlay(SteamAchievements schema, const std::vector<Unlock>& unlocks);
// "ACH_COLLECT_HATS" / "ach_collect_hats" → "Collect hats"; "NEW_ACHIEVEMENT_1_0" stays readable.
std::wstring readableId(std::string_view id);

// The game's schema, else the Steam client's, with the unlocks laid over (no network).
// `named`: a schema gave the names; otherwise they are the ids (the Web API may name them).
struct Local {
    SteamAchievements list;
    bool named = false;
};
Local localList(const Found& found, const std::filesystem::path& steam_root, bool english);
// The public shares of players: applied, and the achievements a list without a schema lacks are
// added locked (every id Steam knows), so the total is right.
void applyShares(SteamAchievements& list, const std::vector<std::pair<std::string, float>>& percentages);

// Where a Steam achievement icon may be: its schema URL, then Steam's other image hosts and paths
// (newer icons are only under community_assets). Empty for a URL that is no Steam image.
std::vector<std::wstring> iconUrls(const std::wstring& url, uint32_t app_id);
// A game with a steam_settings folder but no schema of its own: the Web API's schema (English and,
// when given, Polish names) written there as achievements.json, its icons downloaded into img\ —
// what the game itself needs to unlock them. Nothing is replaced: false when the folder already
// has a schema, cannot be written, or `stop` asked to end (icons already saved stay for next time).
bool saveSchema(const Found& found, const SteamAchievements& english, const SteamAchievements& polish,
                const SteamHttp& get, const std::function<bool()>& stop = {});
} // namespace local_achievements
