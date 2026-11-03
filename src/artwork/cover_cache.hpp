#pragma once

#include "../model/game.hpp"
#include "steamgriddb_search.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

std::filesystem::path coverCacheDirectory();
// poster: a 600x900 vertical grid (screensaver boxes, Flix rows); landscape: a 920x430 /
// 460x215 horizontal grid (Flix "recently played" row). Neither is user-selected.
enum class ArtworkKind { cover, logo, hero, poster, landscape, disc };
const wchar_t* artworkKindSlug(ArtworkKind kind);
std::filesystem::path artworkVariantPath(const Game& game, ArtworkKind kind, uint32_t asset_id,
                                         const std::filesystem::path& cache);
uint32_t selectedArtworkId(const Game& game, ArtworkKind kind, const std::filesystem::path& cache);
// Confirmed artwork identity (e.g. RAWG's Steam link), independent of local store/game IDs.
GridSource selectedArtworkSource(const Game& game, const std::filesystem::path& cache);
bool selectArtworkSource(const Game& game, GridSource source, const std::filesystem::path& cache);
std::vector<uint8_t> selectedArtwork(const Game& game, ArtworkKind kind, const std::filesystem::path& cache);
bool cacheArtworkVariant(const Game& game, ArtworkKind kind, uint32_t asset_id,
                         const std::vector<uint8_t>& bytes, const std::filesystem::path& cache);
bool selectArtworkVariant(const Game& game, ArtworkKind kind, uint32_t asset_id,
                          const std::filesystem::path& cache);
// The files the player added from disk (variant IDs from 0x80000000), newest first; whether the game has any
// of any type; and whether a variant of the type is the one chosen (the "set" dots of Zmień grafikę).
inline constexpr uint32_t local_artwork_first_id = 0x80000000u;
std::vector<uint32_t> localArtworkIds(const Game& game, ArtworkKind kind, const std::filesystem::path& cache);
bool hasLocalArtwork(const Game& game, const std::filesystem::path& cache);
// Downloaded artwork of a non-Steam game (and with `selections`, the user's picks), so a
// renamed game is matched again and a removed one leaves nothing behind.
bool removeGameArtwork(const Game& game, const std::filesystem::path& cache, bool selections);
std::filesystem::path gameArtworkDirectory(const Game& game, const std::filesystem::path& cache);
std::filesystem::path gameCoverPath(const Game& game, const std::filesystem::path& cache);
std::filesystem::path gameArtworkPath(const Game& game, ArtworkKind kind, const std::filesystem::path& cache);
bool cachedArtworkAvailable(const Game& game, ArtworkKind kind, const std::filesystem::path& cache, bool rom_first = true);
// The valid image file already on disk (empty path when none) and its bytes. Never downloads.
std::filesystem::path cachedArtworkFile(const Game& game, ArtworkKind kind, const std::filesystem::path& cache, bool rom_first = true);
std::vector<uint8_t> cachedArtwork(const Game& game, ArtworkKind kind, const std::filesystem::path& cache, bool rom_first = true);
std::vector<uint8_t> artworkForGame(const Game& game, ArtworkKind kind,
                                  const std::filesystem::path& cache,
                                  const std::filesystem::path& steam_root);
// Per-worker transient WinHTTP / 429 / 5xx flag, reset by consuming it.
bool consumeArtworkRetryableError();
uint32_t artworkRateLimitSeconds();
std::vector<uint8_t> storeScreenshotForSteamApp(uint32_t app_id);
std::vector<uint8_t> coverForSteamApp(uint32_t app_id, const std::wstring& title,
                                      const std::filesystem::path& cache,
                                      const std::filesystem::path& steam_root);
std::vector<uint8_t> logoForSteamApp(uint32_t app_id, const std::wstring& title,
                                     const std::filesystem::path& cache);
std::vector<uint8_t> heroForSteamApp(uint32_t app_id, const std::wstring& title,
                                     const std::filesystem::path& cache);
// The background to download from a SteamGridDB heroes list (best rated first): the best rated one at
// least 3000 px wide (its 3840×1240 heroes), else the best rated one. A 1920×620 hero covering a 16:9
// screen is enlarged ~1.75× and looks soft. Empty when the list has no usable URL.
std::wstring preferredHeroUrl(std::string_view body);
// SteamGridDB 600x900 grid (Steam AppID, then the SGDB game matched by title), then
// Steam's own library_600x900 capsule (local library cache, then CDN).
std::vector<uint8_t> posterForSteamApp(uint32_t app_id, const std::wstring& title,
                                       const std::filesystem::path& cache,
                                       const std::filesystem::path& steam_root);
// SteamGridDB 920x430 / 460x215 grid (AppID, then title), then Steam's header capsule
// (local library cache, then CDN header / capsule_616x353).
std::vector<uint8_t> landscapeForSteamApp(uint32_t app_id, const std::wstring& title,
                                          const std::filesystem::path& cache,
                                          const std::filesystem::path& steam_root);
