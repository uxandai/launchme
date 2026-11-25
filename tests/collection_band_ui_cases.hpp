// Included after LauncherUiTest: Siatka's "Kolekcje jako kafle" (src/app/library_collections.cpp). The
// option from Opcje widoku, the rows under the tiles, the pad between the top bar, the tiles and the
// games, a tile opening its collection and Back returning to it, the pointer. Covers are fixtures.

int LauncherUiTest::collectionBand(HWND window) {
    Launcher app;
    app.settings_.launch_animation = false;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 13000;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) return true;
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); Sleep(320); app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    const wchar_t* titles[] = {L"Alan Wake", L"Bastion", L"Celeste", L"Dead Cells", L"Elden Ring", L"Factorio", L"Gris", L"Hades",
                               L"Inside", L"Journey", L"Katana Zero", L"Limbo", L"Minecraft", L"Noita", L"Outer Wilds", L"Portal 2",
                               L"Quake", L"Rimworld", L"Stardew Valley", L"Tunic"};
    auto library = std::make_unique<LibraryMessage>();
    library->account = 1;
    for (uint32_t i = 0; i < std::size(titles); ++i) {
        Game game{3000 + i, titles[i]};
        game.last_played = 100 + i;
        game.installed = true;
        library->games.push_back(std::move(game));
    }
    app.onLibrary(std::move(library));
    // Four collections: more than fit beside each other on a narrow screen.
    app.collections_source_ = {{L"Co-op z Olą", {3000, 3001, 3002, 3003}, "band-coop"}, {L"Roguelike", {3001, 3003, 3007}, "band-rogue"},
                               {L"Na kanapę", {3004}, "band-sofa"}, {L"Kiedyś", {3010, 3011, 3012, 3013, 3014}, "band-later"}};
    app.buildShelves();
    app.openLibrary();
    for (size_t i = 0; i < app.public_library_.size(); ++i) {
        auto cover = std::make_unique<CoverMessage>();
        cover->key = gameKey(app.public_library_[i]); cover->generation = app.jobs_->generation.load();
        cover->bytes = pngFixture(static_cast<uint8_t>(30 + i * 9), 48, 48);
        app.onCover(std::move(cover));
    }
    if (app.libraryView() != library_view::View::grid) {
        app.library_controls_.options.view = library_view::View::grid; app.applyLibraryOptions();
    }
    if (app.libraryBandShown()) return 13001; // Off until chosen.
    // Opcje widoku → Kolekcje jako kafle (Siatka's third option).
    app.openLibraryPanel(library_ui::Panel::views);
    app.library_controls_.view_row = 3; app.adjustLibraryView(1);
    app.closeLibraryPanel(); app.library_controls_.focus = -1; app.focusCard(0);
    if (!app.library_controls_.options.grid_collections || !app.libraryBandShown() || app.libraryBandTiles().size() != 4) return 13002;
    settle();
    const auto view = app.renderer_.viewport();
    const auto options = app.libraryPresentation();
    const float head = library_layout::gridHead(library_view::View::grid, view.width, options);
    if (head < 200 || std::abs(app.cards_[0].y - (190 + head)) > 1) return 13003; // The rows start under the tiles.
    if (!capture(L"collection-band.png")) return 13004;
    // Up from the first row: the tiles; Right along them; Up to the top bar, Down back to the tiles.
    app.changeCategory(-1);
    if (!app.libraryBandFocused() || app.band_focus_ != 0) return 13005;
    settle();
    for (size_t i = 0; i < app.cards_.size(); ++i) if (app.cards_[i].emphasis > 0.05f) return 13006; // No card looks focused.
    app.navigate(1); app.navigate(1); app.navigate(1); app.navigate(1);
    if (app.band_focus_ != 3) return 13007;
    settle();
    // The tiles stand in the grid's columns: the row slides only when there are more of them.
    if ((app.band_slide_ > 0) != (app.libraryBandTiles().size() > static_cast<size_t>(app.libraryColumns()))) return 13008;
    if (!capture(L"collection-band-focus.png")) return 13009;
    if (app.libraryTabsShown()) return 13022; // The tiles lead to the collections: no tabs above them.
    { // Left on the first tile opens Opcje widoku, as from the grid's first column; Back returns to the tile.
        const int last = app.band_focus_;
        app.libraryBandFocus(0); app.libraryHorizontal(-1, true);
        if (app.library_controls_.panel != library_ui::Panel::views) return 13026;
        app.back();
        if (app.library_controls_.open() || !app.libraryBandFocused() || app.band_focus_ != 0) return 13027;
        app.libraryBandFocus(last);
    }
    app.changeCategory(-1);
    if (app.libraryBandFocused() || app.library_controls_.focus != library_ui::filters_button) return 13010;
    app.changeCategory(1);
    if (!app.libraryBandFocused() || app.band_focus_ != 3) return 13011;
    // A tile opens its collection; Back returns to it.
    app.navigate(-1); app.navigate(-1);
    if (app.band_focus_ != 1) return 13012;
    app.accept();
    if (app.library_category_ != "band-rogue") return 13013;
    if (app.cards_.size() != 3) return 13020;
    if (app.libraryBandShown() || !app.libraryTabsShown()) return 13021;
    app.back();
    if (app.screen_ != ScreenMode::library || !app.library_category_.empty() || !app.libraryBandFocused() || app.band_focus_ != 1) return 13014;
    // Down: the games again.
    app.changeCategory(1);
    if (app.libraryBandFocused() || app.selected_ >= static_cast<size_t>(app.libraryColumns())) return 13015;
    // The pointer: a tile under it lights up and a click opens it.
    settle();
    const auto tile = collection_band::tile(0, view.width, app.grid_scroll_, app.band_slide_, options);
    const float x = (tile.left + tile.right) * 0.5f, y = (tile.top + tile.bottom) * 0.5f;
    if (app.libraryBandHit(x, y) != 0) return 13016;
    app.click(x * view.scale, y * view.scale, false);
    if (app.library_category_ != "band-coop") return 13017;
    app.back();
    if (!app.library_category_.empty()) return 13018;
    // Plakaty: the tiles' covers take the grid's shape; Kształt kafelków (square) their corners.
    app.library_controls_.options.grid_posters = true; app.applyLibraryOptions();
    app.renderer_.setTileShape(AppSettings::TileShape::square); app.focusCard(0); app.changeCategory(-1); settle();
    {
        const auto posters = app.libraryPresentation();
        if (!app.libraryBandFocused() || collection_band::tileHeight(view.width, posters) < collection_band::tileWidth(view.width, posters) * 1.4f) return 13023;
        ViewContext context; app.focusContext(context);
        if (!context.band_glide) return 13024; // The ring springs.
    }
    if (!capture(L"collection-band-posters.png")) return 13025;
    app.library_controls_.options.grid_posters = false; app.applyLibraryOptions();
    app.renderer_.setTileShape(AppSettings::TileShape::squircle); app.focusCard(0); settle();
    { // Scrolling: the collection tiles ride with the game tiles, gliding at their rate instead of jumping.
        const float maximum = library_layout::maxScroll(app.libraryView(), app.cards_.size(), view.width, view.height, app.libraryPresentation());
        if (maximum < 60) return 13028;
        const float before = app.band_scroll_, target = std::min(maximum, 320.0f);
        app.grid_scroll_ = target;
        app.last_frame_ -= std::chrono::milliseconds(16); app.tick();
        if (!(app.band_scroll_ > before + 1 && app.band_scroll_ < target - 1)) return 13029; // Mid-glide, not there at once.
        const float rows = 190 + library_layout::gridHead(library_view::View::grid, view.width, app.libraryPresentation());
        if (std::abs(app.cards_[0].y - (rows - app.band_scroll_)) > 2) return 13030; // The tiles and the games move together.
        settle();
        if (app.band_scroll_ != app.grid_scroll_ || std::abs(app.cards_[0].y - (rows - target)) > 1) return 13031;
        // Up to the tiles: the page glides back to the top; Down returns to the first row, not where the wheel left the focus.
        app.selected_ = std::min<size_t>(app.cards_.size() - 1, static_cast<size_t>(app.libraryColumns()) * 2 + 1);
        app.libraryBandFocus(0);
        app.last_frame_ -= std::chrono::milliseconds(16); app.tick();
        if (app.grid_scroll_ != 0 || app.band_scroll_ <= 1) return 13032;
        settle();
        app.changeCategory(1);
        if (app.libraryBandFocused() || app.selected_ != 1) return 13033;
        app.focusCard(0); settle();
    }
    // Off again: the rows return to the top.
    app.openLibraryPanel(library_ui::Panel::views);
    app.library_controls_.view_row = 3; app.adjustLibraryView(-1);
    app.closeLibraryPanel(); app.library_controls_.focus = -1; app.focusCard(0); settle();
    if (app.libraryBandShown() || std::abs(app.cards_[0].y - 190) > 1) return 13019;
    return 0;
}
