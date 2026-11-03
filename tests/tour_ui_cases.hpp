// First-run walkthrough (src/app/tour.cpp): each step opens its real panel, the tour takes the
// input meant for the home screen, the last step picks the layout, finishing stores tour_done
// and Settings → Wygląd → Przewodnik runs it again. Captures: tour-*.png.
int LauncherUiTest::tour(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    app.foreground_ = [](HWND) { return true; };
    app.read_sensors_ = [] { return sensors::Reading{true, 48, 61}; };
    app.discord_.running = [] { return false; };
    if (!app.renderer_.initialize(window)) return 2400;
    const auto view = app.renderer_.viewport();
    wchar_t folder[4096]{};
    const auto folder_length = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    const auto capture = [&](const std::wstring& name) {
        settle();
        app.tour_.step_at = GetTickCount64() - 1000; // Past the step's fade-in.
        app.notifications_.clear();
        return !folder_length || folder_length >= 4096 ||
            app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 8; ++i) { Game game{9000 + i, L"Tour fixture " + std::to_wstring(i)}; game.last_played = 900 - i; library->games.push_back(game); }
    for (int i = 0; i < 2; ++i) {
        SteamCollection collection{L"Tour collection " + std::to_wstring(i), {9000u + i, 9001u + i, 9002u + i}};
        collection.id = "tour-" + std::to_string(i);
        library->collections.push_back(std::move(collection));
    }
    app.onLibrary(std::move(library));
    app.showCategory(0);
    if (app.categories_.size() < 2 || app.settings_.tour_done != true) return 2401;
    app.settings_.tour_done = false;
    app.startTour();
    if (!app.tourOpen() || app.tour_.step != 0 || app.quick_open_ || app.friends_open_ || app.category_menu_open_) return 2402;
    // Keys meant for the home screen belong to the tour.
    app.tourKey(VK_TAB, false); app.tourKey(VK_UP, false); app.tourKey(VK_ESCAPE, false);
    if (app.friends_open_ || app.quick_open_ || app.tour_.step != 0) return 2403;
    if (!capture(L"tour-welcome.png")) return 2404;
    app.tourKey(VK_RETURN, false);
    if (app.tour_.step != 1 || !app.category_menu_open_) return 2405;
    if (!capture(L"tour-categories.png")) return 2406;
    app.tourKey(VK_RETURN, false);
    if (app.tour_.step != 2 || !app.friends_open_ || app.category_menu_open_) return 2407;
    if (!capture(L"tour-drawer.png")) return 2408;
    app.tourKey(VK_RETURN, false);
    if (app.tour_.step != 3 || !app.quick_open_ || app.friends_open_ || app.quick_selected_ != 0) return 2409;
    if (!capture(L"tour-quick-bar.png")) return 2410;
    // The quick bar: Next and Back walk its icons one by one; they also advance on their own.
    app.tourKey(VK_RETURN, false);
    if (app.quick_selected_ != 1 || app.tour_.step != 3) return 2411;
    app.tourKey(VK_ESCAPE, false);
    if (app.quick_selected_ != 0 || app.tour_.step != 3) return 2412;
    app.tour_.icon_at = GetTickCount64() - 3000; app.tick();
    if (app.quick_selected_ != 1) return 2413;
    const int icons = app.tourIcons();
    while (app.quick_selected_ + 2 < icons) app.tourKey(VK_RETURN, false);
    if (quick_menu::action(app.quick_selected_, quick_menu::store_slots) != quick_menu::Action::settings) return 2414;
    if (!capture(L"tour-quick-bar-settings.png")) return 2415;
    app.tourKey(VK_RETURN, false);
    if (app.quick_selected_ != icons - 1 || app.tour_.step != 3) return 2416;
    app.tourKey(VK_RETURN, false);
    if (app.tour_.step != 4 || !app.menu_open_ || app.quick_open_ || !isGame(app.cards_[app.selected_].game)) return 2417;
    if (!capture(L"tour-menu.png")) return 2418;
    app.tourKey(VK_RETURN, false);
    if (app.tour_.step != 5 || app.menu_open_) return 2419;
    {
        TourView step; app.tourView(step);
        if (step.tag.empty() || step.target != TourView::Target::none || step.body.empty()) return 2420;
    }
    if (!capture(L"tour-suspend.png")) return 2421;
    // Back from a later step opens the earlier panel again; Back on the quick bar starts at its last icon.
    app.tourKey(VK_ESCAPE, false);
    if (app.tour_.step != 4 || !app.menu_open_) return 2422;
    app.tourKey(VK_ESCAPE, false);
    if (app.tour_.step != 3 || !app.quick_open_ || app.quick_selected_ != icons - 1 || app.menu_open_) return 2423;
    app.tourKey(VK_RETURN, false); app.tourKey(VK_RETURN, false); app.tourKey(VK_RETURN, false);
    if (app.tour_.step != 6 || app.menu_open_ || !app.tour_.tiles) return 2424;
    // The last step: Left/Right choose a layout at once and the home screen behind follows.
    app.tourKey(VK_RIGHT, false);
    if (app.settings_.home_layout != AppSettings::HomeLayout::xmb || !app.xmbActive() || app.tour_.preview.option != 1) return 2425;
    app.tour_.preview.started = GetTickCount64() - 1250;
    if (!capture(L"tour-layout.png")) return 2426;
    // Mouse: hovering a tile only previews it, a click picks it.
    const tour::Layout layout{view.width, view.height};
    const auto flix = layout.tile(2);
    const float flix_x = (flix.left + flix.right) * 0.5f * view.scale, flix_y = (flix.top + flix.bottom) * 0.5f * view.scale;
    app.mouseMove(flix_x, flix_y);
    if (app.tour_.preview.option != 2 || app.settings_.home_layout != AppSettings::HomeLayout::xmb || !app.hoverButton(flix_x, flix_y)) return 2427;
    app.click(flix_x, flix_y, false);
    if (app.settings_.home_layout != AppSettings::HomeLayout::flix || !app.flixActive()) return 2428;
    app.mouseLeave();
    app.tourKey(VK_LEFT, false); app.tourKey(VK_LEFT, false);
    if (app.settings_.home_layout != AppSettings::HomeLayout::shelves || app.xmbActive() || app.flixActive()) return 2429;
    // Down reaches the buttons; "Gotowe" ends the tour and stores it.
    app.tourKey(VK_DOWN, false);
    if (app.tour_.tiles || app.tour_.button != tour::next) return 2430;
    app.tourKey(VK_RETURN, false);
    if (app.tourOpen() || !app.settings_.tour_done || !app.settings_dirty_ || app.quick_open_ || app.friends_open_) return 2431;
    app.saveSettings();
    if (!loadAppSettings().tour_done) return 2432;
    settle();
    if (app.tour_.alpha != 0) return 2433;
    // Settings → Wygląd: the last row resets the state and starts the walkthrough over the home screen.
    app.openSettings();
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::system)); // "Pomoc": the last row.
    app.buildSettingsRows();
    if (app.settings_rows_.back().id != settings_ui::Id::tour || app.settings_rows_.back().control != settings_ui::Control::action) return 2434;
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(app.settings_rows_.size()) - 1;
    app.settingsActivate();
    if (!app.tourOpen() || app.settingsOpen() || app.screen_ != ScreenMode::shelves || app.tour_.step != 0 ||
        app.settings_.tour_done || loadAppSettings().tour_done) return 2435;
    // Pad: Start and Guide stay with the tour, A is Next, B is Back.
    app.overlay_backend_.foreground = [&] { return window; };
    PadReading pad{true, L"tour-fixture"}; app.controller(pad);
    pad.buttons = XINPUT_GAMEPAD_START; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    if (app.friends_open_ || app.tour_.step != 0) return 2436;
    pad.buttons = XINPUT_GAMEPAD_A; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    if (app.tour_.step != 1) return 2437;
    pad.buttons = XINPUT_GAMEPAD_B; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    if (app.tour_.step != 0) return 2438;
    // XMB: the categories are its top row; the card moves to the bottom to keep it visible.
    app.setHomeLayout(1); app.rebuildCards();
    app.tourKey(VK_RETURN, false);
    {
        TourView step; app.tourView(step);
        if (app.tour_.step != 1 || app.category_menu_open_ || step.target != TourView::Target::xmb_row || !step.card_bottom) return 2439;
        const tour::Layout bottom{view.width, view.height, true};
        const auto next = bottom.button(tour::next);
        if (!app.hoverButton((next.left + 4) * view.scale, (next.top + 4) * view.scale) || next.bottom > view.height) return 2440;
    }
    if (!capture(L"tour-xmb-categories.png")) return 2441;
    // "Pomiń" ends it at once, from any step.
    const tour::Layout bottom{view.width, view.height, true};
    const auto skip = bottom.button(tour::skip);
    app.click((skip.left + skip.right) * 0.5f * view.scale, (skip.top + skip.bottom) * 0.5f * view.scale, false);
    if (app.tourOpen() || !app.settings_.tour_done) return 2442;
    app.setHomeLayout(0); app.rebuildCards();
    return 0;
}
