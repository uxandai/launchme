#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../artwork/failure_cache.hpp"
#include "../artwork/image_compact.hpp"
#include "../library/esde_gamelist.hpp"
#include "../library/game_index_sources.hpp"
#include "../library/screenscraper.hpp"
#include "../platform/atomic_file.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <thread>

namespace {
using screenscraper::unixMs;
bool post(HWND hwnd, std::unique_ptr<RatingsMessage>& message, const std::atomic_bool& alive) {
    if (!alive.load() || !PostMessageW(hwnd, ratings_message, 0, reinterpret_cast<LPARAM>(message.get()))) return false;
    message.release();
    return true;
}

// Emulated shortcuts that neither a RAWG identification nor ES-DE's gamelist describe: cached
// ScreenScraper answers at once, then up to 40 jeuInfos lookups one at a time, spaced by the
// account's per-minute limit. A refusal, quota or throttling ends the pass with a cooldown.
void screenscraperPass(const std::vector<Game>& games, const std::shared_ptr<std::atomic_bool>& alive, HWND hwnd, uint32_t generation) {
    namespace ss = screenscraper;
    const auto root = userConfigDirectory();
    const auto metadata = root / L"metadata";
    std::optional<esde::Library> local;
    if (const auto esde_root = esde::root(); !esde_root.empty()) local.emplace(esde_root / L"gamelists");
    struct Pending { Game game; ss::RomFile rom; std::vector<uint32_t> systems; };
    std::vector<Pending> pending;
    auto cached = std::make_unique<RatingsMessage>(); cached->generation = generation;
    const auto now = unixMs();
    for (const auto& game : games) {
        if (!alive->load()) return;
        if (game.kind != GameKind::nonsteam || game.rom.empty()) continue;
        std::error_code ec;
        if (std::filesystem::exists(metadata / (gameCacheKey(game) + ".rawg.json"), ec)) continue; // The user's identification wins.
        if (local && local->find(game.platform, game.rom, game.name)) continue;                     // ES-DE already has it.
        auto systems = ss::systemIds(game.platform, game.rom);
        // A ROM on a drive that is not connected now keeps its answer and is never asked about:
        // without the file only its name would be sent, and a miss would replace a match.
        if (!std::filesystem::exists(game.rom, ec)) {
            if (const auto entry = ss::loadCached(game, metadata); entry && entry->game) {
                cached->emulation.emplace(gameKey(game), entry->game->details);
                cached->names.emplace(gameKey(game), entry->game->name);
                if (entry->manual) cached->manual.insert(gameKey(game));
            }
            continue;
        }
        auto rom = ss::describeRom(game.rom, 0); // Size only; the MD5 waits for an actual lookup.
        if (systems.empty() || !rom) continue;
        if (const auto entry = ss::loadCached(game, metadata); entry && entry->fingerprint == ss::fingerprint(game, *rom)) {
            if (entry->game) {
                cached->emulation.emplace(gameKey(game), entry->game->details);
                cached->names.emplace(gameKey(game), entry->game->name);
                if (entry->manual) cached->manual.insert(gameKey(game));
                continue;
            }
            if (now - entry->checked < ss::missing_retry_ms) continue;
        }
        pending.push_back({game, std::move(*rom), std::move(systems)});
    }
    if (!cached->emulation.empty() && !post(hwnd, cached, *alive)) return;
    const auto config = ss::load();
    if (pending.empty() || !config.ready() || ss::paused(metadata)) return;
    const std::wstring language = i18n::language() == i18n::Language::pl ? L"pl" : L"en";
    const auto stop = [&](uint64_t pause_ms, std::wstring error) {
        ss::pause(metadata, pause_ms);
        if (error.empty()) return;
        auto message = std::make_unique<RatingsMessage>(); message->generation = generation;
        message->screenscraper_error = std::move(error);
        post(hwnd, message, *alive);
    };
    auto spacing = std::chrono::milliseconds(1200);
    size_t requests = 0;
    for (auto& item : pending) {
        if (!alive->load() || requests >= 40 || ss::paused(metadata)) return; // Paused by another part too.
        if (item.rom.size && item.rom.size <= ss::hash_limit) item.rom.md5 = ss::md5File(item.game.rom);
        ss::Lookup result;
        // Dolphin's "Wii / GameCube" names two systems: the second only after a miss on the first.
        for (size_t i = 0; i < item.systems.size() && i < 2; ++i) {
            if (requests) for (auto waited = std::chrono::milliseconds(0); waited < spacing && alive->load(); waited += std::chrono::milliseconds(100))
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!alive->load()) return;
            HttpBudget budget(25000);
            result = ss::lookup(config, item.systems[i], item.rom, language);
            ++requests;
            if (result.quota.max_per_minute)
                spacing = std::max(std::chrono::milliseconds(1000), std::chrono::milliseconds(60000 / result.quota.max_per_minute + 100));
            if (result.status != ss::Status::missing) break;
        }
        switch (result.status) {
        case ss::Status::found: {
            ss::Cached entry{ss::fingerprint(item.game, item.rom), unixMs(), result.game};
            ss::saveCached(item.game, entry, metadata);
            auto message = std::make_unique<RatingsMessage>(); message->generation = generation;
            message->emulation.emplace(gameKey(item.game), result.game->details);
            message->names.emplace(gameKey(item.game), result.game->name);
            std::vector<ArtworkKind> empty;
            for (const auto kind : {ArtworkKind::cover, ArtworkKind::hero, ArtworkKind::logo})
                if (!cachedArtworkAvailable(item.game, kind, coverCacheDirectory())) empty.push_back(kind);
            if (!empty.empty()) message->artwork_refresh.emplace_back(item.game, std::move(empty));
            if (!post(hwnd, message, *alive)) return;
            break;
        }
        case ss::Status::missing:
            ss::saveCached(item.game, ss::Cached{ss::fingerprint(item.game, item.rom), unixMs(), {}}, metadata);
            break;
        case ss::Status::invalid: break; // Unreadable answer: asked again next start.
        case ss::Status::rejected:
            stop(60 * 60 * 1000, tr(L"ScreenScraper odrzucił dane dewelopera (devid/devpassword) lub konto. Sprawdź Ustawienia → Biblioteka."));
            return;
        case ss::Status::quota:
            stop(12ull * 60 * 60 * 1000, tr(L"Dzienny limit zapytań ScreenScraper wyczerpany. Spróbuję jutro."));
            return;
        case ss::Status::throttled: stop(2 * 60 * 1000, {}); return;
        case ss::Status::closed:
            stop(30 * 60 * 1000, tr(L"ScreenScraper jest chwilowo niedostępny. Spróbuję później."));
            return;
        }
        if (result.quota.exhausted()) { stop(12ull * 60 * 60 * 1000, {}); return; }
    }
}
}

void Launcher::startRatings() {
    // One pass after initial library scans, never from focus/paint.
    if (!runtime_started_ || ratings_started_ || scanning_ || pc_scanning_ || !pc_initial_ready_ || !gog_initial_ready_ || !epic_initial_ready_) return;
    ratings_started_ = true;
    auto games = public_library_;
    for (const auto& [platform, list] : platform_games_) games.insert(games.end(), list.begin(), list.end());
    const auto alive = ratings_alive_; const auto hwnd = hwnd_; const auto generation = ++ratings_generation_;
    // Its own worker: ScreenScraper's spacing never delays the Steam/RAWG ratings.
    work_.start([games, alive, hwnd, generation] { screenscraperPass(games, alive, hwnd, generation); });
    // Once: the artwork cached before compaction existed is re-encoded at drawing size, in the background.
    if (std::error_code ec; !std::filesystem::exists(userConfigDirectory() / L"artwork" / L"compacted.v1", ec))
        work_.start([alive = alive_, hwnd] {
            SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
            const auto root = userConfigDirectory();
            auto message = std::make_unique<CacheCompactedMessage>();
            message->saved = compactImageCache(root, [&] { return alive->load(); });
            SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
            if (!alive->load()) return; // Interrupted: the next start goes on (files done stay done).
            atomic_file::write(root / L"artwork" / L"compacted.v1", std::to_string(message->saved));
            if (PostMessageW(hwnd, cache_compacted_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        });
    // Once (0.3.0): backgrounds that compaction shrank below a 1440 px screen's height go, so they come
    // back at full size the next time they are shown.
    if (std::error_code ec; !std::filesystem::exists(userConfigDirectory() / L"artwork" / L"backgrounds.v2", ec))
        work_.start([alive = alive_] {
            SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
            const auto root = userConfigDirectory();
            const auto removed = releaseShrunkenBackgrounds(root, [&] { return alive->load(); });
            SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
            if (alive->load()) atomic_file::write(root / L"artwork" / L"backgrounds.v2", std::to_string(removed));
        });
    work_.start([games = std::move(games), alive, hwnd, generation] {
        const auto root = userConfigDirectory();
        auto cached = std::make_unique<RatingsMessage>(); cached->generation = generation;
        for (const auto& game : games) {
            if (!alive->load()) return;
            if (!steamArtwork(game) && game.visibility == GameVisibility::public_game)
                if (auto metadata = rawg::load(game, root / L"metadata")) cached->metadata.emplace(gameKey(game), std::move(*metadata));
        }
        // Emulated shortcuts: what ES-DE scraped for the same ROM file. Local files only.
        if (const auto esde_root = esde::root(); !esde_root.empty()) {
            esde::Library library(esde_root / L"gamelists");
            for (const auto& game : games) {
                if (!alive->load()) return;
                if (game.kind != GameKind::nonsteam || cached->metadata.contains(gameKey(game))) continue;
                // A ScreenScraper game the user picked in "Zidentyfikuj grę" wins over ES-DE's match.
                if (const auto picked = screenscraper::loadCached(game, root / L"metadata"); picked && picked->manual && picked->game) {
                    cached->emulation.emplace(gameKey(game), picked->game->details);
                    cached->names.emplace(gameKey(game), picked->game->name);
                    cached->manual.insert(gameKey(game));
                    continue;
                }
                if (auto found = library.find(game.platform, game.rom, game.name)) {
                    if (found->by_rom && !found->entry.name.empty()) cached->names.emplace(gameKey(game), found->entry.name);
                    auto& details = found->entry.details;
                    if (!details.description.empty() || !details.developer.empty() || !details.release_date.empty() || !details.genres.empty())
                        cached->emulation.emplace(gameKey(game), std::move(details));
                }
            }
        }
        if ((!cached->metadata.empty() || !cached->emulation.empty()) && alive->load() && PostMessageW(hwnd, ratings_message, 0, reinterpret_cast<LPARAM>(cached.get()))) cached.release();
        ratings::collect(games, root / L"ratings", userSecret(L"RAWG_API_KEY", L"rawg_api_key.txt"), *alive, [&](ratings::Map values) {
            if (!alive->load()) return;
            auto message = std::make_unique<RatingsMessage>(); message->generation = generation; message->ratings = std::move(values);
            if (PostMessageW(hwnd, ratings_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        });
    });
}
void Launcher::onRatings(std::unique_ptr<RatingsMessage> message) {
    if (message->generation != ratings_generation_) return;
    // The game index learns the same: scores, RAWG answers (re-read with their tags) and emulated games' details.
    for (const auto& [key, rating] : message->ratings) gameIndexMerge(key, game_index::Source::reviews, game_index::fromRating(rating));
    for (const auto& [key, details] : message->emulation) {
        const auto name = message->names.find(key);
        gameIndexMerge(key, game_index::Source::emulation, game_index::fromDetails(details, name == message->names.end() ? std::wstring{} : name->second));
    }
    if (!message->metadata.empty()) {
        std::vector<std::string> keys;
        for (const auto& [key, metadata] : message->metadata) keys.push_back(key);
        gameIndexTouch(std::move(keys));
    }
    for (auto& [key, metadata] : message->metadata) rawg_metadata_.try_emplace(key, std::move(metadata));
    for (auto& [key, details] : message->emulation) emulation_metadata_.insert_or_assign(key, std::move(details));
    bool renamed = false;
    for (auto& [key, name] : message->names) {
        auto& known = emulation_names_[key];
        if (known != name) { known = std::move(name); renamed = true; }
    }
    for (const auto& key : message->manual) renamed |= manual_names_.insert(key).second;
    if (renamed) applyEmulationNames(); // The player's picks show even with ROM titles off.
    for (auto& [key, rating] : message->ratings) if (!rawg_metadata_.contains(key)) ratings_[key] = std::move(rating);
    if (screen_ == ScreenMode::details && !detail_games_.empty() && !steamArtwork(detail_games_[0])) {
        if (const auto* details = localDetails(detail_games_[0])) { details_ = *details; completeDetails(details_, detail_games_[0]); }
    }
    // Newly identified by ScreenScraper: empty slots load again, now with its media.
    const auto failures = coverCacheDirectory().parent_path() / L"artwork" / L"failures";
    for (const auto& [game, kinds] : message->artwork_refresh)
        for (const auto kind : kinds) {
            const auto slot = kind == ArtworkKind::cover ? CoverMessage::Kind::cover : kind == ArtworkKind::hero ? CoverMessage::Kind::hero : CoverMessage::Kind::logo;
            artwork_failure::retry(failures, artRequestKey(game, slot), artwork_failure::now());
            invalidateArtwork(game, slot);
            requestArt(game, slot);
        }
    if (!message->screenscraper_error.empty()) showNotification(NotificationKind::error, L"ScreenScraper", message->screenscraper_error);
    InvalidateRect(hwnd_, nullptr, FALSE);
}
const GameDetails* Launcher::localDetails(const Game& game) const {
    const auto key = gameKey(game);
    if (const auto found = rawg_metadata_.find(key); found != rawg_metadata_.end()) return &found->second.details;
    if (const auto found = emulation_metadata_.find(key); found != emulation_metadata_.end()) return &found->second;
    return nullptr;
}
std::wstring Launcher::ratingLabel() const {
    if (cards_.empty()) return {};
    const auto key = gameKey(cards_[selected_].game);
    if (const auto found = rawg_metadata_.find(key); found != rawg_metadata_.end()) {
        const auto label = ratings::label(found->second.rating);
        return label.empty() ? L"RAWG" : label;
    }
    const auto found = ratings_.find(key);
    return found == ratings_.end() ? std::wstring{} : ratings::label(found->second);
}
std::wstring Launcher::ratingUrl() const {
    if (cards_.empty()) return {};
    const auto& game = cards_[selected_].game; const auto key = gameKey(game);
    if (const auto found = rawg_metadata_.find(key); found != rawg_metadata_.end()) return ratings::url(found->second.rating, game.app_id);
    const auto found = ratings_.find(key);
    return found == ratings_.end() ? std::wstring{} : ratings::url(found->second, game.app_id);
}
int Launcher::ratingDetailAction() const {
    return screen_ == ScreenMode::details && !ratingUrl().empty() ? std::max(lastDetailAction(), achievementsDetailAction()) + 1 : -2;
}
void Launcher::openRatingSource() {
    const auto link = ratingUrl();
    if (!link.empty()) execute_(hwnd_, GameAction{link, {}, {}, ActionKind::open_client});
}

void Launcher::onCacheCompacted(std::unique_ptr<CacheCompactedMessage> message) {
    if (message->saved < 16 * 1024 * 1024) return; // Not worth a word.
    showNotification(NotificationKind::launch, tr(L"Grafiki"),
                     tr(L"Zmniejszono cache grafik o ") + std::to_wstring(message->saved / (1024 * 1024)) + L" MB");
}