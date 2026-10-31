// Launching a game: Enter pressed and held on a card (a hold opens its menu), accept() and
// launch() for every screen, the start itself (store actions, Proton/Wine on Linux, sessions) and
// the launch animation over it.
#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../ui/launch_animation.hpp"
#include "../platform/text_encoding.hpp"
#ifndef _WIN32
#include "../model/launch_options_catalog.hpp"
#include "../platform/wine_runner.hpp"
#endif

#include <algorithm>

void Launcher::enterDown(bool repeated) {
    if (libraryDialogOpen()) { libraryDialogKey(VK_RETURN, repeated); return; }
    if (identifyOpen()) { if (!repeated) identifyAccept(); return; }
    if (gogInstallOpen()) { gogInstallKey(VK_RETURN, repeated); return; }
    if (customEditorOpen()) { customEditorKey(VK_RETURN, repeated); return; }
    if (repeated || pending_launch_) return;
    if (friends_open_) { if (!dashboardPress(1, true, GetTickCount64())) friendsAccept(); return; } // The drawer overlays every screen.
    if (glanceOpen()) { glanceAccept(); return; }
    if (companionOpen()) { companionAccept(); return; }
    if (screen_ == ScreenMode::plugin_page) { pluginPageAccept(); return; }
    if (downloadsOpen()) { downloadsAccept(); return; }
    if (searchBackFocused()) { back(); return; }
    if (libraryAccept()) return;
    if (cropOpen()) { cropAccept(); return; }
    if (devicesOpen()) { deviceAccept(); return; }
    if (artworkOpen()) { artworkAccept(); return; }
    if (flixIconsFocused()) { flixIconAccept(); return; }
    if (quick_open_) { quickAccept(); return; }
    if (screen_ == ScreenMode::settings) { settingsActivate(); return; }
    if (screen_ == ScreenMode::details) { if (menu_open_) menuAccept(); else launch(); return; }
    if (category_menu_open_) { categoryMenuAccept(); return; }
    if (enter_held_) return; // Ignore Windows auto-repeat while holding Enter.
    if (menu_open_) { menuAccept(); return; }
    if (categoryRailActive() && category_rail_focus_) { focusCategoryRail(false); return; }
    if (psfiveDetailsShown()) { psfiveDetailsAccept(); return; } // The details under the row: their screenshot or game.
    if (psfiveActions()) { psfiveActivate(); return; } // "Graj" / "…" act at once, no hold.
    if (psfiveTopFocused()) { psfiveTopActivate(); return; } // The top bar acts immediately too.
    if (cards_.empty()) return;
    if (cards_[selected_].game.kind == GameKind::platform ||
        cards_[selected_].game.kind == GameKind::pc_platform ||
        uiTile(cards_[selected_].game)) { launch(); return; }
    if (!isGame(cards_[selected_].game)) return;
    enter_held_ = true;
    enter_started_ = std::chrono::steady_clock::now();
}

void Launcher::enterUp() {
    if (dashboardPress(1, false, GetTickCount64())) return;
    if (!enter_held_) return;
    enter_held_ = false;
    const float held = std::chrono::duration<float>(std::chrono::steady_clock::now() - enter_started_).count();
    if (menu_open_) return;
    if (held >= enter_hold_seconds) {
        openContextMenu();
    } else accept();
}

void Launcher::cancelEnterHold() { enter_held_ = pad_enter_hold_ = false; dashboardCancelPress(); }

// A press on a game card that may become a hold for its menu (Enter, A on the pad): the
// cases accept() itself would hand to a dialog, panel or non-game tile act at once instead.
bool Launcher::acceptHoldsForMenu() const {
    if (glanceOpen() || companionOpen()) return false;
    if (pending_launch_ || menu_open_ || identifyOpen() || screen_ == ScreenMode::plugin_page || screen_ == ScreenMode::details || downloadsOpen() ||
        cropOpen() || devicesOpen() || artworkOpen() || (categoryRailActive() && category_rail_focus_) || metroTabsFocused() || psfiveActions() || psfiveTopFocused() || psfiveDetailsShown() || flixIconsFocused() ||
        (libraryControls() && (library_controls_.focus >= 0 || library_controls_.open() || library_controls_.action >= 0)) ||
        searchBackFocused() || cards_.empty() || selected_ >= cards_.size()) return false;
    const auto& game = cards_[selected_].game;
    return isGame(game) && game.kind != GameKind::platform && game.kind != GameKind::pc_platform && !uiTile(game);
}

void Launcher::accept() {
    if (libraryDialogOpen()) { collectionDialogAccept(); return; }
    if (desktopActive() && topDialog() == Modal::none && !friends_open_ && !quick_open_) { desktopAccept(); return; }
    if (searchBackFocused() && !friends_open_ && !menu_open_) { back(); return; }
    if (metroTabsFocused()) { launch(); return; } // Enter/A on the tabs: back to the page's tile.
    if (psfiveAccept()) return; // PSfive: a game goes down to "Graj" / "…", a button acts.
    // Only a plain pick of a game card; dialogs, panels, menus, tiles, the Store (always
    // details), the details screen and the Detailed view's buttons keep launch()'s handling.
    const bool details = settings_.accept_action == AppSettings::AcceptAction::details &&
        !launchAnimationActive() && !pending_launch_ && !friends_open_ && !category_menu_open_ && !quick_open_ && !menu_open_ &&
        screen_ != ScreenMode::plugin_page && !identifyOpen() && !gogInstallOpen() && !customEditorOpen() && !cropOpen() &&
        !devicesOpen() && !artworkOpen() && !achievementsOpen() && !(categoryRailActive() && category_rail_focus_) &&
        (screen_ == ScreenMode::shelves || screen_ == ScreenMode::platform_games || screen_ == ScreenMode::library || screen_ == ScreenMode::search) &&
        !(libraryControls() && (library_controls_.focus >= 0 || library_controls_.open() || library_controls_.action >= 0)) &&
        selected_ < cards_.size() && isGame(cards_[selected_].game) && cards_[selected_].game.kind != GameKind::catalog;
    if (details) openDetails(cards_[selected_].game); else launch();
}

void Launcher::launch() {
    if (glanceOpen() && !friends_open_) { glanceAccept(); return; }
    if (companionOpen() && !friends_open_) { companionAccept(); return; }
    if (libraryDialogOpen()) { collectionDialogAccept(); return; }
    if (menu_open_ && screen_ == ScreenMode::details) { menuAccept(); return; }
    if (launchAnimationActive()) return;
    if (screen_ == ScreenMode::plugin_page && !friends_open_) { pluginPageAccept(); return; }
    if (identifyOpen()) { identifyAccept(); return; }
    if (downloadsOpen() && !friends_open_ && !gogInstallOpen()) { downloadsAccept(); return; }
    if (gogInstallOpen()) { gogInstallAccept(); return; }
    if (searchBackFocused() && !friends_open_ && !menu_open_) { back(); return; }
    if (customEditorOpen()) { customEditorAccept(); return; }
    if (libraryAccept()) return;
    if (cropOpen()) { cropAccept(); return; }
    if (devicesOpen()) { deviceAccept(); return; }
    if (artworkOpen()) { artworkAccept(); return; }
    if (categoryRailActive() && category_rail_focus_ && !friends_open_ && !quick_open_) { focusCategoryRail(false); return; }
    if (metroTabsFocused() && !friends_open_ && !quick_open_ && !pending_launch_) { metroFocusTabs(false); audio_.navigate(); return; }
    // No games: Accept presses the empty home's focused button (Dodaj grę, Platformy, Odśwież).
    if (emptyHomeActive()) { emptyHomeActivate(empty_focus_); return; }
    if (cards_.empty() || pending_launch_ || friends_open_ || category_menu_open_ || quick_open_) return;
    if (achievementsOpen()) { // Back above the list closes it; an achievement opens its card, which closes again.
        if (achievements_.card) closeAchievementCard();
        else if (achievements_.selected < 0) closeAchievements();
        else openAchievementCard(achievements_.selected);
        return;
    }
    const auto& active = cards_[selected_].game;
    if (screen_ == ScreenMode::store) { openDetails(active); return; }
    if (screen_ == ScreenMode::details && detail_action_ == -1) { back(); return; }
    if (screen_ == ScreenMode::details && detail_action_ >= 0 && detail_action_ == closeDetailAction()) { const auto game = active; detail_action_ = 0; closeGame(game); return; } // ✕ beside Wróć.
    if (screen_ == ScreenMode::details && detail_action_ >= 0 && detail_action_ == cloudDetailAction()) {
        const auto game = active; openAurelia(&game); aureliaPage(aurelia_ui::Page::cloud); return;
    }
    if (screen_ == ScreenMode::details && detail_action_ == pluginDetailAction()) { openPluginDetail(); return; }
    if (screen_ == ScreenMode::details && detail_action_ == favoriteDetailAction()) { toggleFavorite(active); return; }
    if (screen_ == ScreenMode::details && detail_action_ == collectionsDetailAction()) { openCollectionManager(active); return; }
    if (screen_ == ScreenMode::details && detail_action_ == versionsDetailAction()) { openVersions(active, false); return; }
    if (screen_ == ScreenMode::details && detail_action_ == identifyDetailAction()) { openIdentify(); return; }
    if (screen_ == ScreenMode::details && detail_action_ == ratingDetailAction()) { openRatingSource(); return; }
    if (screen_ == ScreenMode::details && detail_action_ == achievementsDetailAction()) { openAchievements(); return; }
    if (screen_ == ScreenMode::details && detail_action_ == 1) { openContextMenu(); return; }
    if (screen_ == ScreenMode::details && active.kind == GameKind::catalog) { catalogAction(active); return; }
    if (active.kind == GameKind::all) { openLibrary(); return; }
    if (active.kind == GameKind::back) { back(); return; }
    if (active.kind == GameKind::settings) { openSettings(); return; }
    // Metro and XMB: Glance and Towarzyszka are tiles of the layout.
    if (active.kind == GameKind::glance) { openGlance(); return; }
    if (active.kind == GameKind::companion) { openCompanion(); return; }
    // Kafle Wszystkie, Glance…: Tarot and Dodaj grę as tiles of the first row.
    if (active.kind == GameKind::tarot) { openTarot(); return; }
    if (active.kind == GameKind::add) { openCustomEditor({}); return; }
    // XMB collection/console tiles open their column below instead of a separate screen.
    if (xmbActive() && selected_ < xmb_top_.size() && xmbColumn(active)) { xmbVertical(1); return; }
    if (active.kind == GameKind::category && (flixActive() || shelfAllTile() || psfiveActive())) { openCategoryLibrary(active.source_id); return; }
    if (metroActivate()) return; // Metro: store/console shortcuts slide to their page, "Wszystkie" opens the library.
    if (active.kind == GameKind::category) return;
    if (active.kind == GameKind::platform) { openPlatform(active.platform); return; }
    if (active.kind == GameKind::pc_platform) { openPcLibrary(active.source); return; }
    if (!active.version_group.empty() && !(screen_ == ScreenMode::details && detail_version_selected_)) {
        if (const auto pref = library_state_.preferred_versions.find(active.version_group); pref != library_state_.preferred_versions.end())
            for (const auto& copy : public_library_)
                if (const auto group = versions_.by_game.find(gameKey(copy)); gameKey(copy) == pref->second &&
                    group != versions_.by_game.end() && group->second == active.version_group && actionEnabled(copy)) { launchGame(copy); return; }
        openVersions(active, true); return;
    }
    launchGame(active);
}

void Launcher::launchGame(const Game& original, bool via_steam) {
    Game active = original; active.version_group.clear();
    if (pending_launch_ || launchAnimationActive() || !isGame(active)) return;
    if (switchToGame(active)) { focusPlayedGame(gameKey(active)); return; }
    // A preload: downloaded, not released yet; Steam installs it on the release day by itself.
    if (active.preloaded) {
        showNotification(NotificationKind::launch, active.name, tr(L"Pobrana wstępnie — gotowa do gry w dniu premiery."));
        audio_.context(); return;
    }
    if (aurelia_active_ && aurelia::validGame(active) && (aurelia_operations_.contains(active.app_id) || aurelia_playing_.contains(active.app_id))) {
        showNotification(NotificationKind::error, L"Aurelia", tr(L"Operacja dla tej gry już trwa")); return;
    }
    // A game Aurelia manages is installed and launched through it; any other Steam game, or one
    // whose launch through Aurelia was refused, keeps the Steam client (below).
    if (!via_steam && (aureliaManaged(active) || (aurelia_active_ && aurelia::validGame(active) && aurelia_downloads_.contains(active.app_id)))) {
        aureliaLaunch(active); return;
    }
    if (active.source == GameSource::gog && !active.installed && !gog_state_.account.empty() &&
        (!active.account_id.empty() || active.installation.active())) { openGogInstall(active); return; }
    if (active.source == GameSource::epic && !active.installed && !epic_state_.account.empty() &&
        (!active.account_id.empty() || active.installation.active())) { openEpicInstall(active); return; }
    if (active.source == GameSource::epic && active.epic_managed) { startEpicGame(active); return; }
    // Amazon Games: an account title is installed through Nile; an installed one starts from its fuel.json.
    if (active.source == GameSource::amazon && !active.installed && amazonConnected() &&
        (!active.account_id.empty() || active.installation.active())) { openAmazonInstall(active); return; }
    // Xbox: with Instalacja przez Launchme an account's GDK title is installed by the helper; otherwise
    // (and for every other title) Instaluj opens the Xbox app.
    if (active.source == GameSource::xbox && !active.installed && xboxConnected() && xboxOwnInstall(active)) { openXboxInstall(active); return; }
    // A Steam game to install: the library picked in Launchme's dialog (src/app/steam_install.cpp).
    if (steamInstallDialogWanted(active) && openSteamInstall(active)) return;
    // A game added by hand without its file ("Wskaż plik"): its editor opens at the file.
    if (fileNotChosen(active)) {
        dismissMenu();
        openCustomEditor(active.source_id, desktop_mode_ ? custom_game_ui::Item::file : custom_game_ui::Item::file_browse);
        return;
    }
    const auto action = primaryGameAction(active);
    if (!action) {
        if (localFileGame(active.source))
            showNotification(NotificationKind::error, tr(L"Nie znaleziono pliku gry"), active.play_action.target);
        else showNotification(NotificationKind::error, tr(L"Brak akcji dla tej gry"),
                              std::wstring(tr(L"Sprawdź konfigurację ")) + sourceName(active.source));
        return;
    }
#ifndef _WIN32
    // A Windows game and no Proton or Wine on this computer: GE-Proton is downloaded, then it starts.
    if (action.kind == ActionKind::play && compatGame(active) && !compatToolReady(active)) { dismissMenu(); compatProvision(active); return; }
#endif
    // A Steam game another remembered account installed: Steam restarts signed in to it first.
    if (action.kind == ActionKind::play && !beginSteamAccountSwitch(active)) return;
    if (settings_.launch_animation && action.kind == ActionKind::play) startLaunchAnimation(active);
    audio_.launch();
    if (action.kind == ActionKind::play) audio_.stopMusic();
    pending_game_ = active;
    pending_launch_ = true;
    launch_age_ = 0;
    dismissMenu();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::finishLaunch() {
    auto action = primaryGameAction(pending_game_);
    pending_launch_ = false;
    // "Opcje uruchamiania…": a Steam game's own options go through steam.exe -applaunch.
    if (const auto options = launchArguments(pending_game_); !options.empty() && action.kind == ActionKind::play)
        action = steamOptionsAction(std::move(action), steam_executable_(), pending_game_.app_id, options, settings_.steam_silent_launch);
    if (settings_.steam_silent_launch && pending_game_.source == GameSource::steam && action.kind == ActionKind::play)
        action = silentSteamAction(std::move(action), steam_executable_());
#ifndef _WIN32
    // A Windows program runs through umu/Proton/Wine with the game's options in its prefix.
    if (compatGame(pending_game_)) action.compat = launch_options_store::compatLaunch(pending_game_);
#endif
    auto result = execute_(hwnd_, action);
    if (result <= 32 && result != launch_error::cancelled && action.keep_focus) {
        // steam.exe refused (moved, blocked): the plain steam:// URL still starts the game.
        action = primaryGameAction(pending_game_);
        result = execute_(hwnd_, action);
    }
    // The started program's process, when Windows reports one (a link handed to a running
    // client has none): its process tree follows the session (scripts, .lnk, launcher stubs).
    std::shared_ptr<void> job;
    if (const HANDLE process = execute_ == executeGameAction ? takeLaunchedProcess() : HANDLE{}) {
        if (result > 32 && action.kind == ActionKind::play)
            if (const HANDLE created = launchJob(process)) job = ownLaunchJob(created);
        closeLaunchedProcess(process);
    }
    // Linux: why umu/Proton/Wine could not start it, and the options it went without.
    [[maybe_unused]] const auto problems = execute_ == executeGameAction ? takeLaunchProblems() : LaunchProblems{};
    std::wstring skipped;
#ifndef _WIN32
    for (const auto& code : problems.missing)
        if (const auto* text = launch_options_catalog::text(wine_runner::problemText(code)); text && *text) skipped += (skipped.empty() ? L"" : L"; ") + std::wstring(text);
#endif
    if (result <= 32) {
        stopLaunchAnimation();
        if (!anyActiveSession()) audio_.resumeMusic();
        if (result == launch_error::cancelled) return; // The user said no (UAC, the client's own dialog).
        const bool file = std::filesystem::path(action.target).has_root_path() && action.target.find(L"://") == std::wstring::npos;
        const auto title = action.kind == ActionKind::open_client ? tr(L"Nie udało się otworzyć klienta") :
                           action.kind == ActionKind::install ? tr(L"Nie udało się rozpocząć instalacji") : tr(L"Nie udało się uruchomić gry");
        std::wstring detail;
#ifndef _WIN32
        if (!problems.error.empty()) detail = launch_options_catalog::text(wine_runner::problemText(problems.error));
#endif
        if (!detail.empty()) {} // umu/Proton/Wine's own reason ("No Proton or Wine…").
        else if (result == launch_error::no_handler && !file)
            detail = std::wstring(sourceName(pending_game_.source)) + L": " + tr(L"klient nie jest zainstalowany");
        else if ((result == launch_error::file_missing || result == launch_error::path_missing) && file)
            detail = std::wstring(tr(L"Brak pliku: ")) + action.target;
        else if (result == launch_error::denied && file)
            detail = std::wstring(tr(L"Brak dostępu: ")) + action.target;
        else if (file || localFileGame(pending_game_.source)) detail = action.target; // A file on disk: show which one.
        else detail = std::wstring(tr(L"Sprawdź, czy ")) + sourceName(pending_game_.source) + tr(L" jest dostępny");
        showNotification(NotificationKind::error, title, pending_game_.name + L" · " + detail);
    } else if (action.kind == ActionKind::play) {
        // Silent Steam: nobody else may take the foreground, so wait longer for the game's window.
        beginGameSession(pending_game_, action.keep_focus ? silent_focus_ms : default_focus_ms, std::move(job));
        // Clients and updates can take a while. An option whose program is missing is named.
        showNotification(NotificationKind::launch, pending_game_.name, skipped.empty() ? std::wstring(tr(L"Uruchamianie…")) : tr(L"Uruchamianie bez: ") + skipped);
        if (settings_.quick_resume) freezeOthers(gameKey(pending_game_));
        // On return the started game waits at the front of Recent, focused; under the
        // cover animation the view behind it changes only when the animation ends.
        if (launchAnimationActive()) launch_played_key_ = gameKey(pending_game_);
        else focusPlayedGame(gameKey(pending_game_));
    }
}

void Launcher::startLaunchAnimation(const Game& game) {
    if (!settings_.launch_animation) return;
    launch_animation_title_ = game.name;
    launch_artwork_key_ = gameKey(game);
    launch_animation_started_ = std::chrono::steady_clock::now();
    launch_animation_ending_.reset();
    launch_animation_rested_ = false;
    launch_progress_ = 0;
    dismissMenu(); closeQuick(); closeFriends(); closeCategoryMenu();
    cancelEnterHold(); cancelTabHold();
    // Its background and logo on the focused card (the picture uses them); missing ones are asked for.
    if (selected_ < cards_.size() && gameKey(cards_[selected_].game) == launch_artwork_key_) {
        if (const auto* art = art_.find(launch_artwork_key_)) {
            if (art->hero) renderer_.setHero(selected_, art->hero, art->hero_tone, false, art->hero_focus);
            if (art->logo) renderer_.setLogo(selected_, art->logo);
        }
        requestArt(game, CoverMessage::Kind::hero);
        requestArt(game, CoverMessage::Kind::logo);
    }
    POINT cursor{};
    if (GetCursorPos(&cursor) && WindowFromPoint(cursor) == hwnd_) SetCursor(nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::stopLaunchAnimation() {
    if (!launchAnimationActive()) return;
    launch_animation_started_.reset();
    launch_animation_ending_.reset();
    launch_animation_rested_ = false;
    launch_animation_title_.clear();
    launch_artwork_key_.clear();
    launch_played_key_.clear();
    POINT cursor{};
    if (GetCursorPos(&cursor) && WindowFromPoint(cursor) == hwnd_) SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The picture stays until the game is there (src/ui/launch_animation.hpp):
// - another window in front (the game, its launcher): it rests, still, behind it;
// - the launcher seen again after that while the game runs (Alt+Tab, the Guide's home): it fades out;
// - in front all along: at least `seconds`, longer while the game still starts or its window is
//   being brought forward, never beyond `longest`;
// - the game's session ending (or failing to start) ends it at once (onGameActivity).
void Launcher::tickLaunchAnimation(std::chrono::steady_clock::time_point now) {
    if (!launchAnimationActive()) return;
    if (launch_animation_ending_) {
        if (now - *launch_animation_ending_ >= std::chrono::duration<float>(launch_animation::fade_out)) endLaunchAnimation();
        return;
    }
    if (pending_launch_) return; // Waiting for a Steam account switch: the game is not started yet.
    const float age = std::chrono::duration<float>(now - *launch_animation_started_).count();
    if (const HWND foreground = overlay_backend_.foreground(); foreground != hwnd_) {
        if (foreground) launch_animation_rested_ = true; // No foreground at all is a moment between two windows.
        return;
    }
    const auto session = game_sessions_.find(launch_artwork_key_);
    const bool running = session != game_sessions_.end() && session->second.running();
    const bool starting = session != game_sessions_.end() && !running;
    if (launch_animation_rested_ && running) { fadeLaunchAnimation(now); return; }
    if (age < launch_animation::seconds) return;
    if (age < launch_animation::longest && (starting || (running && launch_focus_key_ == launch_artwork_key_))) return;
    fadeLaunchAnimation(now);
}

void Launcher::fadeLaunchAnimation(std::chrono::steady_clock::time_point now) {
    if (!launchAnimationActive() || launch_animation_ending_) return;
    launch_animation_ending_ = now;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Behind the game the picture is still: no frames until it is seen again (or fades out).
bool Launcher::launchAnimationMoving(std::chrono::steady_clock::time_point now) const {
    if (!launchAnimationActive()) return false;
    if (launch_animation_ending_) return true;
    if (overlay_backend_.foreground() == hwnd_) return true;
    return !launch_animation_rested_ && !launch_animation::still(std::chrono::duration<float>(now - *launch_animation_started_).count());
}

// Back (Esc, B, the mouse's back button) hides the picture early; the press that started the game
// (and its repeats) does not.
bool Launcher::dismissLaunchAnimation(std::chrono::steady_clock::time_point now) {
    if (!launchAnimationActive() || now - *launch_animation_started_ < std::chrono::duration<float>(launch_animation::dismiss_after)) return false;
    endLaunchAnimation();
    return true;
}

void Launcher::endLaunchAnimation() {
    const auto played = std::move(launch_played_key_);
    stopLaunchAnimation();
    if (screen_ == ScreenMode::details) {
        store_jobs_->generation.fetch_add(1);
        details_loading_ = false;
    }
    if (screen_ != ScreenMode::shelves) showCategory(category_);
    focusPlayedGame(played);
}
