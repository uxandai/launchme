// Included after LauncherUiTest: Szczegóły → Zrzuty ekranu i podobne gry (src/app/details_media.cpp).
// Tryb TV: Down from the buttons scrolls the page to the screenshots, Right slides their strip,
// Enter shows one full screen (Left/Right walk it, Back closes), Down to the similar games, Enter
// opens one's page (Back still leads home), Back from a strip returns to the buttons; switched off
// in Interfejs: no strips. Desktop: the sheet's sections, a click on a screenshot and a similar game.
// Captures: details-media-*.png.

int LauncherUiTest::detailsMedia(HWND window) {
    Launcher app;
    app.settings_.launch_animation = false;
    app.settings_.glance = app.settings_.companion = false;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 22000;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) return true;
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    auto library = std::make_unique<LibraryMessage>();
    library->account = 1;
    for (uint32_t i = 0; i < 16; ++i) {
        Game game{6000 + i, L"Media game " + std::to_wstring(i)};
        game.last_played = 100 + i;
        game.installed = true;
        library->games.push_back(std::move(game));
    }
    app.onLibrary(std::move(library));
    for (const auto& game : app.public_library_) {
        auto message = std::make_unique<CoverMessage>();
        message->key = gameKey(game); message->generation = app.jobs_->generation.load();
        message->kind = CoverMessage::Kind::hero; message->bytes = pngFixture(static_cast<uint8_t>(40 + (game.app_id % 16) * 12), 160, 90);
        app.onCover(std::move(message));
        message = std::make_unique<CoverMessage>();
        message->key = gameKey(game); message->generation = app.jobs_->generation.load();
        message->kind = CoverMessage::Kind::cover; message->bytes = pngFixture(static_cast<uint8_t>(200 - (game.app_id % 16) * 9), 64, 64);
        app.onCover(std::move(message));
    }
    app.settings_.home_layout = AppSettings::HomeLayout::shelves;
    app.showCategory(0); settle();
    if (app.cards_.empty()) return 22001;
    const auto game = app.cards_[0].game;
    // What the loader would bring (UI tests never ask the Steam store): six screenshots, five similar games.
    const auto fill = [&](const Game& of) {
        app.psfive_details_key_ = gameKey(of);
        app.psfive_shots_loading_ = false;
        app.psfive_shot_full_urls_.assign(6, L"shot_full.img|https://example.invalid/shot.jpg");
        app.psfive_similar_games_.assign(app.public_library_.begin() + 4, app.public_library_.begin() + 9);
        app.psfive_similar_bound_.fill(false);
        app.renderer_.resetPsfiveDetails(6, app.psfive_similar_games_.size());
        for (size_t i = 0; i < 6; ++i) app.renderer_.setPsfiveShot(i, shareImage(pngFixture(static_cast<uint8_t>(50 + i * 30), 192, 108)));
    };
    fill(game);
    app.openDetails(game); settle();
    if (app.screen_ != ScreenMode::details || !app.detailsMediaReady()) return 22002;
    {
        const auto actions = app.detailsActions();
        if (actions.shots != 6 || actions.similar != 5 || app.details_scroll_ != 0) return 22003;
    }
    if (!capture(L"details-media-top.png")) return 22004;
    // Down from the buttons: the first screenshot, the page up so both strips show above the hints.
    app.changeCategory(1); settle();
    if (app.detail_action_ != details_layout::shot_action) return 22005;
    const auto view = app.renderer_.viewport();
    {
        const auto layout = app.detailsLayout();
        const auto actions = app.detailsActions();
        if (app.details_scroll_ < 100 || std::abs(app.details_scroll_ - layout.mediaScroll(actions)) > 1) return 22006;
        if (layout.similar(actions, 0).bottom > view.height - 60 || layout.shot(actions, 0).top < 0) return 22007;
    }
    if (!capture(L"details-media-shots.png")) return 22008;
    // Right along the screenshots: the strip slides to keep the focus in view.
    for (int i = 0; i < 5; ++i) app.navigate(1);
    settle();
    if (app.detail_action_ != details_layout::shot_action + 5 || app.psfive_shots_scroll_ <= 0) return 22009;
    if (app.detailsLayout().shot(app.detailsActions(), 5).right > view.width) return 22010;
    if (!capture(L"details-media-scrolled.png")) return 22011;
    // Enter: full screen; Left walks back one; Back closes it and keeps the focus on that picture.
    app.launch(); settle();
    if (!app.psfive_shot_view_ || app.psfive_shot_ != 5 || app.screen_ != ScreenMode::details) return 22012;
    app.navigate(-1); settle();
    if (app.psfive_shot_ != 4 || !app.psfive_shot_view_) return 22013;
    if (!capture(L"details-media-viewer.png")) return 22014;
    app.back(); settle();
    if (app.psfive_shot_view_ || app.screen_ != ScreenMode::details || app.detail_action_ != details_layout::shot_action + 4) return 22015;
    // Down to the similar games; Back from a strip: the buttons again, the page back at the top.
    app.changeCategory(1); settle();
    if (!details_layout::similarAction(app.detail_action_)) return 22016;
    if (!capture(L"details-media-similar.png")) return 22017;
    app.back(); settle();
    if (app.screen_ != ScreenMode::details || app.detail_action_ != 0 || app.details_scroll_ != 0) return 22018;
    // A similar game's page: Enter opens it in place; Back goes home, not to the first game's page.
    app.changeCategory(1); app.changeCategory(1); settle();
    const int similar = app.detail_action_ - details_layout::similar_action;
    if (similar < 0 || similar >= 5) return 22019;
    const auto other = app.psfive_similar_games_[static_cast<size_t>(similar)];
    app.launch(); settle();
    if (app.screen_ != ScreenMode::details || gameKey(app.cards_[0].game) != gameKey(other) || app.detail_action_ != 0 || app.details_scroll_ != 0) return 22020;
    app.back(); settle();
    if (app.screen_ != ScreenMode::shelves) return 22021;
    // Switched off in Interfejs: no strips, Down stays on the page's own targets.
    app.settings_.details_media = false;
    fill(game);
    app.openDetails(game); settle();
    if (app.detailsMediaReady() || app.detailsActions().shots) return 22022;
    app.changeCategory(1); settle();
    if (app.detailsMediaFocused() || app.details_scroll_ != 0) return 22023;
    app.back(); settle();
    app.settings_.details_media = true;
    // Desktop: the sections under the columns; a screenshot opens the viewer, a click closes it;
    // a similar game opens its page.
    app.desktop_mode_ = true; app.screen_ = ScreenMode::shelves; app.buildDesktopSidebar(); app.rebuildCards(true); settle();
    { // A wide cover whose square crop (Kadruj) is its right end, opaque only there: Podobne gry show that end.
        auto message = std::make_unique<CoverMessage>();
        message->key = gameKey(app.public_library_[4]); message->generation = app.jobs_->generation.load();
        message->kind = CoverMessage::Kind::cover; message->bytes = pngFixture(220, 128, 32, false, WICRect{96, 0, 32, 32});
        message->focus = {1.0f, 0.5f};
        app.onCover(std::move(message));
    }
    fill(game);
    app.openDetails(game); settle();
    if (app.screen_ != ScreenMode::details || !app.detailsMediaReady()) return 22024;
    using Kind = desktop_ui::SheetHit::Kind;
    app.paint(); // The page measured with its sections, then scrolled to its end.
    app.desktop_sheet_scroll_ = app.desktop_sheet_scroll_target_ = 2000; settle();
    app.paint();
    const auto spot = [&](desktop_ui::DetailsExtra which, int index) {
        const auto& spots = app.renderer_.desktopHotspots();
        return std::any_of(spots.begin(), spots.end(), [&](const auto& item) {
            return item.hit == desktop_ui::SheetHit{Kind::tile, index, static_cast<int>(which)}; });
    };
    if (!spot(desktop_ui::DetailsExtra::shot, 5) || !spot(desktop_ui::DetailsExtra::similar, 4)) return 22025;
    if (!capture(L"details-media-desktop.png")) return 22026;
    app.desktopPageActivate({Kind::tile, 2, static_cast<int>(desktop_ui::DetailsExtra::shot)}, false); settle();
    if (!app.psfive_shot_view_ || app.psfive_shot_ != 2) return 22027;
    app.paint();
    if (!spot(desktop_ui::DetailsExtra::viewer_next, -1) || !capture(L"details-media-desktop-viewer.png")) return 22028;
    app.desktopPageActivate({Kind::tile, -1, static_cast<int>(desktop_ui::DetailsExtra::viewer_next)}, false);
    if (app.psfive_shot_ != 3) return 22029;
    app.desktopPageActivate({Kind::tile, -1, static_cast<int>(desktop_ui::DetailsExtra::viewer)}, false);
    if (app.psfive_shot_view_) return 22030;
    app.desktopPageActivate({Kind::tile, 1, static_cast<int>(desktop_ui::DetailsExtra::similar)}, false); settle();
    if (app.screen_ != ScreenMode::details || gameKey(app.cards_[0].game) != gameKey(app.public_library_[5])) return 22031;
    app.desktop_mode_ = false;
    return 0;
}
