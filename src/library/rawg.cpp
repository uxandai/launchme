#include "rawg.hpp"
#include "../net/json.hpp"
#include "../net/url_encoding.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>

namespace rawg {
namespace {
std::wstring field(const json::Value& value, const char* key, size_t limit = 200) {
    const auto* item = value.get(key);
    auto text = item ? text_encoding::wide(item->stringOr()) : std::wstring{};
    if (text.size() > limit) text.resize(limit);
    return text;
}
uint32_t id(const json::Value& value) {
    const auto* item = value.get("id");
    return item && item->type == json::Value::Type::number && std::isfinite(item->numeric) && item->numeric > 0 &&
        item->numeric <= UINT32_MAX && std::floor(item->numeric) == item->numeric ? static_cast<uint32_t>(item->numeric) : 0;
}
std::wstring names(const json::Value& value, const char* key, bool platforms = false) {
    const auto* list = value.get(key);
    if (!list || list->type != json::Value::Type::array) return {};
    std::wstring result;
    for (const auto& item : list->elements) {
        const auto* entry = platforms ? item.get("platform") : &item;
        const auto name = entry ? field(*entry, "name") : std::wstring{};
        if (name.empty()) continue;
        if (!result.empty()) result += L", ";
        result += name;
        if (result.size() >= 300) break;
    }
    return result;
}
std::wstring error(uint32_t status) {
    if (status == 401 || status == 403) return tr(L"RAWG odrzucił klucz API.");
    if (status == 429) return tr(L"Limit zapytań RAWG. Spróbuj później.");
    return tr(L"Nie można pobrać danych RAWG.");
}
std::filesystem::path file(const Game& game, const std::filesystem::path& cache) { return cache / (gameCacheKey(game) + ".rawg.json"); }
}

Search parseSearch(std::string_view body) {
    const auto root = json::parse(body);
    const auto* results = root ? root->get("results") : nullptr;
    if (!results || results->type != json::Value::Type::array) return {{}, tr(L"Nieprawidłowa odpowiedź RAWG.")};
    Search search;
    for (const auto& item : results->elements) {
        Candidate game{id(item), field(item, "name"), field(item, "released"), names(item, "platforms", true), field(item, "background_image", 600)};
        if (game.id && !game.name.empty()) search.games.push_back(std::move(game));
        if (search.games.size() >= 10) break;
    }
    return search;
}
std::optional<Metadata> parseDetails(std::string_view body) {
    const auto parsed = json::parse(body);
    const auto* root = parsed ? &*parsed : nullptr;
    // Versioned cache wrapper keeps RAWG's response intact and saves our verified store mapping.
    const bool wrapped = root && root->get("launchme_cache");
    if (wrapped) {
        if (root->get("launchme_cache")->intOr() != 1) return {};
        root = root->get("response");
        if (!root || root->type != json::Value::Type::object || root->get("launchme_cache")) return {};
    }
    if (!root || !id(*root)) return {};
    Metadata result; result.id = id(*root); result.name = field(*root, "name");
    if (result.name.empty()) return {};
    result.details.description = field(*root, "description_raw", 1800);
    result.details.developer = names(*root, "developers"); result.details.publisher = names(*root, "publishers");
    result.details.genres = names(*root, "genres"); result.details.release_date = field(*root, "released");
    result.rating.source = ratings::Source::rawg; result.rating.rawg_id = result.id;
    if (const auto* slug = root->get("slug")) result.rating.slug = slug->stringOr();
    if (ratings::url(result.rating, 0).empty()) return {}; // Attribution always links back to a safe RAWG URL.
    const auto* score = root->get("rating"); const auto* count = root->get("ratings_count");
    if (count && count->intOr() > 0 && score && score->type == json::Value::Type::number && std::isfinite(score->numeric) && score->numeric >= 0 && score->numeric <= 5) {
        result.rating.count = static_cast<uint32_t>(count->intOr()); result.rating.score = score->numeric;
    }
    if (const auto* critic = root->get("metacritic"); critic && critic->intOr() > 0 && critic->intOr() <= 100) result.rating.metacritic = critic->intOr();
    if (const auto* app = wrapped ? parsed->get("steam_app_id") : nullptr;
        app && app->type == json::Value::Type::number && std::isfinite(app->numeric) &&
        app->numeric > 0 && app->numeric <= UINT32_MAX && std::floor(app->numeric) == app->numeric)
        result.steam_app_id = static_cast<uint32_t>(app->numeric);
    return result;
}
uint32_t steamAppIdFromUrl(std::string_view url) {
    constexpr std::string_view https = "https://store.steampowered.com/", http = "http://store.steampowered.com/";
    if (url.starts_with(https)) url.remove_prefix(https.size());
    else if (url.starts_with(http)) url.remove_prefix(http.size());
    else return 0;
    if (url.starts_with("agecheck/")) url.remove_prefix(9);
    if (!url.starts_with("app/")) return 0; // A package/sub/bundle is not an AppID.
    url.remove_prefix(4);
    const auto digits = url.substr(0, url.find_first_of("/?#"));
    uint32_t result = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), result);
    return ec == std::errc{} && end == digits.data() + digits.size() ? result : 0;
}
StoreLinks parseStoreLinks(uint32_t rawg_id, std::string_view body) {
    const auto root = json::parse(body);
    const auto* results = root ? root->get("results") : nullptr;
    if (!results || results->type != json::Value::Type::array) return {false, 0, tr(L"Nieprawidłowa odpowiedź RAWG.")};
    StoreLinks result{true};
    for (const auto& item : results->elements) {
        // RAWG's store_id identifies the storefront, never a Steam game.
        if (const auto* game = item.get("game_id")) {
            const bool same = (game->type == json::Value::Type::number && game->numeric == rawg_id) ||
                              (game->type == json::Value::Type::string && game->text == std::to_string(rawg_id));
            if (!same) continue;
        }
        const auto* url = item.get("url");
        const auto app = url ? steamAppIdFromUrl(url->stringOr()) : 0;
        if (!app) continue;
        if (result.steam_app_id && result.steam_app_id != app) return {true}; // Ambiguous editions: use title matching instead.
        result.steam_app_id = app;
    }
    return result;
}
StoreLinks stores(uint32_t rawg_id, const std::wstring& key, const Get& get) {
    if (!rawg_id || key.empty()) return {};
    HttpBudget budget(6000);
    const auto response = get(L"https://api.rawg.io/api/games/" + std::to_wstring(rawg_id) + L"/stores?key=" + url_encoding::component(key) + L"&page_size=40",
                              L"Accept: application/json\r\n", 128 * 1024);
    if (response.status != 200) return {false, 0, error(response.status)};
    return parseStoreLinks(rawg_id, std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()));
}
Search search(const std::wstring& query, const std::wstring& key, const Get& get) {
    if (key.empty()) return {{}, tr(L"Dodaj klucz RAWG w Ustawieniach → Media i dane.")};
    if (query.empty()) return {};
    HttpBudget budget(6000);
    const auto response = get(L"https://api.rawg.io/api/games?key=" + url_encoding::component(key) + L"&search=" + url_encoding::component(query) + L"&page_size=10", L"Accept: application/json\r\n", 256 * 1024);
    if (response.status != 200) return {{}, error(response.status)};
    return parseSearch(std::string_view(reinterpret_cast<const char*>(response.body.data()), response.body.size()));
}
Details fetch(uint32_t game_id, const std::wstring& key, const Get& get) {
    if (key.empty()) return {{}, {}, tr(L"Dodaj klucz RAWG w Ustawieniach → Media i dane.")};
    if (!game_id) return {{}, {}, tr(L"Nieprawidłowa odpowiedź RAWG.")};
    HttpBudget budget(6000);
    const auto response = get(L"https://api.rawg.io/api/games/" + std::to_wstring(game_id) + L"?key=" + url_encoding::component(key), L"Accept: application/json\r\n", 512 * 1024);
    if (response.status != 200) return {{}, {}, error(response.status)};
    Details result; result.json.assign(reinterpret_cast<const char*>(response.body.data()), response.body.size());
    result.data = parseDetails(result.json);
    if (!result.data || result.data->id != game_id) { result.data.reset(); result.error = tr(L"Nieprawidłowa odpowiedź RAWG."); }
    return result;
}
std::optional<Metadata> load(const Game& game, const std::filesystem::path& cache) {
    if (gameKey(game).empty()) return {};
    std::ifstream input(file(game, cache), std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 1024 * 1024) return {};
    std::string bytes(static_cast<size_t>(input.tellg()), '\0'); input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return {};
    return parseDetails(bytes);
}
bool save(const Game& game, const Details& details, const std::filesystem::path& cache) {
    if (gameKey(game).empty() || !details.data || details.json.empty()) return false;
    const auto parsed = parseDetails(details.json);
    if (!parsed || parsed->id != details.data->id) return false;
    const auto response = json::parse(details.json);
    if (!response || response->get("launchme_cache")) return false; // Never nest cache wrappers.
    return atomic_file::write(file(game, cache), "{\"launchme_cache\":1,\"steam_app_id\":" + std::to_string(details.data->steam_app_id) +
                              ",\"response\":" + details.json + "}");
}
}
