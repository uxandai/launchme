#pragma once

#include "../model/game.hpp"
#include "rawg.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// "Katalog gier DRM-free": folders the user points at, where every subfolder is one game
// (a folder holding several game folders is looked into once more). drm-free.json in the
// user config directory keeps the folders, what the scanner found and the user's corrections
// (a chosen executable, ignored folders). Parsing, title cleanup, executable ranking and
// RAWG matching are pure or take injectable backends, so they are tested offline.
namespace drm_free {
inline constexpr size_t max_roots = 16;
inline constexpr size_t max_games = 2000;
inline constexpr size_t max_path = 4096;

struct Entry {
    std::string id;                          // 16 hex digits of the folder path: Game::source_id.
    std::filesystem::path folder;            // The game's folder inside one of the roots.
    std::filesystem::path executable;        // Empty: no game executable was found.
    std::wstring arguments;                  // From GOG play tasks; user-chosen files start without.
    std::filesystem::path working_directory; // Empty: the executable's folder.
    std::wstring title;                      // Cleaned folder/metadata title (the RAWG query).
    std::wstring product;                    // Executable's ProductName, a second query when the title fails.
    std::wstring name;                       // Displayed: RAWG's name once identified, else the title.
    int year = 0;                            // A year written in the folder name, a matching hint.
    uint32_t steam_app_id = 0;               // steam_appid.txt or RAWG's Steam store link: artwork only.
    uint32_t rawg_id = 0;
    uint64_t checked = 0;                    // Last RAWG attempt (Unix seconds); 0 = not asked yet.
    uint64_t found = 0;                      // First seen (Unix seconds).
    uint64_t folder_time = 0;                // Folder's last write time when it was last examined.
    bool manual_executable = false;          // Picked by the user: never replaced by detection.
    bool manual_identity = false;            // Named by "Zidentyfikuj grę": RAWG matching leaves it alone.
    bool ignored = false;                    // Kept out of the library and never added again.
    bool missing = false;                    // Executable (or the whole root) not reachable at the last scan.
    bool operator==(const Entry&) const = default;
};

struct Catalog {
    std::vector<std::filesystem::path> roots;
    std::vector<Entry> games;
    uint64_t scanned = 0;    // Last finished scan (Unix seconds).
    uint64_t rawg_pause = 0; // RAWG refused or failed: no automatic matching before this time.
    bool operator==(const Catalog&) const = default;
};

struct Loaded {
    Catalog catalog;
    bool readable = true; // False: a damaged file (copied to "<file>.bak" by load()).
};
// Never throws; invalid entries are skipped.
Loaded parse(std::string_view bytes);
std::string format(const Catalog& catalog);
Loaded load(const std::filesystem::path& file);
bool save(const Catalog& catalog, const std::filesystem::path& file);

// Normalized, without a trailing separator: the key of folder comparisons. Windows: lowercase,
// backslashes; Linux keeps the case (its file systems tell "Game" and "game" apart).
std::wstring folderKey(const std::filesystem::path& folder);
std::string folderId(const std::filesystem::path& folder);
bool validRoot(const std::filesystem::path& root);
bool inside(const std::filesystem::path& path, const std::filesystem::path& folder); // Strictly below it.
const std::filesystem::path* rootOf(const Catalog& catalog, const std::filesystem::path& folder); // At or below one of its roots.

// "The.Witcher.3.Wild.Hunt.v1.32-GOG" → "The Witcher 3 Wild Hunt"; "Celeste (2018)" → "Celeste", 2018.
struct Title { std::wstring text; int year = 0; };
Title cleanTitle(std::wstring_view folder_name);
// Engine/runtime names that never name a game ("Unity Player", "Godot Engine"...).
bool genericProduct(std::wstring_view product);

// --- Executable detection ----------------------------------------------------
struct Candidate {
    std::filesystem::path path;
    int depth = 0;           // Folders below the game folder.
    uint64_t size = 0;
    bool console = false;    // PE subsystem: console tools and servers rank low.
    bool x64 = false;
    // Linux: a native game (an ELF executable or a shell launcher such as GOG's start.sh) ranks
    // above a Windows .exe beside it; gog_start: start.sh next to GOG's gameinfo.
    bool native = false, script = false, gog_start = false;
    // Neighbours that identify an engine's main executable.
    bool unity_data = false, godot_pack = false, unreal_stub = false, gamemaker_data = false, rpgmaker = false, renpy = false;
};
bool excludedExecutable(std::wstring_view file_name);
bool skippedFolder(std::wstring_view folder_name);
int executableScore(const Candidate& candidate, std::wstring_view folder_name, std::wstring_view title);

struct Detection {
    std::filesystem::path executable;
    std::wstring arguments;
    std::filesystem::path working_directory;
    std::wstring title;      // goggame-*.info name; empty = take it from the folder.
    std::wstring product;    // Version resource ProductName (non-generic only).
    uint32_t steam_app_id = 0;
    bool container = false;  // No game here, but several game folders below: scan those instead.
    bool store_owned = false; // Epic/Xbox package or similar: another source owns it.
};
using Cancelled = std::function<bool()>;
// Disk only (no network). Examines at most a few thousand entries per folder.
Detection detect(const std::filesystem::path& folder, const Cancelled& cancelled = {});

// --- Scanning ----------------------------------------------------------------
struct ScanOptions {
    bool full = false;   // Examine every folder again ("Skanuj teraz"); otherwise only changed ones.
    bool gentle = false; // Background: short pauses between folders.
    std::unordered_set<std::wstring> known; // folderKey() of other sources' install folders.
    uint64_t now = 0;
};
struct Progress { size_t done = 0, total = 0; };
// Builds the next catalog from `previous`. A cancelled scan keeps the entries it did not
// reach, so its partial result is still complete. A missing root keeps its games (missing).
Catalog scan(const Catalog& previous, const ScanOptions& options, const Cancelled& cancelled = {},
             const std::function<void(Progress)>& progress = {});

// The scan worked on a snapshot: apply it to the current catalog, keeping folders, user
// choices (executable, ignore, identity) made in the meantime and dropping removed roots.
Catalog merge(const Catalog& current, Catalog scanned);

// --- RAWG matching -----------------------------------------------------------
// Higher is better; candidates with no shared word score below zero.
int matchScore(const Title& query, const rawg::Candidate& candidate, size_t rank);
std::optional<size_t> bestMatch(const Title& query, const std::vector<rawg::Candidate>& candidates);

struct IdentifyBackend {
    rawg::Get get = httpGetPrivate;
    std::function<bool(std::chrono::milliseconds)> wait; // False: stop (shutdown/cancel).
    std::function<uint64_t()> now;
    // Without a Steam page, artwork comes from the SteamGridDB game of that name and year.
    std::wstring sgdb_key;
    rawg::Get sgdb_get = httpGet;
};
struct IdentifyPaths { std::filesystem::path metadata, ratings, covers; };
struct IdentifyLimits {
    size_t games = 40;                         // RAWG lookups per run (each 2-3 requests).
    std::chrono::milliseconds spacing{750};    // Between requests.
    uint64_t retry_unmatched = 7 * 24 * 3600;  // A title without results is asked again after a week.
    uint64_t pause = 30 * 60;                  // After an error: no RAWG requests for this long.
};
// Entries that RAWG matching would ask about now (not ignored, not identified, not recently tried).
bool needsIdentity(const Entry& entry, uint64_t now, const IdentifyLimits& limits = {});
// Network. Picks the best RAWG result for every waiting entry (the user's "always the best
// match" choice), saves its metadata/rating under the game's key and selects Steam artwork
// when RAWG links a Steam page. `changed` is called after every identified game.
void identify(Catalog& catalog, const std::wstring& key, const IdentifyPaths& paths, const Cancelled& cancelled,
              const std::function<void(const Catalog&)>& changed, const IdentifyBackend& backend = {}, IdentifyLimits limits = {});

// --- Library -----------------------------------------------------------------
std::filesystem::path workingDirectory(const Entry& entry);
GameAction action(const Entry& entry);
// GameSource::drmfree, key "drmfree:<id>". Only an entry with a reachable executable is playable.
Game makeGame(const Entry& entry);
} // namespace drm_free
