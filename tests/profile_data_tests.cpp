// The profile's data (src/library/play_journal.hpp, achievement_history.hpp) and the player's own
// play time (metadata_overrides.hpp): day arithmetic, the week, files that survive a round trip and
// damage, unlocks without duplicates, months and completion order.
#include "../src/library/achievement_history.hpp"
#include "../src/library/metadata_overrides.hpp"
#include "../src/library/play_journal.hpp"

#include <Windows.h>
#include <fstream>
#include <iostream>

#define CHECK(value) do { if (!(value)) { std::cerr << "Profile data check failed at line " << __LINE__ << ": " << #value << '\n'; return 1; } } while (false)

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / (L"launchme-profile-tests-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};

    // ---- Days.
    using namespace play_journal;
    CHECK(addDays(20261027, 1) == 20261028 && addDays(20261031, 1) == 20261101 && addDays(20261231, 1) == 20270101);
    CHECK(addDays(20240301, -1) == 20240229 && addDays(20230301, -1) == 20230228 && addDays(20261027, -7) == 20261020);
    CHECK(weekday(20261026) == 0 && weekday(20261027) == 1 && weekday(20261101) == 6); // Monday, Tuesday, Sunday.
    CHECK(dateOf(0) >= 19691231 && dateOf(0) <= 19700101);

    // ---- The journal and its week.
    Journal journal;
    const int today = 20261027;
    played(journal, today, "steam:620", L"Portal 2", 90 * 60000);
    launched(journal, today, "steam:620", L"Portal 2");
    played(journal, addDays(today, -1), "gog:1", L"Witcher\t3", 30 * 60000);
    played(journal, addDays(today, -2), "steam:620", L"Portal 2", 10 * 60000);
    played(journal, addDays(today, -9), "steam:620", L"Portal 2", 60 * 60000); // The week before.
    played(journal, today, "", L"Nobody", 1000);         // No key: nothing.
    played(journal, 20261399, "steam:1", L"Bad day", 1); // No such date.
    CHECK(journal.days.size() == 4 && journal.names.at("gog:1") == L"Witcher 3");
    const auto week = play_journal::week(journal, today);
    CHECK(week.dates[6] == today && week.dates[0] == addDays(today, -6));
    CHECK(week.total_ms == 130ull * 60000 && week.previous_ms == 60ull * 60000 && week.games == 2 && week.launches == 1 && week.active_days == 3);
    CHECK(week.day_ms[6] == 90ull * 60000 && week.top.front().first == "steam:620" && week.top.front().second == 100ull * 60000);
    CHECK(streak(journal, today) == 3 && streak(journal, addDays(today, 1)) == 3 && streak(journal, addDays(today, 3)) == 0);
    // Round trip, and a damaged file reads as empty instead of failing.
    const auto file = root / L"play-journal.tsv";
    CHECK(save(journal, file) && load(file) == journal);
    CHECK(parse("launchme-journal\t1\nd\t20261027\tsteam:1\tnot-a-number\t0\nn\tsteam:1\tOk\n").days.empty());
    CHECK(parse("garbage").days.empty() && parse("").names.empty());
    // A missing file is an empty journal; a damaged or newer one is not (it must not be saved over).
    CHECK(read(root / L"no-journal.tsv") == Journal{});
    const auto damaged = root / L"damaged-journal.tsv";
    { std::ofstream(damaged, std::ios::binary) << "launchme-journal\t2\nd\t20261027\tsteam:1\t1\t1\n"; }
    CHECK(!read(damaged) && load(damaged).days.empty());
    { std::ofstream(damaged, std::ios::binary) << "\x89PNG"; }
    CHECK(!read(damaged));
    Journal session_play, stored = journal;
    played(session_play, today, "steam:620", L"Portal 2", 5 * 60000);
    merge(stored, session_play);
    CHECK(stored.days.at(today).at("steam:620").ms == 95ull * 60000 && stored.days.at(today).at("steam:620").launches == 1);
    Journal long_one;
    for (int i = 0; i < 500; ++i) played(long_one, addDays(today, -i), "steam:1", L"Old", 60000);
    CHECK(long_one.days.size() == max_days && long_one.days.rbegin()->first == today);

    // ---- Achievements.
    using namespace achievement_history;
    History history;
    std::vector<Unlock> portal{{{}, "first", L"First", L"", L"https://cdn/icon.jpg", 1790000000, 52.5f},
                               {{}, "rare", L"Rare one", L"", {}, 1792000000, 3.2f},
                               {{}, "locked", L"Not yet", L"", {}, 0, -1}};
    CHECK(merge(history, "steam:620", L"Portal 2", portal, {2, 51}) == 2);
    CHECK(merge(history, "steam:620", L"Portal 2", portal, {2, 51}) == 0); // Known ones are not added twice.
    portal[0].at = 1799999999; portal[0].name = L"First (renamed)";
    CHECK(merge(history, "steam:620", L"Portal 2", portal, {2, 51}) == 0 && history.unlocks.back().at == 1790000000 &&
          history.unlocks.back().name == L"First (renamed)"); // Its first time stays.
    CHECK(merge(history, "gog:1", L"Witcher 3", {{{}, "all", L"All", L"", {}, 1795000000, -1}}, {78, 78}) == 1);
    CHECK(history.unlocks.size() == 3 && history.unlocks.front().game == "gog:1" && history.unlocks[1].id == "rare");
    CHECK(since(history, 1792000000) == 2 && since(history, 0) == 3);
    const auto order = completion(history.progress);
    CHECK(order.size() == 2 && order[0].game == "gog:1" && order[0].share() == 1.0f && order[1].unlocked == 2);
    history.scanned["steam:620"] = 133;
    const auto history_file = root / L"achievement-history.json";
    CHECK(achievement_history::save(history, history_file) && achievement_history::load(history_file) == history);
    CHECK(achievement_history::parse("{\"version\":2}").unlocks.empty() && achievement_history::parse("not json").progress.empty());
    // Months: the last twelve, oldest first, the year turning over.
    const auto months = monthly(history, 2027, 2);
    CHECK(months[11].year == 2027 && months[11].month == 2 && months[10].month == 1 && months[9].year == 2026 && months[9].month == 12 && months[0].month == 3);
    uint32_t counted = 0;
    for (const auto& month : months) counted += month.count;
    CHECK(counted == 3);

    // ---- The player's own play time (Edytuj metadane → Czas gry).
    using metadata::parsePlaytime;
    CHECK(parsePlaytime(L"12") == 720u && parsePlaytime(L"12,5") == 750u && parsePlaytime(L"1.5 h") == 90u);
    CHECK(parsePlaytime(L"12:30") == 750u && parsePlaytime(L"12 godz. 30 min") == 750u && parsePlaytime(L"45 min") == 45u);
    CHECK(parsePlaytime(L"0:05") == 5u && !parsePlaytime(L"") && !parsePlaytime(L"abc") && !parsePlaytime(L"12:75") && !parsePlaytime(L"1 2 3"));
    CHECK(metadata::playtimeText(750) == L"12:30" && metadata::playtimeText(5) == L"0:05");
    metadata::Override own;
    CHECK(metadata::playtime(own, 300) == 300); // Nothing set: the automatic value.
    own.playtime = 1200; own.playtime_base = 300;
    CHECK(metadata::playtime(own, 300) == 1200 && metadata::playtime(own, 360) == 1260 && metadata::playtime(own, 100) == 1200);
    CHECK(!own.empty());
    metadata::Overrides overrides;
    overrides.set("steam:620", own);
    const auto overrides_file = root / L"metadata_overrides.json";
    CHECK(overrides.save(overrides_file));
    metadata::Overrides loaded;
    CHECK(loaded.load(overrides_file) && loaded.find("steam:620") && loaded.find("steam:620")->playtime == 1200u &&
          loaded.find("steam:620")->playtime_base == 300u);
#ifdef _WIN32
    // Locked while loading: empty, but the next save reads the file again and keeps its entries.
    {
        const HANDLE lock = CreateFileW(overrides_file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(lock != INVALID_HANDLE_VALUE);
        metadata::Overrides locked;
        const bool read_while_locked = locked.load(overrides_file);
        CloseHandle(lock);
        CHECK(!read_while_locked && locked.items().empty());
        metadata::Override other; other.title = L"Edited meanwhile";
        locked.set("gog:1", other);
        CHECK(locked.save(overrides_file) && locked.find("steam:620") && locked.find("gog:1"));
        metadata::Overrides again;
        CHECK(again.load(overrides_file) && again.items().size() == 2);
    }
#endif
    // Damaged: kept as metadata_overrides.unreadable.json before the new file replaces it.
    { std::ofstream(overrides_file, std::ios::binary | std::ios::trunc) << "{\"games\":[]}"; }
    metadata::Overrides damaged_overrides;
    CHECK(!damaged_overrides.load(overrides_file));
    damaged_overrides.set("steam:1", own);
    CHECK(damaged_overrides.save(overrides_file) && fs::exists(root / L"metadata_overrides.unreadable.json"));
    CHECK(loaded.load(overrides_file) && loaded.items().size() == 1);
    std::cout << "Play journal, achievement history and own play time passed\n";
    return 0;
}
