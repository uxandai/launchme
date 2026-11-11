#pragma once
// The store clients and front ends the Stores' Launchers tab offers (src/app/store_hub.cpp): what each
// is, how it is found on this machine and how it can be installed here:
//   winget     Windows Package Manager by its package ID (it checks the installer's hash itself);
//   installer  the vendor's own Windows installer over https, its Authenticode signature checked,
//              then started with its own window (src/store/launcher_install.cpp);
//   flatpak    Linux: the native build from Flathub, for this user;
//   wine       Linux: the vendor's Windows installer inside a Wine prefix the user picks;
//   page       its download page, when nothing above can install it.
// Everything here is pure (the machine's facts come through Probe), so it is unit-tested offline.
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace launchers {
enum class Group { stores, emulation };

struct Entry {
    std::string_view id;
    std::wstring_view name;
    std::wstring_view image;       // "logo:<platformLogo name>", "system:<assets/systems file>", empty: its name as the tile's word.
    uint32_t tone = 0;             // The tile's colour, 0xRRGGBB.
    Group group = Group::stores;
    const wchar_t* about = L"";    // Polish; a tr() key.
    std::string_view winget;       // Package ID; empty: not in winget.
    std::wstring_view installer;   // The vendor's Windows installer (https); empty: none.
    std::wstring_view file;        // The name it is saved under ("SteamSetup.exe").
    std::string_view flatpak;      // Flathub ID of the native Linux build; empty: none.
    std::wstring_view page;        // Its download page.
    std::array<std::wstring_view, 3> programs; // Windows: where it installs, %NAME% expanded.
    std::wstring_view registry_key, registry_value, registry_file; // Windows: a folder (plus this file) or the program itself.
    std::array<std::wstring_view, 2> commands; // Linux: its programs on PATH.
};
std::span<const Entry> catalog();
const Entry* find(std::string_view id);

enum class Method { winget, installer, flatpak, wine, page };
struct Machine {
    bool windows = true;
    bool winget = false;  // Windows: winget.exe answers.
    bool flatpak = false; // Linux: flatpak is installed.
    bool wine = false;    // Linux: umu, a Proton or Wine is there to run the installer.
};
// What can install it here, best first; empty: it is not offered on this system.
std::vector<Method> methods(const Entry& entry, const Machine& machine);

// winget's arguments for a quiet install of this exact package.
std::vector<std::wstring> wingetArguments(const Entry& entry);
// winget's exit codes that mean it is there already (nothing to do), as success.
bool wingetAlreadyInstalled(unsigned long exit_code);
// flatpak's argument lists, in order: the user's Flathub remote, then the install.
std::vector<std::vector<std::wstring>> flatpakCommands(const Entry& entry);
// Removing one: winget's and flatpak's arguments (the user's own Flatpak installation).
std::vector<std::wstring> wingetUninstallArguments(const Entry& entry);
std::vector<std::wstring> flatpakUninstallArguments(const Entry& entry);
// The vendor hosts an installer may come from (and be redirected to): https and one of the
// catalogue's own hosts or their CDNs. Anything else is refused before a byte is run.
bool trustedInstallerUrl(std::wstring_view url);

// The machine's facts detection reads; systemProbe() is this machine's, tests fill their own.
struct Probe {
    bool windows = true;
    std::function<std::wstring(std::wstring_view name)> variable;                           // %NAME%; empty: unknown.
    std::function<bool(const std::filesystem::path& file)> exists;
    std::function<std::wstring(std::wstring_view key, std::wstring_view value)> registry;   // HKCU, then HKLM (both views).
    std::function<std::filesystem::path(std::wstring_view command)> which;                  // Linux: on PATH.
    std::vector<std::filesystem::path> flatpak_apps;                                        // Linux: .../flatpak/app folders.
    std::vector<std::wstring> prefix_users;                                                 // Linux: user folders of a prefix ("steamuser", the login).
    std::function<std::filesystem::path(std::string_view id)> wine_prefix;                  // Linux: the prefix it was installed into here.
};
Probe systemProbe(const std::filesystem::path& config);

struct Found {
    std::filesystem::path program;   // What starts it (Linux Flatpak: flatpak itself).
    std::wstring arguments;          // "run <id>" for a Flatpak.
    std::filesystem::path prefix;    // Linux: the Wine prefix a Windows build lives in; empty: native.
};
std::optional<Found> detect(const Entry& entry, const Probe& probe);
// What can remove an installed launcher here: winget on Windows (when it knows the package), flatpak for
// a Flatpak; nothing for a Windows build in a Wine prefix (its own uninstaller, or the prefix, is the user's).
enum class Removal { none, winget, flatpak };
Removal removal(const Entry& entry, const Machine& machine, const Found& found);
// %NAME% parts replaced; empty when a variable is unknown.
std::wstring expand(std::wstring_view path, const Probe& probe);
// A Windows program path of the catalogue inside a prefix, for one of its user folders.
std::filesystem::path prefixPath(std::wstring_view program, const std::filesystem::path& prefix, std::wstring_view user);

// Linux: the prefixes Windows launchers were installed into, <config>/store-hub/wine.json
// ({"version":1,"prefixes":{"<id>":"/abs/prefix"}}).
std::filesystem::path winePrefix(const std::filesystem::path& config, std::string_view id);
bool rememberWinePrefix(const std::filesystem::path& config, std::string_view id, const std::filesystem::path& prefix);
}
