#pragma once
// Linux only: running Windows programs (.exe, .com, .msi, .bat/.cmd, .lnk) through umu-launcher,
// Proton or Wine with a game's LaunchOptions, as Heroic and Lutris do:
//   [before script] <wrapper> gamemoderun mangohud gamescope <options> -- <runner> <program> <arguments>
//   [after script]
// where <runner> is
//   umu-run                                  (WINEPREFIX, GAMEID, STORE, PROTONPATH, PROTON_VERB)
//   <Proton>/proton waitforexitandrun        (STEAM_COMPAT_DATA_PATH, STEAM_COMPAT_CLIENT_INSTALL_PATH)
//   <Wine>/bin/wine                          (WINEPREFIX, WINEDEBUG, WINEDLLOVERRIDES, DXVK in the prefix)
// plan() is pure (argv, environment, folder, log file), so every combination is unit-tested;
// context() gathers the machine's facts and start() spawns the plan as a session leader (the
// launch job of game_activity.hpp). Prefix tools (winecfg, regedit, winetricks, protontricks,
// any .exe in a game's prefix, wineserver -k, deleting a prefix) use the same plans.
// One prefix layout serves all three runners: <prefix>/pfx is a link to the prefix itself
// (umu-launcher's convention), so WINEPREFIX = STEAM_COMPAT_DATA_PATH = <prefix>. docs/LINUX_WINE.md.
#include "../library/compat_tools.hpp"
#include "../model/launch_options.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace wine_runner {
enum class Runner { none, umu, proton, wine };
enum class Purpose {
    game, // Wrappers, gamescope, scripts, the options' arguments and folder, the game's log.
    tool, // A prefix tool or installer: the prefix's runner and variables only, its own log.
};

// The machine's facts a plan depends on; context() reads them, tests fill them in.
struct Context {
    std::filesystem::path config;           // Launchme's data folder: logs.
    std::optional<compat_tools::Tool> tool; // The resolved tool; nullopt: none installed.
    bool tool_fell_back = false;            // The chosen tool is gone; `tool` is the automatic one.
    std::filesystem::path umu_run;          // Empty: umu-launcher is not installed.
    std::filesystem::path steam_client;     // STEAM_COMPAT_CLIENT_INSTALL_PATH for Proton without umu.
    std::filesystem::path gamemoderun, mangohud, gamescope; // Empty: not installed.
    std::filesystem::path game_performance, dlss_swapper;   // CachyOS's wrappers; empty: not installed.
    bool lsfg_layer = false;                // lsfg-vk's Vulkan layer is installed (system, user or Flatpak).
    std::filesystem::path shell = "/bin/sh";
    std::optional<compat_tools::Tool> dxvk, vkd3d, dxvk_nvapi; // Installed components (plain Wine).
    std::filesystem::path eac_runtime, battleye_runtime;       // Steam's runtime apps, else Launchme's (compat_tools::installedRuntime).
    std::string game_id = "umu-default";    // GAMEID (compat_tools::gameId).
    std::string steam_game_id;              // The launcher's own SteamGameId (Steam's Game Mode), kept.
    bool inside_gamescope = false;          // Steam's Game Mode: gamescope is not nested.
};
Context context(const CompatLaunch& launch);

struct Request {
    // The Windows file (absolute), or a Wine program by name ("winecfg", "regedit", "control",
    // "taskmgr", "explorer", "cmd", "wineboot", "createprefix" for umu).
    std::filesystem::path program;
    std::vector<std::string> arguments;     // Its arguments (the action's, already split).
    std::filesystem::path working_directory; // Empty: the program's folder.
    std::vector<std::pair<std::string, std::string>> environment; // The action's own variables (Amazon's SDK).
    CompatLaunch launch;
    Purpose purpose = Purpose::game;
    std::string verb = "waitforexitandrun"; // Proton's verb (PROTON_VERB for umu).
};

struct Plan {
    Runner runner = Runner::none;
    std::vector<std::string> argv;          // argv[0] is absolute, or a name looked up in PATH.
    // Set on top of the launcher's environment, in order (a later entry wins); then `unset` removed.
    std::vector<std::pair<std::string, std::string>> environment;
    std::vector<std::string> unset;
    std::filesystem::path working_directory;
    std::filesystem::path prefix;           // WINEPREFIX.
    std::filesystem::path log_file;         // stdout and stderr; empty: the launcher's.
    bool proton_prefix = false;             // Proton directly: <prefix>/pfx → . is made before it runs.
    std::vector<std::string> missing;       // Enabled but not installed: "gamemoderun", "mangohud", "gamescope",
                                            // "dxvk", "vkd3d-proton", "dxvk-nvapi", "eac-runtime", "battleye-runtime",
                                            // "game-performance", "dlss-swapper", "lsfg-vk".
    std::string error;                      // Not runnable: "no-tool", "no-umu", "no-wine", "no-program".
};
Plan plan(const Request& request, const Context& context);

// Polish and English text for Plan::error and Plan::missing codes ("no-tool", "gamescope", ...).
launch_options_catalog::Text problemText(std::string_view code);

// The child's environment block: `current` ("KEY=value") with the plan's variables applied.
std::vector<std::string> environmentBlock(const Plan& plan, const std::vector<std::string>& current);
// The launcher's own environment as "KEY=value" entries.
std::vector<std::string> currentEnvironment();
// Prepares the prefix's folder (and Proton's pfx link) and the log, then starts the plan in a session
// of its own. 0 or an errno (ENOENT also when the plan is not runnable).
int start(const Plan& plan, pid_t& pid);

// Helpers, also for tests.
// "C:\\Games\\x.exe" for a Linux path inside the prefix's drive_c, else "Z:\\home\\...".
std::string windowsPath(const std::filesystem::path& file, const std::filesystem::path& prefix);
// A Windows path in the prefix ("C:\\Program Files\\x.exe") → its file; empty when it maps nowhere.
std::filesystem::path unixPath(std::string_view windows, const std::filesystem::path& prefix);
// POSIX shell words (quotes, backslashes) as gamescope's and the custom wrapper's options use.
std::vector<std::string> shellWords(std::string_view text);
std::string shellQuote(std::string_view text);
// Folder-safe name of a game's logs ("epic:Fortnite" → "epic-fortnite").
std::string logName(const CompatLaunch& launch);
std::filesystem::path logFolder(const std::filesystem::path& config); // <config>/logs/wine
// The last launch's files: the runner's output (<game>.log), Proton's own log(s) and the tools' log.
std::vector<std::filesystem::path> logFiles(const CompatLaunch& launch);

// A Windows shortcut (.lnk, MS-SHLLINK): the target's Windows path, its arguments and folder.
struct Shortcut {
    std::string target, arguments, working_directory;
};
std::optional<Shortcut> parseShortcut(std::span<const uint8_t> bytes);
// A request for a .lnk with its target resolved inside the prefix (the target program instead of
// the shortcut); unchanged when the shortcut cannot be resolved (it then runs through start.exe).
Request resolveShortcut(Request request);

// Prefix tools. Each starts a process in a session of its own and reports it in `pid` (0 or an errno).
// Wine's own programs: "winecfg", "regedit", "control", "taskmgr", "explorer", "uninstaller".
int runWineProgram(const CompatLaunch& launch, std::string_view program, pid_t& pid);
// Any Windows program (an installer, a patch, a mod manager) in the game's prefix, waiting for it.
int runInPrefix(const CompatLaunch& launch, const std::filesystem::path& program, const std::vector<std::string>& arguments, pid_t& pid);
// Creates the prefix now (umu-run createprefix, Proton's run wineboot, wine wineboot -u).
int createPrefix(const CompatLaunch& launch, pid_t& pid);
// winetricks verbs (none: its window). umu-run winetricks for Proton through umu, else winetricks
// with the tool's WINE/WINESERVER; protonfixes' bundled winetricks when none is on PATH.
Plan winetricksPlan(const CompatLaunch& launch, const std::vector<std::string>& verbs, const Context& context, const std::filesystem::path& winetricks);
int winetricks(const CompatLaunch& launch, const std::vector<std::string>& verbs, pid_t& pid);
bool winetricksAvailable(const CompatLaunch& launch);
// protontricks for a Steam game's own prefix (native or Flatpak); verbs empty: its window.
bool protontricksAvailable();
std::vector<std::string> protontricksCommand(uint32_t app_id, const std::vector<std::string>& verbs);
int protontricks(uint32_t app_id, const std::vector<std::string>& verbs, pid_t& pid);
// Ends every process of the prefix (wineserver -k with the tool's wineserver); waits up to 5 s.
bool killPrefix(const CompatLaunch& launch);
bool prefixExists(const std::filesystem::path& prefix); // Its system.reg or drive_c/windows (also under pfx/).
// What made a prefix: Proton (umu included) leaves its version and tracked_files beside the Wine
// files of STEAM_COMPAT_DATA_PATH; a plain Wine prefix has only those. none: no prefix there yet.
// Runner::umu is never returned (umu runs Proton).
Runner prefixRunner(const std::filesystem::path& prefix);
// The prefix was made by Proton and the tool is a Wine build, or the other way round: running it
// may break the prefix (a different Wine, Proton's own DLL layout). Unknown either way: false.
bool runnerMismatch(const std::filesystem::path& prefix, const std::optional<compat_tools::Tool>& tool);
// Deletes a prefix folder (Reset: the next launch makes a new one). Refuses anything that is not a
// prefix (no system.reg / drive_c, the home folder, /, a folder above the prefix root). false on failure.
bool deletePrefix(const std::filesystem::path& prefix);
}
