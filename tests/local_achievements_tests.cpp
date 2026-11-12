// Local achievements of DRM-free games (src/library/local_achievements.hpp): the save format and
// AppID found in a game folder, the shared save folders, both formats, the game's own schema with
// its local icons, and the unlocks laid over a schema or none.
#include "../src/library/local_achievements.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
namespace fs = std::filesystem;
int failures = 0;
void check(bool condition, const char* what) {
    if (!condition) { std::cerr << "FAILED: " << what << '\n'; ++failures; }
}
void text(const fs::path& file, const std::string& value) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << value;
}
void picture(const fs::path& file) {
    std::string bytes(128, '\0');
    bytes[0] = static_cast<char>(0x89); bytes[1] = 'P'; bytes[2] = 'N'; bytes[3] = 'G';
    text(file, bytes);
}
const SteamAchievement* item(const SteamAchievements& list, const std::string& id) {
    for (const auto& entry : list.items) if (entry.id == id) return &entry;
    return nullptr;
}
const std::string json_state = R"({
  "ACH_FIRST": {"earned": true, "earned_time": 1747279005},
  "ACH_SECOND": {"earned": false, "earned_time": 0},
  "ach_third": {"earned": true, "earned_time": 1747350889}
})";
const std::string ini_state = "\xef\xbb\xbf[ach_collect_hats]\r\nachieved=true\r\ntimestamp=1747279005\r\n\r\n\r\n"
                                    "[ach_grappled]\nachieved=true\ntimestamp=1747354883\n\n[ach_locked]\nachieved=false\ntimestamp=0\n";
}

int main() {
    using namespace local_achievements;
    const auto root = fs::temp_directory_path() / L"launchme-local-achievements-tests";
    std::error_code ignored;
    fs::remove_all(root, ignored);
    const Places places{root / L"Public Documents", root / L"Roaming"};

    // (1) Save formats.
    {
        const auto saved = parseJson(json_state);
        check(saved.size() == 3, "JSON: every entry");
        check(saved[0].id == "ACH_FIRST" && saved[0].achieved && saved[0].time == 1747279005, "JSON: earned with time");
        check(saved[1].id == "ACH_SECOND" && !saved[1].achieved && !saved[1].time, "JSON: locked");
        check(parseJson("[1,2]").empty() && parseJson("{\"x\": {\"other\": 1}}").empty(), "JSON: not a save");
        const auto ini = parseIni(ini_state);
        check(ini.size() == 3, "INI: every section");
        check(ini[0].id == "ach_collect_hats" && ini[0].achieved && ini[0].time == 1747279005, "INI: BOM, CRLF");
        check(ini[2].id == "ach_locked" && !ini[2].achieved, "INI: locked");
    }

    // (2) JSON saves in the roaming folder and a schema with local icons.
    {
        const auto game = root / L"Games" / L"Celeste";
        text(game / L"steam_api64.dll", "x");
        text(game / L"steam_settings" / L"steam_appid.txt", "504230\n");
        text(game / L"steam_settings" / L"achievements.json", R"([
            {"name": "ACH_FIRST", "displayName": {"english": "First", "polish": "Pierwsze"}, "description": "Do it", "hidden": "0",
             "icon": "achievement_images/first.png", "icongray": "first_gray.png"},
            {"name": "ACH_SECOND", "displayName": "Second", "description": "Again", "hidden": 1, "icon": "img/second.png"},
            {"name": "ACH_THIRD", "displayName": "Third", "description": "", "icon": "https://cdn.example/third.jpg"}
        ])");
        picture(game / L"steam_settings" / L"achievement_images" / L"first.png");
        picture(game / L"steam_settings" / L"img" / L"first_gray.png");
        picture(game / L"steam_settings" / L"img" / L"second.png");
        text(places.roaming_app_data / L"GSE Saves" / L"504230" / L"achievements.json", json_state);
        const auto found = find(game, places);
        check(found && found.format == Format::json && found.app_id == 504230, "JSON: format and AppID");
        check(found.state == places.roaming_app_data / L"GSE Saves" / L"504230" / L"achievements.json", "JSON: shared save folder");
        const auto list = localList(found, {}, false);
        check(list.named && list.list.total == 3 && list.list.unlocked == 2 && list.list.complete, "JSON: counts over the schema");
        const auto* first = item(list.list, "ACH_FIRST");
        check(first && first->name == L"Pierwsze" && first->achieved && first->unlocked_at == 1747279005, "schema: localized name, time");
        check(first && first->icon.ends_with(L"first.png") && fs::exists(first->icon), "schema: icon in achievement_images");
        const auto* second = item(list.list, "ACH_SECOND");
        check(second && !second->achieved && second->hidden && second->icon.ends_with(L"second.png"), "schema: locked, hidden, icon in img");
        const auto* third = item(list.list, "ACH_THIRD");
        check(third && third->achieved && third->icon == L"https://cdn.example/third.jpg", "schema: id matched case-insensitively, https icon");
        check(list.list.items.front().id == "ach_third" || list.list.items.front().id == "ACH_THIRD", "newest unlock first");
    }

    // (3) INI saves: RealAppId beside the game, saves in Public Documents; no schema → ids.
    {
        const auto game = root / L"Games" / L"Shooter";
        text(game / L"Binaries" / L"Win64" / L"OnlineFix.ini", "[Main]\r\nFakeAppId=480\r\nRealAppId=1551360\r\n");
        text(game / L"steam_appid.txt", "480");
        text(places.public_documents / L"OnlineFix" / L"1551360" / L"Stats" / L"Achievements.ini", ini_state);
        const auto found = find(game, places);
        check(found && found.format == Format::ini && found.app_id == 1551360, "INI: RealAppId wins");
        auto list = localList(found, {}, true);
        check(!list.named && list.list.unlocked == 2 && list.list.total == 3, "INI: unlocks without a schema");
        const auto* hats = item(list.list, "ach_collect_hats");
        check(hats && hats->name == L"Collect hats", "INI: readable id");
        // Earned ones only: the public shares complete the list.
        const auto earned = overlay({}, {{"ach_a", true, 5}, {"ach_b", true, 6}});
        check(!earned.complete && earned.total == 2, "earned only is partial");
        auto completed = earned;
        applyShares(completed, {{"ACH_A", 50.f}, {"ach_b", 10.f}, {"ach_c", 3.f}});
        check(completed.complete && completed.total == 3 && completed.unlocked == 2, "shares: the missing one added locked");
        check(item(completed, "ach_a") && item(completed, "ach_a")->global_percent == 50.f, "shares: matched case-insensitively");
    }

    // (4) local_save.txt: saves inside the game folder.
    {
        const auto game = root / L"Games" / L"Portable";
        text(game / L"steam_appid.txt", "777");
        text(game / L"local_save.txt", "saves");
        text(game / L"saves" / L"777" / L"achievements.json", json_state);
        const auto found = find(game, places);
        check(found && found.format == Format::json && found.state == game / L"saves" / L"777" / L"achievements.json", "local_save.txt");
    }

    // (5) A plain DRM-free game: a steam_appid.txt alone has no local achievements.
    {
        const auto game = root / L"Games" / L"Plain";
        text(game / L"steam_appid.txt", "999");
        text(game / L"data" / L"achievements.json", R"({"settings": true})");
        check(!find(game, places), "plain game: none");
        // Nothing saved yet: found, everything locked.
        const auto fresh = root / L"Games" / L"Fresh";
        text(fresh / L"OnlineFix.ini", "RealAppId=1234");
        const auto found = find(fresh, places);
        check(found && found.format == Format::ini && found.state.empty(), "nothing saved yet");
    }

    // (6) The Web API's schema written into a steam_settings folder without one, its icons from
    // whichever Steam host has them; an existing schema is never replaced.
    {
        const auto urls = iconUrls(L"https://steamcdn-a.akamaihd.net/steamcommunity/public/images/apps/1938090/abc123.jpg", 1938090);
        check(urls.size() >= 4 && urls.front().ends_with(L"abc123.jpg"), "icons: the schema's URL first");
        check(std::find(urls.begin(), urls.end(), L"https://shared.fastly.steamstatic.com/community_assets/images/apps/1938090/abc123.jpg") != urls.end(),
              "icons: community_assets");
        check(iconUrls(L"https://example.com/not-an-icon", 1).empty(), "icons: not a Steam image");

        const auto game = root / L"Games" / L"NoSchema";
        text(game / L"steam_settings" / L"steam_appid.txt", "1938090");
        const auto found = find(game, places);
        check(found && found.format == Format::json && found.state.empty(), "steam_settings without saves");
        SteamAchievements english, polish;
        SteamAchievement entry;
        entry.id = "rex_sp_noharmdone"; entry.name = L"No Harm Done"; entry.description = L"Finish it";
        entry.icon = L"https://steamcdn-a.akamaihd.net/steamcommunity/public/images/apps/1938090/aaaa.jpg";
        entry.gray_icon = L"https://steamcdn-a.akamaihd.net/steamcommunity/public/images/apps/1938090/bbbb.jpg";
        english.items.push_back(entry);
        entry.name = L"Bez rozlewu krwi"; entry.description = L"Ukończ";
        polish.items.push_back(entry);
        size_t requests = 0;
        const SteamHttp get = [&](const std::wstring& url, const std::wstring&, size_t) {
            ++requests;
            HttpResponse reply;
            if (url.find(L"community_assets") == std::wstring::npos) { reply.status = 404; return reply; } // Only the newest path has them.
            reply.status = 200;
            reply.body.assign(128, 0); reply.body[0] = 0xff; reply.body[1] = 0xd8;
            return reply;
        };
        check(saveSchema(found, english, polish, get), "schema: written");
        check(fs::exists(game / L"steam_settings" / L"img" / L"aaaa.jpg") && fs::exists(game / L"steam_settings" / L"img" / L"bbbb.jpg"), "schema: icons saved");
        const auto written = localList(find(game, places), {}, false);
        const auto* saved = item(written.list, "rex_sp_noharmdone");
        check(written.named && saved && saved->name == L"Bez rozlewu krwi" && saved->icon.ends_with(L"bbbb.jpg"), "schema: read back, Polish, grey while locked");
        const auto before = requests;
        check(!saveSchema(found, english, polish, get) && requests == before, "schema: an existing one stays");
    }

    // (7) Ids as names.
    check(readableId("ACH_COLLECT_HATS") == L"Collect hats", "id: shouted");
    check(readableId("ACH_AdornedInFlowers") == L"Adorned In Flowers", "id: CamelCase");
    check(readableId("NEW_ACHIEVEMENT_1_0") == L"New achievement 1 0", "id: generated");

    fs::remove_all(root, ignored);
    if (failures) return 1;
    std::cout << "local achievements: ok\n";
    return 0;
}
