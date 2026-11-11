#pragma once
#include "steamgriddb.hpp"

#include <filesystem>
#include <string>

// Interfejs → Tapeta: online wallpapers, none of them needing a key.
//   wallhaven          pictures from wallhaven.cc's API, SFW purity only (general and anime, no people);
//   danbooru           pictures from danbooru.donmai.us tagged rating:general, landscape ones only;
//   motionbgs          live wallpapers from motionbgs.com (an HD MP4), with its 960×540 moving preview;
//   steam_backgrounds  Steam Points Shop animated profile backgrounds (MP4), with their small preview;
//   wallpaper_engine   the video wallpapers subscribed in Wallpaper Engine (Steam Workshop, app 431960)
//                      and its own projects, on this PC: played from where they are, never copied.
// Pictures are kept scaled to cover 2560×1440 (JPEG); videos as they come, in <config>\wallpapers.
namespace wallpaper_sources {
bool own(ArtworkProvider provider);
bool video(ArtworkProvider provider);
// An empty query: the source's best or newest (Wallhaven's toplist, Danbooru's scenery, MotionBGs' 4K list,
// every Steam background), as the picker opens before anything is typed.
GridPage browse(ArtworkProvider provider, const std::wstring& query, int page, const ArtworkHttp& get = httpGet);
// Wallpaper Engine (Video) in these Steam library folders (steamLibraryFolders): items of type "video"
// only (scenes and web pages need Wallpaper Engine), none rated Mature, by title, 24 a page.
GridPage wallpaperEngine(const std::wstring& query, int page, const std::vector<std::filesystem::path>& libraries);
std::vector<uint8_t> thumbnail(const GridAsset& asset, const ArtworkHttp& get = httpGet);
// The chosen wallpaper downloaded into `folder`: the stored file, or empty with `error` set.
std::filesystem::path store(ArtworkProvider provider, const GridAsset& asset, const std::filesystem::path& folder, std::wstring& error);
// A picture or video from disk copied into `folder` the same way.
std::filesystem::path storeLocal(const std::filesystem::path& source, const std::filesystem::path& folder, bool& is_video, std::wstring& error);
// A video wallpaper's moving preview, downloaded once into <folder>\previews.
std::filesystem::path preview(const GridAsset& asset, const std::filesystem::path& folder);
// MotionBGs: the preview and the HD download of an entry, from its thumbnail address.
std::wstring motionbgsPreview(std::wstring_view thumbnail);
}
