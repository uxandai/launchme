// launchme.achievements (plugins/achievements) without a network: GOG's and Epic's answers, the
// store-request rounds as the host runs them, RetroAchievements' hashes and lookups with a fake
// transport, RPCS3's TROPUSR.DAT and shadPS4's TROP.XML from files built here, and the host's
// side of the protocol (src/plugins/achievements.cpp).
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include "../plugins/achievements/achievements.hpp"
#include "../src/plugins/achievements.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/text_encoding.hpp"

#include <Windows.h>

#include <fstream>
#include <iostream>

#define CHECK(value) do { if (!(value)) { std::cerr << "achievements check failed at line " << __LINE__ << ": " << #value << '\n'; return __LINE__; } } while (false)

namespace {
using namespace achievements;
using plugins::wire::text;
using plugins::wire::field;
HttpResponse reply(std::string_view body, uint32_t status = 200) { return {status, std::vector<uint8_t>(body.begin(), body.end())}; }
json::Value request(const std::string& game, const std::string& extra = {}) {
    return *json::parse(R"({"method":"achievements","locale":"en","settings":{"ra_user":"player","ra_key":"secret-key","rpcs3":"","shadps4":""},"params":{"game":)" +
                        game + R"(,"accounts":{"gog":true,"epic":true})" + extra + "}}");
}
void put32(std::vector<uint8_t>& b, uint32_t v) { for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(v >> s)); }
void put64(std::vector<uint8_t>& b, uint64_t v) { put32(b, static_cast<uint32_t>(v >> 32)); put32(b, static_cast<uint32_t>(v)); }
// A TROPUSR.DAT with a type-4 table and a type-6 table of `count` trophies; `unlocked` ones at `at` (unix).
std::vector<uint8_t> tropusr(uint32_t count, const std::vector<uint32_t>& unlocked, uint64_t at) {
    std::vector<uint8_t> b;
    put32(b, 0x818F54AD); put32(b, 0x00010000); put32(b, 2); put32(b, 0); b.resize(48, 0);
    const uint64_t first = 48 + 2 * 32, second = first + count * 0x50ULL;
    for (const auto& [type, size, offset] : {std::tuple{4u, 0x50u, first}, std::tuple{6u, 0x60u, second}}) {
        put32(b, type); put32(b, size); put32(b, 0); put32(b, count); put64(b, offset); put64(b, 0);
    }
    for (uint32_t i = 0; i < count; ++i) { const auto start = b.size(); put32(b, 4); put32(b, 0x50); put32(b, i); put32(b, 0); put32(b, i); put32(b, 1); b.resize(start + 0x50, 0); }
    for (uint32_t i = 0; i < count; ++i) {
        const auto start = b.size();
        const bool earned = std::find(unlocked.begin(), unlocked.end(), i) != unlocked.end();
        put32(b, 6); put32(b, 0x60); put32(b, i); put32(b, 0); put32(b, i); put32(b, earned ? 1 : 0); put32(b, 0); put32(b, 0);
        const uint64_t ticks = earned ? (at + 62135596800ULL) * 1000000ULL : 0;
        put64(b, ticks); put64(b, ticks);
        b.resize(start + 0x60, 0);
    }
    return b;
}
bool write(const std::filesystem::path& file, std::string_view text) {
    std::filesystem::create_directories(file.parent_path());
    return atomic_file::write(file, std::string(text));
}
bool write(const std::filesystem::path& file, const std::vector<uint8_t>& bytes) {
    std::filesystem::create_directories(file.parent_path());
    return atomic_file::write(file, std::span<const uint8_t>(bytes));
}

int formats() {
    CHECK(parseIsoTime("2024-03-05T10:20:30+00:00") == 1709634030);
    CHECK(parseIsoTime("2024-03-05T10:20:30Z") == 1709634030 && parseIsoTime("2024-03-05T12:20:30.123+0200") == 1709634030);
    CHECK(parseIsoTime("2024-03-05 10:20:30") == 1709634030 && !parseIsoTime("yesterday") && !parseIsoTime("2024-13-05T10:20:30Z"));
    // GOG: unlocked with its colour icon and date, locked with the grey one; rarity; hidden = !visible.
    const auto gog = parseGog(R"({"total_count":2,"items":[
        {"achievement_id":"1","achievement_key":"FIRST","visible":true,"name":"First","description":"Begin","image_url_unlocked":"https://images.gog.com/u.png",
         "image_url_locked":"https://images.gog.com/l.png","date_unlocked":"2024-03-05T10:20:30+0000","rarity":42.5},
        {"achievement_id":"2","achievement_key":"SECRET","visible":false,"name":"Secret","description":"?","image_url_unlocked":"https://images.gog.com/u2.png",
         "image_url_locked":"https://images.gog.com/l2.png","date_unlocked":null,"rarity":"3.1"}]})");
    CHECK(gog && gog->items.size() == 2 && gog->source == "GOG");
    CHECK(gog->items[0].id == "FIRST" && gog->items[0].achieved && gog->items[0].unlocked_at == 1709634030 && gog->items[0].icon == "https://images.gog.com/u.png" && gog->items[0].percent == 42.5);
    CHECK(!gog->items[1].achieved && gog->items[1].hidden && gog->items[1].icon == "https://images.gog.com/l2.png" && std::abs(gog->items[1].percent - 3.1) < 1e-9);
    CHECK(!parseGog("{}") && !parseGog("not json"));
    CHECK(gogUrl("1207658924") == L"https://gameplay.gog.com/clients/1207658924/users/{account}/achievements");
    // Epic: the schema's locked texts, the player's unlocks with the unlocked ones.
    auto schema = parseEpicSchema(R"({"data":{"Achievement":{"productAchievementsRecordBySandbox":{"productId":"prod","achievements":[
        {"achievement":{"name":"A1","hidden":false,"rarity":{"percent":12.5},"unlockedDisplayName":"Won","lockedDisplayName":"Win","unlockedDescription":"You won",
         "lockedDescription":"Win once","unlockedIconLink":"https://cdn1.epicgames.com/u.png","lockedIconLink":"https://cdn1.epicgames.com/l.png"}},
        {"achievement":{"name":"A2","hidden":true,"rarity":{"percent":1},"unlockedDisplayName":"Hidden","lockedDisplayName":"","unlockedDescription":"Secret text",
         "lockedDescription":"","unlockedIconLink":"https://cdn1.epicgames.com/u2.png","lockedIconLink":"https://cdn1.epicgames.com/l2.png"}}]}}}})");
    CHECK(schema && schema->product == "prod" && schema->list.items.size() == 2 && schema->list.items[0].name == "Win" && schema->list.items[0].percent == 12.5);
    CHECK(schema->list.items[1].hidden && schema->list.items[1].description.empty()); // A hidden one keeps its secret while locked.
    CHECK(applyEpicPlayer(*schema, R"({"data":{"PlayerProfile":{"playerProfile":{"productAchievements":{"__typename":"PlayerProductAchievementsResponseSuccess","data":{"totalUnlocked":1,
        "playerAchievements":[{"playerAchievement":{"achievementName":"A1","unlocked":true,"unlockDate":"2024-03-05T10:20:30.000Z"}}]}}}}}})"));
    CHECK(schema->list.items[0].achieved && schema->list.items[0].name == "Won" && schema->list.items[0].unlocked_at == 1709634030 && !schema->list.items[1].achieved);
    CHECK(epicPlayerQuery("prod").find("\"epicAccountId\":\"{account}\"") != std::string::npos && epicSchemaQuery("sb", "en-US").find("\"sandboxId\":\"sb\"") != std::string::npos);
    return 0;
}

int retroAchievements(const std::filesystem::path& root) {
    CHECK(retroConsole("Nintendo SNES", {}) == 3 && retroConsole("", L"C:\\roms\\mario.nes") == 7 && retroConsole("Retro", L"C:\\roms\\zelda.gba") == 5);
    CHECK(retroConsole("Sega Genesis", {}) == 1 && retroConsole("", L"x.sfc.zip") == 3 && retroConsole("PC", L"game.exe") == 0);
    // NES: the iNES header does not count; SNES: neither does a copier header; zips by their first file.
    std::vector<uint8_t> body(32768);
    for (size_t i = 0; i < body.size(); ++i) body[i] = static_cast<uint8_t>(i * 7);
    auto headed = std::vector<uint8_t>{'N', 'E', 'S', 0x1A}; headed.resize(16, 0); headed.insert(headed.end(), body.begin(), body.end());
    CHECK(write(root / L"plain.nes", body) && write(root / L"headed.nes", headed));
    const auto plain = retroHash(7, root / L"plain.nes");
    CHECK(plain.size() == 32 && plain == retroHash(7, root / L"headed.nes") && plain == md5Hex(body));
    auto copier = std::vector<uint8_t>(512, 0); copier.insert(copier.end(), body.begin(), body.end());
    CHECK(write(root / L"copier.smc", copier) && retroHash(3, root / L"copier.smc") == plain);
    CHECK(md5Hex({'a', 'b', 'c'}) == "900150983cd24fb0d6963f7d28e17f72");
    // N64: .v64 (byte-swapped) hashes as the big-endian .z64.
    std::vector<uint8_t> z64{0x80, 0x37, 0x12, 0x40}; z64.resize(4096, 0x5A); z64[100] = 1; z64[101] = 2;
    auto v64 = z64; for (size_t i = 0; i + 1 < v64.size(); i += 2) std::swap(v64[i], v64[i + 1]);
    CHECK(write(root / L"a.z64", z64) && write(root / L"a.v64", v64) && retroHash(2, root / L"a.z64") == retroHash(2, root / L"a.v64"));
    {
        mz_zip_archive zip{};
        CHECK(mz_zip_writer_init_heap(&zip, 0, 0));
        CHECK(mz_zip_writer_add_mem(&zip, "readme.txt", "hi", 2, static_cast<mz_uint>(MZ_DEFAULT_LEVEL)));
        CHECK(mz_zip_writer_add_mem(&zip, "game.nes", headed.data(), headed.size(), static_cast<mz_uint>(MZ_DEFAULT_LEVEL)));
        void* data = nullptr; size_t size = 0;
        CHECK(mz_zip_writer_finalize_heap_archive(&zip, &data, &size));
        std::vector<uint8_t> zipped(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        mz_zip_writer_end(&zip);
        CHECK(write(root / L"zipped.zip", zipped) && retroHash(7, root / L"zipped.zip") == plain);
    }
    CHECK(retroHash(27, L"C:\\mame\\sf2.zip") == md5Hex({'s', 'f', '2'}) && retroHash(12, root / L"plain.nes").empty()); // Arcade by name; discs by title.
    CHECK(retroHash(16, root / L"plain.nes").empty() && retroHash(76, root / L"plain.nes").empty()); // GameCube, PC Engine CD: discs.
    // ES-DE / EmuDeck system folders decide first; systems RA has no achievements for stop there.
    CHECK(retroConsole("PS1", L"K:\\Emulation\\roms\\psx\\Castlevania.chd") == 12 && retroConsole("Retro", L"K:\\Emulation\\roms\\snes\\Mario.zip") == 3);
    CHECK(retroConsole("GameCube", L"K:\\Emulation\\roms\\gc\\Zelda.iso") == 16 && retroConsole("PS2", L"K:\\roms\\ps2\\RE.chd") == 21);
    CHECK(retroConsole("Retro", L"K:\\roms\\megadrive\\Japan\\Sonic.md") == 1); // Sub-folders of a system folder.
    CHECK(retroConsole("Switch", L"K:\\Emulation\\roms\\switch\\Zelda.nsp") == 0 && retroConsole("Wii U", L"K:\\Emulation\\roms\\wiiu\\Mario.wux") == 0);
    CHECK(retroConsole("Wii", L"K:\\Emulation\\roms\\wii\\Galaxy.rvz") == 0 && retroConsole("Switch", L"D:\\games\\Zelda.nsp") == 0);
    // A plain zip: by the file inside.
    CHECK(retroConsole("Retro", root / L"zipped.zip") == 7);
    // The console's game list (API_GetGameList, f=1&h=1): by hash, then by title without dump tags.
    const std::string list = R"([{"Title":"Legend of Zelda, The: A Link to the Past","ID":5,"Hashes":["AB12CD34AB12CD34AB12CD34AB12CD34"]},
        {"Title":"Pokemon Red Version | Pokemon Blue Version","ID":9,"Hashes":[]},
        {"Title":"~Hack~ Castlevania: Symphony of the Night","ID":30,"Hashes":[]},
        {"Title":"Castlevania: Symphony of the Night [Subset - Bonus]","ID":31,"Hashes":[]},
        {"Title":"Castlevania: Symphony of the Night","ID":32,"Hashes":[]}])";
    CHECK(retroGameList(list) && !retroGameList("<html></html>") && !retroGameList(R"({"message":"x"})"));
    CHECK(retroGameByHash(list, "ab12cd34ab12cd34ab12cd34ab12cd34") == 5u && !retroGameByHash(list, "00000000000000000000000000000000") && !retroGameByHash(list, ""));
    CHECK(retroGameByTitle(list, "The Legend of Zelda - A Link to the Past") == 5u && retroGameByTitle(list, "Pokemon Blue Version") == 9u);
    CHECK(retroGameByTitle(list, "Castlevania - Symphony of the Night (USA) [Disc 1]") == 32u && retroGameByTitle(list, "Castlevania: Symphony of the Night") == 32u);
    CHECK(!retroGameByTitle(list, "Castlevania"));
    const auto progress = parseRetroProgress(R"({"Title":"Game","NumDistinctPlayers":200,"Achievements":{
        "2":{"ID":2,"Title":"Second","Description":"Two","BadgeName":"00222","NumAwarded":50,"DisplayOrder":2},
        "1":{"ID":1,"Title":"First","Description":"One","BadgeName":"00111","NumAwarded":150,"DisplayOrder":1,"DateEarned":"2024-03-05 10:20:30"}}})");
    CHECK(progress && progress->items.size() == 2 && progress->items[0].id == "1" && progress->items[0].achieved && progress->items[0].unlocked_at == 1709634030);
    CHECK(progress->items[0].icon == "https://media.retroachievements.org/Badge/00111.png" && progress->items[1].icon == "https://media.retroachievements.org/Badge/00222_lock.png");
    CHECK(progress->items[0].percent == 75 && progress->items[1].percent == 25);
    // The service: the console's list (documented web API, key in y), the ROM's hash in it,
    // remembered, then the progress (the key stays in the request, never in an answer).
    Service service;
    std::vector<std::wstring> asked;
    service.get = [&](const std::wstring& url) {
        asked.push_back(url);
        if (url == L"https://retroachievements.org/API/API_GetGameList.php?y=secret-key&i=7&f=1&h=1")
            return reply(R"([{"Title":"Other","ID":76,"Hashes":["ffffffffffffffffffffffffffffffff"]},{"Title":"Not Mario","ID":77,"Hashes":[")" + plain + R"("]}])");
        if (url == L"https://retroachievements.org/API/API_GetGameList.php?y=secret-key&i=12&f=1&h=1")
            return reply(R"([{"Title":"Castlevania: Symphony of the Night","ID":11240,"Hashes":["0123456789abcdef0123456789abcdef"]}])");
        if (url == L"https://retroachievements.org/API/API_GetGameInfoAndUserProgress.php?y=secret-key&u=player&g=77" ||
            url == L"https://retroachievements.org/API/API_GetGameInfoAndUserProgress.php?y=secret-key&u=player&g=11240")
            return reply(R"({"NumDistinctPlayers":4,"Achievements":{"9":{"ID":9,"Title":"Done","Description":"d","BadgeName":"1","NumAwarded":1,"DateEarnedHardcore":"2024-03-05 10:20:30"}}})");
        return reply("", 404);
    };
    service.dispatch(*json::parse(R"({"method":"activate","params":{"dataDirectory":)" + plugins::wire::quote(text_encoding::utf8(root.wstring())) + "}}"));
    const auto game = R"({"key":"shortcut:9","name":"Mario","source":"custom","shortcut":true,"platform":"NES","rom":)" + plugins::wire::quote(text_encoding::utf8((root / L"headed.nes").wstring())) + "}";
    auto answer = service.dispatch(request(game));
    CHECK(field(answer, "achievements").type == json::Value::Type::object && text(field(answer, "achievements"), "source") == "RetroAchievements");
    CHECK(field(field(answer, "achievements"), "items").elements.size() == 1 && asked.size() == 2);
    CHECK(plugins::wire::encode(answer).find("secret-key") == std::string::npos);
    asked.clear();
    answer = service.dispatch(request(game));
    CHECK(asked.size() == 1); // The game ID is remembered.
    // A disc (EmuDeck's roms\psx): by title in the PlayStation list; the list is cached for the next game.
    const auto disc = R"({"key":"shortcut:12","name":"Castlevania: Symphony of the Night","source":"custom","shortcut":true,"platform":"PS1","rom":"K:\\Emulation\\roms\\psx\\Castlevania - Symphony of the Night.chd"})";
    asked.clear();
    CHECK(field(field(service.dispatch(request(disc)), "achievements"), "items").elements.size() == 1 && asked.size() == 2);
    const auto other = R"({"key":"shortcut:13","name":"Unknown Game","source":"custom","shortcut":true,"platform":"PS1","rom":"K:\\Emulation\\roms\\psx\\Unknown.chd"})";
    asked.clear();
    CHECK(field(service.dispatch(request(other)), "achievements").type == json::Value::Type::null_value && asked.empty());
    // Switch, Wii U: no RetroAchievements, nothing asked.
    const auto nsp = R"({"key":"shortcut:14","name":"Zelda","source":"custom","shortcut":true,"platform":"Switch","rom":"K:\\Emulation\\roms\\switch\\Zelda.nsp"})";
    CHECK(field(service.dispatch(request(nsp)), "achievements").type == json::Value::Type::null_value && asked.empty());
    // A wrong key: said so, nothing remembered.
    service.get = [&](const std::wstring& url) { asked.push_back(url); return reply(R"({"message":"Unauthenticated."})", 401); };
    const auto snes = R"({"key":"shortcut:15","name":"Mario World","source":"custom","shortcut":true,"platform":"Retro","rom":"K:\\Emulation\\roms\\snes\\World.sfc"})";
    CHECK(text(service.dispatch(request(snes)), "message").find("Web API key") != std::string::npos);
    CHECK(text(service.dispatch(request(snes)), "message").find("Web API key") != std::string::npos && asked.size() == 2);
    // Without a key: nothing to ask.
    auto keyless = request(game); keyless.properties["settings"].properties["ra_key"] = plugins::wire::string("");
    asked.clear();
    CHECK(field(service.dispatch(keyless), "achievements").type == json::Value::Type::null_value && asked.empty());
    return 0;
}

int trophies(const std::filesystem::path& root) {
    // RPCS3: the game's TROPDIR names its trophy set; the emulator's trophy folder holds the rest.
    const auto game = root / L"games" / L"BLUS30001" / L"PS3_GAME";
    CHECK(write(game / L"USRDIR" / L"EBOOT.BIN", std::string_view("eboot")));
    std::filesystem::create_directories(game / L"TROPDIR" / L"NPWR01234_00");
    CHECK(ps3CommunicationId(game / L"USRDIR" / L"EBOOT.BIN", {}) == "NPWR01234_00");
    const auto ps3_iso = root / L"games" / L"MotorStorm.iso";
    std::string iso_head(0x9000, '\0');
    iso_head.replace(0x8800, 12, "NPWR01234_00");
    CHECK(write(ps3_iso, std::string_view(iso_head)) && ps3CommunicationId(ps3_iso, {}) == "NPWR01234_00");
    const auto rpcs3 = root / L"rpcs3";
    const auto folder = rpcs3 / L"dev_hdd0" / L"home" / L"00000001" / L"trophy" / L"NPWR01234_00";
    CHECK(write(folder / L"TROPCONF.SFM", std::string_view(R"(<?xml version="1.0"?><trophyconf version="1.1"><npcommid>NPWR01234_00</npcommid><title-name>Game &amp; More</title-name>
        <trophy id="000" hidden="no" ttype="P" pid="-1"><name>Platinum</name><detail>All of them</detail></trophy>
        <trophy id="001" hidden="yes" ttype="B" pid="000"><name>Secret</name><detail>Hidden &lt;one&gt;</detail></trophy>
        <trophy id="002" hidden="no" ttype="S" pid="000"><name>Third</name><detail>Three</detail></trophy></trophyconf>)")));
    CHECK(write(folder / L"TROPUSR.DAT", tropusr(3, {1, 2}, 1709634030)));
    CHECK(write(folder / L"TROP001.PNG", std::string_view("png")));
    const auto states = parseTropusr(tropusr(3, {1}, 1709634030));
    CHECK(states && states->size() == 3 && !(*states)[0].second && (*states)[1].second == 1709634030ull);
    CHECK(!parseTropusr({1, 2, 3}));
    const auto found = rpcs3TrophyFolder(rpcs3, "NPWR01234_00");
    CHECK(found && *found == folder);
    const auto ps3 = readRpcs3(*found);
    CHECK(ps3 && ps3->items.size() == 3 && ps3->source == "RPCS3" && !ps3->items[0].achieved && ps3->items[1].achieved && ps3->items[1].hidden);
    CHECK(ps3->items[1].unlocked_at == 1709634030 && ps3->items[1].description == "Hidden <one>" && ps3->items[2].achieved);
    CHECK(text_encoding::wideLenient(ps3->items[1].icon) == (folder / L"TROP001.PNG").wstring() && ps3->items[0].icon.empty());
    // shadPS4: by the CUSA ID in the game's path.
    const auto shadps4 = root / L"shadps4";
    const auto trophy = shadps4 / L"user" / L"game_data" / L"CUSA01234" / L"trophyfiles" / L"trophy00";
    CHECK(write(trophy / L"Xml" / L"TROP.XML", std::string_view(R"(<trophyconf><title-name>Bloodborne</title-name>
        <trophy id="0" hidden="no" ttype="G" unlockstate="true" timestamp="1709634030000"><name>Gold</name><detail>g</detail></trophy>
        <trophy id="1" hidden="yes" ttype="B"><name>Bronze</name><detail>b</detail></trophy></trophyconf>)")));
    CHECK(write(trophy / L"Icons" / L"TROP000.PNG", std::string_view("png")));
    GameInfo bloodborne; bloodborne.name = "Bloodborne"; bloodborne.rom = L"D:\\PS4\\CUSA01234\\eboot.bin";
    const auto ps4 = readShadps4(shadps4, bloodborne);
    CHECK(ps4 && ps4->items.size() == 2 && ps4->items[0].achieved && ps4->items[0].unlocked_at == 1709634030 && !ps4->items[1].achieved && ps4->items[1].hidden);
    CHECK(!ps4->items[0].icon.empty() && ps4->items[1].icon.empty());
    GameInfo by_title; by_title.name = "Bloodborne";
    CHECK(readShadps4(shadps4, by_title));
    // The service routes PS3/PS4 shortcuts to the emulators set in its settings.
    Service service;
    service.get = [](const std::wstring&) { return HttpResponse{}; };
    auto ps3_request = request(R"({"key":"shortcut:3","name":"Game","source":"custom","shortcut":true,"platform":"PS3","rom":)" +
                               plugins::wire::quote(text_encoding::utf8((game / L"USRDIR" / L"EBOOT.BIN").wstring())) + "}");
    ps3_request.properties["settings"].properties["rpcs3"] = plugins::wire::string(text_encoding::utf8(rpcs3.wstring()));
    CHECK(text(field(service.dispatch(ps3_request), "achievements"), "source") == "RPCS3");
    return 0;
}

// The host's loop (pluginAchievementRounds) with a fake account: the plugin asks, the host
// answers in the next round, until a list comes.
int rounds() {
    Service service;
    const auto run = [&](json::Value input, const std::function<HttpResponse(const plugins::StoreRequest&)>& account, int& rounds_made) {
        for (rounds_made = 1; rounds_made <= static_cast<int>(plugins::max_store_rounds); ++rounds_made) {
            const auto response = service.dispatch(input);
            const auto answer = plugins::parseAchievements(response);
            if (answer.kind != plugins::AchievementsAnswer::Kind::requests) return answer;
            std::vector<std::pair<std::string, std::optional<HttpResponse>>> answers;
            for (const auto& item : answer.requests) answers.emplace_back(item.id, account(item));
            input.properties["params"].properties["responses"] = plugins::storeResponses(answers);
            input.properties["params"].properties["state"] = answer.state;
        }
        return plugins::AchievementsAnswer{};
    };
    int made = 0;
    auto gog = run(request(R"({"key":"gog:1207658924","name":"Witcher","source":"gog","sourceId":"1207658924"})"), [](const plugins::StoreRequest& item) {
        return item.account == "gog" && item.request.url == L"https://gameplay.gog.com/clients/1207658924/users/{account}/achievements" && !item.request.post ?
            reply(R"({"items":[{"achievement_key":"K","name":"N","description":"D","visible":true,"date_unlocked":"2024-03-05T10:20:30+0000","image_url_unlocked":"https://images.gog.com/a.png"}]})") :
            reply("", 500);
    }, made);
    CHECK(gog.kind == plugins::AchievementsAnswer::Kind::list && made == 2 && gog.list.total == 1 && gog.list.unlocked == 1 && gog.source == L"GOG");
    CHECK(gog.list.items[0].unlocked_at == 1709634030 && gog.list.items[0].icon == L"https://images.gog.com/a.png");
    auto absent = run(request(R"({"key":"gog:1","name":"No","source":"gog","sourceId":"1"})"), [](const plugins::StoreRequest&) { return reply("", 404); }, made);
    CHECK(absent.kind == plugins::AchievementsAnswer::Kind::none);
    auto signed_out = request(R"({"key":"gog:1","name":"No","source":"gog","sourceId":"1"})");
    signed_out.properties["params"].properties["accounts"].properties["gog"] = plugins::wire::boolean(false);
    CHECK(plugins::parseAchievements(service.dispatch(signed_out)).kind == plugins::AchievementsAnswer::Kind::none);
    auto epic = run(request(R"({"key":"epic:App","name":"Epic game","source":"epic","sourceId":"App","epicNamespace":"sandbox1"})"), [](const plugins::StoreRequest& item) {
        if (item.account != "epic" || !item.request.post || item.request.url != L"https://launcher.store.epicgames.com/graphql") return reply("", 400);
        if (item.id == "schema" && item.request.body.find("\"sandboxId\":\"sandbox1\"") != std::string::npos)
            return reply(R"({"data":{"Achievement":{"productAchievementsRecordBySandbox":{"productId":"p1","achievements":[{"achievement":{"name":"E1","hidden":false,
                "unlockedDisplayName":"Got","lockedDisplayName":"Get","unlockedDescription":"","lockedDescription":"","unlockedIconLink":"https://cdn1.epicgames.com/a.png","lockedIconLink":""}}]}}}})");
        if (item.id == "player" && item.request.body.find("\"productId\":\"p1\"") != std::string::npos && item.request.body.find("{account}") != std::string::npos)
            return reply(R"({"data":{"PlayerProfile":{"playerProfile":{"productAchievements":{"data":{"playerAchievements":[{"playerAchievement":{"achievementName":"E1","unlocked":true,"unlockDate":"2024-03-05T10:20:30.000Z"}}]}}}}}})");
        return reply("", 400);
    }, made);
    CHECK(epic.kind == plugins::AchievementsAnswer::Kind::list && made == 3 && epic.list.unlocked == 1 && epic.list.items[0].name == L"Got");
    return 0;
}

// The host's validation: allowed hosts per account, items and their limits.
int protocol() {
    using plugins::storeHostAllowed;
    CHECK(storeHostAllowed("gog", L"https://gameplay.gog.com/clients/1/users/{account}/achievements") && !storeHostAllowed("gog", L"https://evil.test/gog.com"));
    CHECK(!storeHostAllowed("gog", L"http://gameplay.gog.com/x") && !storeHostAllowed("gog", L"https://gameplay.gog.com@evil.test/"));
    CHECK(storeHostAllowed("epic", L"https://launcher.store.epicgames.com/graphql") && !storeHostAllowed("epic", L"https://epicgames.com.evil.test/"));
    CHECK(!storeHostAllowed("steam", L"https://api.steampowered.com/") && !storeHostAllowed("epic", L"https://notepicgames.com/"));
    bool threw = false;
    try { plugins::parseAchievements(*json::parse(R"({"storeRequests":[{"id":"a","account":"gog","method":"GET","url":"https://evil.test/"}]})")); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { plugins::parseAchievements(*json::parse(R"({"achievements":{"items":[{"id":"a"},{"id":"a"}]}})")); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    const auto list = plugins::parseAchievements(*json::parse(R"({"achievements":{"source":"X","items":[
        {"id":"a","name":"Locked","achieved":false,"unlockedAt":5,"percent":50,"icon":"relative/icon.png"},
        {"id":"b","name":"Old","achieved":true,"unlockedAt":100,"icon":"https://x.test/b.png"},
        {"id":"c","name":"New","achieved":true,"unlockedAt":200,"percent":250}]}})"));
    CHECK(list.kind == plugins::AchievementsAnswer::Kind::list && list.list.total == 3 && list.list.unlocked == 2 && list.list.complete);
    CHECK(list.list.items[0].id == "c" && list.list.items[0].global_percent < 0 && list.list.items[2].id == "a" && list.list.items[2].unlocked_at == 0 && list.list.items[2].icon.empty());
    CHECK(plugins::parseAchievements(*json::parse(R"({"achievements":null})")).kind == plugins::AchievementsAnswer::Kind::none);
    CHECK(plugins::parseAchievements(*json::parse(R"({"message":"later"})")).kind == plugins::AchievementsAnswer::Kind::message);
    HttpResponse big; big.status = 200; big.body.assign(500 * 1024, 'x');
    const auto responses = plugins::storeResponses({{"a", reply("{}")}, {"b", std::nullopt}, {"c", big}});
    CHECK(responses.elements.size() == 3 && text(responses.elements[0], "body") == "{}" && field(responses.elements[1], "status").numeric == 0);
    CHECK(text(responses.elements[2], "body").empty() && field(responses.elements[2], "truncated").flag);
    return 0;
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-achievements-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); } } cleanup{root};
    if (const int failed = formats()) return failed;
    if (const int failed = retroAchievements(root)) return failed;
    if (const int failed = trophies(root)) return failed;
    if (const int failed = rounds()) return failed;
    if (const int failed = protocol()) return failed;
    std::cout << "Achievements plugin: GOG, Epic, RetroAchievements, RPCS3, shadPS4, store-request rounds and validation passed\n";
    return 0;
}
