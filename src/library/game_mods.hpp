#pragma once
// Graphics mods a game (or an emulator) gets in one click (Opcje uruchamiania → Mody): ReShade with
// add-on support, a RenoDX add-on, OptiScaler, newer DLSS DLLs, "DLSS 5" (DLSS neural rendering through
// ReShade and RenoDX's DLSS 5 add-on) and ANR FG (Apex Neural Rendering frame generation). Windows games.
//
// Everything goes into the folder of the game's program (Target::folder): the proxy DLL the game loads
// (dxgi.dll, winmm.dll…) and the mod's files beside it. A file a mod replaces is moved aside to
// "<name>.launchme-backup" and put back when the mod is removed. What Launchme installed is kept in a
// record per folder (<config>\mods\installed\<hash>.json); mods put there by other tools or by hand are
// recognised on disk (scan) and shown, but only Launchme's own are removed.
//
// This file: the catalogue, the record, what is on disk and the files' shared moves. The downloads and
// the steps of each mod: game_mods_install.hpp; the sources' formats: game_mods_sources.hpp.
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game_mods {
enum class Mod { reshade, renodx, optiscaler, dlss, dlss5, anr };
inline constexpr int mod_count = 6;
inline constexpr std::array<Mod, mod_count> all_mods{Mod::reshade, Mod::renodx, Mod::optiscaler, Mod::dlss, Mod::dlss5, Mod::anr};
const char* slug(Mod mod);           // "reshade", "renodx", "optiscaler", "dlss", "dlss5", "anr" (the record, the settings).
std::optional<Mod> fromSlug(std::string_view slug);
const wchar_t* name(Mod mod);        // "ReShade", "RenoDX", "OptiScaler", "DLSS", "DLSS 5", "ANR FG".

// How a mod goes in: ReShade's and OptiScaler's file name (the DLL the game loads), RenoDX's add-on (the
// game's own or an engine's), which DLSS DLLs are replaced, ANR FG's package. The first is the default.
struct Method {
    std::string_view id;
    std::wstring_view label; // Untranslated: file names and product names; tr() for the others in the UI.
};
std::span<const Method> methods(Mod mod);
const Method* findMethod(Mod mod, std::string_view id);
// The preferred method if it is the mod's, else its first.
std::string_view methodOr(Mod mod, std::string_view preferred);
// An emulator's folder takes ReShade only (the others need a game's own upscaler inputs).
bool forEmulators(Mod mod);

enum class TargetKind { game, emulator };
struct Target {
    std::filesystem::path folder;  // The program's folder: proxies and the mods' files.
    std::filesystem::path root;    // The installation (DLSS DLLs anywhere under it); the folder when unknown.
    std::filesystem::path exe;     // The program (its bitness, the anti-cheat beside it); may be empty.
    TargetKind kind = TargetKind::game;
    uint32_t steam_appid = 0;      // Matches RenoDX's list.
    std::wstring title;            // The game's or the emulator's name.
    bool x86 = false;              // A 32-bit program (scan fills it from exe).
};

// One file a mod placed: `backup` holds what was there before (empty: nothing was).
struct PlacedFile {
    std::filesystem::path path, backup;
    bool operator==(const PlacedFile&) const = default;
};
struct RecordEntry {
    std::string version, method;
    std::wstring detail;                          // RenoDX: the add-on's file; ANR: the package's folder in the cache.
    std::vector<PlacedFile> files;
    std::vector<std::filesystem::path> folders;   // Folders it created (removed when empty or its own).
    std::string moved;                            // OptiScaler/ANR: the file name another tool's ReShade had (moved to ReShade64.dll).
    bool operator==(const RecordEntry&) const = default;
};
struct Record {
    std::filesystem::path folder;
    std::map<std::string, RecordEntry> mods; // slug -> entry.
    const RecordEntry* find(Mod mod) const;
    bool empty() const { return mods.empty(); }
};
// <data>\installed\<hash of the folder>.json
std::filesystem::path recordFile(const std::filesystem::path& data, const std::filesystem::path& folder);
std::optional<Record> loadRecord(const std::filesystem::path& data, const std::filesystem::path& folder);
bool saveRecord(const std::filesystem::path& data, const Record& record); // An empty record removes its file.
std::string serialize(const Record& record);
std::optional<Record> parseRecord(std::string_view text);

// A mod as found on disk.
struct Installed {
    bool present = false;
    bool ours = false;       // In Launchme's record (removable here).
    std::string method;      // Its file name or variant ("dxgi.dll", "full").
    std::wstring version;    // "6.8.0", "310.9.16", "V.B20"; empty when unknown.
    std::wstring detail;     // RenoDX's add-on, "OptiScaler" for an ANR inside it…
};
struct State {
    std::array<Installed, mod_count> mods;
    std::vector<std::wstring> anticheat; // "Easy Anti-Cheat", "BattlEye"…: online games that ban injected DLLs.
    bool x86 = false;
    bool has_dlss = false;               // The game ships nvngx_dlss.dll (DLSS can be updated, DLSS 5 has its inputs).
    std::vector<std::filesystem::path> dlss_files; // Every nvngx_dlss*.dll under the root (not ANR's or OptiScaler's).
    const Installed& operator[](Mod mod) const { return mods[static_cast<size_t>(mod)]; }
    Installed& operator[](Mod mod) { return mods[static_cast<size_t>(mod)]; }
};
// Reads the folder (and the root for DLSS, at most a few levels deep): the worker's thread.
State scan(const Target& target, const Record* record);
// The badges a game shows ("ReShade", "OptiScaler · dxgi.dll", "DLSS 310.9.16"…), in the catalogue's order.
// DLSS only when Launchme replaced it or another tool's newer DLL is there is not told apart: a game's
// own DLSS shows too (it is what the game runs with).
std::vector<std::wstring> badges(const State& state);

// Pure helpers shared with the installer and the tests.
// A PE image's machine: true for x86 (i386), false for x64/ARM64 or when unreadable.
bool x86Image(const std::filesystem::path& exe);
// The game's program in an installation: an Unreal game's *-Win64-Shipping.exe, else the largest .exe
// that is not a tool (uninstallers, crash reporters, redistributables), at most `depth` levels down.
std::filesystem::path mainExecutable(const std::filesystem::path& root, int depth = 4);
// "310.9.16.0" -> "310.9.16"; "6.8.0.2104" -> "6.8.0"; other text as it is.
std::wstring shortVersion(std::wstring_view version);
// -1, 0, 1 by the dotted numbers ("310.10" > "310.9"); text after the numbers is ignored.
int compareVersions(std::string_view a, std::string_view b);
// An INI's value set in its section (added at the section's end, the section at the file's end when
// missing); the file's own line endings are kept.
std::string setIniValue(std::string_view text, std::string_view section, std::string_view key, std::string_view value);
std::optional<std::string> iniValue(std::string_view text, std::string_view section, std::string_view key);
// The file's product name / version from its version resource (Windows DLLs).
std::wstring productName(const std::filesystem::path& file);
std::wstring fileVersion(const std::filesystem::path& file);
bool isReShade(const std::filesystem::path& file);
bool isOptiScaler(const std::filesystem::path& file);

// The proxy names ReShade and OptiScaler are recognised under.
inline constexpr std::array<std::wstring_view, 9> reshade_names{L"dxgi.dll", L"d3d9.dll", L"d3d10.dll", L"d3d11.dll", L"d3d12.dll", L"opengl32.dll",
                                                                L"dinput8.dll", L"ReShade64.dll", L"ReShade32.dll"};
inline constexpr std::array<std::wstring_view, 8> optiscaler_names{L"dxgi.dll", L"winmm.dll", L"version.dll", L"dbghelp.dll", L"d3d12.dll",
                                                                   L"wininet.dll", L"winhttp.dll", L"OptiScaler.asi"};
inline constexpr std::wstring_view backup_suffix = L".launchme-backup";

// Puts `source` at `target`: what was there is moved to <target>.launchme-backup (an older backup of
// the same file is kept), the folders are created. The placed file goes to `entry`.
bool placeFile(const std::filesystem::path& source, const std::filesystem::path& target, RecordEntry& entry);
// The same for bytes already in memory.
bool placeBytes(std::string_view bytes, const std::filesystem::path& target, RecordEntry& entry);
// Takes a mod's files out: each removed, its backup put back; its own folders removed. False when a
// file could not be removed (the game runs): the entry then keeps what is left.
bool removeFiles(RecordEntry& entry);
} // namespace game_mods
