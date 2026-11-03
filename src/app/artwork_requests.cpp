// Artwork requests and results: queueing covers/logos/heroes/badges for the download
// workers (library_worker.cpp), binding arrived images to cards, invalidation and retry.
#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../artwork/cover_cache.hpp"
#include "../artwork/failure_cache.hpp"
#include "../ui/grid_layout.hpp"
#include "../ui/carousel.hpp"
#include "../ui/category_rail.hpp"
#include "../ui/psfive_layout.hpp"

#include <algorithm>

bool Launcher::selectedLogoPending() const {
    if (cards_.empty() || !isGame(cards_[selected_].game)) return false;
    const auto& game = cards_[selected_].game;
    const auto* art = art_.peek(gameKey(game));
    if (art && art->logo) return false;
    const auto key = artRequestKey(game, CoverMessage::Kind::logo);
    std::lock_guard lock(jobs_->mutex);
    return jobs_->requested.contains(key) && !jobs_->blocked_until.contains(key);
}

// The focused game has (or is getting) a background of its own: in RAM, queued or downloading, until
// a failure blocks it. The home wallpaper waits meanwhile, as the TV home without cards waits for
// its library; it shows behind tiles without art and games known to have none (background_renderer.cpp).
bool Launcher::selectedHeroPending() const {
    if (cards_.empty()) return emptyLibraryReason() == empty_library::Reason::loading;
    const auto& game = cards_[std::min(selected_, cards_.size() - 1)].game;
    if (!isGame(game)) return false;
    if (const auto* art = art_.peek(gameKey(game)); art && art->hero) return true;
    const auto key = artRequestKey(game, CoverMessage::Kind::hero);
    std::lock_guard lock(jobs_->mutex);
    const auto blocked = jobs_->blocked_until.find(key);
    return blocked == jobs_->blocked_until.end() || std::chrono::steady_clock::now() >= blocked->second;
}

bool Launcher::artBlocked(const Game& game, CoverMessage::Kind kind) const {
    std::lock_guard lock(jobs_->mutex);
    const auto found = jobs_->blocked_until.find(artRequestKey(game, kind));
    return found != jobs_->blocked_until.end() && std::chrono::steady_clock::now() < found->second;
}

void Launcher::onCover(std::unique_ptr<CoverMessage> cover) {
    if (cover->generation != jobs_->generation.load()) return;
    const auto key = cover->key + ':' + std::to_string(static_cast<int>(cover->kind));
    const auto revision = art_revisions_.find(key);
    if (revision != art_revisions_.end() && cover->revision != revision->second) return;
    if (cover->prefetched) {
        {
            std::lock_guard lock(jobs_->mutex);
            jobs_->requested.erase(key); jobs_->disk_cached.insert(key);
        }
        requestVisibleArt(); // A focused in-flight background job is rebound from disk at foreground priority.
        return;
    }
    if (cover->cache_miss) {
        // The disk lane has no poster for this game (a download was queued): in the desktop grid
        // its square cover stands in meanwhile.
        if (cover->kind == CoverMessage::Kind::poster && desktopActive() && desktop_poster_missing_.insert(cover->key).second) desktopRequestArt();
        return;
    }
    if (cover->kind == CoverMessage::Kind::badge) {
        renderer_.setPlatformBadge(cover->badge, std::move(cover->bytes));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (cover->kind == CoverMessage::Kind::disc) {
        auto& asset = art_.at(cover->key); asset.disc = shareImage(std::move(cover->bytes)); art_.resize(cover->key);
        if (libraryCoverflow()) for (size_t i = selected_; i < std::min(cards_.size(), selected_ + 2); ++i)
            if (gameKey(cards_[i].game) == cover->key) renderer_.setDisc(i, asset.disc);
        trimArt(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (cover->kind == CoverMessage::Kind::poster || cover->kind == CoverMessage::Kind::landscape) {
        // XMB/Flix cards use these in the foreground; the screensaver reads posters from disk.
        auto& asset = art_.at(cover->key);
        (cover->kind == CoverMessage::Kind::poster ? asset.poster : asset.landscape) = shareImage(std::move(cover->bytes));
        art_.resize(cover->key);
        if (cover->kind == CoverMessage::Kind::poster) tarotArtArrived(cover->key, cover->kind, asset.poster);
        if (flixActive()) for (size_t i = 0; i < cards_.size(); ++i) if (gameKey(cards_[i].game) == cover->key) bindFlixArt(i, true);
        metroArtArrived(cover->key, cover->kind);
        if (shelfPosters() && cover->kind == CoverMessage::Kind::poster)
            for (size_t i = 0; i < cards_.size(); ++i) if (gameKey(cards_[i].game) == cover->key) bindLibraryPoster(i, true);
        if (cover->kind == CoverMessage::Kind::landscape && shelfWideCard(0) && gameKey(cards_[0].game) == cover->key) bindLibraryLandscape(0, true);
        if (xmbActive() && settings_.xmb_posters && cover->kind == CoverMessage::Kind::poster)
            for (size_t i = 0; i < cards_.size(); ++i) if (gameKey(cards_[i].game) == cover->key) bindXmbArt(i, true);
        if (desktopActive() && cover->kind == CoverMessage::Kind::poster) {
            // The desktop grid's wanted rows: the poster replaces its stand-in.
            desktop_poster_missing_.erase(cover->key);
            const auto range = desktopArtRange();
            const auto edge = desktopArtEdge();
            for (size_t i = range.keep_first; i < range.keep_end; ++i)
                if (gameKey(cards_[i].game) == cover->key) renderer_.setPoster(i, asset.poster, edge);
        }
        if (libraryListPlus() && cover->kind == CoverMessage::Kind::poster && selected_ < cards_.size() && gameKey(cards_[selected_].game) == cover->key)
            bindLibraryPoster(selected_, true);
        if (libraryCarousel() && library_view::posterArt(libraryPresentation()) && cover->kind == CoverMessage::Kind::poster) {
            const auto view = renderer_.viewport();
            const auto [first, end] = coverflow_layout::Layout{view.width, view.height, libraryView(), libraryPresentation().coverflow_cd}.visibleRange(cards_.size(), selected_);
            for (size_t i = first; i < end; ++i) if (gameKey(cards_[i].game) == cover->key) bindLibraryPoster(i, true);
        }
        if (((libraryLandscape() || libraryGridLandscape()) && cover->kind == CoverMessage::Kind::landscape) ||
            (libraryGridPosters() && cover->kind == CoverMessage::Kind::poster)) {
            const auto view = renderer_.viewport();
            const auto [first, end] = library_layout::visibleRange(libraryView(), cards_.size(), view.width, view.height, grid_scroll_, libraryPresentation());
            for (size_t i = first; i < end; ++i) if (gameKey(cards_[i].game) == cover->key) {
                if (libraryGridPosters()) bindLibraryPoster(i, true); else bindLibraryLandscape(i, true);
            }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        trimArt();
        return;
    }
    auto& asset = art_.at(cover->key);
    if (cover->kind == CoverMessage::Kind::cover) {
        asset.cover = shareImage(std::move(cover->bytes));
        asset.focus = cover->focus;
    } else if (cover->kind == CoverMessage::Kind::logo) asset.logo = shareImage(std::move(cover->bytes));
    else { asset.hero = shareImage(std::move(cover->bytes)); asset.hero_tone = cover->hero_tone; asset.hero_focus = cover->focus; }
    art_.resize(cover->key);
    if (cover->kind == CoverMessage::Kind::cover) { tarotArtArrived(cover->key, cover->kind, asset.cover); profileArtArrived(cover->key, cover->kind, asset.cover); }
    if (friends_open_ && plugin_drawer_active_ && cover->kind == CoverMessage::Kind::cover) requestPluginPageArt();
    // The desktop grid binds only around its view (thousands of cards; a card scrolled away is
    // bound again from art_ when it returns) and a cover only where it stands in for a poster.
    const bool desktop = desktopActive();
    const auto range = desktop ? desktopArtRange() : DesktopArtRange{};
    const uint32_t edge = desktop ? desktopArtEdge() : 0;
    for (size_t i = 0; i < cards_.size(); ++i) {
        if (desktop && i != selected_ && (i < range.keep_first || i >= range.keep_end)) continue;
        if (desktop && cover->kind == CoverMessage::Kind::cover && i != selected_ && renderer_.hasPoster(i)) continue;
        if (gameKey(cards_[i].game) != cover->key) continue;
        if (libraryListPlus() && (i != selected_ || cover->kind == CoverMessage::Kind::logo)) continue;
        if (i != selected_ && cover->kind != CoverMessage::Kind::cover) continue;
        if (cover->kind == CoverMessage::Kind::cover) {
            cards_[i].cover_opacity = 0;
            if (cards_[i].game.kind == GameKind::platform || cards_[i].game.kind == GameKind::pc_platform) renderer_.setPlatformCover(i, asset.cover);
            else renderer_.setCover(i, asset.cover, asset.focus, edge);
            if (cropOpen() && !crop_state_.loading && !crop_state_.background) renderer_.setCoverFocus(i, crop_state_.focus);
        }
        else if (cover->kind == CoverMessage::Kind::logo) renderer_.setLogo(i, asset.logo);
        else {
            // Forced: a console/store tile's stand-in (its logo) gives way to a game's background.
            renderer_.setHero(i, asset.hero, asset.hero_tone, true, asset.hero_focus);
            if (cropOpen() && crop_state_.background && !crop_state_.loading) renderer_.setHeroFocus(i, crop_state_.focus); // Being edited.
            detail_ = 0;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (flixActive() && cover->kind == CoverMessage::Kind::hero) {
        // A capsule without its landscape art shows the wide background meanwhile.
        const auto first = flix_rows_.empty() ? size_t{0} : flix_rows_[0].count;
        for (size_t i = 0; i < first && i < cards_.size(); ++i) if (gameKey(cards_[i].game) == cover->key) bindFlixArt(i);
        // A row's "Wszystkie" tile whose category background just arrived (and the page behind it).
        for (size_t i = 0; i < cards_.size(); ++i) {
            if (cards_[i].game.kind != GameKind::category) continue;
            const auto chosen = flix_backdrops_.find(cards_[i].game.source_id);
            if (chosen == flix_backdrops_.end() || chosen->second != cover->key) continue;
            bindFlixArt(i, true);
            if (i == selected_) { renderer_.setHero(i, asset.hero, asset.hero_tone, false, asset.hero_focus); detail_ = 0; }
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
    if (cover->kind == CoverMessage::Kind::hero) metroArtArrived(cover->key, cover->kind);
    if (shelfAllTile() && !cards_.empty() && cards_[0].game.kind == GameKind::category) {
        // The default layout's "Wszystkie": its covers preview, or the background it shows.
        if (cover->kind == CoverMessage::Kind::cover) xmbBindThumbnails(0);
        const auto chosen = flix_backdrops_.find(cards_[0].game.source_id);
        if (cover->kind == CoverMessage::Kind::hero && chosen != flix_backdrops_.end() && chosen->second == cover->key) {
            bindShelfAllTile(0, true);
            if (selected_ == 0) { renderer_.setHero(0, asset.hero, asset.hero_tone, false, asset.hero_focus); detail_ = 0; }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (psfiveActive()) {
        // PSfive's category tiles (a row's "Wszystkie"): their previews, and focused,
        // the background the tile shows.
        if (cover->kind == CoverMessage::Kind::cover)
            for (size_t i = 0; i < cards_.size(); ++i) if (cards_[i].game.kind == GameKind::category) xmbBindThumbnails(i);
        if (cover->kind == CoverMessage::Kind::hero && selected_ < cards_.size() && cards_[selected_].game.kind == GameKind::category) {
            const auto chosen = flix_backdrops_.find(cards_[selected_].game.source_id);
            if (chosen != flix_backdrops_.end() && chosen->second == cover->key) { renderer_.setHero(selected_, asset.hero, asset.hero_tone, false, asset.hero_focus); detail_ = 0; }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (libraryLandscape() && cover->kind == CoverMessage::Kind::hero) requestVisibleArt();
    if (xmbActive() && cover->kind == CoverMessage::Kind::cover) {
        // Collection tiles preview their first covers; bind the one that just arrived.
        for (size_t i = 0; i < xmb_top_.size() && i < cards_.size(); ++i) xmbBindThumbnails(i);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    trimArt();
}

void Launcher::trimArt() {
    if (art_.bytes() <= art_.budget()) return;
    // Kept: the focused card and every card whose cover is bound in the renderer (on
    // screen or next to it); the renderer shares those buffers anyway. In low memory the renderer
    // lets bytes go once uploaded: only those still waiting for their decode are pinned.
    std::unordered_set<std::string> pinned;
    for (size_t i = 0; i < cards_.size(); ++i)
        if (i == selected_ || (settings_.low_memory ? renderer_.artPending(i) : renderer_.hasCover(i) || renderer_.hasPoster(i) || renderer_.hasDisc(i)))
            pinned.insert(gameKey(cards_[i].game));
    forgetArtRequests(art_.trim([&](const std::string& key) { return pinned.contains(key); }));
}

void Launcher::forgetArtRequests(const std::vector<std::string>& dropped) {
    if (dropped.empty()) return;
    // Foreground requests stay marked as done after they arrive; a dropped image must be
    // requestable again (it is then read from the disk cache).
    std::lock_guard lock(jobs_->mutex);
    for (const auto& key : dropped)
        for (const auto kind : {CoverMessage::Kind::cover, CoverMessage::Kind::logo, CoverMessage::Kind::hero,
                                CoverMessage::Kind::poster, CoverMessage::Kind::landscape, CoverMessage::Kind::disc})
            jobs_->requested.erase(key + ':' + std::to_string(static_cast<int>(kind)));
}

void Launcher::prefetchLibraryArt() {
    std::unordered_set<std::string> allowed;
    for (const auto& game : public_library_) allowed.insert(gameKey(game));
    {
        std::lock_guard lock(jobs_->mutex);
        std::erase_if(jobs_->prefetch, [&](const Job& job) {
            if (allowed.contains(gameKey(job.game))) return false;
            jobs_->requested.erase(job.key); return true;
        });
    }
    for (const auto kind : {CoverMessage::Kind::cover, CoverMessage::Kind::hero, CoverMessage::Kind::logo})
        for (const auto& game : public_library_) requestArt(game, kind, true);
    prefetchPosters();
}

// 600x900 posters: the whole public library for XMB and the screensaver's boxes, the Flix rows'
// games for that home layout (landscape capsules for its first row). The same background lanes and
// failure/retry bookkeeping as the other artwork; JobQueue::takeReady takes covers and posters before
// backgrounds and logos, and SteamGridDB's own 1.1 s spacing (net/artwork_limits.hpp) keeps it a trickle.
void Launcher::prefetchPosters() {
    const bool boxes = settings_.screensaver && settings_.screensaver_style == AppSettings::ScreensaverStyle::boxes;
    const bool flix = settings_.home_layout == AppSettings::HomeLayout::flix;
    const bool metro = settings_.home_layout == AppSettings::HomeLayout::metro;
    const bool xmb = settings_.home_layout == AppSettings::HomeLayout::xmb && settings_.xmb_posters;
    const bool shelves = settings_.home_layout == AppSettings::HomeLayout::shelves && settings_.shelves_posters;
    const bool steam = shelves && settings_.shelves_steam; // Wygląd Steam: a wide capsule for Ostatnio grane's first tile.
    const bool desktop = desktop_mode_; // The desktop window's grid: a poster for every game.
    {   // Poster tiles: a game missing both gets its poster first.
        std::lock_guard lock(jobs_->mutex);
        jobs_->posters_first = desktop || xmb || shelves;
    }
    if ((!boxes && !xmb && !shelves && !desktop) || !(flix || metro)) {
        // Posters/capsules still queued for a style or layout no longer in use are not needed any more.
        std::lock_guard lock(jobs_->mutex);
        std::erase_if(jobs_->prefetch, [&](const Job& job) {
            const bool unused = (job.kind == CoverMessage::Kind::poster && !boxes && !flix && !metro && !xmb && !shelves && !desktop) ||
                                (job.kind == CoverMessage::Kind::landscape && !flix && !metro && !steam);
            if (unused) jobs_->requested.erase(job.key);
            return unused;
        });
    }
    if (!runtime_started_ || (!boxes && !flix && !metro && !xmb && !shelves && !desktop)) return;
    const auto queue = [&](const Game& game, CoverMessage::Kind kind) { if (isGame(game)) requestArt(game, kind, true); };
    if (flix) for (size_t i = 0; i < categories_.size(); ++i)
        for (const auto& game : categories_[i].games) queue(game, i ? CoverMessage::Kind::poster : CoverMessage::Kind::landscape);
    // Metro: wide tiles for the recent games, posters for the games each category page shows.
    if (metro) for (size_t i = 0; i < categories_.size(); ++i) {
        size_t shown = 0;
        for (const auto& game : categories_[i].games) {
            if (!isGame(game) || (i && shown++ + 1 >= metro_layout::category_tiles)) continue;
            queue(game, i ? CoverMessage::Kind::poster : CoverMessage::Kind::landscape);
        }
    }
    if (metro) if (const auto* install = metroInstallGame()) queue(*install, CoverMessage::Kind::poster);
    if (boxes || xmb || desktop) for (const auto& game : public_library_) queue(game, CoverMessage::Kind::poster);
    else if (shelves) for (const auto& category : categories_) for (const auto& game : category.games) queue(game, CoverMessage::Kind::poster);
    if (steam && !categories_.empty() && !categories_[0].games.empty()) queue(categories_[0].games.front(), CoverMessage::Kind::landscape);
}

void Launcher::invalidateArtwork(const Game& game, CoverMessage::Kind kind) {
    const auto key = artRequestKey(game, kind);
    const auto revision = ++art_revisions_[key];
    std::lock_guard lock(jobs_->mutex);
    jobs_->revisions[key] = revision;
    jobs_->requested.erase(key); jobs_->blocked_until.erase(key); jobs_->disk_cached.erase(key);
    for (auto* queue : {&jobs_->jobs, &jobs_->secondary, &jobs_->prefetch})
        std::erase_if(*queue, [&](const Job& job) { return job.key == key; });
}

void Launcher::retryGameArtwork(const Game& game, bool notify) {
    if (!isGame(game)) return;
    const auto failures = coverCacheDirectory().parent_path() / L"artwork" / L"failures";
    const auto time = artwork_failure::now();
    std::vector<CoverMessage::Kind> kinds{CoverMessage::Kind::cover, CoverMessage::Kind::logo, CoverMessage::Kind::hero};
    if (settings_.home_layout == AppSettings::HomeLayout::flix || settings_.home_layout == AppSettings::HomeLayout::metro) {
        kinds.push_back(CoverMessage::Kind::poster); kinds.push_back(CoverMessage::Kind::landscape);
    }
    else if ((settings_.home_layout == AppSettings::HomeLayout::xmb && settings_.xmb_posters) ||
             (settings_.home_layout == AppSettings::HomeLayout::shelves && settings_.shelves_posters) ||
             desktop_mode_) kinds.push_back(CoverMessage::Kind::poster); // The desktop window's grid shows posters.
    if (settings_.home_layout == AppSettings::HomeLayout::shelves && settings_.shelves_steam &&
        std::find(kinds.begin(), kinds.end(), CoverMessage::Kind::landscape) == kinds.end()) kinds.push_back(CoverMessage::Kind::landscape);
    if ((libraryLandscape() || libraryGridLandscape()) && std::find(kinds.begin(), kinds.end(), CoverMessage::Kind::landscape) == kinds.end()) kinds.push_back(CoverMessage::Kind::landscape);
    if (library_view::posterArt(libraryPresentation()) && std::find(kinds.begin(), kinds.end(), CoverMessage::Kind::poster) == kinds.end()) kinds.push_back(CoverMessage::Kind::poster);
    if (libraryCoverflow()) kinds.push_back(CoverMessage::Kind::disc);
    for (const auto kind : kinds) if (!artwork_failure::retry(failures, artRequestKey(game, kind), time)) {
        showNotification(NotificationKind::error, tr(L"Nie można wyczyścić stanu pobierania"), L""); return;
    }
    for (const auto kind : kinds) { invalidateArtwork(game, kind); requestArt(game, kind); }
    if (notify) showNotification(NotificationKind::launch, tr(L"Ponawianie grafik"), L"");
}

bool Launcher::requestArt(const Game& game, CoverMessage::Kind kind, bool background,
                          std::chrono::steady_clock::time_point ready_at) {
    if (gameKey(game).empty()) return false;
    if (game.kind == GameKind::pc_platform && kind != CoverMessage::Kind::cover && kind != CoverMessage::Kind::badge &&
        kind != CoverMessage::Kind::hero) return false;
    if (game.kind == GameKind::platform && kind == CoverMessage::Kind::logo) return false;
    if ((kind == CoverMessage::Kind::poster || kind == CoverMessage::Kind::landscape || kind == CoverMessage::Kind::disc) && !isGame(game)) return false;
    if (!game.installed && !metadata_ready_ && i18n::matches(game.name, loading_game_name)) return false;
    std::lock_guard lock(jobs_->mutex);
    const auto key = artRequestKey(game, kind);
    jobs_->revisions[key] = art_revisions_[key];
    if (background && jobs_->disk_cached.contains(key)) return false;
    if (const auto blocked = jobs_->blocked_until.find(key); blocked != jobs_->blocked_until.end()) {
        if (std::chrono::steady_clock::now() < blocked->second) return false;
        jobs_->blocked_until.erase(blocked);
    }
    const bool selected = !background && selected_ < cards_.size() && gameKey(cards_[selected_].game) == gameKey(game);
    if (selected) jobs_->focus = gameKey(game);
    if (!jobs_->requested.insert(key).second) {
        if (!background) {
            const auto queued = std::find_if(jobs_->prefetch.begin(), jobs_->prefetch.end(), [&](const Job& job) { return job.key == key; });
            if (queued != jobs_->prefetch.end()) {
                // Wanted on screen now: the background lane's spacing (up to minutes for the last
                // Flix poster) no longer applies, or visible tiles appear one by one, 400 ms apart.
                // A retry keeps its backoff.
                auto job = std::move(*queued); jobs_->prefetch.erase(queued); job.prefetch = false;
                if (!job.retry_count) job.ready_at = {};
                if (kind == CoverMessage::Kind::cover || (kind == CoverMessage::Kind::poster && desktopActive())) jobs_->jobs.push_front(std::move(job));
                else jobs_->secondary.push_front(std::move(job));
                jobs_->wake.notify_all();
            }
        }
        if (selected && kind == CoverMessage::Kind::cover) {
            const auto pending = std::find_if(jobs_->jobs.begin(), jobs_->jobs.end(), [&](const Job& job) { return job.key == key; });
            if (pending != jobs_->jobs.end()) {
                auto job = std::move(*pending);
                jobs_->jobs.erase(pending);
                jobs_->jobs.push_front(std::move(job));
            }
        }
        if (selected && kind == CoverMessage::Kind::disc) {
            const auto pending = std::find_if(jobs_->secondary.begin(), jobs_->secondary.end(), [&](const Job& job) { return job.key == key; });
            if (pending != jobs_->secondary.end() && !pending->retry_count) { pending->ready_at = {}; jobs_->wake.notify_all(); }
        }
        return false;
    }
    Job job{game, kind, jobs_->generation.load(),
            kind == CoverMessage::Kind::hero && (game.kind == GameKind::platform || game.kind == GameKind::pc_platform)};
    job.revision = art_revisions_[key];
    job.prefetch = background;
    job.ready_at = ready_at;
    job.key = key;
    if (job.platform_background) job.backdrop_games = backdropGames(game);
    // The desktop grid's posters are its visible art: the first lane, as covers elsewhere.
    const bool visible_lane = kind == CoverMessage::Kind::cover || (kind == CoverMessage::Kind::poster && desktopActive());
    if (background) jobs_->prefetch.push_back(std::move(job));
    else if (kind == CoverMessage::Kind::cover && selected) jobs_->jobs.push_front(std::move(job));
    else if (visible_lane) jobs_->jobs.push_back(std::move(job));
    else jobs_->secondary.push_back(std::move(job));
    jobs_->wake.notify_all();
    return true;
}

// A console or store tile's background candidates: its games in random order (a new pick
// each time the tile's background is requested). Bounded, so a store with thousands of
// games costs the worker a few hundred cache probes at most.
std::vector<Game> Launcher::backdropGames(const Game& tile) {
    constexpr size_t limit = 200;
    std::vector<const Game*> pool;
    if (tile.kind == GameKind::platform) {
        if (const auto* games = consoleGames(tile.platform))
            for (const auto& game : *games) pool.push_back(&game);
    } else if (tile.kind == GameKind::pc_platform) {
        // The games its library page lists (openPcLibrary).
        for (const auto& game : public_library_)
            if (game.source == tile.source && game.kind != GameKind::nonsteam && isGame(game)) pool.push_back(&game);
    }
    std::shuffle(pool.begin(), pool.end(), flix_random_);
    if (pool.size() > limit) pool.resize(limit);
    std::vector<Game> games;
    games.reserve(pool.size());
    for (const auto* game : pool) games.push_back(*game);
    return games;
}

void Launcher::bindLibraryLandscape(size_t index, bool replace) {
    if (index >= cards_.size()) return;
    const auto& game = cards_[index].game;
    const auto* art = art_.find(gameKey(game));
    if (art && art->landscape) {
        if (replace || !renderer_.hasPoster(index)) renderer_.setPoster(index, art->landscape);
    } else {
        requestArt(game, CoverMessage::Kind::landscape);
        if (art && art->hero) {
            if (replace || !renderer_.hasPoster(index)) renderer_.setPoster(index, art->hero);
        } else if (index == selected_ || artBlocked(game, CoverMessage::Kind::landscape)) requestArt(game, CoverMessage::Kind::hero);
    }
}

void Launcher::bindLibraryPoster(size_t index, bool replace) {
    if (index >= cards_.size()) return;
    if (shelfWideCard(index)) { bindLibraryLandscape(index, replace); return; } // Wygląd Steam: the wide first tile.
    const auto& game = cards_[index].game;
    if (const auto* art = art_.find(gameKey(game)); art && art->poster) {
        if (replace || !renderer_.hasPoster(index)) renderer_.setPoster(index, art->poster);
    } else if (replace || !renderer_.hasPoster(index)) requestArt(game, CoverMessage::Kind::poster);
}

void Launcher::requestVisibleArt() {
    if (friends_open_ && plugin_drawer_active_) requestPluginPageArt();
    if (screen_ == ScreenMode::plugin_page) { requestPluginPageArt(); return; }
    if (downloadsOpen()) { requestDownloadsArt(); return; }
    if (cropOpen()) return;
    if (cards_.empty()) return;
    if (screen_ == ScreenMode::settings) return;
    if (desktopActive()) { desktopRequestArt(); return; }
    {
        std::lock_guard lock(jobs_->mutex);
        jobs_->focus = gameKey(cards_[selected_].game);
        // A look-ahead is bounded to the current game and +1; rapid navigation discards stale queued work.
        const auto next = libraryCoverflow() && selected_ + 1 < cards_.size() ? gameKey(cards_[selected_ + 1].game) : std::string{};
        for (auto* queue : {&jobs_->jobs, &jobs_->secondary, &jobs_->prefetch})
            std::erase_if(*queue, [&](const Job& job) {
                if (job.kind != CoverMessage::Kind::disc || (libraryCoverflow() && (gameKey(job.game) == jobs_->focus || gameKey(job.game) == next))) return false;
                jobs_->requested.erase(job.key); return true;
            });
        if (libraryLandscape()) {
            // Square covers left over from another view are no longer foreground work.
            for (auto it = jobs_->jobs.begin(); it != jobs_->jobs.end();) {
                if (it->kind == CoverMessage::Kind::cover && gameKey(it->game) != jobs_->focus) {
                    auto job = std::move(*it); it = jobs_->jobs.erase(it); job.prefetch = true;
                    jobs_->prefetch.push_front(std::move(job));
                } else ++it;
            }
        }
    }
    const auto cover = [&](size_t i) {
        const auto* found = art_.find(gameKey(cards_[i].game));
        if (found && found->cover) {
            if (!renderer_.hasCover(i)) {
                cards_[i].cover_opacity = 0;
                if (cards_[i].game.kind == GameKind::platform || cards_[i].game.kind == GameKind::pc_platform) renderer_.setPlatformCover(i, found->cover);
                else renderer_.setCover(i, found->cover, found->focus);
            }
        } else if (!renderer_.hasCover(i)) requestArt(cards_[i].game, CoverMessage::Kind::cover); // Shown: its bytes may be let go (low memory).
    };
    // Long lists keep cover bitmaps only near the viewport: a card far outside [keep_first,
    // keep_end) gives its bitmap back (GPU memory no longer grows with every card ever
    // scrolled past) and is decoded again from RAM/disk when it returns.
    const auto release = [&](size_t keep_first, size_t keep_end) {
        for (size_t i = 0; i < cards_.size(); ++i)
            if ((i < keep_first || i >= keep_end) && i != selected_) {
                renderer_.releaseCover(i); renderer_.releasePoster(i);
                if (libraryPreview()) renderer_.releasePreview(i);
            }
    };
    if (shelfAllTile() && cards_[0].game.kind == GameKind::category) bindShelfAllTile(0);
    if (libraryCarousel()) {
        const auto view = renderer_.viewport();
        const auto [first, end] = coverflow_layout::Layout{view.width, view.height, libraryView(), libraryPresentation().coverflow_cd}.visibleRange(cards_.size(), selected_);
        for (size_t i = first; i < end; ++i) {
            cover(i);
            if (library_view::posterArt(libraryPresentation())) bindLibraryPoster(i);
        }
        release(first, end);
    } else if (libraryListPlus()) {
        bindLibraryPoster(selected_);
        release(selected_, selected_ + 1); // The right-hand list has titles only.
    } else if (desktop_mode_ && screen_ == ScreenMode::store) {
        // Sklep's page: a search's results, each as a poster (its cover meanwhile).
        for (size_t i = 0; i < cards_.size(); ++i) { cover(i); bindLibraryPoster(i); }
    } else if (gridScreen(screen_)) {
        const auto view = renderer_.viewport();
        const auto [first, end] = library_layout::visibleRange(libraryView(), cards_.size(), view.width, view.height, grid_scroll_, libraryPresentation());
        for (size_t i = first; i < end; ++i) {
            if (libraryLandscape() || libraryGridLandscape()) bindLibraryLandscape(i);
            else cover(i);
            if (libraryGridLandscape()) cover(i); // A stand-in while the capsule downloads (shown whole).
            if (libraryGridPosters()) bindLibraryPoster(i);
        }
        const size_t margin = (settings_.low_memory ? 1 : 2) * (end - first); // Two screens above and below (low memory: one).
        release(first > margin ? first - margin : 0, end + margin);
    } else if (xmbActive()) {
        xmbRequestArt();
    } else if (flixActive()) {
        flixRequestArt();
    } else if (metroActive()) {
        metroRequestArt();
    } else if (psfiveActive()) {
        // The row from its left-aligned focused tile to the right edge, and two more;
        // a long shelf (Ctrl+H, a couch collection) gives the far ones back.
        const auto view = renderer_.viewport();
        const size_t before = static_cast<size_t>(psfive_layout::visible_before);
        const size_t first = selected_ > before ? selected_ - before : 0;
        const size_t end = std::min(cards_.size(), selected_ + static_cast<size_t>(psfive_layout::visibleAfter(view.width, psfiveScale())) + 2);
        for (size_t i = first; i < end; ++i) {
            if (cards_[i].game.kind == GameKind::category) xmbBindThumbnails(i); // "Wszystkie": a 2x2 preview.
            else cover(i);
        }
        const size_t behind = settings_.low_memory ? 6 : 16, ahead = settings_.low_memory ? 10 : 24;
        release(first > behind ? first - behind : 0, end + ahead);
    } else if (category_ == 0) {
        const bool posters = shelfPosters();
        for (size_t i = 0; i < cards_.size(); ++i) { cover(i); if (posters) bindLibraryPoster(i); }
    } else {
        const bool posters = shelfPosters();
        const size_t first = selected_ > 0 ? selected_ - 1 : 0;
        // Every card the row shows (as many as the screen is wide: 11 or more on a 16:9 screen,
        // posters as wide as square covers), then two more so the next ones scrolled in are ready.
        const auto view = renderer_.viewport();
        const float inset = categoryRailActive() ? category_rail::Layout{view.width, view.height}.right() : 0;
        const size_t end = std::min(cards_.size(), carousel::visibleEnd(selected_, cards_.size(), view.width - inset) + 2);
        for (size_t i = first; i < end; ++i) {
            cover(i);
            if (posters) bindLibraryPoster(i);
        }
        const size_t behind = settings_.low_memory ? 8 : 24, ahead = settings_.low_memory ? 16 : 40;
        release(selected_ > behind ? selected_ - behind : 0, selected_ + ahead); // A long collection shelf.
    }
    cover(selected_); // Promote current focus after enqueueing surrounding cards.
    if (settings_.low_memory) releaseFarHeroes();
    for (size_t i = 0; i < cards_.size(); ++i)
        if (!libraryCoverflow() || ((i < selected_ || i > selected_ + 1) && cards_[i].disc < 0.003f)) renderer_.releaseDisc(i);
    if (libraryCoverflow()) for (size_t i = selected_; i < std::min(cards_.size(), selected_ + 2); ++i) {
        if (const auto* art = art_.find(gameKey(cards_[i].game)); art && art->disc) renderer_.setDisc(i, art->disc);
        else requestArt(cards_[i].game, CoverMessage::Kind::disc, false,
            i == selected_ ? std::chrono::steady_clock::time_point{} : std::chrono::steady_clock::now() + std::chrono::milliseconds(350));
    }
    if (libraryLandscape()) bindLibraryLandscape(selected_);
    if (snitchActive()) return; // Its home shows covers on a plain background; details request their own art.
    const auto& selected = cards_[selected_].game;
    requestArt(selected, CoverMessage::Kind::badge);
    if (screen_ == ScreenMode::shelves || screen_ == ScreenMode::platform_games || screen_ == ScreenMode::details || libraryPreview()) {
        requestArt(selected, CoverMessage::Kind::hero);
        if (!libraryListPlus() && !libraryBookshelf()) requestArt(selected, CoverMessage::Kind::logo); // Their titles are text.
    } else if (libraryGridBackdrop()) {
        // The grid: the focused game's background behind the covers (no logo; titles are on the tiles).
        {
            // Fast scrolling leaves no queue of backgrounds for games already passed.
            std::lock_guard lock(jobs_->mutex);
            std::erase_if(jobs_->secondary, [&](const Job& job) {
                if (job.kind != CoverMessage::Kind::hero || job.retry_count || gameKey(job.game) == gameKey(selected)) return false;
                jobs_->requested.erase(job.key); return true;
            });
        }
        requestArt(selected, CoverMessage::Kind::hero);
    }
}
