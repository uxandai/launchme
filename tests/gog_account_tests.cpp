#include "../src/store/gog_account.hpp"
#include "../src/library/pc_platforms.hpp"
#include "../src/platform/atomic_file.hpp"
#include "../src/platform/protected_file.hpp"
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
HttpResponse reply(std::string_view body, uint32_t status = 200, uint32_t retry = 0) {
    return {status, std::vector<uint8_t>(body.begin(), body.end()), retry};
}
const gog::Config config{"fixture-client", "fixture-secret", "https://example.test/callback?origin=client"};
const gog::Cancel running = [] { return false; };
Game game(std::string id, std::wstring title = L"Fixture") {
    Game result; result.source = GameSource::gog; result.kind = GameKind::store;
    result.source_id = std::move(id); result.name = std::move(title); result.installed = false; return result;
}
gog::Session session(std::string user = "9007199254740993") {
    return {{"fixture-access", "fixture-refresh", user, config.client_id, gog::nowSeconds() + 3600}, {game("123")}, 1};
}
void apiTests() {
    const auto url = gog::authorizationUrl(config, "nonce");
    check(url.starts_with(L"https://auth.gog.com/auth?") && url.find(L"fixture-secret") == std::wstring::npos, "public authorization URL");
    check(!gog::authorizationUrl({}, "nonce").size(), "missing configuration");
    const std::wstring callback = L"https://example.test/callback?origin=client&code=abc%2B123&state=nonce";
    check(gog::callbackCode(callback, config, "nonce") == "abc+123", "callback decode");
    check(!gog::callbackCode(callback, config, "other"), "state mismatch");
    check(!gog::callbackCode(callback + L"&code=duplicate", config, "nonce"), "duplicate code");
    check(!gog::callbackCode(L"https://example.test.evil/callback?origin=client&code=x&state=nonce", config, "nonce"), "callback origin");
    check(!gog::callbackCode(L"https://example.test/callback?code=x&state=nonce", config, "nonce"), "registered query required");
    check(!gog::callbackCode(callback + L"#fragment", config, "nonce"), "reject fragment");
    int calls = 0;
    auto api = gog::makeBackend([&](const auto& address, const auto&, size_t) {
        ++calls;
        check(address.find(L"grant_type=authorization_code") != std::wstring::npos && address.find(L"code=abc%2B123") != std::wstring::npos, "code exchange contract");
        return reply(R"({"access_token":"next-access","refresh_token":"next-refresh","user_id":9007199254740993,"expires_in":3600,"token_type":"bearer"})");
    });
    gog::Tokens tokens;
    check(static_cast<bool>(api->exchange(config, "abc+123", tokens, running)) && tokens.user == "9007199254740993" && calls == 1, "textual large user ID");
    int users = 0, refreshes = 0;
    api = gog::makeBackend([&](const auto& address, const auto& headers, size_t) {
        if (address.starts_with(L"https://auth.gog.com/token")) {
            ++refreshes;
            check(address.find(L"grant_type=refresh_token") != std::wstring::npos, "refresh grant");
            return reply(R"({"access_token":"rotated","refresh_token":"rotated-refresh","user_id":"9007199254740993","expires_in":3600,"token_type":"Bearer"})");
        }
        if (++users == 1) return reply("", 401);
        check(headers.find(L"Bearer rotated") != std::wstring::npos, "401 uses refreshed bearer");
        return reply(R"({"id":"9007199254740993","username":"not persisted"})");
    });
    check(static_cast<bool>(api->verify(tokens, config, running)) && users == 2 && refreshes == 1, "401 single refresh");
    auto owned = session();
    int pages = 0, metadata = 0;
    api = gog::makeBackend([&](const auto& address, const auto&, size_t) {
        if (address.find(L"/releases") != std::wstring::npos) {
            if (++pages == 1) return reply(R"({"items":[{"platform_id":"gog","external_id":"123","owned":true,"owned_since":1600000000,"date_created":1700000000},{"platform_id":"steam","external_id":"42","owned":true}],"next_page_token":"a+b"})");
            check(address.find(L"page_token=a%2Bb") != std::wstring::npos, "encoded pagination token");
            return reply(R"({"items":[{"platform_id":"gog","external_id":"123","owned":true,"owned_since":1650000000},{"platform_id":"gog","external_id":"9999999999999","owned":true,"owned_since":1700000000},{"platform_id":"gog","external_id":"456","owned":false}],"next_page_token":null})");
        }
        ++metadata;
        return reply(R"({"id":9999999999999,"title":"Other game","game_type":"game","is_secret":true})");
    });
    check(static_cast<bool>(api->library(owned, config, running)) && pages == 2 && metadata == 1 && owned.games.size() == 2, "paged deduplicated library and cached metadata");
    check(owned.games[1].source_id == "9999999999999" && !owned.games[1].app_id && !owned.games[1].installed && owned.games[1].visibility == GameVisibility::private_game, "GOG identity and privacy");
    check(owned.games[0].added_at == 1600000000 && owned.games[1].added_at == 1700000000, "ownership dates enrich cached and fresh titles, earliest duplicate wins");
    const auto saved_time = owned.synced_at;
    pages = 0;
    api = gog::makeBackend([&](const auto&, const auto&, size_t) {
        if (++pages == 1) return reply(R"({"items":[{"platform_id":"gog","external_id":"123","owned":true}],"next_page_token":"next"})");
        return reply("", 429, 90);
    });
    const auto limit = api->library(owned, config, running);
    check(limit.failure == gog::Failure::rate_limit && limit.retry_after == 90 && owned.games.size() == 2 && owned.synced_at == saved_time, "failed page does not commit partial membership");
    api = gog::makeBackend([](const auto&, const auto&, size_t) { return reply(R"({"items":[{"platform_id":"gog","external_id":"123","owned":true}],"next_page_token":"loop"})"); });
    check(api->library(owned, config, running).failure == gog::Failure::invalid, "pagination cycle bounded");
    int requests = 0;
    api = gog::makeBackend([&](const auto&, const auto&, size_t) { ++requests; return reply(R"({"items":[],"next_page_token":null})"); });
    check(api->library(owned, config, [&] { return requests != 0; }).failure == gog::Failure::cancelled && owned.games.size() == 2, "cancel page result");
    check(static_cast<bool>(api->library(owned, config, running)) && owned.games.empty(), "genuine empty library replaces snapshot");
    api = gog::makeBackend([](const auto& address, const auto&, size_t) {
        if (address.find(L"/releases") != address.npos) return reply(R"({"items":[{"platform_id":"gog","external_id":"123","owned":true,"owned_since":null,"date_created":1700000000},{"platform_id":"gog","external_id":"124","owned":true,"owned_since":9999999999999}]})");
        return reply(address.ends_with(L"/123") ? R"({"id":"123","title":"No date","game_type":"game","is_secret":false})" :
            R"({"id":"124","title":"Invalid date","game_type":"game","is_secret":false})");
    });
    check(static_cast<bool>(api->library(owned, config, running)) && owned.games.size() == 2 && !owned.games[0].added_at && !owned.games[1].added_at,
          "missing or invalid ownership dates never use Galaxy record creation");
    int fallbacks = 0;
    api = gog::makeBackend([&](const auto& address, const auto& headers, size_t) {
        if (address.find(L"/releases") != address.npos) return reply(R"({"items":[{"platform_id":"gog","external_id":"456","owned":true,"owned_since":1500000000,"certificate":"synthetic-cert"}]})");
        if (address.starts_with(L"https://api.gog.com/products/")) return reply("", 404);
        check(address == L"https://gamesdb.gog.com/platforms/gog/external_releases/456", "delisted release metadata fallback");
        check(headers.find(L"X-GOG-Library-Cert: synthetic-cert") != headers.npos, "account library certificate");
        ++fallbacks;
        return reply(R"({"external_id":"456","platform_id":"gog","type":"game","title":{"*":"Legacy fixture"},"game":{"visible_in_library":true}})");
    });
    check(static_cast<bool>(api->library(owned, config, running)) && owned.games.size() == 1 && fallbacks == 1, "one product 404 no longer rejects entire library");
    check(owned.games[0].added_at == 1500000000, "delisted game retains acquisition date");
    owned.games.clear();
    api = gog::makeBackend([](const auto& address, const auto&, size_t) {
        if (address.find(L"/releases") != address.npos) return reply(R"({"items":[{"platform_id":"gog","external_id":"456","owned":true}]})");
        if (address.starts_with(L"https://api.gog.com/products/")) return reply("", 404);
        return reply(R"({"external_id":"999","platform_id":"gog","type":"game","title":{"*":"Wrong"},"game":{"visible_in_library":true}})");
    });
    check(api->library(owned, config, running).failure == gog::Failure::invalid && owned.games.empty(), "fallback cannot mix product identity");
    // An account call for a plugin (achievements): the bearer added, a due token refreshed first,
    // a 401 refreshed once; never a POST or a non-https URL.
    int calls_made = 0, renewed = 0;
    api = gog::makeBackend([&](const auto& address, const auto& headers, size_t) {
        if (address.starts_with(L"https://auth.gog.com/token")) {
            ++renewed;
            return reply(R"({"access_token":"renewed","refresh_token":"renewed-refresh","user_id":"9007199254740993","expires_in":3600,"token_type":"Bearer"})");
        }
        ++calls_made;
        check(address == L"https://gameplay.gog.com/clients/1/users/9007199254740993/achievements" && headers.find(L"Bearer renewed") != std::wstring::npos, "account call bearer");
        return reply(R"({"items":[]})");
    });
    auto due = session().tokens; due.expires = gog::nowSeconds();
    HttpResponse answer;
    check(static_cast<bool>(api->call({L"https://gameplay.gog.com/clients/1/users/9007199254740993/achievements"}, due, config, running, answer)) &&
          answer.status == 200 && renewed == 1 && calls_made == 1 && due.access == "renewed", "account call refreshes a due token");
    check(api->call({L"https://gameplay.gog.com/x", "{}", true}, due, config, running, answer).failure == gog::Failure::invalid &&
          api->call({L"http://gameplay.gog.com/x"}, due, config, running, answer).failure == gog::Failure::invalid && calls_made == 1, "account call GET over https only");
}
struct Stored {
    std::mutex mutex;
    std::optional<gog::Session> value;
    std::atomic_int saves{0};
    std::atomic_bool refuse{false};
};
class FakeVault : public gog::Vault {
    std::shared_ptr<Stored> stored_;
public:
    explicit FakeVault(std::shared_ptr<Stored> stored) : stored_(std::move(stored)) {}
    gog::Result load(std::optional<gog::Session>& value) override { std::lock_guard lock(stored_->mutex); value = stored_->value; return {}; }
    bool save(const gog::Session& value) override {
        if (stored_->refuse) return false;
        std::lock_guard lock(stored_->mutex); stored_->value = value; ++stored_->saves; return true;
    }
    bool clear() override { std::lock_guard lock(stored_->mutex); stored_->value.reset(); return !stored_->refuse; }
};
struct Control { std::atomic_bool block{false}, entered{false}, offline{false}; std::atomic_int account{1}, refreshes{0}; };
class FakeBackend : public gog::Backend {
    std::shared_ptr<Control> control_;
    gog::Result wait(const gog::Cancel& cancel) {
        control_->entered = true;
        while (control_->block && !cancel()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return cancel() ? gog::Result{gog::Failure::cancelled} : gog::Result{};
    }
public:
    explicit FakeBackend(std::shared_ptr<Control> control) : control_(std::move(control)) {}
    gog::Result exchange(const gog::Config&, const std::string&, gog::Tokens& tokens, const gog::Cancel& cancel) override {
        const auto result = wait(cancel); tokens = session(std::to_string(control_->account.load())).tokens; return result;
    }
    gog::Result refresh(const gog::Config&, gog::Tokens& tokens, const gog::Cancel&) override {
        ++control_->refreshes; tokens.access = "refreshed-access"; tokens.expires = gog::nowSeconds() + 3600; return {};
    }
    gog::Result verify(gog::Tokens&, const gog::Config&, const gog::Cancel& cancel) override { return wait(cancel); }
    gog::Result library(gog::Session& value, const gog::Config&, const gog::Cancel& cancel) override {
        const auto result = wait(cancel); if (!result) return result;
        if (control_->offline) return {gog::Failure::offline};
        value.games = {game(value.tokens.user + "23")}; value.synced_at = gog::nowSeconds(); return {};
    }
    gog::Result installers(const std::string&, gog::Tokens&, const gog::Config&, const gog::Cancel&, std::vector<gog::InstallerVariant>& value) override {
        value = {{{"windows", L"Fixture", L"1", 3}, {L"https://api.gog.com/fixture"}}}; return {};
    }
    gog::Result resolveInstaller(const gog::InstallerVariant&, gog::Tokens&, const gog::Config&, const gog::Cancel&, std::vector<gog::InstallerPart>& value) override {
        value = {{L"setup.exe", L"https://cdn.gog.com/fixture", "900150983cd24fb0d6963f7d28e17f72", 3}}; return {};
    }
    gog::Result call(const AccountRequest& request, gog::Tokens& tokens, const gog::Config&, const gog::Cancel&, HttpResponse& response) override {
        if (request.url == L"https://users.gog.com/users/" + std::wstring(tokens.user.begin(), tokens.user.end())) {
            response = reply(R"({"id":")" + tokens.user + R"(","username":"Fixture Player"})"); return {};
        }
        check(request.url == L"https://gameplay.gog.com/clients/7/users/" + std::wstring(tokens.user.begin(), tokens.user.end()) + L"/achievements", "account ID filled in by the worker");
        tokens.access = "rotated-by-call"; // A 401 refresh: the worker must keep it.
        response = reply(R"({"items":[]})"); return {};
    }
};
template<class Predicate> void until(Predicate predicate) {
    for (int i = 0; i < 5000; ++i) { if (predicate()) return; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    throw std::runtime_error("worker timeout");
}
gog::Snapshot await(gog::Service& service, gog::State state) {
    std::optional<gog::Snapshot> found;
    until([&] { auto value = service.take(); if (value && value->state == state) found = std::move(value); return found.has_value(); });
    return std::move(*found);
}
void login(gog::Service& service) {
    service.submit(gog::Command::connect);
    const auto value = await(service, gog::State::signing_in);
    const auto pos = value.browser_url.find(L"&state="); check(pos != std::wstring::npos, "state in browser request");
    service.submit(gog::Command::complete, L"https://example.test/callback?origin=client&code=fixture&state=" + value.browser_url.substr(pos + 7));
}
void serviceTests() {
    auto stored = std::make_shared<Stored>(); auto control = std::make_shared<Control>();
    auto service = gog::makeService(std::make_unique<FakeBackend>(control), std::make_unique<FakeVault>(stored), [] { return config; });
    service->submit(gog::Command::restore); await(*service, gog::State::disconnected);
    {
        auto none = service->request({L"https://gameplay.gog.com/clients/7/users/{account}/achievements"});
        check(none.wait_for(std::chrono::seconds(5)) == std::future_status::ready && !none.get(), "no session, no account call");
    }
    login(*service);
    auto value = await(*service, gog::State::connected);
    if (!value.synced_at) value = await(*service, gog::State::connected);
    check(value.account == "1" && value.games.size() == 1, "verified account A");
    {
        // A plugin's account call: answered by the worker, the token's rotation kept, no new generation.
        auto answer = service->request({L"https://gameplay.gog.com/clients/7/users/{account}/achievements"});
        check(answer.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "account call answered");
        const auto response = answer.get();
        check(response && response->status == 200, "account call response");
        std::lock_guard lock(stored->mutex); check(stored->value && stored->value->tokens.access == "rotated-by-call", "account call rotation saved");
    }
    control->offline = true; service->submit(gog::Command::sync);
    value = await(*service, gog::State::offline);
    check(value.games.size() == 1 && value.synced_at, "offline membership retained");
    control->offline = false; control->block = true; control->entered = false;
    service->submit(gog::Command::sync); until([&] { return control->entered.load(); });
    service->submit(gog::Command::cancel); await(*service, gog::State::offline);
    service->submit(gog::Command::logout); await(*service, gog::State::disconnected);
    { std::lock_guard lock(stored->mutex); check(!stored->value, "logout clears durable session"); }
    control->block = false; control->account = 2;
    login(*service); value = await(*service, gog::State::connected);
    if (!value.synced_at) value = await(*service, gog::State::connected);
    check(value.account == "2" && value.games[0].source_id == "223", "A to B isolation");
    service->submit(gog::Command::logout); await(*service, gog::State::disconnected);
    control->block = true; control->entered = false;
    login(*service); until([&] { return control->entered.load(); });
    service->submit(gog::Command::cancel); value = await(*service, gog::State::disconnected);
    { std::lock_guard lock(stored->mutex); check(!stored->value, "cancelled login cannot persist credentials"); }
    control->block = false; stored->refuse = true;
    login(*service); value = await(*service, gog::State::error);
    check(value.failure == gog::Failure::storage && value.account.empty(), "storage failure cannot report connected");
    stored->refuse = false;
    auto timed = gog::makeService(std::make_unique<FakeBackend>(control), std::make_unique<FakeVault>(stored), [] { return config; }, std::chrono::milliseconds(30));
    timed->submit(gog::Command::connect); await(*timed, gog::State::signing_in);
    check(await(*timed, gog::State::error).failure == gog::Failure::expired, "login timeout");
}
void installServiceTests(const std::filesystem::path& root) {
    auto stored = std::make_shared<Stored>(); auto control = std::make_shared<Control>();
    std::atomic_bool blocked{true}, entered{false}; std::atomic_int executed{0};
    auto operations = gog::nativeInstallOperations();
    operations.download = [&](const auto&, const std::filesystem::path& file, const auto& cancel, const auto& progress) {
        entered = true;
        while (blocked && !cancel()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (!cancel()) { std::ofstream(file, std::ios::binary) << "abc"; progress(3); }
        return gog::InstallError::none;
    };
    operations.execute = [&](const auto&, const auto&, const auto&) { ++executed; return gog::InstallError::none; };
    operations.detect = [](const auto&) { return true; };
    auto service = gog::makeService(std::make_unique<FakeBackend>(control), std::make_unique<FakeVault>(stored), [] { return config; }, std::chrono::minutes(5), operations);
    login(*service); auto connected = await(*service, gog::State::connected);
    if (!connected.synced_at) connected = await(*service, gog::State::connected);
    gog::InstallRequest request{"123", "windows", root / "downloads", root / "games", 0};
    service->install(request); until([&] { return entered.load(); });
    service->submit(gog::Command::sync);
    check(service->installBusy(), "F5 must not cancel an active download");
    service->submit(gog::Command::logout); await(*service, gog::State::disconnected);
    const auto cleared = service->takeInstall();
    check(!executed && cleared && cleared->product.empty(), "logout cancels download and clears UI generation before execute");
    { std::lock_guard lock(stored->mutex); check(!stored->value, "download cannot restore logged-out token snapshot"); }
    blocked = false; control->account = 2;
    login(*service); connected = await(*service, gog::State::connected);
    if (!connected.synced_at) connected = await(*service, gog::State::connected);
    service->install(request);
    std::optional<gog::InstallSnapshot> result;
    until([&] { auto next = service->takeInstall(); if (next && next->state == gog::InstallState::error) result = std::move(next); return result.has_value(); });
    check(result->error == gog::InstallError::account && !executed, "A's product cannot install under account B");
    until([&] { return !service->installBusy(); });
    request.product = "223"; service->install(request); result.reset();
    until([&] { auto next = service->takeInstall(); if (next && next->state == gog::InstallState::completed) result = std::move(next); return result.has_value(); });
    check(executed == 1 && result->product == "223", "B's verified membership can download and install once");
}
// Comet (gog_comet.hpp): its arguments and log, and the account worker handing it the credentials.
void cometTests() {
    check(gog::cometUnlocked("[2026-11-16T10:00:00Z INFO  comet::api::handlers::utils] Unlocking achievement first_blood, First Blood, Again") ==
          L"First Blood, Again", "unlock line: the name");
    check(gog::cometUnlocked("[2026-11-16T10:00:00Z INFO  comet] Unlocking achievement only_key") == L"only_key", "unlock line without a name: the key");
    check(!gog::cometUnlocked("[2026-11-16T10:00:00Z INFO  comet] Listening on port 9977"), "other lines are not unlocks");
    // A game's session: GOG's overlay attaches to its process.
    check(gog::cometSession("[2026-11-16T10:00:00Z INFO  comet::api::handlers::communication_service] Requested session start for 4321") == 4321u,
          "session line: the game's process ID");
    check(!gog::cometSession("[2026-11-16T10:00:00Z INFO  comet] Requested session start for ") &&
          !gog::cometSession("Requested session start for 0") && !gog::cometSession("Requested session start for 99999999999"),
          "session line without a usable process ID");
    {
        const auto arguments = gog::overlayArguments(std::filesystem::path(L"C:\\data\\redist"), 4321, "1207658924",
                                                     std::filesystem::path(L"C:\\data\\cache\\1207658924"), L"pl-PL");
        check(arguments.find(L"--startoverlay") != std::wstring::npos && arguments.find(L"--attached-pid=4321") != std::wstring::npos &&
              arguments.find(L"--product-id=1207658924") != std::wstring::npos && arguments.find(L"--client-language-code=pl-PL") != std::wstring::npos &&
              arguments.find(L"--startup-url=file://C:/data/redist/web/overlay.html") != std::wstring::npos &&
              arguments.find(L"-cache-path=\"C:\\data\\cache\\1207658924\"") != std::wstring::npos, "overlay arguments as Galaxy passes them");
        const auto empty = std::filesystem::temp_directory_path() / L"launchme-overlay-none";
        check(!gog::overlayInstalled(empty) && !gog::startOverlay(empty, 4321, "1207658924", L"en-US") &&
              !gog::startOverlay(empty, 4321, "12 && calc", L"en-US"), "no overlay, or a product ID that is not a number: nothing starts");
    }
    const auto arguments = gog::cometArguments({"a", "r", "42", ""});
    check(arguments == std::vector<std::string>{"--access-token", "a", "--refresh-token", "r", "--user-id", "42", "--username", "42", "--quit"},
          "comet arguments, the ID standing in for a missing name");

    struct FakeComet : gog::Comet { bool running() const override { return true; } };
    std::mutex mutex; std::vector<gog::CometCredentials> seen;
    const gog::CometStart start = [&](const gog::CometRequest& request, const gog::CometCredentials& credentials) -> std::shared_ptr<gog::Comet> {
        check(request.exe == std::filesystem::path(L"C:/fixture/comet.exe"), "the prepared binary is started");
        std::lock_guard lock(mutex); seen.push_back(credentials); return std::make_shared<FakeComet>();
    };
    const gog::Config galaxy{std::string(gog::galaxy_client_id), "fixture-secret", "https://example.test/callback?origin=client"};
    const auto ask = [](gog::Service& service) {
        auto answer = service.comet({L"C:/fixture/comet.exe", L"C:/fixture/data", {}});
        check(answer.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "comet request answered");
        return answer.get();
    };
    {
        auto stored = std::make_shared<Stored>(); auto control = std::make_shared<Control>();
        auto service = gog::makeService(std::make_unique<FakeBackend>(control), std::make_unique<FakeVault>(stored), [&] { return galaxy; },
                                        std::chrono::minutes(5), gog::nativeInstallOperations(), start);
        service->submit(gog::Command::restore); await(*service, gog::State::disconnected);
        check(!ask(*service) && seen.empty(), "no session, no comet");
        auto account = session(); account.tokens.client = galaxy.client_id; account.tokens.expires = gog::nowSeconds() + 60;
        { std::lock_guard lock(stored->mutex); stored->value = account; }
        service->submit(gog::Command::restore); await(*service, gog::State::connected);
        check(ask(*service) != nullptr, "comet started for a Galaxy-profile account");
        std::lock_guard lock(mutex);
        check(seen.size() == 1 && control->refreshes == 1 && seen[0].access == "refreshed-access" && seen[0].refresh == "fixture-refresh" &&
              seen[0].user == account.tokens.user && seen[0].username == "Fixture Player", "a due token refreshed first; the account's name");
        std::lock_guard saved(stored->mutex);
        check(stored->value && stored->value->tokens.access == "refreshed-access", "comet's refresh kept in the vault");
    }
    {
        // Another client profile: comet could not get the games' tokens with it.
        auto stored = std::make_shared<Stored>(); auto control = std::make_shared<Control>();
        stored->value = session();
        auto service = gog::makeService(std::make_unique<FakeBackend>(control), std::make_unique<FakeVault>(stored), [] { return config; },
                                        std::chrono::minutes(5), gog::nativeInstallOperations(), start);
        service->submit(gog::Command::restore); await(*service, gog::State::connected);
        check(!ask(*service) && seen.size() == 1, "no comet with a non-Galaxy client profile");
    }
}
void shutdownTests() {
    auto stored = std::make_shared<Stored>(); auto control = std::make_shared<Control>();
    const auto create = [&] { return gog::makeService(std::make_unique<FakeBackend>(control), std::make_unique<FakeVault>(stored), [] { return config; }); };
    // Exercise entry into the first wait, queued work, and return to an idle wait.
    // CTest's timeout also covers a destructor stuck in join().
    for (int i = 0; i < 128; ++i) {
        auto service = create();
        if (i % 3) service->submit(gog::Command::restore);
        if (i % 3 == 2) await(*service, gog::State::disconnected);
    }
    auto signing_in = create();
    signing_in->submit(gog::Command::connect); await(*signing_in, gog::State::signing_in);
    signing_in.reset(); // Interrupt the timed login wait as well.
    stored->value = session(); control->block = true; control->entered = false;
    auto busy = create(); busy->submit(gog::Command::restore);
    until([&] { return control->entered.load(); });
    busy.reset(); // An in-flight backend must observe cancellation and finish.
}
void persistenceAndMerge(const std::filesystem::path& root) {
    int profile_requests = 0;
    const auto profile_http = [&](const auto& url, const auto&, size_t) {
        ++profile_requests;
        check(url.find(L"2423d32a8a22909cd3da39960eed55ad28384858") != std::wstring::npos, "immutable client profile source");
        return reply(R"({"stores":{"gog":{"client_id":"123456","client_secret":"abcdef123456","redirect_uri":"https://embed.gog.com/on_login_success?origin=client","auth_url":"https://auth.gog.com/auth","token_url":"https://auth.gog.com/token"}}})");
    };
    const auto profile = gog::prepareClientProfile(root / L"gog-client.bin", profile_http);
    check(profile.valid() && profile.legacy_manual_callback && profile_requests == 1, "automatic public client profile");
    check(gog::prepareClientProfile(root / L"gog-client.bin", profile_http).valid() && profile_requests == 1, "profile cache works offline");
    check(gog::callbackCode(L"https://embed.gog.com/on_login_success?origin=client&code=fixture", profile, "active-attempt") == "fixture", "legacy manual callback without state");
    check(!gog::callbackCode(L"https://embed.gog.com/on_login_success?origin=client&code=fixture&state=wrong", profile, "active-attempt"), "present state must always match");
    check(!gog::parsePublicClientProfile(R"({"stores":{"gog":{"client_id":"123","client_secret":"abc","redirect_uri":"https://evil.test/","auth_url":"https://auth.gog.com/auth","token_url":"https://auth.gog.com/token"}}})").valid(), "public profile cannot redirect credentials to foreign origin");
    const auto file = root / L"gog.session";
    auto vault = gog::makeVault(file); auto original = session(); original.games[0].added_at = 1600000000;
    check(vault->save(original), "DPAPI atomic save");
    std::ifstream input(file, std::ios::binary); const std::string bytes((std::istreambuf_iterator<char>(input)), {}); input.close();
    check(bytes.find("fixture-access") == std::string::npos && bytes.find("9007199254740993") == std::string::npos, "no plaintext credentials or membership");
    std::optional<gog::Session> restored;
    check(static_cast<bool>(vault->load(restored)) && restored && restored->tokens.user == original.tokens.user && restored->games.size() == 1, "DPAPI roundtrip");
    check(restored->games[0].added_at == 1600000000, "acquisition survives restart");
    auto bad = original; bad.tokens.user = "../bad";
    check(!vault->save(bad) && static_cast<bool>(vault->load(restored)), "failed save retains previous session");
    check(atomic_file::write(file, "corrupt"), "corrupt fixture");
    check(vault->load(restored).failure == gog::Failure::storage && !restored, "corrupt cache fails closed");
    check(vault->clear() && static_cast<bool>(vault->load(restored)) && !restored, "logout clears vault");
    check(protected_file::write(file, "launchme-gog-v1\n\"fixture-client\" \"1\" \"fixture-access\" \"fixture-refresh\" 1800000000 1 1\n\"123\" \"Legacy\" 0\n", L"Fixture"), "legacy vault fixture");
    check(static_cast<bool>(vault->load(restored)) && restored && restored->games.size() == 1 && !restored->games[0].added_at, "old sessions load without invented dates");
    check(vault->clear(), "legacy fixture cleanup");
    StoreLibrary local; auto installed = game("123"); installed.installed = true;
    installed.play_action = {L"C:\\fixture\\game.exe"}; local.visible.push_back(installed);
    auto merged = mergePcLibraries(mergeGogLibrary(local, {original.games[0], game("456")}, "1"), root / L"libraries");
    check(merged.visible.size() == 2 && merged.visible[0].installed && !merged.visible[1].installed &&
        merged.visible[0].account_id.empty() && merged.visible[1].account_id == "1" && merged.visible[0].play_action.target == installed.play_action.target, "local offline merge and online-only membership");
    check(merged.visible[0].added_at == 1600000000, "installed GOG copy inherits the account acquisition date");
    check(atomic_file::write(root / L"libraries" / L"gog.json", R"({"version":1,"games":[{"id":"456","name":"Private","installed":false,"visibility":"private"}]})"), "privacy fixture");
    merged = mergePcLibraries(mergeGogLibrary(local, {game("123"), game("456")}, "1"), root / L"libraries");
    check(merged.visible.size() == 1 && merged.private_games.size() == 1, "catalog privacy after online merge");
    atomic_file::write(root / L"libraries" / L"gog.json", "broken");
    merged = mergePcLibraries(mergeGogLibrary(local, {game("123"), game("456")}, "1"), root / L"libraries");
    check(merged.visible.empty() && !merged.errors.empty(), "broken privacy catalog hides native and online games");
}
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / (L"launchme-gog-test-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); } } cleanup{root};
    try { apiTests(); serviceTests(); installServiceTests(root); cometTests(); shutdownTests(); persistenceAndMerge(root); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "GOG OAuth, pagination, isolation, cancellation, offline, merge and DPAPI passed\n";
}
