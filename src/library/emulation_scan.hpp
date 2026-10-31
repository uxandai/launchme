#pragma once
#include "../model/game.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Emulated games straight from the disk (Ustawienia → Biblioteka → Emulacja → Gry emulowane): the ROM
// folder of an EmuDeck / ES-DE setup read with launchme's own parsers (the systems, globs and
// emulators of EmuDeck's Steam ROM Manager parsers, built in: Steam ROM Manager itself is not needed).
//   the ROM folder   chosen in Settings, else ES-DE's ROMDirectory (es_settings.xml), EmuDeck's
//                    $romsPath (settings.ps1 / settings.sh), else SRM's romsDirectory when it is there;
//   the games        each system folder with games, one game per file its glob takes (**, *, ?, @(a|b),
//                    !(…), {a,b} and ${title}, as SRM's glob parser); .m3u playlists hide their discs and
//                    .cue/.gdi sheets their tracks; SRM's userExceptions.json (exclude, newTitle) and
//                    ES-DE's <hidden> apply when they exist;
//   the command      EmuDeck's launcher (<tools>\launchers\<emulator>.ps1) run directly and hidden, else the
//                    emulator itself (EmuDeck's Emulators folder, RetroArch with the system's core).
// Pure file reading (no Win32): src/app/emulation_library.cpp runs it on a worker.
namespace emulation_scan {

struct Environment {
    std::filesystem::path app_data;  // %APPDATA% (Windows) or ~/.config.
    std::filesystem::path home;      // %USERPROFILE% or $HOME.
    std::filesystem::path esde_root; // ES-DE's own folder (es_settings.xml, gamelists); empty: none.
    std::filesystem::path system_root; // %SystemRoot% (Windows PowerShell); empty elsewhere.
    bool windows = true;
};
Environment systemEnvironment();

struct Paths {
    std::filesystem::path roms;   // Empty: no ROM folder found.
    std::filesystem::path tools;  // EmuDeck's <emulation>\tools (launchers, SRM's portable userData).
    std::filesystem::path srm;    // Steam ROM Manager's userData.
    std::wstring ra_path, ra_cores, steam; // SRM's environment variables.
    std::filesystem::path emulators; // EmuDeck's Emulators folder (%APPDATA%\EmuDeck\Emulators).
    std::wstring origin;          // Where the ROM folder came from: manual, ES-DE, EmuDeck, Steam ROM Manager.
};
// The ROM folder an installed setup names (in the order above); empty when none exists on disk.
std::filesystem::path detectRoms(const Environment& environment, std::wstring* origin = nullptr);
Paths resolve(const Environment& environment, const std::filesystem::path& roms_override);

// One emulator a system can start: EmuDeck's launcher script (<tools>\launchers\<launcher>.ps1) with
// its arguments before the ROM, else the program itself in EmuDeck's Emulators\<folder>.
struct Emulator {
    std::wstring launcher, arguments;  // "pcsx2", "'-f' '-g'" (PowerShell words).
    std::wstring folder, executable;   // "PCSX2-Qt", "pcsx2-qtx64.exe".
    std::wstring direct;               // Its own arguments before the ROM when started directly.
};
// A system folder ("ps2") with its platform, the glob its games match and its emulators, best first.
struct Rule {
    std::wstring system, platform, glob, core; // core: RetroArch's ("snes9x_libretro"); empty for standalone ones.
    std::filesystem::path folder;              // <roms>\<system>
    std::vector<Emulator> emulators;
};
struct Setup {
    Environment environment;
    Paths paths;
    std::vector<Rule> rules; // Every built-in system (also those without a folder: a ROM added by hand).
    const Rule* forFile(const std::wstring& platform, const std::filesystem::path& file) const;
};
Setup load(const Environment& environment, const std::filesystem::path& roms_override);
// An SRM glob tried on a path relative to the system folder ('/' separated, any case): the ${title}
// it captures (the file stem when the glob has none), nullopt when it does not match.
std::optional<std::wstring> match(std::wstring_view glob, std::wstring_view relative);
// How a system's emulator starts one ROM; empty when none of its emulators is installed.
GameAction action(const Rule& rule, const Setup& setup, const std::filesystem::path& rom);
// A typed emulator command ("C:\Emu\pcsx2.exe" -fullscreen %ROM%): %ROM% becomes the quoted ROM
// path, appended when the command does not name it.
GameAction commandAction(std::wstring_view command, const std::filesystem::path& rom);
struct Result {
    std::vector<Game> games;
    size_t hidden = 0, excluded = 0, systems = 0;
};
using Cancelled = std::function<bool()>;
Result scan(const Setup& setup, bool show_hidden = false, const Cancelled& cancelled = {});

// Recognising one file or folder (Dodaj grę, files dropped on the window).
bool romFile(const std::filesystem::path& file); // A disc image or ROM extension (not .exe & co.).
std::vector<std::wstring> romExtensions();         // Every one romFile takes, with the dot.
std::wstring systemOf(const std::filesystem::path& file, const std::filesystem::path& roms = {}); // "ps2" from …\roms\ps2\…
std::wstring platformForSystem(std::wstring_view system); // "Retro" for folders no platform names.
std::wstring guessPlatform(const std::filesystem::path& file, const std::filesystem::path& roms = {});
struct FolderGame {
    std::filesystem::path rom;
    std::wstring platform, title;
};
// A game kept as a folder (PS3: PS3_GAME\USRDIR\EBOOT.BIN, Wii U: code\*.rpx) or a folder holding
// one game's ROM (or its .m3u/.cue); nullopt for anything else.
std::optional<FolderGame> folderGame(const std::filesystem::path& folder);
std::wstring displayTitle(std::wstring_view raw); // "Legend of Zelda, The (Europe) [!]" -> "The Legend of Zelda".
std::wstring romKey(const std::filesystem::path& rom); // Lower case, backslashes: compares ROM paths.
uint32_t romAppId(const std::filesystem::path& rom);
// An emulated game: GameKind::nonsteam (every emulation path treats it as such) from GameSource::custom
// (launched by its own action, played time kept locally), keyed by its ROM.
Game makeGame(const std::wstring& name, const std::wstring& platform, const std::filesystem::path& rom, GameAction action);
} // namespace emulation_scan
