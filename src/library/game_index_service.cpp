#include "game_index_service.hpp"
#include "../platform/file_io.hpp"
#include "../platform/unix_time.hpp"
#include "steam_tags_builtin.hpp"
#include "metadata_sources.hpp"
#include "../artwork/steamgriddb_search.hpp"
#include "../net/json.hpp"
#include "../net/url_encoding.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>

namespace game_index {
namespace {
constexpr uint64_t day = 86400;
std::string readFile(const std::filesystem::path& file, size_t limit) { return file_io::readText(file, limit); }
std::string text(const HttpResponse& response) { return std::string(response.body.begin(), response.body.end()); }
uint64_t modifiedSeconds(const std::filesystem::path& file) {
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(file, ec);
    if (ec) return 0;
    const auto value = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::clock_cast<std::chrono::system_clock>(time).time_since_epoch()).count();
    return value > 0 ? static_cast<uint64_t>(value) : 0;
}
uint64_t systemNow() {
    return unix_time::now();
}
// Consoles whose games the Nintendo eShop (Europe) knows: asked before RAWG.
bool nintendoShop(std::wstring_view platform) {
    return platform == L"Switch" || platform == L"Switch 2" || platform == L"Wii U" || platform == L"3DS" || platform == L"Nintendo 3DS";
}
}

Patch libraryPatch(const LibraryGame& game) {
    Patch patch;
    patch.title = game.title;
    if (!game.store.empty()) patch.store = game.store;
    if (!game.platform.empty()) patch.platform = game.platform;
    patch.installed = game.installed;
    if (game.first_seen) patch.first_seen = game.first_seen;
    if (game.last_played) patch.last_played = game.last_played;
    if (game.playtime_minutes) patch.playtime_minutes = game.playtime_minutes;
    if (!game.own_description.empty()) patch.description = game.own_description;
    if (!game.own_developers.empty()) patch.developers = game.own_developers;
    if (!game.own_publishers.empty()) patch.publishers = game.own_publishers;
    if (!game.own_genres.empty()) patch.genres = game.own_genres;
    if (game.own_release.known()) patch.release = game.own_release;
    if (game.own_hltb_main) patch.hltb_main = game.own_hltb_main;
    return patch;
}

Service::Service(ServiceConfig config) : config_(std::move(config)) {
    if (!config_.now) config_.now = systemNow;
    if (!config_.get) config_.get = httpGet;
    if (!config_.get_private) config_.get_private = httpGetPrivate;
}

std::filesystem::path Service::file() const { return knowledge() / L"games-v1.json"; }

CacheFolders Service::folders() const {
    return {config_.config / L"metadata", config_.config / L"ratings", config_.config / L"plugins" / L"data" / L"launchme.howlongtobeat", knowledge()};
}

void Service::stop() {
    { std::lock_guard lock(inbox_mutex_); alive_ = false; }
    wake_.notify_all();
}

void Service::library(std::vector<LibraryGame> games, uint64_t steam_account) {
    { std::lock_guard lock(inbox_mutex_); inbox_.library.emplace(std::move(games), steam_account); }
    wake_.notify_all();
}

void Service::touch(std::vector<std::string> keys) {
    if (keys.empty()) return;
    { std::lock_guard lock(inbox_mutex_); inbox_.touched.insert(inbox_.touched.end(), keys.begin(), keys.end()); }
    wake_.notify_all();
}

void Service::merge(std::string key, Source source, Patch patch) {
    if (key.empty()) return;
    { std::lock_guard lock(inbox_mutex_); inbox_.patches.emplace_back(std::move(key), source, std::move(patch)); }
    wake_.notify_all();
}

void Service::pause(bool paused) {
    paused_ = paused;
    if (!paused) wake_.notify_all();
}

bool Service::idle() const {
    std::lock_guard lock(inbox_mutex_);
    return !inbox_.library && inbox_.touched.empty() && inbox_.patches.empty() && !working_ && (filled_ || !config_.background);
}

bool Service::mergeLocked(const std::string& key, Source source, Patch patch) {
    std::unique_lock lock(index_mutex_);
    return index_.merge(key, source, std::move(patch));
}

std::optional<LibraryGame> Service::game(const std::string& key) const {
    const auto found = games_.find(key);
    return found == games_.end() ? std::nullopt : std::optional(found->second);
}

bool Service::missed(const std::string& cache_key, const char* source, uint64_t days) const {
    const auto marked = modifiedSeconds(knowledge() / L"miss" / (text_encoding::wideLenient(cache_key) + L"." + text_encoding::wideLenient(source)));
    return marked && config_.now() < marked + days * day;
}

void Service::markMissed(const std::string& cache_key, const char* source) const {
    atomic_file::write(knowledge() / L"miss" / (text_encoding::wideLenient(cache_key) + L"." + text_encoding::wideLenient(source)), std::to_string(config_.now()));
}

HttpResponse Service::fetch(const std::wstring& url, size_t limit, bool secret) {
    ++requests_;
    next_request_ = std::chrono::steady_clock::now() + (requests_ >= config_.requests_per_start ? config_.slow_spacing : config_.spacing);
    HttpBudget budget(15000);
    return (secret ? config_.get_private : config_.get)(url, L"Accept: application/json\r\n", limit);
}

void Service::loadTags() {
    tags_ = parseSteamTags(readFile(knowledge() / L"steam-tags.json", 8 * 1024 * 1024));
    // Until a fresh copy arrives (or offline): the snapshot built into the launcher.
    if (tags_.size() < 20) {
        tags_.clear();
        for (const auto& tag : builtinSteamTags()) tags_.emplace(tag.id, tag.name);
    }
}

void Service::readOffline(const std::vector<std::string>& keys) {
    std::vector<std::tuple<std::string, Source, Patch>> found;
    for (const auto& key : keys) {
        if (!alive_) return;
        const auto item = game(key);
        if (!item) continue;
        for (auto& [source, patch] : readCaches(folders(), {item->key, item->cache_key, item->title, item->app_id}, config_.english))
            found.emplace_back(key, source, std::move(patch));
        if (item->app_id) {
            if (const auto app = appinfo_.find(item->app_id); app != appinfo_.end())
                found.emplace_back(key, Source::steam_appinfo, fromAppInfo(app->second, tags_));
            // The client's play time and achievements: only games that were ever on this disk.
            if (config_.steam_stats && account_ && (item->installed || item->last_played)) {
                const auto [minutes, unlocked, total] = config_.steam_stats(account_, item->app_id);
                Patch stats;
                if (minutes) stats.playtime_minutes = minutes;
                if (total) { stats.achievements_unlocked = unlocked; stats.achievements_total = total; }
                found.emplace_back(key, Source::steam_stats, std::move(stats));
            }
        }
        read_.insert(key);
    }
    std::unique_lock lock(index_mutex_);
    for (auto& [key, source, patch] : found) {
        // An empty answer from a cache does not erase what another reading of it said.
        if (patch.empty() && source != Source::steam_stats) continue;
        index_.merge(key, source, std::move(patch));
    }
}

bool Service::process(Inbox inbox) {
    const auto before = read([](const Index& index) { return index.revision(); });
    if (inbox.library) {
        auto& [games, account] = *inbox.library;
        games_.clear();
        std::vector<std::pair<std::string, Patch>> patches;
        patches.reserve(games.size());
        for (auto& item : games) {
            if (item.key.empty()) continue;
            patches.emplace_back(item.key, libraryPatch(item));
            games_[item.key] = std::move(item);
        }
        const bool account_changed = account != account_;
        account_ = account;
        if (account_changed) read_.clear(); // Another account's play time and achievements.
        {
            std::unique_lock lock(index_mutex_);
            index_.syncLibrary(patches, config_.now());
        }
        // Steam's appinfo cache: read again when it changed or new games appeared in it.
        std::vector<uint32_t> ids;
        for (const auto& [key, item] : games_) if (item.app_id) ids.push_back(item.app_id);
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(config_.steam_root / L"appcache" / L"appinfo.vdf", ec);
        const bool new_ids = std::any_of(ids.begin(), ids.end(), [&](uint32_t id) { return !appinfo_requested_.contains(id); });
        if (!ids.empty() && !config_.steam_root.empty() && !ec && (time != appinfo_time_ || new_ids)) {
            const std::set<uint32_t> wanted(ids.begin(), ids.end());
            if (const auto apps = steam_appinfo::read(config_.steam_root, [&](uint32_t id) { return wanted.contains(id); })) {
                appinfo_.clear();
                for (const auto& app : *apps) appinfo_[app.app_id] = app;
                appinfo_time_ = time;
                appinfo_requested_ = wanted;
                // Every Steam game again: its appinfo part may have changed.
                for (const auto& [key, item] : games_) if (item.app_id) read_.erase(key);
            }
        }
        std::vector<std::string> unread;
        for (const auto& [key, item] : games_) if (!read_.contains(key)) unread.push_back(key);
        readOffline(unread);
        filled_ = false; // New games may need the network.
    }
    if (!inbox.touched.empty()) {
        std::sort(inbox.touched.begin(), inbox.touched.end());
        inbox.touched.erase(std::unique(inbox.touched.begin(), inbox.touched.end()), inbox.touched.end());
        for (const auto& key : inbox.touched) read_.erase(key);
        readOffline(inbox.touched);
    }
    for (auto& [key, source, patch] : inbox.patches) mergeLocked(key, source, std::move(patch));
    return read([](const Index& index) { return index.revision(); }) != before;
}

bool Service::fillStep() {
    // Throttled: the worker sleeps until the pause ends (not a wake-up every 50 ms meanwhile).
    if (std::chrono::steady_clock::now() < backoff_until_) { next_request_ = backoff_until_; return false; }
    const auto now = config_.now();
    // Steam's tag names first (one request a month): appinfo knows tags by number only.
    if (!tags_checked_) {
        tags_checked_ = true;
        const auto file = knowledge() / L"steam-tags.json";
        if (modifiedSeconds(file) + 30 * day < now) {
            const auto response = fetch(L"https://store.steampowered.com/tagdata/populartags/english", 2 * 1024 * 1024, false);
            const auto body = text(response);
            if (response.status == 200)
                if (auto tags = parseSteamTags(body); tags.size() >= 20) {
                    atomic_file::write(file, body);
                    tags_ = std::move(tags);
                    std::vector<std::string> steam;
                    for (const auto& [key, item] : games_) if (item.app_id) steam.push_back(key);
                    readOffline(steam);
                }
            return true;
        }
    }
    // The next game with a gap: installed and recently played first.
    enum class Kind { steam_store, gog, nintendo, rawg };
    struct Candidate { std::string key; Kind kind; bool installed; uint64_t played; };
    std::vector<Candidate> candidates;
    {
        std::shared_lock lock(index_mutex_);
        for (const auto& [key, item] : games_) {
            if (attempted_.contains(key)) continue;
            const auto* record = index_.get(key);
            if (!record || !record->owned) continue;
            std::optional<Kind> kind;
            if (item.app_id) {
                if (!index_.patch(key, Source::steam_store) && !missed(item.cache_key, "steam", 30)) kind = Kind::steam_store;
            } else if (!item.gog_id.empty() && !index_.patch(key, Source::gog) && !missed(item.cache_key, "gog", 30)) kind = Kind::gog;
            const bool gaps = !record->release.known() || record->genres.empty() || record->description.empty();
            // Nintendo's own shop first for its consoles' games: it answers fast and knows Switch 2 games.
            if (!kind && !item.app_id && gaps && nintendoShop(item.platform) && !attempted_.contains(key + "#eshop") && !index_.patch(key, Source::nintendo) &&
                !missed(item.cache_key, "nintendo", 7))
                kind = Kind::nintendo;
            if (!kind && !item.app_id && gaps && !config_.rawg_key.empty() && !index_.patch(key, Source::rawg) && !missed(item.cache_key, "rawg", 7)) // RAWG learns new games: a miss is retried weekly.
                kind = Kind::rawg;
            if (kind) candidates.push_back({key, *kind, record->installed, record->last_played});
        }
    }
    if (candidates.empty()) { filled_ = true; return false; }
    const auto best = std::min_element(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.installed != b.installed) return a.installed;
        if (a.played != b.played) return a.played > b.played;
        return a.key < b.key;
    });
    const auto pick = *best;
    // The eShop is its own attempt: a miss there still leaves RAWG for this start.
    const auto attempt = pick.kind == Kind::nintendo ? pick.key + "#eshop" : pick.key;
    attempted_.insert(attempt);
    const auto item = *game(pick.key);
    const auto throttled = [&](uint32_t status) {
        if (status != 429 && status != 403 && status != 0) return false;
        backoff_until_ = std::chrono::steady_clock::now() + std::chrono::minutes(status ? 15 : 2);
        attempted_.erase(attempt); // Again after the pause.
        return true;
    };
    switch (pick.kind) {
    case Kind::steam_store: {
        // The same request and cache file as Szczegóły (store/game_details.cpp).
        const auto id = std::to_wstring(item.app_id);
        const auto response = fetch(L"https://store.steampowered.com/api/appdetails?appids=" + id + (config_.english ? L"&l=english" : L"&l=polish"), 2 * 1024 * 1024, false);
        if (throttled(response.status)) return true;
        const auto body = text(response);
        if (response.status == 200 && !fromSteamStore(item.app_id, body).empty()) {
            atomic_file::write(config_.config / L"metadata" / (id + (config_.english ? L".en.details.json" : L".details.json")), body);
            readOffline({pick.key});
        } else if (response.status == 200) markMissed(item.cache_key, "steam"); // Delisted or region-locked.
        return true;
    }
    case Kind::gog: {
        const auto response = fetch(L"https://api.gog.com/products/" + text_encoding::wideLenient(item.gog_id) + L"?expand=description", 2 * 1024 * 1024, false);
        if (throttled(response.status)) return true;
        const auto body = text(response);
        if (response.status == 200 && !fromGogProduct(body).empty()) {
            atomic_file::write(knowledge() / L"gog" / (text_encoding::wideLenient(item.gog_id) + L".json"), body);
            readOffline({pick.key});
        } else if (response.status == 200 || response.status == 404) markMissed(item.cache_key, "gog"); // Not a passing server error.
        return true;
    }
    case Kind::nintendo: {
        // The best title match, only when it is the very title (or its core without edition words).
        const auto response = fetch(L"https://searching.nintendo-europe.com/en/select?fq=type:GAME&rows=12&wt=json&q=" + url_encoding::component(item.title),
                                    1024 * 1024, false);
        if (throttled(response.status)) return true;
        metadata_sources::Query query;
        query.title = item.title;
        auto offers = response.status == 200 ? metadata_sources::parseNintendo(text(response), query) : std::vector<metadata_sources::Offer>{};
        const metadata_sources::Offer* match = nullptr;
        int best_score = 0, second = 0;
        for (const auto& offer : offers) {
            const int score = gridTitleScore(item.title, offer.title);
            if (score > best_score) { second = best_score; best_score = score; match = &offer; }
            else second = std::max(second, score);
        }
        if (!match || best_score < 930 || (second >= best_score && fold(match->title) != fold(item.title)) || match->details.description.empty()) {
            if (response.status == 200) markMissed(item.cache_key, "nintendo");
            return true;
        }
        const auto field = [](const std::wstring& value) { return json::quote(text_encoding::utf8Lenient(value)); };
        const std::string body = "{\"title\":" + field(match->title) + ",\"description\":" + field(match->details.description) + ",\"publisher\":" +
                                 field(match->details.publisher) + ",\"release\":" + field(match->details.release_date) + ",\"genres\":" + field(match->details.genres) + "}";
        atomic_file::write(knowledge() / L"nintendo" / (text_encoding::wideLenient(item.cache_key) + L".json"), body);
        readOffline({pick.key});
        return true;
    }
    case Kind::rawg: {
        // Only an unambiguous match of the very title: a wrong game is worse than none.
        const auto key = url_encoding::component(config_.rawg_key);
        auto response = fetch(L"https://api.rawg.io/api/games?key=" + key + L"&search=" + url_encoding::component(item.title) +
                              L"&search_precise=true&page_size=8", 256 * 1024, true);
        if (response.status == 401) { config_.rawg_key.clear(); return true; } // A rejected key: no more RAWG this start.
        if (throttled(response.status)) return true;
        const auto results = response.status == 200 ? json::parse(text(response)) : std::nullopt;
        const auto* list = results ? results->get("results") : nullptr;
        if (!list || list->type != json::Value::Type::array) { if (response.status == 200) markMissed(item.cache_key, "rawg"); return true; }
        const auto wanted = fold(item.title);
        std::string id;
        int matches = 0;
        for (const auto& entry : list->elements) {
            const auto* name = entry.get("name");
            const auto* number = entry.get("id");
            if (!name || !number || number->type != json::Value::Type::number || fold(text_encoding::wideLenient(name->stringOr())) != wanted) continue;
            ++matches;
            id = number->text.empty() ? std::to_string(static_cast<uint64_t>(number->numeric)) : number->text;
        }
        if (matches != 1 || id.empty()) { markMissed(item.cache_key, "rawg"); return true; }
        response = fetch(L"https://api.rawg.io/api/games/" + text_encoding::wideLenient(id) + L"?key=" + key, 512 * 1024, true);
        if (throttled(response.status)) return true;
        const auto body = text(response);
        if (response.status == 200 && !fromRawg(body).empty()) {
            atomic_file::write(knowledge() / L"rawg" / (text_encoding::wideLenient(item.cache_key) + L".json"), body);
            readOffline({pick.key});
        } else if (response.status == 200 || response.status == 404) markMissed(item.cache_key, "rawg");
        return true;
    }
    }
    return true;
}

void Service::save(bool force) {
    const auto revision = read([](const Index& index) { return index.revision(); });
    if (revision == saved_revision_) return;
    if (!force && std::chrono::steady_clock::now() - last_save_ < std::chrono::seconds(10)) return;
    const auto text = read([](const Index& index) { return index.save(); });
    if (atomic_file::write(file(), text)) saved_revision_ = revision;
    last_save_ = std::chrono::steady_clock::now();
}

void Service::run() {
    {
        auto loaded = Index::load(readFile(file(), 256ull * 1024 * 1024));
        std::unique_lock lock(index_mutex_);
        if (loaded) index_ = std::move(*loaded);
        saved_revision_ = index_.revision();
    }
    loaded_ = true;
    loadTags();
    const auto notify = [&] {
        const auto revision = read([](const Index& index) { return index.revision(); });
        if (revision == notified_revision_) return;
        notified_revision_ = revision;
        if (config_.changed) config_.changed(revision);
    };
    notify();
    while (alive_) {
        Inbox inbox;
        {
            std::unique_lock lock(inbox_mutex_);
            const bool fill = config_.background && !paused_ && !filled_ && !games_.empty();
            const auto until = fill ? std::max(next_request_, std::chrono::steady_clock::now() + std::chrono::milliseconds(50))
                                    : std::chrono::steady_clock::now() + std::chrono::seconds(10);
            wake_.wait_until(lock, until, [&] { return !alive_ || inbox_.library || !inbox_.touched.empty() || !inbox_.patches.empty(); });
            if (!alive_) break;
            inbox = std::move(inbox_);
            inbox_ = {};
            working_ = true; // Under the inbox lock: idle() never sees an empty inbox before the work starts.
        }
        process(std::move(inbox));
        notify();
        if (alive_ && config_.background && !paused_ && !filled_ && !games_.empty() && std::chrono::steady_clock::now() >= next_request_) {
            fillStep();
            notify();
        }
        save(false);
        working_ = false;
    }
    save(true);
}
}
