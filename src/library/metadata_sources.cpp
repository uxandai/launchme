#include "metadata_sources.hpp"

#include "../i18n/i18n.hpp"
#include "../net/json.hpp"
#include "../net/url_encoding.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <map>

namespace metadata_sources {
namespace {
using Type = json::Value::Type;
constexpr size_t per_source = 3; // Games kept from each source.
constexpr size_t body_limit = 4 * 1024 * 1024;

std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }
std::string lowerAscii(std::string text) {
    for (auto& ch : text) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return text;
}
std::string upperAscii(std::string text) {
    for (auto& ch : text) if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    return text;
}
std::string field(const json::Value* value, std::string_view name) {
    const auto* found = value ? value->get(name) : nullptr;
    return found && found->type == Type::string ? found->text : std::string{};
}
const json::Value* list(const json::Value* value, std::string_view name) {
    const auto* found = value ? value->get(name) : nullptr;
    return found && found->type == Type::array ? found : nullptr;
}
// ["A", "B"] or [{"name": "A"}, …] → "A, B".
std::wstring joined(const json::Value* items, std::string_view key = {}) {
    std::wstring result;
    if (!items || items->type != Type::array) return result;
    for (const auto& item : items->elements) {
        const auto text = item.type == Type::string ? item.text : field(&item, key);
        if (text.empty()) continue;
        if (!result.empty()) result += L", ";
        result += wide(text);
    }
    return result;
}
std::optional<json::Value> parsed(std::string_view body) {
    if (body.empty()) return std::nullopt;
    return json::parse(body, body_limit);
}
std::string bodyText(const HttpResponse& response) {
    return response.status == 200 ? std::string(reinterpret_cast<const char*>(response.body.data()), response.body.size()) : std::string{};
}
std::wstring clip(std::wstring text, size_t most) {
    if (text.size() > most) { text.resize(most); while (!text.empty() && text.back() == L' ') text.pop_back(); }
    return text;
}

// Plain text with its own line breaks (Nintendo, Microsoft, TitleDB): lines trimmed, at most one blank line.
std::wstring tidyText(std::string_view input) {
    const auto text = wide(input);
    std::wstring result, line;
    int blanks = 0;
    const auto blank = [](wchar_t ch) { return ch == L' ' || ch == L'\t' || ch == 0xA0; };
    const auto flush = [&] {
        size_t a = 0, b = line.size();
        while (a < b && blank(line[a])) ++a;
        while (b > a && blank(line[b - 1])) --b;
        if (a == b) { if (!result.empty()) ++blanks; }
        else { if (!result.empty()) result += blanks ? L"\n\n" : L"\n"; result += line.substr(a, b - a); blanks = 0; }
        line.clear();
    };
    for (const auto ch : text) { if (ch == L'\n') flush(); else if (ch != L'\r') line += ch; }
    flush();
    return result;
}

int monthOf(std::wstring_view word) {
    static constexpr std::wstring_view months[] = {L"jan", L"feb", L"mar", L"apr", L"may", L"jun", L"jul", L"aug", L"sep", L"oct", L"nov", L"dec"};
    if (word.size() < 3) return 0;
    std::wstring head(word.substr(0, 3));
    for (auto& ch : head) ch = static_cast<wchar_t>(std::towlower(ch));
    for (int i = 0; i < 12; ++i) if (head == months[i]) return i + 1;
    return 0;
}
std::wstring two(int value) { return (value < 10 ? L"0" : L"") + std::to_wstring(value); }

// A looser match than SteamGridDB's rules (which tell "Remastered" or "Deluxe" from the game): most words of
// the query are in the title; extra words count against it. 0: not the same game.
int looseScore(std::wstring_view query, std::wstring_view title) {
    const auto words = [](std::wstring_view text) {
        std::vector<std::wstring> list;
        std::wstring word;
        const auto flush = [&] {
            if (word.empty()) return;
            if (word != L"the" && word != L"a" && word != L"an" && word != L"of" && word != L"and" && word != L"edition" && word != L"for") list.push_back(word);
            word.clear();
        };
        for (const auto ch : text) { if (std::iswalnum(ch)) word += static_cast<wchar_t>(std::towlower(ch)); else flush(); }
        flush();
        return list;
    };
    const auto wanted = words(query), got = words(title);
    if (wanted.empty() || got.empty()) return 0;
    size_t matched = 0;
    for (const auto& word : wanted) if (std::find(got.begin(), got.end(), word) != got.end()) ++matched;
    const double coverage = static_cast<double>(matched) / static_cast<double>(wanted.size());
    if (coverage < 0.75) return 0;
    size_t extra = 0;
    for (const auto& word : got) if (std::find(wanted.begin(), wanted.end(), word) == wanted.end()) ++extra;
    return std::max(1, 100 + static_cast<int>(coverage * 200) - static_cast<int>(std::min<size_t>(extra, 8)) * 12);
}

// The scored, trimmed list of one source: matching games best first; with no match at all the first
// result (the list shows the title, so the player can tell).
void rank(std::vector<Offer>& offers, const Query& query, bool keep_unmatched) {
    for (auto& offer : offers) if (offer.score == 0) offer.score = std::max(std::max(0, gridTitleScore(query.title, offer.title)), looseScore(query.title, offer.title));
    std::stable_sort(offers.begin(), offers.end(), [](const Offer& a, const Offer& b) { return a.score > b.score; });
    const bool any = !offers.empty() && offers.front().score > 0;
    if (any) offers.erase(std::remove_if(offers.begin(), offers.end(), [](const Offer& offer) { return offer.score <= 0; }), offers.end());
    else if (!keep_unmatched) offers.clear();
    if (offers.size() > per_source) offers.resize(per_source);
}
} // namespace

const wchar_t* name(Source source) {
    switch (source) {
    case Source::nintendo: return L"Nintendo eShop";
    case Source::steam: return L"Steam";
    case Source::gog: return L"GOG";
    case Source::xbox: return L"Microsoft Store";
    default: return L"TitleDB";
    }
}

std::string titleId(std::wstring_view text) {
    for (size_t i = 0; i + 16 <= text.size(); ++i) {
        if (text[i] != L'0' || (text[i + 1] != L'1' && text[i + 1] != L'4')) continue;
        if (i > 0 && std::iswxdigit(text[i - 1])) continue;
        if (i + 16 < text.size() && std::iswxdigit(text[i + 16])) continue;
        bool hex = true;
        for (size_t j = i; j < i + 16 && hex; ++j) hex = std::iswxdigit(text[j]) != 0;
        if (!hex) continue;
        std::string id;
        for (size_t j = i; j < i + 16; ++j) id += static_cast<char>(std::towupper(text[j]));
        id.replace(13, 3, "000"); // An update (…800) or a DLC: its base game.
        return id;
    }
    return {};
}

std::wstring plainText(std::string_view html) {
    const auto source = wide(html);
    std::wstring text;
    const auto breakLine = [&] { if (!text.empty() && text.back() != L'\n') text += L'\n'; };
    for (size_t i = 0; i < source.size();) {
        if (source[i] == L'<') {
            const auto end = source.find(L'>', i);
            if (end == std::wstring::npos) break;
            std::wstring tag = source.substr(i + 1, end - i - 1);
            for (auto& ch : tag) ch = static_cast<wchar_t>(std::towlower(ch));
            const auto name_end = tag.find_first_of(L" /\t\r\n");
            const auto word = tag.substr(tag.starts_with(L"/") ? 1 : 0, name_end == std::wstring::npos ? std::wstring::npos : name_end - (tag.starts_with(L"/") ? 1 : 0));
            if (word == L"br") { if (!text.empty()) text += L'\n'; } // Two in a row are a blank line.
            else if (word == L"p" || word == L"div" || word == L"li" || word == L"h1" || word == L"h2" || word == L"h3" || word == L"h4" || word == L"ul" || word == L"hr") {
                breakLine();
                if (tag.starts_with(L"/") && (word == L"p" || word.starts_with(L"h"))) { if (text.size() >= 1 && text.back() == L'\n') text += L'\n'; }
                if (word == L"li" && !tag.starts_with(L"/")) text += L"• ";
            }
            i = end + 1;
            continue;
        }
        if (source[i] == L'&') {
            const auto end = source.find(L';', i);
            if (end != std::wstring::npos && end - i <= 8) {
                const auto entity = source.substr(i + 1, end - i - 1);
                std::wstring replacement;
                bool known = true;
                if (entity == L"amp") replacement = L"&"; else if (entity == L"lt") replacement = L"<"; else if (entity == L"gt") replacement = L">";
                else if (entity == L"quot") replacement = L"\""; else if (entity == L"apos" || entity == L"#39") replacement = L"'";
                else if (entity == L"nbsp") replacement = L" "; else if (entity == L"reg" || entity == L"trade" || entity == L"copy") replacement = L"";
                else if (entity.size() > 1 && entity[0] == L'#') {
                    const bool hex = entity.size() > 2 && (entity[1] == L'x' || entity[1] == L'X');
                    wchar_t* stop = nullptr;
                    const auto value = std::wcstoul(entity.c_str() + (hex ? 2 : 1), &stop, hex ? 16 : 10);
                    if (stop && *stop == 0 && value > 0 && value < 0xD800) replacement = std::wstring(1, static_cast<wchar_t>(value)); else known = false;
                } else known = false;
                if (known) { text += replacement; i = end + 1; continue; }
            }
        }
        const wchar_t ch = source[i++];
        if (ch == L'\r') continue;
        if (ch == 0xA0) { text += L' '; continue; }
        if (ch == L'\n' || ch == L'\t') { if (!text.empty() && text.back() != L' ' && text.back() != L'\n') text += L' '; continue; }
        text += ch;
    }
    // Trim every line, at most one empty line between paragraphs.
    std::wstring result, line;
    int blanks = 0;
    const auto flush = [&] {
        size_t a = 0, b = line.size();
        while (a < b && (line[a] == L' ')) ++a;
        while (b > a && line[b - 1] == L' ') --b;
        if (a == b) { if (!result.empty()) ++blanks; }
        else { if (!result.empty()) result += blanks ? L"\n\n" : L"\n"; result += line.substr(a, b - a); blanks = 0; }
        line.clear();
    };
    for (const auto ch : text) { if (ch == L'\n') flush(); else line += ch; }
    flush();
    return result;
}

std::wstring isoDate(std::string_view input) {
    const auto text = wide(input);
    std::vector<int> numbers;
    std::vector<size_t> lengths;
    int month = 0;
    for (size_t i = 0; i < text.size();) {
        if (std::iswdigit(text[i])) {
            int value = 0; size_t start = i;
            while (i < text.size() && std::iswdigit(text[i]) && value < 100000) value = value * 10 + (text[i++] - L'0');
            numbers.push_back(value); lengths.push_back(i - start);
        } else if (std::iswalpha(text[i])) {
            size_t start = i;
            while (i < text.size() && std::iswalpha(text[i])) ++i;
            if (const int found = monthOf(std::wstring_view(text).substr(start, i - start)); found && !month) month = found;
        } else ++i;
    }
    int year = 0, day = 0;
    for (size_t i = 0; i < numbers.size(); ++i) if (lengths[i] == 4 && numbers[i] >= 1950 && numbers[i] <= 2100) { year = numbers[i]; break; }
    if (!year) return {};
    if (month) {
        for (size_t i = 0; i < numbers.size(); ++i) if (lengths[i] <= 2 && numbers[i] >= 1 && numbers[i] <= 31) { day = numbers[i]; break; }
    } else if (numbers.size() >= 3) {
        const bool first = numbers[0] == year; // 2026-06-25 and 2016.08.30, else 25/06/2026.
        const int m = first ? numbers[1] : numbers[1], d = first ? numbers[2] : numbers[0];
        if (m >= 1 && m <= 12 && d >= 1 && d <= 31) { month = m; day = d; }
    }
    std::wstring result = std::to_wstring(year);
    if (month) { result += L"-" + two(month); if (day) result += L"-" + two(day); }
    return result;
}

bool applicable(Source source, const Query& query) {
    return source != Source::titledb || (!query.switch_id.empty() && query.switch_id.starts_with("01"));
}

// --- Nintendo eShop (Europe) ------------------------------------------------
std::vector<Offer> parseNintendo(std::string_view body, const Query& query) {
    std::vector<Offer> offers;
    const auto root = parsed(body);
    const auto* response = root ? root->get("response") : nullptr;
    const auto* docs = list(response, "docs");
    if (!docs) return offers;
    const auto wanted = lowerAscii(query.switch_id);
    for (const auto& doc : docs->elements) {
        Offer offer;
        offer.source = Source::nintendo;
        offer.title = wide(field(&doc, "title"));
        offer.id = field(&doc, "application_id_s");
        if (offer.title.empty()) continue;
        auto description = field(&doc, "excerpt");
        if (description.empty()) description = field(&doc, "product_catalog_description_s");
        offer.details.description = tidyText(description);
        offer.details.publisher = wide(field(&doc, "publisher"));
        offer.details.genres = joined(doc.get("pretty_game_categories_txt"));
        if (const auto* released = list(&doc, "dates_released_dts"); released && !released->elements.empty() && released->elements.front().type == Type::string)
            offer.details.release_date = isoDate(released->elements.front().text);
        if (offer.details.release_date.empty()) offer.details.release_date = isoDate(field(&doc, "pretty_date_s"));
        offer.note = joined(doc.get("system_names_txt"));
        if (!wanted.empty() && lowerAscii(offer.id) == wanted) offer.score = 1000;
        offers.push_back(std::move(offer));
    }
    return offers;
}

namespace {
Found findNintendo(const Query& query, const ArtworkHttp& get) {
    Found found;
    if (!query.switch_id.empty()) {
        found.offers = parseNintendo(bodyText(get(L"https://searching.nintendo-europe.com/en/select?fq=type:GAME&rows=3&wt=json&q=application_id_s:" +
                                                  wide(lowerAscii(query.switch_id)), L"Accept: application/json\r\n", body_limit)), query);
        for (auto& offer : found.offers) offer.score = 1000;
        if (!found.offers.empty()) { found.offers.resize(1); return found; }
    }
    if (!query.title.empty()) {
        found.offers = parseNintendo(bodyText(get(L"https://searching.nintendo-europe.com/en/select?fq=type:GAME&rows=12&wt=json&q=" +
                                                  url_encoding::component(query.title), L"Accept: application/json\r\n", body_limit)), query);
        rank(found.offers, query, false);
    }
    if (found.offers.empty()) found.error = tr(L"Nintendo eShop nie zna tej gry.");
    return found;
}

// --- Steam -------------------------------------------------------------------
} // namespace
std::vector<Offer> parseSteamSearch(std::string_view body, const Query& query) {
    std::vector<Offer> offers;
    const auto root = parsed(body);
    const auto* items = list(root ? &*root : nullptr, "items");
    if (!items) return offers;
    for (const auto& item : items->elements) {
        const auto* id = item.get("id");
        if (!id || id->type != Type::number || id->numeric <= 0) continue;
        Offer offer;
        offer.source = Source::steam;
        offer.id = std::to_string(static_cast<uint32_t>(id->numeric));
        offer.title = wide(field(&item, "name"));
        if (offer.title.empty()) continue;
        offers.push_back(std::move(offer));
    }
    rank(offers, query, false);
    return offers;
}
std::optional<Offer> parseSteamDetails(std::string_view body, uint32_t app) {
    const auto root = parsed(body);
    const auto* entry = root ? root->get(std::to_string(app)) : nullptr;
    const auto* data = entry ? entry->get("data") : nullptr;
    if (!data || data->type != Type::object) return std::nullopt;
    Offer offer;
    offer.source = Source::steam;
    offer.id = std::to_string(app);
    offer.title = wide(field(data, "name"));
    offer.details.description = plainText(field(data, "short_description"));
    offer.details.developer = joined(data->get("developers"));
    offer.details.publisher = joined(data->get("publishers"));
    offer.details.genres = joined(data->get("genres"), "description");
    if (const auto* released = data->get("release_date"); released && released->type == Type::object) {
        const auto* soon = released->get("coming_soon");
        if (!(soon && soon->type == Type::boolean && soon->flag)) offer.details.release_date = isoDate(field(released, "date"));
    }
    return offer;
}
namespace {
Found findSteam(const Query& query, const ArtworkHttp& get) {
    Found found;
    const std::wstring language = query.polish ? L"polish" : L"english";
    std::vector<Offer> hits;
    if (query.steam_app) { Offer offer; offer.source = Source::steam; offer.id = std::to_string(query.steam_app); offer.score = 1000; hits.push_back(std::move(offer)); }
    else if (!query.title.empty())
        hits = parseSteamSearch(bodyText(get(L"https://store.steampowered.com/api/storesearch/?cc=US&l=english&term=" + url_encoding::component(query.title),
                                             L"Accept: application/json\r\n", body_limit)), query);
    for (const auto& hit : hits) {
        const auto app = static_cast<uint32_t>(std::stoul(hit.id));
        const auto details = parseSteamDetails(bodyText(get(L"https://store.steampowered.com/api/appdetails?appids=" + wide(hit.id) + L"&l=" + language,
                                                            L"Accept: application/json\r\n", body_limit)), app);
        if (!details) continue;
        auto offer = *details;
        offer.score = hit.score;
        if (offer.title.empty()) offer.title = hit.title;
        found.offers.push_back(std::move(offer));
    }
    if (found.offers.empty()) found.error = tr(L"Sklep Steam nie zna tej gry.");
    return found;
}

// --- GOG ---------------------------------------------------------------------
} // namespace
std::vector<Offer> parseGogCatalog(std::string_view body, const Query& query) {
    std::vector<Offer> offers;
    const auto root = parsed(body);
    const auto* products = list(root ? &*root : nullptr, "products");
    if (!products) return offers;
    for (const auto& item : products->elements) {
        Offer offer;
        offer.source = Source::gog;
        offer.id = field(&item, "id");
        offer.title = wide(field(&item, "title"));
        if (offer.id.empty() || offer.title.empty()) continue;
        offer.details.developer = joined(item.get("developers"));
        offer.details.publisher = joined(item.get("publishers"));
        offer.details.genres = joined(item.get("genres"), "name");
        offer.details.release_date = isoDate(field(&item, "releaseDate"));
        if (!query.gog_id.empty() && offer.id == query.gog_id) offer.score = 1000;
        offers.push_back(std::move(offer));
    }
    return offers;
}
std::wstring parseGogDescription(std::string_view body) {
    const auto root = parsed(body);
    if (!root) return {};
    auto text = field(&*root, "overview");
    if (plainText(text).empty()) text = field(&*root, "description");
    return plainText(text);
}
namespace {
Found findGog(const Query& query, const ArtworkHttp& get) {
    Found found;
    const std::wstring locale = query.polish ? L"pl-PL" : L"en-US";
    // The game's own product first: the catalog's answer for its id is not searchable, so its title does it.
    if (!query.title.empty())
        found.offers = parseGogCatalog(bodyText(get(L"https://catalog.gog.com/v1/catalog?limit=12&productType=in:game,pack&locale=" + locale + L"&query=like:" +
                                                     url_encoding::component(query.title), L"Accept: application/json\r\n", body_limit)), query);
    rank(found.offers, query, false);
    for (auto& offer : found.offers)
        offer.details.description = parseGogDescription(bodyText(get(L"https://api.gog.com/v2/games/" + wide(offer.id) + L"?locale=" + locale,
                                                                       L"Accept: application/json\r\n", body_limit)));
    if (found.offers.empty()) found.error = tr(L"GOG nie zna tej gry.");
    return found;
}

// --- Microsoft Store -----------------------------------------------------------
} // namespace
std::vector<std::pair<std::string, std::wstring>> parseXboxSuggest(std::string_view body) {
    std::vector<std::pair<std::string, std::wstring>> hits;
    const auto root = parsed(body);
    const auto* results = list(root ? &*root : nullptr, "Results");
    if (!results) return hits;
    for (const auto& family : results->elements) {
        const auto* products = list(&family, "Products");
        if (!products) continue;
        for (const auto& product : products->elements) {
            auto id = field(&product, "ProductId");
            if (!id.empty() && std::none_of(hits.begin(), hits.end(), [&](const auto& hit) { return hit.first == id; }))
                hits.emplace_back(std::move(id), wide(field(&product, "Title")));
        }
    }
    return hits;
}
std::vector<Offer> parseXboxProducts(std::string_view body, const Query& query) {
    std::vector<Offer> offers;
    const auto root = parsed(body);
    const auto* products = list(root ? &*root : nullptr, "Products");
    if (!products) return offers;
    for (const auto& product : products->elements) {
        const auto* localized = list(&product, "LocalizedProperties");
        if (!localized || localized->elements.empty()) continue;
        const auto& properties = localized->elements.front();
        Offer offer;
        offer.source = Source::xbox;
        offer.id = field(&product, "ProductId");
        offer.title = wide(field(&properties, "ProductTitle"));
        if (offer.title.empty()) continue;
        auto description = field(&properties, "ProductDescription");
        if (description.empty()) description = field(&properties, "ShortDescription");
        offer.details.description = tidyText(description);
        offer.details.developer = wide(field(&properties, "DeveloperName"));
        offer.details.publisher = wide(field(&properties, "PublisherName"));
        if (const auto* props = product.get("Properties")) offer.details.genres = wide(field(props, "Category"));
        if (const auto* market = list(&product, "MarketProperties"); market && !market->elements.empty())
            offer.details.release_date = isoDate(field(&market->elements.front(), "OriginalReleaseDate"));
        if (!query.xbox_product.empty() && offer.id == query.xbox_product) offer.score = 1000;
        offers.push_back(std::move(offer));
    }
    return offers;
}
namespace {
Found findXbox(const Query& query, const ArtworkHttp& get) {
    Found found;
    const std::wstring market = query.polish ? L"PL" : L"US", language = query.polish ? L"pl-PL" : L"en-US";
    std::vector<std::pair<std::string, std::wstring>> hits;
    if (!query.xbox_product.empty()) hits.emplace_back(query.xbox_product, std::wstring{});
    if (!query.title.empty())
        for (auto& hit : parseXboxSuggest(bodyText(get(L"https://displaycatalog.mp.microsoft.com/v7.0/productFamilies/autosuggest?market=" + market + L"&languages=" + language +
                                                         L"&productFamilyNames=Games&query=" + url_encoding::component(query.title), L"Accept: application/json\r\n", body_limit))))
            if (std::none_of(hits.begin(), hits.end(), [&](const auto& item) { return item.first == hit.first; })) hits.push_back(std::move(hit));
    // Editions of one game share its name: the best few are asked together.
    std::vector<std::pair<int, std::string>> scored;
    for (const auto& [id, title] : hits) scored.emplace_back(id == query.xbox_product ? 1000 : std::max(std::max(0, gridTitleScore(query.title, title)), looseScore(query.title, title)), id);
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::wstring ids;
    size_t count = 0;
    for (const auto& [score, id] : scored) { if ((score <= 0 && count) || count >= per_source + 2) break; ids += (ids.empty() ? L"" : L",") + wide(id); ++count; }
    if (!ids.empty()) found.offers = parseXboxProducts(bodyText(get(L"https://displaycatalog.mp.microsoft.com/v7.0/products?market=" + market + L"&languages=" + language + L"&bigIds=" + ids,
                                                                    L"Accept: application/json\r\n", body_limit)), query);
    rank(found.offers, query, false);
    if (found.offers.empty()) found.error = tr(L"Microsoft Store nie zna tej gry.");
    return found;
}

// --- TitleDB -----------------------------------------------------------------
} // namespace
std::optional<Offer> parseTitleDb(std::string_view body) {
    const auto root = parsed(body);
    if (!root || root->type != Type::object || field(&*root, "name").empty()) return std::nullopt;
    Offer offer;
    offer.source = Source::titledb;
    offer.id = field(&*root, "id");
    offer.title = wide(field(&*root, "name"));
    auto description = field(&*root, "description");
    if (description.empty()) description = field(&*root, "intro");
    offer.details.description = tidyText(description);
    offer.details.publisher = wide(field(&*root, "publisher"));
    offer.details.developer = wide(field(&*root, "developer"));
    offer.details.genres = joined(root->get("category"));
    offer.details.release_date = isoDate(field(&*root, "releaseDate"));
    offer.note = L"Nintendo Switch";
    return offer;
}
namespace {
Found findTitleDb(const Query& query, const ArtworkHttp& get) {
    Found found;
    if (query.switch_id.empty()) { found.error = tr(L"Brak ID tytułu Switch (16 znaków, np. [0100000000010000]) w nazwie pliku gry lub w polu wyszukiwania."); return found; }
    if (auto offer = parseTitleDb(bodyText(get(L"https://api.nlib.cc/nx/" + wide(query.switch_id), L"Accept: application/json\r\n", body_limit)))) {
        offer->score = 1000;
        found.offers.push_back(std::move(*offer));
    } else found.error = tr(L"TitleDB nie zna tytułu ") + wide(query.switch_id) + L".";
    return found;
}
} // namespace

Found find(Source source, const Query& query, const ArtworkHttp& get) {
    Found found;
    switch (source) {
    case Source::nintendo: found = findNintendo(query, get); break;
    case Source::steam: found = findSteam(query, get); break;
    case Source::gog: found = findGog(query, get); break;
    case Source::xbox: found = findXbox(query, get); break;
    default: found = findTitleDb(query, get); break;
    }
    // The dialog's description box holds this much.
    for (auto& offer : found.offers) offer.details.description = clip(std::move(offer.details.description), 4000);
    return found;
}
} // namespace metadata_sources
