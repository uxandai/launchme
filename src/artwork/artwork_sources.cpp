#include "artwork_sources.hpp"
#include "gog_art.hpp"
#include "../i18n/i18n.hpp"
#include "../net/json.hpp"
#include "../net/url_encoding.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../store/steam_app_match.hpp"

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <optional>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <unordered_set>

namespace artwork_sources {
namespace {
constexpr uint32_t first_id = 0x60000000u;
int sourceIndex(ArtworkProvider provider) {
    switch (provider) {
    case ArtworkProvider::steam: return 0;
    case ArtworkProvider::gog: return 1;
    case ArtworkProvider::xbox: return 2;
    case ArtworkProvider::nintendo: return 3;
    case ArtworkProvider::gametdb: return 4;
    case ArtworkProvider::titledb: return 5;
    case ArtworkProvider::thegamesdb: return 6;
    default: return 7; // wikimedia
    }
}
uint32_t fnv(std::wstring_view text) {
    uint32_t hash = 2166136261u;
    for (const auto ch : text) { hash ^= static_cast<uint32_t>(ch); hash *= 16777619u; }
    return hash;
}
std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }
std::string text(const HttpResponse& response) { return std::string(response.body.begin(), response.body.end()); }
std::string field(const json::Value* value, std::string_view name) {
    const auto* found = value ? value->get(name) : nullptr;
    return found && found->type == json::Value::Type::string ? found->text : std::string{};
}
int number(const json::Value* value, std::string_view name) {
    const auto* found = value ? value->get(name) : nullptr;
    return found && found->type == json::Value::Type::number ? static_cast<int>(found->numeric) : 0;
}
// Protocol-relative and plain-HTTP links of the stores become HTTPS.
std::wstring secure(std::string_view url) {
    if (url.empty()) return {};
    if (url.starts_with("//")) return L"https:" + wide(url);
    if (url.starts_with("http://")) return L"https://" + wide(url.substr(7));
    return wide(url);
}
std::optional<json::Value> getJson(const ArtworkHttp& get, const std::wstring& url, const std::wstring& headers = L"Accept: application/json\r\n",
                                   size_t limit = 8 * 1024 * 1024) {
    const auto response = get(url, headers, limit);
    if (response.status != 200 || response.body.empty()) return std::nullopt;
    return json::parse(text(response), limit);
}

// One gallery being collected: unique URLs, IDs from the source's range.
struct Collector {
    ArtworkProvider provider;
    GridPage page;
    std::set<std::wstring> seen;
    void add(const std::wstring& url, const std::wstring& thumbnail, const std::wstring& author, int width = 0, int height = 0) {
        if (url.empty() || !url.starts_with(L"https://") || !seen.insert(url).second || page.assets.size() >= 96) return;
        GridAsset asset;
        asset.id = first_id | (static_cast<uint32_t>(sourceIndex(provider)) << 26) | (fnv(url) & 0x03ffffffu);
        asset.url = url;
        asset.thumbnail = thumbnail.empty() ? url : thumbnail;
        asset.author = author;
        asset.width = width; asset.height = height;
        page.assets.push_back(std::move(asset));
    }
};

// Results named like the game: those matching it by SteamGridDB's rules, else the source's first two.
template <class Item, class Title>
std::vector<const Item*> matching(const std::vector<Item>& items, const std::wstring& title, Title titleOf, size_t most = 3) {
    std::vector<std::pair<int, const Item*>> scored;
    for (const auto& item : items) if (const int score = gridTitleScore(title, titleOf(item)); score > 0) scored.emplace_back(score, &item);
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<const Item*> picked;
    for (const auto& [score, item] : scored) if (picked.size() < most) picked.push_back(item);
    if (picked.empty()) for (const auto& item : items) if (picked.size() < 2) picked.push_back(&item);
    return picked;
}

bool emulatedGame(const Game& game) { return game.kind == GameKind::nonsteam && game.platform != L"PC"; }
std::wstring lower(std::wstring value) { for (auto& ch : value) ch = static_cast<wchar_t>(std::towlower(ch)); return value; }
// GameTDB's folder for the game's console; empty when it has none.
std::wstring gametdbPlatform(const Game& game) {
    const auto platform = lower(game.platform);
    if (platform.find(L"switch") != std::wstring::npos) return L"switch";
    if (platform.find(L"wii u") != std::wstring::npos || platform.find(L"wiiu") != std::wstring::npos) return L"wiiu";
    if (platform.find(L"wii") != std::wstring::npos || platform.find(L"gamecube") != std::wstring::npos) return L"wii";
    if (platform.find(L"3ds") != std::wstring::npos) return L"3ds";
    if (platform == L"ds" || platform.find(L"nintendo ds") != std::wstring::npos || platform.find(L"nds") != std::wstring::npos) return L"ds";
    return {};
}

// --- Steam ------------------------------------------------------------------
GridPage steam(const Game& game, ArtworkKind kind, const Context& context, const ArtworkHttp& get) {
    Collector out{ArtworkProvider::steam};
    uint32_t app = context.steam_app_id;
    // A typed Steam AppID.
    if (!app && !context.title.empty() && context.title.size() <= 9 && std::all_of(context.title.begin(), context.title.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; }))
        app = static_cast<uint32_t>(std::stoul(context.title));
    if (!app && !context.title.empty())
        app = steamAppForTitle(context.title, context.config / L"metadata", gameCacheKey(game).empty() ? "artwork-source" : gameCacheKey(game), get);
    if (!app) { out.page.error = tr(L"Sklep Steam nie zna tej gry. Wpisz inną nazwę i wybierz Szukaj."); return out.page; }
    const auto id = std::to_wstring(app);
    const auto cdn = L"https://shared.fastly.steamstatic.com/store_item_assets/steam/apps/" + id + L"/";
    const auto details = getJson(get, L"https://store.steampowered.com/api/appdetails?appids=" + id + L"&l=english", L"Accept: application/json\r\n", 2 * 1024 * 1024);
    const json::Value* data = nullptr;
    if (details) if (const auto* entry = details->get(std::to_string(app))) data = entry->get("data");
    const auto shots = [&] {
        if (!data) return;
        if (const auto* list = data->get("screenshots"); list && list->type == json::Value::Type::array)
            for (const auto& shot : list->elements)
                out.add(secure(field(&shot, "path_full")), secure(field(&shot, "path_thumbnail")), L"Steam · " + std::wstring(tr(L"zrzut ekranu")));
    };
    switch (kind) {
    case ArtworkKind::cover:
        out.add(cdn + L"library_600x900_2x.jpg", cdn + L"library_600x900.jpg", L"Steam · library");
        out.add(secure(field(data, "header_image")), {}, L"Steam · header");
        out.add(cdn + L"capsule_616x353.jpg", {}, L"Steam · capsule");
        break;
    case ArtworkKind::poster:
        out.add(cdn + L"library_600x900_2x.jpg", cdn + L"library_600x900.jpg", L"Steam · library 2x");
        out.add(cdn + L"library_600x900.jpg", {}, L"Steam · library");
        break;
    case ArtworkKind::logo:
        out.add(cdn + L"logo.png", {}, L"Steam · logo");
        out.add(cdn + L"logo_2x.png", {}, L"Steam · logo 2x");
        break;
    case ArtworkKind::landscape:
        out.add(secure(field(data, "header_image")), {}, L"Steam · header");
        out.add(cdn + L"capsule_616x353.jpg", {}, L"Steam · capsule");
        out.add(cdn + L"library_hero.jpg", {}, L"Steam · hero");
        shots();
        break;
    default: // hero
        out.add(cdn + L"library_hero.jpg", {}, L"Steam · hero");
        out.add(secure(field(data, "background_raw")), {}, L"Steam · background");
        shots();
        break;
    }
    return out.page;
}

// --- GOG --------------------------------------------------------------------
GridPage gog(const Game& game, ArtworkKind kind, const Context& context, const ArtworkHttp& get) {
    Collector out{ArtworkProvider::gog};
    struct Product { std::string id; std::wstring title; std::string vertical, horizontal, logo, background; std::vector<std::string> shots; };
    std::vector<Product> products;
    if (const auto found = getJson(get, L"https://catalog.gog.com/v1/catalog?limit=12&productType=in:game,pack&locale=en-US&query=like:" +
                                        url_encoding::component(context.title))) {
        if (const auto* list = found->get("products"); list && list->type == json::Value::Type::array)
            for (const auto& item : list->elements) {
                Product product{field(&item, "id"), wide(field(&item, "title")), field(&item, "coverVertical"), field(&item, "coverHorizontal"),
                                field(&item, "logo"), field(&item, "galaxyBackgroundImage"), {}};
                if (const auto* shots = item.get("screenshots"); shots && shots->type == json::Value::Type::array)
                    for (const auto& shot : shots->elements) if (shot.type == json::Value::Type::string) product.shots.push_back(shot.text);
                products.push_back(std::move(product));
            }
    }
    std::vector<const Product*> picked;
    // A GOG game: its own product first (the catalog answer, else the product API).
    if (game.source == GameSource::gog && !game.source_id.empty()) {
        for (const auto& product : products) if (product.id == game.source_id) picked.push_back(&product);
        if (picked.empty()) {
            const auto art = gogArt(game.source_id, context.config / L"covers", get);
            if (kind == ArtworkKind::cover || kind == ArtworkKind::poster) out.add(secure(art.tall), {}, L"GOG");
            if (kind == ArtworkKind::hero || kind == ArtworkKind::landscape) out.add(secure(art.wide), {}, L"GOG");
            if (kind == ArtworkKind::logo) out.add(secure(art.logo), {}, L"GOG · logo");
        }
    }
    if (picked.empty()) picked = matching(products, context.title, [](const Product& product) { return product.title; }, 2);
    for (const auto* product : picked) {
        const auto author = L"GOG · " + product->title;
        // Screenshots carry a {formatter}: a large gallery picture and a small card.
        const auto shots = [&] {
            for (const auto& shot : product->shots) {
                const auto at = shot.find("{formatter}");
                if (at == std::string::npos) continue;
                auto full = shot, small = shot;
                full.replace(at, 11, "ggvgl_2x"); small.replace(small.find("{formatter}"), 11, "product_card_v2_mobile_slider_639");
                out.add(secure(full), secure(small), author);
            }
        };
        switch (kind) {
        case ArtworkKind::cover: out.add(secure(product->vertical), {}, author); out.add(secure(product->horizontal), {}, author); break;
        case ArtworkKind::poster: out.add(secure(product->vertical), {}, author); break;
        case ArtworkKind::logo: out.add(secure(product->logo), {}, author); break;
        case ArtworkKind::landscape: out.add(secure(product->horizontal), {}, author); shots(); break;
        default: out.add(secure(product->background), {}, author); shots(); break;
        }
    }
    if (out.page.assets.empty()) out.page.error = tr(L"GOG nie ma grafik tej gry.");
    return out.page;
}

// --- Xbox (Microsoft Store) -------------------------------------------------
GridPage xbox(const Game& game, ArtworkKind kind, const Context& context, const ArtworkHttp& get) {
    Collector out{ArtworkProvider::xbox};
    std::vector<std::string> ids;
    if (!game.xbox_product.empty()) ids.push_back(game.xbox_product);
    if (ids.empty()) {
        struct Hit { std::string id; std::wstring title; };
        std::vector<Hit> hits;
        if (const auto found = getJson(get, L"https://displaycatalog.mp.microsoft.com/v7.0/productFamilies/autosuggest?market=US&languages=en-US&productFamilyNames=Games&query=" +
                                            url_encoding::component(context.title))) {
            if (const auto* results = found->get("Results"); results && results->type == json::Value::Type::array)
                for (const auto& family : results->elements)
                    if (const auto* list = family.get("Products"); list && list->type == json::Value::Type::array)
                        for (const auto& product : list->elements) {
                            const auto id = field(&product, "ProductId");
                            if (!id.empty() && std::none_of(hits.begin(), hits.end(), [&](const Hit& hit) { return hit.id == id; }))
                                hits.push_back({id, wide(field(&product, "Title"))});
                        }
        }
        for (const auto* hit : matching(hits, context.title, [](const Hit& item) { return item.title; }, 3)) ids.push_back(hit->id);
    }
    if (ids.empty()) { out.page.error = tr(L"Microsoft Store nie zna tej gry."); return out.page; }
    std::wstring joined;
    for (const auto& id : ids) joined += (joined.empty() ? L"" : L",") + wide(id);
    const auto found = getJson(get, L"https://displaycatalog.mp.microsoft.com/v7.0/products?market=US&languages=en-US&bigIds=" + joined);
    const auto* products = found ? found->get("Products") : nullptr;
    // The store's purposes per artwork slot, best first.
    std::vector<std::string> purposes;
    switch (kind) {
    case ArtworkKind::cover: purposes = {"BoxArt", "FeaturePromotionalSquareArt"}; break;
    case ArtworkKind::poster: purposes = {"Poster", "BrandedKeyArt"}; break;
    case ArtworkKind::logo: purposes = {}; break; // Its logos are opaque squares, never drawn over art.
    case ArtworkKind::landscape: purposes = {"TitledHeroArt", "SuperHeroArt", "Screenshot"}; break;
    default: purposes = {"SuperHeroArt", "TitledHeroArt", "Screenshot"}; break;
    }
    if (products && products->type == json::Value::Type::array)
        for (const auto& purpose : purposes)
            for (const auto& product : products->elements) {
                const auto* localized = product.get("LocalizedProperties");
                if (!localized || localized->type != json::Value::Type::array || localized->elements.empty()) continue;
                const auto& properties = localized->elements.front();
                const auto title = wide(field(&properties, "ProductTitle"));
                if (const auto* images = properties.get("Images"); images && images->type == json::Value::Type::array)
                    for (const auto& image : images->elements) {
                        if (field(&image, "ImagePurpose") != purpose) continue;
                        const auto uri = secure(field(&image, "Uri"));
                        if (uri.empty()) continue;
                        const int width = number(&image, "Width"), height = number(&image, "Height");
                        const bool wide_art = kind == ArtworkKind::hero || kind == ArtworkKind::landscape;
                        out.add(wide_art && width > 2560 ? uri + L"?w=2560" : uri, uri + L"?w=480", L"Xbox · " + wide(purpose) + (title.empty() ? L"" : L" · " + title),
                                width, height);
                    }
            }
    if (out.page.assets.empty()) out.page.error = kind == ArtworkKind::logo ? tr(L"Microsoft Store ma tylko nieprzezroczyste logo.") : tr(L"Microsoft Store nie ma grafik tej gry.");
    return out.page;
}

// --- Nintendo eShop (Europe) -------------------------------------------------
GridPage nintendo(ArtworkKind kind, const Context& context, const ArtworkHttp& get) {
    Collector out{ArtworkProvider::nintendo};
    if (kind == ArtworkKind::poster || kind == ArtworkKind::logo) { out.page.error = tr(L"Nintendo eShop ma tylko kwadratowe i szerokie grafiki."); return out.page; }
    struct Doc { std::wstring title; std::string square, square_small, wide2x1, wide16x9; };
    std::vector<Doc> docs;
    if (const auto found = getJson(get, L"https://searching.nintendo-europe.com/en/select?fq=type:GAME&rows=12&wt=json&q=" + url_encoding::component(context.title))) {
        const auto* response = found->get("response");
        if (const auto* list = response ? response->get("docs") : nullptr; list && list->type == json::Value::Type::array)
            for (const auto& doc : list->elements)
                docs.push_back({wide(field(&doc, "title")), field(&doc, "image_url"), field(&doc, "image_url_sq_s"),
                                field(&doc, "image_url_h2x1_s"), field(&doc, "image_url_h16x9_s")});
    }
    // 500 px wide pictures; their 1600 px versions where the CDN has them (the 500 stays the fallback).
    const auto larger = [](const std::string& url) {
        auto bigger = url;
        if (const auto at = bigger.rfind("500w"); at != std::string::npos) bigger.replace(at, 4, "1600w");
        return bigger;
    };
    for (const auto* doc : matching(docs, context.title, [](const Doc& item) { return item.title; }, 3)) {
        const auto author = L"Nintendo eShop · " + doc->title;
        if (kind == ArtworkKind::cover) {
            out.add(secure(doc->square), {}, author);
            out.add(secure(doc->square_small), {}, author);
        } else {
            for (const auto* url : {&doc->wide2x1, &doc->wide16x9})
                if (!url->empty()) out.add(secure(larger(*url)), secure(*url), author);
        }
    }
    if (out.page.assets.empty()) out.page.error = tr(L"Nintendo eShop nie zna tej gry.");
    return out.page;
}

// --- GameTDB -----------------------------------------------------------------
// "<ID> = <Title>" lines of <platform>tdb.txt, kept for 30 days.
std::vector<std::pair<std::string, std::wstring>> gametdbTitles(const std::wstring& platform, const Context& context, const ArtworkHttp& get) {
    static std::mutex guard;
    std::lock_guard lock(guard);
    const auto folder = context.config / L"artwork" / L"gametdb";
    const auto file = folder / (platform + L".txt");
    std::error_code ec;
    std::string body;
    const auto age = std::filesystem::exists(file, ec) ? std::chrono::file_clock::now() - std::filesystem::last_write_time(file, ec) : std::chrono::file_clock::duration::max();
    if (!ec && age < std::chrono::hours(24 * 30)) {
        std::ifstream input(file, std::ios::binary);
        body.assign(std::istreambuf_iterator<char>(input), {});
    }
    if (body.empty()) {
        const auto response = get(L"https://www.gametdb.com/" + platform + L"tdb.txt?LANG=EN", L"Accept: text/plain\r\n", 16 * 1024 * 1024);
        if (response.status == 200 && !response.body.empty()) {
            body = text(response);
            std::filesystem::create_directories(folder, ec);
            atomic_file::write(file, response.body);
        } else if (std::filesystem::exists(file, ec)) { // Stale beats nothing.
            std::ifstream input(file, std::ios::binary);
            body.assign(std::istreambuf_iterator<char>(input), {});
        }
    }
    std::vector<std::pair<std::string, std::wstring>> titles;
    size_t start = 0;
    while (start < body.size()) {
        auto end = body.find('\n', start);
        if (end == std::string::npos) end = body.size();
        std::string_view line(body.data() + start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto equal = line.find(" = ");
        if (equal == std::string_view::npos || equal == 0 || equal > 8 || line.starts_with("TITLES")) continue;
        titles.emplace_back(std::string(line.substr(0, equal)), wide(line.substr(equal + 3)));
    }
    return titles;
}
std::vector<std::wstring> gametdbRegions(const std::wstring& platform, const std::string& id) {
    if (platform == L"switch") return {L"US", L"EN"};
    const char region = id.size() >= 4 ? id[3] : 'E';
    switch (region) {
    case 'E': return {L"US"};
    case 'J': return {L"JA"};
    case 'K': return {L"KO"};
    case 'P': return {L"EN", L"FR", L"DE"};
    case 'D': return {L"DE"};
    case 'F': return {L"FR"};
    default: return {L"EN", L"US"};
    }
}
GridPage gametdb(const Game& game, ArtworkKind kind, const Context& context, const ArtworkHttp& get) {
    Collector out{ArtworkProvider::gametdb};
    const auto platform = gametdbPlatform(game);
    if (platform.empty()) { out.page.error = tr(L"GameTDB ma tylko gry konsol Nintendo."); return out.page; }
    if (kind == ArtworkKind::hero || kind == ArtworkKind::landscape || kind == ArtworkKind::logo) {
        out.page.error = tr(L"GameTDB ma okładki i pudełka, bez teł i logo."); return out.page;
    }
    std::vector<std::string> ids;
    if (platform == L"wii" && !game.rom.empty()) if (auto id = wiiDiscId(game.rom); !id.empty()) ids.push_back(std::move(id));
    if (ids.empty()) {
        const auto titles = gametdbTitles(platform, context, get);
        if (titles.empty()) { out.page.error = tr(L"Nie można pobrać listy gier GameTDB."); return out.page; }
        std::vector<std::pair<int, std::string>> scored;
        for (const auto& [id, name] : titles) if (const int score = gridTitleScore(context.title, name); score > 0) scored.emplace_back(score, id);
        std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& [score, id] : scored) if (ids.size() < 6) ids.push_back(id);
    }
    if (ids.empty()) { out.page.error = tr(L"GameTDB nie zna tej gry."); return out.page; }
    // Each console's picture types, best first, with their extension.
    std::vector<std::pair<std::wstring, std::wstring>> types;
    if (platform == L"wii") types = kind == ArtworkKind::poster ? decltype(types){{L"cover", L"png"}} : decltype(types){{L"cover3D", L"png"}, {L"cover", L"png"}, {L"disc", L"png"}};
    else if (platform == L"wiiu") types = {{L"coverHQ", L"jpg"}, {L"coverM", L"jpg"}, {L"cover", L"jpg"}};
    else if (platform == L"switch") types = {{L"coverHQ", L"jpg"}, {L"coverM", L"jpg"}, {L"cover", L"jpg"}};
    else types = {{L"coverHQ", L"jpg"}, {L"coverM", L"jpg"}, {L"box", L"png"}};
    for (const auto& id : ids)
        for (const auto& region : gametdbRegions(platform, id))
            for (const auto& [type, extension] : types) {
                const auto url = L"https://art.gametdb.com/" + platform + L"/" + type + L"/" + region + L"/" + wide(id) + L"." + extension;
                const auto small = platform == L"wii" ? url : L"https://art.gametdb.com/" + platform + L"/cover/" + region + L"/" + wide(id) + L"." + extension;
                out.add(url, small, L"GameTDB · " + type + L" · " + wide(id) + L" (" + region + L")");
            }
    return out.page;
}

// --- TitleDB (api.nlib.cc) ----------------------------------------------------
GridPage titledb(const Game& game, ArtworkKind kind, const Context& context, const ArtworkHttp& get) {
    Collector out{ArtworkProvider::titledb};
    auto id = switchTitleId(game.rom.filename().wstring());
    if (id.empty()) id = switchTitleId(context.title);
    if (id.empty()) id = switchTitleId(game.name);
    if (id.empty()) { out.page.error = tr(L"Brak ID tytułu Switch (16 znaków, np. [0100000000010000]) w nazwie pliku gry lub w polu wyszukiwania."); return out.page; }
    if (kind == ArtworkKind::poster || kind == ArtworkKind::logo) { out.page.error = tr(L"TitleDB ma ikonę, baner i zrzuty ekranu."); return out.page; }
    const auto found = getJson(get, L"https://api.nlib.cc/nx/" + wide(id));
    if (!found) { out.page.error = tr(L"TitleDB nie zna tytułu ") + wide(id) + L"."; return out.page; }
    const auto title = wide(field(&*found, "name"));
    const auto author = L"TitleDB · " + (title.empty() ? wide(id) : title);
    if (kind == ArtworkKind::cover) out.add(secure(field(&*found, "icon")), {}, author);
    else {
        out.add(secure(field(&*found, "banner")), {}, author);
        if (const auto* screens = found->get("screens"))
            if (const auto* list = screens->get("screenshots"); list && list->type == json::Value::Type::array)
                for (const auto& shot : list->elements) if (shot.type == json::Value::Type::string) out.add(secure(shot.text), {}, author);
    }
    if (out.page.assets.empty()) out.page.error = tr(L"Brak grafik.");
    return out.page;
}

// --- TheGamesDB -----------------------------------------------------------------
GridPage thegamesdb(ArtworkKind kind, const Context& context, const ArtworkHttp& get) {
    Collector out{ArtworkProvider::thegamesdb};
    if (context.thegamesdb_key.empty()) { out.page.error = tr(L"Dodaj klucz TheGamesDB w Ustawieniach → Media i dane → Klucze usług."); return out.page; }
    const auto key = url_encoding::component(context.thegamesdb_key);
    struct Hit { std::string id; std::wstring title; };
    std::vector<Hit> hits;
    const auto search = getJson(get, L"https://api.thegamesdb.net/v1.1/Games/ByGameName?apikey=" + key + L"&name=" + url_encoding::component(context.title));
    if (!search) { out.page.error = tr(L"TheGamesDB nie odpowiada (sprawdź klucz)."); return out.page; }
    if (const auto* data = search->get("data"))
        if (const auto* games = data->get("games"); games && games->type == json::Value::Type::array)
            for (const auto& item : games->elements)
                if (const int id = number(&item, "id")) hits.push_back({std::to_string(id), wide(field(&item, "game_title"))});
    std::wstring ids;
    for (const auto* hit : matching(hits, context.title, [](const Hit& item) { return item.title; }, 3)) ids += (ids.empty() ? L"" : L",") + wide(hit->id);
    if (ids.empty()) { out.page.error = tr(L"TheGamesDB nie zna tej gry."); return out.page; }
    const auto images = getJson(get, L"https://api.thegamesdb.net/v1/Games/Images?apikey=" + key + L"&games_id=" + ids);
    const auto* data = images ? images->get("data") : nullptr;
    const auto* base = data ? data->get("base_url") : nullptr;
    const auto original = field(base, "original"), medium = field(base, "medium");
    const auto* lists = data ? data->get("images") : nullptr;
    if (!lists || lists->type != json::Value::Type::object || original.empty()) { out.page.error = tr(L"Brak grafik."); return out.page; }
    const auto wanted = [&](const json::Value& image) {
        const auto type = field(&image, "type"), side = field(&image, "side");
        switch (kind) {
        case ArtworkKind::cover: case ArtworkKind::poster: return type == "boxart" && side != "back";
        case ArtworkKind::logo: return type == "clearlogo";
        case ArtworkKind::landscape: return type == "banner" || type == "fanart" || type == "screenshot";
        default: return type == "fanart" || type == "screenshot" || type == "titlescreen";
        }
    };
    for (const auto& [game_id, list] : lists->properties) {
        if (list.type != json::Value::Type::array) continue;
        for (const auto& image : list.elements) {
            const auto file = field(&image, "filename");
            if (file.empty() || !wanted(image)) continue;
            out.add(secure(original + file), medium.empty() ? std::wstring{} : secure(medium + file),
                    L"TheGamesDB · " + wide(field(&image, "type")) + L" · " + wide(game_id));
        }
    }
    if (out.page.assets.empty()) out.page.error = tr(L"Brak grafik.");
    return out.page;
}

// --- Wikimedia Commons ----------------------------------------------------------------
constexpr const wchar_t* wikimedia_headers = L"Accept: application/json\r\nApi-User-Agent: launchme (https://github.com/uxandai/launchme)\r\n";
GridPage wikimedia(ArtworkKind kind, const Context& context, const ArtworkHttp& get) {
    Collector out{ArtworkProvider::wikimedia};
    const auto found = getJson(get, L"https://commons.wikimedia.org/w/api.php?action=query&format=json&generator=search&gsrnamespace=6&gsrlimit=40"
                                    L"&prop=imageinfo&iiprop=url%7Csize%7Cmime&iiurlwidth=480&gsrsearch=" +
                                    url_encoding::component(context.title + (kind == ArtworkKind::logo ? L" logo" : L"")),
                               wikimedia_headers);
    const auto* query = found ? found->get("query") : nullptr;
    const auto* pages = query ? query->get("pages") : nullptr;
    if (!pages || pages->type != json::Value::Type::object) { out.page.error = tr(L"Wikimedia Commons nie ma zdjęć tej gry."); return out.page; }
    std::vector<std::pair<int, const json::Value*>> ordered;
    for (const auto& [id, page] : pages->properties) ordered.emplace_back(number(&page, "index"), &page);
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [index, page] : ordered) {
        const auto* infos = page->get("imageinfo");
        if (!infos || infos->type != json::Value::Type::array || infos->elements.empty()) continue;
        const auto& info = infos->elements.front();
        const auto mime = field(&info, "mime");
        if (mime != "image/jpeg" && mime != "image/png") continue;
        if (kind == ArtworkKind::logo && mime != "image/png") continue;
        const int width = number(&info, "width"), height = number(&info, "height");
        if (!width || !height) continue;
        if ((kind == ArtworkKind::hero || kind == ArtworkKind::landscape) && width < height * 1.2f) continue;
        if (kind == ArtworkKind::poster && height < width) continue;
        auto title = wide(field(page, "title"));
        if (title.starts_with(L"File:")) title.erase(0, 5);
        out.add(secure(field(&info, "url")), secure(field(&info, "thumburl")), L"Wikimedia · " + title, width, height);
    }
    if (out.page.assets.empty()) out.page.error = tr(L"Wikimedia Commons nie ma zdjęć tej gry.");
    return out.page;
}
}

const char* slug(ArtworkProvider provider) {
    switch (provider) {
    case ArtworkProvider::steamgriddb: return "steamgriddb";
    case ArtworkProvider::screenscraper: return "screenscraper";
    case ArtworkProvider::esde: return "esde";
    case ArtworkProvider::steam: return "steam";
    case ArtworkProvider::gog: return "gog";
    case ArtworkProvider::xbox: return "xbox";
    case ArtworkProvider::nintendo: return "nintendo";
    case ArtworkProvider::gametdb: return "gametdb";
    case ArtworkProvider::titledb: return "titledb";
    case ArtworkProvider::thegamesdb: return "thegamesdb";
    case ArtworkProvider::wikimedia: return "wikimedia";
    case ArtworkProvider::wallhaven: return "wallhaven";
    case ArtworkProvider::danbooru: return "danbooru";
    case ArtworkProvider::motionbgs: return "motionbgs";
    case ArtworkProvider::steam_backgrounds: return "steam_backgrounds";
    }
    return "";
}
const wchar_t* name(ArtworkProvider provider) {
    switch (provider) {
    case ArtworkProvider::steamgriddb: return L"SteamGridDB";
    case ArtworkProvider::screenscraper: return L"ScreenScraper";
    case ArtworkProvider::esde: return L"ES-DE";
    case ArtworkProvider::steam: return L"Steam";
    case ArtworkProvider::gog: return L"GOG";
    case ArtworkProvider::xbox: return L"Xbox / Microsoft Store";
    case ArtworkProvider::nintendo: return L"Nintendo eShop";
    case ArtworkProvider::gametdb: return L"GameTDB";
    case ArtworkProvider::titledb: return L"TitleDB";
    case ArtworkProvider::thegamesdb: return L"TheGamesDB";
    case ArtworkProvider::wikimedia: return L"Wikimedia Commons";
    case ArtworkProvider::wallhaven: return L"Wallhaven";
    case ArtworkProvider::danbooru: return L"Danbooru";
    case ArtworkProvider::motionbgs: return L"MotionBGs";
    case ArtworkProvider::steam_backgrounds: return L"Steam";
    }
    return L"";
}
const wchar_t* description(ArtworkProvider provider) {
    switch (provider) {
    case ArtworkProvider::steamgriddb: return trKey(L"Okładki, tła, logo, plakaty i kafle od społeczności.");
    case ArtworkProvider::screenscraper: return trKey(L"Pudełka, płyty, logo i zrzuty gier z emulatorów, dopasowane po ROM-ie.");
    case ArtworkProvider::esde: return trKey(L"Grafiki pobrane wcześniej przez ES-DE, z dysku.");
    case ArtworkProvider::steam: return trKey(L"Grafiki biblioteki sklepu Steam i zrzuty ekranu, także dla gier z innych sklepów.");
    case ArtworkProvider::gog: return trKey(L"Okładki, logo, tła i zrzuty ze sklepu GOG.");
    case ArtworkProvider::xbox: return trKey(L"Pudełka, plakaty, tła i zrzuty z Microsoft Store.");
    case ArtworkProvider::nintendo: return trKey(L"Kwadratowe i szerokie grafiki z Nintendo eShop.");
    case ArtworkProvider::gametdb: return trKey(L"Okładki i pudełka gier Wii, GameCube, Wii U, Switch, DS i 3DS.");
    case ArtworkProvider::titledb: return trKey(L"Ikona, baner i zrzuty gier Switch (ID tytułu w nazwie pliku).");
    case ArtworkProvider::thegamesdb: return trKey(L"Pudełka, tła, banery, logo i zrzuty z bazy TheGamesDB.");
    case ArtworkProvider::wikimedia: return trKey(L"Zdjęcia na wolnych licencjach z Wikimedia Commons.");
    default: return trKey(L"Tapety.");
    }
}
bool needsKey(ArtworkProvider provider) {
    return provider == ArtworkProvider::steamgriddb || provider == ArtworkProvider::thegamesdb || provider == ArtworkProvider::screenscraper;
}
bool own(ArtworkProvider provider) {
    switch (provider) {
    case ArtworkProvider::steam: case ArtworkProvider::gog: case ArtworkProvider::xbox: case ArtworkProvider::nintendo:
    case ArtworkProvider::gametdb: case ArtworkProvider::titledb: case ArtworkProvider::thegamesdb: case ArtworkProvider::wikimedia: return true;
    default: return false;
    }
}
bool applicable(ArtworkProvider provider, const Game& game) {
    const bool emulated = emulatedGame(game);
    switch (provider) {
    case ArtworkProvider::screenscraper: case ArtworkProvider::esde: return emulated;
    case ArtworkProvider::steam: case ArtworkProvider::gog: case ArtworkProvider::xbox: return !emulated;
    case ArtworkProvider::gametdb: return emulated && !gametdbPlatform(game).empty();
    case ArtworkProvider::titledb: return emulated && gametdbPlatform(game) == L"switch";
    case ArtworkProvider::steamgriddb: case ArtworkProvider::nintendo: case ArtworkProvider::thegamesdb: case ArtworkProvider::wikimedia: return true;
    default: return false;
    }
}

GridPage browse(ArtworkProvider provider, const Game& game, ArtworkKind kind, const Context& context, const ArtworkHttp& get) {
    GridPage page;
    switch (provider) {
    case ArtworkProvider::steam: page = steam(game, kind, context, get); break;
    case ArtworkProvider::gog: page = gog(game, kind, context, get); break;
    case ArtworkProvider::xbox: page = xbox(game, kind, context, get); break;
    case ArtworkProvider::nintendo: page = nintendo(kind, context, get); break;
    case ArtworkProvider::gametdb: page = gametdb(game, kind, context, get); break;
    case ArtworkProvider::titledb: page = titledb(game, kind, context, get); break;
    case ArtworkProvider::thegamesdb: page = thegamesdb(kind, context, get); break;
    case ArtworkProvider::wikimedia: page = wikimedia(kind, context, get); break;
    default: page.error = tr(L"Brak grafik."); break;
    }
    page.provider = provider;
    if (page.assets.empty() && page.error.empty()) page.error = tr(L"Brak grafik.");
    return page;
}

std::vector<uint8_t> download(ArtworkProvider provider, const GridAsset& asset, bool thumbnail, const ArtworkHttp& get) {
    const std::wstring headers = provider == ArtworkProvider::wikimedia ?
        L"Accept: image/png, image/jpeg\r\nApi-User-Agent: launchme (https://github.com/uxandai/launchme)\r\n" : L"Accept: image/png, image/jpeg\r\n";
    for (const auto& address : {thumbnail ? asset.thumbnail : asset.url, thumbnail ? asset.url : asset.thumbnail}) {
        if (address.empty() || !address.starts_with(L"https://")) continue;
        auto response = get(address, headers, 20 * 1024 * 1024);
        if (response.status == 200 && decodableArtwork(response.body)) return std::move(response.body);
    }
    return {};
}

std::string switchTitleId(std::wstring_view text) {
    for (size_t i = 0; i + 16 <= text.size(); ++i) {
        if (text[i] != L'0' || text[i + 1] != L'1') continue;
        if (i > 0 && std::iswxdigit(text[i - 1])) continue;
        if (i + 16 < text.size() && std::iswxdigit(text[i + 16])) continue;
        bool hex = true;
        for (size_t j = i; j < i + 16 && hex; ++j) hex = std::iswxdigit(text[j]) != 0;
        if (!hex) continue;
        std::string id;
        for (size_t j = i; j < i + 16; ++j) id += static_cast<char>(std::towupper(text[j]));
        // An update (…800) or a DLC: its base game's title.
        id.replace(13, 3, "000");
        return id;
    }
    return {};
}

std::string wiiDiscId(const std::filesystem::path& rom) {
    auto extension = rom.extension().wstring();
    for (auto& ch : extension) ch = static_cast<wchar_t>(std::towlower(ch));
    std::streamoff offset = 0;
    if (extension == L".wbfs") offset = 0x200;
    else if (extension != L".iso" && extension != L".gcm") return {};
    std::ifstream input(rom, std::ios::binary);
    if (!input.seekg(offset)) return {};
    char id[6]{};
    if (!input.read(id, 6)) return {};
    std::string result(id, 6);
    for (const char ch : result) if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) return {};
    return result;
}
}
