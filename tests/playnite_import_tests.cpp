// Import z Playnite: a real Playnite 10 library (tests/fixtures/playnite, written by LiteDB 4.1.4 with
// Playnite's mapper settings; see its generator) read from its folder and from a backup ZIP, matched to
// Launchme's keys and staged, then applied as the next start does.
#include "../src/library/litedb_v7.hpp"
#include "../src/library/playnite_import.hpp"
#include "../src/model/key_codec.hpp"
#include "../src/platform/profile_transfer.hpp"
#include "../src/platform/text_encoding.hpp"
#include "../src/platform/zip_archive.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
const playnite::Game* find(const playnite::Library& library, std::string_view name) {
    const auto found = std::find_if(library.games.begin(), library.games.end(), [&](const auto& game) { return game.name == name; });
    return found == library.games.end() ? nullptr : &*found;
}
}

int main() {
    const auto fixture = std::filesystem::path(text_encoding::wideLenient(LAUNCHME_SOURCE_DIR)) / L"tests" / L"fixtures" / L"playnite" / L"library";
    const auto base = std::filesystem::temp_directory_path() / (L"launchme-playnite-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(base);
    std::filesystem::create_directories(base);
#ifdef _WIN32
    _wputenv_s(L"LAUNCHME_CONFIG_DIR", (base / L"config").c_str());
#else
    setenv("LAUNCHME_CONFIG_DIR", (base / L"config").c_str(), 1);
#endif

    // (1) LiteDB: the header, the collection and malformed input.
    std::wstring error;
    const auto games_file = litedb_v7::load(fixture / L"games.db", error);
    if (!games_file || litedb_v7::collections(*games_file) != std::vector<std::string>{"Game"}) { std::wcerr << error << L"\n"; return 1; }
    if (litedb_v7::documents(*games_file).size() != 7) return 2;
    {
        std::ofstream(base / L"plain.db", std::ios::binary) << std::string(8192, 'x');
        std::wstring other;
        if (litedb_v7::load(base / L"plain.db", other) || other.empty()) return 3;
        if (litedb_v7::bson("") || litedb_v7::bson(std::string("\x05\0\0\0\x01", 5)) || litedb_v7::bson(std::string("\x10\0\0\0\x02" "a\0\xff\xff\xff\x7f" "b\0\0", 16))) return 4;
        const uint8_t guid[16] = {4, 3, 2, 1, 6, 5, 8, 7, 9, 10, 11, 12, 13, 14, 15, 16};
        if (litedb_v7::guidText(guid) != "01020304-0506-0708-090a-0b0c0d0e0f10") return 5;
    }

    // (2) The library: names, ids in .NET's order, dates, play time, actions, ROMs.
    const auto library = playnite::read(fixture, base / L"scratch", error);
    if (!library || library->games.size() != 7) { std::wcerr << error << L"\n"; return 6; }
    if (library->categories.at("11111111-2222-3333-4444-555555555555") != L"RPG" || library->tags.at("66666666-7777-8888-9999-aaaaaaaaaaaa") != L"Co-op") return 7;
    if (library->platforms.at("bbbbbbbb-cccc-dddd-eeee-ffffffffffff").specification != "nintendo_super_nes") return 8;
    const auto* portal = find(*library, "Portal 2");
    if (!portal || portal->id != "01020304-0506-0708-090a-0b0c0d0e0f10" || portal->plugin != "cb91dfc9-b977-43bf-8e70-55f46e410fab" ||
        portal->game_id != "620" || portal->playtime != 36000 || portal->play_count != 12 || !portal->favorite ||
        portal->last_activity != 1790856000000ull || portal->added != 1704164645000ull || portal->categories.size() != 1) return 9;
    const auto* own = find(*library, "Own Game");
    if (!own || own->actions.size() != 1 || own->actions[0].type != "File" || own->actions[0].path != L"{InstallDir}\\own.exe" || !own->actions[0].play) return 10;
    const auto* chrono = find(*library, "Chrono Trigger");
    if (!chrono || chrono->roms.size() != 1 || chrono->actions[0].type != "Emulator") return 11;
    if (playnite::storeKey(*portal) != "steam:620" || playnite::storeKey(*find(*library, "The Witcher 3")) != "gog:1207664643" ||
        playnite::storeKey(*find(*library, "Fortnite")) != "epic:Fortnite" || !playnite::storeKey(*find(*library, "Itch thing")).empty()) return 12;
    if (playnite::consoleFor({L"Nintendo SNES", "nintendo_super_nes"}) != L"SNES" || playnite::consoleFor({L"Sega Dreamcast", ""}) != L"Dreamcast" ||
        !playnite::consoleFor({L"Unknown Thing", ""}).empty()) return 13;

    // (3) The plan: history under this launcher's accounts, collections, favourites, own and emulated games.
    custom_games::Entry kept{"00000000000000aa", L"Mine", L"D:\\Games\\Own\\own.exe"};
    playnite::Accounts accounts{42, "gogger", ""};
    const auto plan = playnite::plan(*library, {}, accounts, {kept});
    const auto& state = plan.state;
#ifdef _WIN32
    if (plan.store != 3 || plan.own != 1 || plan.emulated != 1 || plan.skipped.size() != 2) return 14;
#else // Playnite's own and emulated games have Windows paths: nothing starts them here.
    if (plan.store != 3 || plan.own != 0 || plan.emulated != 0 || plan.skipped.size() != 4 || !plan.games.empty()) return 14;
#endif
    if (state.playtime_ms.at("steam-user:42/steam:620") != 36000000 || state.last_played.at("steam-user:42/steam:620") != 1790856000 ||
        state.first_seen.at("steam:620") != 1704164645 || state.playtime_ms.at("gog-user:gogger/gog:1207664643") != 720000000 ||
        state.playtime_ms.at("local/epic:Fortnite") != 60000) return 15;
    if (!state.collections.members.at("42:user-collections.favorite").at("steam:620") || !state.hidden.contains("gog:1207664643")) return 16;
    std::string rpg, coop;
    for (const auto& [id, name] : state.collections.names) { if (name == L"RPG") rpg = id; if (name == L"Co-op") coop = id; }
    if (!rpg.starts_with("local:") || !coop.starts_with("local:") || plan.collections != 2) return 17;
    if (!state.collections.members.at(rpg).at("steam-user:42/steam:620") || !state.collections.members.at(rpg).at("gog:1207664643")) return 18;
#ifdef _WIN32
    if (!state.collections.members.at(coop).at("custom:00000000000000aa")) return 18; // The own game already here keeps its id.
    if (plan.games.size() != 1 || plan.games[0].platform != L"SNES" || plan.games[0].path != std::filesystem::path(L"E:\\ROMs\\snes\\Chrono Trigger.sfc")) return 19;
#endif
    if (plan.pictures.size() != 2 || plan.pictures[0].key != "steam:620") return 20;
#ifdef _WIN32
    {
        const auto fresh = playnite::plan(*library, {}, accounts, {});
        const auto mine = std::find_if(fresh.games.begin(), fresh.games.end(), [](const auto& g) { return g.name == L"Own Game"; });
        if (fresh.games.size() != 2 || mine == fresh.games.end() || mine->path != std::filesystem::path(L"D:\\Games\\Own\\own.exe") ||
            mine->arguments != L"-windowed" || mine->directory != std::filesystem::path(L"D:\\Games\\Own")) return 21;
        playnite::Options none{}; none.playtime = none.favorites = none.hidden = none.categories = none.tags = none.artwork = none.own_games = none.emulated = false;
        const auto bare = playnite::plan(*library, none, accounts, {});
        if (!bare.games.empty() || !bare.pictures.empty() || !bare.state.playtime_ms.empty() || !bare.state.collections.names.empty()) return 22;
    }
#endif

    // (4) Staged and applied at the next start: merged into what is there.
    const auto config = base / L"config";
    std::filesystem::create_directories(config);
    LibraryState local; local.playtime_ms["steam-user:42/steam:620"] = 99000000;
    if (!saveLibraryState(local, config / L"library-state.ini") || !custom_games::save({kept}, config / L"custom-games.json")) return 23;
    if (!playnite::stage(plan, config, error) || !profile_transfer::pending(config)) { std::wcerr << error << L"\n"; return 24; }
    if (playnite::stage(plan, config, error)) return 25; // One import at a time.
    const auto applied = profile_transfer::applyPending(config);
    if (!applied.ok) { std::wcerr << applied.error << L"\n"; return 26; }
    const auto merged = loadLibraryState(config / L"library-state.ini");
    if (!merged || merged->playtime_ms.at("steam-user:42/steam:620") != 99000000 || merged->playtime_ms.at("local/epic:Fortnite") != 60000 ||
        merged->collections.names.size() != 2) return 27;
#ifdef _WIN32
    if (custom_games::load(config / L"custom-games.json").games.size() != 2) return 28;
#endif
    const auto selection = config / L"artwork" / L"selections" / text_encoding::wideLenient(key_codec::encode("steam:620"));
    if (!std::filesystem::exists(selection / L"poster.selected") || !std::filesystem::exists(selection / L"hero.selected")) return 29;

    // (5) Playnite's backup ZIP: the same games, the pictures they name.
    {
        zip_archive::Writer zip;
        if (!zip.open(base / L"backup.zip")) return 30;
        for (const auto* name : {L"games.db", L"categories.db", L"tags.db", L"platforms.db"})
            if (!zip.addFile("library/" + text_encoding::utf8(name), fixture / name)) return 31;
        for (const auto* name : {L"cover.png", L"background.jpg"})
            if (!zip.addFile("libraryfiles/01020304-0506-0708-090a-0b0c0d0e0f10/" + text_encoding::utf8(name), fixture / L"files" / L"01020304-0506-0708-090a-0b0c0d0e0f10" / name)) return 32;
        if (!zip.finish()) return 33;
        const auto unpacked = playnite::read(base / L"backup.zip", base / L"scratch", error);
        if (!unpacked || unpacked->games.size() != 7 || playnite::plan(*unpacked, {}, accounts, {}).pictures.size() != 2) { std::wcerr << error << L"\n"; return 34; }
        if (playnite::read(fixture / L"games.db", base / L"scratch2", error)) return 35; // A database alone is no ZIP.
    }
    std::filesystem::remove_all(base);
    std::cout << "playnite import tests passed\n";
    return 0;
}
