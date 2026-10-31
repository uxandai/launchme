// Exclusions survive scans/restarts, never enter Ctrl+H, and affect only the selected store copy.
int LauncherUiTest::excludedGames(HWND window) {
    using context_menu::Item;
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    app.settings_.menu_dialog = false; // Clicks land on the anchored menu's rows.
    if (!app.renderer_.initialize(window)) return 3100;
    app.settings_.enabled_pc_sources = {"gog", "epic", "ea"}; app.settings_.show_uninstalled = true;
    Game gog; gog.kind = GameKind::store; gog.source = GameSource::gog; gog.source_id = "exclude-fixture";
    gog.name = L"GOG exclusion fixture"; gog.install_directory = L"C:\\Synthetic game"; gog.last_played = 100;
    Game epic = gog; epic.source = GameSource::epic; epic.name = L"Epic exclusion fixture"; epic.installed = false;
    Game private_game = gog; private_game.source_id = "private-fixture"; private_game.name = L"Private exclusion fixture";
    private_game.visibility = GameVisibility::hidden;
    app.store_library_ = {gog, epic}; app.store_private_library_ = {private_game};
    auto library = std::make_unique<LibraryMessage>(); library->account = 11; library->games = {Game{620, L"Portal 2"}};
    app.onLibrary(std::move(library));
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[32768]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, static_cast<DWORD>(std::size(folder)))) return true;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto present = [&](const auto& games, const Game& wanted) {
        return std::any_of(games.begin(), games.end(), [&](const Game& g) { return gameKey(g) == gameKey(wanted); });
    };
    const auto absent = [&](const Game& game) {
        if (present(app.public_library_, game) || present(app.private_category_.games, game) || present(app.grouped_library_, game)) return false;
        for (const auto& category : app.categories_) if (present(category.games, game)) return false;
        return searchPublicGames(app.public_library_, game.name, true).empty();
    };
    const auto options = [&] {
        app.detail_action_ = 1; app.enterDown(false); app.enterUp();
        return app.menu_open_ && app.libraryMenuAnchor().has_value();
    };
    const auto menuClick = [&](Item item) {
        const auto view = app.renderer_.viewport();
        const auto bounds = context_menu::bounds(app.cards_[0], app.menuRows(), app.menuBeside(), view.width, view.height, app.libraryMenuAnchor());
        const int row = context_menu::row(item, app.menuRows());
        if (row < 0) return false;
        app.mouse_transition_until_ = 0;
        const float x = (bounds.left + 30) * view.scale, y = (bounds.top + 8 + row * 53 + 24) * view.scale;
        if (!app.hoverButton(x, y)) return false;
        app.click(x, y, false); return true;
    };
    app.openPcLibrary(GameSource::gog); app.openDetails(gog);
    // Artwork, folder, Skróty (installed: its shortcuts), Edytuj metadane, hide, exclude and (installed) uninstall.
    if (!options() || context_menu::rows(app.menuRows()) != 7 || context_menu::row(Item::shortcuts, app.menuRows()) != 2 ||
        context_menu::row(Item::uninstall, app.menuRows()) != 6) return 3101;
    app.menu_progress_ = 1;
    if (!capture(L"details-options-exclude.png")) return 3102;
    const auto launches = dispatched.size();
    if (!menuClick(Item::folder) || app.menu_open_ || dispatched.size() != launches + 1 || dispatched.back() != gog.install_directory.wstring()) return 3103;
    if (!options()) return 3104;
    app.back(); // Escape/B closes only the menu; the options button keeps focus.
    if (app.menu_open_ || app.screen_ != ScreenMode::details || app.detail_action_ != 1) return 3105;
    if (!options() || !menuClick(Item::exclude) || app.screen_ == ScreenMode::details || !absent(gog) || app.cards_.size()) return 3106;
    if (!present(app.store_library_, gog) || !app.library_state_.excluded_games.contains(gameKey(gog))) return 3107;
    app.togglePrivateCategory();
    if (!app.private_shelf_ || !absent(gog) || !present(app.public_library_, private_game)) return 3108;
    // Uninstalled Epic: no folder. Keyboard/pad selection acts on the menu, not the page beneath.
    app.openPcLibrary(GameSource::epic); app.openDetails(epic);
    if (!options() || context_menu::rows(app.menuRows()) != 4 || context_menu::row(Item::folder, app.menuRows()) != -1) return 3109;
    app.changeCategory(context_menu::row(Item::exclude, app.menuRows())); app.enterDown(false); app.enterUp();
    if (!absent(epic) || app.screen_ == ScreenMode::details || dispatched.size() != launches + 1) return 3110;
    // A game already hidden by its store must also disappear from the private category.
    app.openLibrary(); app.openDetails(private_game);
    if (!options() || !menuClick(Item::exclude) || !absent(private_game)) return 3111;
    const auto file = userConfigDirectory() / L"exclusions-roundtrip.ini";
    if (!saveLibraryState(app.library_state_, file)) return 3112;
    const auto saved = loadLibraryState(file);
    if (!saved) return 3112;
    app.library_state_ = *saved;
    auto scan = std::make_unique<PcLibraryMessage>(); scan->generation = app.pc_token_->load();
    scan->library.visible = {gog, epic}; scan->library.private_games = {private_game};
    app.onPcLibrary(std::move(scan)); // A fresh, unfiltered platform snapshot cannot undo exclusions.
    if (!absent(gog) || !absent(epic) || !absent(private_game)) return 3113;
    // Every unsupported platform rejects exclusion in both the menu and the command.
    for (const auto source : {GameSource::steam, GameSource::ea, GameSource::battlenet, GameSource::ubisoft, GameSource::xbox, GameSource::custom, GameSource::drmfree}) {
        auto other = gog; other.source = source;
        app.openLibrary(); app.openDetails(other);
        if (app.canExclude(other) || context_menu::row(Item::exclude, app.menuRows()) != -1) return 3114;
        app.excludeGame(other);
        if (app.library_state_.excluded_games.size() != 3) return 3115;
    }
    app.back(); app.openSettings();
    const auto manage = [&](GameSource source) {
        app.openPlatformSettings(source);
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::pc_excluded; });
        if (row == app.settings_rows_.end()) return false;
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
        app.settingsActivate(); return app.libraryDialogOpen();
    };
    if (!manage(GameSource::gog) || app.library_dialog_.rows.size() != 2) return 3116;
    if (!capture(L"excluded-games-gog.png")) return 3117;
    app.library_dialog_.focus = -2;
    for (const auto ch : std::wstring(L"GOG")) app.onCharacter(ch);
    if (app.library_dialog_.rows.size() != 1 || app.library_dialog_.rows[0].key != gameKey(gog)) return 3118;
    app.libraryDialogKey(VK_DOWN, false); app.libraryDialogKey(VK_RETURN, false);
    if (!present(app.public_library_, gog) || !app.library_dialog_.rows.empty() || !absent(epic)) return 3119;
    app.back();
    if (app.libraryDialogOpen() || app.settings_state_.platform() != GameSource::gog) return 3120;
    std::erase_if(app.store_library_, [](const Game& game) { return game.source == GameSource::epic; });
    app.buildShelves(); // The title remains manageable after disconnecting/removing the account library.
    if (!manage(GameSource::epic) || app.library_dialog_.rows.size() != 1) return 3121;
    if (app.library_dialog_.rows[0].title != epic.name) return 3121;
    app.libraryDialogKey(VK_RETURN, false);
    if (app.library_state_.excluded_games.contains(gameKey(epic)) || !app.library_dialog_.rows.empty()) return 3122;
    if (!capture(L"excluded-games-empty.png")) return 3123;
    app.back(); app.openPlatformSettings(GameSource::ea);
    if (std::any_of(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::pc_excluded; })) return 3124;
    // Exclusion from search returns to the same query with no stale result card, including Ctrl+H.
    app.store_library_.push_back(epic); app.buildShelves(); app.showCategory(0); app.openSearch();
    app.search_query_ = L"Epic exclusion"; app.updateSearch();
    if (app.cards_.size() != 1) return 3128;
    app.openDetails(app.cards_[0].game);
    if (!options() || !menuClick(Item::exclude) || app.screen_ != ScreenMode::search || !app.cards_.empty() || !app.search_results_.empty()) return 3129;
    app.togglePrivateCategory();
    if (!app.cards_.empty() || !app.search_results_.empty()) return 3130;
    // A merged game: exclude the selected Epic copy, keep the Steam copy and its title searchable.
    app.library_state_.excluded_games.erase(gameKey(epic));
    epic.name = L"Portal 2"; app.store_library_ = {epic}; app.buildShelves(); app.openLibrary(); app.openDetails(epic);
    if (!options() || !menuClick(Item::exclude)) return 3125;
    const auto results = searchPublicGames(app.public_library_, L"Portal 2", true);
    if (results.size() != 1 || results[0].source != GameSource::steam || present(app.public_library_, epic)) return 3126;
    // Unreadable history must not be replaced by a new exclusion list.
    app.library_state_readable_ = false; app.excludeGame(gog);
    if (app.library_state_.excluded_games.contains(gameKey(gog))) return 3127;
    return 0;
}
