// Game index: dates, Steam's appinfo.vdf (v28 inline keys, v29 string table), the per-source
// adapters over cached files, merging by field priority, queries, statistics, similar games and
// the saved file, all on fixtures. `--appinfo` reads this machine's Steam appinfo cache (manual).
#include "../src/library/game_index.hpp"
#include "../src/library/game_index_service.hpp"
#include "../src/library/game_index_sources.hpp"
#include "../src/library/steam_appinfo.hpp"
#include "../src/library/steam.hpp"
#include "../src/model/key_codec.hpp"
#include "../src/net/companion.hpp"
#include "../src/net/companion_tools.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/text_encoding.hpp"

#include <Windows.h>

#include <atomic>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace game_index;

// Binary KeyValues writer for fixtures: inline keys (v28) or string-table indices (v29).
struct KvWriter {
    bool table = false;
    std::vector<std::string> strings;
    std::vector<uint8_t> out;
    void key(const std::string& name) {
        if (!table) { out.insert(out.end(), name.begin(), name.end()); out.push_back(0); return; }
        auto found = std::find(strings.begin(), strings.end(), name);
        uint32_t index = static_cast<uint32_t>(found - strings.begin());
        if (found == strings.end()) strings.push_back(name);
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(index >> (8 * i)));
    }
    void open(const std::string& name) { out.push_back(0); key(name); }
    void close() { out.push_back(8); }
    void text(const std::string& name, const std::string& value) { out.push_back(1); key(name); out.insert(out.end(), value.begin(), value.end()); out.push_back(0); }
    void int32(const std::string& name, int32_t value) { out.push_back(2); key(name); for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i))); }
};
void put32(std::vector<uint8_t>& out, uint32_t value) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i))); }

// One game record's KeyValues (the parts appinfo.vdf has for Portal 2 and an emulator-era classic).
void appRecord(KvWriter& kv, uint32_t id, const std::string& name, int64_t release, int64_t original, bool rich) {
    kv.open("appinfo");
    kv.int32("appid", static_cast<int32_t>(id));
    kv.open("common");
    kv.text("name", name);
    kv.text("type", "Game");
    kv.text("steam_release_date", std::to_string(release));
    if (original) kv.text("original_release_date", std::to_string(original));
    if (rich) {
        kv.text("review_percentage", "98");
        kv.text("review_score", "9");
        kv.text("metacritic_score", "95");
        kv.open("genres"); kv.text("0", "1"); kv.text("1", "25"); kv.close();
        kv.text("primary_genre", "1");
        kv.open("store_tags"); kv.text("0", "1664"); kv.text("1", "4182"); kv.text("2", "999999"); kv.close();
        kv.open("category"); kv.text("category_2", "1"); kv.text("category_9", "1"); kv.text("category_22", "1"); kv.close();
        kv.open("associations");
        kv.open("0"); kv.text("type", "developer"); kv.text("name", "Valve"); kv.close();
        kv.open("1"); kv.text("type", "publisher"); kv.text("name", "Valve"); kv.close();
        kv.open("2"); kv.text("type", "franchise"); kv.text("name", "Portal"); kv.close();
        kv.close();
        kv.text("controller_support", "full");
        kv.open("steam_deck_compatibility"); kv.text("category", "3"); kv.close();
    }
    kv.close();
    if (!rich) { kv.open("extended"); kv.text("developer", "id Software, Raven"); kv.close(); }
    kv.close();
    kv.close();
}
std::vector<uint8_t> appinfoFile(bool v29) {
    KvWriter kv; kv.table = v29;
    std::vector<uint8_t> records;
    struct App { uint32_t id; std::string name; int64_t release, original; bool rich; };
    const App apps[]{{620, "Portal 2", 1303171200, 0, true}, {2280, "DOOM (1993)", 1186099200, 755481600, false}, {999, "Not wanted", 1, 0, false}};
    for (const auto& app : apps) {
        kv.out.clear();
        appRecord(kv, app.id, app.name, app.release, app.original, app.rich);
        put32(records, app.id);
        put32(records, static_cast<uint32_t>(60 + kv.out.size()));
        records.insert(records.end(), 60, 0); // infoState, lastUpdated, token, hashes, change number.
        records.insert(records.end(), kv.out.begin(), kv.out.end());
    }
    put32(records, 0);
    std::vector<uint8_t> file;
    put32(file, v29 ? 0x07564429 : 0x07564428);
    put32(file, 1);
    if (v29) {
        const uint64_t table = 16 + records.size();
        for (int i = 0; i < 8; ++i) file.push_back(static_cast<uint8_t>(table >> (8 * i)));
    }
    file.insert(file.end(), records.begin(), records.end());
    if (v29) {
        put32(file, static_cast<uint32_t>(kv.strings.size()));
        for (const auto& text : kv.strings) { file.insert(file.end(), text.begin(), text.end()); file.push_back(0); }
    }
    return file;
}

Patch library(const wchar_t* title, bool installed, uint64_t played, std::optional<uint32_t> minutes = std::nullopt, const wchar_t* store = L"Steam") {
    Patch patch;
    patch.title = title; patch.store = store; patch.installed = installed; patch.last_played = played; patch.first_seen = 1'700'000'000;
    if (minutes) patch.playtime_minutes = minutes;
    return patch;
}
Patch facts(Date release, std::vector<std::wstring> genres, std::vector<std::wstring> tags = {}, std::vector<std::wstring> developers = {}) {
    Patch patch;
    if (release.known()) patch.release = release;
    if (!genres.empty()) patch.genres = std::move(genres);
    if (!tags.empty()) patch.tags = std::move(tags);
    if (!developers.empty()) patch.developers = std::move(developers);
    return patch;
}

int dates() {
    if (parseDate(L"2004-11-16") != Date{2004, 11, 16} || parseDate(L"20041116T000000") != Date{2004, 11, 16} ||
        parseDate(L"16 Nov, 2004") != Date{2004, 11, 16} || parseDate(L"Nov 16, 2004") != Date{2004, 11, 16}) return 10;
    if (parseDate(L"3 września 2019") != Date{2019, 9, 3} || parseDate(L"16 lis 2004") != Date{2004, 11, 16} ||
        parseDate(L"12 LUT 2020") != Date{2020, 2, 12} || parseDate(L"1 października 1998") != Date{1998, 10, 1}) return 11;
    if (parseDate(L"Nov 2004") != Date{2004, 11, 0} || parseDate(L"Q4 2004") != Date{2004, 0, 0} || parseDate(L"2004") != Date{2004, 0, 0} ||
        parseDate(L"2004-11") != Date{2004, 11, 0}) return 12;
    if (parseDate(L"Coming soon").known() || parseDate(L"TBA").known() || parseDate(L"").known() || parseDate(L"30 Feb 2021") != Date{2021, 2, 0}) return 13;
    if (dateFromUnix(1303171200) != Date{2011, 4, 19} || dateFromUnix(1303171200000ull) != Date{2011, 4, 19} || dateFromUnix(0).known()) return 14;
    if (Date{1998, 11, 8}.iso() != L"1998-11-08" || Date{1998, 11, 0}.iso() != L"1998-11" || Date{1998, 0, 0}.iso() != L"1998" || !Date{}.iso().empty()) return 15;
    // Labels for people read back as the same date.
    for (const auto english : {false, true})
        for (const Date date : {Date{1998, 11, 8}, Date{2019, 9, 3}, Date{2004, 5, 0}, Date{2010, 0, 0}})
            if (parseDate(dateLabel(date, english)) != date) return 19;
    if (dateLabel({1998, 11, 1}, false) != L"1 listopada 1998" || dateLabel({1998, 11, 1}, true) != L"Nov 1, 1998" || dateLabel({2004, 5, 0}, false) != L"maj 2004") return 19;
    if (fold(L"Half-Life 2™") != L"halflife2" || fold(L"Wiedźmin 3: Dziki Gon") != L"wiedzmin3dzikigon" || fold(L"Łódź") != L"lodz") return 16;
    if (canonicalGenre(L"Przygodowa") != L"Adventure" || canonicalGenre(L"strzelanki") != L"Shooter" || canonicalGenre(L"Roguelike") != L"Roguelike") return 17;
    if (genreLabel(L"Adventure", false) != L"Przygodowe" || genreLabel(L"Adventure", true) != L"Adventure" || genreLabel(L"Roguelike", false) != L"Roguelike") return 18;
    return 0;
}

int appinfo() {
    const auto wanted = [](uint32_t id) { return id != 999; };
    for (const bool v29 : {false, true}) {
        const auto file = appinfoFile(v29);
        const auto apps = steam_appinfo::parse(file, wanted);
        if (!apps || apps->size() != 2) return 20;
        const auto& portal = (*apps)[0];
        if (portal.app_id != 620 || portal.name != L"Portal 2" || portal.type != L"game" || portal.steam_release != 1303171200 || portal.original_release) return 21;
        if (portal.review_percent != 98 || portal.review_score != 9 || portal.metacritic != 95 || portal.genres != std::vector<uint32_t>{1, 25} ||
            portal.primary_genre != 1 || portal.tags != std::vector<uint32_t>{1664, 4182, 999999}) return 22;
        if (portal.categories != std::vector<uint32_t>{2, 9, 22} || portal.developers != std::vector<std::wstring>{L"Valve"} ||
            portal.publishers != std::vector<std::wstring>{L"Valve"} || portal.franchises != std::vector<std::wstring>{L"Portal"} ||
            portal.controller_support != L"full" || portal.deck != 3) return 23;
        const auto& doom = (*apps)[1];
        if (doom.app_id != 2280 || doom.original_release != 755481600 || doom.developers != std::vector<std::wstring>{L"id Software", L"Raven"}) return 24;
        // Torn files and other formats are not an empty cache.
        auto torn = file; torn.resize(torn.size() / 2);
        if (steam_appinfo::parse(torn, wanted) || steam_appinfo::parse(std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}, wanted)) return 25;
        if (v29) {
            // A string-table offset near UINT64_MAX (it once wrapped past the bounds check) and a
            // name count the file cannot hold.
            auto wrapped = file;
            for (int i = 8; i < 16; ++i) wrapped[i] = 0xFF;
            wrapped[8] = 0xFC;
            if (steam_appinfo::parse(wrapped, wanted)) return 290;
            auto counted = file;
            uint64_t offset = 0; std::memcpy(&offset, counted.data() + 8, 8);
            const uint32_t many = 900'000; std::memcpy(counted.data() + offset, &many, 4);
            if (steam_appinfo::parse(counted, wanted)) return 291;
        }
        // Into a patch: the original release wins, tags by name, features after them.
        const TagNames tags{{1664, L"Puzzle"}, {4182, L"Singleplayer"}};
        const auto patch = fromAppInfo(portal, tags);
        if (!patch.release || *patch.release != Date{2011, 4, 19} || patch.genres != std::vector<std::wstring>{L"Action", L"Adventure"} ||
            patch.tags != std::vector<std::wstring>{L"Puzzle", L"Singleplayer", L"Single-player", L"Co-op"} || patch.franchise != L"Portal" || patch.metacritic != 95) return 26;
        if (fromAppInfo(doom, tags).release != Date{1993, 12, 10}) return 27;
        if (fromAppInfo(portal, {}).tags) return 28; // Without the tag names, no guesses.
    }
    if (parseSteamTags(R"([{"tagid":492,"name":"Indie"},{"tagid":19,"name":"Action"},{"name":"x"}])").size() != 2) return 29;
    return 0;
}

int sources() {
    const std::string store = R"({"620":{"success":true,"data":{"name":"Portal 2","short_description":"Gra <b>logiczna</b> &amp; zabawna.","developers":["Valve"],
        "publishers":["Valve"],"genres":[{"id":"1","description":"Akcja"},{"id":"25","description":"Przygodowe"}],"release_date":{"coming_soon":false,"date":"18 kwi 2011"},
        "metacritic":{"score":95},"recommendations":{"total":420000}}}})";
    auto patch = fromSteamStore(620, store);
    if (patch.description != L"Gra logiczna & zabawna." || patch.genres != std::vector<std::wstring>{L"Action", L"Adventure"} ||
        patch.release != Date{2011, 4, 18} || patch.metacritic != 95 || patch.review_count != 420000u || patch.developers != std::vector<std::wstring>{L"Valve"}) return 30;
    if (!fromSteamStore(620, R"({"620":{"success":false}})").empty() || !fromSteamStore(621, store).empty()) return 31;
    if (fromSteamStore(5, R"({"5":{"success":true,"data":{"release_date":{"coming_soon":true,"date":"2027"}}}})").release) return 32;
    const std::string rawg = R"({"launchme_cache":1,"steam_app_id":0,"response":{"id":3328,"name":"The Witcher 3: Wild Hunt","description_raw":"Geralt.","released":"2015-05-18",
        "developers":[{"name":"CD PROJEKT RED"}],"genres":[{"name":"RPG"}],"tags":[{"name":"Open World","language":"eng"},{"name":"Открытый мир","language":"rus"}],
        "rating":4.66,"ratings_count":6000,"metacritic":92}})";
    patch = fromRawg(rawg);
    if (patch.release != Date{2015, 5, 18} || patch.tags != std::vector<std::wstring>{L"Open World"} || !patch.rawg_rating || std::abs(*patch.rawg_rating - 4.66f) > 0.001f ||
        patch.metacritic != 92 || patch.aliases != std::vector<std::wstring>{L"The Witcher 3: Wild Hunt"}) return 33;
    const std::string hltb = R"({"data":[{"comp_100":113833,"comp_main":89992,"comp_plus":99213,"game_id":9150,"game_name":"StarCraft"},{"comp_main":7200,"game_id":1}],"fetched":"1","name":"StarCraft","selected":"9150"})";
    patch = fromHltb(hltb, L"StarCraft");
    if (patch.hltb_main != 1500u || patch.hltb_extra != 1654u || patch.hltb_complete != 1897u) return 34;
    if (!fromHltb(hltb, L"StarCraft II").empty() || !fromHltb(R"({"data":[{"comp_main":7200,"game_id":1}],"name":"X","selected":"0"})", L"X").empty()) return 35;
    GameDetails details;
    details.description = L"A shooter."; details.release_date = L"19931210T000000"; details.developer = L"id Software"; details.genres = L"Shooter / FPS, Action";
    patch = fromDetails(details, L"DOOM");
    if (patch.release != Date{1993, 12, 10} || patch.genres != std::vector<std::wstring>{L"Shooter", L"FPS", L"Action"} || patch.aliases != std::vector<std::wstring>{L"DOOM"}) return 36;
    ratings::Rating rating; rating.source = ratings::Source::steam; rating.score = 97.6; rating.count = 300;
    patch = fromRating(rating);
    if (patch.review_percent != 98 || patch.review_count != 300u || patch.rawg_rating) return 37;
    patch = fromGogProduct(R"({"id":1207658924,"title":"Unreal Tournament","release_date":"1999-11-30T00:00:00+0200","description":{"lead":"<p>Frag.</p>","full":"long"}})");
    if (patch.release != Date{1999, 11, 30} || patch.description != L"Frag.") return 38;
    if (plainDescription(std::string(500, 'a') + " koniec", 400).size() > 401) return 39;

    // readCaches: the launcher's own cache files of one game.
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-index-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    CacheFolders folders{root / L"metadata", root / L"ratings", root / L"hltb", root / L"knowledge"};
    atomic_file::write(folders.metadata / L"620.details.json", store);
    const std::string witcher_key = "676f673a31"; // gog:1
    atomic_file::write(folders.products / L"rawg" / L"676f673a31.json", rawg);
    atomic_file::write(folders.ratings / L"676f673a31.rating.json",
        R"({"version":1,"manual":false,"identity":"676f673a313a77697463686572","checked":1,"rating":{"score":4.5,"count":10,"metacritic":91,"rawg_id":3328,"slug":"w"}})");
    atomic_file::write(folders.hltb / L"676f673a31.json", R"({"data":[{"comp_main":180000,"comp_plus":0,"comp_100":0,"game_id":7}],"name":"Wiedźmin 3","selected":"7"})");
    atomic_file::write(folders.products / L"gog" / L"1.json", R"({"id":1,"title":"The Witcher 3","release_date":"2015-05-19T00:00:00+0200"})");
    const auto steam = readCaches(folders, {"steam:620", "737465616d3a363230", L"Portal 2", 620}, true);
    if (steam.size() != 1 || steam[0].first != Source::steam_store || !steam[0].second.updated) return 40;
    const auto gog = readCaches(folders, {"gog:1", witcher_key, L"Wiedźmin 3", 0}, false);
    std::map<Source, Patch> by;
    for (const auto& [source, value] : gog) by[source] = value;
    if (gog.size() != 4 || !by.contains(Source::rawg) || by[Source::reviews].rawg_rating != 4.5f || by[Source::reviews].metacritic != 91 ||
        by[Source::hltb].hltb_main != 3000u || by[Source::gog].release != Date{2015, 5, 19}) return 41;
    std::filesystem::remove_all(root);
    return 0;
}

int index() {
    Index index;
    const uint64_t now = 1'760'000'000;
    index.syncLibrary({{"steam:620", library(L"Portal 2", true, now - 86400, 600)}, {"steam:2280", library(L"DOOM (1993)", false, 0)},
                       {"gog:1", library(L"Wiedźmin 3", true, now - 40 * 86400, 7200, L"GOG")}, {"custom:x", library(L"Mystery Game", true, 0)},
                       {"shortcut:7", library(L"Hollow Knight", true, now - 5 * 86400, 2400, L"Moje gry")}}, now);
    if (index.size() != 5 || index.keys().size() != 5 || !index.get("steam:620") || index.get("steam:620")->title != L"Portal 2") return 50;
    const auto revision = index.revision();
    // Field priority: appinfo's date and genres beat the store page; the store page's description wins.
    auto appinfo = facts({2011, 4, 19}, {L"Action", L"Adventure"}, {L"Puzzle", L"Co-op"}, {L"Valve"});
    appinfo.franchise = L"Portal"; appinfo.review_percent = 98;
    index.merge("steam:620", Source::steam_appinfo, appinfo);
    auto store = facts({2011, 4, 18}, {L"Akcja"}, {}, {L"Valve Corporation"});
    store.description = L"Myśl portalami."; store.review_count = 420000;
    index.merge("steam:620", Source::steam_store, store);
    Patch reviews; reviews.review_percent = 97; reviews.review_count = 300000;
    index.merge("steam:620", Source::reviews, reviews);
    const auto* portal = index.get("steam:620");
    if (portal->release != Date{2011, 4, 19} || portal->genres != std::vector<std::wstring>{L"Action", L"Adventure"} || portal->description != L"Myśl portalami." ||
        portal->developers != std::vector<std::wstring>{L"Valve"} || portal->review_percent != 97 || portal->review_count != 300000u || portal->franchise != L"Portal" ||
        !portal->from(Source::steam_store) || !portal->from(Source::library) || portal->from(Source::hltb) || index.revision() <= revision) return 51;
    // The same facts again change nothing.
    const auto before = index.revision();
    if (index.merge("steam:620", Source::reviews, reviews) || index.revision() != before) return 52;
    // A year-only date gives way to a full one of the same year from a later source.
    index.merge("steam:2280", Source::steam_appinfo, facts({1993, 0, 0}, {L"Action"}, {L"FPS", L"Classic", L"Retro"}, {L"id Software"}));
    index.merge("steam:2280", Source::rawg, facts({1993, 12, 10}, {L"Shooter"}));
    if (index.get("steam:2280")->release != Date{1993, 12, 10}) return 53;
    index.merge("gog:1", Source::rawg, facts({2015, 5, 18}, {L"RPG", L"Adventure"}, {L"Open World", L"Fantasy"}, {L"CD PROJEKT RED"}));
    Patch hltb; hltb.hltb_main = 3000; index.merge("gog:1", Source::hltb, hltb);
    hltb.hltb_main = 900; index.merge("shortcut:7", Source::hltb, hltb);
    index.merge("shortcut:7", Source::emulation, facts({2017, 2, 24}, {L"Metroidvania", L"Platformer"}, {}, {L"Team Cherry"}));

    // The oldest game, with undated ones counted apart.
    Query query; query.sort = Sort::release_asc; query.limit = 2;
    auto found = index.search(query);
    if (found.total != 5 || found.undated != 1 || found.games.size() != 2 || found.games[0]->key != "steam:2280" || found.games[1]->key != "steam:620") return 60;
    query.sort = Sort::release_desc; query.limit = 10;
    found = index.search(query);
    if (found.games.front()->key != "shortcut:7" || found.games.back()->key != "custom:x") return 61; // Undated last either way.
    // Genres in Polish, tags and words of titles/studios.
    query = {}; query.genres = {L"przygodowe"};
    found = index.search(query);
    if (found.total != 2) return 62;
    query = {}; query.tags = {L"open world"};
    if (index.search(query).total != 1) return 63;
    query = {}; query.text = L"wiedzmin";
    if (index.search(query).total != 1) return 64;
    query = {}; query.text = L"cd projekt";
    if (index.search(query).total != 1) return 65;
    query = {}; query.developer = L"valve";
    if (index.search(query).total != 1) return 66;
    query = {}; query.year_from = 2010; query.year_to = 2016; query.sort = Sort::title;
    found = index.search(query);
    if (found.total != 2 || found.games[0]->key != "steam:620") return 67;
    query = {}; query.status = Status::never_played; query.sort = Sort::title;
    found = index.search(query);
    if (found.total != 2 || found.games[0]->title != L"DOOM (1993)") return 68;
    query = {}; query.status = Status::recent; query.now = now;
    if (index.search(query).total != 2) return 69;
    query = {}; query.sort = Sort::shortest;
    found = index.search(query);
    if (found.games[0]->key != "shortcut:7" || found.untimed != 3) return 70;
    query = {}; query.store = L"gog";
    if (index.search(query).total != 1) return 71;
    query = {}; query.sort = Sort::random; query.seed = 3; query.limit = 5;
    if (index.search(query).games.size() != 5) return 72;
    // Titles: exact, folded, aliases, substrings and longer names.
    auto hits = index.lookup(L"portal 2");
    if (hits.empty() || hits[0]->key != "steam:620") return 73;
    if (index.lookup(L"Wiedzmin 3").empty() || index.lookup(L"Doom").front()->key != "steam:2280" || index.lookup(L"Portal 2 GOTY Edition").front()->key != "steam:620" ||
        !index.lookup(L"Zelda").empty()) return 74;
    // Statistics.
    auto stats = index.stats(Group::genre, 3);
    if (stats.total != 5 || stats.played != 3 || stats.never_played != 2 || stats.dated != 4 || stats.buckets.empty() || stats.buckets[0].name != L"Action" ||
        stats.buckets[0].games != 2 || stats.minutes != 600 + 7200 + 2400) return 75;
    stats = index.stats(Group::decade, 10);
    if (stats.buckets.size() != 3 || stats.buckets[0].name != L"1990s" || stats.buckets[1].games != 3 || stats.buckets.back().name != L"?") return 76;
    Query installed; installed.status = Status::installed;
    if (index.stats(Group::store, 10, &installed).total != 4) return 77;
    // Similar: shared genres, tags, studios.
    index.merge("custom:x", Source::rawg, facts({}, {L"Action", L"Adventure"}, {L"Puzzle"}, {L"Valve"}));
    const auto alike = index.similar("steam:620", 3);
    if (alike.empty() || alike[0].first->key != "custom:x" || alike[0].second <= 0.3f) return 78;
    // Best rated counts the votes: 100% of 5 reviews ranks below 97% of 300 000.
    Patch few; few.review_percent = 100; few.review_count = 5;
    index.merge("custom:x", Source::reviews, few);
    query = {}; query.sort = Sort::rating;
    found = index.search(query);
    if (found.games[0]->key != "steam:620" || found.games[1]->key != "custom:x" || ratingScore(*found.games[0]) < 96) return 79;

    // Saved and loaded: the same records; gone games stay a while, then leave.
    const auto saved = index.save();
    auto loaded = Index::load(saved);
    if (!loaded || loaded->size() != index.size() || loaded->get("steam:620")->description != L"Myśl portalami." ||
        loaded->get("steam:2280")->release != Date{1993, 12, 10} || loaded->get("gog:1")->hltb_main != 3000u || loaded->save() != saved) return 80;
    if (Index::load("{}") || Index::load(R"({"format":"launchme-games","version":2,"games":{}})") || Index::load("not json")) return 81;
    loaded->syncLibrary({{"steam:620", library(L"Portal 2", true, now - 86400, 600)}}, now);
    if (loaded->keys().size() != 1 || loaded->get("gog:1")->owned || loaded->size() != 5) return 82;
    query = {}; query.text = L"wiedzmin";
    if (loaded->search(query).total != 0) return 83;
    loaded->syncLibrary({{"steam:620", library(L"Portal 2", true, now - 86400, 600)}}, now + 61 * 86400);
    if (loaded->size() != 1) return 84;
    // Returning before that: its data is still there.
    index.syncLibrary({{"gog:1", library(L"Wiedźmin 3", true, 0)}}, now);
    index.syncLibrary({{"gog:1", library(L"Wiedźmin 3", true, 0)}, {"steam:620", library(L"Portal 2", true, 0)}}, now + 86400);
    if (!index.get("steam:620")->owned || index.get("steam:620")->description != L"Myśl portalami.") return 85;
    // An empty patch removes that source's part.
    index.merge("steam:620", Source::steam_store, {});
    if (!index.get("steam:620")->description.empty() || index.get("steam:620")->from(Source::steam_store)) return 86;
    return 0;
}

// The worker: offline caches and appinfo on a library snapshot, then the background fill through
// a fake network (tag names, the Steam store page, GOG's product page, RAWG), saved and reloaded.
int service() {
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-index-service-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    const auto steam = root / L"Steam";
    const auto file = appinfoFile(true);
    atomic_file::write(steam / L"appcache" / L"appinfo.vdf", std::span<const uint8_t>(file));
    atomic_file::write(root / L"config" / L"plugins" / L"data" / L"launchme.howlongtobeat" / L"737465616d3a363230.json",
                       R"({"data":[{"comp_main":36000,"game_id":5}],"name":"Portal 2","selected":"5"})");
    std::vector<std::wstring> asked;
    std::mutex asked_mutex;
    ServiceConfig config;
    config.config = root / L"config";
    config.steam_root = steam;
    config.rawg_key = L"secret-key";
    config.spacing = std::chrono::milliseconds(1);
    config.now = [] { return uint64_t{1'760'000'000}; };
    const auto answer = [&](const std::wstring& url, const std::wstring&, size_t) -> HttpResponse {
        { std::lock_guard lock(asked_mutex); asked.push_back(url); }
        std::string body;
        if (url.find(L"tagdata/populartags") != std::wstring::npos) {
            body = "[";
            for (int i = 0; i < 25; ++i) body += (i ? "," : "") + std::string("{\"tagid\":") + std::to_string(i == 0 ? 1664 : 5000 + i) + ",\"name\":\"" + (i == 0 ? "Puzzle" : "Tag" + std::to_string(i)) + "\"}";
            body += "]";
        } else if (url.find(L"appdetails?appids=620") != std::wstring::npos)
            body = R"({"620":{"success":true,"data":{"short_description":"Portale.","genres":[{"id":"1"}],"release_date":{"date":"18 kwi 2011"}}}})";
        else if (url.find(L"appdetails?appids=2280") != std::wstring::npos) body = R"({"2280":{"success":false}})";
        else if (url.find(L"api.gog.com/products/1207658924") != std::wstring::npos)
            body = R"({"id":1207658924,"title":"Unreal Tournament GOTY","release_date":"1999-11-30T00:00:00+0200","description":{"lead":"Frag."}})";
        else if (url.find(L"api.rawg.io/api/games?") != std::wstring::npos)
            body = url.find(L"Hollow") != std::wstring::npos ? R"({"results":[{"id":9767,"name":"Hollow Knight"},{"id":1,"name":"Hollow Knight: Silksong"}]})"
                                                              : R"({"results":[{"id":2,"name":"Something Else"}]})";
        else if (url.find(L"api.rawg.io/api/games/9767") != std::wstring::npos)
            body = R"({"id":9767,"name":"Hollow Knight","released":"2017-02-24","genres":[{"name":"Platformer"}],"description_raw":"Bugs."})";
        else return HttpResponse{404, {}};
        return HttpResponse{200, {body.begin(), body.end()}};
    };
    config.get = answer;
    config.get_private = answer;
    config.steam_stats = [](uint64_t, uint32_t app_id) { return std::tuple<std::optional<uint32_t>, uint32_t, uint32_t>{app_id == 620 ? 600u : 0u, 3u, 10u}; };
    std::atomic<uint64_t> notified{0};
    config.changed = [&](uint64_t revision) { notified = revision; };
    auto worker = std::make_shared<Service>(config);
    std::thread thread([worker] { worker->run(); });
    const auto game = [](const char* key, const wchar_t* title, uint32_t app_id, bool installed, uint64_t played, const char* gog = "") {
        LibraryGame item;
        item.key = key; item.cache_key = key_codec::encode(key); item.title = title; item.app_id = app_id; item.installed = installed; item.last_played = played;
        item.gog_id = gog; item.store = app_id ? L"Steam" : *gog ? L"GOG" : L"Moje gry";
        return item;
    };
    worker->library({game("steam:620", L"Portal 2", 620, true, 1'759'000'000), game("steam:2280", L"DOOM (1993)", 2280, false, 0),
                     game("gog:1207658924", L"Unreal Tournament", 0, true, 0, "1207658924"), game("custom:hk", L"Hollow Knight", 0, true, 0),
                     game("custom:x", L"Unknown Thing", 0, false, 0)}, 76561197960265729ull);
    for (int i = 0; i < 400 && !worker->idle(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    if (!worker->idle() || !worker->loaded()) { worker->stop(); thread.join(); return 90; }
    const auto check = worker->read([&](const Index& index) -> int {
        const auto* portal = index.get("steam:620");
        if (!portal || portal->release != Date{2011, 4, 19} || portal->tags.empty() || portal->tags[0] != L"Puzzle" || portal->hltb_main != 600u ||
            portal->description != L"Portale." || portal->playtime_minutes != 600u || portal->achievements_total != 10) return 91;
        const auto* doom = index.get("steam:2280");
        if (!doom || doom->release != Date{1993, 12, 10} || doom->from(Source::steam_stats)) return 92; // Never installed or played here: no stats.
        if (index.get("gog:1207658924")->release != Date{1999, 11, 30} || index.get("gog:1207658924")->description != L"Frag.") return 93;
        if (index.get("custom:hk")->release != Date{2017, 2, 24} || index.get("custom:hk")->genres != std::vector<std::wstring>{L"Platformer"}) return 94;
        if (index.get("custom:x")->release.known() || index.get("custom:x")->from(Source::rawg)) return 95;
        return 0;
    });
    if (check) { worker->stop(); thread.join(); return check; }
    // Installed games came first; nothing was asked twice; a miss is remembered.
    {
        std::lock_guard lock(asked_mutex);
        if (asked.empty() || asked[0].find(L"populartags") == std::wstring::npos || asked[1].find(L"appids=620") == std::wstring::npos) { worker->stop(); thread.join(); return 96; }
        std::set<std::wstring> unique(asked.begin(), asked.end());
        if (unique.size() != asked.size() || asked.size() != 7) { worker->stop(); thread.join(); return 97; }
    }
    if (!std::filesystem::exists(root / L"config" / L"metadata" / L"620.details.json") || !std::filesystem::exists(root / L"config" / L"knowledge" / L"miss" / L"737465616d3a32323830.steam") ||
        !std::filesystem::exists(root / L"config" / L"knowledge" / L"rawg" / (text_encoding::wide(key_codec::encode("custom:hk")) + L".json"))) { worker->stop(); thread.join(); return 98; }
    // Direct facts and a touched cache.
    Patch emulation; emulation.genres = std::vector<std::wstring>{L"Metroidvania"};
    worker->merge("custom:x", Source::emulation, emulation);
    worker->touch({"steam:620"});
    for (int i = 0; i < 200 && worker->read([](const Index& index) { return index.get("custom:x")->genres.empty(); }); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    worker->stop();
    thread.join();
    if (notified.load() == 0) return 99;
    // Saved at the end: a new worker starts from it.
    auto again = std::make_shared<Service>(config);
    std::thread second([again] { again->run(); });
    for (int i = 0; i < 200 && !again->loaded(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const bool kept = again->read([](const Index& index) {
        return index.size() == 5 && index.get("custom:x")->genres == std::vector<std::wstring>{L"Metroidvania"} && index.get("steam:620")->description == L"Portale.";
    });
    again->stop();
    second.join();
    std::filesystem::remove_all(root);
    return kept ? 0 : 100;
}

// Manual: the index over a copy of a real config folder (`--real <folder>`): the Steam games of its
// steam-owned list, this machine's appinfo.vdf and the copied caches; no network, nothing written
// outside <folder>. Prints what Towarzyszka's tools would answer.
int real(const std::filesystem::path& folder, const std::vector<std::wstring>& questions) {
    std::vector<LibraryGame> games;
    uint64_t account = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder / L"metadata" / L"steam-owned")) {
        std::ifstream input(entry.path());
        std::string line;
        std::getline(input, line);
        account = std::stoull(entry.path().stem().string());
        while (std::getline(input, line)) {
            const auto tab = line.find('\t');
            if (tab == std::string::npos) continue;
            LibraryGame item;
            item.app_id = static_cast<uint32_t>(std::stoul(line.substr(0, tab)));
            item.key = "steam:" + std::to_string(item.app_id);
            item.cache_key = key_codec::encode(item.key);
            item.title = text_encoding::wideLenient(line.substr(tab + 1));
            item.store = L"Steam";
            games.push_back(std::move(item));
        }
    }
    ServiceConfig config;
    config.config = folder;
    config.steam_root = findSteamRoot();
    config.background = false;
    auto service = std::make_shared<Service>(config);
    const auto started = GetTickCount64();
    std::thread thread([service] { service->run(); });
    service->library(games, account);
    for (int i = 0; i < 6000 && !(service->idle() && service->read([](const Index& index) { return !index.keys().empty(); })); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::cout << games.size() << " games indexed in " << GetTickCount64() - started << " ms\n";
    service->read([&](const Index& index) {
        const auto stats = index.stats(Group::genre, 8);
        std::cout << "owned " << stats.total << ", dated " << stats.dated << "\nGenres:";
        for (const auto& bucket : stats.buckets) std::cout << " " << text_encoding::utf8Lenient(bucket.name) << "=" << bucket.games;
        std::cout << "\nDecades:";
        for (const auto& bucket : index.stats(Group::decade, 10).buckets) std::cout << " " << text_encoding::utf8Lenient(bucket.name) << "=" << bucket.games;
        size_t described = 0, tagged = 0, timed = 0;
        for (const auto& key : index.keys()) {
            const auto* record = index.get(key);
            described += !record->description.empty(); tagged += !record->tags.empty(); timed += record->hltb_main.has_value();
        }
        std::cout << "\ndescribed " << described << ", tagged " << tagged << ", HowLongToBeat " << timed << "\n";
        const auto show = [](const char* what, const Found& found) {
            std::cout << what << " (" << found.total << "):";
            for (const auto* record : found.games)
                std::cout << " " << text_encoding::utf8Lenient(record->title) << " [" << text_encoding::utf8Lenient(record->release.iso()) << "]";
            std::cout << "\n";
        };
        Query query; query.sort = Sort::release_asc; query.limit = 5;
        show("Oldest", index.search(query));
        query.sort = Sort::release_desc;
        show("Newest", index.search(query));
        query = {}; query.genres = {L"RPG"}; query.sort = Sort::rating; query.limit = 5;
        show("Best RPG", index.search(query));
        return 0;
    });
    // Her answers over this index, from the real model (FOUNDRY_* or opencode).
    int code = 0;
    if (!questions.empty()) {
        using namespace companion;
        const auto connection = resolve(L"", L"", default_model, systemProbe());
        if (!connection.ready()) { std::cerr << "No Foundry host/token found\n"; code = 1; }
        tools::Context context{false, static_cast<uint64_t>(std::time(nullptr)), 11, {}};
        Tools set;
        set.declarations = tools::declarations(false);
        set.run = [&](const std::string& name, const json::Value& args) {
            std::cout << "  tool " << name << " " << json::serialize(args) << "\n";
            return service->read([&](const Index& index) { return tools::run(name, args, index, context); });
        };
        PromptExtras extras; extras.tools = true;
        for (const auto& question : questions) {
            if (code) break;
            const auto asked = GetTickCount64();
            const auto reply = ask(connection, systemPrompt(Persona::tsundere, Look{}, Digest{}, false, context.now, 3, 21, extras), {{true, question}}, post, 420, 1.0f, &set);
            std::cout << text_encoding::utf8Lenient(question) << " (" << GetTickCount64() - asked << " ms)\n  " << text_encoding::utf8Lenient(reply.ok ? reply.text : reply.error) << "\n";
            if (!reply.ok) code = 2;
        }
    }
    service->stop();
    thread.join();
    return code;
}

// Manual: this machine's Steam appinfo cache.
int live() {
    const auto root = findSteamRoot();
    std::cout << "Steam: " << text_encoding::utf8Lenient(root.wstring()) << "\n";
    const auto started = GetTickCount64();
    size_t total = 0;
    const auto apps = steam_appinfo::read(root, [&](uint32_t) { ++total; return true; });
    if (!apps) { std::cerr << "appinfo.vdf unreadable\n"; return 1; }
    std::cout << apps->size() << " apps decoded of " << total << " in " << GetTickCount64() - started << " ms\n";
    size_t games = 0, dated = 0, tagged = 0;
    for (const auto& app : *apps) {
        if (app.type != L"game") continue;
        ++games; if (app.steam_release || app.original_release) ++dated; if (!app.tags.empty()) ++tagged;
    }
    std::cout << games << " games, " << dated << " dated, " << tagged << " tagged\n";
    int shown = 0;
    for (const auto& app : *apps) {
        if (app.type != L"game" || app.genres.empty() || shown >= 8) continue;
        ++shown;
        const auto patch = fromAppInfo(app, {});
        std::cout << app.app_id << " " << text_encoding::utf8Lenient(app.name) << " | " << text_encoding::utf8Lenient(patch.release ? patch.release->iso() : L"?")
                  << " | reviews " << app.review_percent.value_or(-1) << "% mc " << app.metacritic.value_or(-1) << " | genres";
        for (const auto id : app.genres) std::cout << ' ' << id << (steam_appinfo::genreName(id) ? "=" + text_encoding::utf8Lenient(steam_appinfo::genreName(id)) : std::string("=?"));
        std::cout << " | tags " << app.tags.size() << " | dev " << (app.developers.empty() ? "?" : text_encoding::utf8Lenient(app.developers[0]))
                  << " | franchise " << (app.franchises.empty() ? "-" : text_encoding::utf8Lenient(app.franchises[0])) << "\n";
    }
    return 0;
}
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc == 2 && std::string_view(argv[1]) == "--appinfo") return live();
    if (argc >= 3 && std::string_view(argv[1]) == "--real") {
        std::vector<std::wstring> questions;
        for (int i = 3; i < argc; ++i) questions.push_back(text_encoding::wideLegacy(argv[i]));
        return real(argv[2], questions);
    }
    int code = dates();
    if (!code) code = appinfo();
    if (!code) code = sources();
    if (!code) code = index();
    if (!code) code = service();
    if (code) std::cerr << "Game index test failed at checkpoint " << code << '\n';
    else std::cout << "Game index: done\n";
    return code;
}
