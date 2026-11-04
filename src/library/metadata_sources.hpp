#pragma once
// "Znajdź" in Edytuj metadane: the text details of a game (description, developer, publisher, release
// date, genres) as the stores and databases give them, to pick from. Each source answers a search by
// the game's title (a Switch title ID in its file name, a Steam / GOG / Microsoft Store id, when known,
// goes first), with the games it found, best match first. Nothing here is saved: the window copies the
// chosen fields into Edytuj metadane, and its Zapisz makes them the player's own.
//   nintendo  the Nintendo eShop (Europe) search: the only one that knows Switch 2 games (by their ID);
//             a short description, publisher, release date and category;
//   steam     the store search and appdetails of each match;
//   gog       catalog.gog.com and the game's product API (the description);
//   xbox      the Microsoft Store display catalog;
//   titledb   TitleDB (api.nlib.cc) of a Switch title ID in the file name.
#include "../artwork/steamgriddb_search.hpp"
#include "../model/game_details.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace metadata_sources {
enum class Source { nintendo, steam, gog, xbox, titledb };
inline constexpr std::array<Source, 5> all{Source::nintendo, Source::steam, Source::gog, Source::xbox, Source::titledb};
const wchar_t* name(Source source);

struct Offer {
    Source source = Source::nintendo;
    std::wstring title;
    std::string id;     // The source's own id of the game (AppID, GOG product, Store product, title ID).
    std::wstring note;  // The edition's console or store, as the list shows it ("Nintendo Switch 2").
    GameDetails details; // description, developer, publisher, release_date ("2026-06-25"), genres ("A, B").
    int score = 0;       // How well the title matches the query: higher is better (an ID match counts most).
};

struct Query {
    std::wstring title;            // What to look for.
    std::string switch_id;         // A Switch / Switch 2 title ID (16 hex, upper case) from the game's file name.
    uint32_t steam_app = 0;        // The game's Steam AppID when known.
    std::string gog_id;            // A GOG game's product id.
    std::string xbox_product;      // A Microsoft Store product id.
    bool polish = false;           // Steam, GOG and the Microsoft Store answer in Polish when they can.
};
struct Found {
    std::vector<Offer> offers;     // At most a few per source, best first.
    std::wstring error;            // Why there is none ("Sklep nie zna tej gry."), else empty.
};

// Whether the source can know this game at all (TitleDB: only with a Switch title ID).
bool applicable(Source source, const Query& query);
Found find(Source source, const Query& query, const ArtworkHttp& get = httpGet);

// The first 16-hex Switch title ID in `text` (01… Switch, 04… Switch 2), its base game's ("…000").
std::string titleId(std::wstring_view text);

// Parsers, exposed for tests.
std::wstring plainText(std::string_view html);
std::wstring isoDate(std::string_view text); // "25/06/2026", "2016.08.30", "2021-11-09T05:00:00Z", "24 Feb, 2022" → "2026-06-25"; "" if unknown.
std::vector<Offer> parseNintendo(std::string_view body, const Query& query);
std::vector<Offer> parseSteamSearch(std::string_view body, const Query& query); // Offers with a title only: appdetails fill them.
std::optional<Offer> parseSteamDetails(std::string_view body, uint32_t app);
std::vector<Offer> parseGogCatalog(std::string_view body, const Query& query);
std::wstring parseGogDescription(std::string_view body);
std::vector<std::pair<std::string, std::wstring>> parseXboxSuggest(std::string_view body); // (product id, title)
std::vector<Offer> parseXboxProducts(std::string_view body, const Query& query);
std::optional<Offer> parseTitleDb(std::string_view body);
} // namespace metadata_sources
