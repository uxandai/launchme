#include "launchers.hpp"
#include "../i18n/i18n.hpp"
#include "../net/json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/file_io.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cstdlib>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace launchers {
namespace {
// id, name, image, tone, group, about, winget, installer, file, flatpak, page, programs, registry (key, value, file), commands.
constexpr Entry entries[] = {
    {"steam", L"Steam", L"logo:Steam", 0x1B2838, Group::stores, trKey(L"Sklep i klient Valve: największa biblioteka gier na PC."),
     "Valve.Steam", L"https://cdn.akamai.steamstatic.com/client/installer/SteamSetup.exe", L"SteamSetup.exe",
     "com.valvesoftware.Steam", L"https://store.steampowered.com/about/",
     {L"%ProgramFiles(x86)%\\Steam\\steam.exe", L"%ProgramFiles%\\Steam\\steam.exe", L""},
     L"Software\\Valve\\Steam", L"InstallPath", L"steam.exe", {L"steam", L""}},
    {"gog", L"GOG Galaxy", L"logo:GOG", 0x4B1F6F, Group::stores, trKey(L"Klient GOG: gry bez DRM, osiągnięcia i zapisy w chmurze."),
     "GOG.Galaxy", L"https://webinstallers.gog-statics.com/download/GOG_Galaxy_2.0.exe", L"GOG_Galaxy_2.0.exe",
     "", L"https://www.gog.com/galaxy",
     {L"%ProgramFiles(x86)%\\GOG Galaxy\\GalaxyClient.exe", L"", L""},
     L"Software\\GOG.com\\GalaxyClient\\paths", L"client", L"GalaxyClient.exe", {L"", L""}},
    {"epic", L"Epic Games Launcher", L"logo:Epic Games", 0x2A2A2E, Group::stores, trKey(L"Sklep Epic Games: co tydzień darmowa gra."),
     "EpicGames.EpicGamesLauncher", L"https://launcher-public-service-prod06.ol.epicgames.com/launcher/api/installer/download/EpicGamesLauncherInstaller.msi",
     L"EpicGamesLauncherInstaller.msi", "", L"https://store.epicgames.com/download",
     {L"%ProgramFiles(x86)%\\Epic Games\\Launcher\\Portal\\Binaries\\Win64\\EpicGamesLauncher.exe",
      L"%ProgramFiles(x86)%\\Epic Games\\Launcher\\Portal\\Binaries\\Win32\\EpicGamesLauncher.exe",
      L"%ProgramFiles%\\Epic Games\\Launcher\\Portal\\Binaries\\Win64\\EpicGamesLauncher.exe"},
     L"", L"", L"", {L"", L""}},
    {"ea", L"EA app", L"logo:EA Play", 0x8E1B16, Group::stores, trKey(L"Klient Electronic Arts i abonament EA Play."),
     "ElectronicArts.EADesktop", L"https://origin-a.akamaihd.net/EA-Desktop-Client-Download/installer-releases/EAappInstaller.exe", L"EAappInstaller.exe",
     "", L"https://www.ea.com/ea-app",
     {L"%ProgramFiles%\\Electronic Arts\\EA Desktop\\EA Desktop\\EADesktop.exe", L"%ProgramFiles%\\Electronic Arts\\EA Desktop\\EA Desktop\\EALauncher.exe", L""},
     L"", L"", L"", {L"", L""}},
    {"ubisoft", L"Ubisoft Connect", L"logo:Ubisoft Connect", 0x143C8C, Group::stores, trKey(L"Klient Ubisoftu: jego gry i abonament Ubisoft+."),
     "Ubisoft.Connect", L"https://static3.cdn.ubi.com/orbit/launcher_installer/UbisoftConnectInstaller.exe", L"UbisoftConnectInstaller.exe",
     "", L"https://www.ubisoft.com/ubisoft-connect",
     {L"%ProgramFiles(x86)%\\Ubisoft\\Ubisoft Game Launcher\\UbisoftConnect.exe", L"%ProgramFiles(x86)%\\Ubisoft\\Ubisoft Game Launcher\\upc.exe", L""},
     L"Software\\Ubisoft\\Launcher", L"InstallDir", L"UbisoftConnect.exe", {L"", L""}},
    {"battlenet", L"Battle.net", L"logo:Battle.net", 0x0B3A75, Group::stores, trKey(L"Klient Blizzarda: Diablo, Overwatch, World of Warcraft, Call of Duty."),
     "", L"https://downloader.battle.net/download/getInstaller?os=win&installer=Battle.net-Setup.exe", L"Battle.net-Setup.exe",
     "", L"https://download.battle.net/",
     {L"%ProgramFiles(x86)%\\Battle.net\\Battle.net Launcher.exe", L"%ProgramFiles(x86)%\\Battle.net\\Battle.net.exe", L""},
     L"", L"", L"", {L"", L""}},
    {"amazon", L"Amazon Games", L"", 0x232F3E, Group::stores, trKey(L"Gry z Prime Gaming: co miesiąc kilka za darmo dla abonentów."),
     "Amazon.Games", L"https://download.amazongames.com/AmazonGamesSetup.exe", L"AmazonGamesSetup.exe",
     "", L"https://gaming.amazon.com/amazon-games-app",
     {L"%LOCALAPPDATA%\\Amazon Games\\App\\Amazon Games.exe", L"", L""},
     L"", L"", L"", {L"", L""}},
    {"heroic", L"Heroic Games Launcher", L"", 0x1F2B4D, Group::stores, trKey(L"Otwarty klient gier z Epic, GOG i Amazon, także na Linuksie."),
     "HeroicGamesLauncher.HeroicGamesLauncher", L"", L"", "com.heroicgameslauncher.hgl", L"https://heroicgameslauncher.com/downloads",
     {L"%LOCALAPPDATA%\\Programs\\heroic\\Heroic.exe", L"", L""},
     L"", L"", L"", {L"heroic", L""}},
    {"lutris", L"Lutris", L"", 0x6B3F10, Group::stores, trKey(L"Menedżer gier na Linuksie: instalatory społeczności dla gier z Windows."),
     "", L"", L"", "net.lutris.Lutris", L"https://lutris.net/downloads",
     {L"", L"", L""}, L"", L"", L"", {L"lutris", L""}},
    {"prism", L"Prism Launcher", L"", 0x2F6B45, Group::stores, trKey(L"Minecraft: wiele instancji, wersji i paczek modów."),
     "PrismLauncher.PrismLauncher", L"", L"", "org.prismlauncher.PrismLauncher", L"https://prismlauncher.org/download/",
     {L"%LOCALAPPDATA%\\Programs\\PrismLauncher\\prismlauncher.exe", L"", L""},
     L"", L"", L"", {L"prismlauncher", L""}},
    {"retroarch", L"RetroArch", L"system:retroarch.png", 0x1C1C1F, Group::emulation, trKey(L"Jeden program do emulacji wielu konsol (rdzenie libretro)."),
     "Libretro.RetroArch", L"", L"", "org.libretro.RetroArch", L"https://www.retroarch.com/?page=platforms",
     {L"%SystemDrive%\\RetroArch-Win64\\retroarch.exe", L"%APPDATA%\\RetroArch\\retroarch.exe", L""},
     L"", L"", L"", {L"retroarch", L""}},
    {"esde", L"ES-DE", L"", 0x8F1D1D, Group::emulation, trKey(L"Nakładka na emulatory: biblioteka ROM-ów z okładkami i filmami."),
     "", L"", L"", "", L"https://es-de.org/#Download",
     {L"%ProgramFiles%\\ES-DE\\ES-DE.exe", L"%LOCALAPPDATA%\\Programs\\ES-DE\\ES-DE.exe", L""},
     L"", L"", L"", {L"es-de", L"ES-DE"}},
};

// Hosts an installer may be served from: each entry's own, and the CDN its vendor redirects to.
constexpr std::wstring_view installer_hosts[] = {
    L"cdn.akamai.steamstatic.com", L"cdn.cloudflare.steamstatic.com", L"webinstallers.gog-statics.com",
    L"launcher-public-service-prod06.ol.epicgames.com", L"epicgames-download1.akamaized.net", L"origin-a.akamaihd.net",
    L"static3.cdn.ubi.com", L"downloader.battle.net", L"download.amazongames.com",
};

std::wstring lower(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + (L'a' - L'A'));
    return out;
}
bool present(const Probe& probe, const std::filesystem::path& file) { return !file.empty() && probe.exists && probe.exists(file); }
}

std::span<const Entry> catalog() { return entries; }

const Entry* find(std::string_view id) {
    for (const auto& entry : entries) if (entry.id == id) return &entry;
    return nullptr;
}

std::vector<Method> methods(const Entry& entry, const Machine& machine) {
    std::vector<Method> result;
    if (machine.windows) {
        if (entry.winget.empty() && entry.installer.empty() && entry.programs[0].empty()) return {}; // A Linux-only program.
        if (machine.winget && !entry.winget.empty()) result.push_back(Method::winget);
        if (!entry.installer.empty()) result.push_back(Method::installer);
    } else {
        if (machine.flatpak && !entry.flatpak.empty()) result.push_back(Method::flatpak);
        if (machine.wine && !entry.installer.empty()) result.push_back(Method::wine);
        if (entry.flatpak.empty() && entry.installer.empty() && entry.commands[0].empty()) return {}; // A Windows-only program.
    }
    if (!entry.page.empty()) result.push_back(Method::page);
    return result;
}

std::vector<std::wstring> wingetArguments(const Entry& entry) {
    return {L"install", L"--id", text_encoding::wide(entry.winget), L"--exact", L"--source", L"winget", L"--silent",
            L"--accept-package-agreements", L"--accept-source-agreements", L"--disable-interactivity"};
}

bool wingetAlreadyInstalled(unsigned long exit_code) {
    // APPINSTALLER_CLI_ERROR_UPDATE_NOT_APPLICABLE and _PACKAGE_ALREADY_INSTALLED.
    return exit_code == 0x8A15002Bul || exit_code == 0x8A150061ul;
}

std::vector<std::vector<std::wstring>> flatpakCommands(const Entry& entry) {
    if (entry.flatpak.empty()) return {};
    return {{L"remote-add", L"--user", L"--if-not-exists", L"flathub", L"https://dl.flathub.org/repo/flathub.flatpakrepo"},
            {L"install", L"--user", L"--noninteractive", L"-y", L"flathub", text_encoding::wide(entry.flatpak)}};
}

std::vector<std::wstring> wingetUninstallArguments(const Entry& entry) {
    return {L"uninstall", L"--id", text_encoding::wide(entry.winget), L"--exact", L"--silent", L"--accept-source-agreements", L"--disable-interactivity"};
}

std::vector<std::wstring> flatpakUninstallArguments(const Entry& entry) {
    return {L"uninstall", L"--user", L"--noninteractive", L"-y", text_encoding::wide(entry.flatpak)};
}

Removal removal(const Entry& entry, const Machine& machine, const Found& found) {
    if (machine.windows) return machine.winget && !entry.winget.empty() ? Removal::winget : Removal::none;
    return machine.flatpak && !entry.flatpak.empty() && found.prefix.empty() && found.program == L"flatpak" ? Removal::flatpak : Removal::none;
}

bool trustedInstallerUrl(std::wstring_view url) {
    constexpr std::wstring_view scheme = L"https://";
    if (!url.starts_with(scheme) || url.size() > 2048 || url.find_first_of(std::wstring_view(L"\0\r\n\" \\@", 7)) != url.npos) return false;
    const auto rest = url.substr(scheme.size());
    const auto host = lower(rest.substr(0, rest.find_first_of(L"/?#:")));
    return std::any_of(std::begin(installer_hosts), std::end(installer_hosts), [&](std::wstring_view allowed) { return host == allowed; });
}

std::wstring expand(std::wstring_view path, const Probe& probe) {
    std::wstring out;
    for (size_t at = 0; at < path.size();) {
        if (path[at] != L'%') { out += path[at++]; continue; }
        const auto end = path.find(L'%', at + 1);
        if (end == path.npos) return {};
        const auto value = probe.variable ? probe.variable(path.substr(at + 1, end - at - 1)) : std::wstring{};
        if (value.empty()) return {};
        out += value;
        at = end + 1;
    }
    return out;
}

std::filesystem::path prefixPath(std::wstring_view program, const std::filesystem::path& prefix, std::wstring_view user) {
    struct Folder { std::wstring_view variable; std::wstring place; };
    const Folder folders[] = {
        {L"%ProgramFiles(x86)%\\", L"Program Files (x86)/"}, {L"%ProgramFiles%\\", L"Program Files/"}, {L"%SystemDrive%\\", L""},
        {L"%LOCALAPPDATA%\\", L"users/" + std::wstring(user) + L"/AppData/Local/"}, {L"%APPDATA%\\", L"users/" + std::wstring(user) + L"/AppData/Roaming/"},
    };
    for (const auto& folder : folders) {
        if (!program.starts_with(folder.variable)) continue;
        auto rest = folder.place + std::wstring(program.substr(folder.variable.size()));
        std::replace(rest.begin(), rest.end(), L'\\', L'/');
        return prefix / L"drive_c" / std::filesystem::path(rest);
    }
    return {};
}

std::optional<Found> detect(const Entry& entry, const Probe& probe) {
    if (probe.windows) {
        if (!entry.registry_key.empty() && probe.registry) {
            const auto value = probe.registry(entry.registry_key, entry.registry_value);
            if (!value.empty()) {
                const std::filesystem::path place(value);
                // The value is the program itself (GOG's "client" is its folder; Steam's SteamExe its file).
                for (const auto& file : {place / std::wstring(entry.registry_file), place})
                    if (lower(file.extension().wstring()) == L".exe" && present(probe, file)) return Found{file, {}, {}};
            }
        }
        for (const auto program : entry.programs) {
            if (program.empty()) continue;
            const std::filesystem::path file(expand(program, probe));
            if (present(probe, file)) return Found{file, {}, {}};
        }
        return std::nullopt;
    }
    if (!entry.flatpak.empty())
        for (const auto& apps : probe.flatpak_apps)
            if (present(probe, apps / std::wstring(entry.flatpak.begin(), entry.flatpak.end())))
                return Found{L"flatpak", L"run " + std::wstring(entry.flatpak.begin(), entry.flatpak.end()), {}};
    if (probe.which)
        for (const auto command : entry.commands) {
            if (command.empty()) continue;
            if (const auto file = probe.which(command); !file.empty()) return Found{file, {}, {}};
        }
    if (probe.wine_prefix)
        if (const auto prefix = probe.wine_prefix(entry.id); !prefix.empty())
            for (const auto program : entry.programs)
                for (const auto& user : probe.prefix_users) {
                    const auto file = program.empty() ? std::filesystem::path{} : prefixPath(program, prefix, user);
                    if (present(probe, file)) return Found{file, {}, prefix};
                }
    return std::nullopt;
}

namespace {
std::filesystem::path wineFile(const std::filesystem::path& config) { return config / L"store-hub" / L"wine.json"; }
}

std::filesystem::path winePrefix(const std::filesystem::path& config, std::string_view id) {
    const auto document = json::parse(file_io::readText(wineFile(config), 256 * 1024));
    if (!document || document->type != json::Value::Type::object) return {};
    const auto* prefixes = document->get("prefixes");
    const auto* found = prefixes ? prefixes->get(id) : nullptr;
    if (!found || found->type != json::Value::Type::string) return {};
    const std::filesystem::path prefix(text_encoding::wideLenient(found->text));
    return prefix.is_absolute() ? prefix : std::filesystem::path{};
}

bool rememberWinePrefix(const std::filesystem::path& config, std::string_view id, const std::filesystem::path& prefix) {
    if (!find(id) || !prefix.is_absolute()) return false;
    auto document = json::parse(file_io::readText(wineFile(config), 256 * 1024)).value_or(json::object());
    if (document.type != json::Value::Type::object) document = json::object();
    document.properties["version"] = json::number(1);
    auto& prefixes = document.properties["prefixes"];
    if (prefixes.type != json::Value::Type::object) prefixes = json::object();
    prefixes.properties[std::string(id)] = json::string(text_encoding::utf8Lenient(prefix.wstring()));
    std::error_code ec;
    std::filesystem::create_directories(wineFile(config).parent_path(), ec);
    return atomic_file::write(wineFile(config), json::serialize(document));
}

Probe systemProbe(const std::filesystem::path& config) {
    Probe probe;
    probe.exists = [](const std::filesystem::path& file) { std::error_code ec; return std::filesystem::exists(file, ec); };
#ifdef _WIN32
    probe.windows = true;
    probe.variable = [](std::wstring_view name) {
        wchar_t value[32768];
        const auto size = GetEnvironmentVariableW(std::wstring(name).c_str(), value, static_cast<DWORD>(std::size(value)));
        return size && size < std::size(value) ? std::wstring(value, size) : std::wstring{};
    };
    probe.registry = [](std::wstring_view key, std::wstring_view value) {
        const std::wstring subkey(key), name(value);
        struct Place { HKEY root; DWORD view; };
        for (const auto& place : {Place{HKEY_CURRENT_USER, 0}, Place{HKEY_LOCAL_MACHINE, RRF_SUBKEY_WOW6464KEY}, Place{HKEY_LOCAL_MACHINE, RRF_SUBKEY_WOW6432KEY}}) {
            wchar_t text[4096];
            DWORD bytes = sizeof(text);
            if (RegGetValueW(place.root, subkey.c_str(), name.c_str(), RRF_RT_REG_SZ | place.view, nullptr, text, &bytes) == ERROR_SUCCESS && bytes >= sizeof(wchar_t))
                return std::wstring(text);
        }
        return std::wstring{};
    };
    (void)config;
#else
    probe.windows = false;
    const auto environment = [](const char* name) { const char* value = std::getenv(name); return std::string(value ? value : ""); };
    const std::filesystem::path home(environment("HOME"));
    probe.flatpak_apps = {"/var/lib/flatpak/app", home / ".local/share/flatpak/app"};
    probe.prefix_users = {L"steamuser"};
    if (const auto user = environment("USER"); !user.empty()) probe.prefix_users.push_back(text_encoding::wideLenient(user));
    probe.which = [path = environment("PATH")](std::wstring_view command) {
        const auto name = text_encoding::utf8Lenient(command);
        for (size_t at = 0; at <= path.size();) {
            const auto end = std::min(path.find(':', at), path.size());
            const auto file = std::filesystem::path(path.substr(at, end - at)) / name;
            std::error_code ec;
            if (end > at && std::filesystem::is_regular_file(file, ec)) return file;
            at = end + 1;
        }
        return std::filesystem::path{};
    };
    probe.wine_prefix = [config](std::string_view id) { return winePrefix(config, id); };
#endif
    return probe;
}
}
