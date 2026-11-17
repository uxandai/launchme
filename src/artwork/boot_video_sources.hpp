#pragma once
#include "steamgriddb.hpp"

#include <filesystem>
#include <string>
#include <vector>

// Interfejs → Start → Wideo startowe: community startup and suspend animations from SteamDeckRepo
// (steamdeckrepo.com/api/posts, no key needed) and the player's own files in <config>\boot_videos,
// modelled on SDH-AnimationChanger. Downloaded videos stay in <config>\boot_videos so they can be
// switched offline in "Pobrane" or randomized on startup.
namespace boot_video_sources {
// Rail categories: 0 Popularne (most liked boot videos), 1 Na czasie (trending boot videos),
// 2 Krótkie (suspend animations, 2-4 s), 3 Pobrane (files stored in `folder`).
GridPage browse(int category, const std::wstring& query, int page, const std::filesystem::path& folder,
                const ArtworkHttp& get = httpGet);
std::vector<uint8_t> thumbnail(const GridAsset& asset, const ArtworkHttp& get = httpGet);
// Downloads `asset` into `folder` (or resolves an entry from Pobrane); sets `title` to its clean name.
std::filesystem::path store(const GridAsset& asset, const std::filesystem::path& folder, std::wstring& title, std::wstring& error);
// Copies a local .webm / .mp4 / .mkv / .mov video into `folder`; sets `asset_id` and `title`.
std::filesystem::path storeLocal(const std::filesystem::path& source, const std::filesystem::path& folder,
                                 uint32_t& asset_id, std::wstring& title, std::wstring& error);
// Moving preview of the focused entry (with audio), cached in <folder>\previews.
std::filesystem::path preview(const GridAsset& asset, const std::filesystem::path& folder);
// Every stored boot video in `folder` (excluding previews\), sorted for deterministic listing or random pick.
struct StoredVideo {
    std::filesystem::path file;
    uint32_t id = 0;
    std::wstring title;
};
std::vector<StoredVideo> storedVideos(const std::filesystem::path& folder);
}
