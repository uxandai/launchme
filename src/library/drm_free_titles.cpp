// DRM-free games' names (src/library/drm_free.hpp): folder names cleaned into titles, engine
// products told apart from real names, and RAWG matching for the catalog's games (identify).
#include "drm_free.hpp"
#include "../platform/unix_time.hpp"
#include "drm_free_internal.hpp"
#include "game_ratings.hpp"
#include "../artwork/cover_cache.hpp"
#include "../artwork/identified_artwork.hpp"
#include "../net/json.hpp"
#include "../net/url_encoding.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../store/steam_app_match.hpp"
#include "metadata_sources.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cwctype>
#include <fstream>
#include <regex>
#include <thread>
#include <unordered_set>

namespace drm_free {
using namespace detail;
namespace {

std::wstring joined(const std::vector<std::wstring>& list) {
    std::wstring result;
    for (const auto& word : list) result += word;
    return result;
}
// The words that tell titles apart: no articles, "and" (also written "&"), "of", "edition".
std::vector<std::wstring> significant(const std::vector<std::wstring>& list) {
    std::vector<std::wstring> result;
    for (const auto& word : list)
        if (word != L"the" && word != L"a" && word != L"an" && word != L"of" && word != L"and" && word != L"edition") result.push_back(word);
    return result.empty() ? list : result;
}
// A sequel's or an episode's number ("2", "II" -> "2"), not a year: titles that differ in one are other games.
std::vector<std::wstring> numbers(const std::vector<std::wstring>& list) {
    std::vector<std::wstring> result;
    for (const auto& word : list)
        if (word.size() <= 3 && word.find_first_not_of(L"0123456789") == std::wstring::npos) result.push_back(word);
    std::sort(result.begin(), result.end());
    return result;
}
} // namespace

// --- Titles --------------------------------------------------------------------

Title cleanTitle(std::wstring_view folder_name) {
    Title result;
    // "Name.Of.The.Game-GROUP": no spaces at all means dots and dashes are separators.
    const bool scene = trim(folder_name).find(L' ') == std::wstring::npos;
    // Bracketed groups are release noise; a year inside one is a matching hint.
    std::wstring value;
    int depth = 0;
    std::wstring group;
    for (const wchar_t ch : folder_name) {
        if (ch == L'(' || ch == L'[' || ch == L'{') { if (depth++ == 0) group.clear(); continue; }
        if ((ch == L')' || ch == L']' || ch == L'}') && depth > 0) {
            if (--depth == 0) {
                static const std::wregex year(L"(?:^|[^0-9])((?:19|20)[0-9]{2})(?:[^0-9]|$)");
                std::wsmatch match;
                if (!result.year && std::regex_search(group, match, year)) result.year = std::stoi(match[1].str());
                value += L' ';
            }
            continue;
        }
        if (depth) group += ch; else value += ch;
    }
    // Versions and build numbers go before dots turn into word separators.
    const auto erase = [&](const wchar_t* pattern) {
        const std::wregex expression(pattern, std::regex::icase);
        value = std::regex_replace(value, expression, L" ");
    };
    erase(L"(^|[\\s_.\\-])v\\d+(?:[._]\\d+)*[a-z]?(?=$|[\\s_.\\-])");
    erase(L"(^|[\\s_.\\-])\\d+(?:\\.\\d+){2,}[a-z]?(?=$|[\\s_.\\-])");
    erase(L"(^|[\\s_.\\-])(?:build|b|r)[\\s_.]?\\d{3,}(?=$|[\\s_.\\-])");
    erase(L"(^|[\\s_.\\-])\\d{6,}(?=$|[\\s_.\\-])");
    std::replace(value.begin(), value.end(), L'_', L' ');
    // Scene style "Name.Of.The.Game": dots are spaces, except inside an acronym (S.T.A.L.K.E.R.).
    if (scene) {
        std::vector<std::wstring> parts;
        size_t start = 0;
        for (size_t dot = value.find(L'.'); ; dot = value.find(L'.', start)) {
            parts.push_back(value.substr(start, dot == std::wstring::npos ? std::wstring::npos : dot - start));
            if (dot == std::wstring::npos) break;
            start = dot + 1;
        }
        if (parts.size() >= 2) {
            std::wstring rebuilt;
            for (size_t i = 0; i < parts.size(); ++i) {
                if (parts[i].empty()) continue;
                const bool letter = parts[i].size() == 1 && std::iswalpha(parts[i][0]);
                const bool neighbour = (i > 0 && parts[i - 1].size() == 1 && std::iswalpha(parts[i - 1][0])) ||
                                       (i + 1 < parts.size() && parts[i + 1].size() == 1 && std::iswalpha(parts[i + 1][0]));
                if (letter && neighbour) {
                    if (!rebuilt.empty() && rebuilt.back() != L'.') rebuilt += L' ';
                    rebuilt += parts[i] + L'.';
                } else {
                    if (!rebuilt.empty()) rebuilt += L' ';
                    rebuilt += parts[i];
                }
            }
            value = rebuilt;
        }
    }
    // Release group / store suffix: "Game-GOG", "Game-CODEX".
    static const std::unordered_set<std::wstring> groups{L"gog", L"codex", L"plaza", L"skidrow", L"reloaded", L"flt", L"dodi", L"fitgirl",
        L"elamigos", L"tenoke", L"rune", L"empress", L"razor1911", L"prophet", L"cpy", L"hoodlum", L"portable", L"repack", L"drmfree",
        L"drm-free", L"itch", L"itchio", L"steamrip", L"goldberg", L"dinobytes", L"i_know", L"kaos"};
    if (const auto dash = value.rfind(L'-'); dash != std::wstring::npos && dash > 0) {
        const auto suffix = trim(std::wstring_view(value).substr(dash + 1));
        // An unknown all-capitals tag only in scene-style names; "Final-Fantasy-VIII" keeps its numeral.
        const bool capitals = scene && suffix.size() >= 3 && suffix.size() <= 12 && suffix.find(L' ') == std::wstring::npos &&
            std::any_of(suffix.begin(), suffix.end(), [](wchar_t ch) { return std::iswalpha(ch); }) &&
            std::none_of(suffix.begin(), suffix.end(), [](wchar_t ch) { return std::iswlower(ch); }) &&
            suffix.find_first_not_of(L"IVXLC") != std::wstring::npos;
        if (groups.contains(lower(suffix)) || capitals) value.resize(dash);
    }
    // Words: trailing store/platform noise and a trailing year.
    std::vector<std::wstring> tokens;
    for (size_t at = 0; at < value.size();) {
        const auto start = value.find_first_not_of(L" \t", at);
        if (start == std::wstring::npos) break;
        const auto end = value.find_first_of(L" \t", start);
        tokens.push_back(value.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
        at = end == std::wstring::npos ? value.size() : end;
    }
    static const std::unordered_set<std::wstring> noise{L"gog", L"drm", L"drmfree", L"drm-free", L"portable", L"repack", L"x64", L"x86",
        L"x86_64", L"amd64", L"win64", L"win32", L"windows", L"win", L"pc", L"goty", L"multi", L"eng", L"english", L"en", L"pl", L"polish",
        L"polski", L"+dlc", L"dlc", L"dlcs", L"incl", L"update", L"hotfix", L"-", L"itch", L"installer", L"rip", L"proper", L"64bit", L"32bit"};
    while (tokens.size() > 1) {
        const auto word = lower(tokens.back());
        const bool multi = word.starts_with(L"multi") && word.size() <= 7 && word.find_first_not_of(L"0123456789", 5) == std::wstring::npos;
        if (noise.contains(word) || multi) { tokens.pop_back(); continue; }
        if (word.size() == 4 && (word.starts_with(L"19") || word.starts_with(L"20")) && word.find_first_not_of(L"0123456789") == std::wstring::npos) {
            if (!result.year) result.year = std::stoi(word);
            tokens.pop_back();
            continue;
        }
        break;
    }
    value.clear();
    for (const auto& token : tokens) { if (!value.empty()) value += L' '; value += token; }
    // "HollowKnight" → "Hollow Knight", "DOOMEternal" → "DOOM Eternal", "Portal2" → "Portal 2".
    // Mostly-capital names ("BALLxPIT", "XCOM2") keep their case boundaries.
    if (value.find(L' ') == std::wstring::npos && value.size() > 4) {
        const auto lowercase = std::count_if(value.begin(), value.end(), [](wchar_t ch) { return std::iswlower(ch) != 0; });
        const bool camel = lowercase >= 3;
        std::wstring split;
        for (size_t i = 0; i < value.size(); ++i) {
            const wchar_t ch = value[i];
            if (i > 0) {
                const wchar_t previous = value[i - 1];
                const bool next_lower = i + 1 < value.size() && std::iswlower(value[i + 1]);
                if ((camel && std::iswlower(previous) && std::iswupper(ch)) ||
                    (camel && std::iswupper(previous) && std::iswupper(ch) && next_lower && i >= 2) ||
                    (std::iswalpha(previous) && std::iswdigit(ch) && value.size() - i <= 3 && i >= 3))
                    split += L' ';
            }
            split += ch;
        }
        value = split;
    }
    result.text = trim(value, L" \t-_,:;+");
    if (result.text.empty()) result.text = trim(folder_name);
    if (result.text.size() > max_name) result.text.resize(max_name);
    return result;
}

bool genericProduct(std::wstring_view product) {
    static const std::unordered_set<std::wstring> generic{L"unity", L"unityplayer", L"unitygame", L"unrealengine", L"ue4", L"ue5", L"ue4game",
        L"ue5game", L"bootstrappackagedgame", L"godot", L"godotengine", L"gamemaker", L"gamemakerstudio", L"gamemakerstudio2", L"game",
        L"launcher", L"gamelauncher", L"electron", L"nwjs", L"renpy", L"rpgmaker", L"rpgmakermv", L"rpgmakermz", L"rpgmakervxace", L"rpgmakerxp",
        L"microsoftwindowsoperatingsystem", L"monogame", L"xna", L"love", L"java", L"python", L"dosbox", L"scummvm", L"application", L"app",
        L"windows", L"win64", L"setup", L"defold", L"cocos2dx", L"sdl", L"sfml", L"adobeair", L"flashplayer", L"shippinggame",
        L"start", L"play", L"main", L"client", L"run", L"nw", L"release", L"shipping", L"launch"};
    const auto name = ratings::normalizedTitle(product);
    if (generic.contains(name)) return true;
    for (const auto* engine : {L"kirikiri", L"scriptingplatform", L"unityplayer", L"unrealengine", L"godotengine", L"gamemakerstudio", L"rpgmaker"})
        if (name.find(engine) != std::wstring::npos) return true;
    return false;
}

// --- Matching by title -------------------------------------------------------------

int matchScore(const Title& query, const rawg::Candidate& candidate, size_t rank) {
    const auto wanted = ratings::normalizedTitle(query.text), offered = ratings::normalizedTitle(candidate.name);
    if (wanted.empty() || offered.empty()) return -1000;
    const auto query_words = words(query.text), candidate_words = words(candidate.name);
    const auto query_key = significant(query_words), candidate_key = significant(candidate_words);
    int score = 0;
    if (wanted == offered) score = 1000;
    else if (!query_key.empty() && joined(query_key) == joined(candidate_key)) score = 950; // "Tom & Jerry", "The Witcher 3: Wild Hunt".
    else {
        size_t shared = 0;
        for (const auto& word : query_key)
            if (std::find(candidate_key.begin(), candidate_key.end(), word) != candidate_key.end()) ++shared;
        // Most of the title's own words must be there: one shared word of three is another game.
        if (!shared || shared * 2 < query_key.size()) return -1000 + static_cast<int>(rank);
        const size_t total = query_key.size() + candidate_key.size() - shared;
        score = static_cast<int>(700 * shared / std::max<size_t>(1, total));
        const auto query_join = joined(query_key), candidate_join = joined(candidate_key);
        // A subtitle the folder left out ("Disco Elysium" → "… The Final Cut") costs a little; words of the
        // folder's title the candidate lacks cost more (mostly another game's: "Hollow Knight Silksong").
        if (candidate_join.starts_with(query_join)) score = std::max(score, 820 - 4 * static_cast<int>(candidate_join.size() - query_join.size()));
        else if (query_join.starts_with(candidate_join)) score = std::max(score, 780 - 10 * static_cast<int>(query_join.size() - candidate_join.size()));
        // One word names a game only as its whole title ("Alpha" is no "Alpha Protocol"), or with the year confirming it.
        if (query_key.size() == 1) score = std::min(score, min_match - 120); // With PC and its place on the list still short; a year passes it.
    }
    if (numbers(query_words) != numbers(candidate_words)) score -= 500; // "Portal" for "Portal 2", "DOOM 3" for "DOOM": another game.
    if (query.year && candidate.released.size() >= 4) {
        int year = 0;
        for (size_t i = 0; i < 4; ++i) year = year * 10 + (std::iswdigit(candidate.released[i]) ? candidate.released[i] - L'0' : 0);
        score += year == query.year ? 80 : std::abs(year - query.year) <= 1 ? 30 : -80;
    }
    if (!candidate.platforms.empty()) {
        bool pc = false;
        for (size_t at = 0; at <= candidate.platforms.size();) {
            const auto end = std::min(candidate.platforms.find(L',', at), candidate.platforms.size());
            if (trim(std::wstring_view(candidate.platforms).substr(at, end - at)) == L"PC") pc = true;
            at = end + 1;
        }
        score += pc ? 40 : -150;
    }
    const auto offered_lower = lower(candidate.name), wanted_lower = lower(query.text);
    for (const auto* edition : {L"soundtrack", L" ost", L"artbook", L"demo", L"season pass", L"dlc", L"bundle", L"expansion", L"beta", L"prologue",
                                L"playtest", L"dedicated server", L"editor", L"sdk", L"wallpaper"})
        if (offered_lower.find(edition) != std::wstring::npos && wanted_lower.find(edition) == std::wstring::npos) { score -= 300; break; }
    score += static_cast<int>(rank < 10 ? (10 - rank) * 3 : 0);
    return score;
}

std::optional<size_t> bestMatch(const Title& query, const std::vector<rawg::Candidate>& candidates) {
    std::optional<size_t> best;
    int best_score = min_match - 1;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const int score = matchScore(query, candidates[i], i);
        if (score > best_score) { best = i; best_score = score; }
    }
    return best;
}

bool needsIdentity(const Entry& entry, uint64_t now, const IdentifyLimits& limits) {
    return !entry.ignored && !entry.manual_identity && !entry.rawg_id && !entry.store_identity && !entry.executable.empty() && !entry.title.empty() &&
           (!entry.checked || now >= entry.checked + limits.retry_unmatched || entry.checked > now + 86400);
}

// --- The stores (no key) -------------------------------------------------------------

namespace {
std::string bodyOf(const HttpResponse& response) { return std::string(response.body.begin(), response.body.end()); }
// No answer, a limit or the server's trouble: try later. Any other answer is the store's word (404: it does not know it).
bool troubled(const HttpResponse& response) { return !response.status || response.status == 429 || response.status >= 500; }
constexpr const wchar_t* json_headers = L"Accept: application/json\r\n";

// Steam's store search: its apps as candidates (PC games; no year).
std::optional<std::vector<rawg::Candidate>> steamSearch(const std::wstring& query, const rawg::Get& get) {
    const auto response = get(L"https://store.steampowered.com/api/storesearch/?cc=US&l=english&term=" + url_encoding::component(std::wstring_view(query)),
                              json_headers, 512 * 1024);
    if (troubled(response)) return std::nullopt;
    const auto root = response.status == 200 ? json::parse(bodyOf(response)) : std::nullopt;
    std::vector<rawg::Candidate> result;
    const auto* items = root ? root->get("items") : nullptr;
    if (!items || items->type != json::Value::Type::array) return result;
    for (const auto& item : items->elements) {
        const auto* id = item.get("id");
        const auto* name = item.get("name");
        const auto* type = item.get("type");
        if (!id || id->type != json::Value::Type::number || id->numeric < 1 || id->numeric > 4294967295.0 || !name || name->type != json::Value::Type::string) continue;
        if (type && type->type == json::Value::Type::string && type->text != "app") continue;
        rawg::Candidate candidate;
        candidate.id = static_cast<uint32_t>(id->numeric);
        candidate.name = trim(text_encoding::wideLenient(name->text));
        if (!candidate.name.empty()) result.push_back(std::move(candidate));
    }
    return result;
}
int yearOf(const std::wstring& date) {
    int year = 0;
    for (size_t i = 0; i < 4 && i < date.size() && std::iswdigit(date[i]); ++i) year = year * 10 + (date[i] - L'0');
    return year >= 1950 && year <= 2200 ? year : 0;
}
} // namespace

Identity lookUp(const Lookup& lookup, const std::wstring& rawg_key, const IdentifyBackend& backend, const std::function<bool()>& pace) {
    Identity result;
    const auto store_get = backend.store_get ? backend.store_get : rawg::Get(httpGet);
    const auto rawg_get = backend.get ? backend.get : rawg::Get(httpGetPrivate);
    const auto request = [&] {
        if (pace && !pace()) { result.failed = true; return false; }
        return true;
    };
    // A Steam AppID the game's folder names: its store page is the game.
    const auto steamApp = [&](uint32_t app) -> bool {
        if (!request()) return false;
        const auto response = store_get(L"https://store.steampowered.com/api/appdetails?l=english&appids=" + std::to_wstring(app), json_headers, 1024 * 1024);
        if (troubled(response)) { result.failed = true; return false; }
        const auto offer = response.status == 200 ? metadata_sources::parseSteamDetails(bodyOf(response), app) : std::nullopt;
        if (!offer || offer->title.empty()) return false; // Gone from the store: matched by its title instead.
        result.source = Identity::Source::steam;
        result.name = trim(offer->title);
        result.steam_app_id = app;
        result.details = offer->details;
        result.year = yearOf(result.details.release_date);
        return true;
    };
    if (lookup.steam_app_id) {
        if (steamApp(lookup.steam_app_id) || result.failed) return result;
    }
    // GOG's product: its own name and description; Steam's page of that very name, when it has one, for the artwork.
    if (lookup.gog_id) {
        if (!request()) return result;
        const auto response = store_get(L"https://api.gog.com/products/" + std::to_wstring(lookup.gog_id) + L"?expand=description", json_headers, 1024 * 1024);
        if (troubled(response)) { result.failed = true; return result; }
        const auto root = response.status == 200 ? json::parse(bodyOf(response)) : std::nullopt;
        const auto* title = root ? root->get("title") : nullptr;
        if (title && title->type == json::Value::Type::string && !title->text.empty()) {
            result.source = Identity::Source::gog;
            result.gog_id = lookup.gog_id;
            result.name = trim(text_encoding::wideLenient(title->text));
            if (const auto* released = root->get("release_date"); released && released->type == json::Value::Type::string)
                result.details.release_date = metadata_sources::isoDate(released->text);
            if (const auto* description = root->get("description"); description && description->type == json::Value::Type::object)
                for (const char* part : {"full", "lead"})
                    if (const auto* value = description->get(part); value && value->type == json::Value::Type::string && !value->text.empty()) {
                        auto plain = metadata_sources::plainText(value->text);
                        if (plain.size() > 1800) plain.resize(1800);
                        result.details.description = std::move(plain);
                        break;
                    }
            result.year = yearOf(result.details.release_date);
            if (!request()) { result.failed = false; return result; } // Stopped: what GOG said stands.
            const auto search = store_get(L"https://store.steampowered.com/api/storesearch/?cc=US&l=english&term=" +
                                          url_encoding::component(std::wstring_view(result.name)), json_headers, 512 * 1024);
            if (search.status == 200) result.steam_app_id = matchSteamApp(result.name, bodyOf(search));
            return result;
        }
    }
    // Only a title: RAWG (with a key) and the Steam store answer it and the executable's product name; the best
    // match of all wins, RAWG's on a tie (it knows more: ratings, every store).
    struct Pick { int score = INT_MIN; rawg::Candidate candidate; bool steam = false; };
    Pick pick;
    bool rawg_usable = !rawg_key.empty();
    const auto consider = [&](const Title& query, const std::vector<rawg::Candidate>& candidates, bool steam) {
        if (const auto best = bestMatch(query, candidates)) {
            const int score = matchScore(query, candidates[*best], *best);
            if (score > pick.score || (score == pick.score && !steam && pick.steam)) pick = {score, candidates[*best], steam};
        }
    };
    std::vector<Title> queries{lookup.title};
    if (!lookup.product.empty() && ratings::normalizedTitle(lookup.product) != ratings::normalizedTitle(lookup.title.text))
        queries.push_back({lookup.product, lookup.title.year});
    for (const auto& query : queries) {
        if (query.text.empty() || pick.score >= 1000) continue;
        if (rawg_usable) {
            if (!request()) return result;
            const auto search = rawg::search(query.text, rawg_key, rawg_get);
            if (!search.error.empty()) { result.rawg_refused = true; rawg_usable = false; }
            else consider(query, search.games, false);
        }
        if (pick.score >= 1000) continue;
        if (!request()) return result;
        const auto steam = steamSearch(query.text, store_get);
        if (!steam) { result.failed = true; return result; }
        consider(query, *steam, true);
    }
    if (pick.score == INT_MIN) return result;
    if (pick.steam) {
        steamApp(pick.candidate.id);
        return result;
    }
    if (!request()) return result;
    auto details = rawg::fetch(pick.candidate.id, rawg_key, rawg_get);
    if (!details.data) { result.rawg_refused = true; return result; }
    uint32_t steam = lookup.steam_app_id;
    if (!steam) {
        if (!request()) return result;
        const auto links = rawg::stores(pick.candidate.id, rawg_key, rawg_get);
        if (!links.valid) { result.rawg_refused = true; return result; }
        steam = links.steam_app_id;
    }
    details.data->steam_app_id = steam;
    result.source = Identity::Source::rawg;
    result.rawg_id = pick.candidate.id;
    result.steam_app_id = steam;
    result.name = trim(details.data->name);
    result.details = details.data->details;
    result.year = yearOf(details.data->details.release_date);
    result.rawg = std::move(details);
    return result;
}

// --- Store details on disk ----------------------------------------------------------

namespace {
std::filesystem::path storeFile(const Game& game, const std::filesystem::path& metadata) {
    return metadata / (text_encoding::wide(gameCacheKey(game)) + L".store.json");
}
const char* sourceName(Identity::Source source) { return source == Identity::Source::steam ? "steam" : source == Identity::Source::gog ? "gog" : ""; }
} // namespace

std::optional<StoreDetails> loadStoreDetails(const Game& game, const std::filesystem::path& metadata) {
    if (gameCacheKey(game).empty()) return std::nullopt;
    std::ifstream input(storeFile(game, metadata), std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 256 * 1024) return std::nullopt;
    std::string bytes(static_cast<size_t>(input.tellg()), '\0');
    input.seekg(0);
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) return std::nullopt;
    const auto root = json::parse(bytes);
    if (!root || root->type != json::Value::Type::object) return std::nullopt;
    const auto value = [&](const char* key) {
        const auto* item = root->get(key);
        return item && item->type == json::Value::Type::string ? text_encoding::wideLenient(item->text) : std::wstring{};
    };
    StoreDetails result;
    const auto source = value("source");
    result.source = source == L"steam" ? Identity::Source::steam : source == L"gog" ? Identity::Source::gog : Identity::Source::none;
    result.id = text_encoding::utf8(value("id"));
    result.name = value("name");
    if (result.source == Identity::Source::none || result.name.empty()) return std::nullopt;
    result.details.description = value("description");
    result.details.developer = value("developer");
    result.details.publisher = value("publisher");
    result.details.release_date = value("release_date");
    result.details.genres = value("genres");
    return result;
}

bool saveStoreDetails(const Game& game, const StoreDetails& value, const std::filesystem::path& metadata) {
    if (gameCacheKey(game).empty() || value.source == Identity::Source::none) return false;
    std::string output = "{\"source\":" + json::quote(sourceName(value.source)) + ",\"id\":" + json::quote(value.id);
    const auto put = [&](const char* key, const std::wstring& text) {
        if (!text.empty()) output += std::string(",\"") + key + "\":" + json::quote(text_encoding::utf8(text));
    };
    put("name", value.name);
    put("description", value.details.description);
    put("developer", value.details.developer);
    put("publisher", value.details.publisher);
    put("release_date", value.details.release_date);
    put("genres", value.details.genres);
    output += "}\n";
    std::error_code ec;
    std::filesystem::create_directories(metadata, ec);
    return atomic_file::write(storeFile(game, metadata), output);
}

bool applyIdentity(const Game& game, const Identity& identity, const IdentifyPaths& paths, const IdentifyBackend& backend, const Cancelled& cancelled) {
    if (identity.source == Identity::Source::none) return false;
    std::error_code ec;
    if (identity.source == Identity::Source::rawg) {
        if (!identity.rawg.data || !rawg::save(game, identity.rawg, paths.metadata)) return false;
        ratings::saveIdentified(paths.ratings, game, identity.rawg.data->rating);
        std::filesystem::remove(storeFile(game, paths.metadata), ec);
    } else {
        std::filesystem::remove(paths.metadata / (text_encoding::wide(gameCacheKey(game)) + L".rawg.json"), ec);
        const StoreDetails value{identity.source,
                                 identity.source == Identity::Source::steam ? std::to_string(identity.steam_app_id) : std::to_string(identity.gog_id),
                                 identity.name, identity.details};
        if (!saveStoreDetails(game, value, paths.metadata)) return false;
    }
    if (selectedArtworkSource(game, paths.covers).id) return false; // Chosen before (or by the user): it stays.
    if (identity.steam_app_id) return selectArtworkSource(game, {identity.steam_app_id, true}, paths.covers);
    if (backend.sgdb_key.empty() || (cancelled && cancelled())) return false;
    // Several games share a title ("Revenant"): the identified year picks the right artwork.
    const auto found = searchGridGames(identity.name, backend.sgdb_key, backend.sgdb_get ? backend.sgdb_get : rawg::Get(httpGet));
    if (const auto source = matchIdentifiedArtwork(identity.name, found.games, identity.year); source.id)
        return selectArtworkSource(game, source, paths.covers);
    return false;
}

void identify(Catalog& catalog, const std::wstring& key, const IdentifyPaths& paths, const Cancelled& cancelled,
              const std::function<void(const Catalog&)>& changed, const IdentifyBackend& backend, IdentifyLimits limits) {
    const auto now = backend.now ? backend.now() : unix_time::now();
    const auto stop = [&] { return cancelled && cancelled(); };
    if (stop()) return;
    const auto wait = [&](std::chrono::milliseconds duration) {
        if (backend.wait) return backend.wait(duration) && !stop();
        const auto until = std::chrono::steady_clock::now() + duration;
        while (!stop() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return !stop();
    };
    bool requested = false;
    const auto pace = [&] {
        if (requested && !wait(limits.spacing)) return false;
        requested = true;
        return !stop();
    };
    // RAWG refused or failed a while ago: the stores alone until the pause ends.
    auto rawg_key = catalog.rawg_pause > now ? std::wstring{} : key;
    const auto rawg_waits = [&] { return !key.empty() && rawg_key.empty(); }; // RAWG would be asked, but is paused.
    size_t used = 0;
    for (size_t index = 0; index < catalog.games.size(); ++index) {
        if (used >= limits.games || stop()) return;
        if (!needsIdentity(catalog.games[index], now, limits)) continue;
        ++used;
        const auto& waiting = catalog.games[index];
        const Lookup lookup{{waiting.title, waiting.year}, waiting.product, waiting.steam_app_id, waiting.gog_id};
        auto identity = lookUp(lookup, rawg_key, backend, pace);
        if (identity.rawg_refused) {
            // RAWG refused (a key, a limit) or failed: paused; this game is asked again of the stores alone.
            catalog.rawg_pause = now + limits.pause;
            rawg_key.clear();
            if (changed) changed(catalog);
            if (!identity.failed && identity.source == Identity::Source::none && !stop()) identity = lookUp(lookup, rawg_key, backend, pace);
        }
        if (identity.failed || stop()) return; // Not marked: asked again next time.
        auto& entry = catalog.games[index];
        entry.checked = now;
        if (identity.source == Identity::Source::none) {
            if (rawg_waits()) { entry.checked = 0; continue; } // The stores alone did not know it: RAWG is asked once it answers again.
            if (changed) changed(catalog);
            continue;
        }
        const auto game = makeGame(entry);
        const bool source_changed = applyIdentity(game, identity, paths, backend, cancelled);
        auto name = identity.name;
        if (name.size() > max_name) name.resize(max_name);
        if (name.empty()) name = entry.title;
        // Automatic artwork followed the folder title; the identified game gets its own.
        if (source_changed || ratings::normalizedTitle(name) != ratings::normalizedTitle(entry.name)) removeGameArtwork(game, paths.covers, false);
        entry.rawg_id = identity.rawg_id;
        entry.store_identity = identity.source != Identity::Source::rawg;
        entry.name = name;
        if (identity.steam_app_id) entry.steam_app_id = identity.steam_app_id;
        if (identity.gog_id) entry.gog_id = identity.gog_id;
        if (changed) changed(catalog);
    }
}
} // namespace drm_free
