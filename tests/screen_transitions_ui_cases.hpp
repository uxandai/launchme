// No services or real games: the renderer uses real D2D targets and fixture art.
int LauncherUiTest::screenTransitions(HWND window) {
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.screen_motion_for_test_ = true;
    app.settings_.home_layout = AppSettings::HomeLayout::psfive;
    if (!app.renderer_.initialize(window)) return 5200;
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 3; ++i) {
        Game game{5200 + i, L"Wallpaper fixture " + std::to_wstring(i)};
        game.last_played = 100; library->games.push_back(game);
    }
    app.onLibrary(std::move(library));
    app.focusCard(1);
    const auto game = app.cards_[app.selected_].game;
    const auto hero = shareImage(pngFixture(130, 320, 180));
    auto message = std::make_unique<CoverMessage>();
    message->kind = CoverMessage::Kind::hero; message->key = gameKey(game);
    message->generation = app.jobs_->generation.load(); message->bytes = *hero;
    app.onCover(std::move(message));
    app.detail_ = 1; app.paint();
    auto* bitmap = app.renderer_.heroBitmapForTest(app.selected_);
    if (!bitmap || !app.renderer_.heroShownForTest()) return 5201;
    // Start the real async decoder after seeding the visible bitmap. Rebinding
    // this same image must remain ready immediately, without another decode job.
    app.renderer_.startDecoderForTest();
    const auto decoded = app.renderer_.decoderForTest().completed();
    app.openDetails(game); app.paint();
    if (app.renderer_.heroBitmapForTest(0) != bitmap || app.renderer_.heroLevelForTest() < 0.999f ||
        FAILED(app.renderer_.draw_result_for_test) || app.screen_transition_ >= 1) return 5202;
    if (app.renderer_.decoderForTest().completed() != decoded || app.renderer_.decoderForTest().pending()) return 5208;
    // Details' wallpaper motion is separate from the 350 ms UI entrance: under
    // 0.2% after 350 ms, about 1% only after several seconds, no reset on Back.
    for (int frame = 0; frame < 7; ++frame) {
        app.last_frame_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(50); app.tick();
    }
    if (app.wallpaper_zoom_ <= 1 || app.wallpaper_zoom_ >= 1.002f) return 5222;
    for (int frame = 0; frame < 120; ++frame) {
        app.last_frame_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(50); app.tick();
    }
    if (app.wallpaper_zoom_ <= 1.008f || app.wallpaper_zoom_ > 1.01f) return 5223;
    const float details_zoom = app.wallpaper_zoom_;
    app.back(); app.paint();
    if (app.wallpaper_zoom_ != details_zoom) return 5224;
    app.last_frame_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(50); app.tick();
    if (app.wallpaper_zoom_ >= details_zoom || app.wallpaper_zoom_ <= 1) return 5225;
    if (app.screen_ != ScreenMode::shelves || gameKey(app.cards_[app.selected_].game) != gameKey(game) ||
        app.renderer_.heroBitmapForTest(app.selected_) != bitmap || app.renderer_.heroLevelForTest() < 0.999f) return 5203;

    for (const auto view : {library_view::View::cinematic, library_view::View::detailed, library_view::View::grid}) {
        app.openLibrary();
        app.library_state_.options[app.libraryScope()].view = view;
        app.rebuildCards();
        const auto card = app.findCard(gameKey(game), false);
        if (!card) return 5204;
        app.focusCard(*card); app.detail_ = 1; app.paint();
        if (app.renderer_.heroBitmapForTest(app.selected_) != bitmap) return 5205;
        app.openDetails(game); app.paint();
        if (app.renderer_.heroBitmapForTest(0) != bitmap || app.renderer_.heroLevelForTest() < 0.999f ||
            app.renderer_.heroLeavingForTest() || FAILED(app.renderer_.draw_result_for_test)) return 5206;
        app.back(); app.paint();
        if (app.screen_ != ScreenMode::library || app.libraryView() != view ||
            app.renderer_.heroBitmapForTest(app.selected_) != bitmap) return 5207;
    }

    // Toggle through the actual Settings action and restore from disk. Turning it
    // off also finishes an in-flight animation; turning on takes effect next entry.
    app.openSettings(); app.paint();
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::appearance)); app.buildSettingsRows();
    app.openSettingsPage(settings_ui::Page::appearance_all); // The classic list (no tile since 0.7.6).
    app.revealSetting(settings_ui::Id::screen_transitions);
    const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(),
        [](const auto& r) { return r.id == settings_ui::Id::screen_transitions; });
    if (row == app.settings_rows_.end() || row->control != settings_ui::Control::toggle || !row->checked) return 5209;
    const int index = static_cast<int>(row - app.settings_rows_.begin());
    app.settings_state_.options = true; app.settings_state_.selected[0] = index;
    app.settingsActivate(); app.saveSettings();
    if (app.settings_.screen_transitions || app.settings_rows_[index].checked || app.screen_transition_ != 1 ||
        loadAppSettings().screen_transitions) return 5210;
    app.openLibrary(); app.paint();
    app.openDetails(game); app.paint();
    app.back(); app.paint();
    if (app.screen_transition_ != 1) return 5211;
    app.openSettings(); app.paint();
    app.settings_state_.options = true; app.settings_state_.selected[0] = index;
    app.settingsActivate(); app.saveSettings();
    if (!app.settings_.screen_transitions || !loadAppSettings().screen_transitions) return 5212;
    app.openLibrary(); app.paint();
    if (app.screen_transition_ >= 1) return 5213;

    // Compare actual pixels, not just animation state. The clear area of Details
    // must be identical at every phase: no black flash or warped second wallpaper.
    auto captures = userConfigDirectory() / L"screen-transitions";
    wchar_t directory[4096]{};
    if (const auto n = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", directory, 4096); n && n < 4096) captures = directory;
    std::filesystem::create_directories(captures);
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf())))) return 5214;
    Renderer renderer;
    if (!renderer.initialize(window)) return 5215;
    std::vector<GameCard> cards(1); cards[0].game = game;
    GameDetails details; details.description = L"Foreground over a continuous wallpaper.";
    std::vector<uint8_t> reference;
    uint64_t previous_foreground = 0;
    for (const int phase : {0, 25, 60, 100}) {
        const auto file = captures / (L"wallpaper-details-" + std::to_wstring(phase) + L".png");
        bool same_bitmap = false;
        if (!renderer.captureForTest(file, [&] {
            renderer.resetCovers(1, false); renderer.setHero(0, hero);
            ViewContext context; context.psfive = true;
            renderer.paint(cards, 0, false, 0, 1, context);
            auto* before = renderer.heroBitmapForTest(0);
            renderer.resetCovers(1, true);
            // Equal bytes in another shared buffer must reuse the decoded image too.
            renderer.setHero(0, std::vector<uint8_t>(*hero));
            same_bitmap = before && before == renderer.heroBitmapForTest(0);
            context = {}; context.screen = ScreenMode::details; context.game_details = &details;
            context.screen_transition = phase / 100.0f;
            renderer.paint(cards, 0, false, 0, 0, context);
        }) || !same_bitmap || FAILED(renderer.draw_result_for_test)) return 5216;
        std::ifstream input(file, std::ios::binary);
        const auto image = decodeImage(wic.Get(), std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {}), {});
        if (!image.valid()) return 5217;
        if (phase == 0) reference = image.pixels;
        uint64_t background = 0, foreground = 0;
        const float scale = renderer.viewport().scale;
        for (int y = int(160 * scale); y < int(260 * scale); ++y)
            for (int x = int(1150 * scale); x < int(1450 * scale); ++x)
                for (int c = 0; c < 3; ++c) {
                    const size_t p = (size_t(y) * image.width + x) * 4 + c;
                    if (std::abs(int(image.pixels[p]) - int(reference[p])) > 1) return 5218;
                    background += image.pixels[p];
                }
        if (background < 100000) return 5219;
        // Back button: foreground must really fade in (a no-op transition would
        // otherwise pass the wallpaper comparison).
        for (int y = int(35 * scale); y < int(130 * scale); ++y)
            for (int x = int(60 * scale); x < int(210 * scale); ++x)
                for (int c = 0; c < 3; ++c) {
                    const size_t p = (size_t(y) * image.width + x) * 4 + c;
                    foreground += std::abs(int(image.pixels[p]) - int(reference[p]));
                }
        if (phase && foreground <= previous_foreground) return 5220;
        previous_foreground = foreground;
    }
    // A marker in the wallpaper must keep its bounds across layouts, independent
    // of their different shades. The old 88%-height crop and 102.5% overscan moved it.
    const auto marker = shareImage(pngFixture(255, 320, 180, false, WICRect{220, 25, 40, 45}));
    for (const auto origin : {ScreenMode::shelves, ScreenMode::library}) for (const int phase : {-1, 0, 60, 100}) {
        const auto file = captures / (L"wallpaper-framing-" + std::to_wstring(static_cast<int>(origin)) + L"-" + std::to_wstring(phase) + L".png");
        if (!renderer.captureForTest(file, [&] {
            renderer.resetCovers(1, false); renderer.setHero(0, marker);
            ViewContext context; context.screen = ScreenMode::details; context.game_details = &details;
            renderer.paint(cards, 0, false, 0, 1, context);
            context = {}; context.screen = origin; context.screen_transition = 0;
            context.library_mode = library_view::View::detailed;
            renderer.paint(cards, 0, false, 0, 1, context); // Only the source background.
            if (phase >= 0) {
                context = {}; context.screen = ScreenMode::details; context.game_details = &details;
                context.screen_transition = phase / 100.0f;
                renderer.paint(cards, 0, false, 10, 1, context);
            }
        }) || FAILED(renderer.draw_result_for_test)) return 5226;
        std::ifstream input(file, std::ios::binary);
        const auto image = decodeImage(wic.Get(), std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {}), {});
        if (!image.valid()) return 5227;
        const float scale = renderer.viewport().scale;
        int peak = 0;
        for (int y = int(90 * scale); y < int(390 * scale); ++y)
            for (int x = int(1000 * scale); x < int(1450 * scale); ++x)
                peak = std::max(peak, int(image.pixels[(size_t(y) * image.width + x) * 4 + 1]));
        if (peak < 20) return 5228;
        int left = int(image.width), top = int(image.height), right = 0, bottom = 0;
        for (int y = int(90 * scale); y < int(390 * scale); ++y)
            for (int x = int(1000 * scale); x < int(1450 * scale); ++x)
                if (image.pixels[(size_t(y) * image.width + x) * 4 + 1] > peak / 2) {
                    left = std::min(left, x); right = std::max(right, x);
                    top = std::min(top, y); bottom = std::max(bottom, y);
                }
        if (std::abs(left / scale - 1100) > 3 || std::abs(top / scale - 125) > 3 ||
            std::abs((right + 1) / scale - 1300) > 3 || std::abs((bottom + 1) / scale - 350) > 3) return 5229;
    }
    // A target recreation drops GPU resources, but a newly rebound hero recovers.
    renderer.resetCovers(1, false); renderer.setHero(0, hero);
    renderer.paint(cards, 0, false, 0, 1, {});
    if (!renderer.heroBitmapForTest(0) || FAILED(renderer.draw_result_for_test)) return 5221;
    return 0;
}
