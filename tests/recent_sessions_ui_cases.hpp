// Recent: Steam updates are no new games, games in play lead the shelf and take the focus when
// started, and a background bound again to the same picture continues instead of doubling.
int LauncherUiTest::recentSessions(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute; app.settings_.accept_action = AppSettings::AcceptAction::launch; // These cases pick cards to launch them.
    app.settings_.launch_animation = false; app.settings_.quick_resume = true;
    app.overlay_backend_.foreground = [window] { return window; };
    app.quick_resume_.focus = [](const auto&) { return true; };
    if (!app.renderer_.initialize(window)) return 3700;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    Game newest{3701, L"Newest played"}; newest.last_played = 500;
    Game older{3702, L"Older played"}; older.last_played = 400;
    Game oldest{3703, L"Oldest played"}; oldest.last_played = 300;
    Game updated{3704, L"Updated overnight"}; updated.last_played = 100; updated.last_updated = 900;
    library->games = {newest, older, oldest, updated};
    SteamCollection collection{L"Recent sessions", {3701, 3702, 3703, 3704}}; collection.id = "recent-sessions";
    library->collections.push_back(collection);
    app.onLibrary(std::move(library));
    const auto order = [&] {
        std::vector<uint32_t> ids;
        for (const auto& game : app.categories_[0].games) if (isGame(game)) ids.push_back(game.app_id);
        return ids;
    };
    if (order() != std::vector<uint32_t>{3701, 3702, 3703, 3704}) return 3701;

    // A Steam update keeps FullyInstalled: listed at its own place while it updates, never pinned after.
    LibraryMessage progress; progress.installation_update = true; progress.account = app.account_;
    progress.games = app.installed_library_;
    const auto edit = [&](uint32_t id) -> Game& {
        return *std::find_if(progress.games.begin(), progress.games.end(), [&](const Game& game) { return game.app_id == id; });
    };
    edit(3704).installed = false; edit(3704).installation = {InstallPhase::downloading, 10, 100}; edit(3704).installation.update = true;
    app.updateSteamInstallations(progress);
    if (order() != std::vector<uint32_t>{3701, 3702, 3703, 3704}) return 3702;
    edit(3704).installed = true; edit(3704).installation = {};
    app.updateSteamInstallations(progress);
    if (app.completed_installs_.contains(3704) || order() != std::vector<uint32_t>{3701, 3702, 3703, 3704}) return 3703;
    // A new installation still leads Recent, while downloading and once finished.
    Game fresh{3705, L"Fresh install"}; fresh.installed = false; fresh.installation = {InstallPhase::downloading, 10, 100};
    progress.games.push_back(fresh);
    app.updateSteamInstallations(progress);
    if (order().front() != 3705) return 3704;
    edit(3705).installed = true; edit(3705).installation = {};
    app.updateSteamInstallations(progress);
    if (!app.completed_installs_.contains(3705) || order().front() != 3705) return 3705;

    // Started from the library: Home, its tile first in Recent and focused, before telemetry confirms it.
    app.openLibrary();
    const auto card = std::find_if(app.cards_.begin(), app.cards_.end(), [](const GameCard& item) { return item.game.app_id == 3703; });
    if (card == app.cards_.end()) return 3706;
    app.focusCard(static_cast<size_t>(card - app.cards_.begin()));
    app.launch();
    if (!app.pending_launch_) return 3707;
    app.finishLaunch();
    if (app.screen_ != ScreenMode::shelves || app.categories_[app.category_].id != "recent" ||
        app.cards_[app.selected_].game.app_id != 3703 || order().front() != 3703) return 3708;
    // Suspended games follow the running ones, ahead of installations and play history.
    app.game_sessions_.try_emplace(gameKey(older), GetTickCount64()).first->second.freeze({{4242, 1}});
    app.refreshRecentGames();
    if (order() != std::vector<uint32_t>{3703, 3702, 3705, 3701, 3704} || app.cards_[app.selected_].game.app_id != 3703) return 3709;
    // Telemetry confirms the start, then the game ends: by play history it stays first after the suspended one.
    const auto activity = [&](GameActivity state, uint64_t at) {
        auto message = std::make_unique<GameActivityMessage>();
        message->generation = app.game_activity_jobs_->generation.load(); message->observed_at = at;
        message->activities = {{gameKey(oldest), state}};
        app.onGameActivity(std::move(message));
    };
    const auto now = GetTickCount64();
    activity(GameActivity::running, now);
    activity(GameActivity::stopped, now + 100);
    activity(GameActivity::stopped, now + 2000);
    if (app.game_sessions_.contains(gameKey(oldest))) return 3710;
    if (order() != std::vector<uint32_t>{3702, 3705, 3703, 3701, 3704}) return 3711;
    // "Graj" on the suspended game from another shelf: it resumes and Home focuses its tile.
    app.quick_resume_.resume = [](const auto&) { return process_freeze::Result::ok; };
    const auto other = std::find_if(app.categories_.begin(), app.categories_.end(), [](const auto& item) { return item.id != "recent" && !item.games.empty() && isGame(item.games[0]); });
    if (other == app.categories_.end()) return 3712;
    app.showCategory(static_cast<size_t>(other - app.categories_.begin()));
    const auto frozen_card = std::find_if(app.cards_.begin(), app.cards_.end(), [](const GameCard& item) { return item.game.app_id == 3702; });
    if (frozen_card == app.cards_.end()) return 3713;
    app.focusCard(static_cast<size_t>(frozen_card - app.cards_.begin()));
    app.launch();
    if (app.categories_[app.category_].id != "recent" || app.cards_[app.selected_].game.app_id != 3702 ||
        app.game_sessions_.at(gameKey(older)).frozen() || order().front() != 3702) return 3714;
    app.game_sessions_.clear(); app.refreshRecentGames();

    // Background continuity: the same picture bound again to a rebuilt shelf goes on at full level.
    // Painted on the window target: a capture recreates the target, dropping the layers' bitmaps.
    const auto paint = [&](const wchar_t*) { app.paint(); return true; };
    app.showCategory(0); app.focusCard(0);
    app.renderer_.setHero(0, pngFixture(90, 64, 36));
    app.detail_ = 1;
    paint(L"hero-settled.png"); // Uploads the decoded bitmaps.
    if (!paint(L"hero-settled.png") || !app.renderer_.heroShownForTest() || app.renderer_.heroLevelForTest() < 0.999f) return 3715;
    // The background dot grid is for video only: a picture stays clean.
    if (app.renderer_.backgroundGridCellForTest() != 0) return 3732;
    {
        // A trailer faded in: the grid lies over it, in device-pixel cells (about 360 rows of dots).
        std::vector<uint8_t> frame(64 * 36 * 4, 128);
        app.renderer_.setVideoFrame(frame.data(), 64, 36);
        Sleep(650);
        paint(L"video-grid.png");
        const auto view = app.renderer_.viewport();
        const int expected = std::clamp(static_cast<int>(std::lround(view.height * view.scale / 360.0f)), 3, 12);
        if (app.renderer_.backgroundGridCellForTest() != expected) return 3733;
        for (const auto style : {AppSettings::GridStyle::lines, AppSettings::GridStyle::mesh, AppSettings::GridStyle::dots}) {
            app.renderer_.setBackgroundGrid(0.5f, style); // Another pattern: its tile is built anew.
            paint(L"video-grid.png");
            if (app.renderer_.backgroundGridStyleForTest() != style || app.renderer_.backgroundGridCellForTest() != expected) return 3738;
        }
        app.renderer_.clearVideo();
        paint(L"hero-settled.png");
    }
    // The same shelf rebuilt on screen (a library refresh): its cards and focus stay as they are.
    const auto kept = app.cards_[app.selected_].opacity;
    app.rebuildCards();
    app.renderer_.setHero(app.selected_, pngFixture(90, 64, 36));
    if (app.detail_ != 1 || app.cards_[app.selected_].opacity != kept ||
        !paint(L"hero-kept.png") || app.renderer_.heroLevelForTest() < 0.999f || app.renderer_.heroLeavingForTest()) return 3731;
    app.renderer_.resetCovers(app.cards_.size()); // Slots dropped elsewhere: the next rebuild starts over.
    app.rebuildCards(); // Detail restarts at 0; a new decode of the same bytes in a new buffer.
    app.renderer_.setHero(app.selected_, pngFixture(90, 64, 36));
    if (app.detail_ != 0 || !paint(L"hero-rebound.png") || app.renderer_.heroLevelForTest() < 0.999f || app.renderer_.heroLeavingForTest()) return 3716;
    // Another picture: nothing moves while the focus only passes through, then it cross-fades in.
    auto* first = app.renderer_.heroBitmapForTest(0);
    app.focusCard(1); app.renderer_.setHero(1, pngFixture(200, 64, 36)); app.detail_ = 0.2f;
    auto* second = app.renderer_.heroBitmapForTest(1);
    if (!paint(L"hero-passing.png") || !second || app.renderer_.heroLeavingForTest() || app.renderer_.heroShownBitmapForTest() != first ||
        app.renderer_.heroLevelForTest() < 0.999f) return 3717;
    app.detail_ = 0.5f;
    if (!paint(L"hero-crossing.png") || !app.renderer_.heroLeavingForTest() || app.renderer_.heroShownBitmapForTest() != second) return 3760;
    const float crossing = app.renderer_.heroLevelForTest();
    if (crossing < 0.2f || crossing > 0.95f) return 3761;
    // Back: the same cross-fade runs backwards, the first picture from its visible level, not from zero.
    app.focusCard(0); app.detail_ = 0;
    if (!paint(L"hero-returning.png") || app.renderer_.heroShownBitmapForTest() != first || app.renderer_.heroLeavingBitmapForTest() != second ||
        app.renderer_.heroLevelForTest() < 1 - crossing - 0.05f || app.renderer_.heroLevelForTest() > 0.999f) return 3718;
    app.detail_ = 1; paint(L"hero-settled.png");
    if (app.renderer_.heroShownBitmapForTest() != first || app.renderer_.heroLeavingForTest()) return 3762;
    // A third picture, settled, while the second one's cross-fade runs: that fade finishes first,
    // then the third follows; the second is never dropped half-shown.
    app.focusCard(1); app.detail_ = 0.345f;
    if (!paint(L"hero-crossing.png") || app.renderer_.heroShownBitmapForTest() != second || app.renderer_.heroLeavingBitmapForTest() != first ||
        app.renderer_.heroLevelForTest() > 0.99f) return 3763;
    app.focusCard(2); app.renderer_.setHero(2, pngFixture(30, 64, 36)); app.detail_ = 0.6f;
    auto* third = app.renderer_.heroBitmapForTest(2);
    if (!paint(L"hero-queued.png") || !third || app.renderer_.heroShownBitmapForTest() != second || app.renderer_.heroLeavingForTest() ||
        app.renderer_.heroLevelForTest() < 0.999f) return 3764;
    if (!paint(L"hero-following.png") || app.renderer_.heroShownBitmapForTest() != third || app.renderer_.heroLeavingBitmapForTest() != second) return 3765;

    // Pad: A held on a game opens its menu, as a held Enter; a short press still starts the game.
    app.showCategory(0); app.focusCard(0);
    PadReading pad{true, L"hold-fixture"}; app.controller(pad); // Connection: the first reading is swallowed.
    pad.buttons = XINPUT_GAMEPAD_A; app.controller(pad);
    if (app.pending_launch_ || !app.enter_held_ || app.menu_open_) return 3719;
    app.enter_started_ -= std::chrono::milliseconds(600); app.tick(); // Opens while A is still down.
    if (!app.menu_open_) return 3720;
    pad.buttons = 0; app.controller(pad);
    if (!app.menu_open_ || app.pending_launch_) return 3721; // The release neither closes it nor launches.
    app.dismissMenu();
    pad.buttons = XINPUT_GAMEPAD_A; app.controller(pad); pad.buttons = 0; app.controller(pad);
    if (!app.pending_launch_ || app.menu_open_) return 3722;
    app.pending_launch_ = false;
    // Held, then moved to another game: neither the menu nor a launch on release.
    pad.buttons = XINPUT_GAMEPAD_A; app.controller(pad);
    pad.buttons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_DPAD_RIGHT; app.controller(pad);
    app.enter_started_ -= std::chrono::milliseconds(600); app.tick();
    pad.buttons = 0; app.controller(pad);
    if (app.menu_open_ || app.pending_launch_ || app.selected_ != 1) return 3723;
    // Non-game tiles (a store, "Wszystkie gry") still act on the press.
    if (app.acceptHoldsForMenu() != isGame(app.cards_[app.selected_].game)) return 3724;

    // Details move by position: Back at the top left, the row of buttons (the primary action,
    // Kolekcje, ★, …), the cards under it (achievements); Up from a card returns to
    // the button it was left from.
    app.openDetails(newest);
    app.details_.achievements_unlocked = 1; app.details_.achievements_total = 5; app.details_loading_ = false;
    const int favorite = app.favoriteDetailAction(), collections = app.collectionsDetailAction(), achievements = app.achievementsDetailAction();
    if (favorite < 0 || collections < 0 || achievements < 0 || app.detail_action_ != 0 || app.ratingDetailAction() >= 0) return 3725;
    const auto step = [&](int dx, int dy) { if (dx) app.navigate(dx); else app.changeCategory(dy); return app.detail_action_; };
    if (step(0, -1) != -1 || step(0, 1) != 0 || step(1, 0) != collections || step(1, 0) != favorite || step(1, 0) != 1 || step(-1, 0) != favorite) return 3726;
    if (step(0, 1) != achievements || step(0, -1) != favorite || step(-1, 0) != collections) return 3727;
    if (step(0, 1) != achievements || step(1, 0) != achievements || step(0, -1) != collections) return 3728;
    if (step(-1, 0) != 0 || step(-1, 0) != 0 || step(0, -1) != -1) return 3729;
    app.navigate(10, false); // Programmatic jumps keep the action order.
    if (app.detail_action_ != achievements) return 3730;

    // A shelf remembered far to the right: its entry wave starts at the card before the focus,
    // so the focused card shows at once instead of after every card off screen to the left.
    {
        Launcher shelf; shelf.hwnd_ = window; shelf.execute_ = fakeExecute; shelf.settings_.launch_animation = false;
        if (!shelf.renderer_.initialize(window)) return 3734;
        auto games = std::make_unique<LibraryMessage>(); games->account = 1;
        SteamCollection long_shelf{L"Long shelf", {}}; long_shelf.id = "long-shelf";
        for (uint32_t id = 3800; id < 3830; ++id) { games->games.push_back(Game{id, L"Shelf game " + std::to_wstring(id)}); long_shelf.app_ids.push_back(id); }
        games->collections.push_back(long_shelf);
        shelf.onLibrary(std::move(games));
        const auto found = std::find_if(shelf.categories_.begin(), shelf.categories_.end(), [](const auto& item) { return item.id == "long-shelf"; });
        if (found == shelf.categories_.end()) return 3735;
        const auto index = static_cast<size_t>(found - shelf.categories_.begin());
        shelf.showCategory(index); shelf.focusCard(20);
        shelf.showCategory(0); shelf.showCategory(index); // Back to the remembered card: the row enters again.
        if (shelf.selected_ != 20 || shelf.arrived_ != 0) return 3736;
        for (int frame = 0; frame < 3; ++frame) { // 150 ms: the focus (second in the slow wave) is in.
            shelf.last_frame_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(50); shelf.tick();
        }
        if (shelf.cards_[19].opacity <= 0 || shelf.cards_[20].opacity <= 0 || shelf.cards_[24].opacity != 0) return 3737;
    }
    // An emulator launched through a script may never provide process telemetry. Its
    // successful shortcut launch must survive a restart without inventing playtime.
    {
        const auto file = userConfigDirectory() / L"emulated-history.ini";
        ShortcutGame yoshi;
        yoshi.game = Game{0xF1000001u, L"Yoshi's Woolly World"};
        yoshi.game.kind = GameKind::nonsteam; yoshi.game.platform = L"Wii U";
        yoshi.game.shortcut_id = (uint64_t{yoshi.game.app_id} << 32) | 0x02000000ULL;
        yoshi.game.executable = L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
        yoshi.game.rom = L"K:\\Emulation\\roms\\wiiu\\Yoshi.wua";
        const auto key = gameKey(yoshi.game);
        const auto history = recent_games::historyKey(yoshi.game, 1, {});
        const auto snapshot = [&] {
            auto games = std::make_unique<LibraryMessage>(); games->account = 1;
            games->shortcuts = {yoshi}; return games;
        };
        Launcher emulator; emulator.hwnd_ = window; emulator.settings_.launch_animation = false;
        emulator.library_writer_ = std::make_unique<LibraryStateWriter>(file);
        if (!emulator.renderer_.initialize(window)) return 3740;
        emulator.onLibrary(snapshot());
        emulator.execute_ = [](HWND, const GameAction&) -> INT_PTR { return 31; };
        emulator.launchGame(yoshi.game); emulator.finishLaunch();
        if (!emulator.game_sessions_.empty() || !emulator.library_state_.last_played.empty()) return 3741;
        emulator.execute_ = fakeExecute;
        emulator.launchGame(yoshi.game); emulator.finishLaunch();
        if (!emulator.library_state_.last_played.contains(history) || !emulator.library_state_.last_played.at(history) ||
            emulator.game_sessions_.at(key).running()) return 3742;
        auto unknown = std::make_unique<GameActivityMessage>();
        unknown->generation = emulator.game_activity_jobs_->generation.load(); unknown->observed_at = GetTickCount64();
        unknown->activities = {{key, GameActivity::unknown}}; emulator.onGameActivity(std::move(unknown));
        emulator.game_sessions_.clear(); emulator.refreshRecentGames();
        if (gameKey(emulator.categories_[0].games[0]) != key || !emulator.library_state_.playtime_ms.empty()) return 3743;
        emulator.library_writer_.reset(); // Drain the real asynchronous writer before the new instance reads.
        const auto saved = loadLibraryState(file);
        if (!saved || saved->last_played != emulator.library_state_.last_played) return 3744;
        Launcher restarted; restarted.hwnd_ = window; restarted.library_state_ = *saved;
        restarted.library_writer_ = std::make_unique<LibraryStateWriter>(file);
        if (!restarted.renderer_.initialize(window)) return 3745;
        restarted.onLibrary(snapshot());
        if (!restarted.game_sessions_.empty() || gameKey(restarted.categories_[0].games[0]) != key ||
            restarted.categories_[0].games[0].last_played != saved->last_played.at(history)) return 3746;
        auto other_account = snapshot(); other_account->account = 2;
        restarted.onLibrary(std::move(other_account));
        if (std::any_of(restarted.categories_[0].games.begin(), restarted.categories_[0].games.end(), isGame)) return 3747;
        auto secret = yoshi.game; secret.app_id += 1; secret.shortcut_id += uint64_t{1} << 32;
        secret.visibility = GameVisibility::private_game;
        emulator.launchGame(secret); emulator.finishLaunch();
        if (emulator.library_state_.last_played != saved->last_played) return 3748;
    }
    return 0;
}
