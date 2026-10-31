#pragma once
#include "steamgriddb.hpp"

#include <array>
#include <filesystem>
#include <string>

// "Zmień grafikę": the galleries beside SteamGridDB. Every store and database below is public;
// TheGamesDB alone needs a key (thegamesdb_api_key.txt), like SteamGridDB and ScreenScraper:
//   steam      the store's library art and screenshots of the game's Steam AppID (known, or found by title);
//   gog        catalog.gog.com art (covers, logo, background, screenshots) of the game found by title or its GOG id;
//   xbox       the Microsoft Store display catalog (box art, poster, hero, screenshots) by title or Store id;
//   nintendo   the Nintendo eShop (Europe) search: square and wide key art;
//   gametdb    GameTDB's covers of Wii, GameCube, Wii U, Switch, DS and 3DS games (ID from the disc image or title);
//   titledb    TitleDB (api.nlib.cc) icon, banner and screenshots of a Switch title id in the ROM's name;
//   thegamesdb TheGamesDB box art, fanart, banners, screenshots and clear logos;
//   wikimedia  Wikimedia Commons pictures found by title (free licences).
// Variant IDs live in 0x60000000-0x7FFFFFFF: three bits name the source, the rest hash the URL.
namespace artwork_sources {
inline constexpr std::array<ArtworkProvider, 11> all{
    ArtworkProvider::steamgriddb, ArtworkProvider::steam, ArtworkProvider::gog, ArtworkProvider::xbox, ArtworkProvider::nintendo,
    ArtworkProvider::gametdb, ArtworkProvider::titledb, ArtworkProvider::thegamesdb, ArtworkProvider::wikimedia,
    ArtworkProvider::screenscraper, ArtworkProvider::esde};
// The settings key (AppSettings::artwork_sources_off), the shown name, a short description (a tr key).
const char* slug(ArtworkProvider provider);
const wchar_t* name(ArtworkProvider provider);
const wchar_t* description(ArtworkProvider provider);
bool needsKey(ArtworkProvider provider);
// Whether the source can have art of this game (ScreenScraper and ES-DE: emulated games; GameTDB:
// Nintendo consoles; TitleDB: Switch; the PC stores: games that are not emulated).
bool applicable(ArtworkProvider provider, const Game& game);
// One of the eight sources above (not SteamGridDB, ScreenScraper, ES-DE or a wallpaper source).
bool own(ArtworkProvider provider);

struct Context {
    std::filesystem::path config;   // userConfigDirectory(): title lists and lookups are cached under it.
    uint32_t steam_app_id = 0;      // The game's Steam AppID when known (0: found by its title).
    std::wstring title;             // What to look for (the search field; the game's name by default).
    std::wstring thegamesdb_key;
};
GridPage browse(ArtworkProvider provider, const Game& game, ArtworkKind kind, const Context& context, const ArtworkHttp& get = httpGet);
// The thumbnail (falling back to the full picture) or the full picture (falling back to the thumbnail).
std::vector<uint8_t> download(ArtworkProvider provider, const GridAsset& asset, bool thumbnail, const ArtworkHttp& get = httpGet);

// Parsers, exposed for tests.
std::string switchTitleId(std::wstring_view text);       // The first 16-hex "01..." id in a file name, its base title (…000).
std::string wiiDiscId(const std::filesystem::path& rom); // The six-character ID of a GameCube/Wii image (ISO, GCM, WBFS).
}
