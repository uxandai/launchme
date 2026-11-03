#include "../src/ui/launch_animation.hpp"

int LauncherUiTest::launchAnimation(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    app.foreground_ = [](HWND) { return false; }; // Losing focus must not extend the animation.
    app.overlay_backend_.foreground = [window] { return window; };
    if (!app.renderer_.initialize(window)) return 2500;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    library->games = {Game{620, L"Portal 2"}, Game{400, L"Portal"}};
    app.onLibrary(std::move(library));
    app.openSettings();
    // Biblioteka → Uruchamianie: the picture is always on, without a switch of its own.
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library)); app.buildSettingsRows();
    if (std::any_of(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& item) { return item.title == L"Animacja uruchamiania gry"; }) ||
        !app.settings_.launch_animation || !loadAppSettings().launch_animation) return 2501;
    app.showCategory(0); app.openLibrary(); app.focusCard(0);
    const auto game = app.cards_[0].game;

    dispatched.clear(); app.launch();
    if (!app.launchAnimationActive() || !app.pending_launch_ || app.launch_animation_title_ != game.name || app.launch_artwork_key_ != gameKey(game)) return 2508;
    const auto started = *app.launch_animation_started_;
    app.tick(); // Launch immediately, rather than waiting for the animation to finish.
    if (app.pending_launch_ || dispatched.size() != 1 || !app.launchAnimationActive() || app.launch_progress_ != 0) return 2509;
    app.launch(); app.click(200, 200, true);
    PadReading pad{}; pad.connected = true;
    app.controller(pad); pad.buttons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_DPAD_RIGHT; app.controller(pad);
    if (dispatched.size() != 1 || app.selected_ != 0 || app.quick_open_) return 2510;
    app.tickLaunchAnimation(started + std::chrono::milliseconds(800));
    if (!app.launchAnimationActive() || app.screen_ != ScreenMode::library) return 2511;
    app.tickLaunchAnimation(started + std::chrono::seconds(5)); // About ten seconds, not a blink.
    if (!app.launchAnimationActive()) return 2523;
    using launch_animation::seconds;
    using launch_animation::fade_out;
    if (seconds < 8 || launch_animation::opacity(0) != 0 || launch_animation::opacity(0.2f) <= 0 || launch_animation::opacity(0.2f) >= 1 ||
        launch_animation::opacity(seconds * 0.5f) != 1 || launch_animation::opacity(seconds) != 1 || launch_animation::opacity(600) != 1 ||
        launch_animation::opacity(seconds, fade_out) != 0 || launch_animation::opacity(seconds, fade_out * 0.5f) <= 0 ||
        launch_animation::opacity(seconds, fade_out * 0.5f) >= 1 || launch_animation::opacity(seconds, 0) != 1 ||
        launch_animation::longest < 60 ||
        launch_animation::logo(0) != 0 || launch_animation::logo(3) != 1) return 2519;
    // The white dot flies at one speed from one touch of the ring to the next, never leaving it, and keeps going.
    for (int i = 0; i <= 400; ++i) {
        const float time = static_cast<float>(i) * 0.37f;
        const auto d = launch_animation::dot(time), later = launch_animation::dot(time + 0.01f);
        const float radius = std::hypot(d.x, d.y), step = std::hypot(later.x - d.x, later.y - d.y);
        if (radius > 1.0001f || step > 0.06f) return 2538;
    }
    const auto touch = launch_animation::dot(launch_animation::bounce_seconds * 3);
    if (std::abs(std::hypot(touch.x, touch.y) - 1) > 0.001f || std::hypot(launch_animation::dot(launch_animation::bounce_seconds * 3.5f).x,
        launch_animation::dot(launch_animation::bounce_seconds * 3.5f).y) > 0.9f) return 2539;
    if (launch_animation::dot(1).x == launch_animation::dot(30).x && launch_animation::dot(1).y == launch_animation::dot(30).y) return 2540;

    auto captures = userConfigDirectory() / L"launch-captures";
    wchar_t folder[4096]{};
    if (const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096); count && count < 4096) captures = folder;
    std::filesystem::create_directories(captures);
    app.renderer_.setCover(0, pngFixture(100, 180, 180), {});
    app.launch_animation_started_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(250);
    if (!app.renderer_.captureForTest(captures / L"cover-launch-enter.png", [&] { app.paint(); })) return 2520;
    app.launch_animation_started_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(4000);
    if (!app.renderer_.captureForTest(captures / L"cover-launch.png", [&] { app.paint(); })) return 2512;
    app.launch_animation_started_ = started;
    // Back hides it early, but not the press that started the game.
    if (app.dismissLaunchAnimation(started + std::chrono::milliseconds(300)) || !app.launchAnimationActive()) return 2521;
    if (!app.dismissLaunchAnimation(started + std::chrono::seconds(2))) return 2522;
    if (app.launchAnimationActive() || app.screen_ != ScreenMode::shelves || dispatched.size() != 1) return 2513;
    // Left alone in front, it stays while the game still starts and while its window is being
    // brought forward, then fades out once the game runs without it.
    const auto after = [&](std::chrono::milliseconds ms) { return *app.launch_animation_started_ + ms; };
    const auto past = std::chrono::milliseconds(static_cast<int>(seconds * 1000) + 50);
    const auto fade = std::chrono::milliseconds(static_cast<int>(fade_out * 1000) + 50);
    const auto run = [&](uint64_t at) { app.game_sessions_.at(gameKey(app.pending_game_)).observe(GameActivity::running, at); };
    app.focusCard(0); app.game_sessions_.clear(); app.cancelGameFocus();
    app.launch(); app.tick();
    if (!app.launchAnimationActive() || !app.game_sessions_.contains(app.launch_artwork_key_)) return 2524;
    app.tickLaunchAnimation(after(past)); // The game is not there yet.
    if (!app.launchAnimationActive() || app.launch_animation_ending_) return 2525;
    run(GetTickCount64());
    app.tickLaunchAnimation(after(past)); // Running; its window is still being brought forward.
    if (!app.launchAnimationActive() || app.launch_animation_ending_ || app.launch_focus_key_ != app.launch_artwork_key_) return 2526;
    app.cancelGameFocus();
    app.tickLaunchAnimation(after(past));
    if (!app.launchAnimationActive() || !app.launch_animation_ending_ || !app.launchAnimationMoving(after(past))) return 2527;
    app.tickLaunchAnimation(*app.launch_animation_ending_ + fade);
    if (app.launchAnimationActive() || app.screen_ != ScreenMode::shelves) return 2528;
    // A game that never shows, the launcher in front: it gives up after its longest wait.
    app.focusCard(0); app.game_sessions_.clear(); app.cancelGameFocus();
    app.launch(); app.tick();
    app.tickLaunchAnimation(after(std::chrono::seconds(static_cast<int>(launch_animation::longest) - 1)));
    if (!app.launchAnimationActive() || app.launch_animation_ending_) return 2529;
    app.tickLaunchAnimation(after(std::chrono::seconds(static_cast<int>(launch_animation::longest) + 1)));
    if (!app.launch_animation_ending_) return 2530;
    app.stopLaunchAnimation();
    // The game takes the foreground: the picture rests behind it, still (no frames), as long as it runs.
    HWND foreground = window;
    app.overlay_backend_.foreground = [&] { return foreground; };
    app.focusCard(0); app.game_sessions_.clear(); app.cancelGameFocus();
    app.launch(); app.tick();
    run(GetTickCount64());
    foreground = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x5678));
    app.tickLaunchAnimation(after(std::chrono::seconds(2)));
    if (!app.launchAnimationActive() || !app.launch_animation_rested_ || app.launchAnimationMoving(after(std::chrono::seconds(2)))) return 2531;
    app.tickLaunchAnimation(after(std::chrono::minutes(30)));
    if (!app.launchAnimationActive() || app.launch_animation_ending_) return 2532;
    foreground = nullptr; // A moment between two windows changes nothing.
    app.tickLaunchAnimation(after(std::chrono::minutes(30)));
    if (!app.launchAnimationActive() || app.launch_animation_ending_) return 2533;
    // Seen again (Alt+Tab back): it fades out over the launcher.
    foreground = window;
    app.tickLaunchAnimation(after(std::chrono::minutes(31)));
    if (!app.launch_animation_ending_) return 2534;
    app.tickLaunchAnimation(*app.launch_animation_ending_ + fade);
    if (app.launchAnimationActive()) return 2535;
    // The game ends while the picture rests behind it: it ends with the session.
    app.focusCard(0); app.game_sessions_.clear(); app.cancelGameFocus();
    app.launch(); app.tick();
    const auto started_at = GetTickCount64();
    run(started_at);
    foreground = reinterpret_cast<HWND>(static_cast<UINT_PTR>(0x5678));
    app.tickLaunchAnimation(after(std::chrono::seconds(3)));
    for (const uint64_t at : {started_at + 1000, started_at + 3000}) {
        auto message = std::make_unique<GameActivityMessage>();
        message->generation = app.game_activity_jobs_->generation.load(); message->observed_at = at;
        message->activities.emplace_back(app.launch_artwork_key_, GameActivity::stopped);
        app.onGameActivity(std::move(message));
    }
    if (app.launchAnimationActive() || !app.game_sessions_.empty()) return 2536;
    app.overlay_backend_.foreground = [window] { return window; };
    dispatched.resize(1);

    // Failure reveals the error right away, without leaving a fake loading screen.
    app.game_sessions_.clear(); app.focusCard(0);
    app.execute_ = [](HWND, const GameAction&) -> INT_PTR { return 31; };
    app.launch(); app.finishLaunch();
    if (app.launchAnimationActive() || app.notifications_.empty() || app.audio_.music_target_ <= 0) return 2515;
    app.execute_ = fakeExecute; app.settings_.launch_animation = false;
    app.launch();
    if (app.launchAnimationActive() || !app.pending_launch_) return 2516;
    app.finishLaunch();
    if (app.launchAnimationActive() || dispatched.size() != 2) return 2517;
    // Installation/client actions use their existing feedback even with the effect enabled.
    app.settings_.launch_animation = true; app.game_sessions_.clear();
    app.cards_[app.selected_].game.installed = false;
    app.launch();
    if (app.launchAnimationActive() || !app.pending_launch_) return 2518;
    app.finishLaunch();

    // Biblioteka → Uruchamianie → Ciche uruchamianie Steam: off by default. On, a Steam game goes
    // through steam.exe -silent, without foreground permission and with a longer focus handoff.
    app.settings_.launch_animation = false; app.game_sessions_.clear(); app.cancelGameFocus();
    app.openSettings();
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library)); app.buildSettingsRows();
    app.openPlatformSettings(GameSource::steam); // Steam's own options live on its page.
    const auto silent_row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(),
        [](const auto& item) { return item.id == settings_ui::Id::steam_silent_launch; });
    if (silent_row == app.settings_rows_.end() || silent_row->checked || app.settings_.steam_silent_launch) return 5104;
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(silent_row - app.settings_rows_.begin());
    app.settingsActivate(); app.saveSettings();
    if (!app.settings_.steam_silent_launch || !loadAppSettings().steam_silent_launch) return 5105;
    app.showCategory(0); app.openLibrary(); app.focusCard(0);
    static std::vector<GameAction> silent_actions;
    silent_actions.clear();
    app.steam_executable_ = [] { return std::filesystem::path(L"C:\\Steam\\steam.exe"); };
    app.execute_ = [](HWND, const GameAction& action) -> INT_PTR { silent_actions.push_back(action); return 42; };
    const auto url = L"steam://rungameid/" + std::to_wstring(app.cards_[app.selected_].game.app_id);
    app.launch(); app.finishLaunch();
    if (silent_actions.size() != 1 || silent_actions[0].target != L"C:\\Steam\\steam.exe" ||
        silent_actions[0].arguments != L"-silent -- \"" + url + L"\"" || !silent_actions[0].keep_focus ||
        app.launch_focus_until_ < GetTickCount64() + 30000) return 5106;
    // steam.exe refused: the plain steam:// URL still starts the game, with the usual handoff.
    app.game_sessions_.clear(); app.cancelGameFocus(); silent_actions.clear();
    app.execute_ = [](HWND, const GameAction& action) -> INT_PTR { silent_actions.push_back(action); return action.keep_focus ? 2 : 42; };
    app.launch(); app.finishLaunch();
    if (silent_actions.size() != 2 || silent_actions[1].target != url || silent_actions[1].keep_focus ||
        app.launch_focus_until_ > GetTickCount64() + 16000) return 5107;
    app.game_sessions_.clear(); app.cancelGameFocus(); app.execute_ = fakeExecute;
    app.settings_.steam_silent_launch = false; app.saveSettings();

    // Przełączanie kont Steam (on by default): a game another remembered account installed restarts
    // Steam signed in to it before it starts; the launch waits for the switch, a failed one ends it.
    {
        using namespace steam_accounts;
        static std::vector<std::string> switched;
        switched.clear(); dispatched.clear();
        app.account_ = steam_id_offset + 1;
        app.steam_accounts_ = [] { return std::vector<Account>{{steam_id_offset + 2, "second", L"Second", true, false}, {steam_id_offset + 3, "third", L"Third", false, false}}; };
        app.steam_switcher_ = [](const Account& account) { switched.push_back(account.account_name); return true; };
        if (!app.settings_.steam_account_switch || !app.settings_.steam_install_dialog) return 5110;
        app.showCategory(0); app.openLibrary(); app.focusCard(0);
        // The focused card (each launch can move the focus to the started game on the home screen).
        const auto prepare = [&](uint64_t owner, bool installed) {
            auto& game = app.cards_[app.selected_].game;
            game.installed = installed; game.steam_owner = owner; game.installation = {};
            return game.app_id;
        };
        const auto switched_url = L"steam://rungameid/" + std::to_wstring(prepare(steam_id_offset + 2, true));
        app.launch();
        if (!app.pending_launch_ || !app.steam_switch_pending_) return 5111;
        for (int i = 0; i < 6; ++i) { app.last_frame_ -= std::chrono::milliseconds(200); app.tick(); }
        if (!dispatched.empty() || !app.pending_launch_) return 5112; // The game waits for Steam.
        for (int i = 0; i < 200 && switched.empty(); ++i) Sleep(5);
        if (switched != std::vector<std::string>{"second"}) return 5113;
        app.steamSwitchFinished(app.steam_switch_generation_, true, L"Second");
        for (int i = 0; i < 15 && app.pending_launch_; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
        if (app.pending_launch_ || dispatched.size() != 1 || dispatched.back() != switched_url) return 5114;
        // A failed switch cancels the launch; an account Steam has not remembered is left to Steam.
        app.game_sessions_.clear(); app.cancelGameFocus(); dispatched.clear();
        prepare(steam_id_offset + 2, true);
        app.launch();
        if (!app.steam_switch_pending_) return 5127;
        app.steamSwitchFinished(app.steam_switch_generation_, false, L"Second");
        app.tick();
        if (app.pending_launch_ || app.steam_switch_pending_ || !dispatched.empty() || app.notifications_.empty()) return 5115;
        prepare(steam_id_offset + 3, true);
        app.launch();
        if (!app.pending_launch_ || app.steam_switch_pending_) return 5116;
        app.finishLaunch(); app.game_sessions_.clear(); app.cancelGameFocus();
        prepare(app.account_, true); // Its own account: no switch.
        app.launch();
        if (!app.pending_launch_ || app.steam_switch_pending_) return 5117;
        app.finishLaunch(); app.game_sessions_.clear(); app.cancelGameFocus();

        // Instalacja Steam w launcherze (on by default): Install opens Launchme's dialog on the library
        // with the most free space; Install there writes the stub (a fake) and closes it.
        static std::vector<std::wstring> installs;
        installs.clear(); dispatched.clear();
        app.steam_libraries_ = [] {
            constexpr uint64_t gb = 1024ull * 1024 * 1024;
            return std::vector<Folder>{{L"C:\\Program Files (x86)\\Steam", 40 * gb, 500 * gb}, {L"D:\\SteamLibrary", 812 * gb, 1800 * gb}};
        };
        app.steam_installer_ = [](const Game&, const std::filesystem::path& library, uint64_t) { installs.push_back(library.wstring()); return true; };
        const auto absent = prepare(0, false);
        app.launch();
        if (!app.gogInstallOpen() || !app.gog_install_ui_.steam || app.gog_install_ui_.library != 1 || app.gog_install_ui_.row != 3 ||
            app.pending_launch_ || !dispatched.empty()) return 5118;
        if (!app.renderer_.captureForTest(captures / L"steam-install.png", [&] { app.paint(); })) return 5119;
        app.gog_install_ui_.row = 0; app.gogInstallKey(VK_RIGHT, false);
        if (app.gog_install_ui_.library != 0) return 5120;
        app.gogInstallKey(VK_LEFT, false); app.gogInstallKey(VK_DOWN, false); app.gogInstallKey(VK_DOWN, false); // Library → Steam's window → Install.
        if (app.gog_install_ui_.library != 1 || app.gog_install_ui_.row != 3) return 5121;
        app.gogInstallAccept();
        if (!app.gog_install_ui_.steam_busy) return 5122;
        for (int i = 0; i < 200 && installs.empty(); ++i) Sleep(5);
        if (installs != std::vector<std::wstring>{L"D:\\SteamLibrary"}) return 5123;
        app.steamInstallFinished(app.steam_install_generation_, true);
        if (app.gogInstallOpen() || app.gog_install_ui_.steam) return 5124;
        // "Okno instalacji Steam": Steam's own install window, as with the switch off.
        prepare(0, false);
        app.launch();
        app.gog_install_ui_.row = 1; app.gogInstallAccept();
        if (app.gogInstallOpen() || dispatched.empty() || dispatched.back() != L"steam://install/" + std::to_wstring(absent)) return 5125;
        app.settings_.steam_install_dialog = false; dispatched.clear();
        prepare(0, false);
        app.launch(); app.finishLaunch();
        if (app.gogInstallOpen() || dispatched.empty() || dispatched.back() != L"steam://install/" + std::to_wstring(absent)) return 5126;
        app.settings_.steam_install_dialog = true;
    }
    return 0;
}
