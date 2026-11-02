// Library -> shelves: applying scanned libraries and collection names, building the
// categories, and turning the current screen's source list into cards.
#include "launcher.hpp"
#include "library_snapshot.hpp"
#include "../artwork/cover_cache.hpp"
#include "../library/owned_games.hpp"
#include "../library/emulation_platforms.hpp"
#include "../library/emulation_scan.hpp"
#include "../library/search.hpp"
#include "../ui/grid_layout.hpp"
#include "../ui/psfive_layout.hpp"
#include "../ui/snitch_layout.hpp"

#include <algorithm>

void Launcher::onLibrary(std::unique_ptr<LibraryMessage> library) {
    // Steam Family Sharing through Aurelia: the borrowed games join the client's library.
    if (aurelia_active_ && !aurelia_library_source_ && !library->installation_update) mergeAureliaFamily(*library);
    if (aurelia_active_) for (auto* list : {&library->games, &library->private_games}) for (auto& game : *list)
        if (const auto found = aurelia_downloads_.find(game.app_id); found != aurelia_downloads_.end()) { game.installation = found->second.progress; game.installation.update = game.installed; }
    if (settings_.show_private) revealPrivateSteamGames(*library);
    if (library->installation_update) { updateSteamInstallations(*library); return; }
    // A rescan asked for by Settings (same account) replaces the games but keeps the screen.
    const bool refresh = library->refresh && !scanning_ && library->account == account_;
    const auto selected = cards_.empty() ? std::string{} : library_view::selectionKey(cards_[selected_].game);
    const auto category_id = categories_.empty() ? std::string{} : categories_[category_].id;
    if (!refresh) { rememberPlace(); library_transition_ = true; }
    scanning_ = false;
    if (!refresh) {
        std::lock_guard lock(jobs_->mutex);
        jobs_->generation.fetch_add(1);
        jobs_->jobs.clear();
        jobs_->secondary.clear();
        jobs_->prefetch.clear(); jobs_->disk_cached.clear();
        jobs_->requested.clear();
        jobs_->blocked_until.clear();
        jobs_->revisions.clear();
    }
    if (account_ != library->account) {
        glance_cancel_->store(true); glance_pending_ = false; glance_.feed = {}; glance_roster_.clear();
        renderer_.resetGlanceThumbnails(0);
        // The companion talked about the previous account's games: a new conversation, a new greeting.
        if (account_ && (!companion_.chat.log.empty() || companion_.busy)) { companionReset(); companion_.greeted = false; }
        steam_sync_snapshot_.reset(); steam_sync_error_.clear(); steam_sync_cloud_pending_ = false;
        if (bulk_control_) bulk_control_->cancel = true;
        bulk_ui_.open = false;
        plugin_library_ids_.clear();
        uninstalled_library_.clear();
        ++plugin_library_generation_;
        library_dialog_ = {}; // Never edit the previous account's Steam overlays.
        preview_.clear(); // Play time and achievements belong to the signed-in account.
        ratings_alive_->store(false); ratings_alive_ = std::make_shared<std::atomic_bool>(true);
        ++ratings_generation_; ratings_started_ = false; ratings_.clear(); rawg_metadata_.clear(); emulation_metadata_.clear(); emulation_names_.clear(); manual_names_.clear();
        ++*identify_token_; identify_state_ = {};
        crop_token_->fetch_add(1); pointerUp();
        {
            std::lock_guard lock(artwork_jobs_->mutex);
            artwork_jobs_->generation.fetch_add(1);
            artwork_jobs_->jobs.clear();
        }
        renderer_.resetArtworkThumbnails(0);
        art_revisions_.clear();
        store_jobs_->generation.fetch_add(1);
        store_results_.clear();
        detail_games_.clear();
        store_loading_ = details_loading_ = false;
        account_ = library->account;
        completed_installs_.clear();
        art_.clear();
        renderer_.resetCovers(0, false);
        if (friends_open_) closeFriends();
        private_shelf_ = false; // Another account: its private games are not shown by default.
        category_ = 0;
        pending_launch_ = false;
        launch_progress_ = 0;
        if (game_sessions_.empty()) audio_.resumeMusic();
        screen_ = ScreenMode::shelves;
        quick_open_ = false;
        search_query_.clear(); search_after_ = 0;
    }
    installed_library_ = std::move(library->games);
    collections_source_ = steam_sync_snapshot_ && steam_sync_snapshot_->account == account_ ? steam_sync_snapshot_->collections : std::move(library->collections);
    excluded_ids_ = std::move(library->excluded);
    shortcut_library_.clear();
    private_category_.games = std::move(library->private_games);
    for (auto& shortcut : library->shortcuts) {
        if (shortcut.hidden || excluded_ids_.contains(shortcut.game.app_id)) {
            shortcut.game.visibility = GameVisibility::private_game;
            private_category_.games.push_back(std::move(shortcut.game));
        } else shortcut_library_.push_back(std::move(shortcut.game));
    }
    // Names from shortcuts.vdf, then the ROM-matched titles when chosen (Biblioteka).
    shortcut_original_names_.clear();
    for (const auto& shortcut : shortcut_library_) shortcut_original_names_[gameKey(shortcut)] = shortcut.name;
    renameShortcuts();
    private_category_.selected = 0;
    rebuildUninstalledSteamGames(library->cached_names);
    buildShelves();
    if (refresh) syncGlanceLibrary();
    requestSteamSync();
    refreshPluginLibraries();
    if (refresh) {
        // "Pokaż ukryte/prywatne gry" switched off: games hidden again leave the views open on them.
        const auto hidden = [&](const Game& game) {
            const auto key = gameKey(game);
            return !private_shelf_ && std::any_of(private_category_.games.begin(), private_category_.games.end(),
                [&](const Game& item) { return item.source == GameSource::steam && gameKey(item) == key; });
        };
        if (pending_launch_ && hidden(pending_game_)) { pending_launch_ = false; launch_progress_ = 0; }
        closeEditorsFor(hidden);
        if (screen_ == ScreenMode::details && !detail_games_.empty() && hidden(detail_games_[0])) { suspendDetails(); showCategory(0); return; }
        const auto category = std::find_if(categories_.begin(), categories_.end(), [&](const Category& entry) { return entry.id == category_id; });
        if (category != categories_.end()) category_ = static_cast<size_t>(category - categories_.begin());
        if (screen_ == ScreenMode::search) updateSearch();
        else if (screen_ == ScreenMode::shelves || screen_ == ScreenMode::library) rebuildCards();
        if (const auto found = findCard(selected, true)) focusCard(*found, library_controls_.focus < 0);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    closeCategoryMenu();
    showCategory(0);
    // The PC scan reads no Steam data: one already running (started by the GOG/Epic restore)
    // is kept rather than cancelled and repeated, which would delay the store games again.
    if (!pc_scanning_) refreshPcLibraries();
    library_transition_ = false;
    restorePlace();
    focusCouchCategory(); // Pad census may have arrived before the asynchronous library scan.
}

void Launcher::rebuildUninstalledSteamGames(const std::unordered_map<uint32_t, std::wstring>& cached_names) {
    const auto generation = metadata_token_->fetch_add(1) + 1;
    metadata_ready_ = true;
    auto names = cached_names;
    // Preserve resolved names on plugin refreshes and when a manifest disappears.
    for (const auto* list : {&uninstalled_library_, &public_library_}) for (const auto& game : *list)
        if (game.kind == GameKind::steam && game.source == GameSource::steam && !game.name.empty() && !placeholderGameName(game.name))
            names.try_emplace(game.app_id, game.name);
    uninstalled_library_.clear();
    if (!account_) return;
    std::vector<uint32_t> missing;
    std::unordered_set<uint32_t> included;
    for (const auto& game : installed_library_) included.insert(game.app_id);
    for (const auto& shortcut : shortcut_library_) included.insert(shortcut.app_id);
    auto member_ids = localSteamMembers(library_state_.collections, account_);
    for (const auto& collection : collections_source_) member_ids.insert(member_ids.end(), collection.app_ids.begin(), collection.app_ids.end());
    for (const auto& [plugin, ids] : plugin_library_ids_) member_ids.insert(member_ids.end(), ids.begin(), ids.end());
    for (uint32_t id : member_ids) {
        // Supplements add identities only; manifests and visibility always win.
        if (!id || id == 480 || steamShortcutId(id) || excluded_ids_.contains(id) || !included.insert(id).second) continue;
        Game game; game.app_id = id; game.installed = false;
        const auto found = names.find(id);
        game.name = found == names.end() ? tr(loading_game_name) : found->second;
        if (steamTool(game)) continue; // Ubisoft Connect, FramePacer, runtimes: not games.
        if (placeholderGameName(game.name)) missing.push_back(id);
        uninstalled_library_.push_back(std::move(game));
    }
    if (missing.empty()) return;
    metadata_ready_ = false;
    const auto alive = alive_;
    const auto token = metadata_token_;
    const auto account = account_;
    const auto hwnd = hwnd_;
    work_.start([alive, token, account, generation, hwnd, ids = std::move(missing)] {
        const auto cache = coverCacheDirectory().parent_path() / L"metadata";
        const auto publish = [hwnd, alive, token, account, generation](
            const std::unordered_map<uint32_t, std::wstring>& names, bool finished) {
            if (!alive->load() || token->load() != generation) return;
            auto message = std::make_unique<MetadataMessage>();
            message->account = account; message->generation = generation;
            message->finished = finished; message->names = names;
            if (PostMessageW(hwnd, metadata_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        };
        auto names = collectionGameNames(account, ids, cache, alive.get(),
            [&](const auto& ready) { publish(ready, false); });
        publish(names, true);
    });
}

void Launcher::buildShelves() {
    // Steam's scanner reports store-hidden/private games as private_game. Our hidden copies are
    // derived afresh from the unmodified source lists; never carry them over into the next build.
    std::erase_if(private_category_.games, [this](const Game& game) {
        return game.source != GameSource::steam || !settings_.steam_enabled || (game.kind == GameKind::steam && game.visibility == GameVisibility::hidden) ||
               excludedGame(game);
    });
    for (const auto& game : store_private_library_)
        if (!excludedGame(game) && settings_.enabled_pc_sources.contains(sourceSlug(game.source))) private_category_.games.push_back(game);
    public_library_.clear();
    // Hidden here (Szczegóły → "…" → "Ukryj"): with the store's own hidden/private games, Ctrl+H only.
    const auto user_hidden = [&](const Game& game) {
        if (excludedGame(game)) return true; // Wykluczone: nowhere, not even with Ctrl+H.
        if (!locallyHidden(game)) return false;
        auto copy = game; copy.visibility = GameVisibility::hidden;
        private_category_.games.push_back(std::move(copy));
        return true;
    };
    // Steam switched off (Ustawienia → Biblioteka): none of its games, shortcuts or collections.
    const bool steam = settings_.steam_enabled;
    if (steam) for (const auto& game : installed_library_) // A preload is on disk too (Steam waits for the release).
        if (!user_hidden(game) && (game.installed || game.installation.active() || game.preloaded || settings_.show_uninstalled)) public_library_.push_back(game);
    for (const auto& game : store_library_)
        if (!excludedGame(game) && settings_.enabled_pc_sources.contains(sourceSlug(game.source)) && !user_hidden(game) &&
            (game.installed || game.installation.active() || settings_.show_uninstalled)) public_library_.push_back(game);
    // DRM-free games on an unplugged drive count as not installed.
    const bool drm_enabled = settings_.enabled_pc_sources.contains(sourceSlug(GameSource::drmfree));
    size_t drm_games = 0, custom_games = 0;
    if (drm_enabled) for (const auto& game : drm_library_)
        if (!user_hidden(game) && (game.installed || settings_.show_uninstalled)) { public_library_.push_back(game); ++drm_games; }
    // User-added games stay listed while their file is missing ("Brak pliku"): the user put them there.
    // An emulated one (a ROM with its platform) stands on its console's shelf, not under Moje gry.
    for (const auto& game : custom_library_)
        if (!user_hidden(game)) { public_library_.push_back(game); if (game.kind != GameKind::nonsteam) ++custom_games; }
    if (steam) for (const auto& game : uninstalled_library_)
        if (!user_hidden(game) && settings_.show_uninstalled) public_library_.push_back(game);
    // Emulated games: with launchme's own parsers (Ustawienia → Biblioteka → Gry emulowane) the ROM
    // folder replaces Steam ROM Manager's shortcuts of emulated platforms; a ROM a user-added entry
    // (or a kept shortcut) already lists stays once.
    const bool roms = romLibraryActive();
    std::unordered_set<std::wstring> listed_roms;
    for (const auto& game : custom_library_) if (!game.rom.empty()) listed_roms.insert(emulation_scan::romKey(game.rom));
    std::vector<const Game*> emulated;
    if (steam) for (const auto& shortcut : shortcut_library_) {
        if (roms && shortcut.platform != L"PC") continue;
        if (excludedGame(shortcut)) continue;
        public_library_.push_back(shortcut);
        // A Steam shortcut to a PC program is no emulated game: in the library, not on Emulacja's consoles.
        if (shortcut.platform != L"PC") emulated.push_back(&shortcut);
        if (!shortcut.rom.empty()) listed_roms.insert(emulation_scan::romKey(shortcut.rom));
    }
    if (roms) for (const auto& game : rom_library_)
        if (!user_hidden(game) && listed_roms.insert(emulation_scan::romKey(game.rom)).second) { public_library_.push_back(game); emulated.push_back(&game); }
    for (const auto& game : custom_library_) if (game.kind == GameKind::nonsteam && !user_hidden(game)) emulated.push_back(&game);
    if (private_shelf_ && private_category_.games.empty()) private_shelf_ = false;
    // Ctrl+H: hidden and private games join the shelves, grids and search for this session. They
    // keep their visibility, so play history, the bookmark and the screensaver still skip them.
    if (private_shelf_)
        for (const auto& game : private_category_.games)
            if (game.installed || game.installation.active() || settings_.show_uninstalled)
                public_library_.push_back(game);
    applyIdentifiedNames(public_library_); // Zidentyfikuj grę (and ROM titles), under the player's own titles.
    applyMetadataTitles(public_library_); // Edytuj metadane: the player's own titles (before grouping and sorting).
    recent_games::apply(public_library_, library_state_.last_played, account_, gog_state_.account, epic_state_.account);
    recordLibraryGames(); // Record fallbacks before sorting and grouping this snapshot.
    versions_ = versions::Index(public_library_);
    grouped_library_ = versions_.collapse(public_library_, library_state_.preferred_versions, library_state_.first_seen);
    if (screen_ == ScreenMode::details && !detail_games_.empty() && !detail_games_[0].version_group.empty()) {
        const auto group = versions_.by_game.find(gameKey(detail_games_[0]));
        detail_games_[0].version_group = group == versions_.by_game.end() ? std::string{} : group->second;
        if (!cards_.empty()) cards_[0].game.version_group = detail_games_[0].version_group;
    }
    categories_.clear();
    category_titles_.clear();
    private_category_.title = tr(L"Ukryte");
    Category recent{tr(L"Ostatnio grane")};
    recent.id = "recent";
    // Sorted pointers: only the (at most 8; PSfive/snitch 12) picked games are copied, not the whole library.
    const bool psfive = settings_.home_layout == AppSettings::HomeLayout::psfive;
    const bool snitch = settings_.home_layout == AppSettings::HomeLayout::snitch;
    const size_t recent_limit = snitch ? snitch_layout::row_games : psfive ? psfive_layout::row_games : 8;
    std::vector<const Game*> played;
    played.reserve(public_library_.size());
    for (const auto& game : grouped_library_) played.push_back(&game);
    std::stable_sort(played.begin(), played.end(), [](const Game* a, const Game* b) { return a->last_played > b->last_played; });
    std::unordered_set<std::string> picked;
    const auto pick = [&](const Game& game) {
        if (recent.games.size() < recent_limit && picked.insert(gameKey(game)).second) recent.games.push_back(game);
    };
    // Order: games in play (running or starting, then suspended), new installations in progress,
    // installations finished this session, then the played games by date. An update of an
    // installed game is no new game: it keeps its place by play history, with its progress.
    if (!game_sessions_.empty())
        for (const bool frozen : {false, true})
            for (const auto* game : played)
                if (const auto* session = tileSession(*game); session && session->frozen() == frozen) pick(*game);
    for (const auto* game : played)
        if (game->installation.active() && !game->installation.update) pick(*game);
    for (const auto* game : played)
        if (game->installed && ((game->source == GameSource::steam && game->kind == GameKind::steam && completed_installs_.contains(game->app_id)) ||
            completed_pc_installs_.contains(gameKey(*game)))) pick(*game);
    if (settings_.recent_uninstalled) {
        // With the games not installed (and added without their file): those by the date they were added.
        std::vector<std::pair<uint64_t, const Game*>> timed;
        for (const auto* game : played)
            if (const auto when = recentTime(*game); when && isGame(*game) && !picked.contains(gameKey(*game))) timed.emplace_back(when, game);
        std::stable_sort(timed.begin(), timed.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& entry : timed) pick(*entry.second);
    } else
        for (const auto* game : played)
            // snitch retains uninstalled recent games too; public_library_ already respects show_uninstalled.
            if ((game->installed || game->installation.update || snitch) && game->last_played) pick(*game);
    if ((psfive || snitch) && recent.games.size() < recent_limit) {
        // The console rows stay full: installed games never played yet follow, newest additions first.
        std::vector<std::pair<uint64_t, const Game*>> added;
        for (const auto* game : played) {
            if (!game->installed || game->last_played || !isGame(*game) || picked.contains(gameKey(*game))) continue;
            added.emplace_back(library_view::addedTime(*game, library_state_.first_seen), game);
        }
        const auto count = std::min(added.size(), recent_limit - recent.games.size());
        std::partial_sort(added.begin(), added.begin() + static_cast<std::ptrdiff_t>(count), added.end(),
                          [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t i = 0; i < count; ++i) pick(*added[i].second);
    }
    // Wszystkie gry last; with Kafle Wszystkie, Glance… also Glance, Tarot and Towarzyszka, first or
    // last (homeSpecialTiles); Dodaj grę's "+" with them when it is on.
    auto specials = homeSpecialTiles();
    if (settings_.home_tiles == AppSettings::HomeTiles::first) recent.games.insert(recent.games.begin(), specials.begin(), specials.end());
    else recent.games.insert(recent.games.end(), specials.begin(), specials.end());
    categories_.push_back(std::move(recent));
    if (!settings_.enabled_pc_sources.empty() || custom_games) {
        Category pcs{tr(L"Platformy PC")};
        pcs.id = "pc-platforms";
        // DRM-free: the scanned games and those added by hand, one tile.
        for (const auto source : {GameSource::steam, GameSource::gog, GameSource::battlenet, GameSource::ea, GameSource::ubisoft, GameSource::epic, GameSource::amazon,
                                  GameSource::xbox, GameSource::multimc, GameSource::drmfree}) {
            if (source == GameSource::drmfree ? !drm_games && !custom_games :
                (source == GameSource::steam ? !steam : !settings_.enabled_pc_sources.contains(sourceSlug(source)))) continue;
            // Steam without an account here and none of its games: its tile would open an empty library.
            if (source == GameSource::steam && !account_ &&
                std::none_of(public_library_.begin(), public_library_.end(), [](const Game& game) { return game.source == GameSource::steam && isGame(game); }))
                continue;
            Game platform;
            platform.kind = GameKind::pc_platform;
            platform.source = source;
            platform.name = platform.platform = sourceName(source);
            pcs.games.push_back(std::move(platform));
        }
        categories_.push_back(std::move(pcs));
    }
    platform_games_.clear();
    for (const auto* game : emulated) platform_games_[game->platform].push_back(*game);
    if (private_shelf_)
        for (const auto& game : private_category_.games)
            if (game.kind == GameKind::nonsteam) platform_games_[game.platform].push_back(game);
    // A system's games are copies of the ROM lists: the same names as everywhere else (Zidentyfikuj,
    // ROM titles, then the player's own title from Edytuj metadane).
    for (auto& [platform, games] : platform_games_) { applyIdentifiedNames(games); applyMetadataTitles(games); }
    if (settings_.show_emulation && !platform_games_.empty()) {
        Category emulation{tr(L"Emulacja")};
        emulation.id = "emulation";
        for (const auto& entry : emulation_platforms) {
            const std::wstring name(entry.name);
            if (!platform_games_.contains(name)) continue;
            Game platform;
            platform.name = platform.platform = name;
            platform.app_id = emulation_tile_base + static_cast<uint32_t>(emulationPlatformIndex(entry));
            platform.kind = GameKind::platform;
            emulation.games.push_back(std::move(platform));
        }
        categories_.push_back(std::move(emulation));
    }
    // Wydawcy and Developerzy: a row of logos each (src/app/studio_libraries.cpp).
    refreshStudios();
    for (const auto role : {studios::Role::publisher, studios::Role::developer}) {
        const auto& list = studio_groups_[static_cast<size_t>(role)];
        if (list.empty()) continue;
        Category row{role == studios::Role::publisher ? tr(L"Wydawcy") : tr(L"Developerzy")};
        row.id = role == studios::Role::publisher ? "publishers" : "developers";
        for (const auto& entry : list) row.games.push_back(studioTile(role, entry));
        categories_.push_back(std::move(row));
    }
    settings_collections_ = managedCollections(account_, collections_source_, library_state_.collections);
    std::unordered_map<std::string, size_t> positions;
    for (size_t i = 0; i < settings_.collection_order.size(); ++i)
        positions.emplace(settings_.collection_order[i], i);
    std::stable_sort(settings_collections_.begin(), settings_collections_.end(), [&](const auto& a, const auto& b) {
        const auto left = positions.find(collectionKey(account_, a));
        const auto right = positions.find(collectionKey(account_, b));
        return (left == positions.end() ? SIZE_MAX : left->second) <
               (right == positions.end() ? SIZE_MAX : right->second);
    });
    std::vector<SteamCollection> shown; // One call indexes the library once for every shelf.
    for (const auto& collection : settings_collections_) {
        if (settings_.hidden_collections.contains(collectionKey(account_, collection))) continue;
        if (srmCollectionsHidden() && srmCollection(collection)) continue; // Emulacja (or the ROM folder) lists them by platform.
        shown.push_back(collection);
    }
    for (auto& shelf : managedShelves(account_, shown, library_state_.collections, public_library_, private_shelf_))
        categories_.push_back(Category{std::move(shelf.name), std::move(shelf.games), 0, std::move(shelf.id)});
    if (collections_source_.empty() && steam) {
        std::vector<const Game*> newest;
        newest.reserve(installed_library_.size());
        for (const auto& game : installed_library_) if (game.installed && !locallyHidden(game)) newest.push_back(&game);
        const auto count = std::min<size_t>(newest.size(), 8);
        std::partial_sort(newest.begin(), newest.begin() + static_cast<std::ptrdiff_t>(count), newest.end(),
                          [](const Game* a, const Game* b) { return a->last_updated > b->last_updated; });
        std::vector<Game> updated;
        for (size_t i = 0; i < count; ++i) updated.push_back(*newest[i]);
        if (!updated.empty() && settings_.show_updated_row) categories_.push_back(Category{tr(L"OSTATNIO ZAKTUALIZOWANE"), std::move(updated), 0, "updated"});
        Category discovery{tr(L"JESZCZE NIEGRANE")};
        discovery.id = "discovery";
        for (const auto& game : installed_library_)
            if (!locallyHidden(game) && !game.last_played && (game.installed || game.installation.active() || settings_.show_uninstalled) && discovery.games.size() < 8) discovery.games.push_back(game);
        if (!discovery.games.empty() && settings_.show_discovery_row) categories_.push_back(std::move(discovery));
    }
    // Ctrl+H: the hidden and private games as the last category, in every home layout.
    if (private_shelf_) {
        auto games = private_category_.games;
        if (!settings_.show_uninstalled)
            std::erase_if(games, [](const Game& game) { return !game.installed && !game.installation.active(); });
        categories_.push_back(Category{private_category_.title, std::move(games), 0, "private"});
    }
    for (auto& category : categories_) category.games = versions_.collapse(category.games, library_state_.preferred_versions, library_state_.first_seen);
    pc_library_.clear();
    if (pc_source_) for (const auto& game : public_library_)
        if (platformSource(game.source) == *pc_source_ && game.kind != GameKind::nonsteam) pc_library_.push_back(game);
    category_library_.clear();
    if (!library_category_.empty())
        for (const auto& category : categories_) if (category.id == library_category_) {
            category_library_title_ = category.title;
            for (const auto& game : category.games) if (isGame(game) || studioCategory(category.id)) category_library_.push_back(game); // A board: the logos.
        }
    category_ = std::min(category_, categories_.size() - 1);
    for (size_t i = 0; i < categories_.size(); ++i)
        category_titles_.push_back(i ? categories_[i].title : tr(L"Ostatnie"));
    buildSettingsRows();
    syncGameIndex();
    updateRunningCandidates();
    if (desktop_mode_) buildDesktopSidebar(); // Its sections and counts follow the same library.
    if (libraryDialogOpen()) rebuildCollectionDialog();
    if (runtime_started_) prefetchLibraryArt();
}

void Launcher::onMetadata(std::unique_ptr<MetadataMessage> message) {
    if (message->generation != metadata_token_->load() || message->account != account_) return;
    metadata_ready_ = message->finished;
    std::unordered_set<std::string> renamed;
    const auto update = [&](Game& game) {
        if (const auto* own = metadataOverride(game); own && !own->title.empty()) { game.name = own->title; return; } // Edytuj metadane.
        if (game.installed || game.source != GameSource::steam || game.kind != GameKind::steam) return;
        const auto found = message->names.find(game.app_id);
        if (found != message->names.end()) {
            if (game.name != found->second) renamed.insert(gameKey(game));
            game.name = found->second;
        }
        else if (message->finished && placeholderGameName(game.name)) game.name = tr(missing_game_name);
    };
    for (auto& game : public_library_) update(game);
    for (auto& game : uninstalled_library_) update(game);
    for (auto& game : private_category_.games) update(game);
    for (auto& category : categories_) for (auto& game : category.games) update(game);
    for (auto& card : cards_) update(card.game);
    for (auto& game : grouped_library_) update(game);
    // A resolved name may reveal a tool (Ubisoft Connect, FramePacer): it leaves the library.
    const auto tool = [](const Game& game) { return game.source == GameSource::steam && game.kind == GameKind::steam && !game.installed && steamTool(game); };
    const bool tools = (std::erase_if(uninstalled_library_, tool) + std::erase_if(public_library_, tool)) > 0;
    if (tools && !message->finished) refreshRecentGames();
    for (const auto& game : public_library_) if (renamed.contains(gameKey(game))) {
        for (const auto kind : {CoverMessage::Kind::cover, CoverMessage::Kind::logo, CoverMessage::Kind::hero}) invalidateArtwork(game, kind);
        if (const auto* art = art_.peek(gameKey(game)); art && art->cover) requestArt(game, CoverMessage::Kind::cover);
    }
    // Resolved titles can identify cross-store copies, and every order sorts by names: one rebuild,
    // which keeps the cards, the scroll and the focus (applyLibraryOptions would scroll to the top).
    if (message->finished) refreshRecentGames();
    if (runtime_started_) prefetchLibraryArt();
    if (screen_ == ScreenMode::search) { updateSearch(); return; }
    requestVisibleArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::rebuildCards(bool preserve_background) {
    if (desktopActive()) { rebuildDesktop(preserve_background); return; }
    if (snitchActive()) category_ = 0; // Restored places and couch collections cannot introduce a category row.
    if (xmbActive()) { rebuildXmb(false, preserve_background); return; }
    if (flixActive()) { rebuildFlix(preserve_background); return; }
    if (metroActive()) { rebuildMetro(preserve_background); return; }
    // The same home shelf, library, console page or search rebuilt while on screen (the startup Steam rescan,
    // the PC scan, store syncs, resolved names, the game index learning studios, the restored
    // bookmark): unchanged cards keep their decoded art, position and fade, and the focus stays on its
    // game, instead of the whole view entering again. Other screens rebuild from scratch.
    const auto shelf_id = screen_ == ScreenMode::shelves && category_ < categories_.size() ? categories_[category_].id :
        (screen_ == ScreenMode::library || screen_ == ScreenMode::platform_games) ? "screen:" + std::to_string(static_cast<int>(screen_)) + ':' + libraryScope() :
        screen_ == ScreenMode::search ? "screen:search:" + text_encoding::utf8(search_shown_query_) :
        std::string{};
    // A view's look decides which artwork its slots hold: another view or tile shape starts afresh.
    auto presentation = screen_ == ScreenMode::library ? libraryOptions() : library_view::Options{};
    presentation.order = {}; presentation.installed_only = false;
    const bool same_list = !shelf_id.empty() && shelf_id == shelf_cards_category_ && presentation == shelf_cards_presentation_ &&
        shelf_slots_ == renderer_.slotGeneration() && shelf_card_keys_.size() == cards_.size();
    std::unordered_map<std::string, size_t> previous;
    std::vector<GameCard> old_cards;
    std::string old_focus;
    if (same_list) {
        for (size_t i = 0; i < cards_.size(); ++i) previous.emplace(shelf_card_keys_[i], i);
        if (selected_ < cards_.size()) old_focus = shelf_card_keys_[selected_];
        old_cards = std::move(cards_);
    }
    cards_.clear();
    static const std::vector<Game> empty;
    const auto& source = screen_ == ScreenMode::library ? librarySource() :
                         screen_ == ScreenMode::plugin_page ? plugin_page_games_ :
                         screen_ == ScreenMode::downloads ? downloads_games_ :
                         screen_ == ScreenMode::search ? search_results_ :
                         screen_ == ScreenMode::store ? store_results_ :
                         screen_ == ScreenMode::details ? detail_games_ :
                         screen_ == ScreenMode::crop ? crop_games_ :
                         screen_ == ScreenMode::platform_games ? nested_games_ :
                         (screen_ == ScreenMode::settings || screen_ == ScreenMode::artwork) ? empty :
                         categories_[category_].games;
    std::vector<Game> arranged;
    if (libraryControls()) {
        library_controls_.options = libraryOptions();
        // Release dates come from the game index (src/app/game_index.cpp).
        std::unordered_map<std::string, int> released;
        if (library_controls_.options.order == library_view::Order::release && game_index_)
            released = game_index_->read([](const game_index::Index& index) { return index.releases(); });
        // A board of logos keeps its own order (most games first, or the player's).
        arranged = studioBoard() ? source : library_view::arrange(source, library_controls_.options, library_state_.first_seen, private_shelf_, &released);
        // The library's search field keeps the games whose title contains the typed text, in order,
        // and the games of a typed genre or tag ("rpg", "przygodowe", "roguelike") or studio ("capcom").
        if (screen_ == ScreenMode::library && library_controls_.search) {
            const auto& typed = library_controls_.query;
            const auto first = typed.find_first_not_of(L' ');
            const auto trimmed = first == std::wstring::npos ? std::wstring{} : typed.substr(first, typed.find_last_not_of(L' ') - first + 1);
            const auto query = foldGameTitle(trimmed);
            std::unordered_set<std::string> labelled;
            if (!query.empty() && game_index_) {
                for (auto& key : game_index_->read([&](const game_index::Index& index) { return index.withLabel(trimmed); })) labelled.insert(std::move(key));
                labelled.merge(studioSearch(trimmed)); // Its developer or publisher.
            }
            if (!query.empty())
                std::erase_if(arranged, [&](const Game& game) { return !titleMatch(foldGameTitle(game.name), query) && !labelled.contains(gameKey(game)); });
        }
    }
    const auto& visible = libraryControls() ? arranged : source;
    if (gridScreen(screen_)) {
        const auto view = renderer_.viewport();
        grid_scroll_ = std::clamp(grid_scroll_, 0.0f, library_layout::maxScroll(libraryView(), visible.size(), view.width, view.height, libraryPresentation()));
    }
    library_controls_.action = -1;
    if (library_view::metadata(libraryPresentation())) preview_.settle();
    cards_.reserve(visible.size() + 1);
    std::vector<std::string> keys;
    std::vector<size_t> from;
    const auto add = [&](Game game) {
        auto key = shelf_id.empty() ? std::string{} : library_view::selectionKey(game) + '\n' + gameKey(game);
        const auto old = same_list ? previous.find(key) : previous.end();
        if (old != previous.end()) {
            cards_.push_back(old_cards[old->second]); // Its animation state, with the fresh game data.
            cards_.back().game = std::move(game);
            from.push_back(old->second);
            previous.erase(old);
        } else { cards_.push_back(GameCard{std::move(game)}); from.push_back(SIZE_MAX); }
        if (!shelf_id.empty()) keys.push_back(std::move(key));
    };
    if (shelfAllTile()) {
        Game all;
        all.kind = GameKind::category; // app_id = category index, as the Flix "Wszystkie" tiles.
        all.name = tr(L"Wszystkie");
        all.app_id = static_cast<uint32_t>(category_);
        all.source_id = categories_[category_].id;
        add(std::move(all));
    }
    // `arranged` is already this view's own copy: its games move into the cards.
    const bool psfive_all = psfiveAllTile();
    if (libraryControls()) for (auto& game : arranged) add(std::move(game));
    else {
        size_t shown = 0;
        for (const auto& game : source) {
            if (psfive_all && shown++ >= psfive_layout::row_games) break; // The rest: in "Wszystkie".
            add(game);
        }
    }
    if (psfive_all) {
        // PSfive ends a game category's row as Recent ends with "Wszystkie gry": its whole library.
        Game all;
        all.kind = GameKind::category; // app_id = category index, as the Flix "Wszystkie" tiles.
        all.name = tr(L"Wszystkie");
        all.app_id = static_cast<uint32_t>(category_);
        all.source_id = categories_[category_].id;
        add(std::move(all));
    }
    selected_ = std::min(screen_ == ScreenMode::platform_games ? size_t{1} :
                         screen_ != ScreenMode::shelves ? size_t{0} :
                         categories_[category_].selected,
                          cards_.empty() ? size_t{0} : cards_.size() - 1);
    if (same_list && screen_ != ScreenMode::shelves && !old_focus.empty())
        if (const auto kept = std::find(keys.begin(), keys.end(), old_focus); kept != keys.end()) selected_ = static_cast<size_t>(kept - keys.begin());
    if (same_list) renderer_.remapCovers(from, preserve_background);
    else renderer_.resetCovers(cards_.size(), preserve_background);
    shelf_cards_category_ = shelf_id;
    shelf_cards_presentation_ = presentation;
    shelf_card_keys_ = std::move(keys);
    shelf_slots_ = renderer_.slotGeneration();
    for (size_t i = 0; i < cards_.size(); ++i) {
        if (screen_ != ScreenMode::shelves && i > 28) break;
        if (from[i] != SIZE_MAX) { // Kept card: its slots came along; only the focus may need its art.
            if (i != selected_) continue;
            if (const auto* found = art_.find(gameKey(cards_[i].game))) {
                if (found->logo && !libraryListPlus()) renderer_.setLogo(i, found->logo);
                if (found->hero) renderer_.setHero(i, found->hero, found->hero_tone, true, found->hero_focus);
            }
            continue;
        }
        const auto* found = art_.find(gameKey(cards_[i].game));
        if (!found) continue;
        const auto& asset = *found;
        if (asset.cover) {
            cards_[i].cover_opacity = 1;
            if (cards_[i].game.kind == GameKind::platform || cards_[i].game.kind == GameKind::pc_platform) renderer_.setPlatformCover(i, asset.cover);
            else renderer_.setCover(i, asset.cover, asset.focus);
        }
        if (i == selected_ && asset.logo && !libraryListPlus()) renderer_.setLogo(i, asset.logo);
        if (i == selected_ && asset.hero)
            renderer_.setHero(i, asset.hero, asset.hero_tone, false, asset.hero_focus);
    }
    if (!cards_.empty() && cards_[0].game.kind == GameKind::category) {
        bindShelfAllTile(0);
        if (selected_ == 0) flixFocusArt(); // Its category's background behind the shelf.
    }
    if (psfiveActive()) { // Category tiles (a row's "Wszystkie"): 2x2 previews; focused, a random game's background.
        for (size_t i = 0; i < cards_.size(); ++i) if (cards_[i].game.kind == GameKind::category) xmbBindThumbnails(i);
        if (selected_ < cards_.size() && cards_[selected_].game.kind == GameKind::category) flixFocusArt();
    }
    if (!same_list) arrived_ = 0;
    // The same focus keeps its settled background and title (and on PSfive its focused button).
    if (!same_list || selected_ >= shelf_card_keys_.size() || old_focus != shelf_card_keys_[selected_]) { detail_ = 0; psfive_action_ = psfive_top_ = psfive_details_ = -1; }
    requestVisibleArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::shelfPosters() const {
    if (desktop_mode_ || !settings_.shelves_posters || settings_.home_layout != AppSettings::HomeLayout::shelves ||
        screen_ != ScreenMode::shelves || category_ >= categories_.size()) return false;
    const auto& games = categories_[category_].games;
    return std::any_of(games.begin(), games.end(), [](const Game& game) { return isGame(game); });
}

// The tiles that open the launcher's own screens, for the first row: Wszystkie gry (with a
// library); with Kafle Wszystkie, Glance… (AppSettings::home_tiles) also Glance, Tarot and
// Towarzyszka while they are on, their floating shortcuts giving way (homeWidgets); and Dodaj
// grę's "+" while add_game_tile is on. buildShelves puts them first or last.
std::vector<Game> Launcher::homeSpecialTiles() const {
    std::vector<Game> tiles;
    const bool snitch = settings_.home_layout == AppSettings::HomeLayout::snitch;
    const auto tile = [&](GameKind kind, std::wstring name) { Game game; game.kind = kind; game.name = std::move(name); tiles.push_back(std::move(game)); };
    if (!public_library_.empty()) tile(GameKind::all, snitch ? tr(L"Wszystkie") : tr(L"Wszystkie gry"));
    if (settings_.home_tiles != AppSettings::HomeTiles::layout && !desktop_mode_) {
        if (settings_.glance) tile(GameKind::glance, L"Glance");
        if (settings_.tarot) tile(GameKind::tarot, L"Tarot");
        if (settings_.companion) tile(GameKind::companion, companion::name(companionLook()));
    }
    if (settings_.add_game_tile && !desktop_mode_) tile(GameKind::add, tr(L"Dodaj grę"));
    return tiles;
}

bool Launcher::shelfAllTile() const {
    if (desktop_mode_ || !settings_.shelves_category_all || settings_.home_layout != AppSettings::HomeLayout::shelves ||
        screen_ != ScreenMode::shelves || category_ >= categories_.size()) return false;
    // Not Recent (it ends with "Wszystkie gry"), stores or consoles, as on Flix.
    const auto& category = categories_[category_];
    if (studioCategory(category.id)) return !category.games.empty(); // Wydawcy, Developerzy: the board of all their logos.
    return category.id != "recent" && category.id != "pc-platforms" && category.id != "emulation" &&
           std::any_of(category.games.begin(), category.games.end(), [](const Game& game) { return isGame(game); });
}

void Launcher::bindShelfAllTile(size_t index, bool replace) {
    if (index >= cards_.size() || cards_[index].game.kind != GameKind::category) return;
    if (!shelfPosters()) { xmbBindThumbnails(index); return; }
    if (!replace && renderer_.hasPoster(index)) return;
    const auto* backdrop = flixBackdrop(cards_[index].game.app_id);
    if (!backdrop) return;
    if (const auto* found = art_.find(gameKey(*backdrop)); found && found->hero) renderer_.setPoster(index, found->hero);
    else requestArt(*backdrop, CoverMessage::Kind::hero);
}

void Launcher::showCategory(size_t index) {
    if (index >= categories_.size()) return;
    rememberPlace();
    screen_ = ScreenMode::shelves;
    category_rail_focus_ = false;
    if (index != category_) entrance_lead_ = false; // The start-up pause belongs to the first home only.
    category_ = index;
    rebuildCards();
}
