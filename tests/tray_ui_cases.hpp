// Included after LauncherUiTest (0.3.0, issue #6). Biblioteka → Uruchamianie → "Chowaj do zasobnika
// podczas gry" (src/app/tray.cpp): off by default; on, a game started here hides the window once the
// game holds the foreground, an icon (recorded, never added to the real notification area) stands in,
// the icon brings it back, Explorer's restart adds it again and the game's end shows the window.
// The game's end returns the foreground (0.4.7) unless the user chose another program meanwhile.
// Then "Popraw kadr" on Tło (src/app/crop.cpp): the screen-shaped window moves over a wide
// background, is saved bound to its bytes and reset by "Wyśrodkuj"; "Zmień grafikę" keeps the pages seen
// during a visit; and the empty library view's
// message in the middle of the screen. Captures: background-crop*.png, library-empty-view.png.
#include "../src/artwork/background_focus.hpp"

int LauncherUiTest::trayAndBackgroundCrop(HWND window) {
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
        // The row (Biblioteka → Uruchamianie) and its toggle.
        app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library)); app.buildSettingsRows();
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::hide_while_playing; });
        if (row == app.settings_rows_.end() || row->control != settings_ui::Control::toggle || row->checked) return 1961;
        app.settingsToggle(*row);
        if (!app.settings_.hide_while_playing) return 1962;
        app.game_sessions_.clear(); app.launched_here_.clear();
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
