#include "launcher.hpp"
#include "../store/game_details.hpp"
#include "../library/steam.hpp"
#include "../library/steam_stats.hpp"
#include "../platform/text_encoding.hpp"
#include <algorithm>

namespace {
uint64_t timestamp() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
}

void Launcher::initializeStore() {
    add_requests_.load(userConfigDirectory() / L"store" / L"pending-adds.txt", timestamp());
    const auto queue = store_jobs_;
    const auto alive = alive_;
    const auto hwnd = hwnd_;
    work_.start([queue, alive, hwnd, aurelia = aurelia_active_, invoke = aurelia_transport_] {
        const auto steam = findSteamRoot();
        while (alive->load()) {
            StoreRequest job;
            {
                std::unique_lock lock(queue->mutex);
                queue->wake.wait(lock, [&] { return !alive->load() || queue->pending.has_value(); });
                if (!alive->load()) return;
                job = std::move(*queue->pending);
                queue->pending.reset();
            }
            if (!alive->load() || queue->generation.load() != job.generation) continue;
            auto result = std::make_unique<StoreMessage>();
            result->generation = job.generation;
            result->account = job.account;
            result->details = job.app_id != 0;
            if (job.app_id) {
                result->installation_checked = false; // The shared Steam observer owns live install facts.
                if (job.metadata && alive->load() && queue->generation.load() == job.generation) {
                    if (aurelia) {
                        aurelia::Request request{aurelia::Command::info, job.app_id}; request.language = i18n::steamLanguage();
                        const auto reply = invoke ? invoke(request, [&] { return !alive->load() || queue->generation.load() != job.generation; }, {}) : aurelia::Reply{};
                        result->info = reply.ok ? aurelia::details(reply.data) : GameDetails{}; result->info.error = reply.error;
                        if (reply.ok && invoke && alive->load()) {
                            request.command = aurelia::Command::achievements;
                            const auto response = invoke(request, [&] { return !alive->load() || queue->generation.load() != job.generation; }, {});
                            if (response.ok) { const auto list = aurelia::achievements(response.data); result->info.achievements_total = list.total; result->info.achievements_unlocked = list.unlocked; }
                        }
                    } else {
                        result->info = fetchGameDetails(job.app_id, userConfigDirectory() / L"metadata");
                        const auto stats = readSteamStats(steam, job.account, job.app_id);
                        result->info.playtime_minutes = stats.playtime_minutes;
                        result->info.achievements_unlocked = stats.achievements_unlocked;
                        result->info.achievements_total = stats.achievements_total;
                    }
                    result->info_ready = true;
                }
            }
            if (!alive->load() || queue->generation.load() != job.generation) continue;
            if (PostMessageW(hwnd, store_message, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
        }
    });
}

void Launcher::openStore() {
    // Reached from the Stores tabs (or opened from a store plugin's page there), the catalog screen keeps them.
    if (screen_ != ScreenMode::plugin_page && screen_ != ScreenMode::store) stores_open_ = stores_opening_;
    stores_opening_ = false;
    if (stores_open_ && storeTabOf(catalog_plugin_) >= 0) store_tab_ = storeTabOf(catalog_plugin_);
    desktopLeaveLibrary(); rememberPlace();
    if (!storeEnabled()) return;
    screen_ = ScreenMode::store;
    grid_back_focus_ = false;
    store_jobs_->generation.fetch_add(1);
    store_loading_ = false;
    search_query_.clear(); search_after_ = 0;
    search_dirty_ = false;
    store_results_.clear();
    // The search field placeholder already explains what to type.
    store_status_.clear();
    grid_scroll_ = 0;
    closeQuick();
    rebuildCards(false);
}

void Launcher::searchStore() {
    if (screen_ != ScreenMode::store || !storeEnabled()) return;
    search_dirty_ = false;
    store_results_.clear();
    grid_scroll_ = 0;
    const auto generation = store_jobs_->generation.fetch_add(1) + 1;
    store_loading_ = false;
    if (search_query_.empty()) store_status_.clear();
    else {
        store_loading_ = true;
        store_status_ = tr(L"Szukam w sklepie…");
        plugins::Job job; job.kind = plugins::Job::Kind::search; job.plugin = catalog_plugin_; job.generation = generation;
        job.value = plugins::wire::object(); job.value.properties["query"] = plugins::wire::string(text_encoding::utf8(search_query_));
        submitPlugin(std::move(job));
    }
    rebuildCards(false);
}

void Launcher::retryStore() {
    if (glanceOpen()) { if (!friends_open_) requestGlance(true); return; }
    if (refreshPluginPage(false)) return;
    if (cropOpen()) return;
    if (devicesOpen()) { if (device_service_) device_service_->submit({}); return; }
    if (!artworkOpen() && !settingsOpen() && !cards_.empty()) retryGameArtwork(cards_[selected_].game, false);
    if (artworkOpen() && !artwork_state_.loading && !artwork_state_.applying) loadArtworkPage();
    else if (settingsOpen() && settings_state_.category == static_cast<int>(settings_ui::Category::library) &&
             settings_state_.platform() == GameSource::drmfree) startDrmScan(true); // F5: "Skanuj teraz".
    else if (screen_ == ScreenMode::library && pc_source_ == GameSource::drmfree) startDrmScan(true);
    else if (settingsOpen() && settings_state_.category == static_cast<int>(settings_ui::Category::library)) {
        refreshPcLibraries();
        if ((!settings_state_.platform() || settings_state_.platform() == GameSource::gog) && !gog_state_.account.empty()) gogCommand(gog::Command::sync);
        if ((!settings_state_.platform() || settings_state_.platform() == GameSource::epic) && !epic_state_.account.empty()) epicCommand(epic::Command::sync);
        if ((!settings_state_.platform() || settings_state_.platform() == GameSource::amazon) && amazonConnected()) amazonCommand(amazon::Command::sync);
        if ((!settings_state_.platform() || settings_state_.platform() == GameSource::xbox) && xboxConnected()) xboxCommand(xbox_account::Command::sync);
    }
    else if (screen_ == ScreenMode::library && pc_source_) {
        refreshPcLibraries(); if (pc_source_ == GameSource::gog && !gog_state_.account.empty()) gogCommand(gog::Command::sync);
        if (pc_source_ == GameSource::epic && !epic_state_.account.empty()) epicCommand(epic::Command::sync);
        if (pc_source_ == GameSource::amazon && amazonConnected()) amazonCommand(amazon::Command::sync);
        if (pc_source_ == GameSource::xbox && xboxConnected()) xboxCommand(xbox_account::Command::sync);
    }
    else if (screen_ == ScreenMode::store && !store_loading_) searchStore();
    else if (screen_ == ScreenMode::details && !details_loading_) {
        if (!detail_games_.empty()) requestGameComparisons(detail_games_[0], true);
        details_requested_ = false; requestDetails();
    }
}

Game Launcher::installedCatalogGame(const Game& game) const {
    if (game.kind != GameKind::catalog) return game;
    for (const auto* games : {&installed_library_, &private_category_.games})
        for (const auto& installed : *games)
            if (installed.kind == GameKind::steam && installed.app_id == game.app_id && (installed.installed || installed.installation.active())) {
                auto shown = installed;
                if (shown.visibility == GameVisibility::public_game && locallyHidden(shown)) shown.visibility = GameVisibility::hidden;
                return shown;
            }
    if (add_requests_.ready(game.app_id, timestamp())) {
        auto installable = game;
        installable.kind = GameKind::steam;
        installable.installed = false;
        installable.installation = {};
        return installable;
    }
    return game;
}

// actionLabel/actionEnabled/gameFolder run on every painted frame and hover: only Store
// catalog entries can resolve to another game, so the rest are used in place, not copied.
std::wstring Launcher::actionLabel(const Game& game) const {
    if (launcherTile(game)) return tr(L"Otwórz");
    const auto resolved = game.kind == GameKind::catalog ? std::optional<Game>(installedCatalogGame(game)) : std::nullopt;
    const Game& effective = resolved ? *resolved : game;
    const auto session = sessionState(effective);
    if (session == SessionState::frozen) return tr(L"Wznów");
    if (session == SessionState::running) return tr(L"Wróć");
    if (effective.installation.active() || effective.preloaded) return primaryActionLabel(effective);
    if (effective.kind == GameKind::steam && !effective.installed && add_requests_.ready(game.app_id, timestamp()))
        return tr(L"Zainstaluj");
    if (effective.kind != GameKind::catalog) return effective.installed ? tr(L"Graj") : primaryActionLabel(effective);
    if (add_requests_.pending(game.app_id, timestamp())) return tr(L"Przekazano do pluginu");
    if (game.catalog_available)
        for (const auto& action : plugins_.contributions("catalog")) if (action.plugin == game.catalog_plugin) return action.item.title.localized();
    return tr(L"Niedostępne w sklepie");
}

bool Launcher::actionEnabled(const Game& game) const {
    if (!game.version_group.empty() && versions_.find(game)) return true;
    const auto resolved = game.kind == GameKind::catalog ? std::optional<Game>(installedCatalogGame(game)) : std::nullopt;
    const Game& effective = resolved ? *resolved : game;
    // GOG / Epic account titles are installed, and Launchme-installed Epic titles started, by the
    // launcher itself (launch(): openGogInstall, openEpicInstall, startEpicGame), not through a
    // URI action — those have none, yet the action works.
    if (effective.kind != GameKind::catalog && !effective.installed && (!effective.account_id.empty() || effective.installation.active()) &&
        ((effective.source == GameSource::gog && !gog_state_.account.empty()) || (effective.source == GameSource::epic && !epic_state_.account.empty()) ||
         (effective.source == GameSource::amazon && amazonConnected()) || (xboxOwnInstall(effective) && xboxConnected())))
        return true;
    if (effective.source == GameSource::epic && effective.epic_managed) return true;
    if (fileNotChosen(effective)) return true; // "Wskaż plik": its editor, at the file (launchGame).
    const auto* owner = plugins_.find(game.catalog_plugin);
    return effective.kind != GameKind::catalog ? static_cast<bool>(primaryGameAction(effective)) :
           owner && owner->enabled && owner->error.empty() && !plugins_busy_ && !plugin_command_pending_ &&
           game.catalog_available && !add_requests_.pending(game.app_id, timestamp());
}

std::filesystem::path Launcher::gameFolder(const Game& game) const {
    const auto resolved = game.kind == GameKind::catalog ? std::optional<Game>(installedCatalogGame(game)) : std::nullopt;
    const Game& effective = resolved ? *resolved : game;
    if (!effective.installed || effective.kind == GameKind::catalog) return {};
    if (effective.install_directory.is_absolute()) return effective.install_directory;
    return effective.executable.is_absolute() ? effective.executable.parent_path() : std::filesystem::path{};
}

// Details actions: -1 back, 0 primary, 1 options (artwork, folder, hide/show, Epic/GOG exclusion).
int Launcher::lastDetailAction() const {
    return std::max({1, identifyDetailAction(), collectionsDetailAction(), versionsDetailAction(), closeDetailAction(), cloudDetailAction()});
}
// ✕ beside Wróć while the game runs (or is suspended): Zamknij grę, through closeGame().
int Launcher::closeDetailAction() const {
    if (screen_ != ScreenMode::details || cards_.empty() || !isGame(cards_[0].game)) return -2;
    const auto& game = cards_[0].game;
    const auto state = sessionState(game);
    const bool aurelia = aurelia_active_ && aurelia::validGame(game) && aurelia_playing_.contains(game.app_id);
    if (state != SessionState::running && state != SessionState::frozen && !aurelia) return -2;
    return std::max({1, identifyDetailAction(), collectionsDetailAction(), versionsDetailAction()}) + 1;
}
// The cloud saves of a game Aurelia manages: an icon after "…" that opens its Zapisy w chmurze.
int Launcher::cloudDetailAction() const {
    if (screen_ != ScreenMode::details || cards_.empty() || !aureliaCloudFor(cards_[0].game)) return -2;
    return std::max({1, identifyDetailAction(), collectionsDetailAction(), versionsDetailAction(), closeDetailAction()}) + 1;
}

details_layout::Actions Launcher::detailsActions() const {
    details_layout::Actions actions;
    actions.close = closeDetailAction();
    actions.cloud = cloudDetailAction();
    actions.identify = identifyDetailAction();
    actions.favorite = favoriteDetailAction();
    actions.collections = collectionsDetailAction();
    actions.versions = versionsDetailAction();
    actions.achievements = achievementsDetailAction();
    actions.rating = ratingDetailAction();
    actions.plugin = pluginDetailAction();
    if (actions.rating >= 0 && screen_ == ScreenMode::details) actions.rating_rect = renderer_.ratingLink(); // As last drawn.
    return actions;
}

void Launcher::catalogAction(const Game& original) {
    if (pending_launch_ || !actionEnabled(original)) return;
    auto game = installedCatalogGame(original);
    if (game.kind != GameKind::catalog) {
        audio_.launch();
        if (game.installed) audio_.stopMusic();
        pending_game_ = game;
        pending_launch_ = true;
        launch_age_ = 0;
        dismissMenu();
        return;
    }
    for (const auto& action : plugins_.contributions("catalog")) if (action.plugin == game.catalog_plugin) {
        if (plugin_command_pending_ || !add_requests_.begin(game.app_id)) return;
        pluginAction(action, game); return;
    }
}

void Launcher::openDetails(const Game& game) {
    if (!isGame(game) || pending_launch_) return;
    if (launcherTile(game)) { // A launcher's details are its card in the Stores: open, uninstall, the download page.
        if (!storeHubTabs()) return;
        const auto id = game.source_id.substr(9); // The game may be a card's, and the Stores replace the cards.
        store_tab_ = 0; openStores(); openLauncherCard(id);
        return;
    }
    desktopLeaveLibrary(); rememberPlace();
    detail_return_ = screen_;
    detail_return_selected_ = selected_;
    detail_return_scroll_ = grid_scroll_;
    detail_catalog_ = game.kind == GameKind::catalog;
    detail_catalog_available_ = game.catalog_available;
    detail_action_ = detail_column_ = 0;
    detail_version_selected_ = false;
    // Copy before rebuildCards clears the card holding the argument.
    detail_games_ = {installedCatalogGame(game)};
    details_ = {};
    details_requested_ = false;
    details_loading_ = false;
    dismissMenu();
    screen_ = ScreenMode::details;
    rebuildCards(true);
    requestDetails();
    audio_.drawer();
}

void Launcher::requestDetails() {
    if (screen_ != ScreenMode::details || detail_games_.empty() || details_loading_) return;
    const auto& game = detail_games_.front();
    next_install_poll_ = GetTickCount64() + 4000;
    if (runtime_started_ && details_requested_) return;
    if (!steamArtwork(game)) {
        if (const auto* local = localDetails(game)) details_ = *local;
        else details_.error = tr(L"Brak lokalnego opisu.");
        completeDetails(details_, game); // What the game index knows fills the gaps.
        if (const auto* known = pluginAchievements(game)) { details_.achievements_unlocked = known->list.unlocked; details_.achievements_total = known->list.total; }
        requestPluginAchievements(game); // An achievements plugin's list for games outside Steam.
        details_requested_ = true;
        return;
    }
    details_loading_ = true;
    const auto generation = store_jobs_->generation.fetch_add(1) + 1;
    std::lock_guard lock(store_jobs_->mutex);
    store_jobs_->pending = StoreRequest{generation, account_, game.app_id, !details_requested_, {}, {}};
    details_requested_ = true;
    store_jobs_->wake.notify_one();
}

void Launcher::suspendDetails() {
    store_jobs_->generation.fetch_add(1);
    if (details_loading_) details_requested_ = false;
    details_loading_ = false;
}

void Launcher::onStore(std::unique_ptr<StoreMessage> message) {
    if (message->generation != store_jobs_->generation.load() || message->account != account_) return;
    if (message->details) {
        if (screen_ != ScreenMode::details || detail_games_.empty()) return;
        details_loading_ = false;
        if (message->info_ready) {
            details_ = std::move(message->info);
            completeDetails(details_, detail_games_[0]); // Offline or a gap in the store page: the index's facts.
            gameIndexTouch({gameKey(detail_games_[0])}); // Its store page and play time are on disk now.
        }
        if (!message->installation_checked) return;
        if (message->installed) {
            const auto& game = *message->installed;
            auto& library = game.visibility == GameVisibility::public_game ? installed_library_ : private_category_.games;
            auto& opposite = game.visibility == GameVisibility::public_game ? private_category_.games : installed_library_;
            const auto removed = std::erase_if(opposite, [&](const Game& item) { return gameKey(item) == gameKey(game); });
            const auto upgraded = std::erase_if(uninstalled_library_, [&](const Game& item) { return gameKey(item) == gameKey(game); });
            if (game.visibility == GameVisibility::public_game) excluded_ids_.erase(game.app_id);
            else excluded_ids_.insert(game.app_id);
            auto found = std::find_if(library.begin(), library.end(), [&](const Game& item) { return gameKey(item) == gameKey(game); });
            const bool added = found == library.end();
            if (added) library.push_back(game); else *found = game;
            detail_games_[0] = game;
            if (detail_games_[0].visibility == GameVisibility::public_game && locallyHidden(game)) detail_games_[0].visibility = GameVisibility::hidden;
            cards_[0].game = detail_games_[0];
            detail_action_ = std::min(detail_action_, lastDetailAction());
            if (added || removed || upgraded) buildShelves();
        } else if (detail_games_[0].kind == GameKind::steam && detail_games_[0].installed) {
            auto& game = detail_games_[0];
            game.installed = false;
            if (detail_catalog_) { game.kind = GameKind::catalog; game.catalog_available = detail_catalog_available_; }
            std::erase_if(installed_library_, [&](const Game& item) { return item.app_id == game.app_id; });
            std::erase_if(private_category_.games, [&](const Game& item) { return item.kind == GameKind::steam && item.app_id == game.app_id; });
            cards_[0].game = game;
            buildShelves();
        }
    } else {
        if (screen_ != ScreenMode::store) return;
        store_loading_ = false;
        store_results_ = std::move(message->search.games);
        for (auto& game : store_results_) {
            const auto local = installedCatalogGame(game);
            game.installation = local.installation; game.installed = local.installed;
        }
        store_status_ = std::move(message->search.error);
        if (store_status_.empty()) {
            const auto count = store_results_.size();
            // Polish plural forms; English has "result" only for 1.
            const auto suffix = count == 1 ? tr(L" wynik") : count % 10 >= 2 && count % 10 <= 4 && count / 10 != 1 ? tr(L" wyniki") : tr(L" wyników");
            store_status_ = count ? std::to_wstring(count) + suffix :
                                  std::wstring(tr(L"Brak wyników. Spróbuj innego tytułu."));
        }
        rebuildCards(false);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
