// Protocol and filesystem contracts, using this executable as the fake child. No Steam session,
// network or real Aurelia executable is ever used by this test.
#include "../src/store/aurelia.hpp"
#include "../src/store/aurelia_stream.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/updater.hpp"
#include "../src/platform/text_encoding.hpp"
#include <Windows.h>
#include <algorithm>
#include <iostream>
#include <thread>
#include <atomic>
#ifndef _WIN32
#include <csignal>
#include <cstring>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#define CHECK(x) do { if (!(x)) { std::cerr << "Aurelia check " << __LINE__ << ": " << #x << '\n'; return __LINE__; } } while (false)
using C = aurelia::Command;
json::Value parsed(std::string_view text) { const auto v = json::parse(text); if (!v) throw std::runtime_error("fixture"); return *v; }

int main(int argc, char** argv) {
    // A stand-in for Aurelia's daemon: it serves the endpoint until it is ended.
    if (argc > 2 && std::string_view(argv[1]) == "--fixture-daemon") {
#ifdef _WIN32
        const auto name = text_encoding::wideLenient(argv[2]);
        const auto make = [&] { return CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE, PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, nullptr); };
        HANDLE current = make(); if (current == INVALID_HANDLE_VALUE) return 4;
        // As a real daemon: the next instance waits while a client is served; this one ends when the client does.
        while (true) {
            ConnectNamedPipe(current, nullptr);
            const HANDLE next = make();
            char buffer[64]; DWORD received = 0;
            while (ReadFile(current, buffer, sizeof(buffer), &received, nullptr) && received) {}
            DisconnectNamedPipe(current); CloseHandle(current); current = next;
        }
#else
        const std::string path = argv[2];
        const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un address{}; address.sun_family = AF_UNIX;
        if (fd < 0 || path.size() >= sizeof(address.sun_path)) return 4;
        memcpy(address.sun_path, path.c_str(), path.size() + 1);
        unlink(path.c_str());
        if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(fd, 8) != 0) return 4;
        while (true) { const int client = accept(fd, nullptr, nullptr); if (client >= 0) close(client); }
#endif
    }
    if (argc > 1 && std::string_view(argv[1]) == "--json") {
        const std::string command = argc > 2 ? argv[2] : "";
        if (command == "login") {
            wchar_t profile[4096]{}; if (!GetEnvironmentVariableW(L"AURELIA_CONFIG_DIR", profile, 4096)) return 2;
            wchar_t endpoint[4096]{};
            if (!GetEnvironmentVariableW(L"AURELIA_DAEMON_SOCKET", endpoint, 4096) || aurelia::daemonEndpoint(profile) != endpoint) return 3;
            std::cout << R"({"logged_in":true,"steam_id":76561198000000001,"account":"fixture"})"; return 0;
        }
        if (command == "available") { std::cerr << "log before result\n\x1b[32m{\n  \"available\": true\n}\x1b[0m\n"; return 0; }
        if (command == "info" && argc > 3 && std::string_view(argv[3]) == "903") { for (int i = 0; i < 18000; ++i) std::cout << std::string(1024, 'x'); return 0; }
        if (command == "info") { Sleep(15000); return 0; }
        if (command == "chat") {
            std::cout << R"({"event":"ready","with":76561198000000001})" << std::endl;
            while (true) Sleep(50);
        }
        if (command == "verify") { std::cerr << R"({"event":"progress","state":"verifying","bytes_downloaded":1,"total_bytes":4})" << std::endl;
            std::cout << R"({"app_id":902,"status":"verified"})"; return 0; }
        std::cout << R"({"error":"fixture failure"})"; return 7;
    }
    i18n::setLanguage(i18n::Language::en);
    const auto big = parsed("18446744073709551615"), precision = parsed("8593343465227540543");
    CHECK(aurelia::integer(&big) == UINT64_MAX && aurelia::integer(&precision) == 8593343465227540543ULL);
    for (const auto invalid : {"-1", "1.0", "1e3", "18446744073709551616", "null", "true"}) { const auto n = parsed(invalid); CHECK(!aurelia::integer(&n)); }
    aurelia::Request request{C::downgrade, 902, 903, 8593343465227540543ULL};
    CHECK(aurelia::arguments(request) == std::vector<std::wstring>({L"--json", L"downgrade", L"902", L"--depot", L"903", L"--manifest", L"8593343465227540543"}));
    request = {C::chat_send}; request.id = 76561198000000001ULL; request.text = L"--help & \"hello\" \\";
    CHECK(aurelia::arguments(request).back() == request.text && aurelia::arguments(request)[4] == L"--");
    request.text.assign(4001, L'a'); CHECK(aurelia::arguments(request).empty());
    request = {C::play, 902}; CHECK(aurelia::arguments(request).back() == L"--steam");
#ifdef _WIN32
    request.steam = false; CHECK(aurelia::arguments(request)[6] == L"none" && aurelia::arguments(request)[5] == L"--steam-mode");
#else
    // Linux: never `--windows` (no Proton); the native build or the Windows one through Aurelia's Proton.
    request.steam = false; CHECK(aurelia::arguments(request)[5] == L"none" && aurelia::arguments(request)[4] == L"--steam-mode");
    {
        const auto standalone = aurelia::arguments(request);
        CHECK(std::find(standalone.begin(), standalone.end(), L"--windows") == standalone.end());
    }
    request.text = L"linux";
    CHECK(aurelia::arguments(request) == std::vector<std::wstring>({L"--json", L"play", L"902", L"--no-update", L"--platform", L"linux"}));
    request.text = L"macos"; CHECK(aurelia::arguments(request).empty());
    request = {C::install, 902}; CHECK(aurelia::arguments(request) == std::vector<std::wstring>({L"--json", L"install", L"902"}));
    request.text = L"linux"; request.folder = L"/games/steam";
    CHECK(aurelia::arguments(request) == std::vector<std::wstring>({L"--json", L"install", L"902", L"--platform", L"linux", L"--library", L"/games/steam"}));
#endif
    // Steam's collections, read only (pull, list, show <id>; never push or sync); an id is only letters,
    // digits, '-' and '_'.
    request = {C::collections_pull}; CHECK(aurelia::arguments(request) == std::vector<std::wstring>({L"--json", L"collections", L"pull"}));
    request = {C::collections_list}; CHECK(aurelia::arguments(request) == std::vector<std::wstring>({L"--json", L"collections", L"list"}));
    request = {C::collections_show}; request.text = L"uc-1a2b"; CHECK(aurelia::arguments(request) == std::vector<std::wstring>({L"--json", L"collections", L"show", L"uc-1a2b"}));
    request.text = L"--force"; CHECK(aurelia::arguments(request).empty());
    request.text = L"a b"; CHECK(aurelia::arguments(request).empty());
    request.text.clear(); CHECK(aurelia::arguments(request).empty());
    {
        const auto list = aurelia::collectionList(*json::parse(R"({"namespace_version":3,"collections":[{"id":"favorite","name":"","dynamic":false},{"id":"uc-x","name":"RPG","dynamic":false},{"id":"uc-d","name":"Dyn","dynamic":true}]})"));
        CHECK(list && list->size() == 2 && (*list)[0].id == "user-collections.favorite" && (*list)[1].id == "user-collections.uc-x" && (*list)[1].name == L"RPG");
        CHECK(!aurelia::collectionList(*json::parse(R"({"collections":[{"id":"../x","name":"Bad"}]})")));
        CHECK(!aurelia::collectionList(*json::parse(R"([])")));
        const auto members = aurelia::collectionMembers(*json::parse(R"({"id":"uc-x","app_ids":[620,902]})"));
        CHECK(members && *members == std::vector<uint32_t>({620, 902}));
        CHECK(!aurelia::collectionMembers(*json::parse(R"({"app_ids":[0]})")) && !aurelia::collectionMembers(*json::parse(R"({"app_ids":"620"})")));
        // Aurelia is the library when it is on and replaces Steam, or no Steam client is signed in here.
        CHECK(aurelia::librarySource(true, false, false) && aurelia::librarySource(true, true, true) && !aurelia::librarySource(true, false, true) &&
              !aurelia::librarySource(false, true, false));
    }
    request = {C::presence}; request.text = L"bogus"; CHECK(aurelia::arguments(request).empty());
    request = {C::move, 902}; request.folder = L"relative"; CHECK(aurelia::arguments(request).empty());
    CHECK(aurelia::arguments({C::uninstall, 0}).empty());
    request = {C::cloud_sync, 902}; request.direction = aurelia::Request::Direction::down;
    CHECK(aurelia::arguments(request).back() == L"--down");
    // The in-app sign-in: the account on the command line, the password never (the child's environment).
    CHECK(aurelia::arguments({C::login_qr}) == std::vector<std::wstring>({L"--json", L"login", L"--qr", L"--timeout", L"300"}));
    request = {C::login_password}; request.text = L"fixture"; request.secret = L"hunter2";
    {
        const auto line = aurelia::arguments(request);
        CHECK(line == std::vector<std::wstring>({L"--json", L"login", L"-u", L"fixture"}));
        CHECK(std::find(line.begin(), line.end(), request.secret) == line.end());
    }
    for (const auto* invalid : {L"", L"-h", L"--qr", L"two words", L"tab\tname"}) { request.text = invalid; CHECK(aurelia::arguments(request).empty()); }
    request.text.assign(65, L'a'); CHECK(aurelia::arguments(request).empty());
    request.text = L"fixture"; request.secret.clear(); CHECK(aurelia::arguments(request).empty());
    // Steam Family Sharing: a borrowed game is kept and marked; one neither owned nor shared is not.
    {
        const auto shared = aurelia::library(parsed(R"([{"app_id":902,"name":"Owned","is_installed":true,"is_owned":true},{"app_id":903,"name":"Borrowed","is_installed":false,"is_owned":false,"is_family_shared":true},{"app_id":904,"name":"Neither","is_installed":true,"is_owned":false}])"), 76561198000000001ULL);
        CHECK(shared && shared->size() == 2 && !shared->front().family_shared && shared->back().app_id == 903 && shared->back().family_shared);
    }
    std::vector<json::Value> frames;
    aurelia::JsonStream stream;
    const std::string chunks = "log [debug]\n\x1b[32m{\n\"id\":8593343465227540543,\"text\":\"braces {} [ ] and \\\"quote\\\"\"}\x1b[0m\n[1,2]";
    for (char c : chunks) CHECK(stream.feed(std::string_view(&c, 1), [&](const auto& item) { frames.push_back(item); }));
    CHECK(frames.size() == 2 && aurelia::integer(frames[0].get("id")) == 8593343465227540543ULL && !stream.incomplete());
    aurelia::JsonStream small(8); CHECK(!small.feed("{\"123456789\":0}", {}));
    aurelia::JsonStream incomplete; CHECK(incomplete.feed("{\"id\":", {}) && incomplete.incomplete());
    const auto games = aurelia::library(parsed(R"([{"app_id":902,"name":"Fixture","is_installed":true,"is_owned":true,"install_path":"D:\\Library\\Fixture","playtime_forever_minutes":120}])"), 76561198000000001ULL);
    CHECK(games && games->size() == 1 && games->front().store_playtime_minutes == 120 && games->front().account_id == "76561198000000001");
    CHECK(!aurelia::library(parsed(R"([{"app_id":902,"name":"Broken"}])"), 1));
    CHECK(!aurelia::library(parsed("{}"), 1));
    CHECK(!aurelia::library(parsed("[]"), 0));
    const auto friend_list = aurelia::friends(parsed(R"([{"steam_id":76561198000000001,"persona_name":"Friend","persona_state":1,"game_name":"Fixture"}])"));
    CHECK(friend_list.friends.size() == 1 && friend_list.friends[0].id == 76561198000000001ULL && friend_list.friends[0].status == 1);
    CHECK(!friend_list.no_presence && friend_list.error.empty());
    // Bare IDs (the daemon's persona_state is null until Steam answers) are not a roster of offline friends.
    const auto bare = aurelia::friends(parsed(R"([{"steam_id":76561198000000001,"persona_name":null,"persona_state":null},{"steam_id":76561198000000002}])"));
    CHECK(bare.no_presence && !bare.error.empty() && bare.friends.size() == 2);
    CHECK(!aurelia::friends(parsed(R"([{"steam_id":76561198000000001,"persona_name":"Luna","persona_state":null},{"steam_id":76561198000000002}])")).no_presence); // One name is enough.
    CHECK(!aurelia::friends(parsed(R"([{"steam_id":76561198000000001,"persona_state":0}])")).no_presence); // A known "offline".
    CHECK(!aurelia::friends(parsed("[]")).no_presence);
    auto cloud = aurelia::cloud({true, false, parsed(R"({"files":[{"filename":"save.dat"}]})")}, false);
    CHECK(cloud.state == aurelia::CloudState::files); // A remote file listing never claims synchronization.
    cloud = aurelia::cloud({true, false, parsed(R"({"status":"ok","failed":[{"filename":"save.dat"}]})")}, true);
    CHECK(cloud.state == aurelia::CloudState::failed); // Exit 0 is not a successful restore.
    cloud = aurelia::cloud({true, false, parsed(R"({"status":"conflicts","conflicts":[{}]})")}, true);
    CHECK(cloud.state == aurelia::CloudState::conflicts && cloud.conflicts == 1);
    CHECK(aurelia::cloud({true, false, parsed(R"({"status":"incomplete","skipped":[{}]})")}, true).state == aurelia::CloudState::incomplete);
    CHECK(aurelia::cloud({true, false, parsed(R"({"status":"ok","conflicts":[],"failed":[],"skipped":[]})")}, true).state == aurelia::CloudState::synced);
    CHECK(aurelia::cloud({true, false, parsed(R"({"status":"ok","conflicts":"broken"})")}, true).state == aurelia::CloudState::failed);
    CHECK(!aurelia::checked({C::install, 902}, {true, false, parsed(R"({"app_id":902,"status":"cancelled"})")}).ok);
    CHECK(!aurelia::checked({C::verify, 902}, {true, false, parsed(R"({"app_id":903,"status":"verified"})")}).ok);
    const auto dlc = aurelia::dlcs(parsed(R"({"dlc":[{"app_id":903,"name":"DLC","owned":null,"installed":false,"disabled":false}]})"));
    CHECK(dlc && !dlc->front().owned.has_value());
    const auto progress = aurelia::downloads(parsed(R"([{"app_id":902,"downloaded_bytes":0,"total_bytes":9000000000,"is_downloading":true}])"));
    CHECK(progress && progress->front().progress.percent() == 0 && progress->front().progress.total == 9000000000ULL);
    CHECK(!aurelia::downloads(parsed("{}")));
    // The install backlog: paused, stopped and interrupted entries are not running downloads.
    const auto backlog = aurelia::downloads(parsed(R"([{"app_id":1,"state":"running","is_downloading":true},{"app_id":2,"state":"paused","downloaded_bytes":5,"total_bytes":10},{"app_id":3,"state":"aborted"},{"app_id":4,"state":"queued"}])"));
    CHECK(backlog && backlog->size() == 4 && !(*backlog)[0].backlog && (*backlog)[1].backlog && (*backlog)[2].backlog && (*backlog)[3].backlog);
    CHECK((*backlog)[0].progress.phase == InstallPhase::downloading && (*backlog)[1].progress.phase == InstallPhase::paused &&
          !(*backlog)[1].progress.live && (*backlog)[3].progress.phase == InstallPhase::queued);
    CHECK(aurelia::arguments({C::install_pause, 902}) == std::vector<std::wstring>({L"--json", L"install", L"pause", L"902"}));
    CHECK(aurelia::arguments({C::install_resume, 902}) == std::vector<std::wstring>({L"--json", L"install", L"resume", L"902"}));
    CHECK(aurelia::arguments({C::install_forget, 902}) == std::vector<std::wstring>({L"--json", L"install", L"forget", L"902"}));
    { aurelia::Request forget{C::install_forget, 902}; forget.delete_files = true; CHECK(aurelia::arguments(forget).back() == L"--delete-files"); }
    CHECK(aurelia::arguments({C::install_pause}).empty()); // Needs a game.
    CHECK(aurelia::arguments({C::duplicates}) == std::vector<std::wstring>({L"--json", L"duplicates"}));
    { aurelia::Request relink{C::relink, 902}; CHECK(aurelia::arguments(relink).empty()); // Relative or missing library: refused.
      relink.folder = L"D:\\SteamLibrary";
#ifdef _WIN32
      CHECK(aurelia::arguments(relink) == std::vector<std::wstring>({L"--json", L"relink", L"902", L"D:\\SteamLibrary"}));
      relink.command = C::import_files; CHECK(aurelia::arguments(relink)[1] == L"import");
#endif
    }
    CHECK(aurelia::checked({C::install_resume, 902}, {true, false, parsed(R"({"app_id":902,"status":"installed"})")}).ok);
    CHECK(!aurelia::checked({C::relink, 902}, {true, false, parsed(R"({"app_id":902,"status":"imported"})")}).ok);
    const auto twice = aurelia::duplicates(parsed(R"({"duplicates":[{"app_id":620,"name":"Portal 2","keep":{"install_path":"C:/a"},"remove":[{"install_path":"D:/b","size_bytes":100},{"install_path":"E:/c","size_bytes":50}]},{"app_id":7,"remove":[]}]})"));
    CHECK(twice && twice->size() == 1 && twice->front().app == 620 && twice->front().stale.size() == 2 && twice->front().reclaimable == 150);
    CHECK(!aurelia::duplicates(parsed("[]")));
    const auto history = aurelia::chatHistory(parsed(R"([{"timestamp":20,"from_self":true,"message":"second"},{"timestamp":10,"from_self":false,"message":"first"}])"));
    CHECK(history && history->front().text == L"first");
    CHECK(!aurelia::chatHistory(parsed(R"([{"timestamp":20,"message":"bad"}])")));
    const auto achievements = aurelia::achievements(parsed(R"({"achievements":[{"achievement_key":"FIRST","name":"First","visible":true,"unlocked":true,"unlock_time":123,"rarity":3.4}]})"));
    CHECK(achievements.complete && achievements.total == 1 && achievements.unlocked == 1 && achievements.items[0].unlocked_at == 123);

    const std::string hash(64, 'a');
    const auto asset = text_encoding::utf8(aurelia::asset_name); // aurelia_windows_x86_64.exe, aurelia_linux_x86_64...
    const auto release_text = R"({"tag_name":"v0.1.40","assets":[{"name":")" + asset + R"(","browser_download_url":"https://github.com/Drackrath/Aurelia/releases/download/v0.1.40/)" + asset + R"(","size":8693760,"digest":"sha256:)" + hash + R"("}]})";
    const auto release = aurelia::parseRelease(release_text);
    CHECK(release && release->size == 8693760 && release->sha256 == hash);
    CHECK(!aurelia::parseRelease("{}") && !aurelia::parseRelease("broken"));
    auto wrong = release_text;
    for (size_t at; (at = wrong.find(asset)) != std::string::npos;) wrong.replace(at, asset.size(), "aurelia_macOS_arm64");
    CHECK(!aurelia::parseRelease(wrong)); // Another system's build is never installed.
    CHECK(aurelia::allowedDownload(release->url) && !aurelia::allowedDownload(L"https://github.com.evil/Drackrath/Aurelia/releases/download/a"));
    CHECK(!aurelia::allowedDownload(L"https://github.com/other/Aurelia/releases/download/a"));
    CHECK(!aurelia::allowedDownload(L"http://release-assets.githubusercontent.com/a"));

    const auto root = std::filesystem::temp_directory_path() / (L"launchme-aurelia-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code e; std::filesystem::remove_all(root, e); } } cleanup{root};
    const auto roster = parsed(R"([{"app_id":902,"name":"Owned","is_installed":true,"is_owned":true}])");
    CHECK(aurelia::saveLibraryCache(root / L"cache", 76561198000000001ULL, roster));
    CHECK(aurelia::loadLibraryCache(root / L"cache", 76561198000000001ULL));
    CHECK(!aurelia::loadLibraryCache(root / L"cache", 76561198000000002ULL));
    CHECK(!aurelia::saveLibraryCache(root / L"cache", 0, roster));
    std::vector<C> calls; bool shared = false, target = true, conflict = false, upload_failure = false, native = false;
    std::wstring played; // The build `play` was asked for (Linux).
    aurelia::Transport workflow = [&](const aurelia::Request& r, const aurelia::Cancel&, const aurelia::Event&) {
        calls.push_back(r.command);
        auto reply = [&](std::string_view value) { return aurelia::Reply{true, false, parsed(value)}; };
        if (r.command == C::library) return shared ? reply(R"([{"app_id":902,"name":"Borrowed","is_owned":false,"is_family_shared":true}])") : aurelia::Reply{true, false, roster};
        if (r.command == C::launch_targets && native)
            return reply(R"({"targets":[{"id":"linux","platform":"linux","steam_mode":null,"available":true},{"platform":"windows","steam_mode":"none","available":true}]})");
        if (r.command == C::launch_targets) return target ? reply(R"({"targets":[{"platform":"windows","steam_mode":"none","available":true}]})") : reply(R"({"targets":[{"platform":"windows","steam_mode":"host","available":true}]})");
        if (r.command == C::play) { played = r.text; return reply(R"({"app_id":902,"status":"finished"})"); }
        if (r.command == C::cloud_sync) {
            if (conflict && r.direction == aurelia::Request::Direction::down) return reply(R"({"status":"conflicts","conflicts":[{"filename":"save.dat"}]})");
            if (upload_failure && r.direction == aurelia::Request::Direction::up) return reply(R"({"status":"failed","failed":[{"filename":"save.dat"}]})");
            return reply(R"({"status":"ok","failed":[],"skipped":[],"conflicts":[]})");
        }
        return aurelia::Reply{};
    };
    request = {C::standalone, 902}; request.auto_cloud = true;
    CHECK(aurelia::launchStandalone(workflow, request, [] { return false; }).ok);
    CHECK(calls == std::vector<C>({C::library, C::launch_targets, C::cloud_sync, C::play, C::cloud_sync}));
    shared = true; calls.clear(); CHECK(!aurelia::launchStandalone(workflow, request, [] { return false; }).ok && calls.size() == 1);
    shared = false; target = false; calls.clear(); CHECK(!aurelia::launchStandalone(workflow, request, [] { return false; }).ok && calls.size() == 2);
    target = true; conflict = true; calls.clear();
    auto blocked = aurelia::launchStandalone(workflow, request, [] { return false; });
    CHECK(!blocked.ok && !aurelia::flag(blocked.data, "launch_finished") && calls.size() == 3);
    conflict = false; upload_failure = true; calls.clear();
    auto failed_upload = aurelia::launchStandalone(workflow, request, [] { return false; });
    CHECK(!failed_upload.ok && aurelia::flag(failed_upload.data, "launch_finished") && calls.size() == 5);
#ifndef _WIN32
    // Linux: the native build when it is installed, else the Windows build through Aurelia's Proton.
    upload_failure = false; calls.clear();
    CHECK(aurelia::launchStandalone(workflow, request, [] { return false; }).ok && played == L"windows");
    native = true; target = false;
    CHECK(aurelia::launchStandalone(workflow, request, [] { return false; }).ok && played == L"linux");
    native = false; target = true;
#endif
#ifdef _WIN32
    // Minimal PE header: validates architecture, never executed.
    std::string image(4096, '\0'); IMAGE_DOS_HEADER dos{}; dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 128;
    memcpy(image.data(), &dos, sizeof(dos)); const DWORD sig = IMAGE_NT_SIGNATURE; memcpy(image.data() + 128, &sig, 4);
    IMAGE_FILE_HEADER header{}; header.Machine = IMAGE_FILE_MACHINE_AMD64; memcpy(image.data() + 132, &header, sizeof(header));
#else
    // Minimal ELF64 header of an x86-64 (or AArch64) executable: validated, never executed.
    std::string image(4096, '\0');
    memcpy(image.data(), "\x7f" "ELF\x02\x01\x01", 7);
    image[16] = 2; // ET_EXEC
#if defined(__aarch64__)
    image[18] = static_cast<char>(183);
#else
    image[18] = 62;
#endif
    image[52] = 64; // e_ehsize
    {
        auto pe = image; pe[0] = 'M'; pe[1] = 'Z'; // Not an ELF: refused like a PE on Linux.
        CHECK(atomic_file::write(root / L"pe.part", pe));
        aurelia::Release foreign{L"v0.9.0", {}, pe.size(), updater::sha256File(root / L"pe.part")};
        CHECK(!aurelia::publish(root / L"helper", foreign, root / L"pe.part"));
    }
#endif
    const auto partial = root / L"fixture.part"; CHECK(atomic_file::write(partial, image));
    aurelia::Release package{L"v1.0.0", {}, image.size(), updater::sha256File(partial)};
    CHECK(aurelia::publish(root / L"helper", package, partial));
    CHECK(aurelia::installed(root / L"helper").version == L"v1.0.0");
#ifndef _WIN32
    {
        const auto exe = aurelia::installed(root / L"helper").executable;
        CHECK(exe == root / L"helper" / L"v1.0.0" / L"aurelia" && access(exe.c_str(), X_OK) == 0);
    }
#endif
    CHECK(atomic_file::write(partial, "bad executable")); package.version = L"v1.0.1";
    CHECK(!aurelia::publish(root / L"helper", package, partial));
    CHECK(aurelia::installed(root / L"helper").version == L"v1.0.0"); // Failed update retains the previous install.
    CHECK(atomic_file::write(root / L"helper" / L"current.txt", "../escape")); CHECK(aurelia::installed(root / L"helper").executable.empty());

    wchar_t self[4096]{}; CHECK(GetModuleFileNameW(nullptr, self, 4096));
    aurelia::Runtime runtime{self, root / L"profile"};
    const auto pipe = aurelia::daemonEndpoint(runtime.profile);
#ifdef _WIN32
    CHECK(pipe.starts_with(L"\\\\.\\pipe\\launchme-aurelia-") && pipe != aurelia::daemonEndpoint(root / L"other-profile"));
#else
    // Linux: a Unix socket in the runtime folder, named after the profile and the user.
    CHECK(std::filesystem::path(pipe).filename().wstring().starts_with(L"launchme-aurelia-") && pipe.ends_with(L".sock") &&
          pipe != aurelia::daemonEndpoint(root / L"other-profile") && pipe.size() < 108);
#endif
    CHECK(pipe == aurelia::daemonEndpoint(root / L"." / L"profile"));
    // An existing endpoint without its identity marker must never invoke the CLI's broad stale-daemon fallback.
    const auto marker = aurelia::daemonMarker(pipe);
    const auto healthy = [&](const aurelia::Runtime& r) { return aurelia::invoke(r, {C::health}, [] { return false; }).ok; };
    // Two installed versions of the helper (this executable as both), for a daemon left by the older one.
    const auto versions = root / L"helper-versions";
    std::error_code e;
    std::filesystem::create_directories(versions / L"v1", e); std::filesystem::create_directories(versions / L"v2", e);
    const std::filesystem::path older = versions / L"v1" / std::filesystem::path(self).filename(), newer = versions / L"v2" / std::filesystem::path(self).filename();
    CHECK(std::filesystem::copy_file(self, older, std::filesystem::copy_options::overwrite_existing, e) && !e);
    CHECK(std::filesystem::copy_file(self, newer, std::filesystem::copy_options::overwrite_existing, e) && !e);
#ifndef _WIN32
    std::filesystem::permissions(older, std::filesystem::perms::owner_all, e); std::filesystem::permissions(newer, std::filesystem::perms::owner_all, e);
#endif
    const aurelia::Runtime updated{newer, runtime.profile};
#ifdef _WIN32
    const HANDLE server = CreateNamedPipeW(pipe.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE, 1, 4096, 4096, 0, nullptr);
    CHECK(server != INVALID_HANDLE_VALUE);
    OVERLAPPED pending{}; pending.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ConnectNamedPipe(server, &pending);
    CHECK(!healthy(runtime)); // Something that is not Launchme's Aurelia serves it: refused, never ended.
    CancelIoEx(server, &pending); WaitForSingleObject(pending.hEvent, 1000); CloseHandle(server); CloseHandle(pending.hEvent);
    // A stand-in daemon, ended with its handle whatever a check says (it never outlives the test).
    struct Fixture {
        HANDLE process = nullptr; DWORD pid = 0;
        Fixture() = default;
        Fixture(const Fixture&) = delete;
        Fixture& operator=(const Fixture&) = delete;
        ~Fixture() { if (process) { TerminateProcess(process, 1); WaitForSingleObject(process, 3000); CloseHandle(process); } }
    };
    const auto spawn = [&](Fixture& daemon, const std::filesystem::path& exe) {
        std::wstring line = L"\"" + exe.wstring() + L"\" --fixture-daemon " + pipe;
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
        if (!CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
        CloseHandle(process.hThread); daemon.process = process.hProcess; daemon.pid = process.dwProcessId;
        for (const auto until = GetTickCount64() + 5000; GetTickCount64() < until;) { if (WaitNamedPipeW(pipe.c_str(), 50)) return true; Sleep(20); }
        return false;
    };
    const auto pidMarker = [&](DWORD pid) { return atomic_file::write(marker, "{\"version\":\"0.1.41\",\"pid\":" + std::to_string(pid) + "}"); };
    {
        // Launchme's own daemon with its marker: used as it is.
        Fixture daemon;
        CHECK(spawn(daemon, self) && pidMarker(daemon.pid));
        CHECK(healthy(runtime) && WaitForSingleObject(daemon.process, 0) == WAIT_TIMEOUT);
        // Without its marker (cleaned, or removed by a newer CLI that could not stop it): ended; the command goes on.
        std::filesystem::remove(marker, e);
        CHECK(healthy(runtime) && WaitForSingleObject(daemon.process, 5000) == WAIT_OBJECT_0);
    }
    {
        // An older version's daemon after an update, its marker still there: ended too.
        Fixture daemon;
        CHECK(spawn(daemon, older) && pidMarker(daemon.pid));
        CHECK(healthy(updated) && WaitForSingleObject(daemon.process, 5000) == WAIT_OBJECT_0 && !std::filesystem::exists(marker));
    }
#else
    {
        const std::filesystem::path socket_path(pipe);
        const int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_un address{}; address.sun_family = AF_UNIX;
        memcpy(address.sun_path, socket_path.c_str(), socket_path.string().size() + 1);
        CHECK(server >= 0 && bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 && listen(server, 4) == 0);
        CHECK(!healthy(runtime)); // Something that is not Launchme's Aurelia listens: refused, never ended.
        // A marker naming another process is not trusted either.
        CHECK(atomic_file::write(marker, R"({"pid":1,"version":"0.1.41"})"));
        CHECK(!healthy(runtime));
        close(server);
        std::filesystem::remove(marker, e);
        // A socket nothing listens on any more is left for Aurelia to replace.
        CHECK(healthy(runtime));
        std::filesystem::remove(socket_path, e);
        const auto spawn = [&](const std::filesystem::path& exe) {
            const std::string program = exe.string(), socket_name = socket_path.string();
            char* arguments[] = {const_cast<char*>(program.c_str()), const_cast<char*>("--fixture-daemon"), const_cast<char*>(socket_name.c_str()), nullptr};
            pid_t pid = 0;
            if (posix_spawn(&pid, program.c_str(), nullptr, nullptr, arguments, environ) != 0) return pid_t{0};
            for (int wait = 0; wait < 250; ++wait) {
                const int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
                const bool up = connect(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
                close(probe);
                if (up) break;
                usleep(20000);
            }
            return pid;
        };
        // A stand-in daemon, ended whatever a check says (it never outlives the test).
        struct Fixture { pid_t pid = 0; ~Fixture() { if (pid > 0) { kill(pid, SIGKILL); waitpid(pid, nullptr, 0); } } };
        const auto ended = [](Fixture& daemon) {
            for (int wait = 0; wait < 250; ++wait) { int status = 0; if (waitpid(daemon.pid, &status, WNOHANG) == daemon.pid) { daemon.pid = 0; return true; } usleep(20000); }
            return false;
        };
        const auto pidMarker = [&](pid_t pid) { return atomic_file::write(marker, "{\"version\":\"0.1.41\",\"pid\":" + std::to_string(pid) + "}"); };
        {
            // Launchme's own daemon with its marker: used as it is.
            Fixture daemon{spawn(self)};
            CHECK(daemon.pid > 0 && pidMarker(daemon.pid));
            CHECK(healthy(runtime) && kill(daemon.pid, 0) == 0);
            // Without its marker (removed by a newer CLI that could not stop it): ended; the command goes on.
            std::filesystem::remove(marker, e);
            CHECK(healthy(runtime) && ended(daemon));
        }
        {
            // An older version's daemon after an update, its marker still there: ended too.
            Fixture daemon{spawn(older)};
            CHECK(daemon.pid > 0 && pidMarker(daemon.pid));
            CHECK(healthy(updated) && ended(daemon) && !std::filesystem::exists(marker));
        }
        std::filesystem::remove(socket_path, e);
        // Something else at the socket's path is never handed to Aurelia (it would delete it).
        CHECK(atomic_file::write(socket_path, "not a socket"));
        CHECK(!healthy(runtime));
        std::filesystem::remove(socket_path, e);
    }
#endif
    std::cout << "Aurelia native runner: query" << std::endl;
    const auto never = [] { return false; };
    auto reply = aurelia::invoke(runtime, {C::health}, never);
    CHECK(reply.ok && aurelia::integer(reply.data.get("steam_id")) == 76561198000000001ULL);
    reply = aurelia::invoke(runtime, {C::available, 902}, never); CHECK(reply.ok && aurelia::flag(reply.data, "available"));
    bool event = false; reply = aurelia::invoke(runtime, {C::verify, 902}, never, [&](const auto& v) { if (aurelia::text(v, "event") == "progress") event = true; });
    CHECK(reply.ok && event);
    reply = aurelia::invoke(runtime, {C::unpin, 902}, never); CHECK(!reply.ok && reply.error == L"fixture failure");
    const auto start = GetTickCount64(); reply = aurelia::invoke(runtime, {C::info, 902}, [start] { return GetTickCount64() - start > 200; });
    CHECK(reply.cancelled && GetTickCount64() - start < 5000);
    runtime.query_timeout_ms = 80;
    reply = aurelia::invoke(runtime, {C::info, 902}, never); CHECK(!reply.ok && !reply.cancelled);
    runtime.query_timeout_ms = 90000;
    std::cout << "Aurelia native runner: output limit" << std::endl;
    reply = aurelia::invoke(runtime, {C::info, 903}, never); CHECK(!reply.ok);
    std::cout << "Aurelia native runner: streaming cancellation" << std::endl;
    std::atomic_bool stop = false; request = {C::chat_live}; request.id = 76561198000000001ULL;
    const auto live_start = GetTickCount64();
    reply = aurelia::invoke(runtime, request, [&] { return stop.load() || GetTickCount64() - live_start > 3000; }, [&](const auto& v) { if (aurelia::text(v, "event") == "ready") stop = true; });
    CHECK(stop && reply.cancelled);
    std::cout << "Aurelia protocol, installer, process cancellation and state contracts passed\n";
    return 0;
}
