#pragma once
// Linux: a Steam game's own Proton settings, as Steam keeps them. Its launch options live in
// userdata/<account>/config/localconfig.vdf (UserLocalConfigStore/Software/Valve/Steam/apps/<app>/
// LaunchOptions), its compatibility tool in config/config.vdf (InstallConfigStore/Software/Valve/Steam/
// CompatToolMapping/<app>/name). Steam holds both in memory and writes them back when it quits, so the
// files take a change only while Steam is closed; while it runs, its CEF context does
// (SteamClient.Apps.SetAppLaunchOptions / SpecifyCompatTool, as Decky's plugins call them; the
// expressions are built here, src/platform/steam_library_sync.hpp evaluates them).
#include "../model/launch_options_catalog.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace steam_game_config {
struct Tool {
    std::string name;   // Steam's internal name (CompatToolMapping, SpecifyCompatTool): "GE-Proton10-4", "proton_9".
    std::wstring label; // Its display name.
};
struct Settings {
    std::string launch_options; // The whole string ("PROTON_ENABLE_NVAPI=1 %command% -dx11").
    std::string compat_tool;    // Empty: Steam's default.
    std::vector<Tool> tools;    // What Steam offers this game (live reads only).
};

// <root>/userdata/<account ID>/config/localconfig.vdf; `account` is a SteamID64 or the 32-bit ID.
std::filesystem::path localConfig(const std::filesystem::path& steam_root, uint64_t account);
std::filesystem::path installConfig(const std::filesystem::path& steam_root);
// What the files say (empty strings for what they do not set); nullopt when a file exists but is unreadable.
std::optional<Settings> read(const std::filesystem::path& local_config, const std::filesystem::path& install_config, uint32_t app);
// Steam closed: each file is copied to <file>.launchme-backup first. nullopt leaves that setting alone;
// an empty tool removes the game's mapping (Steam's default again). False when a file cannot be read or written.
bool write(const std::filesystem::path& local_config, const std::filesystem::path& install_config, uint32_t app,
           const std::optional<std::string>& launch_options, const std::optional<std::string>& compat_tool);

// Changes waiting for Steam to close (Steam running without its CEF port): kept in a file of Launchme's
// so a restart does not lose them; one entry per game, a later change of the same setting replaces it.
struct Pending {
    uint32_t app = 0;
    std::optional<std::string> launch_options, compat_tool;
    bool operator==(const Pending&) const = default;
};
std::vector<Pending> loadPending(const std::filesystem::path& file);
bool savePending(const std::filesystem::path& file, const std::vector<Pending>& list); // An empty list removes the file.
void queue(std::vector<Pending>& list, const Pending& change);

// The file that makes Steam open its CEF debugging port (127.0.0.1:8080) from its next start, as Decky's installer does.
std::filesystem::path cefFlag(const std::filesystem::path& steam_root);

// Live (Steam running, its CEF port open): JavaScript for SharedJSContext. Both resolve to JSON text
// {"launch":"…","tool":"…","tools":[{"name":"…","label":"…"}]}; write applies, then reads back.
std::string readExpression(uint32_t app);
std::string writeExpression(uint32_t app, const std::optional<std::string>& launch_options, const std::optional<std::string>& compat_tool);
std::optional<Settings> parseReply(std::string_view json);

// Steam's internal name of a tool folder: compatibilitytool.vdf's compat_tools key (compatibilitytools.d),
// else Valve's Proton by its folder ("Proton 9.0" → proton_9, "Proton - Experimental" → proton_experimental).
std::string internalName(const std::filesystem::path& folder);

// Switches that compose Steam's launch options: an environment variable before %command% or a wrapper.
struct Switch {
    const char* id;
    const char* word; // "PROTON_ENABLE_NVAPI=1", "gamemoderun".
    launch_options_catalog::Text title, description;
};
const std::vector<Switch>& switches();
bool has(std::string_view options, const Switch& item);
// The options with the switch added (variables first, wrappers just before %command%) or removed;
// the game's own arguments and anything else typed stay.
std::string toggle(std::string_view options, const Switch& item, bool on);
}
