#pragma once

#include "../artwork/steamgriddb_search.hpp"
#include "../model/game.hpp"
#include "steam_trailer.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

// Nintendo Switch and Switch 2 store trailers (nintendo.com's US catalog on Algolia + Cloudinary MP4
// clips on assets.nintendo.com): 30 s from second 10, 480p H.264 without sound, kept in
// <folder>\nintendo\videos\<cache_key>.clip.mp4 under the shared trailer_space_mb limit.

inline bool nintendoSwitchGame(const Game& game) {
    return isGame(game) && (game.platform == L"Switch" || game.platform == L"Switch 2");
}

// The Cloudinary video publicId ("store/software/switch2/.../Video/...") matched in an Algolia
// store_game_en_us answer for `title`, preferring `switch2` when both consoles have the game; empty
// for no or an ambiguous match, DLC, bundles or upgrade packs.
std::string matchNintendoTrailerId(std::wstring_view title, bool switch2, std::string_view algolia_json);

// Cached in <metadata>\<cache_key>.nintendotrailer: a found publicId for good, no match / no video
// for a week, a failed request not at all. Resolves a 16-hex Switch / Switch 2 title ID through the
// European eShop when the game's own name finds nothing.
std::string nintendoTrailerIdForGame(const Game& game, const std::filesystem::path& metadata,
                                     const ArtworkHttp& get = httpGet);

std::wstring nintendoTrailerClipUrl(std::string_view public_id, bool skip_intro = true);

std::filesystem::path nintendoTrailerFile(const Game& game, const std::filesystem::path& config_root,
                                          const std::filesystem::path& folder,
                                          uint64_t limit_bytes = 250ull * 1024 * 1024,
                                          const ArtworkHttp& get = httpGet,
                                          const TrailerDownload& download = {});
