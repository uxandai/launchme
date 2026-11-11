// Library worker: the initial Steam scan, the poller that follows installations and
// Steam account switches, independent artwork cache reads and three downloaders.
#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "library_snapshot.hpp"
#include "../platform/steam_downloads.hpp"
#include "../artwork/cover_cache.hpp"
#include "../artwork/background_focus.hpp"
#include "../artwork/platform_logo.hpp"
#include "../artwork/esde_art.hpp"
#include "../artwork/screenscraper_art.hpp"
#include "../artwork/retry_policy.hpp"
#include "../artwork/failure_cache.hpp"
#include "../library/visibility.hpp"
#include "../library/owned_games.hpp"
#include "../library/steam_owned.hpp"
#include "../net/http.hpp"
#ifndef _WIN32
#include "../library/steam_registry.hpp"
#endif

#include <algorithm>
#include <thread>

void Launcher::startLibraryWorker() {
    // Aurelia supplements Steam: the Steam client's own library (every account's installations,
    // collections, hidden games) stays the source while Steam is installed here; Aurelia's list
    // is the library only without a Steam client.
    if (aurelia_active_) {
        startAureliaLibraryWorker();
        if (aurelia_library_source_) { startArtworkWorkers(); return; }
    }
    const auto hwnd = hwnd_;
    const auto alive = alive_;
    // Scanning and network traffic never run on the UI thread. The worker shares
    // only the window handle and its cancellation flag, never a Launcher pointer.
    // Each loop is owned directly by BackgroundWork; no unguarded child threads
    // or references to another worker's stack survive partial startup/failure.
    const auto failure = [hwnd, alive](bool artwork) {
        return [hwnd, alive, artwork, reported = std::make_shared<std::atomic_bool>(false)](std::exception_ptr error) {
            // Every failure is logged; the user is told once per session.
            diagnostics::log("error", std::string(artwork ? "Artwork worker: " : "Steam library worker: ") + diagnostics::describe(error));
            if (!alive->load() || reported->exchange(true)) return;
            OutputDebugStringW(artwork ? L"Launchme: artwork worker failed\n" : L"Launchme: Steam library worker failed\n");
            auto message = std::make_unique<LibraryWorkerErrorMessage>(); message->artwork = artwork;
            if (PostMessageW(hwnd, library_worker_error_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        };
    };
    const auto library_error = failure(false);
    work_.start([hwnd, alive, library_error, rescan = steam_rescan_] {
        // Nothing here polls Steam's files. The worker sleeps in ChangeWatch::wait() on the
        // folders and registry key it reads, and rescans only what a change can affect:
        //   <steam>/config (loginusers.vdf: account; libraryfolders.vdf), every steamapps
        //   folder (appmanifest_*.acf), userdata/<account>/config and its cloudstorage
        //   (shortcuts, hidden/private games, collections), appcache (appinfo.vdf: owned
        //   games) and HKCU\Software\Valve\Steam\ActiveProcess (Steam started or closed).
        // Only an active download wakes it on a timer, for its live progress.
        const auto steam = findSteamRoot();
        const auto metadata = coverCacheDirectory().parent_path() / L"metadata";
        SteamOwnedGames owned(steam, metadata / L"steam-owned");
        enum class Owned { cached, fetch, force };
        const auto scanAccount = [&](uint64_t id, Owned mode, const SteamLibrary* previous = nullptr) {
            auto scan = scanSteamLibrary(steam);
            if (previous) scan = retainUnavailableSteamGames(std::move(scan), *previous);
            mergeSteamOwnedGames(scan, mode == Owned::cached ? owned.cached(id) : owned.read(id, mode == Owned::force));
            return scan;
        };
        const auto snapshot = [&](uint64_t id, Owned mode, bool refresh) {
            auto scan = scanAccount(id, mode);
            auto library = std::make_unique<LibraryMessage>();
            library->refresh = refresh;
            library->games = std::move(scan.visible);
            library->private_games = std::move(scan.private_games);
            library->account = id;
            library->collections = readSteamCollections(steam);
            library->excluded = excludedSteamApps(steam);
            library->shortcuts = scanSteamShortcuts(steam);
            applyCollectionPlatforms(library->shortcuts, library->collections);
            std::vector<uint32_t> ids;
            for (const auto& collection : library->collections)
                for (uint32_t app : collection.app_ids)
                    if (!steamShortcutId(app) && !library->excluded.contains(app)) ids.push_back(app);
            library->cached_names = cachedCollectionGameNames(ids, metadata);
            return library;
        };
        const auto userConfig = [&](uint64_t id) {
            constexpr uint64_t offset = 76561197960265728ULL;
            return id > offset ? steam / L"userdata" / std::to_wstring(id - offset) / L"config" : std::filesystem::path{};
        };
        // What each kind of change looks like; a notification that changes none of these
        // (a log, a shader cache, Steam touching an unrelated file) costs one directory listing.
        struct Stamps { uint64_t login = 0, folders = 0, manifests = 0, user = 0, appinfo = 0; std::vector<std::filesystem::path> libraries; };
        const auto stamps = [&](uint64_t id) {
            Stamps result;
            result.login = fileStamp(steam / L"config" / L"loginusers.vdf");
            result.folders = fileStamp(steam / L"steamapps" / L"libraryfolders.vdf");
            result.libraries = steamLibraryFolders(steam);
            uint64_t manifests = 1469598103934665603ULL;
            for (const auto& library : result.libraries) manifests = (manifests ^ folderStamp(library / L"steamapps", L"appmanifest_*.acf")) * 1099511628211ULL;
            result.manifests = manifests;
            if (const auto config = userConfig(id); !config.empty()) {
                uint64_t user = 1469598103934665603ULL;
                for (const auto& file : {config / L"localconfig.vdf", config / L"shortcuts.vdf", config / L"cloudstorage" / L"cloud-storage-namespace-1.json"})
                    user = (user ^ fileStamp(file)) * 1099511628211ULL;
                result.user = user;
            }
            result.appinfo = fileStamp(steam / L"appcache" / L"appinfo.vdf");
            return result;
        };
        // The running Steam client and its signed-in user (0 when closed or signed out).
        const auto steamProcess = [] {
#ifdef _WIN32
            const auto value = [](const wchar_t* name) {
                DWORD result = 0, size = sizeof(result);
                RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", name, RRF_RT_REG_DWORD, nullptr, &result, &size);
                return result;
            };
            return static_cast<uint64_t>(value(L"pid")) << 32 | value(L"ActiveUser");
#else
            // The same key in Steam's registry.vdf (~/.steam/registry.vdf).
            const auto file = steam_registry::file();
            const auto value = [&](const char* name) { return static_cast<uint32_t>(steam_registry::number(file, {"ActiveProcess"}, name).value_or(0)); };
            return static_cast<uint64_t>(value("pid")) << 32 | value("ActiveUser");
#endif
        };
        // A full snapshot identical to the last one posted (Steam rewrote localconfig.vdf for a
        // setting the launcher does not read) is not sent: the UI would rebuild every shelf.
        const auto digest = [](const LibraryMessage& message) {
            uint64_t hash = 1469598103934665603ULL;
            const auto mix = [&](uint64_t value) { hash = (hash ^ value) * 1099511628211ULL; };
            const auto text = [&](std::wstring_view value) { for (const wchar_t c : value) mix(c); mix(0); };
            const auto game = [&](const Game& item) {
                mix(item.app_id); mix(item.shortcut_id); text(item.name); mix(item.installed); mix(item.last_played); mix(item.last_updated);
                mix(item.added_at);
                mix(static_cast<uint64_t>(item.visibility)); mix(static_cast<uint64_t>(item.installation.phase));
                text(item.executable.wstring()); text(item.rom.wstring()); text(item.platform); text(item.install_directory.wstring());
            };
            for (const auto* list : {&message.games, &message.private_games}) { mix(list->size()); for (const auto& item : *list) game(item); }
            mix(message.shortcuts.size());
            for (const auto& shortcut : message.shortcuts) { game(shortcut.game); mix(shortcut.hidden); text(shortcut.game.play_action.arguments); }
            mix(message.collections.size());
            for (const auto& collection : message.collections) {
                text(collection.name); for (const char c : collection.id) mix(static_cast<uint8_t>(c));
                mix(collection.app_ids.size()); for (const auto app : collection.app_ids) mix(app);
            }
            std::vector<uint32_t> excluded(message.excluded.begin(), message.excluded.end());
            std::sort(excluded.begin(), excluded.end());
            mix(excluded.size()); for (const auto app : excluded) mix(app);
            return hash;
        };
        ChangeWatch watch;
        watch.wake(rescan->event);
        std::vector<std::filesystem::path> watched_libraries;
        uint64_t watched_account = UINT64_MAX;
        const auto rewatch = [&](uint64_t id, const std::vector<std::filesystem::path>& libraries) {
            watch.clear();
            watch.directory(steam / L"config");
            watch.directory(steam / L"appcache");
            for (const auto& library : libraries) watch.directory(library / L"steamapps");
            if (const auto config = userConfig(id); !config.empty()) { watch.directory(config); watch.directory(config / L"cloudstorage"); }
#ifdef _WIN32
            watch.registry(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", 0, false);
#else
            watch.directory(steam_registry::file().parent_path()); // registry.vdf: Steam started or closed.
#endif
            watched_libraries = libraries; watched_account = id;
        };
        uint64_t account = activeSteamId(steam);
        SteamLibrary previous_scan;
        uint64_t posted = 0; // digest() of the last full snapshot sent.
        bool reload = false;
        try {
            // The first screen uses the owned-games list cached by the last session; asking the
            // Steam client (appinfo.vdf, one call per app) follows right after.
            auto library = snapshot(account, Owned::cached, false);
            if (account != activeSteamId(steam)) { account = activeSteamId(steam); library = snapshot(account, Owned::cached, false); }
            posted = digest(*library);
            if (!alive->load() || !postLibrarySnapshot(hwnd, std::move(library), previous_scan)) return;
        } catch (...) {
            // Report it and keep following Steam: the full snapshot is retried (every 30 s, and
            // on any change) instead of the library staying empty for the session.
            library_error(std::current_exception());
            reload = true;
        }
        Stamps last = stamps(account);
        rewatch(account, last.libraries);
        uint64_t previous = account;
        uint64_t last_process = steamProcess();
        bool owned_due = true;
        int owned_attempts = 0; // Unanswered asks since Steam (re)started; bounded retries.
        auto next_owned = std::chrono::steady_clock::time_point{};
        const auto ownedAsked = [&] {
            ++owned_attempts;
            owned_due = !owned.answered() && (last_process >> 32) && owned_attempts < 5; // Steam still starting.
            next_owned = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        };
        SteamDownloads downloads(steam);
        const auto activeDownload = [&] {
            const auto active = [](const auto& list) { return std::any_of(list.begin(), list.end(), [](const Game& game) { return game.installation.active(); }); };
            return active(previous_scan.visible) || active(previous_scan.private_games);
        };
        const auto postUpdate = [&](SteamLibrary scan, uint64_t id) {
            for (auto* games : {&scan.visible, &scan.private_games}) for (auto& game : *games)
                if (game.installation.active()) applyLiveDownload(game.installation, downloads.read(game.app_id));
            previous_scan = scan;
            auto update = std::make_unique<LibraryMessage>();
            update->account = id; update->installation_update = true;
            update->games = std::move(scan.visible); update->private_games = std::move(scan.private_games);
            update->excluded.insert(scan.excluded.begin(), scan.excluded.end());
            if (PostMessageW(hwnd, library_message, 0, reinterpret_cast<LPARAM>(update.get()))) update.release();
        };
        while (alive->load()) {
            const bool downloading = activeDownload();
            DWORD timeout = downloading ? 800 : INFINITE;
            // No Steam folder (installed later) or nothing watchable: one cheap look a minute.
            if (watch.empty()) timeout = std::min<DWORD>(timeout, 60000);
            if (reload) timeout = std::min<DWORD>(timeout, 30000);
            const auto now = std::chrono::steady_clock::now();
            if (owned_due) timeout = std::min<DWORD>(timeout, now >= next_owned ? 0 :
                static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(next_owned - now).count()) + 1);
            const auto result = watch.wait(timeout);
            if (!alive->load()) break;
            try {
                if (result == ChangeWatch::Result::changed) watch.settle(150, 1500); // Steam writes in bursts.
                if (!alive->load()) break;
                // A settings change (hidden/private games), Steam sync or F5 asks for the whole snapshot
                // again. Taken after settling: a request made meanwhile is not lost.
                const bool requested = rescan->take();
                const bool changed = result != ChangeWatch::Result::timeout || requested || watch.empty() || reload;
                const uint64_t next = changed ? activeSteamId(steam) : previous;
                const Stamps now_stamps = changed ? stamps(next) : last;
                if (next != watched_account || now_stamps.libraries != watched_libraries || watch.empty()) rewatch(next, now_stamps.libraries);
                if (const auto process = changed ? steamProcess() : last_process; process != last_process) {
                    if ((process >> 32) != (last_process >> 32)) owned_attempts = 0;
                    last_process = process; owned_due = true; // Steam started, closed or signed in.
                }
                if (now_stamps.appinfo != last.appinfo) { owned_due = true; owned_attempts = 0; } // A purchase, a free game, a removed licence.
                const bool fetch_owned = requested || (owned_due && std::chrono::steady_clock::now() >= next_owned);
                if (next == previous && !reload && !requested && now_stamps.user == last.user) {
                    const bool games_changed = now_stamps.manifests != last.manifests || now_stamps.folders != last.folders;
                    last = now_stamps;
                    if (!games_changed && !fetch_owned) {
                        // Only the live progress of a running download: no file is read for it.
                        if (downloading) postUpdate(previous_scan, next);
                        continue;
                    }
                    auto scan = scanAccount(next, fetch_owned ? Owned::fetch : Owned::cached, &previous_scan);
                    if (fetch_owned) ownedAsked();
                    if (!alive->load() || activeSteamId(steam) != next) continue;
                    postUpdate(std::move(scan), next);
                    continue;
                }
                last = now_stamps;
                // The same account keeps its shelves until the new snapshot replaces them.
                const bool refresh = next == previous && !reload;
                if (!refresh) {
                    reload = true; // Also retry if the account flips back after this empty snapshot.
                    owned_due = true; owned_attempts = 0;
                    auto empty = std::make_unique<LibraryMessage>();
                    empty->account = next;
                    if (!alive->load() || !PostMessageW(hwnd, library_message, 0,
                                                         reinterpret_cast<LPARAM>(empty.get()))) break;
                    empty.release();
                    posted = 0;
                }
                const auto mode = requested ? Owned::force : fetch_owned || !refresh ? Owned::fetch : Owned::cached;
                auto library = snapshot(next, mode, refresh);
                if (mode != Owned::cached) ownedAsked();
                if (!alive->load() || activeSteamId(steam) != next) continue;
                const auto sent = digest(*library);
                if (refresh && !requested && sent == posted) continue; // Nothing the launcher shows changed.
                if (!alive->load() || !postLibrarySnapshot(hwnd, std::move(library), previous_scan)) break;
                posted = sent;
                previous = next; // A failed account scan must be retried in full.
                reload = false;
            } catch (...) {
                library_error(std::current_exception());
                // A file that throws on every read (a broken shortcuts.vdf) is retried in 30 s, not
                // at once: owned_due with a past next_owned would make the wait return immediately.
                next_owned = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            }
        }
    }, library_error);
    startArtworkWorkers();
}

void Launcher::startArtworkWorkers(bool network) {
    const auto hwnd = hwnd_; const auto alive = alive_; const auto jobs = jobs_;
    const auto artwork_error = [hwnd, alive, reported = std::make_shared<std::atomic_bool>(false)](std::exception_ptr) {
        if (!alive->load() || reported->exchange(true)) return;
        OutputDebugStringW(L"Launchme: artwork worker failed\n");
        auto message = std::make_unique<LibraryWorkerErrorMessage>(); message->artwork = true;
        if (PostMessageW(hwnd, library_worker_error_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    jobs_->rom_art = settings_.emulation_rom_art;
    const auto download = [hwnd, alive, jobs, artwork_error](bool background_worker, bool cache_worker) {
        const auto steam = findSteamRoot();
        const auto cache = coverCacheDirectory();
        while (alive->load()) {
            Job job{};
            {
                std::unique_lock lock(jobs->mutex);
                using clock = std::chrono::steady_clock;
                while (alive->load()) {
                    const auto now = clock::now();
                    // While the screensaver runs only its poster prefetch downloads; the rest waits.
                    const bool quiet = jobs->quiet;
                    const bool bulk = jobs->bulk;
                    const auto allowed = [&](const Job& candidate) { return jobs->allowed(candidate, cache_worker); };
                    if (auto ready = jobs->takeReady(background_worker, cache_worker, now)) { job = std::move(*ready); break; }
                    auto soonest = clock::time_point::max();
                    for (const auto& pending : jobs->jobs) if (allowed(pending)) soonest = std::min(soonest, pending.ready_at);
                    for (const auto& pending : jobs->secondary) if (allowed(pending)) soonest = std::min(soonest, pending.ready_at);
                    if (background_worker) for (const auto& pending : jobs->prefetch) if (allowed(pending)) soonest = std::min(soonest, pending.ready_at);
                    if (soonest == clock::time_point::max())
                        jobs->wake.wait(lock, [&] {
                            const auto any = [&](const std::deque<Job>& queue) { return std::any_of(queue.begin(), queue.end(), allowed); };
                            return !alive->load() || jobs->quiet != quiet || jobs->bulk != bulk || any(jobs->jobs) || any(jobs->secondary) ||
                                   (background_worker && any(jobs->prefetch));
                        });
                    else jobs->wake.wait_until(lock, soonest);
                }
                if (!alive->load()) break;
            }
            try {
#ifdef _WIN32
                // Prefetch (the library's art for later, and its compaction) yields the CPU and the disk
                // to everything else, a game first; the same thread serves the screen's art at full priority.
                struct BackgroundScope { // Not "Yield": Windows headers define a Yield() macro.
                    bool on;
                    explicit BackgroundScope(bool prefetch) : on(prefetch && SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN)) {}
                    ~BackgroundScope() { if (on) SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END); }
                } background{job.prefetch};
#endif
                auto result = std::make_unique<CoverMessage>();
                result->key = gameKey(job.game);
                result->kind = job.kind;
                result->generation = job.generation;
                result->revision = job.revision;
                if (jobs->generation.load() != job.generation) continue;
                const auto key = artRequestKey(job.game, job.kind);
                const auto failures = cache.parent_path() / L"artwork" / L"failures";
                const auto started = artwork_failure::now();
                const bool persist_failure = isGame(job.game) && job.kind != CoverMessage::Kind::badge;
                const bool rom_first = jobs->rom_art.load();
                if (cache_worker) {
                    // No HTTP, title lookup or OCR here: slow downloads cannot hold cached art hostage.
                    const auto platforms = cache.parent_path() / L"platforms";
                    const bool tile = job.game.kind == GameKind::platform || job.game.kind == GameKind::pc_platform;
                    bool provisional = false; // Shown now; a download lane still looks for a game's art.
                    if (job.kind == CoverMessage::Kind::badge) {
                        result->badge = activePlatformName(job.game);
                        result->bytes = platformLogoCached(result->badge, platforms);
                    } else if (job.kind == CoverMessage::Kind::hero && job.platform_background) {
                        // A random one of its games whose background is on disk already.
                        for (const auto& candidate : job.backdrop_games) {
                            if (!alive->load() || jobs->generation.load() != job.generation) break;
                            if (auto bytes = cachedArtwork(candidate, ArtworkKind::hero, cache, rom_first); !bytes.empty()) {
                                result->bytes = std::move(bytes);
                                result->hero_tone = HeroTone::dimmed;
                                break;
                            }
                        }
                        // Emulated games: ES-DE's scraped fanart is on disk too (copied to the cache once read).
                        for (size_t i = 0, tried = 0; result->bytes.empty() && i < job.backdrop_games.size() && tried < 4; ++i) {
                            const auto& candidate = job.backdrop_games[i];
                            if (candidate.kind != GameKind::nonsteam) continue;
                            ++tried;
                            result->bytes = esdeArtForShortcut(candidate.app_id, candidate.platform, candidate.name,
                                EmuArtKind::background, cache.parent_path() / L"emulation", candidate.rom);
                            if (!result->bytes.empty()) result->hero_tone = HeroTone::dimmed;
                        }
                        if (result->bytes.empty()) {
                            // The console's own backdrop, else its (a store's) logo, faint.
                            bool logo = job.game.kind == GameKind::pc_platform;
                            result->bytes = logo ? platformLogoCached(job.game.platform, platforms) :
                                                   platformBackgroundCached(job.game.platform, platforms, logo);
                            result->hero_tone = logo ? HeroTone::logo : HeroTone::full;
                            provisional = !job.backdrop_games.empty();
                        }
                    } else if (tile) {
                        result->bytes = platformLogoCached(job.game.platform, platforms);
                    } else {
                        result->bytes = cachedArtwork(job.game, artworkKind(job.kind), cache, rom_first);
                        if (!result->bytes.empty() && job.kind == CoverMessage::Kind::cover) {
                            const auto image = cachedArtworkFile(job.game, ArtworkKind::cover, cache, rom_first);
                            auto metadata = image; metadata.replace_extension(L".focus");
                            if (const auto focus = storedTextCrop(image, metadata, job.game.name)) result->focus = *focus;
                            else result->bytes.clear(); // OCR runs on a download worker, never blocks this lane.
                        } else if (!result->bytes.empty() && job.kind == CoverMessage::Kind::hero)
                            result->focus = backgroundFocus(job.game, result->bytes, cache).value_or(CropFocus{});
                    }
                    if (provisional && !result->bytes.empty()) {
                        // The console's own backdrop or its faint logo until a game's background is found.
                        auto stand_in = std::make_unique<CoverMessage>(*result);
                        if (!alive->load() || !PostMessageW(hwnd, cover_message, 0, reinterpret_cast<LPARAM>(stand_in.get()))) break;
                        stand_in.release();
                        result->bytes.clear();
                    }
                    if (result->bytes.empty()) {
                        const bool poster = job.kind == CoverMessage::Kind::poster;
                        {
                            std::lock_guard lock(jobs->mutex);
                            if (jobs->generation.load() == job.generation && jobs->revisions[key] == job.revision) {
                                job.cache_checked = true;
                                (job.kind == CoverMessage::Kind::cover ? jobs->jobs : jobs->secondary).push_back(std::move(job));
                                jobs->wake.notify_all();
                            }
                        }
                        // The desktop grid shows the square cover until a missing poster is downloaded.
                        if (poster) {
                            result->cache_miss = true;
                            if (!alive->load() || !PostMessageW(hwnd, cover_message, 0, reinterpret_cast<LPARAM>(result.get()))) break;
                            result.release();
                        }
                    } else {
                        if (!alive->load() || !PostMessageW(hwnd, cover_message, 0, reinterpret_cast<LPARAM>(result.get()))) break;
                        result.release();
                    }
                    continue;
                }
                if (persist_failure && !cachedArtworkAvailable(job.game, artworkKind(job.kind), cache, rom_first)) {
                    const auto until = artwork_failure::blockedUntil(failures, key, job.game.name, started);
                    if (until) {
                        std::lock_guard lock(jobs->mutex);
                        if (jobs->generation.load() == job.generation && jobs->revisions[key] == job.revision) {
                            jobs->requested.erase(key);
                            jobs->blocked_until[key] = std::chrono::steady_clock::now() + std::chrono::milliseconds(until - started);
                            if (job.kind == CoverMessage::Kind::poster && !job.prefetch) { // Known to fail for now: its cover stands in.
                                result->cache_miss = true;
                                if (PostMessageW(hwnd, cover_message, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
                            }
                        }
                        continue;
                    }
                }
                HttpBudget request_budget(30000);
                consumeArtworkRetryableError(); // Clear the worker's previous request status.
                const auto& game = job.game;
                bool disc_retryable = false;
                // An emulated game's background: the user's pick, ES-DE fanart/screenshots, then SteamGridDB.
                const auto gameHero = [&](const Game& emulated) {
                    if (auto bytes = cachedArtwork(emulated, ArtworkKind::hero, cache, rom_first); !bytes.empty()) return bytes;
                    auto bytes = selectedArtwork(emulated, ArtworkKind::hero, cache);
                    if (bytes.empty()) bytes = esdeArtForShortcut(emulated.app_id, emulated.platform, emulated.name,
                        EmuArtKind::background, cache.parent_path() / L"emulation", emulated.rom);
                    if (bytes.empty()) bytes = artworkForGame(emulated, ArtworkKind::hero, cache, steam);
                    if (bytes.empty()) bytes = screenscraperArt(emulated, EmuArtKind::background, cache.parent_path());
                    return bytes;
                };
                if (launcherTile(game)) { // The Launchery row: the mark its tile has in the Stores.
                    if (const auto* entry = launchers::find(std::string_view(game.source_id).substr(9)); entry && job.kind == CoverMessage::Kind::cover)
                        result->bytes = store_hub::tileImage(text_encoding::utf8Lenient(entry->image), cache.parent_path());
                } else if (job.kind == CoverMessage::Kind::disc) {
                    auto disc = discArtwork(game, cache);
                    result->bytes = std::move(disc.bytes); disc_retryable = disc.retryable;
                } else if (job.kind == CoverMessage::Kind::badge) {
                    result->badge = activePlatformName(game);
                    result->bytes = platformLogo(result->badge, cache.parent_path() / L"platforms");
                } else if (job.kind == CoverMessage::Kind::hero && job.platform_background) {
                    // A console or store tile: the background of one of its games (shuffled by the UI),
                    // dimmed. The disk lane found none on disk (it showed a stand-in): a few lookups.
                    const auto backdrop = [&](const Game& candidate) {
                        return candidate.kind == GameKind::nonsteam ? gameHero(candidate) :
                                                                      artworkForGame(candidate, ArtworkKind::hero, cache, steam);
                    };
                    if (!job.cache_checked)
                        for (const auto& candidate : job.backdrop_games)
                            if (cachedArtworkAvailable(candidate, ArtworkKind::hero, cache, rom_first)) {
                                result->bytes = backdrop(candidate);
                                if (!result->bytes.empty()) break;
                            }
                    for (size_t i = 0; result->bytes.empty() && i < std::min<size_t>(job.backdrop_games.size(), 4); ++i)
                        result->bytes = backdrop(job.backdrop_games[i]);
                    if (!result->bytes.empty()) result->hero_tone = HeroTone::dimmed;
                    else {
                        bool logo = game.kind == GameKind::pc_platform;
                        result->bytes = logo ? platformLogo(game.platform, cache.parent_path() / L"platforms") :
                                               platformBackground(game.platform, cache.parent_path() / L"platforms", logo);
                        result->hero_tone = logo ? HeroTone::logo : HeroTone::full;
                    }
                } else if (game.kind == GameKind::platform || game.kind == GameKind::pc_platform) {
                    result->bytes = platformLogo(game.platform, cache.parent_path() / L"platforms");
                } else if (game.kind == GameKind::nonsteam && job.kind == CoverMessage::Kind::hero) {
                    result->bytes = gameHero(game);
                    if (result->bytes.empty()) {
                        bool logo = false;
                        result->bytes = platformBackground(game.platform, cache.parent_path() / L"platforms", logo);
                        result->hero_tone = logo ? HeroTone::logo : HeroTone::full;
                    }
                } else if (job.kind == CoverMessage::Kind::cover) {
                    auto image_path = cachedArtworkFile(game, ArtworkKind::cover, cache, rom_first);
                    if (!image_path.empty()) result->bytes = cachedArtwork(game, ArtworkKind::cover, cache, rom_first);
                    else image_path = gameCoverPath(game, cache);
                    // Emulated games: the user's pick, then art matched by ROM file (ES-DE, ScreenScraper),
                    // then SteamGridDB by title; with emulation_rom_art off SteamGridDB comes first.
                    const bool emu_rom_first = game.kind == GameKind::nonsteam && rom_first;
                    const auto romCover = [&] {
                        auto bytes = esdeArtForShortcut(game.app_id, game.platform, game.name,
                            EmuArtKind::cover, cache.parent_path() / L"emulation", game.rom);
                        if (bytes.empty()) bytes = screenscraperArt(game, EmuArtKind::cover, cache.parent_path());
                        if (!bytes.empty()) image_path = esdeArtCachePath(game.app_id, EmuArtKind::cover, cache.parent_path() / L"emulation");
                        return bytes;
                    };
                    if (result->bytes.empty() && emu_rom_first) {
                        result->bytes = selectedArtwork(game, ArtworkKind::cover, cache);
                        if (result->bytes.empty()) result->bytes = romCover();
                    }
                    if (result->bytes.empty()) result->bytes = artworkForGame(game, ArtworkKind::cover, cache, steam);
                    if (result->bytes.empty() && game.kind == GameKind::nonsteam && !emu_rom_first) result->bytes = romCover();
                    if (!result->bytes.empty())
                        result->focus = cachedTextCrop(image_path,
                                                       std::filesystem::path(image_path).replace_extension(L".focus"), game.name);
                } else if (job.kind == CoverMessage::Kind::logo) {
                    // Wheels (marquees) matched by ROM: ES-DE's, then ScreenScraper's; SteamGridDB by title.
                    result->bytes = cachedArtwork(game, ArtworkKind::logo, cache, rom_first);
                    const bool emu_rom_first = game.kind == GameKind::nonsteam && rom_first;
                    const auto romLogo = [&] {
                        auto bytes = esdeArtForShortcut(game.app_id, game.platform, game.name,
                            EmuArtKind::logo, cache.parent_path() / L"emulation", game.rom);
                        if (bytes.empty()) bytes = screenscraperArt(game, EmuArtKind::logo, cache.parent_path());
                        return bytes;
                    };
                    if (result->bytes.empty() && emu_rom_first) {
                        result->bytes = selectedArtwork(game, ArtworkKind::logo, cache);
                        if (result->bytes.empty()) result->bytes = romLogo();
                    }
                    if (result->bytes.empty()) result->bytes = artworkForGame(game, ArtworkKind::logo, cache, steam);
                    if (result->bytes.empty() && game.kind == GameKind::nonsteam && !emu_rom_first) result->bytes = romLogo();
                }
                else if (job.kind == CoverMessage::Kind::poster || job.kind == CoverMessage::Kind::landscape) // Screensaver boxes, Flix rows.
                    result->bytes = artworkForGame(game, artworkKind(job.kind), cache, steam);
                else result->bytes = artworkForGame(game, ArtworkKind::hero, cache, steam);
                const bool transient = consumeArtworkRetryableError() || disc_retryable;
                if (jobs->generation.load() != job.generation) continue;
                if (result->bytes.empty()) {
                    const bool exhausted = !transient || job.retry_count >= artwork_retry::max_retries;
                    const auto cooldown_ms = std::max(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(artwork_retry::cooldown(transient)).count()),
                        static_cast<uint64_t>(artworkRateLimitSeconds()) * 1000);
                    if (persist_failure && exhausted) artwork_failure::record(failures, key, game.name, started,
                        artwork_failure::now() + cooldown_ms, transient);
                    std::lock_guard lock(jobs->mutex);
                    if (jobs->generation.load() != job.generation || jobs->revisions[key] != job.revision) continue;
                    if (transient && job.retry_count < artwork_retry::max_retries) {
                        job.ready_at = std::chrono::steady_clock::now() +
                                       artwork_retry::delay(job.retry_count, static_cast<uint32_t>(std::hash<std::string>{}(key)));
                        ++job.retry_count;
                        if (job.prefetch) jobs->prefetch.push_back(std::move(job));
                        else if (job.kind == CoverMessage::Kind::cover) jobs->jobs.push_back(std::move(job));
                        else jobs->secondary.push_back(std::move(job));
                        jobs->wake.notify_all();
                    } else {
                        jobs->requested.erase(key);
                        jobs->blocked_until[key] = std::chrono::steady_clock::now() + std::chrono::milliseconds(cooldown_ms);
                        if (job.kind == CoverMessage::Kind::poster && !job.prefetch) { // No poster for now: its cover stands in.
                            result->cache_miss = true;
                            if (PostMessageW(hwnd, cover_message, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
                        }
                    }
                    continue;
                }
                if (persist_failure) artwork_failure::clear(failures, key);
                if (job.kind == CoverMessage::Kind::hero && !job.platform_background) // The part of it a screen shows.
                    result->focus = backgroundFocus(game, result->bytes, cache).value_or(CropFocus{});
                result->prefetched = job.prefetch;
                if (job.prefetch) std::vector<uint8_t>{}.swap(result->bytes); // Disk cache, not a library-sized RAM cache.
                if (!alive->load() || !PostMessageW(hwnd, cover_message, 0,
                                                    reinterpret_cast<LPARAM>(result.get()))) break;
                result.release();
            } catch (...) {
                // A failed file/decoder operation only ends this job. Allow
                // a later request (or explicit retry) instead of wedging it.
                {
                    std::lock_guard lock(jobs->mutex);
                    if (jobs->generation.load() == job.generation && jobs->revisions[job.key] == job.revision) {
                        jobs->requested.erase(job.key);
                        jobs->blocked_until[job.key] = std::chrono::steady_clock::now() + artwork_retry::cooldown(true);
                    }
                }
                artwork_error(std::current_exception());
            }
        }
    };
    // Disk reads never wait for the network; two downloaders serve navigation, two also prefetch
    // (while one waits for SteamGridDB's answer or its 1.1 s spacing, the other downloads an image).
    work_.start([download] { download(false, true); }, artwork_error);
    if (network) for (int i = 0; i < 4; ++i) work_.start([download, background = i >= 2] { download(background, false); }, artwork_error);
}

void Launcher::onLibraryWorkerError(std::unique_ptr<LibraryWorkerErrorMessage> message) {
    if (!message->artwork) scanning_ = false;
    showNotification(NotificationKind::error,
        message->artwork ? tr(L"Nie można odczytać grafik gry") : tr(L"Nie można odczytać biblioteki Steam"), L"");
}
