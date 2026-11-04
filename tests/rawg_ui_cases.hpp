int LauncherUiTest::rawgFlow(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute; dispatched.clear();
    app.boundary_feedback_ = [] {};
    if (!app.renderer_.initialize(window)) return 1500;
    Game game; game.name = L"My local game"; game.kind = GameKind::store; game.source = GameSource::custom; game.source_id = "rawg-fixture";
    game.last_played = 100;
    app.custom_library_.push_back(game); app.buildShelves(); app.showCategory(0);
    app.openDetails(game);
    if (app.identifyDetailAction() != 2) return 1501;
    app.detail_action_ = app.identifyDetailAction(); app.enterDown(false); app.enterUp();
    if (!app.identifyOpen() || !app.identify_state_.busy || !dispatched.empty() || app.identify_state_.manual) return 1502;
    // The lookup runs by itself; "Dopasuj ręcznie" (Enter) shows the field and the list instead.
    app.identifyKey(VK_RETURN, false);
    if (!app.identify_state_.manual || !app.identify_state_.query_focus) return 1502;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    auto search = std::make_unique<IdentifyMessage>(); search->generation = app.identify_token_->load();
    search->search.games = {{73, L"Hades", L"2020-09-17", L"PC, Nintendo Switch"}, {74, L"Hades II", L"2025-09-25", L"PC"}};
    app.onIdentify(std::move(search));
    if (app.identify_state_.busy || app.identify_state_.query_focus || app.identify_state_.games.size() != 2 || !capture(L"rawg-identify-candidates.png")) return 1503;
    const auto stale_generation = app.identify_token_->load();
    app.back();
    auto stale = std::make_unique<IdentifyMessage>(); stale->generation = stale_generation;
    app.onIdentify(std::move(stale));
    if (app.identifyOpen() || app.screen_ != ScreenMode::details) return 1504;
    app.openIdentify();
    if (app.identify_state_.manual) return 1504;
    app.onCharacter(L'H'); app.onCharacter(L'a'); // Typing is choosing by hand.
    if (!app.identify_state_.manual) return 1504;
    if (app.identify_state_.query != L"Ha" || app.identify_state_.busy || !app.identify_state_.query_focus) return 1505;
    app.identifyKey(VK_BACK, false);
    if (app.identify_state_.query != L"H") return 1506;
    app.identifyKey(VK_RETURN, false);
    search = std::make_unique<IdentifyMessage>(); search->generation = app.identify_token_->load();
    search->search.games = {{73, L"Hades", L"2020-09-17", L"PC"}};
    app.onIdentify(std::move(search));
    app.controllerNavigate(2); // Boundary never moves focus to the details behind the modal.
    if (app.detail_action_ != 2 || !app.identifyOpen()) return 1507;
    app.launch();
    if (!app.identify_state_.applying || app.pending_launch_ || !dispatched.empty()) return 1508;
    auto result = std::make_unique<IdentifyMessage>(); result->generation = app.identify_token_->load(); result->identified = true;
    rawg::Metadata metadata; metadata.id = 73; metadata.name = L"Hades";
    metadata.details.description = L"A confirmed RAWG description for this non-Steam game.";
    metadata.details.developer = metadata.details.publisher = L"Supergiant Games"; metadata.details.genres = L"Action"; metadata.details.release_date = L"2020-09-17";
    metadata.rating = {ratings::Source::rawg, 4.5, 120, 93, 73, "hades"};
    metadata.steam_app_id = 1145360;
    result->metadata = metadata;
    for (const auto kind : {ArtworkKind::cover, ArtworkKind::hero, ArtworkKind::logo, ArtworkKind::poster, ArtworkKind::landscape})
        result->artwork.images.push_back({kind, pngFixture()});
    app.onIdentify(std::move(result));
    if (app.identifyOpen() || app.details_.developer != L"Supergiant Games" || app.details_.description.empty() ||
        !app.renderer_.hasCover(0) || !app.menuRows().hide || app.ratingDetailAction() != app.collectionsDetailAction() + 1 || app.ratingUrl() != L"https://rawg.io/games/hades") return 1509;
    const auto* artwork = app.art_.find(gameKey(game));
    if (!artwork || !artwork->cover || !artwork->hero || !artwork->logo || !artwork->poster || !artwork->landscape ||
        app.cards_[0].game.app_id || gameKey(app.cards_[0].game) != gameKey(game)) return 1514;
    // Results already in flight before identification must not overwrite the newly bound images.
    auto old_art = std::make_unique<CoverMessage>(); old_art->generation = app.jobs_->generation.load();
    old_art->key = gameKey(game); old_art->kind = CoverMessage::Kind::hero; old_art->revision = 0;
    app.onCover(std::move(old_art));
    if (!app.art_.at(gameKey(game)).hero) return 1515;
    app.cards_[0].cover_opacity = 1;
    if (!capture(L"rawg-identified-details.png")) return 1510;
    app.detail_action_ = app.ratingDetailAction(); app.launch();
    if (dispatched != std::vector<std::wstring>{L"https://rawg.io/games/hades"} || app.pending_launch_) return 1511;
    // Switching back to Flix immediately binds both its capsule and poster from the new RAM cache.
    app.settings_.home_layout = AppSettings::HomeLayout::flix; app.buildShelves(); app.showCategory(0); app.flixRequestArt();
    size_t matched = 0;
    for (size_t i = 0; i < app.cards_.size(); ++i) if (gameKey(app.cards_[i].game) == gameKey(game)) {
        ++matched; if (!app.renderer_.hasPoster(i)) return 1516;
    }
    if (!matched) return 1517;
    app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::media)); app.buildSettingsRows();
    app.revealSetting(settings_ui::Id::rawg_key); // Media i dane → "Klucze usług".
    const auto key = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) { return row.id == settings_ui::Id::rawg_key; });
    if (key == app.settings_rows_.end()) return 1512;
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(key - app.settings_rows_.begin());
    app.settingsActivate();
    for (const auto ch : std::wstring(L"synthetic-rawg-key")) app.onCharacter(ch);
    app.settingsActivate();
    if (app.editingSecret() || userSecret(L"LAUNCHME_TEST_UNUSED_KEY", L"rawg_api_key.txt") != L"synthetic-rawg-key") return 1513;
    dispatched.clear();
    return 0;
}
