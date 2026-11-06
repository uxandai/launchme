// Actual navigation routes with a fake rumble sink; real settings picker and renderer.
int LauncherUiTest::boundaryAndShapes(HWND window) {
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 1400;
    int taps = 0;
    app.boundary_feedback_ = [&] { ++taps; };
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 18; ++i) {
        Game game{6000 + i, L"Shape fixture " + std::to_wstring(i)}; game.last_played = 100;
        library->games.push_back(game);
    }
    library->collections.push_back({L"Collection", {6000, 6001, 6002}});
    app.onLibrary(std::move(library));
    app.focusCard(app.cards_.size() - 1);
    app.navigate(1); // Keyboard/programmatic boundary must never rumble.
    if (taps) return 1401;
    app.controllerNavigate(1); app.controllerNavigate(1); app.controllerNavigate(1);
    if (taps != 1) return 1402;
    app.controllerNavigate(0); app.controllerNavigate(1);
    if (taps != 2) return 1403; // Release rearms the tap.
    app.controllerNavigate(-1); app.controllerNavigate(1);
    if (taps != 2) return 1404; // Normal motion, including arriving on the last item.
    app.controllerNavigate(1);
    if (taps != 3) return 1405;
    app.focusCard(0); app.controllerNavigate(0); app.controllerNavigate(-1);
    if (!app.category_menu_open_ || taps != 3) return 1406; // Opening a drawer is not a boundary.
    app.closeCategoryMenu(); app.openLibrary();
    app.focusCard(static_cast<size_t>(app.libraryColumns() - 1));
    app.controllerNavigate(1); app.controllerNavigate(1);
    if (taps != 4) return 1407; // Grid row edge, not just the last game in the entire library.
    app.library_controls_.options.view = library_view::View::list; app.applyLibraryOptions();
    app.focusCard(app.cards_.size() - 1); app.controllerNavigate(0);
    app.controllerNavigate(2); app.controllerNavigate(2);
    if (taps != 5) return 1408;
    for (const auto layout : {AppSettings::HomeLayout::xmb, AppSettings::HomeLayout::flix, AppSettings::HomeLayout::metro}) {
        app.settings_.home_layout = layout; app.buildShelves(); app.showCategory(0);
        if (app.xmbActive()) app.xmbFocus(app.xmb_top_.size() - 1, -1);
        else if (app.metroActive()) app.metroFocus(app.cards_.size() - 1); // The last page's last tile.
        else app.flixFocus(0, app.flix_rows_[0].count - 1);
        const int before = taps;
        app.controllerNavigate(0); app.controllerNavigate(1); app.controllerNavigate(1);
        if (taps != before + 1) return 1409;
    }
    app.openSettings(); app.settings_state_.selectCategory(0); app.buildSettingsRows();
    app.controllerNavigate(0); const int before = taps;
    app.controllerNavigate(-2); app.controllerNavigate(-2);
    if (taps != before + 1) return 1410;
    app.openSettingsPage(settings_ui::Page::appearance_all); // The classic list of every appearance setting.
    const auto shape_row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(),
        [](const auto& row) { return row.id == settings_ui::Id::tile_shape; });
    if (shape_row == app.settings_rows_.end() || shape_row->choices.size() != 3) return 1411;
    const int row_index = static_cast<int>(shape_row - app.settings_rows_.begin());
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[0] = row_index; app.ensureSettingVisible();
    const auto capture = [&](const std::wstring& name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    app.settingsActivate();
    if (!capture(L"settings-tile-shapes.png")) return 1412;
    app.settingsMove(2); app.back();
    if (app.settings_.tile_shape != AppSettings::TileShape::squircle) return 1413; // Cancel is side-effect free.
    for (int choice = 0; choice < 3; ++choice) {
        app.openSettings(); app.settings_state_.selectCategory(0); app.buildSettingsRows();
        app.openSettingsPage(settings_ui::Page::appearance_all);
        app.settings_state_.enter(static_cast<int>(app.settings_rows_.size())); app.settings_state_.selected[0] = row_index;
        app.settingsActivate(); app.settingsMove(choice); app.settingsActivate();
        if (app.settings_.tile_shape != static_cast<AppSettings::TileShape>(choice)) return 1414;
        app.saveSettings();
        if (loadAppSettings().tile_shape != app.settings_.tile_shape) return 1415;
        for (const auto layout : {AppSettings::HomeLayout::shelves, AppSettings::HomeLayout::xmb, AppSettings::HomeLayout::flix, AppSettings::HomeLayout::metro}) {
            app.settings_.home_layout = layout; app.buildShelves(); app.showCategory(0); app.focusCard(0);
            // Fixture artwork exercises clipping as well as outlines and shadows.
            for (size_t i = 0; i < app.cards_.size(); ++i) {
                app.renderer_.setCover(i, pngFixture(80, 48, 48), {});
                app.renderer_.setPoster(i, shareImage(pngFixture(80, 48, 72)));
                app.cards_[i].cover_opacity = 1;
            }
            for (int i = 0; i < 35; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
            if (!capture(L"tiles-" + std::to_wstring(choice) + L"-layout-" + std::to_wstring(static_cast<int>(layout)) + L".png")) return 1416;
        }
        // Next picker opens at the current choice; reset selection via settings, below.
        app.openSettings(); app.settings_state_.selectCategory(0); app.buildSettingsRows();
        app.openSettingsPage(settings_ui::Page::appearance_all);
        app.settings_state_.enter(static_cast<int>(app.settings_rows_.size())); app.settings_state_.selected[0] = row_index;
        app.settingsActivate(); app.settingsMove(-2); app.settingsActivate();
    }
    // Window borders: the first of "Okna i podpowiedzi", off by default, saved like the others.
    app.openSettings(); app.settings_state_.selectCategory(0); app.buildSettingsRows();
    app.openSettingsPage(settings_ui::Page::appearance_all);
    const auto borders_row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(),
        [](const auto& row) { return row.id == settings_ui::Id::panel_borders; });
    if (borders_row == app.settings_rows_.end() || borders_row->section != tr(L"Okna i podpowiedzi") ||
        borders_row->control != settings_ui::Control::toggle || borders_row->checked || app.settings_.panel_borders) return 1417;
    const int borders_index = static_cast<int>(borders_row - app.settings_rows_.begin());
    {
        // Efekt szkła (Interfejs → "Tło i szkło", first on its page): a 5% step from 70%, saved; 0% keeps solid panels.
        if (!revealClassicSetting(app, settings_ui::Id::glass) || !app.settings_state_.at(settings_ui::Page::backdrop) ||
            app.settings_rows_[app.settings_state_.row()].id != settings_ui::Id::glass) return 1426;
        const int glass_index = app.settings_state_.row(); // After the wallpaper's rows in Tryb TV.
        app.settingsActivate(); app.settingsAdjust(-1); app.settingsActivate();
        app.saveSettings();
        if (std::abs(app.settings_.glass - 0.65f) > 0.001f || std::abs(loadAppSettings().glass - 0.65f) > 0.001f ||
            app.settings_rows_[glass_index].value != L"65%") return 1425;
        app.settings_.glass = AppSettings{}.glass; app.settingsChanged(); app.saveSettings();
        app.closeSettingsPage();
    }
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size())); app.settings_state_.selected[0] = borders_index;
    app.settingsActivate();
    if (!app.settings_.panel_borders || !app.settings_rows_[borders_index].checked) return 1418;
    app.saveSettings();
    if (!loadAppSettings().panel_borders) return 1419;
    app.settingsActivate();
    if (app.settings_.panel_borders || app.settings_rows_[borders_index].checked) return 1420;
    app.saveSettings();
    // Console tiles: in "Kafelki" (opened by the tile shape), on by default; the toggle keeps the
    // shelves (a visual setting only) and is saved.
    const auto consoles_row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(),
        [](const auto& row) { return row.id == settings_ui::Id::console_colors; });
    if (consoles_row == app.settings_rows_.end() ||
        consoles_row - app.settings_rows_.begin() <= row_index || consoles_row->control != settings_ui::Control::toggle ||
        !consoles_row->checked || !app.settings_.console_colors) return 1421;
    const int consoles_index = static_cast<int>(consoles_row - app.settings_rows_.begin());
    app.settings_state_.selected[0] = consoles_index;
    app.settingsActivate();
    app.saveSettings();
    if (app.settings_.console_colors || app.settings_rows_[consoles_index].checked || loadAppSettings().console_colors) return 1422;
    app.openSettings(); app.settings_state_.selectCategory(0); app.buildSettingsRows();
    app.openSettingsPage(settings_ui::Page::appearance_all);
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size())); app.settings_state_.selected[0] = consoles_index;
    app.settingsActivate(); app.saveSettings();
    if (!app.settings_.console_colors || !loadAppSettings().console_colors) return 1424;
    return 0;
}
