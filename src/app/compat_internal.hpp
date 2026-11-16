#pragma once
// Linux: helpers shared by the Proton i Wine settings files (compat_settings.cpp,
// compat_workers.cpp, compat_rows.cpp, compat_activate.cpp): text and path keys, a game's default
// options, the accordion's sections and the store launchers installed with a click.
#ifndef _WIN32
#include "launcher.hpp"
#include "../model/launch_options_catalog.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/wine_runner.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace compat_detail {
namespace catalog = launch_options_catalog;
namespace store = launch_options_store;
inline std::wstring wide(std::string_view value) { return text_encoding::wideLenient(value); }
inline std::string narrow(const std::wstring& value) { return text_encoding::utf8Lenient(value); }
inline std::wstring label(const catalog::Text& value) { return catalog::text(value); }
inline std::string keyOf(const std::filesystem::path& path) { return narrow(path.wstring()); }
inline std::filesystem::path pathOf(const std::string& key) { return std::filesystem::path(wide(key)); }
inline bool chosen(const std::string& tool) { return !tool.empty() && tool != "default"; }

// A game's options before it has its own: the default ones, with the tool and the prefix left to its
// prefix and the global choice.
inline LaunchOptions gameDefaults() {
    auto options = store::defaults();
    options.compat_tool = "default";
    options.prefix.clear();
    return options;
}

// The warning when a prefix made by Proton would run with a Wine build (or the other way round): `tool_id`
// as stored ("default": the automatic choice). Empty when they agree or either is unknown.
inline std::wstring runnerWarning(const std::vector<compat_tools::Tool>& tools, const std::string& tool_id, const std::filesystem::path& prefix) {
    const auto tool = compat_tools::resolve(tools, tool_id);
    if (!wine_runner::runnerMismatch(prefix, tool)) return {};
    return wine_runner::prefixRunner(prefix) == wine_runner::Runner::proton ?
        tr(L"Uwaga: ten prefiks utworzył Proton, a wybrana wersja to Wine. Może go uszkodzić: lepiej użyć osobnego prefiksu.") :
        tr(L"Uwaga: ten prefiks utworzyło Wine, a wybrana wersja to Proton. Może go uszkodzić: lepiej użyć osobnego prefiksu.");
}

// The accordion's sections: the catalog's, with the DLL overrides (from Proton i Wine) of their own.
struct Fold {
    const char* id;
    std::optional<catalog::Section> section;
    const wchar_t* title;
    const wchar_t* description;
};
inline constexpr Fold folds[] = {
    {"runner", catalog::Section::runner, trKey(L"Proton / Wine i prefiks"), trKey(L"Wersja Protona lub Wine, prefiks gry i umu-launcher.")},
    {"proton", catalog::Section::proton, trKey(L"Proton i Wine"), trKey(L"Synchronizacja, DirectX, HDR, FSR, pady, dźwięk i logi.")},
    {"extras", catalog::Section::extras, trKey(L"Dodatki i obejścia"),
     trKey(L"Niskie opóźnienie, OptiScaler, filmiki, stare DirectX, okna i dźwięk (Proton-CachyOS, GE-Proton).")},
    {"dll", std::nullopt, trKey(L"Biblioteki DLL"), trKey(L"Mody, ReShade i ładowarki ASI (WINEDLLOVERRIDES).")},
    {"nvidia", catalog::Section::nvidia, L"NVIDIA", trKey(L"DLSS, Reflex, laptopy z PRIME i pamięć shaderów.")},
    {"mesa", catalog::Section::mesa, trKey(L"AMD i Mesa"), trKey(L"RADV, Vulkan i OpenGL na kartach AMD i Intel.")},
    {"wrappers", catalog::Section::wrappers, trKey(L"Nakładki i wrappery"),
     trKey(L"GameMode, MangoHud, generowanie klatek lsfg-vk, gamescope i własne polecenie.")},
    {"gamescope", catalog::Section::gamescope, L"Gamescope", trKey(L"Rozdzielczość, skalowanie i limit klatek gamescope.")},
    {"game", catalog::Section::game, trKey(L"Zmienne i argumenty"), trKey(L"Zmienne środowiska, argumenty, folder roboczy i skrypty.")},
};

// Store launchers installed with a click (Sklepy i launchery): the official installer, downloaded with curl
// and run in a prefix named after the launcher; the program it installs (the first of `programs` under
// drive_c that exists) is then added to the library as a game in that prefix.
struct StoreInstaller {
    const char* id;
    const wchar_t* name;
    const wchar_t* description; // trKey
    const wchar_t* url;
    const wchar_t* file;
    const wchar_t* programs[3];
};
inline constexpr StoreInstaller store_installers[] = {
    {"battlenet", L"Battle.net", trKey(L"Blizzard i Activision: World of Warcraft, Diablo, Overwatch, Call of Duty."),
     L"https://downloader.battle.net/download/getInstaller?os=win&installer=Battle.net-Setup.exe", L"Battle.net-Setup.exe",
     {L"Program Files (x86)/Battle.net/Battle.net Launcher.exe", L"Program Files (x86)/Battle.net/Battle.net.exe", nullptr}},
    {"ea", L"EA app", trKey(L"Gry Electronic Arts i EA Play."),
     L"https://origin-a.akamaihd.net/EA-Desktop-Client-Download/installer-releases/EAappInstaller.exe", L"EAappInstaller.exe",
     {L"Program Files/Electronic Arts/EA Desktop/EA Desktop/EALauncher.exe", L"Program Files/Electronic Arts/EA Desktop/EA Desktop/EADesktop.exe", nullptr}},
    {"epic", L"Epic Games Launcher", trKey(L"Gry Epic, których nie uruchamia Legendary (np. z własnym launcherem albo anti-cheatem)."),
     L"https://launcher-public-service-prod06.ol.epicgames.com/launcher/api/installer/download/EpicGamesLauncherInstaller.msi", L"EpicGamesLauncherInstaller.msi",
     {L"Program Files (x86)/Epic Games/Launcher/Portal/Binaries/Win64/EpicGamesLauncher.exe",
      L"Program Files (x86)/Epic Games/Launcher/Portal/Binaries/Win32/EpicGamesLauncher.exe", nullptr}},
    {"ubisoft", L"Ubisoft Connect", trKey(L"Gry Ubisoftu, także kupione w innych sklepach."),
     L"https://static3.cdn.ubi.com/orbit/launcher_installer/UbisoftConnectInstaller.exe", L"UbisoftConnectInstaller.exe",
     {L"Program Files (x86)/Ubisoft/Ubisoft Game Launcher/UbisoftConnect.exe", L"Program Files (x86)/Ubisoft/Ubisoft Game Launcher/upc.exe", nullptr}},
    {"rockstar", L"Rockstar Games Launcher", trKey(L"GTA, Red Dead Redemption i inne gry Rockstar."),
     L"https://gamedownloads.rockstargames.com/public/installer/Rockstar-Games-Launcher.exe", L"Rockstar-Games-Launcher.exe",
     {L"Program Files/Rockstar Games/Launcher/Launcher.exe", nullptr, nullptr}},
    {"gog", L"GOG Galaxy", trKey(L"Klient GOG: gry z nakładkami i osiągnięciami Galaxy, zapisy w chmurze."),
     L"https://webinstallers.gog-statics.com/download/GOG_Galaxy_2.0.exe", L"GOG_Galaxy_2.0.exe",
     {L"Program Files (x86)/GOG Galaxy/GalaxyClient.exe", nullptr, nullptr}},
};
} // namespace compat_detail
#endif
