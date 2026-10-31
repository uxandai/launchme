#pragma once
#include "steamgriddb.hpp"

#include <filesystem>
#include <string>

// Interfejs → Tapeta: online wallpapers, none of them needing a key.
//   wallhaven          pictures from wallhaven.cc's API, SFW purity only (general and anime, no people);
//   danbooru           pictures from danbooru.donmai.us tagged rating:general, landscape ones only;
//   motionbgs          live wallpapers from motionbgs.com (an HD MP4), with its 960×540 moving preview;
//   steam_backgrounds  Steam Points Shop animated profile backgrounds (MP4), with their small preview.
// Pictures are kept scaled to cover 2560×1440 (JPEG); videos as they come, in <config>\wallpapers.
namespace wallpaper_sources {
bool own(ArtworkProvider provider);
bool video(ArtworkProvider provider);
// What a first visit shows before anything is typed.
std::wstring defaultQuery(ArtworkProvider provider);
GridPage browse(ArtworkProvider provider, const std::wstring& query, int page, const ArtworkHttp& get = httpGet);
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
