#pragma once

#include "game_ratings.hpp"
#include "../model/game_details.hpp"

namespace rawg {
struct Candidate {
    uint32_t id = 0;
    std::wstring name, released, platforms;
    std::wstring image; // background_image: a screenshot of the game, for the list.
};
struct Search { std::vector<Candidate> games; std::wstring error; };
struct Metadata {
    uint32_t id = 0;
    std::wstring name;
    GameDetails details;
    ratings::Rating rating; // Also provides the RAWG source link when the game has no votes.
    uint32_t steam_app_id = 0; // Artwork lookup only; never replaces the local game's identity/actions.
};
struct Details { std::optional<Metadata> data; std::string json; std::wstring error; };
using Get = std::function<HttpResponse(const std::wstring&, const std::wstring&, size_t)>;
Search parseSearch(std::string_view body);
std::optional<Metadata> parseDetails(std::string_view body);
uint32_t steamAppIdFromUrl(std::string_view url);
struct StoreLinks { bool valid = false; uint32_t steam_app_id = 0; std::wstring error; };
StoreLinks parseStoreLinks(uint32_t rawg_id, std::string_view body);
StoreLinks stores(uint32_t rawg_id, const std::wstring& key, const Get& get = httpGetPrivate);
Search search(const std::wstring& query, const std::wstring& key, const Get& get = httpGetPrivate);
Details fetch(uint32_t id, const std::wstring& key, const Get& get = httpGetPrivate);
std::optional<Metadata> load(const Game& game, const std::filesystem::path& cache);
bool save(const Game& game, const Details& details, const std::filesystem::path& cache);
using Map = std::unordered_map<std::string, Metadata>;
}
