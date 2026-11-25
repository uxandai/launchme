#include "pc_platforms.hpp"
#include "../platform/file_io.hpp"
#include "battlenet.hpp"
#include "ubisoft.hpp"
#include "../net/json.hpp"
#include "../platform/text_encoding.hpp"

#ifdef _WIN32
#include <Windows.h>
#include <appmodel.h>
#include <objbase.h>
#include <shellapi.h>
#else
#include "heroic.hpp"
#include "lutris.hpp"
#endif
#include <algorithm>
#include <cwctype>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <set>

namespace {
// Store manifests and registry-derived text: strict, malformed input becomes empty.
std::wstring wide(std::string_view value) { return text_encoding::wide(value); }
std::string utf8(const std::wstring& value) { return text_encoding::utf8(value); }
std::wstring lower(std::wstring value) { std::transform(value.begin(), value.end(), value.begin(), towlower); return value; }
bool directory(const std::filesystem::path& path) { std::error_code ec; return !path.empty() && path.is_absolute() && std::filesystem::is_directory(path, ec); }
bool file(const std::filesystem::path& path) { std::error_code ec; return !path.empty() && path.is_absolute() && std::filesystem::is_regular_file(path, ec); }
std::string readFile(const std::filesystem::path& path, size_t limit = 2 * 1024 * 1024) { return file_io::readText(path, limit); }
std::string textFile(const std::filesystem::path& path) {
    auto bytes = readFile(path);
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.erase(0, 3);
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff && static_cast<unsigned char>(bytes[1]) == 0xfe) {
        if (bytes.size() % 2) return {};
        std::wstring text((bytes.size() - 2) / 2, L'\0');
        std::memcpy(text.data(), bytes.data() + 2, bytes.size() - 2);
        return utf8(text);
    }
    return bytes;
}
std::string field(const json::Value& value, const char* key) { const auto* found = value.get(key); return found ? found->stringOr() : std::string{}; }
std::wstring cleanName(std::wstring name) {
    std::erase_if(name, [](wchar_t ch) { return ch == L'®' || ch == L'™' || ch == L'©'; });
    while (!name.empty() && iswspace(name.back())) name.pop_back();
    return name;
}
std::wstring client(const std::vector<StoreClient>& clients, GameSource source) {
    for (const auto& entry : clients) if (entry.source == source && file(entry.executable)) return entry.executable.wstring();
    return {};
}
std::wstring client(const PcDiscovery& input, GameSource source) { return client(input.clients, source); }
Game baseGame(GameSource source, std::string id, std::wstring name) {
    Game game;
    game.source = source;
    game.source_id = std::move(id);
    game.kind = GameKind::store;
    game.name = cleanName(std::move(name));
    return game;
}
std::string xmlContentId(std::string_view text) {
    size_t begin = 0;
    while ((begin = text.find("<contentID", begin)) != std::string_view::npos) {
        begin += 10;
        if (begin >= text.size() || (text[begin] != '>' && text[begin] != ' ' && text[begin] != '\t')) continue;
        begin = text.find('>', begin);
        if (begin == std::string_view::npos) return {};
        const auto end = text.find("</contentID>", ++begin);
        if (end == std::string_view::npos || end - begin > 96) return {};
        auto value = text.substr(begin, end - begin);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
        return std::string(value);
    }
    return {};
}
#ifdef _WIN32
struct Key {
    HKEY handle = nullptr;
    Key(HKEY root, const std::wstring& path, REGSAM view) { RegOpenKeyExW(root, path.c_str(), 0, KEY_READ | view, &handle); }
    ~Key() { if (handle) RegCloseKey(handle); }
};
std::wstring regString(HKEY root, const wchar_t* value) {
    if (!root) return {};
    wchar_t buffer[4096]{};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, nullptr, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return {};
    return buffer;
}
std::vector<std::wstring> children(HKEY root) {
    std::vector<std::wstring> result;
    if (!root) return result;
    for (DWORD i = 0; i < 10000; ++i) {
        wchar_t name[512]{};
        DWORD length = static_cast<DWORD>(std::size(name));
        const auto status = RegEnumKeyExW(root, i, name, &length, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status == ERROR_SUCCESS) result.emplace_back(name, length);
    }
    return result;
}
std::filesystem::path environment(const wchar_t* name) {
    wchar_t value[4096]{};
    const auto count = GetEnvironmentVariableW(name, value, 4096);
    return count && count < 4096 ? std::filesystem::path(value) : std::filesystem::path{};
}
std::filesystem::path protocolClient(const wchar_t* scheme) {
    Key command(HKEY_CLASSES_ROOT, std::wstring(scheme) + L"\\shell\\open\\command", 0);
    const auto text = regString(command.handle, nullptr);
    if (text.empty()) return {};
    int count = 0;
    auto args = CommandLineToArgvW(text.c_str(), &count);
    std::filesystem::path result;
    if (args) { if (count > 0 && file(args[0])) result = args[0]; LocalFree(args); }
    return result;
}
// Packaged apps (Xbox / Microsoft Store): installed full names of a family for the current user.
std::vector<std::wstring> familyPackages(const std::wstring& family) {
    UINT32 count = 0, length = 0;
    const auto status = GetPackagesByPackageFamily(family.c_str(), &count, nullptr, &length, nullptr);
    if ((status != ERROR_INSUFFICIENT_BUFFER && status != ERROR_SUCCESS) || !count || !length) return {};
    std::vector<PWSTR> names(count);
    std::vector<wchar_t> buffer(length);
    if (GetPackagesByPackageFamily(family.c_str(), &count, names.data(), &length, buffer.data()) != ERROR_SUCCESS) return {};
    std::vector<std::wstring> result;
    for (UINT32 i = 0; i < count && i < names.size(); ++i) if (names[i]) result.emplace_back(names[i]);
    return result;
}
std::filesystem::path packagePath(const std::wstring& full_name) {
    UINT32 length = 0;
    if (GetPackagePathByFullName(full_name.c_str(), &length, nullptr) != ERROR_INSUFFICIENT_BUFFER || !length) return {};
    std::wstring path(length, L'\0');
    if (GetPackagePathByFullName(full_name.c_str(), &length, path.data()) != ERROR_SUCCESS) return {};
    path.resize(wcsnlen(path.c_str(), path.size()));
    return path;
}
std::wstring packageFamily(const std::wstring& full_name) {
    wchar_t family[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1]{};
    UINT32 length = PACKAGE_FAMILY_NAME_MAX_LENGTH + 1;
    return PackageFamilyNameFromFullName(full_name.c_str(), &length, family) == ERROR_SUCCESS ? family : L"";
}
// "ms-resource:" display names are resolved against the installed package (shlwapi, loaded on demand).
std::wstring packageString(const std::wstring& full_name, const std::wstring& value) {
    if (!value.starts_with(L"ms-resource:")) return value;
    using Load = HRESULT(WINAPI*)(PCWSTR, PWSTR, UINT, void**);
    const auto module = LoadLibraryExW(L"shlwapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return {};
    std::wstring result;
    if (const auto load = reinterpret_cast<Load>(GetProcAddress(module, "SHLoadIndirectString"))) {
        wchar_t text[512]{};
        if (SUCCEEDED(load((L"@{" + full_name + L"?" + value + L"}").c_str(), text, static_cast<UINT>(std::size(text)), nullptr))) result = text;
    }
    FreeLibrary(module);
    return result;
}
#endif
constexpr const wchar_t* xbox_app_family = L"Microsoft.GamingApp_8wekyb3d8bbwe";
#ifdef _WIN32
// Gaming Services registers every GDK title under GameConfig\<package full name>; entries of
// removed games stay behind, so only families installed for this user are kept.
std::vector<XboxInstall> discoverXboxGames() {
    std::vector<XboxInstall> result;
    std::set<std::wstring> families;
    Key configs(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\GamingServices\\GameConfig", KEY_WOW64_64KEY);
    for (const auto& full_name : children(configs.handle)) {
        const auto family = packageFamily(full_name);
        if (family.empty() || families.contains(family)) continue;
        const auto installed = familyPackages(family);
        if (installed.empty()) continue;
        families.insert(family);
        Key entry(configs.handle, full_name, 0);
        Key visuals(entry.handle, L"ShellVisuals", 0);
        Key executable(entry.handle, L"Executable\\00000000", 0);
        auto name = packageString(installed.front(), regString(visuals.handle, L"DefaultDisplayName"));
        if (name.empty()) name = regString(entry.handle, L"Name");
        result.push_back({utf8(family), std::move(name), regString(executable.handle, L"Id"), packagePath(installed.front())});
    }
    return result;
}
#endif
}

#ifdef _WIN32
PcDiscovery discoverPcInstallations(const std::vector<std::filesystem::path>& multimc_chosen) {
    PcDiscovery result;
    const auto data = environment(L"PROGRAMDATA");
    if (!data.empty()) {
        result.epic_manifests = data / L"Epic" / L"EpicGamesLauncher" / L"Data" / L"Manifests";
        result.epic_installed = data / L"Epic" / L"UnrealEngineLauncher" / L"LauncherInstalled.dat";
        result.battlenet_database = data / L"Battle.net" / L"Agent" / L"product.db";
        result.battlenet_aggregate = data / L"Battle.net" / L"Agent" / L"aggregate.json";
    }
    result.programs = installedPrograms();
    for (const auto root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) for (const REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
        for (const auto source : {GameSource::gog, GameSource::ubisoft, GameSource::ea}) {
            const auto path = source == GameSource::gog ? L"SOFTWARE\\GOG.com\\Games" :
                              source == GameSource::ubisoft ? L"SOFTWARE\\Ubisoft\\Launcher\\Installs" : L"SOFTWARE\\EA Games";
            Key installs(root, path, view);
            for (const auto& id : children(installs.handle)) {
                Key entry(installs.handle, id, 0);
                auto name = regString(entry.handle, source == GameSource::gog ? L"gameName" : L"DisplayName");
                auto folder = regString(entry.handle, source == GameSource::gog ? L"path" : source == GameSource::ea ? L"Install Dir" : L"InstallDir");
                result.installs.push_back({source, utf8(id), std::move(name), folder});
            }
        }
    }
    const auto pf = environment(L"ProgramFiles"), pf86 = environment(L"ProgramFiles(x86)");
    for (const auto source : pc_sources) {
        if (source == GameSource::xbox) {
            // The Xbox app is packaged: its EXE only marks presence, connectPcClient() activates it by ID.
            const auto app = familyPackages(xbox_app_family);
            const auto folder = app.empty() ? std::filesystem::path{} : packagePath(app.front());
            result.clients.push_back({source, !folder.empty() && file(folder / L"XboxPcApp.exe") ? folder / L"XboxPcApp.exe" : std::filesystem::path{}});
            result.xbox = discoverXboxGames();
            continue;
        }
        if (source == GameSource::multimc) {
            // No client of its own: the first launcher found opens (connectPcClient).
            result.multimc = multimc::discover(multimc_chosen);
            result.clients.push_back({source, result.multimc.empty() ? std::filesystem::path{} : result.multimc.front().executable});
            continue;
        }
        if (source == GameSource::amazon) {
            // Amazon's games go through Nile (src/store/amazon.cpp): no client to find (the defaults
            // below are Epic's).
            result.clients.push_back({source, {}});
            continue;
        }
        const auto scheme = source == GameSource::gog ? L"goggalaxy" : source == GameSource::battlenet ? L"battlenet" :
                            source == GameSource::ea ? L"origin2" : source == GameSource::ubisoft ? L"uplay" : L"com.epicgames.launcher";
        const auto protocol = protocolClient(scheme);
        std::filesystem::path executable;
        std::vector<std::filesystem::path> roots;
        for (const auto& base : {pf86, pf}) if (!base.empty()) {
            roots.push_back(base / (source == GameSource::gog ? L"GOG Galaxy" : source == GameSource::battlenet ? L"Battle.net" :
                source == GameSource::ea ? L"Electronic Arts\\EA Desktop\\EA Desktop" :
                source == GameSource::ubisoft ? L"Ubisoft\\Ubisoft Game Launcher" : L"Epic Games"));
        }
        for (const auto& program : result.programs) {
            const auto name = lower(program.name);
            if ((source == GameSource::gog && name.find(L"gog galaxy") != std::wstring::npos) ||
                (source == GameSource::battlenet && name == L"battle.net") ||
                (source == GameSource::ea && (name == L"ea app" || name == L"ea desktop")) ||
                (source == GameSource::ubisoft && (name == L"ubisoft connect" || name == L"uplay")) ||
                (source == GameSource::epic && name == L"epic games launcher")) roots.insert(roots.begin(), program.directory);
        }
        // Epic keeps no install folder in the registry (its default key below is Battle.net's).
        if (source != GameSource::epic) for (const REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
            Key settings(HKEY_LOCAL_MACHINE, source == GameSource::gog ? L"SOFTWARE\\GOG.com\\GalaxyClient\\paths" :
                source == GameSource::ea ? L"SOFTWARE\\Electronic Arts\\EA Desktop" :
                source == GameSource::ubisoft ? L"SOFTWARE\\Ubisoft\\Launcher" : L"SOFTWARE\\Blizzard Entertainment\\Battle.net", view);
            const auto folder = regString(settings.handle, source == GameSource::gog ? L"client" :
                source == GameSource::ea ? L"InstallLocation" : source == GameSource::battlenet ? L"InstallPath" : L"InstallDir");
            if (!folder.empty()) roots.insert(roots.begin(), folder);
        }
        const std::vector<std::filesystem::path> suffixes = source == GameSource::gog ? std::vector<std::filesystem::path>{L"GalaxyClient.exe"} :
            source == GameSource::battlenet ? std::vector<std::filesystem::path>{L"Battle.net.exe", L"Battle.net Launcher.exe"} :
            source == GameSource::ea ? std::vector<std::filesystem::path>{L"EADesktop.exe", L"EA Desktop\\EADesktop.exe"} :
            source == GameSource::ubisoft ? std::vector<std::filesystem::path>{L"UbisoftConnect.exe", L"upc.exe", L"UbisoftGameLauncher.exe"} :
            std::vector<std::filesystem::path>{L"Launcher\\Portal\\Binaries\\Win32\\EpicGamesLauncher.exe", L"Launcher\\Portal\\Binaries\\Win64\\EpicGamesLauncher.exe"};
        for (const auto& root : roots) for (const auto& suffix : suffixes)
            if (executable.empty() && file(root / suffix)) executable = root / suffix;
        if (executable.empty()) executable = protocol;
        result.clients.push_back({source, executable});
        // Current clients keep their account cache in LOCALAPPDATA, older ones next to the EXE.
        if (source == GameSource::ubisoft && !executable.empty()) result.ubisoft_caches.push_back(executable.parent_path() / L"cache");
    }
    if (const auto local = environment(L"LOCALAPPDATA"); !local.empty())
        result.ubisoft_caches.insert(result.ubisoft_caches.begin(), local / L"Ubisoft Game Launcher" / L"cache");
    return result;
}
#else
PcDiscovery discoverPcInstallations(const std::vector<std::filesystem::path>& multimc_chosen) {
    // No registry: the stores' games come from the Linux launchers that installed them. Heroic opens
    // for Epic and GOG, Lutris for the stores it runs in Wine; Xbox has no client here.
    PcDiscovery result;
    result.heroic_configs = heroic::configFolders();
    result.lutris_databases = lutris::databases();
    result.lutris = lutris::executable();
    const auto heroic_client = heroic::executable();
    result.multimc = multimc::discover(multimc_chosen);
    for (const auto source : pc_sources) {
        std::filesystem::path executable;
        if (source == GameSource::multimc) { if (!result.multimc.empty()) executable = result.multimc.front().executable; }
        else if (source == GameSource::gog || source == GameSource::epic) executable = heroic_client.empty() ? result.lutris : heroic_client;
        else if (source != GameSource::xbox) executable = result.lutris;
        result.clients.push_back({source, executable});
    }
    return result;
}
#endif

StoreLibrary scanNativePcLibraries(const PcDiscovery& discovery, const std::vector<Game>& previous_battlenet) {
    StoreLibrary result;
    result.clients = discovery.clients;
    std::map<std::string, Game> games;
    const auto add = [&](Game game) {
        if (!game.name.empty() && validStoreGameId(game.source, game.source_id)) games.try_emplace(gameKey(game), std::move(game));
    };
    auto installs = discovery.installs;
    std::vector<battlenet::RegistryHint> battle_hints;
    for (const auto& program : discovery.programs) {
        if (lower(program.publisher) == L"gog.com") {
            const auto end = program.key.find(L"_is1");
            if (end != std::wstring::npos) installs.push_back({GameSource::gog, utf8(program.key.substr(0, end)), program.name, program.directory});
        }
        const auto command = lower(program.uninstall);
        if (command.find(L"eainstaller") != std::wstring::npos && command.find(L"cleanup.exe") != std::wstring::npos)
            installs.push_back({GameSource::ea, {}, program.name, program.directory});
        if (const auto uid = battlenet::uninstallUid(program.uninstall); !uid.empty()) battle_hints.push_back({uid, program.directory});
    }
    // Ubisoft Connect: owned products of the last signed-in account, from the client's cache files.
    const auto ubisoft_account = ubisoft::readAccount(discovery.ubisoft_caches);
    const auto ubisoft_names = ubisoft::loadNames(discovery.ubisoft_names);
    const auto ubisoftTitle = [&](const std::string& id) {
        if (ubisoft_account) for (const auto& product : ubisoft_account->products)
            if (product.id == id) return ubisoft::title(ubisoft_names, product);
        return ubisoft::title(ubisoft_names, {id, {}});
    };
    for (const auto& install : installs) {
        if (!directory(install.directory)) continue;
        auto folder = install.directory;
        while (folder.has_relative_path() && folder.filename().empty()) folder = folder.parent_path();
        auto title = install.name;
        if (title.empty() && install.source == GameSource::ubisoft) title = ubisoftTitle(install.id);
        auto game = baseGame(install.source, install.id, title.empty() ? folder.filename().wstring() : title);
        std::optional<json::Value> manifest;
        if (install.source == GameSource::ea) game.source_id = xmlContentId(textFile(install.directory / L"__Installer" / L"installerdata.xml"));
        else if (install.source == GameSource::gog) {
            manifest = json::parse(textFile(install.directory / (L"goggame-" + wide(install.id) + L".info")));
            if (manifest) {
                const auto parent = field(*manifest, "rootGameId");
                if (!parent.empty() && parent != install.id) continue;
                if (const auto name = field(*manifest, "name"); !name.empty()) game.name = cleanName(wide(name));
            }
        }
        configureStoreActions(game, client(discovery, game.source), install.directory.wstring());
        if (manifest && !game.play_action) {
            const auto* tasks = manifest->get("playTasks");
            if (tasks && tasks->type == json::Value::Type::array) for (const auto& task : tasks->elements) {
                const auto* primary = task.get("isPrimary");
                const auto relative = std::filesystem::path(wide(field(task, "path")));
                if (!primary || !primary->flag || field(task, "type") != "FileTask" || relative.empty() || relative.is_absolute()) continue;
                const auto executable = install.directory / relative;
                if (file(executable) && lower(executable.extension().wstring()) == L".exe") {
                    const std::filesystem::path specified(wide(field(task, "workingDir")));
                    const auto working = specified.empty() ? install.directory : specified.is_absolute() ? specified : install.directory / specified;
                    if (directory(working)) {
                        game.play_action = {executable.wstring(), wide(field(task, "arguments")), working};
                        game.executable = executable;
                    }
                }
            }
        }
        add(std::move(game));
    }
#ifndef _WIN32
    // Linux launchers' store games (before Battle.net's own retained entries; add() never replaces).
    bool lutris_battlenet = false;
    for (const auto& config : discovery.heroic_configs) for (auto& game : heroic::scan(config)) add(std::move(game));
    for (const auto& database : discovery.lutris_databases) {
        auto lutris = lutris::scan(database, discovery.lutris);
        lutris_battlenet |= lutris.battlenet;
        for (auto& game : lutris.games) add(std::move(game));
    }
#endif
    auto battle = battlenet::scan(discovery.battlenet_database, discovery.battlenet_aggregate,
        client(discovery, GameSource::battlenet), battle_hints, previous_battlenet);
    result.battlenet_available = battle.available;
#ifndef _WIN32
    result.battlenet_available |= lutris_battlenet;
#endif
    for (auto& game : battle.games) add(std::move(game));
    std::map<std::string, std::filesystem::path> epic_locations;
    if (const auto list = json::parse(textFile(discovery.epic_installed))) {
        const auto* entries = list->get("InstallationList");
        if (entries && entries->type == json::Value::Type::array) for (const auto& entry : entries->elements)
            epic_locations[field(entry, "AppName")] = wide(field(entry, "InstallLocation"));
    }
    std::error_code ec;
    for (std::filesystem::directory_iterator it(discovery.epic_manifests, ec), end; !ec && it != end; it.increment(ec)) {
        if (lower(it->path().extension().wstring()) != L".item") continue;
        const auto manifest = json::parse(textFile(it->path()));
        if (!manifest) continue;
        const auto* incomplete = manifest->get("bIsIncompleteInstall");
        const auto app = field(*manifest, "AppName"), space = field(*manifest, "CatalogNamespace");
        if (app.empty() || app.starts_with("UE_") || space == "ue" || (incomplete && incomplete->flag)) continue;
        bool addon = false, launchable = false, plugin = false;
        const auto* categories = manifest->get("AppCategories");
        if (categories && categories->type == json::Value::Type::array) for (const auto& category : categories->elements) {
            addon |= category.stringOr() == "addons";
            launchable |= category.stringOr() == "addons/launchable";
            plugin |= category.stringOr().starts_with("plugins");
        }
        if ((addon && !launchable) || plugin) continue;
        auto folder = std::filesystem::path(wide(field(*manifest, "InstallLocation")));
        if (!directory(folder) && epic_locations.contains(app)) folder = epic_locations[app];
        const std::filesystem::path launch(wide(field(*manifest, "LaunchExecutable")));
        if (!directory(folder) || launch.empty() || launch.is_absolute() || !file(folder / launch)) continue;
        auto game = baseGame(GameSource::epic, app, wide(field(*manifest, "DisplayName")));
        configureStoreActions(game, client(discovery, game.source), folder.wstring(), space, field(*manifest, "CatalogItemId"));
        if (game.play_action) add(std::move(game));
    }
    for (const auto& install : discovery.xbox) {
        // Package application IDs are letters, digits and dots; anything else is not a launch target.
        const bool app = !install.app_id.empty() && install.app_id.size() <= 64 && std::all_of(install.app_id.begin(), install.app_id.end(),
            [](wchar_t ch) { return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9') || ch == L'.'; });
        if (!app || !directory(install.directory)) continue;
        auto game = baseGame(GameSource::xbox, install.family, install.name.empty() ? wide(install.family) : install.name);
        configureStoreActions(game, {}, install.directory.wstring());
        if (!validStoreGameId(GameSource::xbox, game.source_id)) continue;
        game.play_action = {L"shell:AppsFolder\\" + wide(install.family) + L"!" + install.app_id};
        add(std::move(game));
    }
    for (auto& game : multimc::scan(discovery.multimc)) add(std::move(game));
    if (ubisoft_account) {
        // Not installed here: Instaluj hands the product to Ubisoft Connect (uplay://install).
        // A local installation of the same product keeps precedence (add() never replaces).
        result.ubisoft_account = true;
        const auto ubisoft_client = client(discovery, GameSource::ubisoft);
        for (const auto& product : ubisoft_account->products) {
            auto game = baseGame(GameSource::ubisoft, product.id, ubisoft::title(ubisoft_names, product));
            game.installed = false;
            game.account_id = ubisoft_account->id;
            configureStoreActions(game, ubisoft_client, {});
            add(std::move(game));
        }
        for (auto& product : ubisoft::unnamed(ubisoft_names, ubisoft_account->products))
            result.ubisoft_unnamed.emplace_back(std::move(product.id), std::move(product.uuid));
        for (auto& [key, game] : games) {
            (void)key;
            if (game.source != GameSource::ubisoft) continue;
            if (const auto played = ubisoft_account->last_played.find(game.source_id); played != ubisoft_account->last_played.end())
                game.last_played = std::max(game.last_played, played->second);
        }
    }
    // Ubisoft Connect's own art of its games (installed or owned), where its client keeps it: a stand-in
    // where SteamGridDB has nothing (artworkForGame).
    if (const auto art = ubisoft::readConfigurations(discovery.ubisoft_caches); !art.empty())
        for (auto& [key, game] : games) {
            (void)key;
            if (game.source != GameSource::ubisoft) continue;
            if (const auto found = art.find(game.source_id); found != art.end()) {
                game.store_tall_art = found->second.thumb; game.store_wide_art = found->second.background; game.store_logo_art = found->second.logo;
            }
        }
    for (auto& [key, game] : games) { (void)key; result.visible.push_back(std::move(game)); }
    return result;
}

StoreLibrary mergePcLibraries(StoreLibrary native, const std::filesystem::path& catalogs) {
    StoreLibrary result;
    result.battlenet_available = native.battlenet_available;
    result.ubisoft_account = native.ubisoft_account;
    result.ubisoft_unnamed = std::move(native.ubisoft_unnamed);
    result.clients = std::move(native.clients);
    for (const auto source : pc_sources) {
        auto manual = makeStoreAdapter(source)->scan(catalogs);
        if (!manual.errors.empty()) {
            result.errors.insert(result.errors.end(), manual.errors.begin(), manual.errors.end());
            continue; // A broken visibility override must not expose that source's native games.
        }
        std::map<std::string, Game> games;
        for (const auto* list : {&native.visible, &native.private_games})
            for (const auto& game : *list) if (game.source == source) games.emplace(gameKey(game), game);
        for (const auto* overrides : {&manual.visible, &manual.private_games}) for (const auto& custom : *overrides) {
            const auto key = gameKey(custom);
            if (const auto found = games.find(key); found != games.end()) {
                found->second.visibility = custom.visibility;
                found->second.name = custom.name;
                found->second.last_played = custom.last_played;
            } else games.emplace(key, custom);
        }
        for (auto& [key, game] : games) {
            (void)key;
            (game.visibility == GameVisibility::public_game ? result.visible : result.private_games).push_back(std::move(game));
        }
        for (auto& client : manual.clients) if (file(client.executable)) {
            auto found = std::find_if(result.clients.begin(), result.clients.end(), [&](const StoreClient& entry) { return entry.source == source; });
            if (found == result.clients.end()) result.clients.push_back(std::move(client));
            else if (found->executable.empty()) *found = std::move(client);
        }
    }
    return result;
}

StoreLibrary mergeGogLibrary(StoreLibrary native, const std::vector<Game>& owned, const std::string& account) {
    if (account.empty()) return native;
    std::map<std::string, size_t> local;
    for (size_t i = 0; i < native.visible.size(); ++i) local.emplace(gameKey(native.visible[i]), i);
    for (const auto& game : owned) {
        if (game.source != GameSource::gog || game.source_id.empty()) continue;
        if (const auto found = local.find(gameKey(game)); found != local.end()) {
            auto& installed = native.visible[found->second];
            installed.added_at = game.added_at;
            if (game.visibility != GameVisibility::public_game) installed.visibility = game.visibility;
            continue;
        }
        auto entry = game;
        entry.account_id = account;
        entry.installed = false;
        entry.play_action = {};
        entry.install_action = {L"https://www.gog.com/account", {}, {}, ActionKind::open_client};
        local.emplace(gameKey(entry), native.visible.size());
        native.visible.push_back(std::move(entry));
    }
    return native;
}
bool epicExcludedTitle(std::wstring_view name) {
    std::wstring folded(name);
    for (auto& ch : folded) if (ch >= L'A' && ch <= L'Z') ch = static_cast<wchar_t>(ch - L'A' + L'a');
    for (const std::wstring_view excluded : {L"twinmotion"}) if (folded.find(excluded) != std::wstring::npos) return true;
    return false;
}
StoreLibrary mergeEpicLibrary(StoreLibrary native, const std::vector<Game>& owned, const std::string& account, const std::vector<Game>& installed) {
    std::erase_if(native.visible, [](const Game& game) { return game.source == GameSource::epic && epicExcludedTitle(game.name); });
    for (const auto& game : installed) {
        if (game.source != GameSource::epic || !game.epic_managed || !game.installed || epicExcludedTitle(game.name)) continue;
        const auto found = std::find_if(native.visible.begin(), native.visible.end(), [&](const Game& g) { return gameKey(g) == gameKey(game); });
        if (found == native.visible.end()) native.visible.push_back(game); else *found = game;
    }
    if (account.empty()) return native;
    for (const auto& game : owned) {
        if (game.source != GameSource::epic || game.source_id.empty() || epicExcludedTitle(game.name)) continue;
        const auto found = std::find_if(native.visible.begin(), native.visible.end(), [&](const Game& g) { return gameKey(g) == gameKey(game); });
        if (found != native.visible.end()) {
            found->epic_namespace = game.epic_namespace; found->epic_catalog = game.epic_catalog; found->epic_partner = game.epic_partner;
            found->store_tall_art = game.store_tall_art; found->store_wide_art = game.store_wide_art; found->store_logo_art = game.store_logo_art;
            found->store_playtime_minutes = std::max(found->store_playtime_minutes, game.store_playtime_minutes); // Epic's own count.
            found->added_at = game.added_at;
            if (game.visibility != GameVisibility::public_game) found->visibility = game.visibility;
        } else {
            auto entry = game; entry.account_id = account; entry.installed = false; entry.play_action = {};
            native.visible.push_back(std::move(entry));
        }
    }
    return native;
}
StoreLibrary mergeImportedLibrary(StoreLibrary native, const std::vector<Game>& imported) {
    std::set<std::string> present;
    for (const auto& game : native.visible) present.insert(gameKey(game));
    for (const auto& game : imported) {
        if ((game.source != GameSource::ea && game.source != GameSource::battlenet) || game.account_id.empty() ||
            !validStoreGameId(game.source, game.source_id) || game.name.empty()) continue;
        if (!present.insert(gameKey(game)).second) {
            // Installed here as well: the installation stays, with the import's art (EA's offers).
            const auto found = std::find_if(native.visible.begin(), native.visible.end(), [&](const Game& g) { return gameKey(g) == gameKey(game); });
            if (found != native.visible.end() && found->store_tall_art.empty() && found->store_wide_art.empty() && found->store_logo_art.empty()) {
                found->store_tall_art = game.store_tall_art; found->store_wide_art = game.store_wide_art; found->store_logo_art = game.store_logo_art;
            }
            continue;
        }
        Game entry;
        entry.source = game.source; entry.kind = GameKind::store; entry.installed = false;
        entry.source_id = game.source_id; entry.name = game.name; entry.account_id = game.account_id;
        entry.store_tall_art = game.store_tall_art; entry.store_wide_art = game.store_wide_art; entry.store_logo_art = game.store_logo_art;
        configureStoreActions(entry, client(native.clients, entry.source), {});
        entry.play_action = {};
        // EA app: the launch link with autoDownload opens the game's download (lutris ea_app.py).
        // Battle.net keeps "open the client": no external command starts a download there.
        if (entry.source == GameSource::ea) {
            Game planned = entry; configureStoreActions(planned, {}, {});
            if (planned.play_action) entry.install_action = {planned.play_action.target + L"&autoDownload=1", {}, {}, ActionKind::install};
        }
        native.visible.push_back(std::move(entry));
    }
    return native;
}
StoreLibrary mergeAmazonLibrary(StoreLibrary native, const std::vector<Game>& games) {
    std::set<std::string> present;
    for (const auto& game : native.visible) present.insert(gameKey(game));
    for (const auto& game : games)
        if (game.source == GameSource::amazon && validStoreGameId(GameSource::amazon, game.source_id) && !game.name.empty() &&
            present.insert(gameKey(game)).second) native.visible.push_back(game);
    return native;
}
StoreLibrary mergeXboxLibrary(StoreLibrary native, const std::vector<Game>& games) {
    const bool xbox_app = std::any_of(native.clients.begin(), native.clients.end(), [](const StoreClient& client) {
        return client.source == GameSource::xbox && !client.executable.empty();
    });
    std::map<std::string, size_t> present;
    for (size_t i = 0; i < native.visible.size(); ++i) present.emplace(gameKey(native.visible[i]), i);
    for (const auto& game : games) {
        if (game.source != GameSource::xbox || !validStoreGameId(GameSource::xbox, game.source_id) || game.name.empty()) continue;
        if (const auto found = present.find(gameKey(game)); found != present.end()) {
            // The Xbox app installed it (or the account lists a game twice): the installation stays.
            auto& local = native.visible[found->second];
            if (local.source != GameSource::xbox) continue;
            if (local.account_id.empty()) local.account_id = game.account_id;
            if (local.xbox_product.empty()) local.xbox_product = game.xbox_product;
            if (!local.xbox_title_id) local.xbox_title_id = game.xbox_title_id;
            local.xbox_game_pass = local.xbox_game_pass || game.xbox_game_pass;
            // The account's play history (activity.json) for a game the Xbox app installed.
            local.store_playtime_minutes = std::max(local.store_playtime_minutes, game.store_playtime_minutes);
            local.last_played = std::max(local.last_played, game.last_played);
            for (auto [mine, theirs] : {std::pair{&local.store_tall_art, &game.store_tall_art}, std::pair{&local.store_wide_art, &game.store_wide_art},
                                        std::pair{&local.store_logo_art, &game.store_logo_art}})
                if (mine->empty()) *mine = *theirs;
            continue;
        }
        auto entry = game;
        if (!entry.installed && !entry.xbox_product.empty())
            entry.install_action = xbox_app ? GameAction{L"msxbox://game/?productId=" + wide(entry.xbox_product), {}, {}, ActionKind::install} :
                                              GameAction{L"ms-windows-store://pdp/?ProductId=" + wide(entry.xbox_product), {}, {}, ActionKind::open_client};
        present.emplace(gameKey(entry), native.visible.size());
        native.visible.push_back(std::move(entry));
    }
    return native;
}
StoreLibrary scanPcLibraries(const std::filesystem::path& catalogs, const std::vector<Game>& gog_owned, const std::string& gog_account,
                             const std::vector<Game>& previous_battlenet, const std::vector<Game>& epic_owned,
                             const std::string& epic_account, const std::vector<Game>& epic_installed,
                             const std::vector<Game>& imported, const std::vector<std::filesystem::path>& multimc_chosen,
                             const std::vector<Game>& amazon, const std::vector<Game>& xbox) {
    auto discovery = discoverPcInstallations(multimc_chosen);
    discovery.ubisoft_names = catalogs.parent_path() / L"ubisoft" / L"names.tsv";
    // Offline GOG installers are also tracked independently of account state,
    // including installers that did not create an uninstall registry entry.
    std::error_code ec;
    size_t count = 0;
    for (std::filesystem::directory_iterator it(catalogs.parent_path() / L"installed" / L"gog", ec), end; !ec && it != end && count++ < 10000; it.increment(ec)) {
        if (it->path().extension() != L".path") continue;
        const auto id = utf8(it->path().stem().wstring());
        if (!validStoreGameId(GameSource::gog, id)) continue;
        const auto folder = std::filesystem::path(wide(readFile(it->path(), 8192)));
        if (directory(folder) && file(folder / (L"goggame-" + wide(id) + L".info")))
            discovery.installs.push_back({GameSource::gog, id, {}, folder});
    }
    return mergePcLibraries(mergeXboxLibrary(mergeAmazonLibrary(mergeImportedLibrary(mergeEpicLibrary(mergeGogLibrary(scanNativePcLibraries(discovery, previous_battlenet), gog_owned, gog_account),
        epic_owned, epic_account, epic_installed), imported), amazon), xbox), catalogs);
}

GameAction connectPcClient(GameSource source, const std::vector<StoreClient>& clients) {
    if (source == GameSource::xbox) {
        // Packaged app: activated through its application ID, never by starting its EXE.
        const bool installed = std::any_of(clients.begin(), clients.end(), [](const StoreClient& client) { return client.source == GameSource::xbox && file(client.executable); });
        return {installed ? L"shell:AppsFolder\\" + std::wstring(xbox_app_family) + L"!Microsoft.Xbox.App" : L"ms-windows-store://pdp/?ProductId=9MV0B5HZVK9Z",
                {}, {}, ActionKind::open_client};
    }
    for (const auto& client : clients) if (client.source == source && file(client.executable))
        return {client.executable.wstring(), {}, {}, ActionKind::open_client};
    switch (source) {
    case GameSource::gog: return {L"https://www.gog.com/account", {}, {}, ActionKind::open_client};
    case GameSource::battlenet: return {L"https://download.battle.net/", {}, {}, ActionKind::open_client};
    case GameSource::ea: return {L"https://www.ea.com/ea-app", {}, {}, ActionKind::open_client};
    case GameSource::ubisoft: return {L"https://www.ubisoft.com/ubisoft-connect", {}, {}, ActionKind::open_client};
    case GameSource::epic: return {L"https://store.epicgames.com/download", {}, {}, ActionKind::open_client};
    case GameSource::multimc: return {L"https://prismlauncher.org/download/", {}, {}, ActionKind::open_client};
    default: return {};
    }
}

std::vector<InstalledProgram> installedPrograms() {
    std::vector<InstalledProgram> programs;
#ifdef _WIN32
    for (const auto root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) for (const REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
        Key list(root, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", view);
        for (const auto& name : children(list.handle)) {
            Key entry(list.handle, name, 0);
            programs.push_back({name, regString(entry.handle, L"DisplayName"), regString(entry.handle, L"Publisher"),
                regString(entry.handle, L"UninstallString"), regString(entry.handle, L"InstallLocation")});
        }
    }
#endif
    return programs;
}

GameAction uninstallAction(const Game& game, const std::vector<InstalledProgram>& programs) {
    if (!isGame(game) || !game.installed || game.installation.active()) return {};
    if (game.source == GameSource::steam) {
        if (game.kind != GameKind::steam || !game.app_id) return {};
        return {L"steam://uninstall/" + std::to_wstring(game.app_id), {}, {}, ActionKind::open_client};
    }
    // Removing an entry is not uninstalling; a Minecraft instance is removed in its own launcher.
    if (game.kind == GameKind::nonsteam || localFileGame(game.source) || game.source == GameSource::multimc) return {};
    const auto folder = [](const std::filesystem::path& path) {
        auto text = lower(path.lexically_normal().wstring());
        std::replace(text.begin(), text.end(), L'/', L'\\');
        while (text.size() > 3 && text.back() == L'\\') text.pop_back();
        return text;
    };
    const auto installed = game.install_directory.is_absolute() ? folder(game.install_directory) : std::wstring{};
    const InstalledProgram* match = nullptr;
    for (const auto& program : programs) {
        if (program.uninstall.empty()) continue;
        // GOG installers register "<product id>_is1".
        if (game.source == GameSource::gog && !game.source_id.empty() && lower(program.key) == lower(wide(game.source_id)) + L"_is1") { match = &program; break; }
        // Another store's own client, or an entry for a whole folder of things, is never the game.
        if (!installed.empty() && program.directory.is_absolute() && folder(program.directory) == installed) match = &program;
    }
    if (!match) return {};
    // "C:\Games\X\unins000.exe" /SILENT-free: the entry's own command, interactive, as Windows runs it.
    GameAction action;
#ifdef _WIN32
    int count = 0;
    auto** parts = CommandLineToArgvW(match->uninstall.c_str(), &count);
    if (!parts || count < 1) { if (parts) LocalFree(parts); return {}; }
    action.target = parts[0];
#else
    // CommandLineToArgvW's rule for the program: quoted up to the next quote, else up to a space.
    if (!match->uninstall.empty() && match->uninstall.front() == L'"') action.target = match->uninstall.substr(1, match->uninstall.find(L'"', 1) - 1);
    else action.target = match->uninstall.substr(0, match->uninstall.find_first_of(L" \t"));
#endif
    action.kind = ActionKind::open_client;
    // The rest of the line exactly as registered (quoting preserved), after the program itself.
    const auto& line = match->uninstall;
    size_t after = 0;
    if (!line.empty() && line.front() == L'"') { after = line.find(L'"', 1); after = after == std::wstring::npos ? line.size() : after + 1; }
    else after = std::min(line.find(L' '), line.size());
    action.arguments = line.substr(after);
    while (!action.arguments.empty() && action.arguments.front() == L' ') action.arguments.erase(0, 1);
#ifdef _WIN32
    LocalFree(parts);
#endif
    if (action.target.empty()) return {};
    // MsiExec.exe /X{...} names a system program; an installer's own uninstaller runs in its folder.
    if (std::filesystem::path(action.target).is_absolute()) action.working_directory = std::filesystem::path(action.target).parent_path();
    return action;
}

bool innoUninstaller(const GameAction& action) {
    const auto name = lower(std::filesystem::path(action.target).filename().wstring());
    // Inno Setup names its uninstaller unins000.exe (unins001.exe… beside another install's).
    return std::filesystem::path(action.target).is_absolute() && name.size() == 12 && name.starts_with(L"unins") && name.ends_with(L".exe") &&
        std::all_of(name.begin() + 5, name.begin() + 8, [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

GameAction silentUninstallAction(const GameAction& interactive) {
    if (!innoUninstaller(interactive)) return {};
    GameAction action = interactive;
    // The entry's own switches stay (e.g. /LOG); only the ones that make it windowless are added.
    for (const auto* option : {L"/VERYSILENT", L"/SUPPRESSMSGBOXES", L"/NORESTART"}) {
        if (lower(action.arguments).find(lower(option)) != std::wstring::npos) continue;
        if (!action.arguments.empty()) action.arguments += L' ';
        action.arguments += option;
    }
    return action;
}

UninstallResult runSilentUninstaller(const GameAction& action, void* owner, const std::function<bool()>& cancel) {
    if (!innoUninstaller(action) || !file(action.target)) return UninstallResult::failed;
#ifndef _WIN32
    // Inno Setup uninstallers are Windows programs: on Linux their games are removed in Heroic or Lutris.
    (void)owner; (void)cancel;
    return UninstallResult::failed;
#else
    const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct Com { HRESULT result; ~Com() { if (SUCCEEDED(result)) CoUninitialize(); } } initialized{com};
    const auto folder = action.working_directory.empty() ? std::filesystem::path(action.target).parent_path() : action.working_directory;
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.hwnd = static_cast<HWND>(owner);
    info.lpFile = action.target.c_str();
    info.lpParameters = action.arguments.empty() ? nullptr : action.arguments.c_str();
    info.lpDirectory = folder.c_str();
    info.nShow = SW_HIDE;
    // A game under Program Files asks Windows (UAC) itself; declining is not a failure.
    if (!ShellExecuteExW(&info)) return GetLastError() == ERROR_CANCELLED ? UninstallResult::refused : UninstallResult::failed;
    if (!info.hProcess) return UninstallResult::failed;
    struct Process { HANDLE value; ~Process() { CloseHandle(value); } } process{info.hProcess};
    while (WaitForSingleObject(info.hProcess, 100) == WAIT_TIMEOUT) if (cancel()) return UninstallResult::failed;
    DWORD code = 0;
    GetExitCodeProcess(info.hProcess, &code);
    // Inno's first phase copies itself to %TEMP% and the copy removes the game, deleting the
    // uninstaller last: its disappearance is the end. A failed first phase gets a short grace.
    const auto started = GetTickCount64();
    const uint64_t limit = code == 0 ? 30ull * 60 * 1000 : 5000;
    while (GetTickCount64() - started < limit) {
        if (!file(action.target)) return UninstallResult::done;
        if (cancel()) return UninstallResult::failed;
        Sleep(250);
    }
    return file(action.target) ? UninstallResult::failed : UninstallResult::done;
#endif
}
