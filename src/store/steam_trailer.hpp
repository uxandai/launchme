#pragma once

#include "../net/http.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Steam store trailers behind Steam games (AppSettings::steam_trailers, src/app/game_video.cpp):
// the store page's first trailer (its highlight first), downloaded once to
// <folder>\steam\videos\<AppID>.mp4. Older pages give an MP4 (480p); current ones only DASH/HLS,
// so the video track of the DASH stream (at most 480 lines) is joined into one fragmented MP4,
// without sound: trailers play silently over the launcher's music. The appdetails answer comes
// from the store details cache.
using TrailerDownload = std::function<HttpResponse(const std::wstring& url, const std::filesystem::path& file)>;
std::wstring steamTrailerUrl(uint32_t app_id, std::string_view appdetails);     // MP4, or empty.
std::wstring steamTrailerDashUrl(uint32_t app_id, std::string_view appdetails); // dash_h264 MPD, or empty.
// The video representation's init segment, then the media segments of a clip: from start_seconds
// for at most clip_seconds (shorter trailers: their last clip_seconds), against the MPD's URL.
std::vector<std::wstring> dashVideoSegments(std::string_view mpd, std::wstring_view mpd_url, int max_height = 480,
                                            double start_seconds = 10, double clip_seconds = 30);
// A media segment for the joined file: only its moof/mdat boxes (no styp/sidx), its decode time
// moved so the clip starts at 0 (origin: the first segment's time, set by the first call).
std::vector<uint8_t> rebaseDashSegment(std::span<const uint8_t> segment, std::optional<uint64_t>& origin);
// Oldest-played trailers go until the folders together fit limit_bytes (played ones are touched).
void trimTrailers(std::span<const std::filesystem::path> folders, uint64_t limit_bytes);
void trimTrailers(const std::filesystem::path& videos, uint64_t limit_bytes);
// Shared gate so browsing past several games downloads at most one store trailer at a time.
bool tryBeginTrailerDownload();
void endTrailerDownload();
// <folder>\steam\videos\<AppID>.clip.mp4; trailer folders are trimmed to limit_bytes after a download.
std::filesystem::path steamTrailerFile(uint32_t app_id, const std::filesystem::path& config_root, const std::filesystem::path& folder,
                                       uint64_t limit_bytes = 250ull * 1024 * 1024, const TrailerDownload& download = {});
