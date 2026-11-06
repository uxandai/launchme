#include "game_index_sources.hpp"
#include "../platform/file_io.hpp"
#include "../model/key_codec.hpp"
#include "../net/json.hpp"
#include "../platform/text_encoding.hpp"
#include "../platform/unicode_text.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace game_index {
namespace {
std::wstring wide(std::string_view text) { return text_encoding::wideLenient(text); }
std::string readFile(const std::filesystem::path& file, size_t limit) { return file_io::readText(file, limit); }
uint64_t modified(const std::filesystem::path& file) {
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(file, ec);
    if (ec) return 0;
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(time);
    const auto value = std::chrono::duration_cast<std::chrono::seconds>(system.time_since_epoch()).count();
    return value > 0 ? static_cast<uint64_t>(value) : 0;
}
std::optional<double> numeric(const json::Value* value) {
    if (!value) return std::nullopt;
    if (value->type == json::Value::Type::number && std::isfinite(value->numeric)) return value->numeric;
    if (value->type == json::Value::Type::string && !value->text.empty()) {
        char* end = nullptr;
        const double parsed = std::strtod(value->text.c_str(), &end);
        if (end == value->text.c_str() + value->text.size() && std::isfinite(parsed)) return parsed;
    }
    return std::nullopt;
}
std::wstring text(const json::Value* value) {
    return value && value->type == json::Value::Type::string ? wide(value->text) : std::wstring{};
}
// [{"name": "…"}] or ["…"]: up to `limit` distinct names.
std::vector<std::wstring> names(const json::Value* list, const char* property, size_t limit = 8) {
    std::vector<std::wstring> result;
    if (!list || list->type != json::Value::Type::array) return result;
    for (const auto& item : list->elements) {
        auto name = property && item.type == json::Value::Type::object ? text(item.get(property)) : text(&item);
        if (name.empty() || std::find(result.begin(), result.end(), name) != result.end()) continue;
        result.push_back(std::move(name));
        if (result.size() >= limit) break;
    }
    return result;
}
void addUnique(std::vector<std::wstring>& list, std::wstring value) {
    if (!value.empty() && std::find(list.begin(), list.end(), value) == list.end()) list.push_back(std::move(value));
}
// "Valve, Hidden Path Entertainment" (GameDetails' joined lists).
std::vector<std::wstring> split(std::wstring_view joined) {
    std::vector<std::wstring> result;
    size_t start = 0;
    while (start <= joined.size()) {
        auto end = joined.find(L',', start);
        if (end == std::wstring_view::npos) end = joined.size();
        auto item = joined.substr(start, end - start);
        while (!item.empty() && item.front() == L' ') item.remove_prefix(1);
        while (!item.empty() && item.back() == L' ') item.remove_suffix(1);
        if (!item.empty()) addUnique(result, std::wstring(item));
        start = end + 1;
    }
    return result;
}
// Steam's feature categories that say how a game is played, named like its store tags.
const wchar_t* categoryLabel(uint32_t id) {
    switch (id) {
    case 1: return L"Multi-player";
    case 2: return L"Single-player";
    case 9: return L"Co-op";
    case 20: return L"MMO";
    case 24: return L"Shared/Split Screen";
    case 27: return L"Cross-Platform Multiplayer";
    case 36: return L"Online PvP";
    case 38: return L"Online Co-op";
    case 39: return L"Split Screen Co-op";
    case 48: return L"LAN Co-op";
    case 49: return L"PvP";
    case 31: case 53: case 54: return L"VR";
    default: return nullptr;
    }
}
}

TagNames parseSteamTags(std::string_view json) {
    TagNames tags;
    const auto root = json::parse(json, 8 * 1024 * 1024);
    if (!root || root->type != json::Value::Type::array) return tags;
    for (const auto& item : root->elements) {
        const auto id = numeric(item.get("tagid"));
        auto name = text(item.get("name"));
        if (id && *id > 0 && *id <= UINT32_MAX && !name.empty()) tags[static_cast<uint32_t>(*id)] = std::move(name);
    }
    return tags;
}

std::wstring plainDescription(std::string_view html, size_t limit) {
    std::string plain;
    bool tag = false;
    for (const char ch : html) {
        if (ch == '<') { tag = true; if (!plain.empty() && plain.back() != ' ') plain += ' '; }
        else if (ch == '>') tag = false;
        else if (!tag) plain += ch == '\r' || ch == '\n' || ch == '\t' ? ' ' : ch;
    }
    for (const auto& [entity, replacement] : {std::pair{"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&apos;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "}}) {
        for (size_t at = plain.find(entity); at != std::string::npos; at = plain.find(entity, at + 1)) plain.replace(at, std::strlen(entity), replacement);
    }
    auto result = wide(plain);
    std::wstring collapsed;
    for (const wchar_t ch : result) {
        if (ch == L' ' && (collapsed.empty() || collapsed.back() == L' ')) continue;
        collapsed += ch;
    }
    while (!collapsed.empty() && collapsed.back() == L' ') collapsed.pop_back();
    if (collapsed.size() > limit) {
        auto cut = collapsed.rfind(L' ', limit);
        if (cut == std::wstring::npos || cut < limit / 2) cut = limit;
        unicode_text::truncate(collapsed, cut);
        collapsed += L"…";
    }
    return collapsed;
}

Patch fromAppInfo(const steam_appinfo::App& app, const TagNames& tags) {
    Patch patch;
    if (!app.name.empty()) patch.title = app.name;
    const auto release = app.original_release && (!app.steam_release || app.original_release < app.steam_release) ? app.original_release : app.steam_release;
    if (const auto date = dateFromUnix(release); date.known()) patch.release = date;
    if (app.review_percent) patch.review_percent = app.review_percent;
    if (app.review_score) patch.review_score = app.review_score;
    if (app.metacritic) patch.metacritic = app.metacritic;
    std::vector<std::wstring> genres;
    for (const auto id : app.genres) if (const auto* name = steam_appinfo::genreName(id)) addUnique(genres, name);
    if (!genres.empty()) patch.genres = std::move(genres);
    std::vector<std::wstring> labels;
    for (const auto id : app.tags) if (const auto found = tags.find(id); found != tags.end()) addUnique(labels, found->second);
    if (!labels.empty() || !tags.empty()) {
        // Features that say how it is played, after the players' own tags.
        for (const auto id : app.categories) if (const auto* label = categoryLabel(id)) addUnique(labels, label);
        if (!labels.empty()) patch.tags = std::move(labels);
    }
    if (!app.developers.empty()) patch.developers = app.developers;
    if (!app.publishers.empty()) patch.publishers = app.publishers;
    if (!app.franchises.empty()) patch.franchise = app.franchises.front();
    return patch;
}

Patch fromSteamStore(uint32_t app_id, std::string_view json) {
    Patch patch;
    const auto root = json::parse(json);
    const auto* entry = root ? root->get(std::to_string(app_id)) : nullptr;
    const auto* success = entry ? entry->get("success") : nullptr;
    const auto* data = entry ? entry->get("data") : nullptr;
    if (!data || data->type != json::Value::Type::object || !success || success->type != json::Value::Type::boolean || !success->flag) return patch;
    if (auto name = text(data->get("name")); !name.empty()) patch.title = std::move(name);
    if (const auto* description = data->get("short_description"); description && description->type == json::Value::Type::string)
        if (auto plain = plainDescription(description->text); !plain.empty()) patch.description = std::move(plain);
    if (auto list = names(data->get("developers"), nullptr); !list.empty()) patch.developers = std::move(list);
    if (auto list = names(data->get("publishers"), nullptr); !list.empty()) patch.publishers = std::move(list);
    // Genres by ID: English whatever the page's language.
    if (const auto* list = data->get("genres"); list && list->type == json::Value::Type::array) {
        std::vector<std::wstring> genres;
        for (const auto& item : list->elements) {
            const auto id = numeric(item.get("id"));
            if (const auto* name = id ? steam_appinfo::genreName(static_cast<uint32_t>(*id)) : nullptr) addUnique(genres, name);
        }
        if (!genres.empty()) patch.genres = std::move(genres);
    }
    if (const auto* release = data->get("release_date")) {
        const auto* soon = release->get("coming_soon");
        if (!(soon && soon->type == json::Value::Type::boolean && soon->flag))
            if (const auto date = parseDate(text(release->get("date"))); date.known()) patch.release = date;
    }
    if (const auto* critic = data->get("metacritic"))
        if (const auto score = numeric(critic->get("score")); score && *score > 0 && *score <= 100) patch.metacritic = static_cast<int>(*score);
    if (const auto* recommendations = data->get("recommendations"))
        if (const auto total = numeric(recommendations->get("total")); total && *total > 0 && *total < 1e9) patch.review_count = static_cast<uint32_t>(*total);
    return patch;
}

Patch fromRawg(std::string_view json) {
    Patch patch;
    const auto parsed = json::parse(json);
    const json::Value* root = parsed ? &*parsed : nullptr;
    if (root && root->get("launchme_cache")) root = root->get("response");
    if (!root || root->type != json::Value::Type::object || !numeric(root->get("id"))) return patch;
    if (auto name = text(root->get("name")); !name.empty()) { patch.title = name; patch.aliases = std::vector<std::wstring>{std::move(name)}; }
    if (const auto* description = root->get("description_raw"); description && description->type == json::Value::Type::string)
        if (auto plain = plainDescription(description->text); !plain.empty()) patch.description = std::move(plain);
    if (const auto date = parseDate(text(root->get("released"))); date.known()) patch.release = date;
    if (auto list = names(root->get("developers"), "name"); !list.empty()) patch.developers = std::move(list);
    if (auto list = names(root->get("publishers"), "name"); !list.empty()) patch.publishers = std::move(list);
    if (auto list = names(root->get("genres"), "name"); !list.empty()) patch.genres = std::move(list);
    // Tags in English only (RAWG mixes in Russian ones).
    if (const auto* list = root->get("tags"); list && list->type == json::Value::Type::array) {
        std::vector<std::wstring> tags;
        for (const auto& item : list->elements) {
            const auto* language = item.get("language");
            if (language && language->stringOr() != "eng") continue;
            addUnique(tags, text(item.get("name")));
            if (tags.size() >= 15) break;
        }
        if (!tags.empty()) patch.tags = std::move(tags);
    }
    if (const auto score = numeric(root->get("rating")), count = numeric(root->get("ratings_count")); score && count && *count > 0 && *score >= 0 && *score <= 5)
        patch.rawg_rating = static_cast<float>(*score);
    if (const auto critic = numeric(root->get("metacritic")); critic && *critic > 0 && *critic <= 100) patch.metacritic = static_cast<int>(*critic);
    return patch;
}

Patch fromRating(const ratings::Rating& rating) {
    Patch patch;
    if (rating.source == ratings::Source::steam) {
        if (rating.count) { patch.review_percent = static_cast<int>(std::lround(std::clamp(rating.score, 0.0, 100.0))); patch.review_count = rating.count; }
    } else if (rating.count && rating.score >= 0 && rating.score <= 5) patch.rawg_rating = static_cast<float>(rating.score);
    if (rating.metacritic) patch.metacritic = *rating.metacritic;
    return patch;
}

Patch fromHltb(std::string_view json, std::wstring_view title) {
    Patch patch;
    const auto root = json::parse(json);
    if (!root || root->type != json::Value::Type::object) return patch;
    // The plugin drops a cache written for another name; so does the index.
    if (!title.empty() && fold(text(root->get("name"))) != fold(title)) return patch;
    const auto selected = numeric(root->get("selected"));
    const auto* data = root->get("data");
    if (!selected || *selected <= 0 || !data || data->type != json::Value::Type::array) return patch;
    for (const auto& match : data->elements) {
        if (numeric(match.get("game_id")) != selected) continue;
        const auto minutes = [&](const char* name) -> std::optional<uint32_t> {
            const auto value = numeric(match.get(name));
            return value && *value >= 60 && *value < 1e8 ? std::optional<uint32_t>(static_cast<uint32_t>(std::lround(*value / 60))) : std::nullopt;
        };
        patch.hltb_main = minutes("comp_main");
        patch.hltb_extra = minutes("comp_plus");
        patch.hltb_complete = minutes("comp_100");
        break;
    }
    return patch;
}

Patch fromDetails(const GameDetails& details, std::wstring_view name) {
    Patch patch;
    if (!name.empty()) patch.aliases = std::vector<std::wstring>{std::wstring(name)};
    if (!details.description.empty()) patch.description = plainDescription(text_encoding::utf8Lenient(details.description));
    if (const auto date = parseDate(details.release_date); date.known()) patch.release = date;
    if (auto list = split(details.developer); !list.empty()) patch.developers = std::move(list);
    if (auto list = split(details.publisher); !list.empty()) patch.publishers = std::move(list);
    if (auto list = split(details.genres); !list.empty()) {
        // ES-DE/ScreenScraper write "Action / Platform" too.
        std::vector<std::wstring> genres;
        for (const auto& genre : list)
            if (const auto slash = genre.find(L" / "); slash != std::wstring::npos) { addUnique(genres, genre.substr(0, slash)); addUnique(genres, genre.substr(slash + 3)); }
            else addUnique(genres, genre);
        patch.genres = std::move(genres);
    }
    return patch;
}

Patch fromGogProduct(std::string_view json) {
    Patch patch;
    const auto root = json::parse(json);
    if (!root || root->type != json::Value::Type::object || !numeric(root->get("id"))) return patch;
    if (auto name = text(root->get("title")); !name.empty()) patch.title = std::move(name);
    if (const auto date = parseDate(text(root->get("release_date"))); date.known()) patch.release = date;
    if (const auto* description = root->get("description"); description && description->type == json::Value::Type::object) {
        const auto* lead = description->get("lead");
        const auto* full = description->get("full");
        const auto* best = lead && !lead->stringOr().empty() ? lead : full;
        if (best && best->type == json::Value::Type::string)
            if (auto plain = plainDescription(best->text); !plain.empty()) patch.description = std::move(plain);
    }
    return patch;
}

Patch fromEpicCatalog(std::string_view json) {
    Patch patch;
    const auto root = json::parse(json);
    if (!root || root->type != json::Value::Type::object) return patch;
    if (auto name = text(root->get("title")); !name.empty()) patch.title = std::move(name);
    if (auto description = text(root->get("description")); !description.empty())
        patch.description = plainDescription(text_encoding::utf8Lenient(description));
    if (auto developer = text(root->get("developer")); !developer.empty()) patch.developers = split(developer);
    // Release from the catalog's own fields, else its custom attributes.
    for (const char* name : {"releaseDate", "effectiveDate", "creationDate"})
        if (const auto date = parseDate(text(root->get(name))); date.known()) { patch.release = date; break; }
    if (const auto* categories = root->get("categories"); categories && categories->type == json::Value::Type::array) {
        std::vector<std::wstring> genres;
        for (const auto& item : categories->elements) {
            const auto path = text(item.get("path"));
            if (path.starts_with(L"games/") || path == L"games" || path == L"applications") continue;
            if (path.find(L'/') == std::wstring::npos && !path.empty()) addUnique(genres, path);
        }
        if (!genres.empty()) patch.genres = std::move(genres);
    }
    return patch;
}

Patch fromNintendo(std::string_view json) {
    const auto root = json::parse(json);
    if (!root || root->type != json::Value::Type::object) return {};
    GameDetails details;
    details.description = text(root->get("description"));
    details.publisher = text(root->get("publisher"));
    details.release_date = text(root->get("release"));
    details.genres = text(root->get("genres"));
    auto patch = fromDetails(details);
    if (auto name = text(root->get("title")); !name.empty() && !patch.empty()) patch.aliases = std::vector<std::wstring>{std::move(name)};
    return patch;
}

std::vector<std::pair<Source, Patch>> readCaches(const CacheFolders& folders, const CachedGame& game, bool english) {
    std::vector<std::pair<Source, Patch>> result;
    const auto add = [&](Source source, Patch patch, const std::filesystem::path& file) {
        if (patch.empty()) return;
        patch.updated = modified(file);
        result.emplace_back(source, std::move(patch));
    };
    if (game.app_id && !folders.metadata.empty()) {
        // The store page in the UI language first; the other one fills the gaps.
        const auto id = std::to_wstring(game.app_id);
        for (const auto* suffix : english ? std::array{L".en.details.json", L".details.json"} : std::array{L".details.json", L".en.details.json"}) {
            const auto file = folders.metadata / (id + suffix);
            if (auto patch = fromSteamStore(game.app_id, readFile(file, 2 * 1024 * 1024)); !patch.empty()) { add(Source::steam_store, std::move(patch), file); break; }
        }
    }
    if (!game.cache_key.empty()) {
        const auto wide_key = wide(game.cache_key);
        // The player's own RAWG identification, else what the index looked up itself.
        std::vector<std::filesystem::path> rawg;
        if (!folders.metadata.empty()) rawg.push_back(folders.metadata / (wide_key + L".rawg.json"));
        if (!folders.products.empty()) rawg.push_back(folders.products / L"rawg" / (wide_key + L".json"));
        for (const auto& file : rawg)
            if (auto patch = fromRawg(readFile(file, 1024 * 1024)); !patch.empty()) { add(Source::rawg, std::move(patch), file); break; }
        if (!folders.ratings.empty()) {
            const auto file = folders.ratings / (wide_key + L".rating.json");
            if (const auto root = json::parse(readFile(file, 16384)); root && root->get("rating") && root->get("rating")->type == json::Value::Type::object) {
                const auto* identity = root->get("identity");
                // A rating belongs to this game (renamed shortcuts carry their title in it).
                if (identity && identity->stringOr().starts_with(game.cache_key)) {
                    const auto* value = root->get("rating");
                    ratings::Rating rating;
                    rating.source = game.app_id ? ratings::Source::steam : ratings::Source::rawg;
                    rating.score = numeric(value->get("score")).value_or(-1);
                    rating.count = static_cast<uint32_t>(std::max(0.0, numeric(value->get("count")).value_or(0)));
                    if (const auto critic = numeric(value->get("metacritic")); critic && *critic > 0 && *critic <= 100) rating.metacritic = static_cast<int>(*critic);
                    if (rating.score >= 0) add(Source::reviews, fromRating(rating), file);
                }
            }
        }
        if (!folders.hltb.empty()) {
            const auto file = folders.hltb / (wide_key + L".json");
            add(Source::hltb, fromHltb(readFile(file, 128 * 1024), game.title), file);
        }
        if (game.key.starts_with("gog:") && !folders.products.empty()) {
            const auto file = folders.products / L"gog" / (wide(game.key.substr(4)) + L".json");
            add(Source::gog, fromGogProduct(readFile(file, 2 * 1024 * 1024)), file);
        }
        if (!folders.products.empty()) {
            const auto file = folders.products / L"nintendo" / (wide_key + L".json");
            add(Source::nintendo, fromNintendo(readFile(file, 256 * 1024)), file);
        }
    }
    return result;
}
}
