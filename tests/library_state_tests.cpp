#include "../src/platform/library_state.hpp"
#include "../src/library/recent_games.hpp"
#include "../src/library/versions.hpp"
#include "../src/model/library_time.hpp"
#include "../src/platform/atomic_file.hpp"
#include <Windows.h>
#include <fstream>
#include <iostream>

int main() {
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-library-state-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup{root};
    LibraryState state;
    state.bookmark = LibraryBookmark{42, LibraryPlace::library, "recent", "gog:123", "gog", L"PS2 · Gry", 725.5f};
    state.options["42:gog"] = {library_view::Order::alphabetical, true};
    state.options["43:gog"] = {library_view::Order::added, false};
    state.options["43:all"] = {library_view::Order::last_played, true, library_view::View::list};
    state.options["42:all"] = {library_view::Order::last_played, false, library_view::View::cinematic};
    state.options["42:steam"] = {library_view::Order::alphabetical, true, library_view::View::detailed};
    state.options["43:steam"] = {library_view::Order::last_played, false, library_view::View::bookshelf};
    state.options["43:steam"].bookshelf_gray = false; // Półka's options, the last appended columns.
    state.options["43:epic"] = {library_view::Order::added, false, library_view::View::coverflow};
    state.options["44:all"] = {library_view::Order::last_played, false, library_view::View::floppy};
    state.options["44:steam"] = {library_view::Order::alphabetical, false, library_view::View::cartridge_switch};
    state.options["44:gog"] = {library_view::Order::added, true, library_view::View::list_plus};
    state.options["44:gog"].list_plus_steelbook = true;
    state.options["45:all"] = {library_view::Order::alphabetical, false, library_view::View::overview};
    // Siatka's landscape tiles and Kinowy's column with its facts (appended columns) survive too.
    state.options["45:all"].grid_landscape = true; state.options["45:all"].cinematic_column = true;
    state.options["45:all"].cinematic_platform = true; state.options["45:all"].cinematic_playtime = true;
    state.options["46:all"] = {library_view::Order::last_played, false, library_view::View::list_plus};
    state.options["46:all"].grid_posters = true;
    state.options["46:all"].list_year = false; state.options["46:all"].list_rating = false;
    state.options["46:all"].cinematic_horizontal = true; state.options["46:all"].overview_columns = 2;
    state.options["46:all"].coverflow_cd = true;
    state.first_seen = {{"steam:1", 10}, {"steam:2", 20}, {"steam:3", 30}};
    state.last_played = {{"steam-user:42/steam:1", 300}, {"gog-user:A/gog:123", 400}};
    state.collections.names = {{"local:test", L"Moje RPG"}, {"42:user-collections.one", L"Wspólna"}};
    state.collections.creates = {"42:user-collections.uc-launchme-0123456789abcdef"};
    state.collections.members["42:user-collections.one"] = {{"gog:123", true}, {"steam:1", false}};
    state.playtime_ms["local/custom:one"] = 123456;
    state.preferred_versions["portal 2"] = "gog:123";
    state.hidden = {"gog:123", "custom:one \"quoted\"", "steam-user:42/steam:620"};
    state.steam_hidden_pending = {{"steam-user:42/steam:620", true}, {"steam-user:43/steam:620", false}};
    state.steam_unsynced = {{"42:hidden:3564860", true}, {"42:user-collections.favorite:3595150", false}};
    state.excluded_games = {{"gog:456", L"Zażółć \"GOG\""}, {"epic:456", L"Epic game"}};
    state.launch_arguments = {{"steam:620", L"-dx11 \"-name=Gość\" -skipintro"}};
    const auto file = root / L"library-state.ini";
    std::filesystem::create_directories(root);
    for (const auto* tail : {"1 0", "2 1 1 1 0 5 0", "0 1 1 1 0 6 0", "0 1 1 1 0 2 -1", "0 1 1 1 0 5 0 2", "0 1 1 1 0 5 0 1 1"}) {
        const auto invalid = root / L"view-options-invalid.ini";
        { std::ofstream output(invalid); output << "launchme-library 1\noption \"" << key_codec::encode("1:all") << "\" 1 0 0 " << tail << '\n'; }
        if (loadLibraryState(invalid)) return 60;
    }
    {
        Game steam{620, L"Portal 2"}, gog; gog.source = GameSource::gog; gog.kind = GameKind::store; gog.source_id = "p2"; gog.name = L"PORTAL  2™";
        Game remaster = gog; remaster.source_id = "remaster"; remaster.name = L"Portal 2 Remastered";
        Game rom = steam; rom.kind = GameKind::nonsteam; rom.shortcut_id = 999;
        versions::Index index({steam, gog, remaster, rom});
        const auto collapsed = index.collapse({steam, gog, remaster, rom}, {});
        if (collapsed.size() != 3 || collapsed[0].version_group.empty() || !activePlatformName(collapsed[0]).empty()) return 40;
        if (index.find(remaster) || index.find(rom) || !index.find(gog) || index.find(gog)->size() != 2) return 41;
        const auto preferred = index.collapse({steam, gog}, {{versions::identity(steam), gameKey(gog)}});
        if (preferred.size() != 1 || preferred[0].source != GameSource::gog) return 42;
        // Other titles, one .exe (a "Moje gry" entry for a GOG installation): one game, the newer play.
        Game saya; saya.source = GameSource::gog; saya.kind = GameKind::store; saya.source_id = "1541066319"; saya.name = L"The Song of Saya";
        saya.executable = L"K:\\GryE\\The Song of Saya\\Saya_en.exe"; saya.last_played = 10;
        Game mine; mine.source = GameSource::custom; mine.kind = GameKind::store; mine.source_id = "713ab1b3e8a99aab"; mine.name = L"Saya no Uta";
        mine.executable = L"k:/gryE/The Song of Saya/saya_EN.exe"; mine.last_played = 20;
        Game other = mine; other.source_id = "other"; other.name = L"Other"; other.executable = L"K:\\GryE\\Other\\other.exe";
        Game hidden = saya; hidden.source = GameSource::steam; hidden.app_id = 702050; hidden.visibility = GameVisibility::private_game;
        versions::Index linked({saya, mine, other, hidden});
        const auto merged = linked.collapse({saya, mine, other, hidden}, {});
        if (merged.size() != 3 || merged[0].version_group.empty() || merged[0].last_played != 20 ||
            !linked.find(mine) || linked.find(mine)->size() != 2 || linked.find(other) || linked.find(hidden)) return 43;
    }
    if (!saveLibraryState(state, file)) return 1;
    const auto restored = loadLibraryState(file);
    if (restored != state) return 2;
    {
        auto invalid = state; invalid.excluded_games["platform:456"] = L"Not a game";
        if (saveLibraryState(invalid, file) || loadLibraryState(file) != state) return 44;
        // Wyklucz grę: any store, a scanned DRM-free game, an emulated one; Emulator per gra: an id or a program.
        auto excluded = state;
        excluded.excluded_games["steam:456"] = L"Steam game"; excluded.excluded_games["drmfree:0123456789abcdef"] = L"Folder game";
        excluded.excluded_games["shortcut:12716030611542769664"] = L"Kirby Air Riders";
        excluded.game_emulators["shortcut:12716030611542769664"] = {{}, L"K:\\Emulators\\Switch 2\\emulator.exe"};
        excluded.game_emulators["shortcut:17314267876185604096"] = {"ryujinx", {}};
        if (!saveLibraryState(excluded, file) || loadLibraryState(file) != excluded) return 47;
        auto both = state; both.game_emulators["shortcut:1"] = {"ryujinx", L"C:\\x.exe"}; // One of them, never both.
        if (saveLibraryState(both, file)) return 48;
        if (!saveLibraryState(state, file)) return 1;
        const auto malformed = root / L"malformed-exclusion.ini";
        for (const auto& key : {"platform:456", "gog:", "epic:", "steam:"}) {
            std::ofstream(malformed) << "launchme-library 1\nexcluded-game \"" << key_codec::encode(key) << "\" \"\"\n";
            if (loadLibraryState(malformed)) return 45;
        }
    }
    {
        Game steam{1, L"Steam"}, external; external.source = GameSource::gog; external.kind = GameKind::store; external.source_id = "123";
        const SteamCollection c{L"Steam collection", {1}, "user-collections.one"};
        if (collectionContains(state.collections, 42, c, steam) || !collectionContains(state.collections, 42, c, external)) return 30;
        if (!collectionContains(state.collections, 43, c, steam) || collectionContains(state.collections, 43, c, external)) return 31;
        const auto collections = managedCollections(42, {c}, state.collections);
        if (collections.size() != 3 || collections[1].name != L"Wspólna" || collections[2].id != "local:test") return 32;
        auto edits = state.collections;
        setCollectionMember(edits, 42, collections[2], external, true);
        if (!collectionContains(edits, 43, collections[2], external)) return 33; // Own collections are account independent.
        setCollectionMember(edits, 42, collections[2], steam, true);
        if (!collectionContains(edits, 42, collections[2], steam) || collectionContains(edits, 43, collections[2], steam) ||
            localSteamMembers(edits, 42) != std::vector<uint32_t>{1} || !localSteamMembers(edits, 43).empty()) return 37;
        setCollectionMember(edits, 42, collections[2], steam, false);
        setCollectionMember(edits, 42, collections[0], external, true);
        if (!collectionContains(edits, 43, collections[0], external)) return 34;
        auto shelves = managedShelves(42, collections, edits, {steam, external});
        if (shelves.size() != 3 || shelves[1].games.size() != 1 || shelves[1].games[0].source != GameSource::gog) return 35;
        external.visibility = GameVisibility::private_game;
        if (!managedShelves(42, collections, edits, {steam, external}).empty()) return 36; // Empty/private shelves disappear.
    }
    {
        LibraryStateWriter writer(file);
        writer.save(state);
        state.bookmark->selection = "gog:456";
        state.bookmark->scroll = 999;
        writer.save(state);
    }
    if (loadLibraryState(file) != state) return 7;
    Game a{1, L"Zulu"}, b{2, L"alpha"}, c{3, L"Beta"}, hidden{4, L"A private game"}, back;
    a.last_played = 99; b.installed = false; hidden.visibility = GameVisibility::private_game; back.kind = GameKind::back;
    const std::vector<Game> input{a, b, c, hidden};
    const auto sorted = library_view::arrange(input, {library_view::Order::alphabetical, false}, state.first_seen);
    if (sorted.size() != 3 || sorted[0].app_id != 2 || sorted[1].app_id != 3 || sorted[2].app_id != 1) return 3;
    // Ctrl+H: the private game sorts in with the others.
    const auto revealed = library_view::arrange(input, {library_view::Order::alphabetical, false}, state.first_seen, true);
    if (revealed.size() != 4 || revealed[0].app_id != 4) return 20;
    const auto recent = library_view::arrange(input, {library_view::Order::last_played, true}, state.first_seen);
    if (recent.size() != 2 || recent[0].app_id != 1) return 4;
    const auto added = library_view::arrange({back, a, b, c}, {library_view::Order::added, false}, state.first_seen);
    if (added[0].kind != GameKind::back || added[1].app_id != 3 || added[2].app_id != 2) return 5;
    {
        // A later store sync must not outrank a newer purchase. File/update/play times do not count.
        Game steam{10, L"Zulu purchase"}, gog, epic, manual, scanned, unknown{11, L"Unknown"};
        gog.source = GameSource::gog; gog.kind = GameKind::store; gog.source_id = "123"; gog.name = L"Alpha old purchase";
        epic = gog; epic.source = GameSource::epic; epic.name = L"Beta purchase";
        manual = gog; manual.source = GameSource::custom; manual.name = L"Manual";
        scanned = gog; scanned.source = GameSource::drmfree; scanned.name = L"Scanned";
        steam.added_at = 300; epic.added_at = 200; gog.added_at = 100;
        gog.last_updated = gog.last_played = 999999;
        const std::map<std::string, uint64_t> detected{{gameKey(steam), 600}, {gameKey(gog), 900}, {gameKey(epic), 800},
            {gameKey(manual), 500}, {gameKey(scanned), 400}};
        const auto result = library_view::arrange({unknown, gog, steam, scanned, epic, manual, back}, {library_view::Order::added}, detected);
        const std::vector<std::string> expected{"", gameKey(manual), gameKey(scanned), gameKey(steam), gameKey(epic), gameKey(gog), gameKey(unknown)};
        for (size_t i = 0; i < expected.size(); ++i) if (gameKey(result[i]) != expected[i]) return 61;
        // Equal/unknown dates keep deterministic natural title order; no invented per-game times.
        epic.added_at = gog.added_at;
        const auto tied = library_view::arrange({epic, gog}, {library_view::Order::added}, detected);
        if (tied[0].source != GameSource::gog) return 62;
        gog.name = steam.name;
        versions::Index index({steam, gog});
        for (const auto& preference : {gameKey(steam), gameKey(gog)}) {
            const auto group = index.collapse({gog, steam}, {{versions::identity(steam), preference}}, detected);
            if (group.size() != 1 || group[0].added_at != 300 || gameKey(group[0]) != preference) return 63;
        }
        // An undated version uses its own first detection, not that of the preferred representative.
        gog.added_at = 0;
        const auto group = index.collapse({gog, steam}, {}, detected);
        if (group.size() != 1 || group[0].added_at != 900) return 64;
    }
    {
        constexpr uint64_t now = 1800000000;
        if (library_time::seconds("1700000000", now) != 1700000000 ||
            library_time::utc("2023-11-14T22:13:20.123Z", now) != 1700000000 ||
            library_time::utc("2023-11-14T22:13:20Z", now) != 1700000000 ||
            library_time::utc("2024-02-29T00:00:00Z", now) != 1709164800) return 65;
        for (const auto* bad : {"", "-1", "1700000000000", "1700000000.5", "1e9", "18446744073709551616", "1900000000"})
            if (library_time::seconds(bad, now)) return 66;
        for (const auto* bad : {"2023-02-29T00:00:00Z", "2023-11-14T24:00:00Z", "2023-11-14T22:13:60Z", "2023-11-14T22:13:20.Z",
                               "2023-11-14T22:13:20.aZ", "2023-11-14T22:13:20Zjunk", "2030-01-01T00:00:00Z", "2023-11-14T22:13:20"})
            if (library_time::utc(bad, now)) return 67;
    }
    // Release dates (from the game index): newest first, undated last in title order.
    const std::unordered_map<std::string, int> dates{{"steam:1", 19981119}, {"steam:3", 20110419}};
    const auto by_release = library_view::arrange({back, a, b, c}, {library_view::Order::release, false}, state.first_seen, false, &dates);
    if (by_release[0].kind != GameKind::back || by_release[1].app_id != 3 || by_release[2].app_id != 1 || by_release[3].app_id != 2) return 26;
    Game gog; gog.source = GameSource::gog; gog.kind = GameKind::store; gog.source_id = "123"; gog.name = L"GOG fixture";
    auto mixed = std::vector<Game>{a, gog};
    recent_games::apply(mixed, restored->last_played, 42, "A");
    if (mixed[0].last_played != 300 || mixed[1].last_played != 400 || gameKey(mixed[1]) != "gog:123") return 8;
    mixed = {a, gog}; recent_games::apply(mixed, restored->last_played, 43, "B");
    if (mixed[0].last_played != 99 || mixed[1].last_played) return 9;
    { std::ofstream corrupt(file); corrupt << "launchme-library 1\nbookmark 42 9 \"\" \"\" \"\" \"\" 0\noption \"zz\" 1 0\n"; }
    const auto bad = loadLibraryState(file);
    if (bad) return 6;
    // Files written before the view column keep their options and show the grid; an unknown view is rejected.
    { std::ofstream older(file); older << "launchme-library 1\noption \"" << key_codec::encode("1:all") << "\" 1 1\n"; }
    const auto old = loadLibraryState(file);
    if (!old || old->options.size() != 1 || old->options.at("1:all") != library_view::Options{library_view::Order::alphabetical, true, library_view::View::grid}) return 10;
    // The original four order numbers stay on disk: 0 (the removed unsorted "Domyślnie") reads as
    // Recently played, which is written back as 2; 4 is out of range.
    { std::ofstream legacy(file); legacy << "launchme-library 1\noption \"" << key_codec::encode("1:all") << "\" 0 0\noption \""
                                         << key_codec::encode("2:all") << "\" 3 0\noption \"" << key_codec::encode("3:all") << "\" 2 0\n"; }
    const auto legacy = loadLibraryState(file);
    if (!legacy || legacy->options.at("1:all").order != library_view::Order::last_played ||
        legacy->options.at("2:all").order != library_view::Order::added || legacy->options.at("3:all").order != library_view::Order::last_played) return 21;
    if (!saveLibraryState(*legacy, file)) return 22;
    {
        std::ifstream written(file); const std::string text{std::istreambuf_iterator<char>{written}, {}};
        if (text.find("\" 2 0 0 0 1 1 1 0 5 0 0 0 0 0 0 0 1 1\n") == std::string::npos || text.find("\" 0 0") != std::string::npos) return 23;
    }
    // Removed views keep their numbers out of new files: Plakat (4) reads as Lista+, Kartridż NES (8)
    // as Kartridż Switch; 12 is out of range. Rows of seven options (before the steelbook) still load.
    { std::ofstream removed(file); removed << "launchme-library 1\noption \"" << key_codec::encode("1:all") << "\" 1 0 4\noption \""
                                           << key_codec::encode("2:all") << "\" 1 0 8 0 1 1 1 0 5 0\n"; }
    const auto migrated = loadLibraryState(file);
    if (!migrated || migrated->options.at("1:all").view != library_view::View::list_plus ||
        migrated->options.at("2:all").view != library_view::View::cartridge_switch || migrated->options.at("2:all").list_plus_steelbook) return 28;
    { std::ofstream beyond(file); beyond << "launchme-library 1\noption \"" << key_codec::encode("1:all") << "\" 1 0 12\n"; }
    if (loadLibraryState(file)) return 29;
    // 4 is the release-date order (added later); 5 is out of range.
    { std::ofstream release(file); release << "launchme-library 1\noption \"" << key_codec::encode("1:all") << "\" 4 0\n"; }
    const auto released = loadLibraryState(file);
    if (!released || released->options.at("1:all").order != library_view::Order::release) return 25;
    { std::ofstream beyond(file); beyond << "launchme-library 1\noption \"" << key_codec::encode("1:all") << "\" 5 0\n"; }
    if (loadLibraryState(file)) return 24;
    { std::ofstream older(file); older << "launchme-library 1\noption \"" << key_codec::encode("1:all") << "\" 1 1\n"; }
    { std::ofstream invalid(file, std::ios::app); invalid << "option \"" << key_codec::encode("2:all") << "\" 0 0 99\n"; }
    if (loadLibraryState(file)) return 11; // Partial corrupt data must not be saved over the full file.
    // ...but it is no dead end: every readable row is salvaged (the launcher keeps the original aside).
    if (const auto salvaged = salvageLibraryState(file); !salvaged || salvaged->skipped_rows != 1 || salvaged->state.options.size() != 1) return 27;

    // A realistic large history previously saved successfully but loaded empty.
    LibraryState large = state;
    for (uint32_t i = 0; i < 20000; ++i) {
        Game game{1000000 + i, L"History fixture"};
        large.first_seen[gameKey(game)] = 1700000000;
        large.last_played[recent_games::historyKey(game, 76561198000000000ULL, {})] = 1700000001;
    }
    if (!saveLibraryState(large, file) || std::filesystem::file_size(file) <= 2 * 1024 * 1024 || loadLibraryState(file) != large) return 12;
    auto invalid = state; invalid.first_seen[std::string(1025, 'x')] = 1;
    if (saveLibraryState(invalid, file) || loadLibraryState(file) != large) return 13;
    LibraryState oversized;
    for (int i = 0; i < 17000; ++i) {
        auto key = std::to_string(i); key.resize(1024, 'x'); oversized.first_seen.emplace(std::move(key), 1);
    }
    if (saveLibraryState(oversized, file) || loadLibraryState(file) != large) return 14;
    // An exclusively opened file is an I/O error, not a new profile.
    const auto handle = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return 15;
    const bool unreadable = !loadLibraryState(file);
    CloseHandle(handle);
    if (!unreadable || loadLibraryState(file) != large) return 16;
    const auto missing = loadLibraryState(root / L"missing.ini");
    if (!missing || *missing != LibraryState{}) return 17;
    { // Sparse oversized fixture, without allocating its contents in the test.
        const auto huge = root / L"oversized.ini";
        const auto out = CreateFileW(huge.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE) return 18;
        LARGE_INTEGER size{}; size.QuadPart = max_library_state_bytes + 1;
        const bool made = SetFilePointerEx(out, size, nullptr, FILE_BEGIN) && SetEndOfFile(out);
        CloseHandle(out);
        if (!made || loadLibraryState(huge)) return 19;
    }
    std::cout << "Library identity, account-scoped options, sorting, privacy and persistence passed\n";
}
