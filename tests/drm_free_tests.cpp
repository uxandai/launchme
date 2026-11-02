#include "../src/library/drm_free.hpp"
#include "../src/artwork/cover_cache.hpp"
#include "../src/platform/text_encoding.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
namespace fs = std::filesystem;

// A minimal PE image: enough for the scanner's header check (GUI or console, x64).
void executable(const fs::path& file, size_t size, bool console = false) {
    fs::create_directories(file.parent_path());
    std::string bytes(std::max<size_t>(size, 512), '\0');
    bytes[0] = 'M'; bytes[1] = 'Z'; bytes[0x3C] = static_cast<char>(0x80);
    bytes[0x80] = 'P'; bytes[0x81] = 'E';
    bytes[0x84] = static_cast<char>(0x64); bytes[0x85] = static_cast<char>(0x86); // AMD64
    bytes[0x80 + 22] = 0x22;                                                         // Executable image.
    bytes[0x80 + 24] = 0x0b; bytes[0x80 + 25] = 0x02;                                // PE32+
    bytes[0x80 + 24 + 68] = console ? 3 : 2;
    std::ofstream(file, std::ios::binary) << bytes;
}
#ifndef _WIN32
// A minimal ELF header: an executable (type 2) or a shared library (type 3, no interpreter).
void elf(const fs::path& file, size_t size, bool library = false) {
    fs::create_directories(file.parent_path());
    std::string bytes(std::max<size_t>(size, 128), '\0');
    bytes[0] = 0x7F; bytes[1] = 'E'; bytes[2] = 'L'; bytes[3] = 'F'; bytes[4] = 2; bytes[5] = 1;
    bytes[16] = library ? 3 : 2; bytes[18] = 62; // x86-64.
    std::ofstream(file, std::ios::binary) << bytes;
}
#endif
void text(const fs::path& file, const std::string& value) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << value;
}
HttpResponse response(uint32_t status, const std::string& body) {
    HttpResponse result; result.status = status; result.body.assign(body.begin(), body.end()); return result;
}
const drm_free::Entry* byFolder(const drm_free::Catalog& catalog, const fs::path& folder) {
    for (const auto& entry : catalog.games) if (drm_free::folderKey(entry.folder) == drm_free::folderKey(folder)) return &entry;
    return nullptr;
}
} // namespace

int main() {
    using namespace drm_free;
    i18n::setLanguage(i18n::Language::pl);

    // (1) Folder names become RAWG queries; a year is kept as a hint.
    const std::pair<const wchar_t*, const wchar_t*> titles[]{
        {L"The.Witcher.3.Wild.Hunt.GOTY-GOG", L"The Witcher 3 Wild Hunt"},
        {L"Hollow_Knight_v1.5.78", L"Hollow Knight"},
        {L"Stardew Valley [GOG]", L"Stardew Valley"},
        {L"Baldurs.Gate.3.v4.1.1.3767641", L"Baldurs Gate 3"},
        {L"Factorio_x64_1.1.110", L"Factorio"},
        {L"HollowKnight", L"Hollow Knight"},
        {L"DOOMEternal", L"DOOM Eternal"},
        {L"Portal2", L"Portal 2"},
        {L"BALLxPIT", L"BALLxPIT"},
        {L"RiverCityGirls", L"River City Girls"},
        {L"Half-Life 2", L"Half-Life 2"},
        {L"Final-Fantasy-VIII", L"Final-Fantasy-VIII"},
        {L"S.T.A.L.K.E.R.Shadow.of.Chernobyl", L"S.T.A.L.K.E.R. Shadow of Chernobyl"},
        {L"Disco Elysium - The Final Cut", L"Disco Elysium - The Final Cut"},
        {L"Celeste", L"Celeste"}};
    for (const auto& [folder, expected] : titles)
        if (cleanTitle(folder).text != expected) { std::wcerr << L"cleanTitle(" << folder << L") = " << cleanTitle(folder).text << L'\n'; return 1; }
    if (cleanTitle(L"Celeste (2018)").text != L"Celeste" || cleanTitle(L"Celeste (2018)").year != 2018 ||
        cleanTitle(L"Prey 2017").text != L"Prey" || cleanTitle(L"Prey 2017").year != 2017 || cleanTitle(L"2048").text != L"2048") return 2;
    if (!genericProduct(L"Unity Player") || !genericProduct(L"UnrealEngine") || genericProduct(L"Hollow Knight")) return 3;
    if (!excludedExecutable(L"unins000.exe") || !excludedExecutable(L"UnityCrashHandler64.exe") || !excludedExecutable(L"vc_redist.x64.exe") ||
        !excludedExecutable(L"CrashReportClient.exe") || excludedExecutable(L"Celeste.exe") || excludedExecutable(L"witcher3.exe")) return 4;
#ifdef _WIN32
    if (folderId(L"D:\\Gry\\Celeste") != folderId(L"d:/gry/celeste/") || folderId(L"D:\\Gry\\Celeste").size() != 16 ||
        folderId(L"D:\\Gry\\Celeste") == folderId(L"D:\\Gry\\Celeste 2")) return 5;
    if (!inside(L"D:\\Gry\\Celeste", L"D:\\Gry") || inside(L"D:\\Gry2\\Celeste", L"D:\\Gry") || inside(L"D:\\Gry", L"D:\\Gry") ||
        !inside(L"D:\\Gry", L"D:\\") || validRoot(L"Gry") || !validRoot(L"D:\\Gry")) return 6;
#else
    // Linux paths keep their case.
    if (folderId(L"/mnt/Gry/Celeste") != folderId(L"/mnt/Gry/Celeste/") || folderId(L"/mnt/Gry/Celeste").size() != 16 ||
        folderId(L"/mnt/Gry/Celeste") == folderId(L"/mnt/gry/celeste") || folderId(L"/mnt/Gry/Celeste") == folderId(L"/mnt/Gry/Celeste 2")) return 5;
    if (!inside(L"/mnt/Gry/Celeste", L"/mnt/Gry") || inside(L"/mnt/Gry2/Celeste", L"/mnt/Gry") || inside(L"/mnt/Gry", L"/mnt/Gry") ||
        !inside(L"/mnt/Gry", L"/") || validRoot(L"Gry") || !validRoot(L"/mnt/Gry")) return 6;
#endif

    // (2) A real folder tree: engines, a folder of games, a GOG install, noise and other sources.
#ifdef _WIN32
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const auto base = fs::path(temp) / (L"launchme-drm-free-" + std::to_wstring(GetCurrentProcessId()));
#else
    const auto base = fs::temp_directory_path() / (L"launchme-drm-free-" + std::to_wstring(GetCurrentProcessId()));
#endif
    std::error_code ignored_error;
    fs::remove_all(base, ignored_error);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code e; fs::remove_all(path, e); } } cleanup{base};
    const auto root = base / L"Gry";
    executable(root / L"Celeste" / L"Celeste.exe", 200 * 1024);
    fs::create_directories(root / L"Celeste" / L"Celeste_Data");
    executable(root / L"Celeste" / L"UnityCrashHandler64.exe", 2 * 1024 * 1024);
    executable(root / L"Celeste" / L"unins000.exe", 3 * 1024 * 1024);
    executable(root / L"Hollow_Knight_v1.5.78" / L"hollow_knight.exe", 700 * 1024);
    fs::create_directories(root / L"Hollow_Knight_v1.5.78" / L"hollow_knight_Data");
    executable(root / L"Hollow_Knight_v1.5.78" / L"_CommonRedist" / L"big.exe", 8 * 1024 * 1024);
    executable(root / L"SpaceGame" / L"SpaceGame.exe", 250 * 1024);
    executable(root / L"SpaceGame" / L"SpaceGame" / L"Binaries" / L"Win64" / L"SpaceGame-Win64-Shipping.exe", 4 * 1024 * 1024);
    executable(root / L"SpaceGame" / L"Engine" / L"Binaries" / L"Win64" / L"CrashReportClient.exe", 4 * 1024 * 1024);
    executable(root / L"Indie" / L"Alpha" / L"Alpha.exe", 300 * 1024);
    executable(root / L"Indie" / L"Beta Game" / L"bin" / L"Beta.exe", 300 * 1024);
    executable(root / L"Fixture GOG" / L"bin" / L"game.exe", 300 * 1024);
    executable(root / L"Fixture GOG" / L"editor.exe", 9 * 1024 * 1024);
    text(root / L"Fixture GOG" / L"goggame-1207.info",
         R"({"gameId":"1207","rootGameId":"1207","name":"GOG Fixture","playTasks":[{"category":"game","isPrimary":true,"type":"FileTask",)"
         R"("path":"bin\\game.exe","workingDir":"bin","arguments":"-windowed"}]})");
    text(root / L"Fixture GOG" / L"steam_appid.txt", "620\n");
    executable(root / L"Server Kit" / L"server.exe", 5 * 1024 * 1024, true);
    executable(root / L"Server Kit" / L"Server Kit.exe", 400 * 1024);
    text(root / L"Docs" / L"readme.txt", "not a game");
    executable(root / L"EpicGame" / L"EpicGame.exe", 300 * 1024);
    fs::create_directories(root / L"EpicGame" / L".egstore");
    executable(root / L"Known" / L"Known.exe", 300 * 1024);
    // An emulator's folder is no game, whatever else it holds; a game named like one still is.
    executable(root / L"PS4" / L"App" / L"shadPS4.exe", 30 * 1024 * 1024);
    executable(root / L"PS4" / L"Versions" / L"extractor.exe", 300 * 1024);
    executable(root / L"Dolphin Olympics" / L"Dolphin Olympics.exe", 300 * 1024);
    // A cryptic folder name: the executable's name is the second RAWG query.
    executable(root / L"RCG" / L"Epic-588cec64" / L"RiverCityGirls.exe", 900 * 1024);
    executable(root / L"SteamLibrary" / L"steamapps" / L"common" / L"X" / L"x.exe", 300 * 1024);

    Catalog catalog; catalog.roots = {root};
    ScanOptions options; options.full = true; options.now = 1000;
    options.known.insert(folderKey(root / L"Known"));
    Progress last;
    const auto scanned = scan(catalog, options, {}, [&](Progress progress) { last = progress; });
    const auto expect = [&](const fs::path& folder, const fs::path& exe) {
        const auto* entry = byFolder(scanned, folder);
        if (!entry) { std::wcerr << L"missing " << folder.wstring() << L'\n'; return false; }
        if (folderKey(entry->executable) != folderKey(exe)) { std::wcerr << folder.wstring() << L" chose " << entry->executable.wstring() << L'\n'; return false; }
        return entry->found == 1000 && !entry->missing && entry->id == folderId(folder);
    };
    if (!expect(root / L"Celeste", root / L"Celeste" / L"Celeste.exe") ||
        !expect(root / L"Hollow_Knight_v1.5.78", root / L"Hollow_Knight_v1.5.78" / L"hollow_knight.exe") ||
        !expect(root / L"SpaceGame", root / L"SpaceGame" / L"SpaceGame.exe") ||
        !expect(root / L"Indie" / L"Alpha", root / L"Indie" / L"Alpha" / L"Alpha.exe") ||
        !expect(root / L"Indie" / L"Beta Game", root / L"Indie" / L"Beta Game" / L"bin" / L"Beta.exe") ||
        !expect(root / L"Fixture GOG", root / L"Fixture GOG" / L"bin" / L"game.exe") ||
        !expect(root / L"Server Kit", root / L"Server Kit" / L"Server Kit.exe") ||
        !expect(root / L"Dolphin Olympics", root / L"Dolphin Olympics" / L"Dolphin Olympics.exe") ||
        !expect(root / L"RCG", root / L"RCG" / L"Epic-588cec64" / L"RiverCityGirls.exe")) return 10;
    if (scanned.games.size() != 9 || byFolder(scanned, root / L"Docs") || byFolder(scanned, root / L"EpicGame") || byFolder(scanned, root / L"PS4") ||
        byFolder(scanned, root / L"Known") || byFolder(scanned, root / L"Indie") || byFolder(scanned, root / L"SteamLibrary")) return 11;
    if (byFolder(scanned, root / L"RCG")->title != L"RCG" || byFolder(scanned, root / L"RCG")->product != L"River City Girls" ||
        !byFolder(scanned, root / L"Celeste")->product.empty()) return 14;
    const auto* gog = byFolder(scanned, root / L"Fixture GOG");
    const auto* hollow = byFolder(scanned, root / L"Hollow_Knight_v1.5.78");
    if (gog->title != L"GOG Fixture" || gog->arguments != L"-windowed" || folderKey(gog->working_directory) != folderKey(root / L"Fixture GOG" / L"bin") ||
        gog->steam_app_id != 620 || hollow->title != L"Hollow Knight" || hollow->name != L"Hollow Knight" || scanned.scanned != 1000 ||
        last.done != last.total || last.total < 9) return 12;
    const auto game = makeGame(*gog);
    if (game.source != GameSource::drmfree || gameKey(game) != "drmfree:" + gog->id || !game.installed || game.play_action.arguments != L"-windowed" ||
        game.install_directory != gog->folder || game.executable != gog->executable || std::string(sourceSlug(GameSource::drmfree)) != "drmfree") return 13;

    // (2b) A root that is one game's own folder (its program at the top, beside an installed game's files):
    // that one game, not its mod tool's subfolder; a root with a stray program at the top but no such
    // files stays a folder of games.
    {
        const auto single = base / L"Football Life 2025";
        executable(single / L"FL_2025.exe", 30 * 1024 * 1024);
        executable(single / L"Settings.exe", 400 * 1024);
        executable(single / L"Uninstall FL25.exe", 300 * 1024);
        text(single / L"steam_api64.dll", "dll");
        executable(single / L"SiderAddons" / L"sider.exe", 2 * 1024 * 1024);
        Catalog one; one.roots = {single};
        const auto found = scan(one, options);
        if (found.games.size() != 1 || folderKey(found.games[0].folder) != folderKey(single) ||
            folderKey(found.games[0].executable) != folderKey(single / L"FL_2025.exe") || byFolder(found, single / L"SiderAddons")) return 15;
        // Its file round trip and an unplugged drive keep it, as any other game.
        if (parse(format(found)).catalog != found || rootOf(found, single) != &found.roots[0]) return 16;
        const auto shelf = base / L"Shelf";
        executable(shelf / L"tool.exe", 30 * 1024 * 1024);
        executable(shelf / L"First" / L"First.exe", 300 * 1024);
        executable(shelf / L"Second" / L"Second.exe", 300 * 1024);
        Catalog many; many.roots = {shelf};
        const auto shelved = scan(many, options);
        if (!byFolder(shelved, shelf / L"First") || !byFolder(shelved, shelf / L"Second") || byFolder(shelved, shelf)) return 17;
    }

    // (3) The file round trip keeps everything; damaged entries are dropped one by one.
    const auto reloaded = parse(format(scanned));
    if (!reloaded.readable || reloaded.catalog != scanned) return 20;
    if (parse("{").readable || parse(R"({"version":2,"roots":[],"games":[]})").readable) return 21;
    auto foreign = scanned; foreign.games[0].id = "0123456789abcdef"; // Not the folder's ID.
    if (parse(format(foreign)).catalog.games.size() != scanned.games.size() - 1) return 22;
    const auto file = base / L"drm-free.json";
    if (!save(scanned, file) || load(file).catalog != scanned) return 23;

    // (4) Incremental scans keep unchanged folders and the user's choices.
    auto edited = scanned;
    for (auto& entry : edited.games) {
        if (entry.folder == root / L"Server Kit") { entry.executable = root / L"Server Kit" / L"server.exe"; entry.manual_executable = true; }
        if (entry.folder == root / L"Celeste") entry.ignored = true;
        if (entry.folder == root / L"SpaceGame") { entry.rawg_id = 99; entry.name = L"Space Game Deluxe"; }
    }
    options.full = false; options.now = 2000;
    auto again = scan(edited, options);
    const auto* manual = byFolder(again, root / L"Server Kit");
    if (again.games.size() != 9 || !manual || !manual->manual_executable || manual->executable.filename() != L"server.exe" ||
        !byFolder(again, root / L"Celeste")->ignored || byFolder(again, root / L"SpaceGame")->name != L"Space Game Deluxe" ||
        byFolder(again, root / L"Celeste")->found != 1000) return 30;
    // A new game appears, a removed one disappears.
    executable(root / L"Newcomer" / L"Newcomer.exe", 300 * 1024);
    fs::remove_all(root / L"SpaceGame");
    again = scan(edited, options);
    if (!byFolder(again, root / L"Newcomer") || byFolder(again, root / L"SpaceGame") || byFolder(again, root / L"Newcomer")->found != 2000) return 31;
    // Cancelled before anything: the previous catalog survives intact.
    const auto cancelled = scan(edited, options, [] { return true; });
    if (cancelled.games != edited.games || cancelled.scanned != edited.scanned) return 32;
    // An unplugged root keeps its games as missing.
    auto offline = edited; offline.roots = {base / L"Unplugged"};
    for (auto& entry : offline.games) entry.folder = base / L"Unplugged" / entry.folder.filename(), entry.id = folderId(entry.folder);
    const auto unplugged = scan(offline, options);
    if (unplugged.games.size() != offline.games.size() || std::any_of(unplugged.games.begin(), unplugged.games.end(), [](const auto& e) { return !e.missing; })) return 33;

    // (5) Merging a scan made on an older snapshot keeps newer user choices and removed roots.
    auto current = edited;
    for (auto& entry : current.games) if (entry.folder == root / L"Hollow_Knight_v1.5.78") entry.ignored = true;
    const auto merged = merge(current, scanned);
    if (!byFolder(merged, root / L"Hollow_Knight_v1.5.78")->ignored || !byFolder(merged, root / L"Server Kit")->manual_executable ||
        byFolder(merged, root / L"Server Kit")->executable.filename() != L"server.exe") return 40;
    auto no_roots = current; no_roots.roots.clear();
    if (!merge(no_roots, scanned).games.empty()) return 41;

    // (6) RAWG: the best result wins, subtitles and years count, other media lose.
    using rawg::Candidate;
    const std::vector<Candidate> hades{{1, L"Hades II", L"2025-09-25", L"PC"}, {2, L"Hades", L"2020-09-17", L"PC, Nintendo Switch"},
                                       {3, L"Hades Original Soundtrack", L"2020-09-17", L"PC"}};
    if (bestMatch({L"Hades", 0}, hades) != 1u) return 50;
    const std::vector<Candidate> prey{{4, L"Prey", L"2006-07-11", L"PC"}, {5, L"Prey", L"2017-05-05", L"PC, PlayStation 4"}};
    if (bestMatch({L"Prey", 2017}, prey) != 1u || bestMatch({L"Prey", 2006}, prey) != 0u) return 51;
    if (bestMatch({L"Disco Elysium", 0}, {{6, L"Disco Elysium - The Final Cut", L"2021-03-30", L"PC"}}) != 0u) return 52;
    if (bestMatch({L"The Witcher 3 Wild Hunt", 0}, {{7, L"The Witcher 3: Wild Hunt", L"2015-05-18", L"PC"}, {8, L"The Witcher", L"2007-10-24", L"PC"}}) != 0u) return 53;
    if (bestMatch({L"Something Completely Different", 0}, {{9, L"Unrelated Title", L"", L"PC"}})) return 54;
    if (bestMatch({L"Doom 2", 0}, {{10, L"DOOM II", L"1994-09-30", L"PC"}, {11, L"DOOM", L"1993-12-10", L"PC"}}) != 0u) return 55;

    // (7) Identification with a fake RAWG: metadata, rating, artwork source and the new name.
    auto waiting = scanned;
    std::vector<std::wstring> requests;
    IdentifyBackend backend;
    backend.now = [] { return uint64_t{1790000000}; };
    backend.wait = [](std::chrono::milliseconds) { return true; };
    backend.get = [&](const std::wstring& url, const std::wstring&, size_t) {
        requests.push_back(url);
        if (url.find(L"/games/73/stores?") != std::wstring::npos)
            return response(200, R"({"results":[{"game_id":73,"url":"https://store.steampowered.com/app/504230/Celeste/"}]})");
        if (url.find(L"/games/80/stores?") != std::wstring::npos) return response(200, R"({"results":[]})");
        if (url.find(L"/games/73?") != std::wstring::npos)
            return response(200, R"({"id":73,"name":"Celeste","slug":"celeste","description_raw":"Fixture","released":"2018-01-25","rating":4.4,"ratings_count":10})");
        if (url.find(L"/games/80?") != std::wstring::npos)
            return response(200, R"({"id":80,"name":"GOG Fixture: Remastered","slug":"gog-fixture","released":"2020-01-01"})");
        if (url.find(L"search=Celeste") != std::wstring::npos)
            return response(200, R"({"results":[{"id":12,"name":"Celeste Classic","released":"2016-01-01","platforms":[{"platform":{"id":4,"name":"PC"}}]},)"
                                 R"({"id":73,"name":"Celeste","released":"2018-01-25","platforms":[{"platform":{"id":4,"name":"PC"}}]}]})");
        if (url.find(L"/games/81/stores?") != std::wstring::npos) return response(200, R"({"results":[]})");
        if (url.find(L"/games/81?") != std::wstring::npos) return response(200, R"({"id":81,"name":"River City Girls","slug":"river-city-girls"})");
        if (url.find(L"search=River") != std::wstring::npos)
            return response(200, R"({"results":[{"id":81,"name":"River City Girls","released":"2019-09-05","platforms":[{"platform":{"id":4,"name":"PC"}}]}]})");
        if (url.find(L"search=GOG") != std::wstring::npos)
            return response(200, R"({"results":[{"id":80,"name":"GOG Fixture: Remastered","released":"2020-01-01","platforms":[{"platform":{"id":4,"name":"PC"}}]}]})");
        return response(200, R"({"results":[]})");
    };
    const IdentifyPaths paths{base / L"cache" / L"metadata", base / L"cache" / L"ratings", base / L"cache" / L"covers"};
    size_t notifications = 0;
    identify(waiting, L"fixture-key", paths, {}, [&](const Catalog&) { ++notifications; }, backend);
    const auto* identified = byFolder(waiting, root / L"Celeste");
    if (!identified || identified->rawg_id != 73 || identified->name != L"Celeste" || identified->steam_app_id != 504230 ||
        identified->checked != 1790000000 || !notifications) return 60;
    // "Always the best result": a subtitled edition is still taken, under RAWG's name.
    const auto* gog_identified = byFolder(waiting, root / L"Fixture GOG");
    if (gog_identified->rawg_id != 80 || gog_identified->name != L"GOG Fixture: Remastered" || gog_identified->steam_app_id != 620 ||
        byFolder(waiting, root / L"Indie" / L"Alpha")->checked != 1790000000 || byFolder(waiting, root / L"Indie" / L"Alpha")->rawg_id) return 61;
    // "RCG" finds nothing; the executable's name does.
    if (byFolder(waiting, root / L"RCG")->rawg_id != 81 || byFolder(waiting, root / L"RCG")->name != L"River City Girls") return 67;
    const auto identified_game = makeGame(*identified);
    const auto metadata = rawg::load(identified_game, paths.metadata);
    if (!metadata || metadata->id != 73 || metadata->steam_app_id != 504230 || !selectedArtworkSource(identified_game, paths.covers).steam ||
        selectedArtworkSource(identified_game, paths.covers).id != 504230) return 62;
    // steam_appid.txt already names the Steam page: no store lookup for that game.
    if (std::any_of(requests.begin(), requests.end(), [](const auto& url) { return url.find(L"/games/80/stores") != std::wstring::npos; })) return 63;
    // Unmatched titles wait a week; identified ones are never asked again.
    requests.clear();
    identify(waiting, L"fixture-key", paths, {}, {}, backend);
    if (!requests.empty()) return 64;
    // A refused key pauses matching instead of marking games as unknown.
    auto refused = scanned;
    backend.get = [&](const std::wstring&, const std::wstring&, size_t) { return response(429, "{}"); };
    identify(refused, L"fixture-key", paths, {}, {}, backend);
    if (refused.rawg_pause != 1790000000 + IdentifyLimits{}.pause ||
        std::any_of(refused.games.begin(), refused.games.end(), [](const Entry& e) { return e.checked || e.rawg_id; })) return 65;
    // No key: nothing is asked.
    requests.clear();
    auto keyless = scanned;
    identify(keyless, L"", paths, {}, {}, backend);
    if (keyless != scanned) return 66;

#ifndef _WIN32
    // (8) Native Linux games: ELF executables and launcher scripts, ahead of a Windows build beside them.
    const auto native = base / L"Linux";
    elf(native / L"Celeste" / L"Celeste.x86_64", 300 * 1024);
    fs::create_directories(native / L"Celeste" / L"Celeste_Data");
    executable(native / L"Celeste" / L"Celeste.exe", 300 * 1024);
    elf(native / L"Celeste" / L"UnityPlayer.so", 30 * 1024 * 1024, true);
    elf(native / L"Factorio" / L"bin" / L"x64" / L"factorio", 40 * 1024 * 1024);
    elf(native / L"Factorio" / L"bin" / L"x64" / L"libsteam_api.so", 400 * 1024, true);
    text(native / L"GOG Linux" / L"start.sh", "#!/bin/bash\nexec game/run\n");
    text(native / L"GOG Linux" / L"gameinfo", "Linux Fixture\n1.0\n");
    text(native / L"GOG Linux" / L"uninstall-Linux Fixture.sh", "#!/bin/bash\n");
    elf(native / L"GOG Linux" / L"game" / L"LinuxFixture", 9 * 1024 * 1024);
    elf(native / L"GOG Linux" / L"support" / L"helper", 9 * 1024 * 1024);
    text(native / L"Notes" / L"README", "not an executable");
    text(native / L"Notes" / L"notes.sh", "echo no shebang");
    Catalog linux_catalog; linux_catalog.roots = {native};
    ScanOptions linux_options; linux_options.full = true; linux_options.now = 3000;
    const auto linux_scan = scan(linux_catalog, linux_options);
    const auto chose = [&](const fs::path& folder, const fs::path& exe) {
        const auto* entry = byFolder(linux_scan, folder);
        if (!entry) { std::wcerr << L"missing " << folder.wstring() << L'\n'; return false; }
        if (entry->executable != exe) { std::wcerr << folder.wstring() << L" chose " << entry->executable.wstring() << L'\n'; return false; }
        return true;
    };
    if (!chose(native / L"Celeste", native / L"Celeste" / L"Celeste.x86_64") ||
        !chose(native / L"Factorio", native / L"Factorio" / L"bin" / L"x64" / L"factorio") ||
        !chose(native / L"GOG Linux", native / L"GOG Linux" / L"start.sh")) return 70;
    if (linux_scan.games.size() != 3 || byFolder(linux_scan, native / L"Notes") || byFolder(linux_scan, native / L"GOG Linux")->title != L"Linux Fixture" ||
        !byFolder(linux_scan, native / L"Celeste")->product.empty()) return 71;
    // A link to a folder inside a game is not followed; the root's own links are.
    fs::create_directory_symlink(native / L"Factorio", native / L"Factorio Link");
    if (!byFolder(scan(linux_catalog, linux_options), native / L"Factorio Link")) return 72;
#endif
    std::cout << "DRM-free titles, executable detection, scans, merge and RAWG matching passed\n";
    return 0;
}
