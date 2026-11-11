// Included after LauncherUiTest: captures of the TV mode home layouts with a small library and
// fixture artwork, for looking at (LAUNCHME_UI_CAPTURE_DIR). Checks only that each view opens.
// Captures: tv-shots-*.png.

int LauncherUiTest::tvShots(HWND window) {
    Launcher app;
    app.settings_.launch_animation = false;
    app.settings_.glance = app.settings_.companion = false;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 9800;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) return true;
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    app.settings_.enabled_pc_sources.insert("gog");
    auto library = std::make_unique<LibraryMessage>();
    library->account = 1;
    for (uint32_t i = 0; i < 24; ++i) {
        Game game{5000 + i, L"Shot game with a longer title " + std::to_wstring(i)};
        game.last_played = i < 8 ? 100 + i : 0;
        game.installed = i % 5 != 3;
        library->games.push_back(std::move(game));
    }
    app.onLibrary(std::move(library));
    const auto deliver = [&](const Game& game, CoverMessage::Kind kind, std::vector<uint8_t> bytes) {
        auto message = std::make_unique<CoverMessage>();
        message->key = gameKey(game); message->generation = app.jobs_->generation.load(); message->kind = kind; message->bytes = std::move(bytes);
        app.onCover(std::move(message));
    };
    for (const auto& game : app.public_library_) {
        const auto tint = static_cast<uint8_t>(40 + (game.app_id % 20) * 9);
        deliver(game, CoverMessage::Kind::landscape, pngFixture(tint, 92, 43));
        deliver(game, CoverMessage::Kind::poster, pngFixture(tint, 40, 60));
    }
    const auto shoot = [&](AppSettings::HomeLayout layout, const wchar_t* name) {
        app.settings_.home_layout = layout;
        app.showCategory(0);
        settle();
        return capture(name);
    };
    if (!shoot(AppSettings::HomeLayout::shelves, L"tv-shots-shelves.png")) return 9801;
    if (!shoot(AppSettings::HomeLayout::xmb, L"tv-shots-xmb.png")) return 9802;
    // XMB: a PC platform's column (12 games, then "Zobacz wszystkie").
    for (size_t i = 0; i < app.xmb_top_.size(); ++i)
        if (app.xmb_top_[i].kind == GameKind::pc_platform) {
            app.xmbFocus(i, -1); settle();
            if (!capture(L"tv-shots-xmb-pc.png")) return 9806;
            const auto* column = app.xmbColumn(app.xmb_top_[i]);
            if (!column || column->size() != Launcher::xmb_pc_limit + 1 || column->back().kind != GameKind::all) return 9807;
            app.xmbFocus(i, 2); settle();
            if (!capture(L"tv-shots-xmb-pc-row.png")) return 9808;
            app.xmbFocus(i, static_cast<int>(column->size()) - 1); settle();
            if (!capture(L"tv-shots-xmb-pc-all.png")) return 9809;
            app.launch();
            if (app.screen_ != ScreenMode::library) return 9810;
            app.back();
            break;
        }
    if (!shoot(AppSettings::HomeLayout::metro, L"tv-shots-metro.png")) return 9803;
    app.settings_.metro_title_corner = true; // Tytuł w lewym dolnym rogu.
    if (!shoot(AppSettings::HomeLayout::metro, L"tv-shots-metro-corner.png")) return 9820;
    app.settings_.metro_title_corner = false;
    if (!shoot(AppSettings::HomeLayout::flix, L"tv-shots-flix.png")) return 9804;
    if (!shoot(AppSettings::HomeLayout::psfive, L"tv-shots-psfive.png")) return 9805;
    app.settings_.home_layout = AppSettings::HomeLayout::shelves;
    app.showCategory(0);
    app.openSearch(); settle();
    if (!capture(L"tv-shots-search-empty.png")) return 9811;
    for (const wchar_t c : std::wstring(L"title 1")) app.onCharacter(c);
    app.updateSearch(); settle();
    if (!capture(L"tv-shots-search-typed.png")) return 9812;
    // A game opened from the results remembers the query; the empty field lists it.
    if (app.cards_.empty()) return 9813;
    app.openDetails(app.cards_[0].game);
    app.back(); app.back();
    app.openSearch(); settle();
    if (app.recent_searches_.empty() || app.recent_searches_.front() != L"title 1") return 9814;
    app.changeCategory(1);
    if (!app.recentSearchFocused()) return 9815;
    if (!capture(L"tv-shots-search-recent.png")) return 9816;
    app.askForgetSearch(); settle();
    if (!capture(L"tv-shots-search-forget.png")) return 9817;
    app.exitPromptAccept(); // Anuluj.
    if (app.recent_searches_.empty()) return 9818;
    app.useRecentSearch();
    if (app.search_query_ != L"title 1" || app.cards_.empty()) return 9819;
    return 0;
}
