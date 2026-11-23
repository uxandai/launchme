// Included after LauncherUiTest (0.3.0, issue #6). System → "Chowaj do zasobnika podczas gry"
// (src/app/tray.cpp): off by default; on, a game started here hides the window once the
// game holds the foreground, an icon (recorded, never added to the real notification area) stands in,
// the icon brings it back, Explorer's restart adds it again and the game's end shows the window.
// The game's end returns the foreground (0.4.7) unless the user chose another program meanwhile.
// System → "Zamknięcie okna chowa do zasobnika": the icon stays, a close hides the window (a balloon
// once), a game's end leaves it there, the icon's menu (recent games, the game being played, Settings)
// works, Zamknij launchme quits, and a start with the system waits in the tray.
// Then "Popraw kadr" on Tło (src/app/crop.cpp): the screen-shaped window moves over a wide
// background, is saved bound to its bytes and reset by "Wyśrodkuj"; "Zmień grafikę" keeps the pages seen
// during a visit; and the empty library view's
// message in the middle of the screen. Captures: background-crop*.png, library-empty-view.png.
#include "../src/artwork/background_focus.hpp"

int LauncherUiTest::trayAndBackgroundCrop(HWND window) {
    {
        // Started with Windows in Tryb TV: the launcher takes the front back from what logon brings forward
        // (retried, recorded here), until it has held it a few seconds; never over a game, never later.
        Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
        HWND front = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x4321)); // Explorer, say.
        int attempts = 0;
        app.foreground_window_ = [&] { return front; };
        app.take_foreground_ = [&](HWND) { ++attempts; return false; };
        ShowWindow(window, SW_SHOWNOACTIVATE);
        app.claimStartupForeground();
        const auto start = app.startup_claim_until_ - Launcher::startup_claim_ms;
        if (!app.startup_claim_until_ || attempts != 1) return 1940;
        app.pollStartupForeground(start + 100); // Not yet: one try every half second.
        if (attempts != 1) return 1941;
        app.pollStartupForeground(start + Launcher::startup_claim_retry_ms);
        if (attempts != 2) return 1942;
        // In front, then taken away before it settled: taken back.
        front = window; app.pollStartupForeground(start + 1000);
        front = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x4322)); app.pollStartupForeground(start + 2000);
        if (attempts != 3 || !app.startup_claim_until_) return 1943;
        // Held long enough: done; what comes forward later is the user's.
        front = window; app.pollStartupForeground(start + 3000); app.pollStartupForeground(start + 3000 + Launcher::startup_front_held_ms);
        if (app.startup_claim_until_) return 1944;
        front = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x4323)); app.pollStartupForeground(start + 9000);
        if (attempts != 3) return 1945;
        // Never past startup_claim_ms, and never in the desktop window.
        app.claimStartupForeground(); attempts = 0;
        app.pollStartupForeground(app.startup_claim_until_);
        if (app.startup_claim_until_ || attempts != 0) return 1946;
        app.desktop_mode_ = true; app.claimStartupForeground();
        if (app.startup_claim_until_ || attempts != 0) return 1947;
    }
    {
        Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
        app.settings_.launch_animation = false;
        std::vector<DWORD> calls;
        app.tray_call_ = [&](DWORD message, NOTIFYICONDATAW* data) {
            calls.push_back(message);
            return data && data->hWnd == window && data->uID == 1 && (message != NIM_ADD || data->uCallbackMessage == Launcher::tray_message);
        };
        HWND front = window;
        const HWND game_window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x5678));
        app.foreground_window_ = [&] { return front; };
        ShowWindow(window, SW_SHOWNOACTIVATE);
        const Game game{6400, L"Tray game"};
        const auto activity = [&](GameActivity state, uint64_t time) {
            auto message = std::make_unique<GameActivityMessage>();
            message->generation = app.game_activity_jobs_->generation.load(); message->observed_at = time;
            message->activities.emplace_back(gameKey(game), state);
            app.onGameActivity(std::move(message));
        };
        const auto start = GetTickCount64();
        app.beginGameSession(game);
        activity(GameActivity::running, start + 100);
        front = game_window;
        // Off by default: the window stays.
        if (AppSettings{}.hide_while_playing) return 1950;
        app.pollTray();
        if (app.inTray() || !calls.empty() || !IsWindowVisible(window)) return 1951;
        app.settings_.hide_while_playing = true;
        front = window; app.pollTray(); // The launcher still in front (its focus hand-off to the game).
        if (app.inTray()) return 1952;
        front = game_window; app.pollTray();
        if (!app.inTray() || calls != std::vector<DWORD>{NIM_ADD, NIM_SETVERSION} || IsWindowVisible(window)) return 1953;
        // Back from the icon (here without the foreground); behind the game again, it hides again.
        app.showFromTray();
        if (app.inTray() || calls.back() != NIM_DELETE || !IsWindowVisible(window)) return 1954;
        app.pollTray();
        if (!app.inTray() || IsWindowVisible(window)) return 1955;
        // Explorer restarted: the icon comes back.
        calls.clear(); app.taskbarCreated();
        if (!app.inTray() || calls != std::vector<DWORD>{NIM_ADD, NIM_SETVERSION}) return 1956;
        // The game ends: the window is back (and the icon gone).
        activity(GameActivity::stopped, start + 900);
        activity(GameActivity::stopped, start + 2700);
        if (!app.game_sessions_.empty() || app.inTray() || calls.back() != NIM_DELETE || !IsWindowVisible(window)) return 1957;
        // No icon, no way back: the window does not hide.
        app.tray_call_ = [&](DWORD message, NOTIFYICONDATAW*) { calls.push_back(message); return FALSE; };
        app.beginGameSession(game);
        activity(GameActivity::running, start + 3000);
        app.pollTray();
        if (app.inTray() || !IsWindowVisible(window)) return 1958;
        // Turned off while hidden: shown at once.
        app.tray_call_ = [&](DWORD message, NOTIFYICONDATAW*) { calls.push_back(message); return TRUE; };
        app.pollTray();
        if (!app.inTray()) return 1959;
        app.settings_.hide_while_playing = false; app.pollTray();
        if (app.inTray() || !IsWindowVisible(window)) return 1960;
        // The rows (System → Okno i język) and their toggles.
        app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::system)); app.buildSettingsRows();
        const auto find = [&](settings_ui::Id id) {
            return std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const auto& r) { return r.id == id; });
        };
        const auto row = find(settings_ui::Id::hide_while_playing);
        if (row == app.settings_rows_.end() || row->control != settings_ui::Control::toggle || row->checked) return 1961;
        app.settingsToggle(*row);
        if (!app.settings_.hide_while_playing) return 1962;
        const auto close_row = find(settings_ui::Id::close_to_tray);
        if (close_row == app.settings_rows_.end() || close_row->control != settings_ui::Control::toggle || close_row->checked) return 28400;
        app.settingsToggle(*close_row);
        if (!app.settings_.close_to_tray) return 28401;
        app.settings_.close_to_tray = app.settings_.hide_while_playing = false;
        app.game_sessions_.clear(); app.launched_here_.clear();
        ShowWindow(window, SW_HIDE);
    }
    {
        // Zamknięcie okna chowa do zasobnika.
        if (AppSettings{}.close_to_tray || AppSettings{}.close_to_tray_told) return 28402;
        Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
        app.settings_.launch_animation = false; app.settings_.exit_animation = true;
        std::vector<DWORD> calls;
        std::vector<std::wstring> balloons;
        app.tray_call_ = [&](DWORD message, NOTIFYICONDATAW* data) {
            calls.push_back(message);
            if (message == NIM_MODIFY && (data->uFlags & NIF_INFO)) balloons.push_back(data->szInfoTitle);
            return TRUE;
        };
        HWND front = window;
        app.foreground_window_ = [&] { return front; };
        app.take_foreground_ = [&](HWND) { front = window; return true; };
        ShowWindow(window, SW_SHOWNOACTIVATE);
        const auto code = [&]() -> int {
            // Off: no icon, and a close is a close.
            app.pollTray();
            if (app.closeToTray() || app.inTray() || !calls.empty()) return 28403;
            // On: the icon comes at once and stays with the window shown.
            app.settings_.close_to_tray = true; app.pollTray();
            if (!app.tray_icon_ || calls != std::vector<DWORD>{NIM_ADD, NIM_SETVERSION} || !IsWindowVisible(window)) return 28404;
            // The window's close hides it, with the balloon the first time only.
            if (!app.requestClose() || app.closing_ || !app.inTray() || IsWindowVisible(window) || balloons.size() != 1 ||
                !app.settings_.close_to_tray_told) return 28405;
            app.trayIconEvent(0, MAKELPARAM(NIN_SELECT, 0)); // A click on the icon.
            if (app.inTray() || !IsWindowVisible(window) || !app.tray_icon_ || calls.back() == NIM_DELETE) return 28406;
            app.requestClose();
            if (!app.inTray() || balloons.size() != 1) return 28407;
            // A game's end leaves the closed window in the tray; the tooltip and the menu name the game meanwhile.
            const Game game{6410, L"Closed tray game"};
            const auto activity = [&](GameActivity state, uint64_t time) {
                auto message = std::make_unique<GameActivityMessage>();
                message->generation = app.game_activity_jobs_->generation.load(); message->observed_at = time;
                message->activities.emplace_back(gameKey(game), state);
                app.onGameActivity(std::move(message));
            };
            const auto start = GetTickCount64();
            app.beginGameSession(game);
            activity(GameActivity::running, start + 100);
            app.tray_refresh_at_ = 0; app.pollTray();
            if (app.tray_tip_ != L"launchme · Closed tray game" || !app.inTray()) return 28408;
            const auto playing = app.trayMenu();
            if (std::none_of(playing.begin(), playing.end(), [](const auto& entry) {
                    return entry.command == tray_menu::end_game && entry.label.find(L"Closed tray game") != std::wstring::npos; })) return 28409;
            activity(GameActivity::stopped, start + 900);
            activity(GameActivity::stopped, start + 2700);
            app.pollTray();
            if (!app.game_sessions_.empty() || !app.inTray() || IsWindowVisible(window)) return 28410;
            // The menu: Pokaż launchme first, the five last played public games, the mode, Ustawienia,
            // a separator and Zamknij launchme last.
            std::vector<Game> library;
            for (int i = 0; i < 7; ++i) {
                Game played{static_cast<uint32_t>(6420 + i), L"Played " + std::to_wstring(i)};
                played.installed = true; played.last_played = 1000 + static_cast<uint64_t>(i);
                library.push_back(played);
            }
            library[6].visibility = GameVisibility::private_game; // Never in the notification area.
            library[0].name = L"Ratchet & Clank: a name much longer than the menu would ever want to show";
            library[0].last_played = 999999;
            app.grouped_library_ = library;
            const auto menu = app.trayMenu();
            if (menu.size() != 6 || menu[0].command != tray_menu::show || !menu[0].bold || menu[1].children.size() != 5 ||
                menu[2].command != tray_menu::switch_mode || menu[3].command != tray_menu::settings || !menu[4].separator() ||
                menu.back().command != tray_menu::close) return 28411;
            const auto& recent = menu[1].children;
            if (recent[0].command != tray_menu::recent_first || recent[0].label.size() != tray_menu::label_limit ||
                recent[1].label != L"Played 5" || recent[4].label != L"Played 2") return 28412;
            // A recent game starts from the menu while the window stays in the tray.
            app.trayCommand(tray_menu::recent_first + 1);
            if (!app.pending_launch_ || gameKey(app.pending_game_) != gameKey(library[5]) || !app.inTray() || IsWindowVisible(window)) return 28413;
            app.pending_launch_ = false;
            // Ustawienia: the window comes back with Settings open.
            app.trayCommand(tray_menu::settings);
            if (app.inTray() || !IsWindowVisible(window) || !app.settingsOpen()) return 28414;
            // Zamknij launchme (the icon's menu or the power menu): the next close quits, the icon goes.
            app.quit_requested_ = true;
            if (!app.requestClose() || !app.closing_ || app.inTray() || calls.back() != NIM_DELETE) return 28415;
            return 0;
        }();
        ShowWindow(window, SW_HIDE);
        if (code) return code;
    }
    {
        // Started with the system: the desktop window waits in the tray and its first show is a normal one;
        // Tryb TV takes the screen as before.
        Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
        app.settings_.close_to_tray = true; app.settings_.desktop_maximized = false;
        app.tray_call_ = [](DWORD, NOTIFYICONDATAW*) { return TRUE; };
        HWND front = nullptr;
        app.foreground_window_ = [&] { return front; };
        app.take_foreground_ = [&](HWND) { front = window; return true; };
        ShowWindow(window, SW_HIDE);
        if (app.startInTray()) return 28416; // Tryb TV.
        app.desktop_mode_ = true;
        if (!app.startInTray() || !app.inTray() || !app.tray_never_shown_ || IsWindowVisible(window)) return 28417;
        app.pollTray(); // A game's absence does not show it.
        if (!app.inTray() || IsWindowVisible(window)) return 28418;
        app.trayCommand(tray_menu::show);
        if (app.inTray() || app.tray_never_shown_ || !IsWindowVisible(window) || !app.tray_icon_) return 28419;
        // No notification area: the window's close closes.
        app.settings_.exit_animation = true;
        app.removeTrayIcon();
        app.tray_call_ = [](DWORD, NOTIFYICONDATAW*) { return FALSE; };
        if (app.closeToTray() || app.inTray() || !IsWindowVisible(window)) return 28420;
        app.desktop_mode_ = false;
        ShowWindow(window, SW_HIDE);
    }
    {
        // 0.4.7: the game ends while the window waits in the tray. Windows activated the next window in
        // line (a terminal, a browser) as the game's went away: the launcher, shown on top again, takes
        // the foreground (refused once by Windows: tried again) instead of leaving the keyboard and the
        // pad with that program. One in front since well before the end keeps the screen; the launcher
        // waits right behind it.
        Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
        app.settings_.launch_animation = false; app.settings_.hide_while_playing = true;
        app.fullscreen_ = false; // Never resized to the monitor here.
        app.tray_call_ = [](DWORD, NOTIFYICONDATAW*) { return TRUE; };
        const auto helper = [](const wchar_t* title) {
            return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"LaunchmeUiRegression", title, WS_POPUP, -4000, -4000, 64, 64,
                                   nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        };
        const HWND game_window = helper(L"Game"), own = helper(L"Sign-in page");
        // Other programs' windows: neither a store client nor the desktop.
        const HWND terminal = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x6666));
        const HWND browser = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x7777));
        const auto code = [&]() -> int {
            if (!game_window || !own) return 1982;
            ShowWindow(own, SW_SHOWNOACTIVATE);
            HWND front = window;
            app.foreground_window_ = [&] { return front; };
            int attempts = 0;
            bool grant = false;
            app.take_foreground_ = [&](HWND target) {
                ++attempts;
                if (target == window && grant) front = window;
                return front == window;
            };
            const Game game{6401, L"Focus game"};
            const auto activity = [&](GameActivity state, uint64_t time) {
                auto message = std::make_unique<GameActivityMessage>();
                message->generation = app.game_activity_jobs_->generation.load(); message->observed_at = time;
                message->activities.emplace_back(gameKey(game), state);
                app.onGameActivity(std::move(message));
            };
            // Started here and confirmed; the game in front hides the launcher.
            const auto play = [&](uint64_t at) {
                front = window; ShowWindow(window, SW_SHOWNOACTIVATE);
                app.beginGameSession(game);
                activity(GameActivity::running, at);
                front = game_window; app.foregroundChanged(game_window);
                app.pollTray();
                return app.inTray() && !IsWindowVisible(window);
            };
            const auto end = [&](uint64_t at) {
                activity(GameActivity::stopped, at); activity(GameActivity::stopped, at + 1800);
                return app.game_sessions_.empty() && !app.inTray() && IsWindowVisible(window);
            };
            const auto start = GetTickCount64();
            if (!play(start + 100)) return 1983;
            front = terminal; app.foregroundChanged(terminal); // The next window in line, as the game ended.
            if (!end(start + 900) || attempts != 1 || front == window || !app.return_focus_until_) return 1984;
            app.pollReturnFocus(); // Not before the retry interval.
            if (attempts != 1) return 1985;
            grant = true; app.return_focus_next_ = 0; app.pollReturnFocus();
            if (attempts != 2 || front != window || app.return_focus_until_) return 1986;
            // Refused again, then the user goes to another program: no more attempts.
            grant = false;
            if (!play(start + 5000)) return 1987;
            front = terminal; app.foregroundChanged(terminal);
            if (!end(start + 5800) || attempts != 3 || !app.return_focus_until_) return 1988;
            front = browser; app.return_focus_next_ = 0; app.pollReturnFocus();
            if (attempts != 3 || app.return_focus_until_) return 1989;
            // In the browser for a minute while the game ran: it keeps the screen.
            if (!play(start + 10000)) return 1990;
            front = browser; app.foregroundChanged(browser);
            const uint64_t ended = start + 11800, minute = Launcher::game_exit_window_ms + 60000;
            app.foreground_seen_at_ = ended > minute ? ended - minute : 0;
            if (!end(start + 10000) || attempts != 3 || front != browser || app.return_focus_until_) return 1991;
            // The launcher's own window in front (never seen by the hook): kept too, the launcher right behind it.
            if (!play(start + 15000)) return 1992;
            front = own;
            if (!end(start + 15000) || attempts != 3 || front != own) return 1993;
            bool behind = false;
            for (HWND next = GetWindow(own, GW_HWNDNEXT); next && !behind; next = GetWindow(next, GW_HWNDNEXT)) behind = next == window;
            if (!behind) return 1994;
            return 0;
        }();
        app.game_sessions_.clear(); app.launched_here_.clear(); app.removeTrayIcon();
        if (game_window) DestroyWindow(game_window);
        if (own) DestroyWindow(own);
        ShowWindow(window, SW_HIDE);
        if (code) return code;
    }
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    app.settings_.launch_animation = false;
    if (!app.renderer_.initialize(window)) return 1963;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) { app.paint(); return true; }
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.tick(); app.paint(); Sleep(5); } };
    {
        // System → Okno i język: the two tray rows under Uruchamiaj z systemem Windows.
        app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::system)); app.buildSettingsRows();
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::close_to_tray; });
        if (row == app.settings_rows_.end()) return 28421;
        app.settings_state_.options = true;
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin()); app.ensureSettingVisible();
        settle();
        if (!capture(L"settings-system-tray.png")) return 28422;
        app.back();
    }
    {
        // System → Funkcje: the client the Widżety's Discord tile opens.
        app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::system)); app.buildSettingsRows();
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::discord_program; });
        if (row == app.settings_rows_.end() || std::any_of(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::discord_program_reset; })) return 28423;
        app.settings_state_.options = true;
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin()); app.ensureSettingVisible();
        settle();
        if (!capture(L"settings-system-discord.png")) return 28424;
        app.back();
        // Chosen: its path, and the way back to Discord's own client.
        app.settings_.discord_program = std::filesystem::temp_directory_path() / L"Vesktop" / L"Vesktop.exe";
        app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::system)); app.buildSettingsRows();
        const auto reset = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::discord_program_reset; });
        if (reset == app.settings_rows_.end()) return 28425;
        app.settings_state_.options = true;
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(reset - app.settings_rows_.begin()); app.ensureSettingVisible();
        app.settingsActivate();
        if (!app.settings_.discord_program.empty()) return 28426;
        app.back();
    }
    {
        // Wygląd → Okno desktopowe → Panel boczny (desktop window): Sklep, Platformy, Wydawcy i developerzy.
        app.desktop_mode_ = true;
        app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::appearance)); app.buildSettingsRows();
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::sidebar_store; });
        if (row == app.settings_rows_.end()) return 28427;
        app.settings_state_.options = true;
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin()); app.ensureSettingVisible();
        app.settingsActivate();
        if (app.settings_.desktop_sidebar_store) return 28428;
        settle();
        if (!capture(L"settings-desktop-sidebar.png")) return 28429;
        app.settingsActivate();
        if (!app.settings_.desktop_sidebar_store) return 28430;
        app.back(); app.desktop_mode_ = false;
    }
    // A wide background (3.1:1, as SteamGridDB's heroes) of a game from another store.
    Game game; game.kind = GameKind::store; game.source = GameSource::epic; game.source_id = "wide"; game.name = L"Wide Background";
    game.installed = true;
    const auto cache = coverCacheDirectory();
    const auto hero_file = gameArtworkPath(game, ArtworkKind::hero, cache);
    std::filesystem::create_directories(hero_file.parent_path());
    const std::vector<uint8_t> hero(pngFixture(70, 744, 240));
    if (hero.empty() || !atomic_file::write(hero_file, hero)) return 1964;
    app.store_library_ = {game}; app.buildShelves();
    app.openCropEditor(game, true);
    if (!app.cropOpen() || !app.crop_state_.background || app.crop_state_.aspect < 1.0f) return 1965;
    auto message = std::make_unique<CropMessage>(); message->generation = app.crop_token_->load();
    message->image = hero_file; message->bytes = hero; message->width = 744; message->height = 240;
    app.onCrop(std::move(message));
    if (app.crop_state_.loading || !app.crop_state_.adjustable() || std::abs(app.crop_state_.focus.x - 0.5f) > 0.001f) return 1966;
    // The window keeps the screen's shape and fits the picture's height.
    const auto [window_width, window_height] = app.crop_state_.window();
    if (std::abs(window_height - 240) > 0.5f || std::abs(window_width / window_height - app.crop_state_.aspect) > 0.01f) return 1967;
    settle();
    if (!capture(L"background-crop.png")) return 1968;
    for (int i = 0; i < 80; ++i) app.cropMove(-1, 0); // To the left edge, never past it.
    const float left = window_width / (2 * 744.0f);
    if (std::abs(app.crop_state_.focus.x - left) > 0.002f || std::abs(app.crop_state_.focus.y - 0.5f) > 0.001f) return 1969;
    app.paint();
    if (!capture(L"background-crop-left.png")) return 1970;
    app.crop_state_.control = 1; app.cropAccept(); // Zapisz kadr.
    const auto saved = backgroundFocus(game, hero, cache);
    if (app.cropOpen() || !saved || std::abs(saved->x - left) > 0.002f) return 1971;
    const auto* art = app.art_.peek(gameKey(game));
    if (!art || std::abs(art->hero_focus.x - left) > 0.002f) return 1972;
    // "Wyśrodkuj" and save: the crop goes, the picture is centred again.
    app.openCropEditor(game, true);
    message = std::make_unique<CropMessage>(); message->generation = app.crop_token_->load();
    message->image = hero_file; message->bytes = hero; message->width = 744; message->height = 240; message->focus = *saved;
    app.onCrop(std::move(message));
    app.crop_state_.control = 0; app.cropAccept(); // Wyśrodkuj.
    if (std::abs(app.crop_state_.focus.x - 0.5f) > 0.001f || !app.crop_state_.automatic_selected) return 1973;
    app.crop_state_.control = 1; app.cropAccept();
    if (app.cropOpen() || backgroundFocus(game, hero, cache)) return 1974;
    // "Zmień grafikę": a page seen during the visit comes back at once (no new list request, its
    // thumbnails from memory, only the missing ones asked for); closing frees them.
    app.openArtworkBrowser(game);
    if (!app.artworkOpen()) return 1977;
    const auto list = [&](std::vector<GridAsset> assets) {
        auto message = std::make_unique<ArtworkBrowserMessage>();
        message->type = ArtworkBrowserMessage::Type::list; message->generation = app.artwork_jobs_->generation.load();
        message->page.assets = std::move(assets); message->page.source = {77, false};
        app.onArtworkBrowser(std::move(message));
    };
    const auto pending = [&](ArtworkBrowserMessage::Type type) {
        std::lock_guard lock(app.artwork_jobs_->mutex);
        return std::count_if(app.artwork_jobs_->jobs.begin(), app.artwork_jobs_->jobs.end(), [&](const auto& job) { return job.type == type; });
    };
    const int first_kind = app.artwork_state_.category;
    list({{101, L"https://x.test/a.png", L"https://x.test/a-thumb.png"}, {102, L"https://x.test/b.png", L"https://x.test/b-thumb.png"}});
    {
        auto thumbnail = std::make_unique<ArtworkBrowserMessage>();
        thumbnail->type = ArtworkBrowserMessage::Type::thumbnail; thumbnail->generation = app.artwork_jobs_->generation.load();
        thumbnail->asset_id = 101; thumbnail->bytes = pngFixture(50, 60, 90);
        app.onArtworkBrowser(std::move(thumbnail));
    }
    app.artwork_state_.category = artwork_ui::category(ArtworkKind::hero); app.loadArtworkPage();
    if (pending(ArtworkBrowserMessage::Type::list) != 1) return 1978; // Not seen yet: asked for.
    list({{201, L"https://x.test/c.png", L"https://x.test/c-thumb.png"}});
    app.artwork_state_.category = first_kind; app.loadArtworkPage();
    if (app.artwork_state_.loading || app.artwork_state_.assets.size() != 2 || pending(ArtworkBrowserMessage::Type::list) != 0) return 1979;
    {
        std::lock_guard lock(app.artwork_jobs_->mutex);
        const auto& jobs = app.artwork_jobs_->jobs;
        if (jobs.size() != 1 || jobs.front().type != ArtworkBrowserMessage::Type::thumbnail || jobs.front().asset.id != 102) return 1980;
    }
    app.closeArtworkBrowser();
    if (!app.artwork_pages_.empty()) return 1981;
    // The library's empty view: its message in the middle of the screen.
    app.store_library_.clear();
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    Game uninstalled{6500, L"Not installed"}; uninstalled.installed = false; uninstalled.last_played = 100;
    library->games.push_back(uninstalled);
    app.onLibrary(std::move(library));
    app.scanning_ = false; app.pc_initial_ready_ = true; // Loaded: an empty view is not "loading".
    app.openLibrary();
    app.library_controls_.options.installed_only = true; // A filter that leaves nothing.
    app.applyLibraryOptions();
    settle();
    if (!app.cards_.empty() || app.screen_ != ScreenMode::library) return 1975;
    if (!capture(L"library-empty-view.png")) return 1976;
    return 0;
}
