#include "launcher.hpp"
#include "../ui/grid_layout.hpp"
#include "../store/epic_legendary.hpp"
#include "../store/amazon.hpp"
#include "../store/xbox_account.hpp"
#include "../library/ubisoft_catalog.hpp"
#include "../platform/text_encoding.hpp"
#ifndef _WIN32
#include "../library/heroic.hpp"
#include "../library/lutris.hpp"
#include "../library/xdg_paths.hpp"
#endif
#include <algorithm>
#include <map>

namespace {
bool sameAction(const GameAction& a, const GameAction& b) {
    return a.target == b.target && a.arguments == b.arguments && a.working_directory == b.working_directory && a.kind == b.kind;
}
bool sameStructure(const Game& a, const Game& b) {
    return gameKey(a) == gameKey(b) && a.name == b.name && a.installed == b.installed &&
        a.installation.active() == b.installation.active() && a.visibility == b.visibility &&
        a.last_played == b.last_played && a.last_updated == b.last_updated && a.added_at == b.added_at && a.account_id == b.account_id &&
        a.install_directory == b.install_directory && a.executable == b.executable &&
        sameAction(a.play_action, b.play_action) && sameAction(a.install_action, b.install_action);
}
PcLibraryMessage readPcLibrary(const PcLibraryRequest& request, const std::function<bool()>& cancel) {
    PcLibraryMessage message;
    const auto root = userConfigDirectory();
    message.ea_import = web_import::load(root, GameSource::ea);
    message.battlenet_import = web_import::load(root, GameSource::battlenet);
    std::vector<Game> imported;
    for (const auto* snapshot : {&message.ea_import, &message.battlenet_import})
        if (*snapshot) imported.insert(imported.end(), (*snapshot)->games.begin(), (*snapshot)->games.end());
    const auto scan = [&] { return scanPcLibraries(root / L"libraries", request.gog_owned, request.gog_account,
        request.previous_battlenet, request.epic_owned, request.epic_account, epic::installedGames(root), imported, request.multimc,
        amazon::readProfile(root).games, xbox_account::readProfile(root).games); };
    if (cancel()) return message;
    message.library = scan();
    // Owned Ubisoft products are listed by title only; new titles come from the public catalogue.
    if (!cancel() && !message.library.ubisoft_unnamed.empty() && ubisoft::refreshNames(root / L"ubisoft", message.library.ubisoft_unnamed))
        message.library = scan();
    return message;
}
}

void Launcher::openPcLibrary(GameSource source) {
    rememberPlace(); library_controls_ = {};
    source = platformSource(source); // Gry dodane ręcznie: DRM-free's library.
    pc_source_ = source; library_console_.clear();
    library_category_.clear(); category_library_.clear();
    pc_library_.clear();
    for (const auto& game : public_library_)
        if (platformSource(game.source) == source && game.kind != GameKind::nonsteam) pc_library_.push_back(game);
    screen_ = ScreenMode::library;
    grid_scroll_ = 0;
    closeQuick();
    dismissMenu();
    rebuildCards(true);
    audio_.drawer();
}

void Launcher::refreshPcLibraries() {
    if (!runtime_started_) return;
    if (pc_scanning_) {
        // Coalesce requests, but invalidate the scan that predates them. Its
        // completion still releases the slot and starts one fresh snapshot.
        if (!pc_refresh_pending_) { pc_refresh_pending_ = true; ++*pc_token_; }
        return;
    }
    pc_scanning_ = true;
    pc_refresh_pending_ = false;
    const auto generation = pc_token_->fetch_add(1) + 1;
    pc_scan_generation_ = generation;
    const auto token = pc_token_;
    const auto alive = alive_;
    const auto window = hwnd_;
    PcLibraryRequest request{gog_state_.games, epic_state_.games, {}, gog_state_.account, epic_state_.account, {}};
    if (!settings_.multimc_path.empty()) request.multimc.push_back(settings_.multimc_path);
    for (const auto* list : {&store_library_, &store_private_library_}) for (const auto& game : *list)
        if (game.source == GameSource::battlenet && !game.manifest_path.empty()) request.previous_battlenet.push_back(game);
    const auto failure = [generation, alive, window](std::exception_ptr) {
        if (!alive->load()) return;
        auto message = std::make_unique<PcLibraryMessage>();
        message->generation = generation; message->failed = true;
        if (PostMessageW(window, pc_library_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    if (!work_.start([token, generation, alive, window, request = std::move(request), reader = read_pc_library_ ? read_pc_library_ : readPcLibrary] {
        auto message = std::make_unique<PcLibraryMessage>(reader(request, [&] { return !alive->load() || token->load() != generation; }));
        message->generation = generation;
        // Even a cancelled/stale scan must acknowledge completion to the UI.
        if (alive->load() && PostMessageW(window, pc_library_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    }, failure)) {
        pc_scanning_ = false; pc_initial_ready_ = true;
        next_pc_poll_ = GetTickCount64() + 30000;
    }
}

// Only an install in progress (Battle.net's live progress) or a failed scan sets a deadline;
// installs, uninstalls and moves done in a store client arrive through startPcWatcher().
void Launcher::pollPcInstallations() {
    if (!runtime_started_ || pc_scanning_ || !pc_initial_ready_ || settings_.enabled_pc_sources.empty()) return;
    if (GetTickCount64() >= next_pc_poll_) refreshPcLibraries();
}

// The PC stores' installation data, followed instead of rescanned on a timer: the uninstall
// registry (all stores' installers), GOG/Ubisoft/EA/Gaming Services keys, the Epic and
// Battle.net manifests, Launchme's own GOG/Legendary installs and store catalogs. A burst of
// changes (an installer writing dozens of keys) settles first and becomes one rescan.
void Launcher::startPcWatcher() {
    work_.start([window = hwnd_, alive = alive_, wake = pc_watch_wake_, config = userConfigDirectory()] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        const auto environment = [](const wchar_t* name) {
            wchar_t buffer[MAX_PATH * 2]{};
            const DWORD length = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
            return length && length < std::size(buffer) ? std::filesystem::path(buffer) : std::filesystem::path{};
        };
        ChangeWatch watch;
        watch.wake(wake->event);
        const auto build = [&] {
            watch.clear();
#ifndef _WIN32
            // Linux: the launchers' own files (Heroic and its legendary/gogdl/nile, Lutris' database,
            // Prism Launcher's instances) and Launchme's installs; Steam is watched by its library worker.
            (void)environment;
            for (const auto& folder : heroic::configFolders()) {
                watch.directory(folder / L"store_cache");
                watch.directory(folder / L"GamesConfig");
                watch.directory(folder / L"legendaryConfig" / L"legendary");
                watch.directory(folder / L"gog_store");
            }
            for (const auto& database : lutris::databases()) watch.directory(database.parent_path());
            for (const auto* data : {L"PrismLauncher", L"PolyMC", L"multimc"}) watch.directory(xdg_paths::dataHome() / data / L"instances");
            watch.directory(xdg_paths::configHome() / L"legendary");
#else
            for (const REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
                watch.registry(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", view);
                watch.registry(HKEY_LOCAL_MACHINE, L"SOFTWARE\\GOG.com\\Games", view);
                watch.registry(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Ubisoft\\Launcher\\Installs", view);
                watch.registry(HKEY_LOCAL_MACHINE, L"SOFTWARE\\EA Games", view);
            }
            watch.registry(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
            watch.registry(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\GamingServices\\GameConfig", KEY_WOW64_64KEY);
            if (const auto data = environment(L"PROGRAMDATA"); !data.empty()) {
                watch.directory(data / L"Epic" / L"EpicGamesLauncher" / L"Data" / L"Manifests");
                watch.directory(data / L"Epic" / L"UnrealEngineLauncher");
                watch.directory(data / L"Battle.net" / L"Agent");
            }
            if (const auto local = environment(L"LOCALAPPDATA"); !local.empty())
                watch.directory(local / L"Ubisoft Game Launcher" / L"cache" / L"ownership");
            // Minecraft instances added, renamed or removed in Prism Launcher / PolyMC.
            if (const auto roaming = environment(L"APPDATA"); !roaming.empty()) {
                watch.directory(roaming / L"PrismLauncher" / L"instances");
                watch.directory(roaming / L"PolyMC" / L"instances");
            }
#endif
            watch.directory(config / L"libraries");
            watch.directory(config / L"installed" / L"gog");
            watch.directory(config / L"installed" / L"epic");
            watch.directory(config / L"epic" / L"legendary");
            watch.directory(amazon::nileDirectory(config)); // Nile's library and installs.
            watch.directory(xbox_account::profileDirectory(config)); // The Xbox helper's library and installs.
        };
        build();
        size_t watched = watch.size();
        while (alive->load()) {
            // A folder that does not exist yet (a store installed later) is looked for again
            // once an hour; everything else waits for a notification.
            const auto result = watch.wait(3600000);
            if (!alive->load()) return;
            if (result == ChangeWatch::Result::woken) continue;
            if (result == ChangeWatch::Result::timeout) {
                build();
                if (watch.size() == watched) continue;
                watched = watch.size();
            } else if (!watch.settle(1500, 15000) && !alive->load()) return;
            auto message = std::make_unique<StoreChangeMessage>();
            if (alive->load() && PostMessageW(window, store_change_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
            if (watch.size() != watched) { build(); watched = watch.size(); }
        }
    });
}

void Launcher::onStoreChange(std::unique_ptr<StoreChangeMessage> message) {
    if (!message->pc || !runtime_started_ || closing_) return;
    if (!message->uninstalled.empty()) {
        if (message->failed) showNotification(NotificationKind::error, tr(L"Nie udało się odinstalować gry"), message->uninstalled);
        else showNotification(NotificationKind::launch, tr(L"Gra odinstalowana"), message->uninstalled);
    }
    refreshPcLibraries(); // Coalesced with a scan already running.
}

bool Launcher::canUninstall(const Game& game) const {
    if (!isGame(game) || !game.installed || game.installation.active() || game.kind == GameKind::catalog || game.kind == GameKind::nonsteam) return false;
    if (game.source == GameSource::steam) return game.kind == GameKind::steam && game.app_id;
    if (localFileGame(game.source) || game.source == GameSource::multimc) return false; // Removing an entry from the list is not uninstalling.
    return game.epic_managed || game.source == GameSource::gog || game.source == GameSource::xbox || game.install_directory.is_absolute();
}

void Launcher::uninstallGame(const Game& game) {
    if (!canUninstall(game) || sessionState(game) != SessionState::none) return; // Never a running or suspended game.
    // A game Aurelia manages is removed by it; every other Steam game by the Steam client below.
    if (aureliaManaged(game)) { aureliaUninstall(game); return; }
    dismissMenu();
    const auto ask = [&](GameAction silent) {
        // Legendary and a silent GOG uninstaller delete without asking: the launcher asks first
        // (Steam's and the other stores' dialogs do it themselves).
        uninstall_game_ = game;
        uninstall_silent_ = std::move(silent);
        prompt_kind_ = PromptKind::uninstall;
        exit_prompt_ = 1; // "Otwórz folder gry" is the safe default; Odinstaluj is one step up.
        audio_.context();
        InvalidateRect(hwnd_, nullptr, FALSE);
    };
    if (game.epic_managed || game.source == GameSource::amazon) { ask({}); return; } // Nile deletes it too.
    if (game.source == GameSource::gog)
        if (auto silent = silentUninstallAction(uninstallAction(game, installed_programs_()))) { ask(std::move(silent)); return; }
    if (xbox_account::ownInstall(game)) { ask({}); return; } // Installed by the Xbox helper: Launchme deletes its folder.
    if (game.source == GameSource::xbox) { // Packaged games: Windows' own Apps list removes them.
        if (execute_(hwnd_, GameAction{L"ms-settings:appsfeatures", {}, {}, ActionKind::open_client}) > 32)
            showNotification(NotificationKind::launch, game.name, tr(L"Odinstaluj w Ustawieniach Windows → Aplikacje."));
        return;
    }
    const auto action = game.source == GameSource::steam ? uninstallAction(game, {}) : uninstallAction(game, installed_programs_());
    if (!action) {
        // No uninstaller registered in Windows: the store's client does it.
        const auto client = connectPcClient(game.source, pc_clients_);
        if (client && execute_(hwnd_, client) > 32)
            showNotification(NotificationKind::launch, game.name, std::wstring(tr(L"Odinstaluj w kliencie ")) + sourceName(game.source));
        else showNotification(NotificationKind::error, tr(L"Nie znaleziono deinstalatora"), game.name);
        return;
    }
    const auto result = execute_(hwnd_, action);
    if (result <= 32 && result != launch_error::cancelled)
        showNotification(NotificationKind::error, tr(L"Nie udało się uruchomić deinstalatora"), game.name + L" · " + action.target);
    // Its end shows up by itself: the uninstall registry and manifests are watched (startPcWatcher),
    // and Steam's manifest folder too.
}

void Launcher::uninstallManagedGame(const Game& game, const GameAction& silent) {
    const bool xbox_own = xbox_account::ownInstall(game);
    if (!game.epic_managed && game.source != GameSource::amazon && !xbox_own && !silent) return;
    work_.start([window = hwnd_, alive = alive_, game, silent, xbox_own, run = silent_uninstall_, root = userConfigDirectory()] {
        auto message = std::make_unique<StoreChangeMessage>();
        message->uninstalled = game.name;
        const auto cancel = [&] { return !alive->load(); };
        if (game.epic_managed) message->failed = !epic::uninstallManaged(root, game, cancel);
        else if (game.source == GameSource::amazon) message->failed = !amazon::uninstall(root, game, cancel);
        else if (xbox_own) message->failed = !xbox_account::uninstall(root, game);
        else {
            const auto result = run(silent, window, cancel);
            if (result == UninstallResult::refused) message->uninstalled.clear(); // UAC declined: nothing to report.
            message->failed = result == UninstallResult::failed;
            if (result == UninstallResult::done && validStoreGameId(GameSource::gog, game.source_id)) {
                // A game Launchme's GOG installer put there: its marker would list a leftover folder.
                std::error_code ec;
                std::filesystem::remove(root / L"installed" / L"gog" / (text_encoding::wide(game.source_id) + L".path"), ec);
            }
        }
        if (alive->load() && PostMessageW(window, store_change_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
    showNotification(NotificationKind::launch, game.name, tr(L"Odinstalowywanie…"));
}

void Launcher::onPcLibrary(std::unique_ptr<PcLibraryMessage> message) {
    if (pc_scanning_ && message->generation != pc_scan_generation_) return;
    if (message->generation != pc_token_->load()) {
        if (pc_scanning_ && message->generation == pc_scan_generation_) {
            pc_scanning_ = false;
            if (pc_refresh_pending_) refreshPcLibraries();
        }
        return;
    }
    pc_scanning_ = false;
    if (message->failed) {
        pc_initial_ready_ = true; next_pc_poll_ = GetTickCount64() + 30000; // One retry; changes also trigger one.
        showNotification(NotificationKind::error, tr(L"Nie można odczytać biblioteki PC"), L"");
        return; // A failed scan never replaces the last complete library.
    }
    battlenet_available_ = message->library.battlenet_available;
    ubisoft_account_ = message->library.ubisoft_account;
    if (!ea_import_.busy) ea_import_.snapshot = std::move(message->ea_import);
    if (!battlenet_import_.busy) battlenet_import_.snapshot = std::move(message->battlenet_import);
    bool structure = !pc_initial_ready_;
    pc_initial_ready_ = true;
    const auto selected_key = cards_.empty() ? std::string{} : gameKey(cards_[selected_].game);
    const auto category_id = categories_.empty() ? std::string{} : categories_[category_].id;
    std::map<std::string, Game> previous;
    for (const auto* list : {&store_library_, &store_private_library_}) for (const auto& game : *list) previous.emplace(gameKey(game), game);
    store_library_ = std::move(message->library.visible);
    store_private_library_ = std::move(message->library.private_games);
    pc_clients_ = std::move(message->library.clients);
    applyGogInstallProgress();
    std::map<std::string, const Game*> current;
    bool active = false, changed = structure;
    std::vector<std::string> art_arrived; // Store art the account sync brought for a game shown without it.
    for (const auto* list : {&store_library_, &store_private_library_}) for (const auto& game : *list) {
        const auto key = gameKey(game);
        current.emplace(key, &game);
        active |= game.source == GameSource::battlenet && game.installation.active();
        const auto old = previous.find(key);
        if (old == previous.end() || !sameStructure(old->second, game)) structure = changed = true;
        if (old != previous.end()) {
            if (game.source == GameSource::battlenet && game.visibility == GameVisibility::public_game) {
                if (old->second.installation.active() && old->second.installation.phase != InstallPhase::unknown &&
                    !game.installation.active() && (!old->second.installation.update || game.last_updated > old->second.last_updated) &&
                    game.installed) downloadFinished(game.name, &game);
                else if (!old->second.installation.active() && game.installation.active() && game.installation.phase != InstallPhase::unknown)
                    downloadQueued(game.name);
            }
            changed |= old->second.installation != game.installation || old->second.store_playtime_minutes != game.store_playtime_minutes;
            if (old->second.store_tall_art != game.store_tall_art || old->second.store_wide_art != game.store_wide_art || old->second.store_logo_art != game.store_logo_art) {
                changed = true;
                if ((old->second.store_tall_art.empty() && !game.store_tall_art.empty()) || (old->second.store_wide_art.empty() && !game.store_wide_art.empty()) ||
                    (old->second.store_logo_art.empty() && !game.store_logo_art.empty())) art_arrived.push_back(key);
            }
            if (game.source == GameSource::battlenet && old->second.installation.active() &&
                old->second.installation.phase != InstallPhase::unknown && game.installed && game.visibility == GameVisibility::public_game)
                completed_pc_installs_.insert(key);
        }
    }
    if (previous.size() != current.size()) structure = changed = true;
    // Once per profile: when no store was switched on yet, those whose games are installed here
    // are switched on at once (Ustawienia → Biblioteka → Platformy turns them off), instead of
    // hiding the games until the user finds the switch.
    if (!settings_.pc_sources_detected && !pc_sources_detected_) {
        pc_sources_detected_ = true;
        settings_.pc_sources_detected = true;
        settings_dirty_ = true; settings_save_failed_ = false; settings_edited_ = std::chrono::steady_clock::now();
        if (settings_.enabled_pc_sources.empty())
            for (const auto* list : {&store_library_, &store_private_library_}) for (const auto& game : *list)
                if (game.installed && isGame(game) && settings_.enabled_pc_sources.insert(sourceSlug(game.source)).second)
                    structure = changed = true;
    }
    // Live Battle.net progress is read while an install runs; otherwise no timer at all.
    next_pc_poll_ = active ? GetTickCount64() + 800 : UINT64_MAX;
    for (const auto& error : message->library.errors)
        if (std::find(pc_scan_errors_.begin(), pc_scan_errors_.end(), error) == pc_scan_errors_.end())
            showNotification(NotificationKind::error, tr(L"Biblioteka PC"), error);
    pc_scan_errors_ = std::move(message->library.errors);
    if (settingsOpen() && settings_state_.category == static_cast<int>(settings_ui::Category::library)) buildSettingsRows();
    if (!changed) { restorePlace(); return; }
    const auto visible = [&](const Game& game) {
        if (game.source == GameSource::steam || localFileGame(game.source)) return true; // Not part of this scan.
        const auto found = current.find(gameKey(game));
        return found != current.end() && (private_shelf_ || (found->second->visibility == GameVisibility::public_game &&
            !library_state_.hidden.contains(gameKey(game))));
    };
    if (pending_launch_ && !visible(pending_game_)) { pending_launch_ = false; launch_progress_ = 0; }
    closeEditorsFor([&](const Game& game) { return !visible(game); });
    if (screen_ == ScreenMode::details && !detail_games_.empty() && !visible(detail_games_[0])) { suspendDetails(); showCategory(0); }
    const auto update = [&](Game& game) {
        if (game.source == GameSource::steam || !isGame(game)) return;
        if (const auto found = current.find(gameKey(game)); found != current.end()) {
            const auto& next = *found->second;
            if (structure) {
                const auto played = game.account_id == next.account_id ? std::max(game.last_played, next.last_played) : next.last_played;
                const auto group = game.version_group;
                game = next; game.last_played = played; game.version_group = group;
                // Hidden in Launchme: the store reports it public, the copy stays hidden.
                if (game.visibility == GameVisibility::public_game && library_state_.hidden.contains(gameKey(game))) game.visibility = GameVisibility::hidden;
                return;
            }
            // Preserve derived local play history. Progress-only updates mutate
            // facts in place, leaving bitmaps, crop, focus and scroll untouched.
            game.installed = next.installed; game.installation = next.installation;
            game.play_action = next.play_action; game.install_action = next.install_action;
            game.install_directory = next.install_directory; game.executable = next.executable;
            game.store_playtime_minutes = next.store_playtime_minutes;
            game.store_tall_art = next.store_tall_art; game.store_wide_art = next.store_wide_art; game.store_logo_art = next.store_logo_art;
        }
    };
    // Its earlier art request found nothing (and was recorded so): ask again with the store's art.
    const auto retryArt = [&] {
        for (const auto& key : art_arrived) if (const auto found = current.find(key); found != current.end() && visible(*found->second)) retryGameArtwork(*found->second, false);
    };
    for (auto* list : {&public_library_, &grouped_library_, &pc_library_, &search_results_, &detail_games_, &store_results_, &private_category_.games, &nested_games_, &xmb_top_})
        for (auto& game : *list) update(game);
    for (auto* list : {&categories_, &library_categories_}) for (auto& category : *list) for (auto& game : category.games) update(game);
    for (auto& card : cards_) update(card.game);
    if (pending_launch_) update(pending_game_);
    if (!structure) { retryArt(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    buildShelves();
    const auto category = std::find_if(categories_.begin(), categories_.end(), [&](const Category& entry) { return entry.id == category_id; });
    if (category != categories_.end()) category_ = static_cast<size_t>(category - categories_.begin());
    if (screen_ == ScreenMode::library) {
        const auto view = renderer_.viewport();
        grid_scroll_ = std::min(grid_scroll_, library_layout::maxScroll(libraryView(), librarySource().size(), view.width, view.height, libraryPresentation()));
    }
    if (screen_ == ScreenMode::shelves || screen_ == ScreenMode::library || screen_ == ScreenMode::search) {
        if (screen_ == ScreenMode::search) updateSearch();
        else rebuildCards();
        if (homeUntouched()) focusCard(0, false); // The emulators' games arrive after Steam's: Recent re-sorts under the focus.
        else if (const auto found = findCard(selected_key, false)) focusCard(*found, library_controls_.focus < 0);
    }
    retryArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
    restorePlace();
}
