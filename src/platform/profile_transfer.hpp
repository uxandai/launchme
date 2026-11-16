#pragma once
// Kopia i przenoszenie (Ustawienia → System): the profile packed into one ZIP
// (launchme_backup_YYYY-MM-DD--HH-MM.zip) by parts the user picks, and brought back on this or another
// computer, Windows or Linux. Settings and the library travel as Launchme's own data (parsed, filtered,
// merged), pictures and other folders as files. Whatever this user's key protects (protected_file.hpp:
// API keys, store sessions, plugin secrets, the companion's chats) is read out and sealed with a
// password instead (password_crypto.hpp), and protected again for the user who imports it.
//
// An import is staged (verified and unpacked into <config>/import-pending) while Launchme runs, then
// applied by the next start before anything is loaded (applyPending), so the closing launcher cannot
// write its older state over it. Files it changes are kept in <config>/backups/before-import-<time>/.
#include "user_config.hpp"
#include "library_state.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace profile_transfer {
inline constexpr uint32_t format_version = 1;
inline constexpr const wchar_t* pending_folder = L"import-pending";
inline constexpr const wchar_t* done_file = L"import-done.json";

enum class Part { settings, library, artwork, artwork_cache, keys, accounts, plugins, companion };
inline constexpr std::array<Part, 8> parts{Part::settings, Part::library, Part::artwork, Part::artwork_cache,
                                           Part::keys, Part::accounts, Part::plugins, Part::companion};
const char* slug(Part part);
std::optional<Part> partFromSlug(std::string_view slug);
// Parts whose data this user's key protects: chosen ones need a password.
bool needsPassword(Part part);

// done, total in bytes; returning true from `cancel` stops the work (the result says so).
using ProgressFn = std::function<void(uint64_t done, uint64_t total)>;
using CancelFn = std::function<bool()>;

struct Result {
    bool ok = false, cancelled = false;
    std::filesystem::path path;
    std::wstring error;
    size_t files = 0;
    uint64_t bytes = 0;
};

// What each part would hold now (the export page shows sizes).
struct PartSize { size_t files = 0; uint64_t bytes = 0; };
PartSize measure(const std::filesystem::path& config, Part part);

struct ExportInput {
    std::filesystem::path config, folder; // The data folder; the folder the ZIP goes to.
    std::set<Part> parts;
    std::string password;                 // UTF-8; needed with a part that needsPassword().
    std::optional<AppSettings> settings;  // The running launcher's own (unsaved changes too); else settings.ini.
    std::optional<LibraryState> library;  // Likewise library-state.ini.
    std::string app_version;
    ProgressFn progress;
    CancelFn cancel;
};
// "launchme_backup_2026-11-16--14-05.zip" for the local time `unix_seconds`.
std::wstring backupName(uint64_t unix_seconds);
// A new ZIP in `folder`, never over an existing file (a "-2" … suffix then).
Result exportProfile(const ExportInput& input);

struct PartInfo { size_t files = 0; uint64_t bytes = 0; };
struct Manifest {
    uint32_t format = 0;
    std::string app_version, system, created; // system: "windows" or "linux"; created: ISO 8601 UTC.
    std::wstring config;                      // The data folder it came from (paths inside it are moved).
    std::vector<std::pair<Part, PartInfo>> parts;
    bool sealed = false;                      // Has the password-sealed secrets.
    bool has(Part part) const;
};
// Reads and checks a backup's manifest; nullopt with `error` when it is no Launchme backup.
std::optional<Manifest> inspect(const std::filesystem::path& zip, std::wstring& error);

enum class Mode { merge, replace };
// One path prefix moved to another ("D:\ROMs" → "/run/media/deck/sd/ROMs"): ROM folders, own games,
// DRM-free folders and emulators. The backup's own data folder always moves to this one.
using PathMap = std::vector<std::pair<std::wstring, std::wstring>>;
// "D:\ROMs => /mnt/roms; E:\Games => /games": the rules typed in Ustawienia; malformed ones are skipped.
PathMap parsePathMap(std::wstring_view text);

struct ImportInput {
    std::filesystem::path config, zip;
    std::set<Part> parts;
    std::string password;
    Mode mode = Mode::merge;
    PathMap paths;
    ProgressFn progress;
    CancelFn cancel;
};
// Verifies the backup and unpacks the chosen parts into <config>/import-pending (replacing an older
// staged import). Secrets are protected for this user there. Nothing else in the profile changes.
Result stageImport(const ImportInput& input);

bool pending(const std::filesystem::path& config);
void discardPending(const std::filesystem::path& config);

struct Applied {
    bool ok = false;
    std::wstring error;
    size_t files = 0;
    std::filesystem::path backup; // Where the replaced files went (empty: none were replaced).
    std::vector<std::string> parts;
};
// The start of Launchme, before settings and the library are read: applies a staged import, merges
// what is merged, writes <config>/import-done.json for the launcher's message and removes the stage.
// No staged import: nothing (ok, files 0).
Applied applyPending(const std::filesystem::path& config);
// The launcher reads (and removes) what the start applied; nullopt: nothing new.
std::optional<Applied> takeApplied(const std::filesystem::path& config);

// --- Merging, exposed for tests and for Playnite's import (playnite_import.hpp) ---
void mergeLibraryState(LibraryState& local, const LibraryState& imported, Mode mode);
// Imported settings over the local ones, without what belongs to this computer (window, monitor,
// autostart, folders that do not exist here).
AppSettings mergeSettings(const AppSettings& local, AppSettings imported, const PathMap& paths);
std::filesystem::path mapPath(const std::filesystem::path& path, const PathMap& paths);
}
