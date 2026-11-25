// Xbox account / launchme-xbox contracts with fixture files and a fake helper: no network, no
// Microsoft account and no real helper is ever used by this test.
#include "../src/store/xbox_account.hpp"
#include "../src/library/pc_platforms.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/file_io.hpp"
#include "../src/platform/text_encoding.hpp"
#include "../src/net/json.hpp"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#ifndef _WIN32
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define CHECK(x) do { if (!(x)) { std::cerr << "Xbox check " << __LINE__ << ": " << #x << '\n'; return __LINE__; } } while (false)

namespace {
constexpr auto owned = "9NBLGGH4R315";
constexpr auto pass = "9PGAMEPASS01";
constexpr auto family = "Studio.Moonlit_8wekyb3d8bbwe";
std::string library() {
    return R"json({"version":1,"synced_at":1790000000,"username":"pat@example.com","game_pass":"CFQ7TTC0KGQ8",
      "gamepass_recent":["9PGAMEPASS01","<bad>","9PGAMEPASS01"],"games":[
      {"product_id":"9NBLGGH4R315","title":"Moonlit: Archive","pfn":"Studio.Moonlit_8wekyb3d8bbwe","own_install":"supported","acquisition":"purchased",
       "xbox_title_id":"1717113201","bundle":"9PREMIUMED01",
       "addons":[{"product_id":"9NEXPANSION1","title":"Moonlit: Deep Archive","content":true,"image":"https://store-images.s-microsoft.com/a.png"},
                 {"product_id":"9NSOUNDTRAK1","title":"Soundtrack","content":false,"image":"javascript:x"},
                 {"product_id":"9NEXPANSION1","title":"Duplicate"},{"product_id":"9NBLGGH4R315","title":"Itself"},{"product_id":"<x>","title":"Bad"}],
       "images":{"poster":"https://store-images.s-microsoft.com/poster.png","hero":"https://store-images.s-microsoft.com/hero.jpg","logo":"https://store-images.s-microsoft.com/logo.png"}},
      {"product_id":"9PGAMEPASS01","title":"Pass Quest","pfn":null,"own_install":"unsupported","acquisition":"gamepass","images":{"box":"https://store-images.s-microsoft.com/box.png","hero":"javascript:alert(1)"}},
      {"product_id":"<bad>","title":"Broken"},
      {"product_id":"9NBLGGH4R315","title":"Duplicate","pfn":"Studio.Moonlit_8wekyb3d8bbwe"}]})json";
}
bool write(const std::filesystem::path& file, std::string_view text) { return atomic_file::write(file, text); }
// The helper's `activity` file: Moonlit played (minutes, counts), another title, and junk.
constexpr auto activity_json = R"json({"version":1,"synced_at":1790000000,"titles":[
  {"title_id":"1717113201","name":"Moonlit","last_played":1789990000,"minutes":754,
   "achievements":{"unlocked":12,"total":40,"gamerscore":250,"total_gamerscore":1000}},
  {"title_id":"99","name":"Other","last_played":1700000000,"minutes":null,"achievements":null},
  {"title_id":"x1","last_played":5},{"title_id":"1717113201","minutes":1},
  {"title_id":"55","achievements":{"unlocked":5,"total":2}}]})json";
std::string marker(const std::string& product, const std::string& exe) {
    return "{\"product_id\":\"" + product + "\",\"executable\":" + json::quote(exe) + ",\"display_name\":\"Moonlit\"}";
}
template<class F> bool wait(F done, int ms = 5000) {
    for (int i = 0; i < ms / 10; ++i) { if (done()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    return done();
}
std::wstring option(const std::vector<std::wstring>& args, std::wstring_view name) {
    for (size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == name) return args[i + 1];
    return {};
}
#ifndef _WIN32
// Linux: the run action's wrapper when the launcher's GameAction has one (see runAction).
template<class Action> std::vector<std::wstring> wrapperOf(const Action& action) {
    if constexpr (requires { action.wrapper.front(); }) return {action.wrapper.begin(), action.wrapper.end()};
    else return {};
}
bool script(const std::filesystem::path& file, std::string_view body) {
    return write(file, "#!/bin/sh\n" + std::string(body)) && chmod(file.c_str(), 0755) == 0;
}
#endif
}

int main() {
    using namespace xbox_account;
#ifdef _WIN32
    wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH, temp);
    const auto root = std::filesystem::path(temp) / (L"launchme-xbox-test-" + std::to_wstring(GetCurrentProcessId()));
#else
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-xbox-test-" + std::to_wstring(GetCurrentProcessId()));
#endif
    std::error_code ec; std::filesystem::remove_all(root, ec); std::filesystem::create_directories(root, ec);
    struct Cleanup { std::filesystem::path p; ~Cleanup() { std::error_code e; std::filesystem::remove_all(p, e); } } cleanup{root};

    // Identifiers and the helper's answer: the last JSON line wins, unknown error codes are internal.
    CHECK(validProductId(owned) && !validProductId("9N") && !validProductId("9NBLG-GH4R315"));
    CHECK(validFamilyName(family) && !validFamilyName("NoHash") && !validFamilyName("Bad Name_x") && !validFamilyName("_x"));
    CHECK(folderName(L"Moonlit: Archive\u2122 ", owned) == L"Moonlit Archive" && folderName(L"???", owned) == L"9NBLGGH4R315");
    CHECK(market().size() == 2 && !language().empty());
#ifndef _WIN32
    {
        // Linux: the Store's market and language from the user's locale.
        const char* before = getenv("LC_ALL"); const std::string saved = before ? before : "";
        setenv("LC_ALL", "pl_PL.UTF-8", 1); CHECK(market() == L"PL" && language() == L"pl-PL");
        setenv("LC_ALL", "pt_BR@euro", 1); CHECK(market() == L"BR" && language() == L"pt-BR");
        setenv("LC_ALL", "C.UTF-8", 1); CHECK(market() == L"US" && language() == L"en-US");
        if (before) setenv("LC_ALL", saved.c_str(), 1); else unsetenv("LC_ALL");
    }
#endif
    {
        const auto ok = parseResult("log line\n{\"ok\":true,\"username\":\"pat@example.com\",\"puid\":\"0123ABCD\"}\r\n");
        CHECK(ok && ok->ok && ok->username == "pat@example.com" && ok->puid == "0123ABCD");
        const auto failed = parseResult("{\"ok\":false,\"error\":\"not_entitled\",\"message\":\"no license\"}");
        CHECK(failed && !failed->ok && failed->error == "not_entitled" && failed->message == L"no license");
        const auto weird = parseResult("{\"ok\":false,\"error\":\"rm -rf /\"}");
        CHECK(weird && weird->error == "internal");
        CHECK(!parseResult("") && !parseResult("not json") && !parseResult("{\"ok\":tru"));
        const auto progress = parseProgress(R"({"progress":{"stage":"download","done":125,"total":1000}})");
        CHECK(progress && progress->stage == "download" && progress->done == 125 && progress->total == 1000);
        CHECK(!parseProgress(R"({"progress":{"stage":"download","done":9,"total":3}})") && !parseProgress("Downloading 5%") &&
              !parseProgress(R"({"progress":{"stage":"download","done":-1,"total":3}})"));
        CHECK(parseAccount(R"({"username":"pat@example.com","puid":"0123ABCD"})") && !parseAccount(R"({"username":"","puid":"0123ABCD"})") &&
              !parseAccount(R"({"username":"pat","puid":"<x>"})"));
    }

    // Helper 0.2 answers: play history, the offline PC, updates and a title's achievements.
    {
        const auto activity = parseActivity(activity_json);
        CHECK(activity.size() == 3 && activity.contains(1717113201) && activity.contains(99) && activity.contains(55));
        const auto& moonlit = activity.at(1717113201);
        CHECK(moonlit.last_played == 1789990000 && moonlit.minutes == 754u && moonlit.unlocked == 12 && moonlit.total == 40 &&
              moonlit.gamerscore == 250 && moonlit.total_gamerscore == 1000);
        CHECK(!activity.at(99).minutes && activity.at(99).total == 0 && activity.at(55).total == 0); // 5 of 2: not counts.
        CHECK(parseActivity("").empty() && parseActivity("{\"titles\":{}}").empty());
        std::vector<Game> games(3);
        for (auto& g : games) g.source = GameSource::xbox;
        games[0].xbox_title_id = 1717113201; games[0].store_playtime_minutes = 10; games[0].last_played = 1;
        games[1].xbox_title_id = 1717113201; games[1].store_playtime_minutes = 9000; games[1].last_played = 1800000000;
        games[2].source = GameSource::steam; games[2].xbox_title_id = 1717113201;
        applyActivity(games, activity);
        CHECK(games[0].store_playtime_minutes == 754 && games[0].last_played == 1789990000);
        CHECK(games[1].store_playtime_minutes == 9000 && games[1].last_played == 1800000000); // Never lowered.
        CHECK(games[2].store_playtime_minutes == 0 && games[2].last_played == 0);             // Xbox games only.

        const auto offline = parseOffline(R"({"version":1,"enabled":true,"checked_at":1790000000,"remaining":2,"max":3,"period_days":365,
            "next_change_at":1800000000,"games":{"9NBLGGH4R315":1792000000,"9PGAMEPASS01":0,"<x>":1792000000,"9P4D0K92BM7V":"soon"}})");
        CHECK(offline.enabled == true && offline.checked_at == 1790000000 && offline.remaining == 2u && offline.max == 3u && offline.period_days == 365u &&
              offline.next_change_at == 1800000000 && offline.games.size() == 1);
        CHECK(offlineUntil(offline, owned, 1790000000) == 1792000000 && offlineUntil(offline, owned, 1792000000) == 0 && offlineUntil(offline, pass, 0) == 0);
        CHECK(!parseOffline("").enabled && parseOffline(R"({"enabled":false})").enabled == false && !parseOffline(R"({"enabled":"yes"})").enabled);
        CHECK(offlineDate(1792000000) == L"14.10.2026" && offlineDate(0) == L"01.01.1970");
        Snapshot s; s.account = "0123ABCD";
        CHECK(!offlineText(s, 1790000000).empty());
        s.offline = offline; s.offline_failure = Failure::declined;
        const auto text = offlineText(s, 1790000000);
        CHECK(text.find(L"1") != std::wstring::npos && text.find(L"2/3") != std::wstring::npos && text.find(L"15.01.2027") != std::wstring::npos);

        const auto updates = parseUpdates(R"({"ok":true,"updates":[{"product_id":"9NBLGGH4R315","installed":"1.0.0.0","available":"1.0.1.0","update":true},
            {"product_id":"9PGAMEPASS01","update":false},{"product_id":"<x>","update":true},{"product_id":"9P4D0K92BM7V","update":"true"}]})");
        CHECK(updates == std::set<std::string>{owned});
        CHECK(parseUpdates(serializeUpdates({owned, pass})) == (std::set<std::string>{owned, pass}) && parseUpdates("[]").empty());
        CHECK(answerLine("x\n{\"ok\":true}\r\nnot json\n") == "{\"ok\":true}" && answerLine("no answer").empty() && answerLine("").empty());

        const auto list = parseAchievements(R"({"ok":true,"title_id":"1717113201","unlocked":1,"total":3,"partial":true,"items":[
            {"id":"1","name":"First Light","description":"Begin.","unlocked":true,"secret":false,"gamerscore":10,"unlocked_at":1789000000,
             "icon":"https://images-eds-ssl.xboxlive.com/i.png","rarity":42.5},
            {"id":"2","name":"Hidden","description":"","unlocked":false,"secret":true,"icon":"http://plain.example/i.png","rarity":150},
            {"id":"1","name":"Duplicate"},{"id":"","name":"No id"},
            {"id":"3","name":"Third","unlocked":false,"unlocked_at":1789000000}]})", 1717113201);
        CHECK(list && list->total == 3 && list->unlocked == 1 && !list->complete);
        CHECK(list->items[0].id == "1" && list->items[0].name == L"First Light" && list->items[0].achieved && list->items[0].unlocked_at == 1789000000 &&
              list->items[0].icon == L"https://images-eds-ssl.xboxlive.com/i.png" && list->items[0].global_percent > 42.0f);
        CHECK(list->items[1].hidden && !list->items[1].achieved && list->items[1].icon.empty() && list->items[1].global_percent < 0.5f);
        CHECK(list->items[2].unlocked_at == 0); // Locked: no unlock time.
        CHECK(!parseAchievements(R"({"title_id":"1717113201","items":[]})", 99) && !parseAchievements("{}", 1717113201) && !parseAchievements("", 0));

        gog::InstallSnapshot failed; failed.state = gog::InstallState::error;
        failed.error = gog::InstallError::device_limit; CHECK(xbox_account::installStatusText(failed).find(L"account.microsoft.com/devices") != std::wstring::npos);
        failed.error = gog::InstallError::account; CHECK(xbox_account::installStatusText(failed).find(L"GOG") == std::wstring::npos && !xbox_account::installStatusText(failed).empty());
        CHECK(installError("device_limit") == gog::InstallError::device_limit && installError("not_entitled") == gog::InstallError::account);
    }

    // Install records: the marker names an executable inside the folder; nothing outside it.
    const auto config = root / L"config";
    const auto game_dir = root / L"Games" / L"Xbox-Moonlit";
    std::filesystem::create_directories(game_dir / L"Bin", ec);
    CHECK(write(game_dir / L"Bin" / L"Game.exe", "MZ"));
    CHECK(markerExecutable(marker(owned, "Bin\\Game.exe"), game_dir, owned) == game_dir / L"Bin" / L"Game.exe");
    CHECK(markerExecutable(marker(owned, "..\\..\\cmd.exe"), game_dir, owned).empty());
    CHECK(markerExecutable(marker(owned, "C:\\Windows\\notepad.exe"), game_dir, owned).empty());
    CHECK(markerExecutable(marker(owned, "Bin\\Missing.exe"), game_dir, owned).empty());
    CHECK(markerExecutable(marker(pass, "Bin\\Game.exe"), game_dir, owned).empty()); // Another product's folder.
    {
        const std::vector<OwnInstall> installs{{owned, family, game_dir}, {"<x>", family, game_dir}, {pass, pass, L"relative\\dir"}};
        const auto parsed = parseInstalls(serializeInstalls(installs));
        CHECK(parsed.size() == 1 && parsed[0].product == owned && parsed[0].directory == game_dir);
    }

    // The library: account games by package family name (else product ID), art only over https.
    {
        const auto games = parseLibrary(library(), {}, "0123ABCD", config);
        CHECK(games.size() == 2);
        CHECK(games[0].source == GameSource::xbox && games[0].source_id == family && games[0].xbox_product == owned && games[0].xbox_own_install &&
              !games[0].installed && games[0].account_id == "0123ABCD" && games[0].name == L"Moonlit: Archive");
        CHECK(games[0].store_tall_art == "https://store-images.s-microsoft.com/poster.png" && games[0].store_wide_art == "https://store-images.s-microsoft.com/hero.jpg" &&
              games[0].store_logo_art == "https://store-images.s-microsoft.com/logo.png");
        CHECK(games[1].source_id == pass && !games[1].xbox_own_install && games[1].store_tall_art == "https://store-images.s-microsoft.com/box.png" &&
              games[1].store_wide_art.empty());
        // Demos, betas and "Not Supported" leftovers stay out; words only (Demonic, Alphabet keep theirs).
        CHECK(unlistedTitle(L"Demo Forza Horizon 4") && unlistedTitle(L"Forza Motorsport 7 Demo") && unlistedTitle(L"Fable Legends Closed Beta") &&
              unlistedTitle(L"Roblox - Not Supported") && unlistedTitle(L"GAME (BETA)"));
        CHECK(!unlistedTitle(L"Demonic Souls") && !unlistedTitle(L"Betamax Racer") && !unlistedTitle(L"Gears of War 4") && !unlistedTitle(L"Quantum Break"));
        // Add-ons under their game: content or licence, https art only, no duplicates or the game itself.
        const auto addons = parseAddons(library());
        CHECK(addons.size() == 1 && addons.contains(owned) && addons.at(owned).size() == 2);
        CHECK(addons.at(owned)[0].product == "9NEXPANSION1" && addons.at(owned)[0].title == L"Moonlit: Deep Archive" && addons.at(owned)[0].content &&
              addons.at(owned)[0].image == "https://store-images.s-microsoft.com/a.png");
        CHECK(!addons.at(owned)[1].content && addons.at(owned)[1].image.empty() && parseAddons("{}").empty());
        // Game Pass: only the subscription's games (a bundle's or a bought one is the account's own).
        CHECK(!games[0].xbox_game_pass && games[1].xbox_game_pass && games[0].xbox_title_id == 1717113201 && games[1].xbox_title_id == 0);
#ifdef _WIN32
        CHECK(games[0].install_action.target == L"ms-windows-store://pdp/?ProductId=9NBLGGH4R315");
        CHECK(xboxAppInstall(owned, true).target == L"msxbox://game/?productId=9NBLGGH4R315" && !xboxAppInstall("<x>", true));
#else
        // No Xbox app or Microsoft Store on Linux: nothing to hand the install to.
        CHECK(!games[0].install_action && !xboxAppInstall(owned, true) && !xboxAppInstall(owned, false));
#endif
        CHECK(parseLibrary(library(), {}, {}, config).empty()); // Signed out: no account games.
        CHECK(parseLibrary("[", {}, "a", config).empty() && parseLibrary("{}", {}, "a", config).empty());
    }
    // Graj of a game the helper installed runs the helper, not the (encrypted) game EXE.
    {
        const auto action = runAction(config, game_dir);
        CHECK(action.target == helperPath(config).wstring() && action.kind == ActionKind::play && action.working_directory == game_dir);
        CHECK(action.arguments.find(L"run") != std::wstring::npos && action.arguments.find(L"\"" + game_dir.wstring() + L"\"") != std::wstring::npos &&
              action.arguments.find(L"--profile") != std::wstring::npos);
    }
#ifndef _WIN32
    // Linux: the game's EXE is the target the launcher's Wine/Proton runner builds its command for,
    // and the helper wraps that command (`run ... --`); without GameAction::wrapper, the helper itself.
    {
        const auto exe = game_dir / L"Bin" / L"Game.exe";
        const auto action = runAction(config, game_dir, exe);
        const auto wrapper = wrapperOf(action);
        if (!wrapper.empty()) {
            CHECK(action.target == exe.wstring() && action.working_directory == game_dir && action.arguments.empty());
            CHECK(wrapper.front() == helperPath(config).wstring() && wrapper.back() == L"--" && option(wrapper, L"--dir") == game_dir.wstring() &&
                  option(wrapper, L"--profile") == profileDirectory(config).wstring() && std::find(wrapper.begin(), wrapper.end(), L"run") != wrapper.end());
        } else CHECK(action.target == helperPath(config).wstring() && action.arguments == runAction(config, game_dir).arguments);
    }
#endif

    // Merge: an Xbox app installation of the same package keeps its entry and gains the account's art;
    // the others install through the Xbox app when it is there.
    {
        StoreLibrary native;
        native.clients.push_back({GameSource::xbox, L"C:\\Program Files\\WindowsApps\\Microsoft.GamingApp\\XboxPcApp.exe"});
        Game local; local.kind = GameKind::store; local.source = GameSource::xbox; local.source_id = family; local.name = L"Moonlit (local)";
        local.installed = true; local.play_action = {L"shell:AppsFolder\\Studio.Moonlit_8wekyb3d8bbwe!Game"};
        native.visible.push_back(local);
        const auto merged = mergeXboxLibrary(native, parseLibrary(library(), {}, "0123ABCD", config));
        CHECK(merged.visible.size() == 2);
        CHECK(merged.visible[0].installed && merged.visible[0].name == L"Moonlit (local)" && merged.visible[0].account_id == "0123ABCD" &&
              merged.visible[0].xbox_product == owned && merged.visible[0].store_tall_art == "https://store-images.s-microsoft.com/poster.png" &&
              merged.visible[0].play_action.target.starts_with(L"shell:AppsFolder\\"));
        CHECK(merged.visible[1].install_action.target == L"msxbox://game/?productId=9PGAMEPASS01" && merged.visible[1].install_action.kind == ActionKind::install);
        native.clients.clear(); native.visible.clear();
        const auto without_app = mergeXboxLibrary(native, parseLibrary(library(), {}, "0123ABCD", config));
        CHECK(without_app.visible.size() == 2 && without_app.visible[1].install_action.target == L"ms-windows-store://pdp/?ProductId=9PGAMEPASS01");
    }

    // The service with a fake helper: sign-in, sync, install with progress, uninstall and sign-out.
    std::vector<std::vector<std::wstring>> calls;
    std::mutex calls_mutex;
    bool expire_next_sync = false;
    Operations operations;
    operations.prepare = [&](const std::filesystem::path& c, bool, const Cancel&, const std::function<void(uint64_t)>&) {
        return c == config ? std::filesystem::path(L"C:\\fake\\launchme-xbox.exe") : std::filesystem::path{};
    };
    operations.invoke = [&](const std::filesystem::path&, const std::filesystem::path& c, const std::vector<std::wstring>& args, const Cancel&,
                            const std::function<void(std::string_view)>& line) {
        { std::lock_guard lock(calls_mutex); calls.push_back(args); }
        helper_process::Result result; result.started = c == config; result.exit_code = 0; result.ok = true;
        const auto command = args.empty() ? std::wstring{} : args[0];
        if (command == L"login") result.output = "{\"ok\":true,\"signed_in\":true,\"username\":\"pat@example.com\",\"puid\":\"0123ABCD\"}";
        else if (command == L"library") {
            if (expire_next_sync) { result.ok = false; result.exit_code = 1; result.output = "{\"ok\":false,\"error\":\"signed_out\",\"message\":\"expired\"}"; }
            else { write(option(args, L"--out"), library()); result.output = "{\"ok\":true,\"games\":2}"; }
        } else if (command == L"install") {
            const std::filesystem::path target(option(args, L"--dest"));
            if (option(args, L"--product") != text_encoding::wide(owned)) {
                result.ok = false; result.output = "{\"ok\":false,\"error\":\"not_entitled\",\"message\":\"x\"}"; return result;
            }
            if (line) { line(R"({"progress":{"stage":"metadata","done":0,"total":0}})"); line(R"({"progress":{"stage":"download","done":1,"total":2}})"); }
            std::this_thread::sleep_for(std::chrono::milliseconds(200)); // The poller sees the progress.
            std::error_code e; std::filesystem::create_directories(target / L"Bin", e);
            write(target / L"Bin" / L"Game.exe", "encrypted");
            write(target / L".xodus-streaming.msixvc", "header");
            write(target / L".launchme-xbox.json", marker(owned, "Bin\\Game.exe"));
            result.output = "{\"ok\":true,\"executable\":\"Bin\\\\Game.exe\"}";
        } else if (command == L"logout") result.output = "{\"ok\":true,\"signed_in\":false}";
        else if (command == L"activity") { write(option(args, L"--out"), activity_json); result.output = "{\"ok\":true,\"titles\":2}"; }
        else if (command == L"updates") result.output = "{\"ok\":true,\"updates\":[{\"product_id\":\"9NBLGGH4R315\",\"update\":true}]}";
        else if (command == L"offline-pc") {
            // The helper writes the designation it read (or set) into the profile's offline.json.
            const bool set = option(args, L"--set") == L"true";
            if (!option(args, L"--set").empty() && option(args, L"--confirm") != L"yes") { result.ok = false; result.output = "{\"ok\":false,\"error\":\"usage\"}"; return result; }
            write(profileDirectory(config) / L"offline.json", std::string("{\"enabled\":") + (set ? "true" : "false") +
                  ",\"checked_at\":1790000000,\"remaining\":2,\"max\":3,\"games\":{\"9NBLGGH4R315\":4000000000}}");
            result.output = "{\"ok\":true}";
        } else if (command == L"achievements") {
            if (option(args, L"--title") != L"1717113201") { result.ok = false; result.output = "{\"ok\":false,\"error\":\"not_found\"}"; return result; }
            result.output = "{\"ok\":true,\"title_id\":\"1717113201\",\"items\":[{\"id\":\"1\",\"name\":\"First Light\",\"unlocked\":true}]}";
        }
        else { result.ok = false; result.output = "{\"ok\":false,\"error\":\"usage\"}"; }
        return result;
    };
    {
        auto service = makeService(config, operations);
        std::optional<Snapshot> snapshot;
        const auto until = [&](auto done) { return wait([&] { if (auto s = service->take()) snapshot = std::move(s); return snapshot && done(*snapshot); }); };
        service->submit(Command::restore);
        CHECK(until([](const Snapshot& s) { return s.state == State::disconnected; }) && snapshot->account.empty());
        service->submit(Command::connect);
        CHECK(until([](const Snapshot& s) { return s.state == State::connected; }));
        CHECK(snapshot->account == "0123ABCD" && snapshot->name == L"pat@example.com" && snapshot->games == 2 && snapshot->game_pass &&
              snapshot->synced_at == 1790000000);
        // The sync also read the play history; Game Pass's newest come with the library.
        CHECK(snapshot->activity.size() == 3 && snapshot->gamepass_recent == std::vector<std::string>{pass} && snapshot->updates.empty());
        {
            const auto profile = readProfile(config);
            CHECK(profile.games[0].store_playtime_minutes == 754 && profile.games[0].last_played == 1789990000);
            std::lock_guard lock(calls_mutex); // No installs yet: no `updates` question.
            CHECK(std::none_of(calls.begin(), calls.end(), [](const auto& a) { return !a.empty() && a[0] == L"updates"; }));
        }
        // The offline PC: asked, then turned on (the confirmation is the caller's: --confirm yes).
        service->submit(Command::offline_status);
        CHECK(until([](const Snapshot& s) { return s.offline.enabled == false && !s.offline_busy; }));
        service->submit(Command::offline_on);
        CHECK(until([](const Snapshot& s) { return s.offline.enabled == true && !s.offline_busy && s.offline_failure == Failure::none; }));
        CHECK(offlineUntil(snapshot->offline, owned, 1790000000) == 4000000000ull && snapshot->offline.remaining == 2u);
        {
            std::lock_guard lock(calls_mutex);
            const auto set = std::find_if(calls.begin(), calls.end(), [](const auto& a) { return !a.empty() && a[0] == L"offline-pc" && option(a, L"--set") == L"true"; });
            CHECK(set != calls.end() && option(*set, L"--confirm") == L"yes");
        }
        // A title's achievements, answered once; an unknown title fails without a list.
        service->achievements(1717113201);
        std::optional<AchievementsAnswer> answer;
        CHECK(wait([&] { if (auto a = service->takeAchievements()) answer = std::move(a); return answer.has_value(); }));
        CHECK(answer->title_id == 1717113201 && !answer->failed && answer->list.total == 1 && answer->list.unlocked == 1);
        answer.reset();
        service->achievements(12345);
        CHECK(wait([&] { if (auto a = service->takeAchievements()) answer = std::move(a); return answer.has_value(); }));
        CHECK(answer->title_id == 12345 && answer->failed && answer->list.items.empty());
        {
            std::lock_guard lock(calls_mutex);
            const auto sync = std::find_if(calls.begin(), calls.end(), [](const auto& a) { return !a.empty() && a[0] == L"library"; });
            CHECK(sync != calls.end() && option(*sync, L"--out") == libraryPath(config).wstring() && option(*sync, L"--market").size() == 2);
        }
        // Install: only an account game the helper supports, into the chosen folder, progress in bytes, verified.
        auto games = readProfile(config).games;
        CHECK(games.size() == 2);
        const auto moonlit = games[0];
        gog::InstallRequest request; request.product = moonlit.source_id; request.install_directory = game_dir.parent_path() / L"Xbox-Moonlit-2";
        CHECK(service->install(moonlit, request));
        CHECK(!service->install(moonlit, request)); // One at a time.
        gog::InstallSnapshot install; bool saw_bytes = false;
        CHECK(wait([&] {
            while (auto s = service->takeInstall()) { install = *s; saw_bytes |= install.state == gog::InstallState::downloading && install.total == 2; }
            return install.state == gog::InstallState::completed || install.state == gog::InstallState::error;
        }));
        if (install.state != gog::InstallState::completed) std::cerr << "install state " << static_cast<int>(install.state) << " error " << static_cast<int>(install.error) << "\n";
        CHECK(install.state == gog::InstallState::completed && saw_bytes && install.product == family);
        {
            const auto profile = readProfile(config);
            CHECK(profile.account && profile.games.size() == 2 && profile.games[0].installed && profile.games[0].install_directory == request.install_directory);
#ifdef _WIN32
            CHECK(profile.games[0].executable == request.install_directory / L"Bin" / L"Game.exe" &&
                  profile.games[0].play_action.target == helperPath(config).native());
#else
            const auto& play = profile.games[0].play_action;
            CHECK(profile.games[0].executable == request.install_directory / L"Bin" / L"Game.exe" &&
                  (wrapperOf(play).empty() ? play.target == helperPath(config).wstring() :
                   play.target == profile.games[0].executable.wstring() && wrapperOf(play).front() == helperPath(config).wstring()));
#endif
            Game copy = profile.games[0];
            CHECK(ownInstall(copy));
        }
        // With a game installed here, a sync asks for updates; installing it again clears its mark.
        service->submit(Command::sync);
        CHECK(until([](const Snapshot& s) { return s.state == State::connected && s.updates.contains(owned); }));
        CHECK(service->install(moonlit, request));
        CHECK(wait([&] { while (auto s = service->takeInstall()) install = *s; return install.state == gog::InstallState::completed; }));
        CHECK(parseUpdates(file_io::readText(updatesPath(config), 1 << 20)).empty());
        // The Game Pass title is MSIXVC2 here: the helper is never asked.
        CHECK(service->install(games[1], request));
        CHECK(wait([&] { while (auto s = service->takeInstall()) install = *s; return install.state == gog::InstallState::error; }));
        CHECK(install.error == gog::InstallError::unavailable);
        Game stranger = moonlit; stranger.xbox_product = "9ZZZZZZZZZZZ";
        CHECK(service->install(stranger, request));
        CHECK(wait([&] { while (auto s = service->takeInstall()) install = *s; return install.state == gog::InstallState::error; }));
        CHECK(install.error == gog::InstallError::account);

        // Uninstall: only a recorded folder with its marker; the record goes with it.
        Game unrecorded = readProfile(config).games[0]; unrecorded.install_directory = game_dir;
        CHECK(!uninstall(config, unrecorded) && std::filesystem::exists(game_dir / L"Bin" / L"Game.exe"));
        CHECK(uninstall(config, readProfile(config).games[0]));
        CHECK(!std::filesystem::exists(request.install_directory) && !readProfile(config).games[0].installed);

        // An expired session signs the account out locally.
        expire_next_sync = true;
        service->submit(Command::sync);
        CHECK(until([](const Snapshot& s) { return s.state == State::error && s.failure == Failure::expired; }));
        CHECK(!readProfile(config).account && !std::filesystem::exists(libraryPath(config)));
        expire_next_sync = false;
        service->submit(Command::connect);
        CHECK(until([](const Snapshot& s) { return s.state == State::connected; }));
        service->submit(Command::logout);
        CHECK(until([&](const Snapshot& s) { return s.account.empty() && !service->busy(); }));
        CHECK(!std::filesystem::exists(profileDirectory(config) / L"account.json") && !std::filesystem::exists(libraryPath(config)) &&
              !std::filesystem::exists(activityPath(config)) && !std::filesystem::exists(updatesPath(config)));
        std::lock_guard lock(calls_mutex);
        CHECK(std::any_of(calls.begin(), calls.end(), [](const auto& a) { return a.size() == 1 && a[0] == L"logout"; }));
    }
#ifndef _WIN32
    // Linux: the real helper path and process with a fake launchme-xbox (a shell script): the helper
    // is the one next to the launcher (here LAUNCHME_XBOX_HELPER), never downloaded, and runs with
    // its private 0700 profile.
    {
        const auto fake = root / L"bin" / L"launchme-xbox";
        const auto fixture = root / L"library-fixture.json";
        CHECK(write(fixture, library()));
        CHECK(script(fake,
            "[ \"$1\" = --profile ] || exit 9\nprofile=\"$2\"; command=\"$3\"; shift 3; out=\n"
            "while [ $# -gt 1 ]; do [ \"$1\" = --out ] && out=\"$2\"; shift 2; done\n"
            "echo '{\"progress\":{\"stage\":\"x\",\"done\":0,\"total\":0}}' >&2\n"
            "case \"$command\" in\n"
            "  login) echo '{\"ok\":true,\"signed_in\":true,\"username\":\"pat@example.com\",\"puid\":\"0123ABCD\"}' ;;\n"
            "  library) cp \"" + fixture.string() + "\" \"$out\" && echo '{\"ok\":true,\"games\":2}' ;;\n"
            "  logout) echo '{\"ok\":true,\"signed_in\":false}' ;;\n"
            "  *) echo '{\"ok\":false,\"error\":\"usage\"}'; exit 2 ;;\n"
            "esac\n"));
        const auto native_config = root / L"native";
        setenv("LAUNCHME_XBOX_HELPER", fake.c_str(), 1);
        CHECK(helperPath(native_config) == fake && helperInstalled(native_config));
        CHECK(nativeOperations().prepare(native_config, true, [] { return false; }, {}) == fake);
        const auto status = nativeOperations().invoke(fake, native_config, {L"status"}, [] { return false; }, {});
        CHECK(status.started && !status.ok && parseResult(status.output) && parseResult(status.output)->error == "usage");
        struct stat mode{};
        CHECK(stat(profileDirectory(native_config).c_str(), &mode) == 0 && (mode.st_mode & 0777) == 0700);
        {
            auto service = makeService(native_config);
            std::optional<Snapshot> snapshot;
            service->submit(Command::connect);
            CHECK(wait([&] { if (auto s = service->take()) snapshot = std::move(s); return snapshot && snapshot->state == State::connected; }));
            CHECK(snapshot->helper && snapshot->account == "0123ABCD" && snapshot->games == 2);
            service->submit(Command::logout);
            CHECK(wait([&] { if (auto s = service->take()) snapshot = std::move(s); return snapshot && snapshot->account.empty() && !service->busy(); }));
        }
        setenv("LAUNCHME_XBOX_HELPER", (root / L"bin" / L"missing").c_str(), 1);
        CHECK(!helperInstalled(native_config) && nativeOperations().prepare(native_config, true, [] { return false; }, {}).empty());
        unsetenv("LAUNCHME_XBOX_HELPER");
    }
#endif
    // Status texts for every state exist (no empty status in Ustawienia).
    for (const auto state : {State::disconnected, State::preparing, State::signing_in, State::syncing, State::connected, State::error}) {
        Snapshot s; s.state = state; CHECK(!statusText(s).empty());
        s.failure = Failure::expired; CHECK(!statusText(s).empty());
    }
    std::cout << "Xbox account contracts passed\n";
    return 0;
}
