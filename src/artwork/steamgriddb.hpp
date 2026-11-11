#pragma once
#include "cover_cache.hpp"
#include "steamgriddb_search.hpp"
#include <span>
#include <string_view>

struct GridAsset {
    uint32_t id = 0;
    std::wstring url, thumbnail, author;
    int width = 0, height = 0;
    std::wstring preview; // A video wallpaper's short moving preview (MP4), played before it is downloaded.
    bool animated = false; // SteamGridDB's animated grid or hero: `url` is an animated WebP (animated_image.hpp).
};
// "Zmień grafikę" galleries: SteamGridDB, or for emulated shortcuts the media ScreenScraper
// lists for the identified ROM (artwork/screenscraper_art.hpp) and ES-DE's downloaded media; the
// stores and databases of artwork_sources.hpp; and the wallpaper sources of wallpaper_sources.hpp
// (pictures: Wallhaven, Danbooru; videos: MotionBGs, Steam's animated profile backgrounds, the player's
// Wallpaper Engine video subscriptions).
enum class ArtworkProvider { steamgriddb, screenscraper, esde, steam, gog, xbox, nintendo, gametdb, titledb, thegamesdb, wikimedia,
                             wallhaven, danbooru, motionbgs, steam_backgrounds, wallpaper_engine,
                             local }; // local: the files the player added from disk (Zmień grafikę → Z dysku…)
struct GridPage {
    std::vector<GridAsset> assets;
    GridSource source;
    bool more = false;
    std::wstring error;
    ArtworkProvider provider = ArtworkProvider::steamgriddb;
    int total = -1; // SteamGridDB's count of every asset the query matches (-1: not given).
};
constexpr int grid_page_size = 24;
GridPage parseGridPage(std::string_view bytes);
GridPage browseSteamGridDB(const Game& game, ArtworkKind kind, int page, GridSource source,
                           const std::wstring& key, const ArtworkHttp& get = httpGet);
// Zmień grafikę's gallery: the animated grids or heroes first (GridAsset::animated; Windows only, see
// animated_image::supported), then the static ones; a page holds the animated ones from page * 24
// on, the static ones filling it. Automatic artwork keeps browseSteamGridDB (static only).
GridPage browseSteamGridDBGallery(const Game& game, ArtworkKind kind, int page, GridSource source,
                                  const std::wstring& key, const ArtworkHttp& get = httpGet);
bool decodableArtwork(const std::vector<uint8_t>& bytes);
std::vector<uint8_t> downloadGridImage(const GridAsset& asset, bool thumbnail, const ArtworkHttp& get = httpGet);
// An animated asset's start (a few seconds, animated_preview_bytes), cut to whole frames: its moving
// tile in the gallery. `get` is asked for the first max_bytes of the URL (httpGetPrefix).
constexpr size_t animated_preview_bytes = 3 * 1024 * 1024;
std::vector<uint8_t> downloadAnimatedPreview(const GridAsset& asset, const ArtworkHttp& get = {});
// The whole animated asset into `file` (a cut-off download resumes), kept only when it is an animated
// WebP whose first frame decodes. `download` (url, headers, file, max_bytes) defaults to httpDownloadFile.
using ArtworkFileHttp = std::function<HttpResponse(const std::wstring&, const std::wstring&, const std::filesystem::path&, size_t)>;
constexpr size_t animated_artwork_limit = 64 * 1024 * 1024;
bool downloadAnimatedGrid(const GridAsset& asset, const std::filesystem::path& file, const ArtworkFileHttp& download = {});
// An animation's first frame as a still for the caches and the views until it moves: JPEG, or PNG
// when it has transparency. Empty when it does not decode.
std::vector<uint8_t> animatedStill(const std::filesystem::path& file);
std::vector<uint8_t> animatedStill(std::span<const uint8_t> bytes);
