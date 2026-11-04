// Native UI coverage of the category-free row, persistent dock and library return path.
#include "../src/ui/snitch_layout.hpp"

int LauncherUiTest::snitch(HWND window) {
    Launcher app;
    app.settings_.launch_animation = false;
    app.settings_.home_layout = AppSettings::HomeLayout::snitch;
    app.settings_.show_uninstalled = true;
    app.settings_.companion = app.settings_.glance = false;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 8200;
    app.local_time_ = [] { SYSTEMTIME now{}; now.wYear = 2026; now.wMonth = 10; now.wDay = 20; now.wHour = 12; now.wMinute = 39; return now; };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        app.notifications_.clear();
        if (!count || count >= 4096) { app.paint(); return SUCCEEDED(app.renderer_.draw_result_for_test); }
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); }) && SUCCEEDED(app.renderer_.draw_result_for_test);
    };
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 24; ++i) {
        Game game{8200 + i, L"snitch game " + std::to_wstring(i)};
        game.last_played = i < 8 ? 100 + i : 0;
        game.installed = i != 6; // An uninstalled recent game stays in the row with an outline cartridge.
        library->games.push_back(std::move(game));
    }
    library->collections.push_back(SteamCollection{L"Akcja", {8200, 8201, 8202}, "snitch-action"});
    app.onLibrary(std::move(library)); app.showCategory(0);
    if (!app.snitchActive() || app.cards_.size() != 13 || app.cards_.back().game.kind != GameKind::all ||
        app.cards_.back().game.name != L"Wszystkie" || app.cards_[0].game.app_id != 8207 || app.cards_[7].game.app_id != 8200 ||
        app.cards_[8].game.last_played || !isGame(app.cards_[11].game)) return 8201;
    for (const auto& game : app.public_library_) {
        auto image = std::make_unique<CoverMessage>(); image->key = gameKey(game);
        image->generation = app.jobs_->generation.load(); image->kind = CoverMessage::Kind::cover;
        image->bytes = pngFixture(static_cast<uint8_t>(45 + game.app_id % 12 * 16), 256, 256);
        app.onCover(std::move(image));
    }
    settle();
    const auto view = app.renderer_.viewport();
    if (std::abs(app.cards_[0].size - app.cards_[1].size) > 0.1f || app.cards_[0].y < 200 ||
        app.cards_[0].y + app.cards_[0].size >= snitch_layout::dockY(view.height) - 60) return 8202;
    if (!capture(L"snitch-row.png")) return 8203;
    app.navigate(1); settle();
    if (app.cards_[app.selected_].game.app_id != 8206 || app.cards_[app.selected_].game.installed ||
        !capture(L"snitch-uninstalled.png")) return 8222;
    app.navigate(-1); settle();
    app.navigate(-1); app.openCategoryMenu(); app.changeCategory(-1);
    if (app.selected_ || app.category_menu_open_ || app.category_ || app.quick_open_) return 8204;
    // Up from the row lands on the profile picture (top left): the row loses its focus, Back or Down returns.
    if (!app.snitchProfileFocused()) return 8270;
    settle();
    if (app.snitch_profile_progress_ < 0.95f || app.cards_[0].emphasis > 0.01f || !capture(L"snitch-profile-focus.png")) return 8271;
    app.back(); settle();
    if (app.snitchProfileFocused() || app.snitch_profile_progress_ > 0.01f) return 8272;
    app.changeCategory(-1); app.changeCategory(1); settle();
    if (app.snitchProfileFocused() || app.quick_open_) return 8273;
    app.navigate(2); settle();
    const auto key = gameKey(app.cards_[app.selected_].game);
    if (app.selected_ != 2 || std::abs(app.cards_[2].x - snitch_layout::left) > 1 ||
        app.cardHit(1, snitch_layout::clip_left - 1, app.cards_[1].y + 12)) return 8205;
    app.changeCategory(1); app.quickMove(1); settle();
    if (!app.quick_open_ || app.quick_selected_ != 1 || app.cards_[2].emphasis > 0.01f) return 8206;
    if (!capture(L"snitch-dock.png")) return 8207;
    app.settings_.snitch_colour_icons = true; settle(); // Opcje układu → Kolorowe ikony: a few discs in colour.
    if (!capture(L"snitch-dock-colour.png")) return 8223;
    app.settings_.snitch_colour_icons = false; settle();
    app.changeCategory(-1); settle();
    if (app.quick_open_ || gameKey(app.cards_[app.selected_].game) != key) return 8208;
    app.openContextMenu(); settle();
    if (!app.menu_open_ || !capture(L"snitch-menu.png")) return 8221;
    app.dismissMenu(); app.menu_progress_ = 0;
    app.focusCard(12); settle();
    if (!capture(L"snitch-all.png")) return 8209;
    app.accept();
    if (app.screen_ != ScreenMode::library || app.cards_.size() != 24) return 8210;
    app.back(); settle();
    if (!app.snitchActive() || app.category_ || app.selected_ != 12) return 8211;
    // Persistent dock accepts a single click while the game row owns keyboard focus.
    const int plugins = quick_menu::store_slots;
    const int settings = quick_menu::count(plugins) - 2;
    const float x = snitch_layout::dockX(view.width, settings, plugins), y = snitch_layout::dockY(view.height);
    if (!app.hoverButton(x * view.scale, y * view.scale)) return 8212;
    app.click(x * view.scale, y * view.scale, false);
    if (!app.settingsOpen()) return 8213;
    if (!capture(L"settings-snitch.png")) return 8214;
    app.back(); app.showCategory(2); settle();
    if (app.category_ || app.cards_.size() != 13) return 8215;
    app.settings_.companion = app.settings_.glance = true;
    const auto spots = app.homeWidgetSpots();
    if (!spots.companion || !spots.glance || spots.companion->left < snitch_layout::profile().right ||
        spots.glance->bottom >= snitch_layout::rowTop(view.height)) return 8216;
    // Size changes keep all clickable circles inside the window, even with extra plugin pages.
    for (const float width : {1100.0f, 1600.0f, 2400.0f}) for (const int extra : {0, 3, 16}) {
        const float radius = 44 * snitch_layout::dockScale(width, extra);
        if (snitch_layout::dockX(width, 0, extra) - radius < 0 ||
            snitch_layout::dockX(width, quick_menu::count(extra) - 1, extra) + radius > width) return 8217;
        for (int i = 0; i < quick_menu::count(extra); ++i)
            if (snitch_layout::hitDock(width, 900, snitch_layout::dockX(width, i, extra), snitch_layout::dockY(900), extra) != i) return 8218;
    }
    app.settings_.companion = app.settings_.glance = false;
    app.installed_library_.clear(); app.public_library_.clear(); app.buildShelves(); app.showCategory(0); settle();
    if (!app.cards_.empty() || !capture(L"snitch-empty.png")) return 8219;
    app.changeCategory(1); app.quick_selected_ = settings; app.quickAccept();
    if (!app.settingsOpen()) return 8220;
    return 0;
}
