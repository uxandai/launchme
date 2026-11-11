// The Stores' built-in tabs, offline: the launcher catalogue (methods, detection, installation flows
// through a fake backend), the promotions' parsers, merge and cache, and the grid page's layout.
#include "../src/net/promos.hpp"
#include "../src/platform/system.hpp"
#include "../src/platform/text_encoding.hpp"
#include "../src/store/launcher_install.hpp"
#include "../src/ui/plugin_page.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

namespace {
using launchers::Method;

constexpr int64_t now = 1794000000; // 2026-11-06 17:20 UTC.

constexpr auto epic = R"({"data":{"Catalog":{"searchStore":{"elements":[
{"title":"Wolna Gra","productSlug":"wolna-gra/home","urlSlug":"x","catalogNs":{"mappings":[{"pageSlug":"wolna-gra"}]},
 "keyImages":[{"type":"Thumbnail","url":"https://cdn1.epicgames.com/t.jpg"},{"type":"OfferImageWide","url":"https://cdn1.epicgames.com/wide.jpg"}],
 "price":{"totalPrice":{"discountPrice":0,"originalPrice":14200,"currencyCode":"PLN"}},
 "promotions":{"promotionalOffers":[{"promotionalOffers":[{"startDate":"2026-11-05T16:00:00.000Z","endDate":"2026-11-12T16:00:00.000Z","discountSetting":{"discountType":"PERCENTAGE","discountPercentage":0}}]}],"upcomingPromotionalOffers":[]}},
{"title":"Za Tydzień","productSlug":null,"urlSlug":"za-tydzien","catalogNs":{"mappings":null},"keyImages":[],
 "price":{"totalPrice":{"discountPrice":6900,"originalPrice":6900,"currencyCode":"PLN"}},
 "promotions":{"promotionalOffers":[],"upcomingPromotionalOffers":[{"promotionalOffers":[{"startDate":"2026-11-12T16:00:00.000Z","endDate":"2026-11-19T16:00:00.000Z","discountSetting":{"discountType":"PERCENTAGE","discountPercentage":0}}]}]}},
{"title":"Tylko Przecena","urlSlug":"przecena","keyImages":[],"price":{"totalPrice":{"discountPrice":100,"originalPrice":200,"currencyCode":"PLN"}},
 "promotions":{"promotionalOffers":[{"promotionalOffers":[{"startDate":"2026-11-01T16:00:00.000Z","endDate":"2026-12-01T16:00:00.000Z","discountSetting":{"discountType":"PERCENTAGE","discountPercentage":20}}]}]}},
{"title":"Bez Promocji","urlSlug":"nic","keyImages":[],"price":{"totalPrice":{"discountPrice":100,"originalPrice":100,"currencyCode":"PLN"}},"promotions":null},
{"title":"Dawno Po","urlSlug":"po","keyImages":[],"price":{"totalPrice":{"discountPrice":0,"originalPrice":100,"currencyCode":"PLN"}},
 "promotions":{"promotionalOffers":[{"promotionalOffers":[{"startDate":"2026-10-01T16:00:00.000Z","endDate":"2026-10-08T16:00:00.000Z","discountSetting":{"discountType":"PERCENTAGE","discountPercentage":0}}]}]}}
]}}}})";
constexpr auto steam = R"({"specials":{"items":[
{"id":10,"type":0,"name":"Tania","discounted":true,"discount_percent":75,"original_price":10000,"final_price":2500,"currency":"PLN","large_capsule_image":"https://shared.akamai.steamstatic.com/a.jpg","discount_expiration":1795456800},
{"id":11,"type":1,"name":"Paczka","discounted":true,"discount_percent":10,"original_price":1000,"final_price":900,"currency":"EUR","header_image":"https://shared.akamai.steamstatic.com/b.jpg"},
{"id":12,"type":0,"name":"Rozdawana","discounted":true,"discount_percent":100,"original_price":5000,"final_price":0,"currency":"PLN"},
{"id":"zly","name":"Bez numeru","discount_percent":50,"final_price":1},
{"id":13,"name":"Bez rabatu","discount_percent":0,"final_price":100,"currency":"PLN"}
]}})";
constexpr auto gog = "{\"products\":["
 "{\"title\":\"Stara Gra\",\"storeLink\":\"https://www.gog.com/pl/game/stara_gra\",\"coverHorizontal\":\"https://images.gog-statics.com/abc.png\","
 "\"price\":{\"final\":\"9,99\\u00a0z\\u0142\",\"base\":\"39,99\\u00a0z\\u0142\",\"discount\":\"-75%\",\"finalMoney\":{\"amount\":\"9.99\",\"currency\":\"PLN\"}}},"
 "{\"title\":\"Prezent\",\"storeLink\":\"https://www.gog.com/pl/game/prezent\",\"price\":{\"final\":\"0\",\"base\":\"19,99\",\"discount\":\"-100%\",\"finalMoney\":{\"amount\":\"0.00\"}}},"
 "{\"title\":\"Cudzy link\",\"storeLink\":\"https://example.com/x\",\"price\":{\"final\":\"1\",\"base\":\"2\",\"discount\":\"-50%\",\"finalMoney\":{\"amount\":\"1.00\"}}},"
 "{\"title\":\"Pe\\u0142na cena\",\"storeLink\":\"https://www.gog.com/pl/game/pelna\",\"price\":{\"final\":\"5\",\"base\":\"5\",\"discount\":null,\"finalMoney\":{\"amount\":\"5.00\"}}}"
 "]}";
constexpr auto giveaways = R"([
{"id":1,"title":"Wolna Gra (Epic Games) Giveaway","worth":"$24.99","image":"https://www.gamerpower.com/offers/1b/a.jpg","open_giveaway_url":"https://www.gamerpower.com/open/wolna-gra","type":"Game","platforms":"PC, Epic Games Store","end_date":"2026-11-12 23:59:00","status":"Active"},
{"id":2,"title":"Mała Gra (itch.io) Giveaway","worth":"N/A","image":"https://www.gamerpower.com/offers/1b/b.jpg","open_giveaway_url":"https://www.gamerpower.com/open/mala-gra","type":"Game","platforms":"PC, Itch.io, DRM-Free","end_date":"N/A","status":"Active"},
{"id":3,"title":"Klucz (Steam) Key Giveaway","worth":"$9.99","open_giveaway_url":"https://www.gamerpower.com/open/klucz","type":"Game","platforms":"PC, Steam","end_date":"2026-11-01 23:59:00","status":"Active"},
{"id":4,"title":"Skórka Giveaway","open_giveaway_url":"https://www.gamerpower.com/open/skorka","type":"DLC","platforms":"PC","end_date":"N/A","status":"Active"},
{"id":5,"title":"Obcy Giveaway","open_giveaway_url":"https://evil.example/open","type":"Game","platforms":"PC","end_date":"N/A","status":"Active"}
])";

int promotions(const std::filesystem::path& root) {
    using namespace promos;
    if (parseTime("2026-11-05T16:00:00.000Z") != 1793894400 || parseTime("2026-11-12 23:59:00") != 1794527940 || parseTime("N/A") || parseTime("2026-13-01T00:00:00Z")) return 1;
    if (money(7649, "PLN") != L"76,49 zł" || money(1299, "USD") != L"$12.99" || money(5, "EUR") != L"0,05 €" || money(100, "SEK") != L"1.00 SEK" || !money(-1, "PLN").empty()) return 2;
    if (titleKey(L"The Witcher® 3: Wild Hunt") != titleKey(L"the witcher® 3 wild hunt") || titleKey(L"A-b C") != L"abc") return 3;
    if (tilePicture(L"https://cdn1.epicgames.com/wide.jpg") != L"https://cdn1.epicgames.com/wide.jpg?h=360&resize=1&w=640" ||
        tilePicture(L"https://images.gog-statics.com/abcdef0123456789.png") != L"https://images.gog-statics.com/abcdef0123456789_product_tile_extended_432x243.jpg" ||
        tilePicture(L"https://shared.akamai.steamstatic.com/a.jpg?t=1") != L"https://shared.akamai.steamstatic.com/a.jpg?t=1") return 4;

    Region region; region.country = "PL"; region.currency = "PLN"; region.polish = true;
    const auto e = parseEpic(epic, now, region);
    if (e.size() != 2 || e[0].title != L"Wolna Gra" || !e[0].free || e[0].starts != 0 || e[0].ends != 1794499200 || e[0].regular != L"142,00 zł" ||
        e[0].url != L"https://store.epicgames.com/pl/p/wolna-gra" || e[0].image != L"https://cdn1.epicgames.com/wide.jpg") return 5;
    if (e[1].title != L"Za Tydzień" || e[1].starts != 1794499200 || e[1].url != L"https://store.epicgames.com/pl/p/za-tydzien" || !e[1].image.empty()) return 6;
    region.polish = false;
    if (parseEpic(epic, now, region)[0].url != L"https://store.epicgames.com/en-US/p/wolna-gra") return 7;

    const auto s = parseSteam(steam);
    if (s.size() != 3 || s[0].title != L"Tania" || s[0].discount != 75 || s[0].price != L"25,00 zł" || s[0].regular != L"100,00 zł" || s[0].free ||
        s[0].url != L"https://store.steampowered.com/app/10" || s[0].ends != 1795456800 || s[0].image != L"https://shared.akamai.steamstatic.com/a.jpg") return 8;
    if (s[1].url != L"https://store.steampowered.com/sub/11" || s[1].price != L"9,00 €" || !s[2].free || !s[2].price.empty()) return 9;

    const auto g = parseGog(gog);
    if (g.size() != 2 || g[0].title != L"Stara Gra" || g[0].discount != 75 || g[0].price != L"9,99 zł" || g[0].regular != L"39,99 zł" || g[0].free ||
        g[0].url != L"https://www.gog.com/pl/game/stara_gra" || !g[1].free || g[1].discount != 100) return 10;

    const auto p = parseGamerPower(giveaways, now);
    if (p.size() != 2 || p[0].title != L"Wolna Gra" || p[0].where != L"Epic Games Store · GamerPower" || p[0].store != Store::epic || p[0].regular != L"$24.99" || p[0].ends != 1794527940) return 11;
    if (p[1].title != L"Mała Gra" || p[1].where != L"Itch.io, DRM-Free · GamerPower" || p[1].store != Store::other || !p[1].regular.empty() || p[1].ends) return 12;

    // Damaged or foreign answers give nothing and never throw.
    for (const auto* broken : {"", "{", "[]", "{\"data\":7}", "null", "{\"specials\":{\"items\":7},\"products\":\"x\"}"})
        if (!parseEpic(broken, now, region).empty() || !parseSteam(broken).empty() || !parseGog(broken).empty() || !parseGamerPower(broken, now).empty()) return 13;

    const auto feed = combine(e, s, g, p, now);
    // Free now: Epic's own (the aggregator's copy of it dropped), Steam's and GOG's giveaways, the itch.io one; the soonest end first.
    if (feed.free_now.size() != 4 || feed.free_now[0].title != L"Wolna Gra" || feed.free_now[0].where != L"Epic Games" || feed.free_now.back().title != L"Mała Gra") return 14;
    if (feed.free_soon.size() != 1 || feed.free_soon[0].title != L"Za Tydzień") return 15;
    if (feed.deals.size() != 3 || feed.deals[0].title != L"Tania" || feed.deals[1].title != L"Stara Gra" || feed.deals[2].title != L"Paczka") return 16;

    // The cache: one request per source, none within half an hour, the stale copy when the network fails.
    const auto cache = root / L"promos";
    std::map<std::string, int> asked;
    bool online = true;
    const Get get = [&](const std::wstring& url) {
        const auto name = url.find(L"epicgames") != url.npos ? "epic" : url.find(L"steampowered") != url.npos ? "steam" : url.find(L"gog.com") != url.npos ? "gog" : "gamerpower";
        ++asked[name];
        if (!online) return HttpResponse{};
        const std::string body = name == std::string("epic") ? epic : name == std::string("steam") ? steam : name == std::string("gog") ? gog : giveaways;
        if (name == std::string("epic") && (url.find(L"country=PL") == url.npos || url.find(L"locale=pl") == url.npos)) return HttpResponse{};
        if (name == std::string("gog") && url.find(L"currencyCode=PLN") == url.npos) return HttpResponse{};
        return HttpResponse{200, {body.begin(), body.end()}};
    };
    region.polish = true;
    auto loaded = load(cache, region, false, now, get);
    if (loaded.unavailable || loaded.free_now.size() != 4 || asked.size() != 4 || asked["epic"] != 1) return 17;
    loaded = load(cache, region, false, now + 60, get);
    if (loaded.free_now.size() != 4 || asked["epic"] != 1 || asked["gog"] != 1) return 18;
    online = false;
    loaded = load(cache, region, true, now + 120, get);
    if (loaded.unavailable || loaded.deals.size() != 3 || asked["steam"] != 2) return 19;
    loaded = load(root / L"promos-empty", region, false, now, get);
    if (!loaded.unavailable || !loaded.empty()) return 20;
    // A region that is not a code never reaches an address.
    Region odd; odd.country = "P&L"; odd.currency = "zł";
    online = true;
    std::wstring seen;
    load(root / L"promos-odd", odd, true, now, [&](const std::wstring& url) { seen += url; return HttpResponse{}; });
    if (seen.find(L"country=US") == seen.npos || seen.find(L"currencyCode=USD") == seen.npos || seen.find(L"P&L") != seen.npos) return 21;
    return 0;
}

int catalogue() {
    using namespace launchers;
    std::set<std::string_view> ids;
    for (const auto& entry : catalog()) {
        if (!ids.insert(entry.id).second || entry.name.empty() || !entry.tone || !entry.page.starts_with(L"https://")) return 30;
        if (!entry.installer.empty() && (!trustedInstallerUrl(entry.installer) || entry.file.empty())) return 31;
        if (entry.installer.empty() != entry.file.empty()) return 32;
    }
    if (ids.size() < 10 || !find("steam") || find("nope")) return 33;
    if (trustedInstallerUrl(L"http://cdn.akamai.steamstatic.com/x.exe") || trustedInstallerUrl(L"https://cdn.akamai.steamstatic.com.evil.example/x.exe") ||
        trustedInstallerUrl(L"https://user@cdn.akamai.steamstatic.com/x.exe") || trustedInstallerUrl(L"https://evil.example/SteamSetup.exe") ||
        !trustedInstallerUrl(L"https://CDN.akamai.steamstatic.com/client/installer/SteamSetup.exe")) return 34;

    const auto& steam_entry = *find("steam");
    const auto& gog_entry = *find("gog");
    const auto& esde = *find("esde");
    const auto& lutris = *find("lutris");
    if (methods(steam_entry, {true, true, false, false}) != std::vector<Method>{Method::winget, Method::installer, Method::page}) return 35;
    if (methods(steam_entry, {true, false, false, false}) != std::vector<Method>{Method::installer, Method::page}) return 36;
    if (methods(esde, {true, true, false, false}) != std::vector<Method>{Method::page} || !methods(lutris, {true, true, false, false}).empty()) return 37;
    if (methods(steam_entry, {false, false, true, true}) != std::vector<Method>{Method::flatpak, Method::wine, Method::page}) return 38;
    if (methods(gog_entry, {false, false, true, true}) != std::vector<Method>{Method::wine, Method::page} ||
        methods(gog_entry, {false, false, true, false}) != std::vector<Method>{Method::page}) return 39;
    const auto winget = wingetArguments(steam_entry);
    if (winget.size() < 6 || winget[0] != L"install" || winget[2] != L"Valve.Steam" || std::find(winget.begin(), winget.end(), L"--exact") == winget.end() ||
        std::find(winget.begin(), winget.end(), L"--silent") == winget.end()) return 40;
    if (!wingetAlreadyInstalled(0x8A15002Bul) || wingetAlreadyInstalled(1)) return 41;
    const auto flatpak = flatpakCommands(steam_entry);
    if (flatpak.size() != 2 || flatpak[0][0] != L"remote-add" || flatpak[1].back() != L"com.valvesoftware.Steam" ||
        std::find(flatpak[1].begin(), flatpak[1].end(), L"--user") == flatpak[1].end() || !flatpakCommands(gog_entry).empty()) return 42;

    // Detection on Windows: the registry's folder, then the known places.
    std::set<std::wstring> files{L"D:\\Gry\\Steam\\steam.exe", L"C:\\Program Files (x86)\\Battle.net\\Battle.net Launcher.exe"};
    Probe windows;
    windows.variable = [](std::wstring_view name) { return name == L"ProgramFiles(x86)" ? std::wstring(L"C:\\Program Files (x86)") : name == L"ProgramFiles" ? std::wstring(L"C:\\Program Files") : std::wstring{}; };
    // Built on Linux too, where std::filesystem joins with '/': the check is of the names, not the separator.
    const auto backslashes = [](std::wstring text) { std::replace(text.begin(), text.end(), L'/', L'\\'); return text; };
    windows.exists = [&](const std::filesystem::path& file) { return files.contains(backslashes(file.wstring())); };
    windows.registry = [](std::wstring_view key, std::wstring_view value) { return key == L"Software\\Valve\\Steam" && value == L"InstallPath" ? std::wstring(L"D:\\Gry\\Steam") : std::wstring{}; };
    const auto steam_found = detect(steam_entry, windows);
    if (!steam_found || backslashes(steam_found->program.wstring()) != L"D:\\Gry\\Steam\\steam.exe" || !detect(*find("battlenet"), windows) || detect(gog_entry, windows) || detect(*find("amazon"), windows)) return 43;
    if (expand(L"%ProgramFiles%\\x", windows) != L"C:\\Program Files\\x" || !expand(L"%LOCALAPPDATA%\\x", windows).empty() || !expand(L"%broken", windows).empty()) return 44;

    // Linux: a Flatpak, a program on PATH, a Windows build in the prefix it was installed into.
    const std::filesystem::path prefix = L"/home/u/Games/launchme/prefixes/gog-galaxy";
    std::set<std::filesystem::path> linux_files{std::filesystem::path(L"/var/lib/flatpak/app") / L"com.valvesoftware.Steam",
                                                prefixPath(L"%ProgramFiles(x86)%\\GOG Galaxy\\GalaxyClient.exe", prefix, L"steamuser"),
                                                prefixPath(L"%LOCALAPPDATA%\\Amazon Games\\App\\Amazon Games.exe", prefix, L"u")};
    Probe linux_probe;
    linux_probe.windows = false;
    linux_probe.exists = [&](const std::filesystem::path& file) { return linux_files.contains(file); };
    linux_probe.flatpak_apps = {L"/var/lib/flatpak/app"};
    linux_probe.prefix_users = {L"steamuser", L"u"};
    linux_probe.which = [](std::wstring_view command) { return command == L"lutris" ? std::filesystem::path(L"/usr/bin/lutris") : std::filesystem::path{}; };
    linux_probe.wine_prefix = [&](std::string_view id) { return id == "gog" || id == "amazon" ? prefix : std::filesystem::path{}; };
    const auto flat = detect(steam_entry, linux_probe);
    const auto galaxy = detect(gog_entry, linux_probe);
    const auto amazon = detect(*find("amazon"), linux_probe);
    if (!flat || flat->program != L"flatpak" || flat->arguments != L"run com.valvesoftware.Steam" || !flat->prefix.empty()) return 45;
    if (!galaxy || galaxy->prefix != prefix || galaxy->program != prefix / L"drive_c" / L"Program Files (x86)" / L"GOG Galaxy" / L"GalaxyClient.exe") return 46;
    if (!amazon || amazon->program != prefix / L"drive_c" / L"users" / L"u" / L"AppData" / L"Local" / L"Amazon Games" / L"App" / L"Amazon Games.exe") return 47;
    if (!detect(lutris, linux_probe) || detect(*find("epic"), linux_probe) || !prefixPath(L"D:\\x.exe", prefix, L"u").empty()) return 48;
    // Removal: winget on Windows when it knows the package, flatpak for a Flatpak, nothing for a prefix's Windows build.
    if (removal(steam_entry, {true, true, false, false}, *steam_found) != Removal::winget || removal(steam_entry, {true, false, false, false}, *steam_found) != Removal::none ||
        removal(*find("battlenet"), {true, true, false, false}, *steam_found) != Removal::none) return 49;
    if (removal(steam_entry, {false, false, true, true}, *flat) != Removal::flatpak || removal(gog_entry, {false, false, true, true}, *galaxy) != Removal::none ||
        removal(lutris, {false, false, true, true}, *detect(lutris, linux_probe)) != Removal::none) return 50;
    const auto out = wingetUninstallArguments(steam_entry);
    if (out[0] != L"uninstall" || out[2] != L"Valve.Steam" || std::find(out.begin(), out.end(), L"--exact") == out.end() ||
        flatpakUninstallArguments(steam_entry) != std::vector<std::wstring>{L"uninstall", L"--user", L"--noninteractive", L"-y", L"com.valvesoftware.Steam"}) return 51;
    return 0;
}

int installation(const std::filesystem::path& root) {
    using namespace launcher_install;
    const auto config = root / L"config";
    // The prefix a Windows launcher went into is remembered per launcher, and only for the catalogue's.
    const std::filesystem::path prefix = root / L"prefixes" / L"gog";
    if (!launchers::winePrefix(config, "gog").empty() || !launchers::rememberWinePrefix(config, "gog", prefix) || launchers::winePrefix(config, "gog") != prefix ||
        launchers::rememberWinePrefix(config, "nope", prefix) || launchers::rememberWinePrefix(config, "epic", L"relative") ||
        !launchers::rememberWinePrefix(config, "epic", root / L"prefixes" / L"epic") || launchers::winePrefix(config, "gog") != prefix) return 60;

    std::vector<std::wstring> log;
    std::vector<Phase> phases;
    std::optional<unsigned long> tool = 0ul;
    bool network = true, signature = true, opens = true;
    Backend backend;
    backend.winget = L"winget.exe"; backend.flatpak = L"flatpak";
    backend.run = [&](const std::filesystem::path& program, const std::vector<std::wstring>& arguments, const std::function<bool()>&) {
        log.push_back(L"run " + program.wstring() + L' ' + arguments.front() + L' ' + arguments.back());
        return tool;
    };
    backend.download = [&](const std::wstring& url, const std::filesystem::path& file, const std::function<bool()>&) {
        log.push_back(L"download " + url);
        if (!network) return false;
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << "MZ";
        return true;
    };
    backend.trusted = [&](const std::filesystem::path&) { log.push_back(L"verify"); return signature; };
    backend.open = [&](const std::filesystem::path& file) { log.push_back(L"open " + file.filename().wstring()); return opens; };
    backend.wine = [&](const launchers::Entry& entry, const std::filesystem::path& file, const std::filesystem::path& where, const std::function<bool()>&) -> std::optional<unsigned long> {
        log.push_back(L"wine " + std::wstring(entry.name) + L' ' + file.filename().wstring() + L' ' + where.filename().wstring());
        return 1ul; // Installers often end with 1 under Wine after a good install.
    };
    const auto& steam_entry = *launchers::find("steam");
    const auto run = [&](const launchers::Entry& entry, std::vector<Method> methods, std::filesystem::path where = {}, bool cancelled = false) {
        log.clear(); phases.clear();
        return install({&entry, std::move(methods), config, std::move(where)}, backend, [=] { return cancelled; }, [&](Phase phase) { phases.push_back(phase); });
    };

    // winget does it: nothing is downloaded.
    auto result = run(steam_entry, {Method::winget, Method::installer, Method::page});
    if (!result.ok() || result.method != Method::winget || result.handed_over || log != std::vector<std::wstring>{L"run winget.exe install --disable-interactivity"} ||
        phases != std::vector<Phase>{Phase::installing}) return 61;
    tool = 0x8A150061ul; // Already installed.
    if (!run(steam_entry, {Method::winget}).ok()) return 62;
    // winget fails: the vendor's installer, checked and opened; the user finishes it.
    tool = 5ul;
    result = run(steam_entry, {Method::winget, Method::installer, Method::page});
    if (!result.ok() || result.method != Method::installer || !result.handed_over || log.size() != 4 ||
        log[1] != L"download https://cdn.akamai.steamstatic.com/client/installer/SteamSetup.exe" || log[2] != L"verify" || log[3] != L"open SteamSetup.exe" ||
        phases != std::vector<Phase>{Phase::installing, Phase::downloading, Phase::verifying, Phase::waiting}) return 63;
    if (installerFile(config, steam_entry) != config / L"store-hub" / L"installers" / L"SteamSetup.exe" || !std::filesystem::exists(installerFile(config, steam_entry))) return 64;
    // An installer without a valid signature is deleted, never opened.
    signature = false;
    result = run(steam_entry, {Method::installer});
    if (result.ok() || result.error != Error::signature || std::filesystem::exists(installerFile(config, steam_entry)) ||
        std::find(log.begin(), log.end(), L"open SteamSetup.exe") != log.end()) return 65;
    signature = true; network = false;
    if (run(steam_entry, {Method::installer}).error != Error::network) return 66;
    network = true; opens = false;
    if (run(steam_entry, {Method::installer}).error != Error::start) return 67;
    opens = true;
    // winget alone failing reports its exit code; nothing to install with reports that.
    result = run(steam_entry, {Method::winget});
    if (result.error != Error::failed || result.exit_code != 5) return 68;
    if (run(*launchers::find("esde"), {Method::page}).error != Error::unavailable || run(steam_entry, {Method::winget}, {}, true).error != Error::cancelled) return 69;
    tool.reset();
    if (run(steam_entry, {Method::winget}).error != Error::start) return 70;

    // Linux: Flathub for this user; a failed remote stops before the install.
    tool = 0ul;
    result = run(steam_entry, {Method::flatpak});
    if (!result.ok() || log != std::vector<std::wstring>{L"run flatpak remote-add https://dl.flathub.org/repo/flathub.flatpakrepo", L"run flatpak install com.valvesoftware.Steam"}) return 71;
    tool = 1ul;
    result = run(steam_entry, {Method::flatpak});
    if (result.error != Error::failed || log.size() != 1) return 72;
    // Wine: the installer in the chosen prefix; its exit code does not decide. Without a prefix it does not run.
    result = run(*launchers::find("gog"), {Method::wine, Method::page}, prefix);
    if (!result.ok() || result.method != Method::wine || result.handed_over || log.size() != 2 || log[1] != L"wine GOG Galaxy GOG_Galaxy_2.0.exe gog" ||
        phases != std::vector<Phase>{Phase::downloading, Phase::installing}) return 73;
    if (run(*launchers::find("gog"), {Method::wine}).error != Error::unavailable || !log.empty()) return 74;
    // Uninstalling: the tool's own command, its failure and a missing tool reported.
    log.clear(); tool = 0ul;
    result = uninstall(steam_entry, launchers::Removal::winget, backend, {});
    if (!result.ok() || log != std::vector<std::wstring>{L"run winget.exe uninstall --disable-interactivity"}) return 75;
    log.clear();
    result = uninstall(steam_entry, launchers::Removal::flatpak, backend, {});
    if (!result.ok() || log != std::vector<std::wstring>{L"run flatpak uninstall com.valvesoftware.Steam"}) return 76;
    tool = 3ul;
    if (uninstall(steam_entry, launchers::Removal::winget, backend, {}).error != Error::failed || uninstall(steam_entry, launchers::Removal::none, backend, {}).error != Error::unavailable) return 77;
    backend.winget.clear();
    if (uninstall(steam_entry, launchers::Removal::winget, backend, {}).error != Error::unavailable) return 78;
    return 0;
}

int grid() {
    using namespace plugin_ui;
    Page page; page.tiled = true;
    const auto add = [&](bool tile, bool actionable = true) {
        Row row; row.tile = tile; row.heading = !tile; row.title = L"x";
        if (tile && actionable) row.command = "go";
        page.rows.push_back(row);
    };
    add(false); for (int i = 0; i < 5; ++i) add(true);   // Rows 1-5: a line of three, a line of two.
    add(false); for (int i = 0; i < 4; ++i) add(true);   // Rows 7-10: a line of three, a line of one.
    page.arrange(940, 280); // (940 + 20) / 300 = 3 columns.
    if (page.columns != 3 || page.tile_width != 300 || page.tile_height != 169) return 80;
    const float line = page.tile_height + Page::tile_caption + Page::tile_gap;
    if (page.rows[1].top != 48 || page.rows[3].top != 48 || page.rows[3].column != 2 || page.rows[4].top != 48 + line || page.rows[4].column != 0 ||
        page.rows[6].top != 48 + 2 * line || page.rows[7].top != 96 + 2 * line || page.rows[10].column != 0 || page.total != 96 + 4 * line) return 81;
    page.focus = page.next(1);
    if (page.focus != 1) return 82;
    const auto go = [&](int dx, int dy) { page.focus = page.step(dx, dy); return page.focus; };
    if (go(1, 0) != 2 || go(1, 0) != 3 || go(1, 0) != 3) return 83;            // The line's end: a boundary.
    if (go(0, 1) != 5 || go(0, 1) != 8 || go(0, 1) != 10 || go(0, 1) != 10) return 84; // Down: the nearest column, across the heading.
    if (go(0, -1) != 7 || go(1, 0) != 8 || go(0, -1) != 5 || go(0, -1) != 2 || go(0, -1) != -1) return 85; // Up to the back button.
    if (go(0, 1) != 1 || go(-1, 0) != -1 || go(1, 0) != 1) return 86;         // Left of the first column: back; from back: the first tile.
    // The pointer: a tile's picture and caption hit it, the gap beside it does not.
    const Layout layout{1280, 720};
    page.arranged_width = 0; page.arrange(layout.contentWidth());
    const auto box = layout.tile(page, 2);
    if (layout.hit(page, box.left + 5, box.top + 5) != 2 || layout.hit(page, box.right - 2, box.bottom - 2) != 2 || layout.hit(page, box.right + Page::tile_gap * 0.5f, box.top + 5) != -2) return 87;
    // A narrower room: two columns, and the same width again changes nothing.
    page.arrange(500, 280);
    if (page.columns != 2 || page.rows[3].column != 0 || page.rows[3].top == page.rows[2].top) return 88;
    return 0;
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-store-hub-" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    int code = promotions(root);
    if (!code) code = catalogue();
    if (!code) code = installation(root);
    if (!code) code = grid();
    std::filesystem::remove_all(root, ec);
    if (code) { std::cerr << "Store hub regression failed at checkpoint " << code << '\n'; return code; }
    std::cout << "Store hub: promotions (parsers, merge, cache), launcher catalogue, detection, installation flows and the tile grid passed\n";
    return 0;
}
