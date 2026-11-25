#include "../src/store/epic_account.hpp"
#include "../src/store/epic_legendary.hpp"
#include "../src/library/pc_platforms.hpp"
#include "../src/library/recent_games.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/protected_file.hpp"
#include "../src/platform/text_encoding.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <shellapi.h>

#define CHECK(value) do { if (!(value)) { std::cerr << "Epic check failed at line " << __LINE__ << ": " << #value << '\n'; return 1; } } while (false)
namespace {
const epic::Cancel never = [] { return false; };
const epic::Config config{"test-client", "test-secret"};
Game game(std::string id = "AppOne") {
    Game g; g.source = GameSource::epic; g.source_id = std::move(id); g.kind = GameKind::store; g.installed = false;
    g.name = L"Test Game"; g.epic_namespace = "sandbox"; g.epic_catalog = "catalog"; return g;
}
epic::Session session(std::string account = "account-A") {
    return {{"access-only-test", "refresh-only-test", std::move(account), "Player", epic::nowSeconds() + 7200}, {game()}, 1};
}
HttpResponse response(std::string text, uint32_t status = 200) { return {status, {text.begin(), text.end()}}; }
template<class Predicate> bool wait(Predicate predicate) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    do { if (predicate()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); } while (std::chrono::steady_clock::now() < until);
    return false;
}
struct ApiState { bool offline = false, malformed = false, cycle = false, unauthorized = false, rate = false; int pages = 0, refresh = 0, catalogs = 0; std::string refresh_account = "account-A"; };
epic::Http fixture(ApiState& state) {
    return [&](const std::wstring& url, const std::wstring& headers, const std::string& data, bool post) -> HttpResponse {
        if (state.offline) return {};
        if (url.ends_with(L"/token")) {
            if (!post || !headers.starts_with(L"Content-Type: application/x-www-form-urlencoded") || data.find("token_type=eg1") == data.npos) return {};
            ++state.refresh;
            return response("{\"access_token\":\"rotated-access\",\"refresh_token\":\"rotated-refresh\",\"account_id\":\"" + state.refresh_account + "\",\"displayName\":\"Test\",\"expires_in\":7200}");
        }
        if (post || !headers.starts_with(L"Authorization: Bearer ")) return {};
        if (url.ends_with(L"/verify")) {
            if (state.unauthorized) { state.unauthorized = false; return response("", 401); }
            return response(R"({"account_id":"account-A","client_id":"test-client"})");
        }
        if (url.find(L"/playtime/account/account-A/all") != url.npos)
            return response(R"([{"accountId":"account-A","artifactId":"AppOne","totalTime":7260},{"accountId":"account-A","artifactId":"../x","totalTime":60}])");
        if (url.find(L"library/api") != url.npos) {
            ++state.pages;
            if (state.rate) return {429, {}, 123};
            if (state.malformed && state.pages > 1) return response(R"({"records":false})");
            if (url.find(L"cursor=") == url.npos || state.cycle)
                return response(R"({"records":[{"appName":"AppOne","namespace":"sandbox","catalogItemId":"catalog","acquisitionDate":"2023-11-14T22:13:20.123Z"},{"appName":"UE_5.0","namespace":"ue"}],"responseMetadata":{"nextCursor":"next/one"}})");
            return response(R"({"records":[{"appName":"AppOne","namespace":"sandbox","catalogItemId":"catalog"},{"appName":"DLC","namespace":"sandbox","catalogItemId":"dlc"}],"responseMetadata":{}})");
        }
        if (url.find(L"id=dlc") != url.npos) return response(R"({"dlc":{"id":"dlc","title":"DLC","mainGameItem":{"id":"catalog"},"categories":[]}})");
        ++state.catalogs;
        return response(R"({"catalog":{"id":"catalog","title":"Owned Game","categories":[{"path":"games"}],"keyImages":[{"type":"Thumbnail","url":"https://cdn1.epicgames.com/thumb.jpg"},{"type":"DieselGameBoxTall","url":"https://cdn1.epicgames.com/tall.jpg"},{"type":"DieselGameBox","url":"https://cdn1.epicgames.com/wide.jpg"},{"type":"DieselGameBoxLogo","url":"https://cdn1.epicgames.com/logo.png"},{"type":"OfferImageWide","url":"http://insecure.test/x.jpg"}]}})");
    };
}
struct BackendState { std::atomic_bool block{false}, entered{false}; std::atomic_int installs{0}; };
class FakeBackend final : public epic::Backend {
    BackendState& state_;
public:
    explicit FakeBackend(BackendState& s) : state_(s) {}
    epic::Result exchange(const epic::Config&, const std::string& code, epic::Tokens& t, const epic::Cancel&) override { t = session(code == "BBBBBBBBBBBBBBBB" ? "account-B" : "account-A").tokens; return {}; }
    epic::Result verify(const epic::Config&, epic::Tokens& t, const epic::Cancel&) override { t.refresh = "new-refresh"; return {}; }
    epic::Result library(const epic::Config&, epic::Session& s, const epic::Cancel& cancel) override {
        state_.entered = true; while (state_.block && !cancel()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (cancel()) return {epic::Failure::cancelled}; s.games = {game()}; s.synced_at = epic::nowSeconds(); return {};
    }
};
class FakeRunner final : public epic::Runner {
    BackendState& state_;
public:
    explicit FakeRunner(BackendState& s) : state_(s) {}
    epic::Result install(const epic::Tokens&, const Game&, const gog::InstallRequest&, const epic::Cancel& cancel, const gog::InstallProgress& progress) override {
        ++state_.installs; state_.entered = true; progress(gog::InstallState::downloading, 2500, 10000);
        while (state_.block && !cancel()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return cancel() ? epic::Result{epic::Failure::cancelled} : epic::Result{};
    }
    epic::Result launch(const epic::Tokens&, const Game&, GameAction& action, const epic::Cancel&, bool) override { action = {L"fixture.exe"}; return {}; }
};
}
int main() {
    using namespace epic;
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-epic-tests-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); } } cleanup{root};
    CHECK(authorizationUrl(config).starts_with(L"https://www.epicgames.com/id/login?redirectUrl="));
    CHECK(!authorizationUrl({}).size());
    CHECK(authorizationCode(L" {\"authorizationCode\":\"ABCDEFGHIJKLMNOP\"} ") == "ABCDEFGHIJKLMNOP");
    CHECK(authorizationCode(L"\"ABCDEFGHIJKLMNOP\"") == "ABCDEFGHIJKLMNOP");
    CHECK(!authorizationCode(L"https://attacker.test/?code=ABCDEFGHIJKLMNOP"));
    CHECK(!authorizationCode(L"ABCDEFGHIJKLMNOP\nother"));
    CHECK(!identifier("../app") && !identifier("--argument") && !identifier("drive:app"));
    auto state = session(); ApiState api_state; auto api = makeBackend(fixture(api_state));
    CHECK(api->verify(config, state.tokens, never));
    api_state.unauthorized = true; CHECK(api->verify(config, state.tokens, never)); CHECK(api_state.refresh == 1 && state.tokens.refresh == "rotated-refresh");
    state.games.clear(); state.synced_at = 0;
    CHECK(api->library(config, state, never)); CHECK(state.games.size() == 1 && state.games[0].source_id == "AppOne" && api_state.pages == 2);
    CHECK(state.games[0].name == L"Owned Game");
    CHECK(state.games[0].added_at == 1700000000); // Duplicate records without a date cannot erase it.
    CHECK(state.games[0].store_playtime_minutes == 121); // Epic's own play time (library service), 7260 s.
    // Epic's own art (keyImages): the tall box art before the thumbnail, https only.
    CHECK(state.games[0].store_tall_art == "https://cdn1.epicgames.com/tall.jpg" && state.games[0].store_wide_art == "https://cdn1.epicgames.com/wide.jpg" &&
          state.games[0].store_logo_art == "https://cdn1.epicgames.com/logo.png");
    {   // A known game is not asked again; one saved before the art was read (0.2.4) is, once.
        const int before = api_state.catalogs; api_state.pages = 0;
        state.games[0].added_at = 0; // A pre-acquisition cache is enriched without refetching the catalog.
        CHECK(api->library(config, state, never) && api_state.catalogs == before);
        CHECK(state.games[0].added_at == 1700000000);
        state.games[0].store_tall_art.clear(); api_state.pages = 0;
        CHECK(api->library(config, state, never) && api_state.catalogs == before + 1 && state.games[0].store_tall_art == "https://cdn1.epicgames.com/tall.jpg");
    }
    {
        // An account call for a plugin (achievements): POST with the bearer, one refresh on 401.
        int posts = 0, renewals = 0;
        auto caller = makeBackend([&](const std::wstring& url, const std::wstring& headers, const std::string& data, bool post) -> HttpResponse {
            if (url.ends_with(L"/token")) {
                ++renewals;
                return response(R"({"access_token":"rotated-access","refresh_token":"rotated-refresh","account_id":"account-A","displayName":"Test","expires_in":7200})");
            }
            if (url == L"https://launcher.store.epicgames.com/graphql" && post && headers.starts_with(L"Authorization: Bearer ") &&
                headers.find(L"Content-Type: application/json") != std::wstring::npos && data == R"({"q":1})")
                return ++posts == 1 ? response("", 401) : response(R"({"data":{}})");
            return {};
        });
        auto tokens = session().tokens;
        HttpResponse out;
        CHECK(caller->call(config, {L"https://launcher.store.epicgames.com/graphql", R"({"q":1})", true}, tokens, never, out));
        CHECK(out.status == 200 && posts == 2 && renewals == 1 && tokens.access == "rotated-access");
        CHECK(caller->call(config, {L"http://launcher.store.epicgames.com/graphql"}, tokens, never, out).failure == Failure::invalid);
    }
    api_state.pages = 0; api_state.malformed = true;
    CHECK(!api->library(config, state, never)); CHECK(state.games.size() == 1 && state.games[0].name == L"Owned Game");
    api_state.malformed = false; api_state.cycle = true; api_state.pages = 0;
    CHECK(!api->library(config, state, never)); CHECK(api_state.pages == 2);
    api_state.cycle = false; api_state.rate = true;
    const auto rate = api->library(config, state, never); CHECK(rate.failure == Failure::rate_limit && rate.retry_after == 123);
    api_state.rate = false; api_state.refresh_account = "account-B"; state.tokens.expires = 1;
    CHECK(api->verify(config, state.tokens, never).failure == Failure::expired); CHECK(state.tokens.account == "account-A");
    auto vault = makeVault(root / L"epic.session"); CHECK(vault->save(state));
    std::optional<Session> restored; CHECK(vault->load(restored)); CHECK(restored && restored->games[0].source_id == "AppOne");
    CHECK(restored->games[0].store_playtime_minutes == 121);
    CHECK(restored->games[0].added_at == 1700000000);
    CHECK(restored->games[0].store_tall_art == state.games[0].store_tall_art && restored->games[0].store_logo_art == state.games[0].store_logo_art);
    std::ifstream encrypted(root / L"epic.session", std::ios::binary); const std::string ciphertext{std::istreambuf_iterator<char>{encrypted}, {}}; encrypted.close();
    CHECK(ciphertext.find(state.tokens.refresh) == ciphertext.npos && ciphertext.find("Owned Game") == ciphertext.npos);
    CHECK(vault->clear()); CHECK(vault->load(restored) && !restored);
    for (const int version : {1, 2, 3}) {
        const std::string legacy = "launchme-epic-v" + std::to_string(version) + "\n\"access-fixture\" \"refresh-fixture\" \"account-A\" \"Player\" 1800000000 1 1\n"
            "\"AppOne\" \"Old cached game\" \"sandbox\" \"catalog\" \"\" 0" + std::string(version >= 2 ? " 42" : "") +
            (version >= 3 ? " \"-\" \"\" \"\"" : "") + "\n";
        CHECK(protected_file::write(root / L"epic.session", legacy, L"Fixture"));
        CHECK(vault->load(restored) && restored && !restored->games[0].added_at && restored->games[0].name == L"Old cached game");
    }
    CHECK(atomic_file::write(root / L"epic.session", "corrupt")); CHECK(vault->load(restored).failure == Failure::storage);
    CHECK(vault->clear());
    CHECK(sessionJson(state.tokens, false).find(state.tokens.refresh) == std::string::npos);
    // Local installs survive logout; membership, hiding and history stay account-scoped.
    auto installed = game(); installed.installed = true; installed.epic_managed = true; installed.play_action = {L"fixture.exe"};
    auto online = game("AppTwo");
    auto merged = mergeEpicLibrary({}, {state.games[0], online}, "account-A", {installed});
    CHECK(merged.visible.size() == 2 && merged.visible[0].account_id.empty() && merged.visible[1].account_id == "account-A");
    CHECK(merged.visible[0].added_at == 1700000000);
    CHECK(mergeEpicLibrary({}, {}, {}, {installed}).visible.size() == 1);
    CHECK(recent_games::historyKey(online, 0, {}, "A") != recent_games::historyKey(online, 0, {}, "B"));
    CHECK(atomic_file::write(root / L"libraries" / L"epic.json", "broken"));
    CHECK(mergePcLibraries(merged, root / L"libraries").visible.empty());
    // Windows argument quoting round trip, spaces, quotes and trailing slash.
    const std::wstring arg = L"path with spaces\\\"quoted\\";
    int argc = 0; auto argv = CommandLineToArgvW((L"program " + quoteArgument(arg)).c_str(), &argc);
    CHECK(argv && argc == 2 && argv[1] == arg); LocalFree(argv);
    CHECK(downloadPercent("[DLManager] INFO: Progress: 12.34% (12/100)") == 1234u);
    CHECK(!downloadPercent("Progress: nan%") && !downloadPercent("Progress: 101%"));
    const auto folder = root / L"game"; CHECK(atomic_file::write(folder / L"game.exe", "fixture"));
    auto launch = json::parse(R"({"game_directory":"","game_executable":"game.exe","game_parameters":[],"user_parameters":[],"egl_parameters":["-AUTH_PASSWORD=one-use-token"],"launch_command":[],"environment":{}})");
    launch->properties["game_directory"].text = text_encoding::utf8(folder.native());
    CHECK(launchAction(*launch, folder));
    launch->properties["game_executable"].text = "../game.exe"; CHECK(!launchAction(*launch, folder));
    // Real installer orchestration with a fake subprocess. Exit 0 is insufficient;
    // only verified files can produce a persisted installation visible to scanning.
    bool verified = false, credentials_ok = false;
    const auto install_root = root / L"install-pipeline";
    const auto game_dir = install_root / L"game";
    const auto profile = install_root / L"epic" / L"legendary";
    LegendaryOperations ops;
    ops.prepare = [](const auto& root, const auto&) { return root / L"fake-helper.exe"; };
    ops.invoke = [&](const auto&, const auto& profile, const auto& args, const auto&, const auto& line) -> ProcessResult {
        std::ifstream file(profile / L"user.json"); const std::string auth{std::istreambuf_iterator<char>{file}, {}};
        credentials_ok = auth.find("refresh-only-test") == auth.npos && auth.find("access-only-test") != auth.npos;
        if (args.size() > 1 && args[1] == L"verify") { if (verified) line("[cli] INFO: Verification finished successfully."); return {true}; }
        atomic_file::write(game_dir / L"game.exe", "fixture");
        const auto directory = text_encoding::utf8(game_dir.generic_wstring());
        atomic_file::write(profile / L"installed.json", "{\"AppOne\":{\"app_name\":\"AppOne\",\"install_path\":\"" + directory +
            "\",\"title\":\"Installed fixture\",\"platform\":\"Windows\",\"executable\":\"game.exe\",\"version\":\"1\"}}");
        line("[DLManager] INFO: Progress: 50.00% (1/2)"); return {true};
    };
    auto native = makeRunner(install_root, std::move(ops)); gog::InstallRequest request; request.product = "AppOne"; request.install_directory = game_dir;
    const auto progress = [](auto, auto, auto) {};
    CHECK(!native->install(session().tokens, game(), request, never, progress));
    CHECK(credentials_ok && !std::filesystem::exists(profile / L"user.json") && installedGames(install_root).empty());
    verified = true; CHECK(native->install(session().tokens, game(), request, never, progress));
    CHECK(installedGames(install_root).size() == 1 && installedGames(install_root)[0].epic_managed);
    CHECK(!std::filesystem::exists(profile / L"user.json"));
    std::filesystem::remove(game_dir / L"game.exe"); CHECK(installedGames(install_root).empty());
    // Single worker: incomplete grants cannot be injected; logout cancels an install,
    // prevents stale completion, and cannot be dropped by the next Connect.
    BackendState worker;
    auto service = makeService(std::make_unique<FakeBackend>(worker), makeVault(root / L"epic.session"), std::make_unique<FakeRunner>(worker), [] { return config; }, std::chrono::milliseconds(150));
    service->submit(Command::complete, L"AAAAAAAAAAAAAAAA");
    CHECK(wait([&] { auto s = service->take(); return s && s->state == State::error; }));
    service->submit(Command::connect); CHECK(wait([&] { auto s = service->take(); return s && s->state == State::signing_in; }));
    CHECK(wait([&] { auto s = service->take(); return s && s->failure == Failure::expired; }));
    service->submit(Command::connect); CHECK(wait([&] { auto s = service->take(); return s && s->state == State::signing_in; }));
    service->submit(Command::complete, L"AAAAAAAAAAAAAAAA");
    CHECK(wait([&] { auto s = service->take(); return s && s->state == State::connected && s->account == "account-A"; }));
    CHECK(service->install(game("not-owned"), {}));
    CHECK(wait([&] { auto s = service->takeInstall(); return s && s->error == gog::InstallError::account; })); CHECK(worker.installs == 0);
    CHECK(wait([&] { return !service->busy(); }));
    worker.block = true; worker.entered = false; CHECK(service->install(game(), {}));
    CHECK(wait([&] { return worker.entered.load(); }));
    service->submit(Command::logout); service->submit(Command::connect);
    CHECK(wait([&] { auto s = service->take(); return s && s->state == State::signing_in && s->games.empty(); }));
    CHECK(vault->load(restored) && !restored);
    worker.block = false;
    service->submit(Command::complete, L"BBBBBBBBBBBBBBBB");
    CHECK(wait([&] { auto s = service->take(); return s && s->state == State::connected && s->account == "account-B"; }));
    CHECK(vault->load(restored) && restored && restored->tokens.account == "account-B");
    // A restore while an install runs cancels it (a new generation); busy ends with it, not
    // after the install's own end, which no longer matches the generation.
    while (service->takeInstall()) {}
    worker.block = true; worker.entered = false; CHECK(service->install(game(), {}));
    CHECK(wait([&] { return worker.entered.load(); }));
    service->submit(Command::restore);
    CHECK(!service->busy());
    CHECK(wait([&] { auto s = service->takeInstall(); return s && s->state == gog::InstallState::cancelled; }));
    worker.block = false;
    CHECK(wait([&] { auto s = service->take(); return s && s->state == State::connected; }));
    CHECK(service->launch(game())); CHECK(wait([&] { auto s = service->takeLaunch(); return s && s->action.target == L"fixture.exe"; }));
    service->submit(Command::logout); service.reset(); CHECK(vault->load(restored) && !restored);
    // Shutdown before the first wait, with queued work and just after publishing
    // its result; then interrupt both the timed login wait and an active backend.
    const auto create = [&] { return makeService(std::make_unique<FakeBackend>(worker), makeVault(root / L"epic.session"), std::make_unique<FakeRunner>(worker), [] { return config; }); };
    for (int i = 0; i < 128; ++i) {
        auto idle = create();
        if (i % 3) idle->submit(Command::restore);
        if (i % 3 == 2) CHECK(wait([&] { auto s = idle->take(); return s && s->state == State::disconnected; }));
    }
    auto signing_in = create(); signing_in->submit(Command::connect);
    CHECK(wait([&] { auto s = signing_in->take(); return s && s->state == State::signing_in; }));
    signing_in.reset();
    CHECK(vault->save(session())); worker.block = true; worker.entered = false;
    auto busy = create(); busy->submit(Command::restore);
    CHECK(wait([&] { return worker.entered.load(); }));
    busy.reset();
    std::cout << "Epic account, privacy, cancellation, library merge and launch contracts passed.\n";
    return 0;
}
