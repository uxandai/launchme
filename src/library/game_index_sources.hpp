#pragma once
// What each source the launcher already has says about a game, as game_index patches. Every
// adapter reads data the launcher fetched or found anyway (Steam's appinfo cache, the cached
// store pages, RAWG answers, review scores, HowLongToBeat matches, ES-DE/ScreenScraper,
// GOG/Epic product data); nothing here touches the network.
#include "game_index.hpp"
#include "steam_appinfo.hpp"
#include "../model/game_details.hpp"
#include "../model/game_rating.hpp"

#include <filesystem>
#include <string_view>
#include <unordered_map>

namespace game_index {
// Steam's store tags by ID (store.steampowered.com/tagdata/populartags/english).
using TagNames = std::unordered_map<uint32_t, std::wstring>;
TagNames parseSteamTags(std::string_view json);

Patch fromAppInfo(const steam_appinfo::App& app, const TagNames& tags);
// A cached store page (api/appdetails, either language): description, studios, genres (by ID,
// so always English), release date, Metacritic and the number of user reviews.
Patch fromSteamStore(uint32_t app_id, std::string_view json);
// A cached RAWG game (rawg.cpp's {"launchme_cache":1,…} wrapper or the bare answer).
Patch fromRawg(std::string_view json);
Patch fromRating(const ratings::Rating& rating);
// The HowLongToBeat plugin's cache file of one game: the chosen match's times (seconds → minutes).
Patch fromHltb(std::string_view json, std::wstring_view title);
// Emulated games: what ES-DE or ScreenScraper says (GameDetails), plus their name for it.
Patch fromDetails(const GameDetails& details, std::wstring_view name = {});
// GOG's api.gog.com/products/<id>?expand=description and Epic's catalog item, as the launcher
// keeps them (gog_api.cpp / epic_api.cpp).
Patch fromGogProduct(std::string_view json);
Patch fromEpicCatalog(std::string_view json);
// The Nintendo eShop match the index chose for a game (knowledge/nintendo/<cache key>.json, written by the
// service): {"title", "description", "publisher", "release" ("2026-06-25"), "genres" ("A, B")}.
Patch fromNintendo(std::string_view json);

// The plain text of a store description: tags, entities and runs of space removed; ≤ 400 chars
// on a word boundary.
std::wstring plainDescription(std::string_view html, size_t limit = 400);

// Where the launcher keeps its caches (<config>/…) and what identifies one game in them.
struct CacheFolders {
    std::filesystem::path metadata, ratings, hltb, products;
};
struct CachedGame {
    std::string key;        // gameKey()
    std::string cache_key;  // gameCacheKey() (hex of the key)
    std::wstring title;
    uint32_t app_id = 0;    // Steam only.
};
// Every cached description of one game, newest store page language first.
std::vector<std::pair<Source, Patch>> readCaches(const CacheFolders& folders, const CachedGame& game, bool english);
}
