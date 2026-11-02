// Experimental quick resume: suspend a running game in RAM from its menu and
// resume it later. Resuming or starting one game suspends the others. The list
// of suspended processes is persisted, so a restart never orphans a game.
#include "launcher.hpp"
#include "../ui/exit_prompt.hpp"
#include "../ui/settings_view.hpp"

#include <algorithm>

namespace {
// A Game from the library can be observed by path/registry telemetry; a restored
// placeholder cannot, and keeps being observed by its exact processes instead.
bool observable(const Game& game) {
    return (game.source == GameSource::steam && game.app_id && isGame(game)) ||
           game.executable.is_absolute() || game.install_directory.is_absolute();
}
}

std::filesystem::path Launcher::quickResumeFile() const { return userConfigDirectory() / L"quick-resume.txt"; }

SessionState Launcher::sessionState(const Game& game) const {
    if (!isGame(game)) return SessionState::none;
    const auto state = ::sessionState(game_sessions_, gameKey(game));
    // A failed recovery must remain resumable even after the feature was disabled.
    return settings_.quick_resume || settings_.guide_overlay || state == SessionState::frozen ? state : SessionState::none;
}

bool Launcher::anyActiveSession() const {
    return std::any_of(game_sessions_.begin(), game_sessions_.end(), [](const auto& entry) { return !entry.second.frozen(); });
}

bool Launcher::sameTile(const Game& tile, const std::string& key) const {
    if (key.empty() || !isGame(tile)) return false;
    if (gameKey(tile) == key) return true;
    if (tile.version_group.empty()) return false;
    const auto group = versions_.by_game.find(key);
    return group != versions_.by_game.end() && group->second == tile.version_group;
}

const GameSession* Launcher::tileSession(const Game& tile) const {
    if (game_sessions_.empty() || !isGame(tile)) return nullptr;
    if (const auto found = game_sessions_.find(gameKey(tile)); found != game_sessions_.end()) return &found->second;
    if (tile.version_group.empty()) return nullptr;
    for (const auto& [key, session] : game_sessions_) if (sameTile(tile, key)) return &session;
    return nullptr;
}

void Launcher::focusPlayedGame(const std::string& key) {
    if (key.empty() || categories_.empty() || scanning_ || library_transition_) return;
    refreshRecentGames(); // Its session puts it at the front of Recent.
    // An open editor or prompt keeps the view behind it; Home follows on the next start.
    if (exit_prompt_ || power_menu_ || gogInstallOpen() || customEditorOpen() || identifyOpen() || settingsOpen() ||
        artworkOpen() || cropOpen() || devicesOpen() || libraryDialogOpen() || gameOverlayOpen()) return;
    restore_pending_ = false; // The game wins over a startup bookmark still waiting for a store scan.
    cancelEnterHold(); cancelTabHold();
    dismissMenu(); closeQuick(); closeCategoryMenu(); closeFriends();
    menu_progress_ = quick_progress_ = category_menu_progress_ = friends_progress_ = 0;
    // The normal closing paths for libraries, details and nested views.
    for (int i = 0; i < 8 && screen_ != ScreenMode::shelves; ++i) back();
    if (screen_ != ScreenMode::shelves) return;
    if (desktop_mode_) {
        // The desktop window stays in its section; the game keeps (or regains) the selection there.
        desktop_menu_.reset(); desktop_downloads_open_ = false;
        rebuildCards(true);
        for (size_t i = 0; i < cards_.size(); ++i) if (sameTile(cards_[i].game, key)) { desktopFocus(i); break; }
        return;
    }
    const auto recent = std::find_if(categories_.begin(), categories_.end(), [](const Category& item) { return item.id == "recent"; });
    if (recent == categories_.end()) return;
    const auto category = static_cast<size_t>(recent - categories_.begin());
    const auto game = std::find_if(recent->games.begin(), recent->games.end(), [&](const Game& item) { return sameTile(item, key); });
    if (game == recent->games.end()) return;
    const auto position = static_cast<size_t>(game - recent->games.begin());
    const auto tile = [&](size_t begin, size_t end) -> std::optional<size_t> {
        for (size_t i = begin; i < end && i < cards_.size(); ++i) if (sameTile(cards_[i].game, key)) return i;
        return std::nullopt;
    };
    if (xmbActive()) {
        if (const auto found = tile(0, xmb_top_.size())) xmbFocus(*found, -1); // Recent opens the XMB tile row.
    } else if (flixActive()) {
        const auto row = std::find_if(flix_rows_.begin(), flix_rows_.end(), [&](const FlixRow& item) { return item.category == category; });
        if (row != flix_rows_.end())
            if (const auto found = tile(row->begin, row->begin + row->count)) flixFocus(static_cast<size_t>(row - flix_rows_.begin()), *found - row->begin);
    } else if (metroActive()) {
        const auto page = std::find_if(metro_pages_.begin(), metro_pages_.end(), [&](const MetroPage& item) { return item.category == category; });
        if (page != metro_pages_.end())
            if (const auto found = tile(page->begin, page->begin + page->count)) metroFocus(*found);
    } else {
        categories_[category].selected = position;
        showCategory(category);
        if (const auto found = tile(0, cards_.size())) focusCard(*found);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

context_menu::Rows Launcher::menuRows() const {
    if (cards_.empty()) return {};
    const auto& game = cards_[selected_].game;
    if (studioMenuTile(game)) { context_menu::Rows rows; rows.studio = true; return rows; }
    if (screen_ == ScreenMode::details) {
        context_menu::Rows rows{false, 0, false, true, !gameFolder(game).empty(), canExclude(game), !hideableCopies(game).empty()};
        rows.uninstall = canUninstall(game) && sessionState(game) == SessionState::none; // Never while it runs.
        rows.launch = launchEditable(game);
        rows.compat = compatGame(game) && !gameKey(game).empty();
        rows.shortcuts = shortcutAvailable(game, ShortcutPlace::desktop) || shortcutAvailable(game, ShortcutPlace::menu) ||
                         shortcutAvailable(game, ShortcutPlace::steam);
        rows.aurelia = aurelia_active_ && aurelia::validGame(game);
        rows.steam_account = steamAccountChoices(game).size() > 1;
        rows.emulator = gameEmulatorEditable(game);
        rows.metadata = isGame(game) && game.kind != GameKind::catalog && !gameKey(game).empty(); // Not a store's offer.
        rows.remove = canRemoveFromLibrary(game) && sessionState(game) == SessionState::none; // Usuń z biblioteki, never while it runs.
        return rows;
    }
    const auto state = sessionState(game);
    context_menu::Rows rows{settings_.quick_resume && state == SessionState::running, static_cast<int>(pluginContext(game).size()), state != SessionState::none};
    rows.secondary_only = psfiveActive();
    rows.uninstall = canUninstall(game);
    rows.remove = canRemoveFromLibrary(game);
    rows.aurelia = aurelia_active_ && aurelia::validGame(game);
    rows.identify = unidentifiedGame(game);
    return rows;
}

Game Launcher::trackedGame(const std::string& key) const {
    std::lock_guard lock(game_activity_jobs_->mutex);
    const auto found = game_activity_jobs_->active.find(key);
    return found == game_activity_jobs_->active.end() ? Game{} : found->second.game;
}

bool Launcher::freezeGame(const std::string& key, bool notify) {
    const auto session = game_sessions_.find(key);
    if (session == game_sessions_.end() || session->second.frozen() || !session->second.running()) return false;
    const auto game = trackedGame(key);
    auto processes = quick_resume_.find(game);
    const auto fail = [&](const wchar_t* title, const wchar_t* reason) {
        if (notify) showNotification(NotificationKind::error, title, reason);
        return false;
    };
    if (processes.empty()) return fail(tr(L"Nie można wstrzymać gry"), tr(L"Nie znaleziono procesu gry"));
    // Write ahead: a storage failure cannot strand a newly suspended process,
    // and a crash between suspension and the final update still leaves its PID.
    FrozenGame pending{key, processes};
    if (!persistFrozenGames(&pending)) return false;
    const auto result = quick_resume_.suspend(processes);
    if (result != process_freeze::Result::ok) {
        persistFrozenGames(); // Remove the intent after rollback/exit.
        return fail(tr(L"Nie można wstrzymać gry"), result == process_freeze::Result::denied ?
            tr(L"Brak dostępu: gra działa jako administrator lub z ochroną anti-cheat") : tr(L"Gra została zamknięta"));
    }
    session->second.freeze(processes);
    trackSession(key, game, processes);
    // A process started between the scan and the suspension would keep running.
    auto late = quick_resume_.find(game);
    std::erase_if(late, [&](const GameProcess& process) { return std::find(processes.begin(), processes.end(), process) != processes.end(); });
    if (!late.empty()) {
        pending.processes = processes;
        pending.processes.insert(pending.processes.end(), late.begin(), late.end());
        if (persistFrozenGames(&pending) && quick_resume_.suspend(late) == process_freeze::Result::ok)
            processes.insert(processes.end(), late.begin(), late.end());
    }
    session->second.freeze(processes);
    trackSession(key, game, std::move(processes));
    persistFrozenGames();
    if (!anyActiveSession() && !pending_launch_) audio_.resumeMusic();
    if (!categories_.empty()) refreshRecentGames(); // Suspended games follow the running ones in Recent.
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void Launcher::freezeOthers(const std::string& except) {
    std::vector<std::string> running;
    for (const auto& [key, session] : game_sessions_)
        if (key != except && !session.frozen() && session.running()) running.push_back(key);
    for (const auto& key : running) freezeGame(key, true);
}

void Launcher::resumeGame(const Game& game) {
    const auto key = gameKey(game);
    const auto session = game_sessions_.find(key);
    if (session == game_sessions_.end() || !session->second.frozen()) return;
    if (settings_.quick_resume) freezeOthers(key);
    const auto processes = session->second.frozenProcesses();
    const auto result = quick_resume_.resume(processes);
    if (result == process_freeze::Result::denied) {
        showNotification(NotificationKind::error, tr(L"Nie można wznowić gry"), tr(L"Spróbuj ponownie"));
        return;
    }
    session->second.thaw();
    // Once resumed, path/registry telemetry also sees processes the game starts later.
    const auto tracked = trackedGame(key);
    const auto& observed = observable(tracked) ? tracked : game;
    trackSession(key, observed, observable(observed) ? std::vector<GameProcess>{} : processes);
    persistFrozenGames();
    if (!categories_.empty()) refreshRecentGames();
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (result == process_freeze::Result::gone) return; // The activity worker ends the session.
    audio_.stopMusic();
    if (!quick_resume_.focus(processes)) requestGameFocus(observed);
}

// "Zamknij grę": ends the game's processes at once, like quitting on a console.
// A suspended game cannot answer a close request, so there is no graceful path.
// A game launched through Aurelia is closed the same way: its processes, found under its folder,
// end here and Aurelia's `play` returns with them. Only when none is found yet does Aurelia stop
// the process it recorded (aureliaStop), and the game counts as closed when `play` returns.
void Launcher::closeGame(const Game& game) {
    const bool aurelia = aurelia_active_ && aurelia::validGame(game) && aurelia_playing_.contains(game.app_id);
    const auto key = gameKey(game);
    const auto session = game_sessions_.find(key);
    if (session == game_sessions_.end()) { if (aurelia) aureliaStop(game); return; }
    const bool frozen = session->second.frozen();
    auto tracked = trackedGame(key);
    if (!observable(tracked)) tracked = game;
    const auto processes = frozen ? session->second.frozenProcesses() : quick_resume_.find(tracked);
    if (processes.empty() && aurelia && !frozen) { aureliaStop(game); return; }
    if (processes.empty()) {
        showNotification(NotificationKind::error, tr(L"Nie można zamknąć gry"), tr(L"Nie znaleziono procesu gry"));
        return;
    }
    if (quick_resume_.terminate(processes) == process_freeze::Result::denied) {
        showNotification(NotificationKind::error, tr(L"Nie można zamknąć gry"), tr(L"Brak dostępu: gra działa jako administrator lub z ochroną anti-cheat"));
        return;
    }
    // An explicit close ends the session now, without the exit debounce.
    game_sessions_.erase(session);
    {
        std::lock_guard lock(game_activity_jobs_->mutex);
        game_activity_jobs_->active.erase(key);
        game_activity_jobs_->generation.fetch_add(1); game_activity_jobs_->wake.notify_one();
    }
    if (frozen) persistFrozenGames();
    if (!anyActiveSession() && !pending_launch_) audio_.resumeMusic();
    if (!categories_.empty()) refreshRecentGames(); // No longer pinned to the front of Recent.
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// "Graj" on a game that already runs switches to it instead of starting it again. This holds
// whatever the experimental options say: a second copy of a running game is never started.
bool Launcher::switchToGame(const Game& game) {
    const auto state = sessionState(game);
    if (state == SessionState::frozen) { dismissMenu(); resumeGame(game); return true; }
    const auto key = gameKey(game);
    const auto session = game_sessions_.find(key);
    if (session == game_sessions_.end() || session->second.frozen()) return false;
    if (!session->second.running()) {
        // Dispatched, not seen yet (a client starting, Steam updating it). A second press within
        // that time is not a second start; after a while it is a deliberate retry.
        if (GetTickCount64() - session->second.startedAt() >= 20000) return false;
        dismissMenu();
        showNotification(NotificationKind::launch, game.name, tr(L"Gra się uruchamia…"));
        return true;
    }
    auto tracked = trackedGame(key);
    if (!observable(tracked)) tracked = game;
    const auto processes = quick_resume_.find(tracked);
    dismissMenu();
    if (settings_.quick_resume) freezeOthers(key);
    audio_.stopMusic();
    if (processes.empty() || !quick_resume_.focus(processes)) requestGameFocus(tracked);
    return true;
}

void Launcher::resumeFrozenGames() {
    bool resumed = false, denied = false;
    for (auto& [key, session] : game_sessions_) {
        if (!session.frozen()) continue;
        const auto processes = session.frozenProcesses();
        if (quick_resume_.resume(processes) == process_freeze::Result::denied) { denied = true; continue; }
        session.thaw();
        const auto tracked = trackedGame(key);
        trackSession(key, tracked, observable(tracked) ? std::vector<GameProcess>{} : processes);
        resumed = true;
    }
    persistFrozenGames();
    if (denied) showNotification(NotificationKind::error, tr(L"Nie można wznowić gry"), tr(L"Spróbuj ponownie"));
    if (resumed) { audio_.stopMusic(); if (!categories_.empty() && !closing_) refreshRecentGames(); InvalidateRect(hwnd_, nullptr, FALSE); }
}

bool Launcher::persistFrozenGames(const FrozenGame* pending) {
    std::vector<FrozenGame> frozen;
    for (const auto& [key, session] : game_sessions_)
        if (session.frozen() && (!pending || pending->key != key)) frozen.push_back({key, session.frozenProcesses()});
    if (pending) frozen.push_back(*pending);
    if (frozen_state_readable_ && quick_resume_.save(quickResumeFile(), frozen)) return true;
    showNotification(NotificationKind::error, tr(L"Nie można zapisać stanu wstrzymanych gier"), L"");
    return false;
}

// Games left suspended by an earlier launcher (exit prompt, crash) come back as
// suspended sessions. With the feature off they are resumed immediately.
void Launcher::restoreFrozenGames() {
    auto records = process_freeze::load(quickResumeFile());
    frozen_state_readable_ = records.has_value();
    if (!records) {
        showNotification(NotificationKind::error, tr(L"Nie można odczytać stanu wstrzymanych gier"), L"");
        return; // Do not replace an unreadable journal with an empty one.
    }
    bool denied = false;
    for (auto& record : *records) {
        if (!quick_resume_.alive(record.processes)) continue;
        if (!settings_.quick_resume) {
            if (quick_resume_.resume(record.processes) != process_freeze::Result::denied) continue;
            denied = true; // Retain both the durable record and a retryable session.
        }
        auto [session, inserted] = game_sessions_.try_emplace(record.key, GetTickCount64());
        if (!inserted) continue;
        session->second.freeze(record.processes);
        Game placeholder;
        placeholder.kind = GameKind::store; // Never observable: exact processes decide its end.
        trackSession(record.key, placeholder, std::move(record.processes));
    }
    persistFrozenGames();
    if (denied) showNotification(NotificationKind::error, tr(L"Nie można wznowić gry"), tr(L"Spróbuj ponownie"));
}

bool Launcher::requestClose() {
    stopLaunchAnimation();
    if (closing_) return !close_ready_;
    if (gameOverlayOpen()) overlayDashboard();
    if (exit_prompt_) return true;
    const auto frozen = std::count_if(game_sessions_.begin(), game_sessions_.end(), [](const auto& entry) { return entry.second.frozen(); });
    if (!frozen) { beginClose(); return true; }
    stopScreensaver(false);
    dismissMenu(); closeQuick(); closeCategoryMenu(); closeFriends(); cancelEnterHold();
    // Closed from the taskbar while minimized (or from the tray while hidden): the question must be visible.
    leaveTray();
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    prompt_kind_ = PromptKind::exit;
    exit_prompt_ = static_cast<int>(exit_prompt::Choice::resume);
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void Launcher::beginClose() {
    if (closing_) return;
    closing_ = true;
    removeTrayIcon();
    close_started_ = last_frame_ = std::chrono::steady_clock::now();
    pending_launch_ = false;
    cancelGameFocus();
    cancelEnterHold(); cancelTabHold(); pointerUp(true);
    controller_input_.stopRumble();
    audio_.cancelPendingMusic(); audio_.stopMusic();
    POINT cursor{};
    if (GetCursorPos(&cursor) && WindowFromPoint(cursor) == hwnd_) SetCursor(nullptr);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::tickClose(std::chrono::steady_clock::time_point now) {
    if (close_ready_) return;
    exit_progress_ = std::clamp(std::chrono::duration<float>(now - close_started_).count(), 0.0f, 1.0f);
    audio_.update(std::clamp(std::chrono::duration<float>(now - last_frame_).count(), 0.0f, 0.05f));
    last_frame_ = now;
    // Present a fully black frame before allowing WM_CLOSE through. A minimized
    // window has no paint loop, so its exit still completes after the same second.
    if (exit_progress_ >= 1 && (exit_black_painted_ || IsIconic(hwnd_) || !IsWindowVisible(hwnd_))) {
        // Posted, never destroyed here: WM_DESTROY deletes this Launcher.
        close_ready_ = PostMessageW(hwnd_, WM_CLOSE, 0, 0) != FALSE;
    } else InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::exitPromptMove(int amount) {
    if (!exit_prompt_) return;
    const int next = std::clamp(*exit_prompt_ + amount, 0, exit_prompt::count - 1);
    if (next == *exit_prompt_) { navigationBoundary(); return; }
    exit_prompt_ = next;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::exitPromptAccept() {
    if (!exit_prompt_) return;
    const auto choice = static_cast<exit_prompt::Choice>(*exit_prompt_);
    exit_prompt_.reset();
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (prompt_kind_ == PromptKind::uninstall) {
        prompt_kind_ = PromptKind::exit;
        const int index = static_cast<int>(choice); // 0 uninstall, 1 open its folder first, 2 cancel.
        const auto game = std::exchange(uninstall_game_, Game{});
        const auto silent = std::exchange(uninstall_silent_, GameAction{});
        if (index == 0) uninstallManagedGame(game, silent);
        else if (index == 1 && game.install_directory.is_absolute())
            execute_(hwnd_, GameAction{game.install_directory.wstring(), {}, {}, ActionKind::open_client});
        return;
    }
    if (prompt_kind_ == PromptKind::remove) {
        prompt_kind_ = PromptKind::exit;
        const int index = static_cast<int>(choice); // 0 remove it from the library, 1 its editor, 2 cancel.
        const auto game = std::exchange(remove_game_, Game{});
        if (index == 0) {
            if (removeCustomGame(game.source_id)) showNotification(NotificationKind::launch, tr(L"Usunięto z biblioteki"), game.name);
            else showNotification(NotificationKind::error, tr(L"Nie udało się zapisać listy gier."), game.name);
        } else if (index == 1) openCustomEditor(game.source_id);
        return;
    }
    if (prompt_kind_ == PromptKind::exclude) {
        prompt_kind_ = PromptKind::exit;
        const int index = static_cast<int>(choice); // 0 exclude it, 1 only hide it (Ctrl+H shows it), 2 cancel.
        const auto game = std::exchange(exclude_game_, Game{});
        if (index == 0) excludeGame(game);
        else if (index == 1 && !hiddenInLauncher(game)) toggleHidden(game);
        return;
    }
    if (prompt_kind_ == PromptKind::delete_collection) {
        prompt_kind_ = PromptKind::exit;
        const int index = static_cast<int>(choice); // 0 delete it, 1 hide it on (or show it again on) the home screen, 2 cancel.
        const auto key = std::exchange(delete_collection_key_, std::string{});
        const auto found = std::find_if(settings_collections_.begin(), settings_collections_.end(),
                                        [&](const SteamCollection& c) { return collectionKey(account_, c) == key; });
        if (found == settings_collections_.end() || index == 2) return;
        const auto collection = *found;
        if (index == 0) {
            settings_state_.subcontrol = 0; // The next row's switch, not a trash it may not have.
            deleteCollection(collection);
            showNotification(NotificationKind::launch, tr(L"Usunięto kolekcję"), collection.name);
        } else {
            if (!settings_.hidden_collections.erase(key)) settings_.hidden_collections.insert(key);
            settingsChanged(true);
        }
        audio_.context();
        return;
    }
    if (prompt_kind_ == PromptKind::restore_saves) {
        prompt_kind_ = PromptKind::exit;
        const int index = static_cast<int>(choice); // 0 restore, 1 back up the current saves instead, 2 cancel.
        const auto game = std::exchange(restore_game_, Game{});
        if (index == 0) gameSaves(game, true, false);
        else if (index == 1) gameSaves(game, false, false);
        return;
    }
    if (prompt_kind_ == PromptKind::move_videos) {
        prompt_kind_ = PromptKind::exit;
        const int index = static_cast<int>(choice); // 0 move them, 1 leave them where they are, 2 cancel.
        if (index == 2) return;
        const auto from = video_move_from_, to = video_move_to_;
        settings_.video_folder = to == userConfigDirectory() / L"media" ? std::filesystem::path{} : to;
        settingsChanged(); audio_.context();
        if (index == 0) moveVideoFolder(from, to);
        return;
    }
    if (choice == exit_prompt::Choice::cancel) return;
    if (choice == exit_prompt::Choice::resume) resumeFrozenGames();
    // Suspended games stay listed in quick-resume.txt for the next launcher start.
    beginClose();
}

void Launcher::exitPromptKey(WPARAM key, bool repeated) {
    if (key == VK_UP || key == VK_LEFT || key == 'W' || key == 'A') exitPromptMove(-1);
    else if (key == VK_DOWN || key == VK_RIGHT || key == 'S' || key == 'D') exitPromptMove(1);
    else if ((key == VK_RETURN || key == VK_SPACE) && !repeated) exitPromptAccept();
    else if (key == VK_ESCAPE || key == VK_BACK) { exit_prompt_.reset(); prompt_kind_ = PromptKind::exit; InvalidateRect(hwnd_, nullptr, FALSE); }
}

void Launcher::exitPromptClick(float x, float y) {
    const auto view = renderer_.viewport();
    const exit_prompt::Layout layout{view.width, view.height};
    const int choice = layout.hit(x, y);
    if (choice >= 0) { exit_prompt_ = choice; exitPromptAccept(); }
    else if (!layout.panel().contains(x, y)) { exit_prompt_.reset(); prompt_kind_ = PromptKind::exit; InvalidateRect(hwnd_, nullptr, FALSE); }
}
