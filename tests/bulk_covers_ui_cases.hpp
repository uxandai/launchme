int LauncherUiTest::bulkCovers(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 2800;
    app.screen_ = ScreenMode::settings;
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::media));
    app.buildSettingsRows();
    const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::bulk_covers; });
    if (row == app.settings_rows_.end()) return 2801;
    app.settings_state_.options = true;
    app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
    app.public_library_ = {Game{3393110,L"AION 2"}};
    auto calls = std::make_shared<std::atomic_int>(0);
    app.bulk_ops_ = bulk_covers::Operations{
        [](const Game&, ArtworkKind kind) { return kind == ArtworkKind::cover; },
        [calls](const Game&, ArtworkKind) -> bulk_covers::Attempt { ++*calls; return {bulk_covers::Result::retry, 60, L"SteamGridDB — Retry-After"}; }
    };
    const auto wait = [&](const auto& predicate) {
        const auto end = GetTickCount64() + 4000;
        while (GetTickCount64() < end) {
            MSG msg{};
            while (PeekMessageW(&msg, window, bulk_covers_message, bulk_covers_message, PM_REMOVE))
                app.onBulkCovers(std::unique_ptr<BulkCoversMessage>(reinterpret_cast<BulkCoversMessage*>(msg.lParam)));
            if (predicate()) return true;
            Sleep(10);
        }
        return false;
    };
    app.settingsActivate();
    if (!wait([&] { return app.bulk_ui_.progress.phase == bulk_covers::Phase::ready; }) || *calls ||
        app.bulk_ui_.progress.total != 3 || app.bulk_ui_.progress.existing != 1) return 2802;
    const auto generation = app.bulk_generation_;
    app.openBulkCovers(); if (app.bulk_generation_ != generation) return 2803;
    app.bulkCoversKey(VK_RETURN, false);
    if (!wait([&] { return app.bulk_ui_.progress.phase == bulk_covers::Phase::waiting; }) || *calls != 1) return 2804;
    app.bulkCoversKey(VK_RETURN, false);
    if (!app.bulk_control_->pause || !app.bulk_ui_.paused) return 2805;
    {
        wchar_t folder[4096]{};
        if (GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096))
            if (!app.renderer_.captureForTest(std::filesystem::path(folder) / L"bulk-covers.png", [&] { app.paint(); })) return 2806;
    }
    app.back();
    if (!wait([&] { return app.bulk_ui_.progress.terminal(); }) || !app.bulk_control_->cancel ||
        app.bulk_ui_.progress.phase != bulk_covers::Phase::cancelled || *calls != 1) return 2807;
    if (app.bulk_ui_.buttons() != 1 || app.bulk_ui_.focus != 0) return 2810;
    app.bulkCoversKey(VK_ESCAPE, false);
    if (app.bulkCoversOpen() || !app.settingsOpen()) return 2808;
    auto stale = std::make_unique<BulkCoversMessage>(); stale->generation = generation - 1; stale->progress.total = 1000;
    app.onBulkCovers(std::move(stale));
    if (app.bulk_ui_.progress.total != 3) return 2809;
    // Complete a fresh pass with only a missing GOG landscape. Revisit cached fallbacks immediately.
    Game gog; gog.kind = GameKind::store; gog.source = GameSource::gog; gog.source_id = "bulk-landscape"; gog.name = L"GOG landscape fixture";
    app.settings_.enabled_pc_sources.insert("gog"); app.store_library_ = {gog}; app.buildShelves();
    const auto cache = coverCacheDirectory();
    const auto key = gameKey(gog) + ':' + std::to_string(static_cast<int>(CoverMessage::Kind::landscape));
    const auto horizontal = pngFixture(80, 92, 43);
    app.art_.at(gameKey(gog)).hero = shareImage(pngFixture(20, 96, 54));
    app.jobs_->requested.insert(key); app.jobs_->blocked_until[key] = std::chrono::steady_clock::now() + std::chrono::hours(1);
    app.bulk_ops_ = bulk_covers::Operations{
        [cache](const Game& game, ArtworkKind kind) { return kind != ArtworkKind::landscape || cachedArtworkAvailable(game, kind, cache); },
        [cache, horizontal](const Game& game, ArtworkKind kind) -> bulk_covers::Attempt {
            const auto file = gameArtworkPath(game, kind, cache);
            std::filesystem::create_directories(file.parent_path());
            std::ofstream out(file, std::ios::binary); out.write(reinterpret_cast<const char*>(horizontal.data()), static_cast<std::streamsize>(horizontal.size()));
            return {out.good() ? bulk_covers::Result::saved : bulk_covers::Result::failed};
        }
    };
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    app.openBulkCovers();
    if (!wait([&] { return app.bulk_ui_.progress.phase == bulk_covers::Phase::ready; }) ||
        app.bulk_ui_.progress.total != 3 || app.bulk_ui_.progress.existing != 2 || !capture(L"bulk-covers-ready.png")) return 2811;
    app.bulkCoversKey(VK_RETURN, false);
    if (!wait([&] { return app.bulk_ui_.progress.terminal(); }) || app.bulk_ui_.progress.downloaded != 1 ||
        app.bulk_ui_.progress.failed || app.jobs_->blocked_until.contains(key) || app.jobs_->requested.contains(key)) return 2812;
    if (app.bulk_ui_.buttons() != 1 || app.bulk_ui_.focus != 0 || !capture(L"bulk-covers-finished.png")) return 2813;
    for (const auto input : {VK_RIGHT, VK_DOWN, VK_TAB, VK_LEFT, VK_UP}) app.bulkCoversKey(input, false);
    if (app.bulk_ui_.focus != 0) return 2814;
    const auto view = app.renderer_.viewport();
    const bulk_covers_ui::Layout done{view.width, view.height, 1}, active{view.width, view.height, 2};
    const auto removed = active.button(1);
    if (app.hoverButton((removed.right - 10) * view.scale, (removed.top + 10) * view.scale)) return 2815;
    const auto close = done.button(0);
    app.mouse_transition_until_ = 0;
    app.click((close.left + 20) * view.scale, (close.top + 20) * view.scale, false);
    if (app.bulkCoversOpen()) return 2816;
    app.openPcLibrary(GameSource::gog); app.library_state_.options[app.libraryScope()].view = library_view::View::overview; app.rebuildCards(false);
    auto landscape = std::make_unique<CoverMessage>(); landscape->key = gameKey(gog); landscape->kind = CoverMessage::Kind::landscape;
    landscape->generation = app.jobs_->generation.load(); landscape->revision = app.art_revisions_.at(key);
    landscape->bytes = cachedArtwork(gog, ArtworkKind::landscape, cache); app.onCover(std::move(landscape));
    if (!app.art_.peek(gameKey(gog))->landscape || *app.art_.peek(gameKey(gog))->landscape != horizontal || !app.renderer_.hasPoster(0)) return 2817;
    // Biblioteka → Gry dodane ręcznie → Okładki: only those games, started at once (no Start step).
    {
        custom_games::Entry entry; entry.id = "00000000000000c1"; entry.name = L"Hand-added fixture"; entry.path = L"C:\\Games\\fixture.exe";
        app.custom_games_ = {entry}; app.custom_library_ = {custom_games::makeGame(entry, true)};
        auto planned = std::make_shared<std::vector<std::string>>();
        app.bulk_ops_ = bulk_covers::Operations{
            [planned](const Game& game, ArtworkKind) { planned->push_back(gameKey(game)); return true; },
            [](const Game&, ArtworkKind) -> bulk_covers::Attempt { return {bulk_covers::Result::saved}; }
        };
        app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library));
        app.openPlatformSettings(GameSource::drmfree); // Okładki: on DRM-free's page, after Dodaj grę.
        const auto covers = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::custom_covers; });
        if (covers == app.settings_rows_.end() || covers - app.settings_rows_.begin() != 1) return 2818;
        app.settings_state_.options = true; app.settings_state_.selected[app.settings_state_.category] = 1;
        app.settingsActivate();
        if (!app.bulkCoversOpen() || !app.bulk_ui_.started || !wait([&] { return app.bulk_ui_.progress.terminal(); }) ||
            planned->empty() || std::any_of(planned->begin(), planned->end(), [](const auto& key) { return !key.starts_with("custom:"); })) return 2819;
        app.bulkCoversKey(VK_ESCAPE, false);
        if (app.bulkCoversOpen()) return 2820;
    }
    return 0;
}
