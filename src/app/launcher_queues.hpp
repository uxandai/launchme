#pragma once
// Work queues between Launcher (the UI thread) and its worker threads: the requests each worker
// takes under the queue's mutex, and generations that let late answers to older requests drop.
// Launcher keeps them under their old names (Launcher::JobQueue, …).
#include "worker_messages.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gog { class Service; }
namespace epic { class Service; }

namespace launcher_queues {

// Artwork downloads and disk reads (src/app/artwork_requests.cpp, library_worker.cpp).
struct Job {
    Game game;
    CoverMessage::Kind kind;
    uint32_t generation = 0;
    bool platform_background = false;
    unsigned retry_count = 0;
    std::chrono::steady_clock::time_point ready_at{};
    uint32_t revision = 0;
    bool prefetch = false;
    bool cache_checked = false; // Foreground cache reads have a worker independent of HTTP/OCR.
    std::string key; // artRequestKey(game, kind), kept so queue scans do not rebuild it per job.
    // A console or store tile's background: its games, shuffled; the first with art lends it (dimmed).
    std::vector<Game> backdrop_games;
};
struct JobQueue {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    std::deque<Job> secondary;
    std::deque<Job> prefetch;
    std::unordered_set<std::string> disk_cached;
    std::unordered_set<std::string> requested;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> blocked_until;
    std::unordered_map<std::string, uint32_t> revisions;
    std::atomic_uint32_t generation{0};
    bool quiet = false; // Screensaver running: only poster prefetch jobs download.
    bool bulk = false; // Manual artwork pass owns the background lane until it ends.
    std::atomic_bool rom_art{true}; // AppSettings::emulation_rom_art, read by the workers.
    std::string focus;
    bool allowed(const Job& job, bool local) const {
        // Store/console logos, badges and console/store backgrounds are on disk almost always:
        // the disk lane answers them first instead of queueing them behind downloads.
        const bool cache = !job.prefetch && !job.cache_checked &&
            (isGame(job.game) || job.kind == CoverMessage::Kind::badge ||
             job.game.kind == GameKind::platform || job.game.kind == GameKind::pc_platform);
        return cache == local && (!quiet || job.kind == CoverMessage::Kind::poster) && (!bulk || !job.prefetch);
    }
    std::optional<Job> takeReady(bool background, bool local, std::chrono::steady_clock::time_point now) {
        const auto take = [&](std::deque<Job>& queue, bool focused) -> std::optional<Job> {
            const auto at = std::find_if(queue.begin(), queue.end(), [&](const Job& job) {
                return allowed(job, local) && job.ready_at <= now && (!focused || gameKey(job.game) == focus);
            });
            if (at == queue.end()) return {};
            auto job = std::move(*at); queue.erase(at); return job;
        };
        // Current game's hero/landscape/logo must not sit behind unrelated square covers.
        for (const bool focused : {true, false})
            for (auto* queue : {&jobs, &secondary}) if (auto job = take(*queue, focused)) return job;
        if (!background) return std::nullopt;
        // The background lane fills what tiles show first: every missing cover and poster (the
        // poster first where the tiles are posters), then landscapes, backgrounds, logos, discs.
        const auto rank = [&](CoverMessage::Kind kind) {
            switch (kind) {
            case CoverMessage::Kind::cover: return posters_first ? 1 : 0;
            case CoverMessage::Kind::poster: return posters_first ? 0 : 1;
            case CoverMessage::Kind::landscape: return 2;
            case CoverMessage::Kind::hero: return 3;
            case CoverMessage::Kind::logo: return 4;
            default: return 5;
            }
        };
        auto best = prefetch.end();
        for (auto at = prefetch.begin(); at != prefetch.end(); ++at) {
            if (!allowed(*at, local) || at->ready_at > now || (best != prefetch.end() && rank(at->kind) >= rank(best->kind))) continue;
            best = at;
            if (!rank(best->kind)) break;
        }
        if (best == prefetch.end()) return std::nullopt;
        auto job = std::move(*best); prefetch.erase(best); return job;
    }
    bool posters_first = false; // The desktop window's grid (posters) or a poster layout.
};

// Store pages, music and sounds, running games, the artwork browser, achievements, the drawer's
// dashboard and the screensaver's images.
struct StoreRequest {
    uint32_t generation = 0;
    uint64_t account = 0;
    uint32_t app_id = 0;
    bool metadata = false;
    std::wstring query, key;
};
struct StoreQueue {
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<StoreRequest> pending;
    std::atomic_uint32_t generation{0};
};
struct MusicQueue {
    struct CueRequest {
        SoundCue cue = SoundCue::navigation;
        int index = 0;
        bool preview = false;
        uint32_t generation = 0;
        std::wstring file; // A file of the sound folder instead of the option's own file.
    };
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<std::wstring> pending; // Music file name to decode.
    std::vector<CueRequest> cues;
    std::atomic_uint32_t generation{0};
};
struct TrackedGame {
    Game game;
    // Exact identities replace path telemetry for a suspended game and for one
    // restored after a restart before the library supplied its Game.
    std::vector<GameProcess> processes;
    std::string history_key; // Captured at launch; account switches cannot reattribute it.
    // Started here as a program or script: a job object holding it and everything it starts
    // (platform/game_activity.hpp launchJob). A script, a .lnk or a launcher stub whose game
    // path telemetry cannot see stays "running" exactly while one of those processes lives.
    std::shared_ptr<void> job;
};
struct GameActivityQueue {
    std::mutex mutex;
    std::condition_variable wake;
    std::map<std::string, TrackedGame> active;
    std::atomic_uint32_t generation{0};
    std::string focus_key;
    uint64_t focus_id = 0, focus_until = 0;
};
// Installed library games the running-game scan looks for (game_sessions.cpp).
struct RunningScanQueue {
    std::mutex mutex;
    std::condition_variable wake;
    std::vector<Game> candidates;
    uint32_t version = 0; // 0: no library yet, the scan waits.
    bool poke = false;    // Scan now (the launcher got focus back).
};
struct ArtworkBrowserJob {
    ArtworkBrowserMessage::Type type = ArtworkBrowserMessage::Type::list;
    uint32_t generation = 0;
    Game game;
    ArtworkKind kind = ArtworkKind::cover;
    int page = 0;
    GridSource source;
    GridAsset asset;
    std::wstring query;
    ArtworkProvider provider = ArtworkProvider::steamgriddb;
    bool wallpaper = false; // The wallpaper picker's job (wallpaper_sources.hpp).
    uint32_t steam_app_id = 0; // The game's Steam AppID when known (artwork_sources::Context).
};
struct ArtworkBrowserQueue {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<ArtworkBrowserJob> jobs;
    std::atomic_uint32_t generation{0};
};
// Achievements: the list, then icons of the rows on screen; opening again drops the rest.
struct AchievementsJob {
    uint32_t generation = 0;
    bool icon = false;
    uint32_t app_id = 0;
    uint64_t account = 0;
    size_t index = 0;
    std::wstring url;
    std::wstring folder; // Icons' cache folder under achievements\: the AppID, or a plugin game's cache key.
    // A DRM-free game's local achievements (local_achievements.hpp): its folder and game key.
    bool local = false;
    std::filesystem::path game_folder;
    std::string key;
};
// The store accounts the plugin worker may call for a plugin (achievements' store requests,
// src/app/plugin_achievements.cpp). The Launcher fills it as its services start and empties it
// before they go; a call holds the mutex only to queue its request.
struct StoreGateway {
    std::mutex mutex;
    ::gog::Service* gog = nullptr;
    ::epic::Service* epic = nullptr;
};
struct AchievementsQueue {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<AchievementsJob> jobs;
    std::atomic_uint32_t generation{0};
};
// Dashboard poller: runs while the drawer is open; a new generation (close) wakes and ends it.
struct DashboardQueue {
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic_uint32_t generation{0};
};
// Screensaver worker: scans the disk cache once per run, then reads requested images.
struct ScreensaverRequest {
    uint32_t sequence = 0;
    Game game;
    ArtworkKind kind = ArtworkKind::hero;
};
struct ScreensaverQueue {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<ScreensaverRequest> requests;
    std::atomic_uint32_t generation{0};
};

// Steam's collections and hidden state written back to the client (src/app/steam_sync.cpp).
struct SteamSyncQueue {
    std::mutex mutex;
    std::condition_variable wake;
    uint64_t account = 0;
    std::vector<steam_sync::Change> pending;
    bool dirty = false;
};
// ES-DE gamelist.xml changes (ES-DE scraped meanwhile): details and missing artwork reload.
struct EsdeWatch {
    std::mutex mutex;
    std::unordered_map<std::wstring, std::filesystem::file_time_type> times;
    bool primed = false;
    std::atomic_bool running{false};
};

} // namespace launcher_queues
