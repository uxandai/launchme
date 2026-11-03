#include <xinput.h>

int LauncherUiTest::gameOverlay(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 1800;
    auto library = std::make_unique<LibraryMessage>();
    Game game{5100, L"Overlay fixture"}; game.install_directory = LR"(C:\Fixture\Overlay)";
    library->games = {game}; app.onLibrary(std::move(library));
    app.game_sessions_.try_emplace(gameKey(game), 0).first->second.observe(GameActivity::running, 1);
    app.trackSession(gameKey(game), game, {});
    const GameWindow target{reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x1234)), {1234, 42}, gameKey(game)};
    GameOverlayMonitor::Snapshot snapshot{target, {game_fps::Status::ready, 117, 83}, 1};
    HWND foreground = target.window, focused = nullptr;
    bool valid = true, allow_show = true;
    int focuses = 0, shows = 0, game_switches = 0, suspends = 0;
    app.overlay_backend_.valid = [&](const GameWindow& value) { return valid && value == target; };
    app.overlay_backend_.foreground = [&] { return foreground; };
    app.overlay_backend_.read = [&] { return snapshot; };
    app.overlay_backend_.focus = [&](HWND value) { focused = foreground = value; ++focuses; };
    app.overlay_backend_.show = [&](OverlayWindow& overlay, const RECT&) {
        ++shows;
        SetWindowPos(overlay.handle(), nullptr, 0, 0, 1600, 900, SWP_NOACTIVATE | SWP_NOZORDER); // Remains hidden.
        if (allow_show) foreground = overlay.handle();
        return allow_show;
    };
    app.quick_resume_.find = [&](const Game&) { return std::vector<GameProcess>{target.process}; };
    app.quick_resume_.focus = [&](const auto&) { ++game_switches; return true; };
    app.quick_resume_.suspend = [&](auto&) { ++suspends; return process_freeze::Result::denied; };
    if (app.openGameOverlay(target) || app.game_overlay_ || shows) return 1801; // Opt-in.
    app.settings_.guide_overlay = true; app.settings_.guide_open_widgets = false; // The bar-first flow below.
    if (app.actionLabel(game) != L"Wróć" || app.menuRows().suspend) return 1802;
    // Keep the existing running-game action, even with Quick Resume off.
    if (!app.switchToGame(game) || game_switches != 1 || suspends) return 1803;
    PadReading pad{true, L"overlay-fixture"};
    app.controller(pad);
    app.settings_.guide_overlay = false;
    pad.buttons = XINPUT_GAMEPAD_START; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    if (app.gameOverlayOpen() || app.friends_open_) return 1804;
    app.settings_.guide_overlay = true;
    pad.buttons = pad_guide; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    if (!app.gameOverlayOpen() || shows != 1 || app.overlay_selected_ || suspends) return 1805;
    // XInput does not grant foreground permission. Guide must remain open and
    // accept the pad when Windows leaves the game as the foreground window.
    foreground = target.window;
    app.controller(pad);
    if (!app.gameOverlayOpen()) return 1831;
    app.overlayMessage(app.game_overlay_->handle(), WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(target.window));
    if (!app.gameOverlayOpen()) return 1832;
    foreground = nullptr;
    app.controller(pad);
    app.overlayMessage(app.game_overlay_->handle(), WM_ACTIVATE, WA_INACTIVE, 0);
    if (!app.gameOverlayOpen()) return 1833;
    foreground = target.window; // The rest of pad navigation also runs without foreground ownership.
    app.overlay_snapshot_ = snapshot;
    const auto captures = [&] {
        wchar_t directory[4096]{};
        const auto n = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", directory, 4096);
        return n && n < 4096 ? std::filesystem::path(directory) : userConfigDirectory();
    }();
    const auto capture = [&](const wchar_t* name) {
        return app.game_overlay_->renderer().captureForTest(captures / name, [&] { app.paint(true); });
    };
    const auto animate = [&] {
        for (int frame = 0; frame < 30 && app.gameOverlayOpen(); ++frame) app.tickGameOverlay(app.overlay_tick_at_ + 16);
    };
    const int plugins = quick_menu::store_slots;
    int widgets_index = -1;
    for (int i = 0; i < quick_menu::count(plugins, true); ++i)
        if (quick_menu::action(i, plugins, true) == quick_menu::Action::widgets) widgets_index = i;
    if (widgets_index < 0) return 1881;
    const auto selectWidgets = [&] {
        for (int i = 0; i < quick_menu::count(plugins, true) && app.overlay_selected_ != widgets_index; ++i)
            app.overlayKey(app.overlay_selected_ < widgets_index ? VK_RIGHT : VK_LEFT);
    };
    if (app.overlay_motion_.bar != 0 || app.overlay_motion_.opacity != 0) return 1849;
    app.tickGameOverlay(app.overlay_tick_at_ + 16);
    if (app.overlay_motion_.bar <= 0 || app.overlay_motion_.bar >= 1) return 1850;
    if (!capture(L"game-guide-opening.png")) return 1851;
    animate();
    if (app.overlay_motion_.bar != 1 || app.overlay_motion_.opacity != 1) return 1852;
    if (!capture(L"game-guide-bar.png")) return 1806;
    // The interactive surface also has a real transparent UpdateLayeredWindow target.
    ViewContext context; context.guide_overlay = true; context.quick_progress = 1;
    if (!app.game_overlay_->renderer().paintOverlay(context)) return 1807;
    const int scrim_alpha = app.game_overlay_->renderer().overlayAlphaForTest(800, 400);
    if (scrim_alpha < 30 || scrim_alpha > 60 || app.game_overlay_->renderer().overlayAlphaForTest(800, 899) < 200) return 1827;
    app.settings_.game_fps = true; app.settings_.game_fps_low = true;
    pad.id = L"overlay-steam-virtual"; // Options must not act on a physical -> virtual identity handoff.
    pad.buttons = XINPUT_GAMEPAD_START; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    if (app.friends_open_ || app.overlay_selected_ != 0 || app.overlay_closing_) return 1882;
    pad.buttons = XINPUT_GAMEPAD_START; app.controller(pad); // Nor on the same identity.
    pad.buttons = 0; app.controller(pad);
    app.overlayKeyboard(VK_TAB, false, false); // Ignore Steam Input's keyboard mapping too.
    if (app.friends_open_ || app.overlay_closing_) return 1883;
    // Guide -> Widgets -> A opens the drawer in the overlay, without focusing the dashboard.
    for (int i = 0; i < widgets_index; ++i) {
        pad.buttons = XINPUT_GAMEPAD_DPAD_RIGHT; app.controller(pad);
        pad.buttons = 0; app.controller(pad);
    }
    if (app.overlay_selected_ != widgets_index) return 1884;
    animate();
    if (!capture(L"game-guide-widgets-button.png")) return 1885;
    pad.buttons = XINPUT_GAMEPAD_A; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    const auto overlay_tiles = app.dashboardLayout().items;
    if (!app.friends_open_ || app.friends_list_ || app.dashboardTileCount() != 6 ||
        std::any_of(overlay_tiles.begin(), overlay_tiles.end(), [](const auto& item) { return item.id.empty() || item.id == "profile"; })) return 1808; // No profile over a game.
    if (focuses || shows != 1 || !app.gameOverlayOpen()) return 1886;
    app.couch_category_pending_ = true;
    app.focusCouchCategory(); // A virtual second pad must not close the Guide drawer.
    if (!app.couch_category_pending_ || !app.friends_open_) return 1857;
    app.couch_category_pending_ = false;
    // Options and its mapped Tab also leave an already-open drawer untouched.
    pad.buttons = XINPUT_GAMEPAD_START; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    app.overlayKeyboard(VK_TAB, false, false);
    if (!app.friends_open_ || app.overlay_closing_) return 1848;
    app.tickGameOverlay(app.overlay_tick_at_ + 16);
    if (app.overlay_motion_.drawer <= 0 || app.overlay_motion_.drawer >= 1) return 1853;
    animate();
    if (app.overlay_motion_.drawer != 1 || app.overlay_motion_.bar != 0) return 1854;
    app.overlayKey(VK_DOWN); app.overlayKey(VK_DOWN); // FPS below the four built-ins.
    if (app.dashboard_tile_ != 5) return 1809;
    // A click on FPS (CPU, GPU) opens the widget's options, as a power menu does: the master switch on top, then each line and the look.
    app.settings_.hud_pins.clear(); app.settings_dirty_ = false; app.settings_.hud_enabled = true;
    app.overlayKey(VK_RETURN);
    if (!app.gameOverlayOpen() || !app.powerMenuOpen() || !app.hud_menu_ || app.settings_.hud_pins.size() != 0) return 1810;
    using HudAction = Launcher::HudAction;
    const auto hudRow = [&](HudAction action, const char* key = "") {
        for (size_t i = 0; i < app.hud_menu_rows_.size(); ++i)
            if (app.hud_menu_rows_[i].action == action && app.hud_menu_rows_[i].key == key) return static_cast<int>(i);
        return -1;
    };
    const auto hudPick = [&](HudAction action, const char* key = "") {
        const int row = hudRow(action, key);
        if (row < 0) return false;
        app.power_menu_ = row; app.overlayKey(VK_RETURN);
        return true;
    };
    if (app.hud_menu_rows_.size() != 12 || hudRow(HudAction::master) != 0 || hudRow(HudAction::pin, "fps") != 1 ||
        app.hud_menu_rows_.back().action != HudAction::close || !app.hud_menu_rows_.back().gap) return 9860;
    if (!capture(L"game-guide-hud-options.png")) return 1811;
    if (!hudPick(HudAction::pin, "fps") || !hudPick(HudAction::pin, "cpu")) return 9861; // The dialog stays open.
    if (!app.powerMenuOpen() || !app.settings_dirty_ || app.settings_.hud_pins != std::vector<std::string>{"fps", "cpu"} ||
        app.hud_menu_rows_[1].toggle != 1 || app.hud_menu_rows_[2].toggle != 1 || app.hud_menu_rows_[3].toggle != 0) return 9875;
    if (!hudPick(HudAction::layout) || app.settings_.hud_layout != 1) return 9862; // A strip.
    if (!hudPick(HudAction::colors) || !app.settings_.hud_label_colors) return 9863;
    if (!hudPick(HudAction::size) || app.settings_.hud_size != 90) return 9864; // 100 -> 90.
    for (int i = 0; i < 4; ++i) hudPick(HudAction::size); // 80, 70, 60, 50.
    if (app.settings_.hud_size != 50) return 9865;
    hudPick(HudAction::size); // ... and round to the largest again.
    if (app.settings_.hud_size != 100) return 9866;
    if (!hudPick(HudAction::graph) || !app.settings_.fps_graph) return 9867;
    // Left / Right: a switch off and on, a value one step; the dialog scrolls past its eight rows.
    app.power_menu_ = hudRow(HudAction::colors); app.overlayKey(VK_LEFT);
    if (app.settings_.hud_label_colors) return 9880;
    app.overlayKey(VK_RIGHT);
    if (!app.settings_.hud_label_colors) return 9881;
    app.power_menu_ = hudRow(HudAction::size); app.overlayKey(VK_LEFT); app.overlayKey(VK_LEFT);
    if (app.settings_.hud_size != 80) return 9882;
    app.overlayKey(VK_RIGHT); app.overlayKey(VK_RIGHT); app.overlayKey(VK_RIGHT);
    if (app.settings_.hud_size != 100) return 9883; // Not past the largest.
    app.settings_.fps_limiter = false;
    app.power_menu_ = hudRow(HudAction::limiter); app.overlayKey(VK_RIGHT);
    if (!app.settings_.fps_limiter || hudRow(HudAction::limit) < 0 || hudRow(HudAction::mode) < 0) return 9884; // Its limit and mode appear.
    app.power_menu_ = hudRow(HudAction::limit);
    for (int i = 0; i < 4; ++i) app.overlayKey(VK_RIGHT); // Bez limitu, 30, 40, 45, 60.
    const int limited = app.overlay_snapshot_.target ? app.gameFpsLimit(app.overlay_snapshot_.target->key) : app.settings_.fps_limit;
    if (limited != 60) return 9885;
    app.overlayKey(VK_RIGHT); app.overlayKey(VK_RIGHT);
    if ((app.overlay_snapshot_.target ? app.gameFpsLimit(app.overlay_snapshot_.target->key) : app.settings_.fps_limit) != 90) return 9886;
    app.overlayKey(VK_LEFT);
    if ((app.overlay_snapshot_.target ? app.gameFpsLimit(app.overlay_snapshot_.target->key) : app.settings_.fps_limit) != 72) return 9887;
    app.power_menu_ = hudRow(HudAction::mode); app.overlayKey(VK_RIGHT);
    if (app.settings_.fps_limit_mode != 1) return 9888;
    app.overlayKey(VK_LEFT);
    if (app.settings_.fps_limit_mode != 0) return 9889;
    if (app.hud_menu_first_ == 0 || !capture(L"game-guide-hud-options-scrolled.png")) return 9890; // The window followed the row.
    app.settings_.fps_limiter = false; app.settings_.fps_game_limits.clear(); app.settings_.fps_limit = 0; app.buildHudMenu();
    // The master switch hides every row below it and the widget altogether; the lines are remembered.
    if (!hudPick(HudAction::master) || app.settings_.hud_enabled || app.hud_menu_rows_.size() != 3 ||
        app.settings_.hud_pins != std::vector<std::string>{"fps", "cpu"} || *app.power_menu_ != 0) return 9876;
    if (!capture(L"game-guide-hud-options-off.png")) return 9877;
    if (!hudPick(HudAction::master) || !app.settings_.hud_enabled || app.hud_menu_rows_.size() != 12) return 9878;
    app.settings_.hud_pins.clear(); app.settings_.hud_layout = 0; app.settings_.hud_label_colors = false; app.settings_.fps_graph = false;
    if (!hudPick(HudAction::close)) return 9879;
    if (!app.gameOverlayOpen() || app.powerMenuOpen() || app.hud_menu_) return 9868;
    app.toggleHudPin("fps"); // The dot on the tile.
    if (!capture(L"game-guide-widgets.png")) return 9869; // With the pin's dot.
    app.settings_.fps_graph = true; // The curve in the tile: a steady 8 ms with one hitch.
    app.overlay_snapshot_.fps.graph.assign(96, 8.3f);
    for (int i = 60; i < 64; ++i) app.overlay_snapshot_.fps.graph[i] = 52.0f;
    app.invalidateGameOverlay();
    if (!capture(L"game-guide-fps-graph.png")) return 2003;
    app.settings_.fps_graph = false; app.overlay_snapshot_.fps.graph.clear();
    app.settings_.hud_layout = 0; app.settings_.hud_label_colors = false; app.settings_.hud_size = 100;
    app.toggleHudPin("fps"); // Let go again, as the old click did.
    if (!app.gameOverlayOpen() || !app.settings_.hud_pins.empty()) return 1990;
    app.overlay_snapshot_.fps = {game_fps::Status::denied};
    if (!capture(L"game-guide-fps-denied.png")) return 1834;
    app.overlay_snapshot_ = snapshot;
    // The same widget editor in Guide, including key-up, pad holds and mouse capture.
    app.dashboard_tile_ = 0;
    app.overlayKey(VK_UP); app.overlayKey(VK_RETURN); app.overlayKey(VK_DOWN);
    if (!app.dashboardEditing() || app.dashboard_tile_ != 0) return 1870;
    const HWND overlay_hwnd = app.game_overlay_->handle();
    app.overlayMessage(overlay_hwnd, WM_KEYDOWN, VK_RETURN, 0);
    app.overlayMessage(overlay_hwnd, WM_KEYUP, VK_RETURN, 0);
    if (app.dashboardLayout().items.front().size != widgets::Size::wide) return 1871;
    pad.buttons = XINPUT_GAMEPAD_A; app.controller(pad); app.dashboard_pressed_at_ -= 500;
    pad.buttons |= XINPUT_GAMEPAD_DPAD_DOWN; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    if (app.dashboardLayout().items.front().id == "steam" || app.dashboard_grabbed_) return 1872;
    const auto widget_rect = app.dashboardLayout().tile(0, 1600 - friends_layout::drawer_width, app.dashboard_scroll_);
    const auto mouse = MAKELPARAM(static_cast<int>((widget_rect.left + widget_rect.right) / 2), static_cast<int>((widget_rect.top + widget_rect.bottom) / 2));
    app.overlayMessage(overlay_hwnd, WM_LBUTTONDOWN, 0, mouse);
    app.overlayMessage(overlay_hwnd, WM_CAPTURECHANGED, 0, 0);
    const auto before_cancelled_release = app.settings_.widget_layout;
    app.overlayMessage(overlay_hwnd, WM_LBUTTONUP, 0, mouse);
    if (app.settings_.widget_layout != before_cancelled_release) return 1975;
    app.overlayMessage(overlay_hwnd, WM_LBUTTONDOWN, 0, mouse);
    app.overlayMessage(overlay_hwnd, WM_LBUTTONUP, 0, mouse);
    const auto steam = std::find_if(app.settings_.widget_layout.begin(), app.settings_.widget_layout.end(), [](const auto& p) { return p.id == "steam"; });
    if (steam == app.settings_.widget_layout.end() || steam->size != widgets::Size::icon) return 1873;
    app.overlayKey(VK_ESCAPE);
    if (!app.friends_open_ || app.dashboardEditing() || !app.gameOverlayOpen()) return 1874;
    app.settings_.widget_layout.clear(); app.settings_dirty_ = true; app.saveSettings(); app.dashboard_scroll_ = 0;
    app.overlayKey(VK_ESCAPE);
    if (app.friends_open_ || !app.gameOverlayOpen() || app.overlay_selected_ != widgets_index) return 1812;
    animate();
    app.overlayKey(VK_ESCAPE);
    if (!app.gameOverlayOpen() || !app.overlay_closing_) return 1855;
    app.tickGameOverlay(app.overlay_tick_at_ + 16);
    if (app.overlay_motion_.opacity <= 0 || app.overlay_motion_.opacity >= 1) return 1856;
    animate();
    if (app.gameOverlayOpen() || focused != target.window || focuses != 1) return 1813;
    const auto overlay_windows = [] {
        int count = 0;
        EnumThreadWindows(GetCurrentThreadId(), [](HWND handle, LPARAM value) -> BOOL {
            wchar_t name[80]{}; GetClassNameW(handle, name, 80);
            if (std::wstring_view(name) == L"LaunchmeGameOverlay") ++*reinterpret_cast<int*>(value);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&count));
        return count;
    };
    const int windows_before = overlay_windows();
    app.next_overlay_poll_ = 0; app.pollGameOverlay();
    if (overlay_windows() != windows_before || IsWindowVisible(app.game_overlay_->handle())) return 1835;
    // Reopen, select Home. The only action is to activate the dashboard: no new game.
    if (!app.openGameOverlay(target)) return 1814;
    app.overlayKey(VK_RETURN);
    if (app.gameOverlayOpen() || focused != window || app.screen_ != ScreenMode::shelves ||
        app.actionLabel(game) != L"Wróć" || suspends || game_switches != 1) return 1815;
    if (!app.openGameOverlay(target)) return 1816;
    animate();
    const float widgets_x = quick_menu::center(1600, widgets_index, plugins, true);
    app.overlayClick(widgets_x, quick_menu::centerY(900, 1, true)); animate();
    if (!app.friends_open_ || app.overlay_selected_ != widgets_index || focused != window) return 1887;
    const auto tile = friends_layout::dashboard::tile(1600 - friends_layout::drawer_width, 4);
    app.overlayClick((tile.left + tile.right) * 0.5f, (tile.top + tile.bottom) * 0.5f);
    if (!app.hud_menu_ || !app.powerMenuOpen() || !app.friends_open_) return 1817; // The mouse opens the options too...
    {
        const auto options = app.powerMenuGeometryFor(1600, 900);
        const auto row = options.row(1);
        app.overlayClick((row.left + row.right) * 0.5f, (row.top + row.bottom) * 0.5f); // ...and a click on the FPS row pins it.
    }
    if (app.settings_.hud_pins != std::vector<std::string>{"fps"} || !app.powerMenuOpen()) return 9873;
    app.closePowerMenu();
    app.settings_.hud_pins.clear();
    const int before = focuses;
    foreground = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x5678));
    app.overlayMessage(app.game_overlay_->handle(), WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(foreground));
    if (app.gameOverlayOpen() || focuses != before) return 1818; // Alt+Tab to another app never steals focus back.
    allow_show = false;
    if (app.openGameOverlay(target) || app.gameOverlayOpen()) return 1819;
    allow_show = true;
    if (!app.openGameOverlay(target)) return 1820;
    valid = false; app.closeGameOverlay(true);
    if (focuses != before || app.gameOverlayOpen()) return 1821; // Exited/reused process identity.
    valid = true;
    if (!app.openGameOverlay(target)) return 1822;
    app.settings_.guide_overlay = false; app.configureGameOverlay();
    if (app.gameOverlayOpen() || focuses != before + 1) return 1823;

    // A newly discovered window gets the same running session immediately;
    // exiting through Guide still opens the existing frozen-games prompt.
    app.game_sessions_.clear(); app.settings_.guide_overlay = true;
    if (!app.openGameOverlay(target) || app.actionLabel(game) != L"Wróć") return 1828;
    app.game_sessions_.at(gameKey(game)).freeze({target.process});
    if (!app.requestClose() || app.gameOverlayOpen() || !app.exitPromptOpen() || focused != window) return 1829;
    app.exit_prompt_.reset(); app.game_sessions_.clear();

    // X closes the game behind Guide, not another running game selected in the
    // hidden dashboard. Opening/cancelling/repeating the dialog never terminates.
    const Game other{5200, L"Other running game"};
    app.game_sessions_.try_emplace(gameKey(game), 0).first->second.observe(GameActivity::running, 1);
    app.trackSession(gameKey(game), game, {target.process});
    app.game_sessions_.try_emplace(gameKey(other), 0).first->second.observe(GameActivity::running, 1);
    app.trackSession(gameKey(other), other, {{5678, 51}});
    app.cards_ = {GameCard{other}}; app.selected_ = 0;
    app.quick_resume_.find = [&](const Game& value) {
        return value.app_id == game.app_id ? std::vector<GameProcess>{target.process} : std::vector<GameProcess>{{5678, 51}};
    };
    std::vector<uint32_t> terminated;
    bool deny_close = true;
    app.quick_resume_.terminate = [&](const auto& processes) {
        for (const auto& process : processes) terminated.push_back(process.pid);
        return deny_close ? process_freeze::Result::denied : process_freeze::Result::ok;
    };
    if (!app.openGameOverlay(target)) return 1840;
    animate();
    const bool saved_quick_resume = app.settings_.quick_resume;
    app.settings_.quick_resume = false;
    const auto closeMenu = [&] { return app.power_menu_ && app.hud_menu_ && app.hud_menu_kind_ == Launcher::MenuKind::game_close; };
    app.overlayKey(VK_RIGHT); app.overlayKey(VK_RETURN);
    // A menu like Zasilanie: Zamknij grę (danger) and Anuluj, Anuluj chosen first. Without quick resume no Wstrzymaj.
    if (!closeMenu() || app.hud_menu_rows_.size() != 2 || *app.power_menu_ != 1 || !terminated.empty()) return 1841;
    if (!app.hud_menu_rows_[0].danger || app.hud_menu_rows_[0].action != Launcher::HudAction::close_game) return 1848;
    if (!capture(L"game-guide-close-confirm.png")) return 1842;
    app.overlayKey(VK_RETURN, true);
    if (!closeMenu() || !terminated.empty()) return 1843;
    app.overlayKey(VK_RETURN); // Default Cancel.
    if (app.power_menu_ || !app.gameOverlayOpen() || !terminated.empty()) return 1844;
    app.overlayAccept(); app.overlayKey(VK_ESCAPE);
    if (app.power_menu_ || !app.gameOverlayOpen() || !terminated.empty()) return 1845;
    // With quick resume on, the first row suspends the game and returns to the launcher; a denial keeps Guide open.
    app.settings_.quick_resume = true;
    std::vector<uint32_t> suspended;
    bool deny_suspend = true;
    app.quick_resume_.suspend = [&](std::vector<GameProcess>& processes) {
        for (const auto& process : processes) suspended.push_back(process.pid);
        return deny_suspend ? process_freeze::Result::denied : process_freeze::Result::ok;
    };
    app.overlayAccept();
    if (!closeMenu() || app.hud_menu_rows_.size() != 3 || app.hud_menu_rows_[0].action != Launcher::HudAction::suspend_game) return 1849;
    app.overlayKey(VK_UP); app.overlayKey(VK_UP); app.overlayKey(VK_RETURN);
    if (app.power_menu_ || !app.gameOverlayOpen() || suspended != std::vector<uint32_t>{1234} || app.notifications_.empty() ||
        app.game_sessions_.at(gameKey(game)).frozen()) return 1850;
    app.settings_.quick_resume = false; app.notifications_.clear(); // Back to two rows for the close flow below.
    app.overlayAccept(); app.overlayKey(VK_UP); app.overlayKey(VK_RETURN);
    if (!app.gameOverlayOpen() || !app.game_sessions_.contains(gameKey(game)) ||
        terminated != std::vector<uint32_t>{1234} || app.notifications_.empty()) return 1846;
    deny_close = false;
    app.overlayAccept(); app.overlayKey(VK_UP); app.overlayKey(VK_RETURN);
    if (app.gameOverlayOpen() || app.game_sessions_.contains(gameKey(game)) || !app.game_sessions_.contains(gameKey(other)) ||
        terminated != std::vector<uint32_t>{1234, 1234} || focused != window) return 1847;
    app.settings_.quick_resume = saved_quick_resume;

    // Controllers, Sound and Settings from Guide: leaving them returns to the game.
    for (const int index : {5, 3, 2}) { // Settings, Sound, Controllers.
        app.game_sessions_.try_emplace(gameKey(game), 0).first->second.observe(GameActivity::running, 1);
        app.trackSession(gameKey(game), game, {target.process});
        app.overlayKey(VK_HOME); // Whatever was open before.
        if (!app.openGameOverlay(target)) return 9873;
        animate();
        app.overlay_selected_ = index;
        focused = window;
        app.overlayAccept();
        if (app.gameOverlayOpen() || !app.overlay_return_ || !(app.settingsOpen() || app.devicesOpen())) return 9874;
        app.tick();
        if (!app.overlay_return_ || focused != window) return 9875; // Still in the screen: no return yet.
        app.back();
        app.tick();
        if (app.overlay_return_ || focused != target.window) return 9876;
    }

    // Options never opens Guide: normal presses, holding, a virtual identity handoff,
    // and reconnecting with the button down. No window/focus side effects.
    foreground = target.window;
    const int before_options_shows = shows, before_options_focuses = focuses;
    pad.buttons = 0; app.controller(pad);
    pad.buttons = XINPUT_GAMEPAD_START; app.controller(pad);
    app.pad_start_.update(true, GetTickCount64() + HoldGesture::threshold_ms + 1);
    app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    if (app.gameOverlayOpen() || app.friends_open_ || app.power_menu_ || app.devicesOpen()) return 1860;
    pad.id = L"overlay-direct-virtual";
    pad.buttons = XINPUT_GAMEPAD_START; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    app.pad_last_seen_ = GetTickCount64() - 500;
    app.controller(PadReading{});
    pad.buttons = XINPUT_GAMEPAD_START; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    if (app.gameOverlayOpen() || app.friends_open_ || shows != before_options_shows || focuses != before_options_focuses) return 1864;
    // Guide still opens the bar; Guide again closes everything directly from Widgets.
    pad.buttons = pad_guide; app.controller(pad);
    pad.buttons = 0; app.controller(pad); animate();
    if (!app.gameOverlayOpen() || app.friends_open_ || app.overlay_motion_.bar != 1) return 1865;
    selectWidgets(); app.overlayKey(VK_RETURN); animate();
    pad.buttons = pad_guide; app.controller(pad);
    pad.buttons = 0; app.controller(pad); animate();
    if (app.gameOverlayOpen() || app.friends_open_ || focused != target.window) return 1866;

    // Shift+Tab belongs to Guide only over a recognized game or its overlay;
    // desktop/launcher backwards-Tab navigation must remain available.
    app.overlay_snapshot_ = snapshot;
    app.settings_.guide_overlay = false;
    app.guideKeyboardHotkey();
    if (app.guideHotkeyWanted() || app.gameOverlayOpen()) return 1870;
    app.settings_.guide_overlay = true;
    foreground = window;
    app.guideKeyboardHotkey();
    if (app.guideHotkeyWanted() || app.gameOverlayOpen()) return 1871;
    foreground = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x5678));
    app.guideKeyboardHotkey();
    if (app.guideHotkeyWanted() || app.gameOverlayOpen()) return 1872;
    foreground = target.window;
    if (!app.guideHotkeyWanted()) return 1873;
    app.guideKeyboardHotkey(); animate();
    if (!app.gameOverlayOpen() || app.friends_open_) return 1874;
    app.overlayKeyboard(VK_TAB, false, false); animate();
    if (app.friends_open_) return 1888;
    selectWidgets(); app.overlayKeyboard(VK_RETURN, false, false); animate();
    if (!app.friends_open_ || app.overlay_closing_) return 1875;
    app.overlayKeyboard(VK_TAB, true, true); // A held chord does not toggle repeatedly.
    if (app.overlay_closing_) return 1876;
    app.guide_hotkey_active_ = true; // Simulate successful registration, without taking a real system hotkey.
    app.overlayKeyboard(VK_TAB, false, true);
    if (app.overlay_closing_ || !app.friends_open_) return 1877; // WM_KEYDOWN is not a second toggle.
    app.guideKeyboardHotkey(); animate(); // The WM_HOTKEY action closes both panes.
    app.guide_hotkey_active_ = false;
    if (app.gameOverlayOpen() || focused != target.window) return 1878;
    if (!app.openGameOverlay(target)) return 1879;
    selectWidgets(); app.overlayKey(VK_RETURN); animate();
    app.overlayKeyboard(VK_TAB, false, true); animate(); // Focused-window fallback also closes Widgets.
    if (app.gameOverlayOpen()) return 1880;
    // By default Guide in a game opens on the widgets, without a second click on the bar.
    app.settings_.guide_open_widgets = true;
    if (!app.openGameOverlay(target, true) || !app.friends_open_) return 1991;
    app.dismissGameOverlay(); animate();
    app.settings_.guide_open_widgets = false;
    if (app.gameOverlayOpen()) return 1992;

    // Ustawienia → System → FPS i limiter: the widget, its graph, the limiter and the limits of single games.
    {
        using settings_ui::Id;
        app.settings_.game_fps = true; app.settings_.fps_limiter = true; app.settings_.fps_limit = 60; app.settings_.fps_graph = true;
        app.overlay_snapshot_ = snapshot;
        app.screen_ = ScreenMode::settings;
        app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::system));
        app.buildSettingsRows();
        if (!app.revealSetting(Id::fps_limiter) || !app.settings_state_.at(settings_ui::Page::fps_limiter)) return 1994;
        const auto find = [&](Id id) { return std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const auto& row) { return row.id == id; }); };
        for (const auto id : {Id::game_fps, Id::fps_graph, Id::fps_limiter, Id::fps_limit, Id::fps_limit_precise, Id::fps_limiter_status, Id::fps_game_limit})
            if (find(id) == app.settings_rows_.end()) return 1995;
        // What the widget over the game shows: a switch for each line, and the limiter's mode and badges.
        if (find(Id::fps_limit_mode) == app.settings_rows_.end() || find(Id::fps_badges) == app.settings_rows_.end() ||
            std::count_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) { return row.id == Id::hud_pin; }) != 5) return 2004;
        for (const auto id : {Id::hud_enabled, Id::hud_layout, Id::hud_label_colors, Id::hud_size, Id::cpu_temp_source})
            if (find(id) == app.settings_rows_.end()) return 9870;
        app.settings_state_.choice = 1; app.settingsChoose(*find(Id::hud_layout));
        app.settingsToggle(*find(Id::hud_label_colors));
        if (!app.revealSetting(Id::hud_size)) return 9871;
        app.settingsSetLevel(0.0f);
        if (app.settings_.hud_size != 50 || app.settings_.hud_layout != 1 || !app.settings_.hud_label_colors) return 9872;
        app.settings_.hud_layout = 0; app.settings_.hud_label_colors = false; app.settings_.hud_size = 100;
        app.settings_.hud_pins.clear();
        const auto ram = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) { return row.id == Id::hud_pin && row.key == "ram"; });
        app.settingsToggle(*ram);
        if (app.settings_.hud_pins != std::vector<std::string>{"ram"}) return 2005;
        app.settingsToggle(*std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) { return row.id == Id::hud_pin && row.key == "ram"; }));
        if (!app.settings_.hud_pins.empty()) return 2006;
        app.settings_state_.choice = 1; app.settingsChoose(*find(Id::fps_limit_mode));
        if (app.settings_.fps_limit_mode != 1) return 2007;
        app.settings_.fps_limit_mode = 0;
        auto own = find(Id::fps_game_limit);
        if (own->key != target.key || own->choice != 0 || own->choices.size() != 12 || find(Id::fps_limit)->value != L"60 FPS") return 1996;
        app.settings_state_.choice = 4; // "45 FPS": like global, none, 30, 40, 45.
        app.settingsChoose(*own);
        if (own = find(Id::fps_game_limit); app.settings_.fps_game_limits[target.key] != 45 || own->value != L"45 FPS") return 1997;
        if (app.gameFpsLimit(target.key) != 45) return 1998;
        app.settings_state_.choice = 1; app.settingsChoose(*find(Id::fps_game_limit)); // None for this game.
        if (app.gameFpsLimit(target.key) != 0 || app.settings_.fps_game_limits.at(target.key) != 0) return 1999;
        app.settings_state_.choice = 0; app.settingsChoose(*find(Id::fps_game_limit)); // Like the global one again.
        if (app.gameFpsLimit(target.key) != 60 || !app.settings_.fps_game_limits.empty()) return 2000;
        app.notifications_.clear();
        if (!app.renderer_.captureForTest(captures / L"fps-limiter-settings.png", [&] { app.paint(); })) return 2001;
        app.settings_.fps_limiter = false;
        if (app.gameFpsLimit(target.key) != 0) return 2002; // Off: nothing is limited.
        app.settings_.game_fps = false; app.settings_.fps_graph = false;
        app.closeSettingsPage();
        app.screen_ = ScreenMode::shelves;
    }

    // Options remains a local shortcut in Launchme's main window, short and held.
    foreground = window;
    app.cards_.clear(); app.buildShelves(); app.showCategory(0);
    pad.buttons = 0; app.controller(pad);
    pad.buttons = XINPUT_GAMEPAD_START; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    if (!app.friends_open_ || app.gameOverlayOpen()) return 1889;
    pad.buttons = XINPUT_GAMEPAD_START; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    if (app.friends_open_) return 1890;
    // The main quick bar exposes Widgets as well.
    app.openQuick();
    int local_widgets = -1;
    for (int i = 0; i < quick_menu::count(plugins); ++i)
        if (quick_menu::action(i, plugins) == quick_menu::Action::widgets) local_widgets = i;
    if (local_widgets < 0) return 1891;
    app.quick_selected_ = local_widgets;
    app.quickAccept();
    if (!app.friends_open_ || app.quickOpen() || app.power_menu_ || app.gameOverlayOpen()) return 1892;
    app.closeFriends();
    app.pad_start_.update(true, GetTickCount64() - HoldGesture::threshold_ms - 1);
    pad.buttons = XINPUT_GAMEPAD_START; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    if (!app.devicesOpen() || app.device_state_.panel != devices::Panel::audio || app.friends_open_) return 1893;
    app.back();

    // A small real layered HWND, with a refused foreground request: pixels must
    // be presented BEFORE activation is attempted, and the window stays visible.
    // No real app is activated and the test never sends synthetic user input.
    OverlayWindow native;
    bool painted = false, requested = false, painted_before_activation = false;
    if (!native.create(false, [&](HWND w, UINT message, WPARAM p, LPARAM l) -> LRESULT {
        if (message == WM_PAINT) { painted = native.renderer().paintOverlay(ViewContext{}); return 0; }
        return DefWindowProcW(w, message, p, l);
    })) return 1824;
    const bool shown = native.show({0, 0, 64, 64}, true, [&](HWND) -> BOOL {
        requested = true; painted_before_activation = painted; return FALSE;
    });
    const bool visible = IsWindowVisible(native.handle()) != FALSE;
    const int alpha = native.renderer().overlayAlphaForTest(32, 32);
    native.hide();
    if (!shown || !visible || !requested || !painted_before_activation || alpha < 30 || alpha > 60) return 1825;
    app.settings_dirty_ = false;
    return 0;
}
