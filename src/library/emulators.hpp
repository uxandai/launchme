#pragma once
#include <array>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The emulators launchme starts emulated games with (Ustawienia → Biblioteka → Emulacja): one entry
// per emulator, found on its own in this order:
//   manual           the program chosen by hand (AppSettings::emulator_overrides);
//   emudeck_script   EmuDeck's launcher <tools>\launchers\<script>.ps1 (.sh on Linux), the exe its
//                    $emulatorFile names shown beside it (a launcher whose program is gone is passed over);
//   emudeck_exe      the program in EmuDeck's Emulators folder;
//   esde_rule        ES-DE's es_find_rules.xml (staticpath with a wildcard, systempath, winregistrypath,
//                    winregistryvalue), when ES-DE is there;
//   registry         Windows' App Paths and the installed programs' InstallLocation;
//   known_path       where installers put it (Program Files, %LOCALAPPDATA%\Programs, Scoop, Steam);
//   path_env         a program of its name on PATH;
//   flatpak/appimage Linux: an installed flatpak (flatpak run <id>) or an AppImage in ~/Applications.
// Pure file reading (the registry on Windows only): the ROM scan's worker runs it.
namespace emulators {

struct Info {
    std::wstring_view id, name;
    std::wstring_view script, script_args;            // EmuDeck's launcher and its words before the ROM (PowerShell).
    std::wstring_view emudeck_folder;                 // Its folder in EmuDeck's Emulators.
    std::array<std::wstring_view, 3> exe_names;       // Windows programs, EmuDeck's first.
    std::array<std::wstring_view, 3> esde_names;      // Its names in es_find_rules.xml.
    std::array<std::wstring_view, 3> known_dirs;      // Folder names installers use.
    std::wstring_view flatpak;                        // Linux: its flatpak ID.
    std::array<std::wstring_view, 2> linux_names;     // Linux: its program names (PATH, AppImage).
    std::wstring_view direct_args;                    // Its own arguments before the ROM when started directly.
    bool shared_exe = false;                          // Its program's name is another emulator's too (PrimeHack: Dolphin.exe).
};
std::span<const Info> catalog();
const Info* find(std::wstring_view id);

enum class Via { manual, emudeck_script, emudeck_exe, esde_rule, registry, known_path, path_env, flatpak, appimage, none };
struct Found {
    std::wstring id;
    Via via = Via::none;
    std::filesystem::path program;  // What runs: the emulator, EmuDeck's launcher, flatpak.
    std::wstring prefix;            // Words before the emulator's own (flatpak: "run <id>").
    std::filesystem::path target;   // The emulator itself (behind a launcher: its $emulatorFile).
    std::wstring detail;            // The rule, key or file that found it.
    bool found() const { return via != Via::none; }
};
// Chosen by hand: the program (empty: found on its own) and the arguments before the ROM
// (empty: its default; %ROM% places the ROM, %CORE% RetroArch's core).
struct Override {
    std::filesystem::path program;
    std::wstring arguments;
    bool operator==(const Override&) const = default;
};
struct Choices {
    std::map<std::string, Override> overrides; // Emulator ID (UTF-8) -> what was chosen.
    std::map<std::string, std::string> systems; // System folder ("ps2") -> emulator ID.
};

struct Context {
    bool windows = true;
    std::filesystem::path tools;           // EmuDeck's tools (launchers\).
    std::filesystem::path emudeck;         // EmuDeck's Emulators folder.
    std::filesystem::path esde_install;    // ES-DE's program folder (%ESPATH%), holding resources\systems.
    std::filesystem::path home, app_data, local_app_data, system_drive, steam;
    std::vector<std::filesystem::path> program_dirs; // Program Files, %LOCALAPPDATA%\Programs...
    std::vector<std::filesystem::path> search_path;  // PATH.
    bool registry = true;                  // Windows: App Paths and installed programs (tests: off).
};

// Every emulator of the catalogue, in its order.
std::vector<Found> detectAll(const Context& context, const std::map<std::string, Override>& overrides);
Found detect(const Info& info, const Context& context, const Override* manual = nullptr);
// EmuDeck's launcher's program ($emulatorFile = "…" in its first lines); empty when it names none.
std::filesystem::path scriptTarget(const std::filesystem::path& script, const Context& context);
// The arguments it starts a game with when nothing is typed (RetroArch: -L %CORE%).
std::wstring defaultArguments(const Info& info, bool script, bool core);
} // namespace emulators
