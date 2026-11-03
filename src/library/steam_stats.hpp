#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

// Play time and achievement progress of one Steam game for the signed-in account, from
// the Steam client's own files (never the network): total minutes from
// userdata/<id>/config/localconfig.vdf, achievements from appcache/stats (schema and the
// account's unlock bits, exact), else from the library cache the client keeps for its game
// pages (librarycache/achievement_progress.json, then <AppID>.json).
// Called on workers; localconfig.vdf is parsed once per change of the file.
struct SteamStats {
    std::optional<uint32_t> playtime_minutes;
    uint32_t achievements_unlocked = 0, achievements_total = 0; // total 0 = no known achievements.
    bool operator==(const SteamStats&) const = default;
};
SteamStats readSteamStats(const std::filesystem::path& steam_root, uint64_t steam_id, uint32_t app_id);
// Only the play time, from the same parsed file: cheap enough for the UI thread, a list asks for every visible row.
std::optional<uint32_t> readSteamPlaytime(const std::filesystem::path& steam_root, uint64_t steam_id, uint32_t app_id);

// "36 min", "1,5 godz." / "1.5 h", "124 godz." / "124 h" in the UI language.
std::wstring formatPlaytime(uint32_t minutes);
