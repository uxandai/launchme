#pragma once
#include "cover_cache.hpp"
#include "steamgriddb_search.hpp"
#include <string_view>

struct GridAsset {
    uint32_t id = 0;
    std::wstring url, thumbnail, author;
    int width = 0, height = 0;
    std::wstring preview; // A video wallpaper's short moving preview (MP4), played before it is downloaded.
};
// "Zmień grafikę" galleries: SteamGridDB, or for emulated shortcuts the media ScreenScraper
// lists for the identified ROM (artwork/screenscraper_art.hpp) and ES-DE's downloaded media; the
// stores and databases of artwork_sources.hpp; and the wallpaper sources of wallpaper_sources.hpp
// (pictures: Wallhaven, Danbooru; videos: MotionBGs, Steam's animated profile backgrounds).
enum class ArtworkProvider { steamgriddb, screenscraper, esde, steam, gog, xbox, nintendo, gametdb, titledb, thegamesdb, wikimedia,
                             wallhaven, danbooru, motionbgs, steam_backgrounds };
struct GridPage {
    std::vector<GridAsset> assets;
    GridSource source;
    bool more = false;
    std::wstring error;
    ArtworkProvider provider = ArtworkProvider::steamgriddb;
};
constexpr int grid_page_size = 24;
GridPage parseGridPage(std::string_view bytes);
GridPage browseSteamGridDB(const Game& game, ArtworkKind kind, int page, GridSource source,
                           const std::wstring& key, const ArtworkHttp& get = httpGet);
bool decodableArtwork(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> downloadGridImage(const GridAsset& asset, bool thumbnail, const ArtworkHttp& get = httpGet);
