#include "promos.hpp"
#include "json.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/file_io.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace promos {
namespace {
constexpr size_t max_body = 2 * 1024 * 1024;
constexpr size_t max_offers = 48;

const json::Value& at(const json::Value& value, std::string_view key) {
    static const json::Value empty;
    const auto* found = value.get(key);
    return found ? *found : empty;
}
std::wstring text(const json::Value& value, std::string_view key) {
    const auto& found = at(value, key);
    return found.type == json::Value::Type::string ? text_encoding::wideLenient(found.text) : std::wstring{};
}
bool https(std::wstring_view url) {
    return url.starts_with(L"https://") && url.size() <= 2048 && url.find_first_of(std::wstring_view(L"\0\r\n\" ", 5)) == url.npos;
}
std::wstring clean(std::wstring value) {
    for (auto& c : value) if (c == 0x00A0 || c < L' ') c = L' '; // GOG writes prices with a no-break space.
    while (!value.empty() && value.back() == L' ') value.pop_back();
    while (!value.empty() && value.front() == L' ') value.erase(value.begin());
    return value.substr(0, 256);
}
int64_t daysFromCivil(int64_t year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const auto yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}
}

int64_t parseTime(std::string_view value) {
    // YYYY-MM-DD[T ]HH:MM:SS, the rest (fraction, Z) ignored.
    if (value.size() < 19) return 0;
    const auto number = [&](size_t from, size_t count) -> int {
        int result = 0;
        for (size_t i = from; i < from + count; ++i) { if (value[i] < '0' || value[i] > '9') return -1; result = result * 10 + (value[i] - '0'); }
        return result;
    };
    const int year = number(0, 4), month = number(5, 2), day = number(8, 2), hour = number(11, 2), minute = number(14, 2), second = number(17, 2);
    if (value[4] != '-' || value[7] != '-' || (value[10] != 'T' && value[10] != ' ') || value[13] != ':' || value[16] != ':' ||
        year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60) return 0;
    return daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 + hour * 3600 + minute * 60 + second;
}

std::wstring money(int64_t cents, std::string_view currency) {
    if (cents < 0) return {};
    const auto whole = std::to_wstring(cents / 100);
    const auto part = cents % 100;
    const auto fraction = std::wstring(part < 10 ? L"0" : L"") + std::to_wstring(part);
    if (currency == "PLN") return whole + L',' + fraction + L" zł";
    if (currency == "EUR") return whole + L',' + fraction + L" €";
    if (currency == "USD") return L'$' + whole + L'.' + fraction;
    if (currency == "GBP") return L'£' + whole + L'.' + fraction;
    if (currency == "BRL") return L"R$ " + whole + L',' + fraction;
    return whole + L'.' + fraction + L' ' + text_encoding::wideLenient(currency.substr(0, 3));
}

std::wstring titleKey(std::wstring_view title) {
    std::wstring key;
    for (wchar_t c : title) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + (L'a' - L'A'));
        if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c > 0x7F) key += c;
    }
    return key;
}

std::vector<Offer> parseEpic(std::string_view body, int64_t now, const Region& region) {
    std::vector<Offer> result;
    const auto document = json::parse(body, max_body);
    if (!document) return result;
    const auto& elements = at(at(at(at(*document, "data"), "Catalog"), "searchStore"), "elements");
    for (const auto& element : elements.elements) {
        Offer offer;
        offer.store = Store::epic; offer.where = L"Epic Games";
        offer.title = clean(text(element, "title"));
        // Free through a promotion only: a 0 % "discount price" window that is open now or announced.
        const auto window = [&](const json::Value& groups, bool upcoming) {
            for (const auto& group : groups.elements)
                for (const auto& promotion : at(group, "promotionalOffers").elements) {
                    const auto& setting = at(promotion, "discountSetting");
                    if (at(setting, "discountType").stringOr() != "PERCENTAGE" || at(setting, "discountPercentage").type != json::Value::Type::number ||
                        at(setting, "discountPercentage").numeric != 0) continue;
                    const auto starts = parseTime(at(promotion, "startDate").stringOr()), ends = parseTime(at(promotion, "endDate").stringOr());
                    if (!starts || !ends || ends <= now || (upcoming ? starts <= now : starts > now)) continue;
                    offer.starts = starts; offer.ends = ends;
                    return true;
                }
            return false;
        };
        const auto& promotions = at(element, "promotions");
        const bool current = window(at(promotions, "promotionalOffers"), false);
        if (!current && !window(at(promotions, "upcomingPromotionalOffers"), true)) continue;
        if (current) offer.starts = 0;
        offer.free = true; offer.discount = 100;
        const auto& total = at(at(element, "price"), "totalPrice");
        if (at(total, "originalPrice").type == json::Value::Type::number && at(total, "originalPrice").numeric > 0)
            offer.regular = money(static_cast<int64_t>(std::llround(at(total, "originalPrice").numeric)), at(total, "currencyCode").stringOr());
        std::wstring slug;
        for (const auto& mapping : at(at(element, "catalogNs"), "mappings").elements) if (slug.empty()) slug = text(mapping, "pageSlug");
        if (slug.empty()) slug = text(element, "productSlug");
        if (slug.empty()) slug = text(element, "urlSlug");
        if (const auto home = slug.find(L"/home"); home != slug.npos) slug.erase(home);
        if (slug.empty() || slug.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != slug.npos) offer.url = L"https://store.epicgames.com/free-games";
        else offer.url = std::wstring(L"https://store.epicgames.com/") + (region.polish ? L"pl" : L"en-US") + L"/p/" + slug;
        for (const auto* wanted : {"OfferImageWide", "DieselStoreFrontWide", "Thumbnail"}) {
            for (const auto& image : at(element, "keyImages").elements)
                if (offer.image.empty() && at(image, "type").stringOr() == wanted && https(text(image, "url"))) offer.image = text(image, "url");
            if (!offer.image.empty()) break;
        }
        if (!offer.title.empty() && result.size() < max_offers) result.push_back(std::move(offer));
    }
    return result;
}

std::vector<Offer> parseSteam(std::string_view body) {
    std::vector<Offer> result;
    const auto document = json::parse(body, max_body);
    if (!document) return result;
    for (const auto& item : at(at(*document, "specials"), "items").elements) {
        const auto& id = at(item, "id");
        const auto& percent = at(item, "discount_percent");
        const auto& final_price = at(item, "final_price");
        if (id.type != json::Value::Type::number || id.numeric < 1 || id.numeric > 4294967295.0 || percent.type != json::Value::Type::number ||
            percent.numeric < 1 || percent.numeric > 100 || final_price.type != json::Value::Type::number) continue;
        Offer offer;
        offer.store = Store::steam; offer.where = L"Steam";
        offer.title = clean(text(item, "name"));
        offer.discount = static_cast<int>(percent.numeric);
        offer.free = final_price.numeric == 0;
        const auto currency = at(item, "currency").stringOr();
        if (!offer.free) offer.price = money(static_cast<int64_t>(std::llround(final_price.numeric)), currency);
        if (at(item, "original_price").type == json::Value::Type::number && at(item, "original_price").numeric > 0)
            offer.regular = money(static_cast<int64_t>(std::llround(at(item, "original_price").numeric)), currency);
        if (at(item, "discount_expiration").type == json::Value::Type::number) offer.ends = static_cast<int64_t>(at(item, "discount_expiration").numeric);
        // Type 0 is an app; 1 a package ("sub"), 2 a bundle.
        const int type = at(item, "type").intOr(0);
        offer.url = std::wstring(L"https://store.steampowered.com/") + (type == 1 ? L"sub/" : type == 2 ? L"bundle/" : L"app/") + std::to_wstring(static_cast<uint32_t>(id.numeric));
        for (const auto* wanted : {"large_capsule_image", "header_image"})
            if (offer.image.empty() && https(text(item, wanted))) offer.image = text(item, wanted);
        if (!offer.title.empty() && result.size() < max_offers) result.push_back(std::move(offer));
    }
    return result;
}

std::vector<Offer> parseGog(std::string_view body) {
    std::vector<Offer> result;
    const auto document = json::parse(body, max_body);
    if (!document) return result;
    for (const auto& product : at(*document, "products").elements) {
        const auto& price = at(product, "price");
        auto percent = text(price, "discount"); // "-75%"
        std::erase_if(percent, [](wchar_t c) { return c < L'0' || c > L'9'; });
        Offer offer;
        offer.store = Store::gog; offer.where = L"GOG";
        offer.title = clean(text(product, "title"));
        offer.url = text(product, "storeLink");
        offer.discount = percent.empty() || percent.size() > 3 ? 0 : std::min(100, std::stoi(percent));
        offer.price = clean(text(price, "final")); offer.regular = clean(text(price, "base"));
        const auto amount = at(at(price, "finalMoney"), "amount").stringOr();
        offer.free = !amount.empty() && amount.find_first_not_of("0.") == amount.npos;
        if (offer.free) { offer.discount = 100; offer.price.clear(); }
        if (https(text(product, "coverHorizontal"))) offer.image = text(product, "coverHorizontal");
        if (offer.title.empty() || offer.discount < 1 || !offer.url.starts_with(L"https://www.gog.com/") || !https(offer.url)) continue;
        if (result.size() < max_offers) result.push_back(std::move(offer));
    }
    return result;
}

std::vector<Offer> parseGamerPower(std::string_view body, int64_t now) {
    std::vector<Offer> result;
    const auto document = json::parse(body, max_body);
    if (!document || document->type != json::Value::Type::array) return result;
    for (const auto& item : document->elements) {
        if (at(item, "type").stringOr() != "Game" || at(item, "status").stringOr("Active") != "Active") continue;
        Offer offer;
        offer.free = true; offer.discount = 100;
        offer.title = clean(text(item, "title"));
        // "Flotsam (Epic Games) Giveaway", "Dwarven Realms (Steam) Key Giveaway": the game's own name.
        for (const std::wstring_view suffix : {L" Key Giveaway", L" Giveaway"})
            if (offer.title.ends_with(suffix)) offer.title.erase(offer.title.size() - suffix.size());
        if (offer.title.ends_with(L")")) if (const auto open = offer.title.rfind(L" ("); open != offer.title.npos && open > 0) offer.title.erase(open);
        offer.where = clean(text(item, "platforms"));
        if (offer.where.starts_with(L"PC, ")) offer.where.erase(0, 4);
        if (offer.where == L"PC") offer.where.clear();
        const auto lower = titleKey(offer.where);
        offer.store = lower.find(L"epic") != lower.npos ? Store::epic : lower.find(L"steam") != lower.npos ? Store::steam : lower.find(L"gog") != lower.npos ? Store::gog : Store::other;
        offer.where += (offer.where.empty() ? L"" : L" · ") + std::wstring(L"GamerPower");
        offer.url = text(item, "open_giveaway_url");
        offer.image = https(text(item, "image")) ? text(item, "image") : std::wstring{};
        offer.regular = clean(text(item, "worth"));
        if (offer.regular == L"N/A") offer.regular.clear();
        offer.ends = parseTime(at(item, "end_date").stringOr());
        if (offer.title.empty() || !offer.url.starts_with(L"https://www.gamerpower.com/") || !https(offer.url) || (offer.ends && offer.ends <= now)) continue;
        if (result.size() < max_offers) result.push_back(std::move(offer));
    }
    return result;
}

Feed combine(std::vector<Offer> epic, std::vector<Offer> steam, std::vector<Offer> gog, std::vector<Offer> giveaways, int64_t now) {
    Feed feed;
    std::set<std::wstring> seen;
    const auto add = [&](std::vector<Offer>& into, Offer offer) {
        if (!seen.insert(titleKey(offer.title) + (offer.starts > now ? L"/soon" : L"")).second) return;
        into.push_back(std::move(offer));
    };
    // The stores' own listings first: a giveaway the aggregator repeats is the same game.
    for (auto& offer : epic) add(offer.starts > now ? feed.free_soon : feed.free_now, std::move(offer));
    for (auto* list : {&steam, &gog})
        for (auto& offer : *list) if (offer.free) add(feed.free_now, offer);
    for (auto& offer : giveaways) add(feed.free_now, std::move(offer));
    // Ending soonest first; those without an end date keep their order after them.
    std::stable_sort(feed.free_now.begin(), feed.free_now.end(), [](const Offer& a, const Offer& b) { return (a.ends ? a.ends : INT64_MAX) < (b.ends ? b.ends : INT64_MAX); });
    std::stable_sort(feed.free_soon.begin(), feed.free_soon.end(), [](const Offer& a, const Offer& b) { return a.starts < b.starts; });
    // Discounts: the two stores in turn, each deepest first.
    for (auto* list : {&steam, &gog}) {
        std::erase_if(*list, [](const Offer& offer) { return offer.free; });
        std::stable_sort(list->begin(), list->end(), [](const Offer& a, const Offer& b) { return a.discount > b.discount; });
    }
    for (size_t i = 0; i < std::max(steam.size(), gog.size()) && feed.deals.size() < max_offers; ++i) {
        if (i < steam.size()) feed.deals.push_back(std::move(steam[i]));
        if (i < gog.size()) feed.deals.push_back(std::move(gog[i]));
    }
    return feed;
}

namespace {
bool code(std::string_view value, size_t length) {
    return value.size() == length && std::all_of(value.begin(), value.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
}
// A source's body: the cached one while fresh, else the network's (kept), else the stale copy.
std::string body(const std::filesystem::path& cache, const wchar_t* name, const std::wstring& url, bool refresh, int64_t now, const Get& get, bool& answered) {
    const auto file = cache / (std::wstring(name) + L".json"), stamp = cache / (std::wstring(name) + L".time");
    const auto cached = file_io::readText(file, max_body);
    int64_t fetched = 0;
    try { fetched = std::stoll(file_io::readText(stamp, 32)); } catch (...) {}
    if (!refresh && !cached.empty() && fetched <= now && now - fetched < cache_seconds) { answered = true; return cached; }
    const auto reply = get ? get(url) : httpGet(url, L"Accept: application/json\r\n", max_body);
    if (reply.status == 200 && !reply.body.empty() && !reply.truncated && json::parse(std::string_view(reinterpret_cast<const char*>(reply.body.data()), reply.body.size()), max_body)) {
        std::error_code ec;
        std::filesystem::create_directories(cache, ec);
        const std::string fresh(reply.body.begin(), reply.body.end());
        if (atomic_file::write(file, fresh)) atomic_file::write(stamp, std::to_string(now));
        answered = true;
        return fresh;
    }
    if (!cached.empty()) answered = true;
    return cached;
}
}

Region systemRegion(bool polish) {
    Region region;
    region.polish = polish;
#ifdef _WIN32
    wchar_t country[16]{}, currency[16]{};
    if (GetUserDefaultGeoName(country, static_cast<int>(std::size(country))) > 0) region.country = text_encoding::utf8Lenient(country);
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SINTLSYMBOL, currency, static_cast<int>(std::size(currency))) > 0) region.currency = text_encoding::utf8Lenient(currency);
#else
    // "pl_PL.UTF-8": the country after the underscore; its currency from a short table (else the euro).
    std::string locale;
    for (const char* name : {"LC_ALL", "LC_MONETARY", "LANG"}) if (const char* value = std::getenv(name); value && *value && locale.empty()) locale = value;
    if (const auto under = locale.find('_'); under != locale.npos && locale.size() >= under + 3) region.country = locale.substr(under + 1, 2);
    struct Money { const char* country; const char* currency; };
    static constexpr Money table[] = {{"US", "USD"}, {"PL", "PLN"}, {"GB", "GBP"}, {"BR", "BRL"}, {"CA", "CAD"}, {"AU", "AUD"}, {"CH", "CHF"},
                                      {"SE", "SEK"}, {"NO", "NOK"}, {"DK", "DKK"}, {"CZ", "CZK"}, {"JP", "JPY"}, {"MX", "MXN"}};
    region.currency = "EUR";
    for (const auto& entry : table) if (region.country == entry.country) region.currency = entry.currency;
#endif
    if (!code(region.country, 2)) region.country = "US";
    if (!code(region.currency, 3)) region.currency = "USD";
    return region;
}

std::wstring tilePicture(std::wstring url) {
    if (url.find(L'?') != url.npos) return url;
    if (url.starts_with(L"https://cdn1.epicgames.com/") || url.starts_with(L"https://cdn2.epicgames.com/")) return url + L"?h=360&resize=1&w=640";
    constexpr std::wstring_view gog = L"https://images.gog-statics.com/";
    if (url.starts_with(gog) && url.size() > gog.size() + 8 && url.find(L'_', gog.size()) == url.npos)
        if (const auto dot = url.rfind(L'.'); dot != url.npos && dot > gog.size()) return url.substr(0, dot) + L"_product_tile_extended_432x243.jpg";
    return url;
}

Feed load(const std::filesystem::path& cache, const Region& region, bool refresh, int64_t now, const Get& get) {
    const auto country = text_encoding::wideLenient(code(region.country, 2) ? region.country : "US");
    const auto currency = text_encoding::wideLenient(code(region.currency, 3) ? region.currency : "USD");
    const std::wstring language = region.polish ? L"pl" : L"en-US", steam_language = region.polish ? L"polish" : L"english";
    bool any = false;
    auto epic = parseEpic(body(cache, L"epic", L"https://store-site-backend-static.ak.epicgames.com/freeGamesPromotions?locale=" + language + L"&country=" + country +
                                                   L"&allowCountries=" + country, refresh, now, get, any), now, region);
    auto steam = parseSteam(body(cache, L"steam", L"https://store.steampowered.com/api/featuredcategories?cc=" + country + L"&l=" + steam_language, refresh, now, get, any));
    auto gog = parseGog(body(cache, L"gog", L"https://catalog.gog.com/v1/catalog?limit=48&order=desc:trending&discounted=eq:true&productType=in:game,pack&countryCode=" +
                                                country + L"&locale=" + (region.polish ? L"pl-PL" : L"en-US") + L"&currencyCode=" + currency, refresh, now, get, any));
    auto giveaways = parseGamerPower(body(cache, L"gamerpower", L"https://www.gamerpower.com/api/giveaways?platform=pc&type=game", refresh, now, get, any), now);
    auto feed = combine(std::move(epic), std::move(steam), std::move(gog), std::move(giveaways), now);
    feed.unavailable = !any;
    return feed;
}
}
