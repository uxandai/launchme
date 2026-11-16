#include "../src/store/gog_account.hpp"
#include "../src/platform/user_config.hpp"
#include "../src/net/json.hpp"
#include <iostream>
#include <mutex>
#include <regex>
#include <thread>

namespace {
// --comet: the account's comet started as Launchme starts it, its log shown with every long
// token-like run masked, the port watched, then ended. Never prints credentials.
int cometProbe() {
    auto service = gog::makeService(gog::makeBackend(), gog::makeVault(userConfigDirectory() / L"accounts" / L"gog.session"));
    service->submit(gog::Command::restore);
    std::optional<gog::Snapshot> state;
    for (int i = 0; i < 600; ++i) {
        if (auto next = service->take()) state = std::move(next);
        if (state && state->state != gog::State::syncing && state->state != gog::State::preparing && state->state != gog::State::verifying) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::cout << "account_state=" << (state ? static_cast<int>(state->state) : -1) << "; failure=" << (state ? static_cast<int>(state->failure) : -1)
              << "; games=" << (state ? state->games.size() : 0) << '\n';
    if (!state || state->account.empty()) return 1;
    const auto exe = gog::prepareComet(userConfigDirectory(), [] { return false; });
    std::cout << "comet_binary=" << !exe.empty() << "; port_busy_before=" << gog::galaxyPortBusy() << '\n';
    if (exe.empty()) return 1;
    std::mutex print;
    const std::regex secret("[A-Za-z0-9_\\-\\.]{32,}");
    gog::CometRequest request{exe, userConfigDirectory() / L"gog" / L"comet"};
    request.unlocked = [&](std::wstring) { std::lock_guard lock(print); std::cout << "UNLOCK seen\n"; };
    request.log = [&](std::string_view line) {
        std::lock_guard lock(print); std::cout << "comet| " << std::regex_replace(std::string(line), secret, "<masked>") << '\n';
    };
    auto answer = service->comet(std::move(request));
    if (answer.wait_for(std::chrono::seconds(60)) != std::future_status::ready) { std::cout << "comet_request=timeout\n"; return 1; }
    auto comet = answer.get();
    std::cout << "comet_started=" << (comet != nullptr) << '\n';
    if (!comet) return 1;
    for (int second = 0; second < 40; ++second) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (second % 10 == 9) { std::lock_guard lock(print); std::cout << "t=" << second + 1 << "s running=" << comet->running() << " port_busy=" << gog::galaxyPortBusy() << '\n'; }
    }
    comet.reset();
    std::cout << "comet_stopped; port_busy_after=" << gog::galaxyPortBusy() << '\n';
    return 0;
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--comet") return cometProbe();
    // Opt-in diagnostic: run with Launchme closed. Never print identifiers,
    // URLs, credentials, titles, response bodies or server-provided errors.
    auto vault = gog::makeVault(userConfigDirectory() / L"accounts" / L"gog.session");
    std::optional<gog::Session> session;
    const auto loaded = vault->load(session);
    std::cout << "session_readable=" << static_cast<bool>(loaded) << "; present=" << session.has_value() << '\n';
    if (!loaded || !session) return 1;
    const auto config = gog::loadConfig();
    const bool offline = argc == 2 && std::string_view(argv[1]) == "--offline";
    auto backend = gog::makeBackend([offline](const std::wstring& url, const std::wstring& headers, size_t limit) {
        const auto response = offline ? HttpResponse{} : httpGetPrivate(url, headers, limit);
        const char* stage = url.starts_with(L"https://auth.gog.com/") ? "token" :
            url.starts_with(L"https://users.gog.com/") ? "profile" :
            url.starts_with(L"https://galaxy-library.gog.com/") ? "library" : url.starts_with(L"https://gamesdb.gog.com/") ? "gamesdb" : "product";
        const auto root = json::parse({reinterpret_cast<const char*>(response.body.data()), response.body.size()});
        std::cout << "stage=" << stage << "; http=" << response.status << "; json=" << root.has_value();
        const auto type = [](const json::Value* value) { return value ? static_cast<int>(value->type) : -1; };
        if (root && std::string_view(stage) == "library") {
            const auto items = root->get("items");
            std::cout << "; items_type=" << type(items) << "; next_type=" << type(root->get("next_page_token"));
            if (items) {
                size_t gog_items = 0, invalid_owned = 0, invalid_id = 0;
                for (const auto& item : items->elements) {
                    const auto platform = item.get("platform_id");
                    if (!platform || platform->stringOr() != "gog") continue;
                    ++gog_items;
                    const auto owned = item.get("owned"), id = item.get("external_id");
                    invalid_owned += !owned || owned->type != json::Value::Type::boolean;
                    invalid_id += !id || (id->type != json::Value::Type::string && id->type != json::Value::Type::number);
                }
                std::cout << "; gog_items=" << gog_items << "; invalid_owned=" << invalid_owned << "; invalid_id=" << invalid_id;
            }
        }
        if (root && std::string_view(stage) == "product") {
            for (const auto key : {"id", "title", "game_type", "is_secret"}) std::cout << "; " << key << "_type=" << type(root->get(key));
            const auto kind = root->get("game_type");
            std::cout << "; known_kind=" << (kind && (kind->stringOr() == "game" || kind->stringOr() == "dlc" || kind->stringOr() == "pack"));
        }
        std::cout << '\n';
        return response;
    });
    auto next = *session;
    const auto running = [] { return false; };
    auto result = argc == 2 && std::string_view(argv[1]) == "--refresh" ? backend->refresh(config, next.tokens, running) : gog::Result{};
    if (result) result = backend->verify(next.tokens, config, running);
    std::cout << "verify_failure=" << static_cast<int>(result.failure) << '\n';
    if (result && argc == 3 && std::string_view(argv[1]) == "--installers") {
        std::vector<gog::InstallerVariant> variants;
        result = backend->installers(argv[2], next.tokens, config, running, variants);
        std::cout << "windows_variants=" << variants.size() << '\n';
        if (result && !variants.empty()) {
            std::vector<gog::InstallerPart> parts;
            result = backend->resolveInstaller(variants.front(), next.tokens, config, running, parts);
            uint64_t size = 0; for (const auto& part : parts) size += part.bytes;
            std::cout << "resolved_parts=" << parts.size() << "; exact_bytes=" << size << '\n';
        }
    } else if (result) result = backend->library(next, config, running);
    if (!result) { next.games = session->games; next.synced_at = session->synced_at; }
    const bool saved = vault->save(next); // Preserve rotated refresh tokens atomically, even on a library failure.
    std::cout << "sync_failure=" << static_cast<int>(result.failure) << "; games=" << next.games.size() << "; saved=" << saved << '\n';
    if (offline) return result.failure == gog::Failure::offline && next.games.size() == session->games.size() && saved ? 0 : 1;
    return result && saved ? 0 : 1;
}
