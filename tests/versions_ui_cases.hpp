// Ustawienia → Biblioteka → Łącz wersje tej samej gry: Steam, GOG and a console copy as one tile;
// Zmień wersję beside Graj (desktop pane, Szczegóły) picks the copy and the tile switches to it.
int LauncherUiTest::versionMerge(HWND window) {
    Launcher app; app.hwnd_ = window; app.execute_ = fakeExecute;
    app.settings_.launch_animation = false;
    if (!app.renderer_.initialize(window)) return 9370;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[32768]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, static_cast<DWORD>(std::size(folder)))) return true;
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    Game steam{620, L"Portal 2"};
    Game gog; gog.name = L"PORTAL 2"; gog.kind = GameKind::store; gog.source = GameSource::gog; gog.source_id = "p2"; gog.installed = true;
    Game rom; rom.name = L"Portal 2"; rom.kind = GameKind::nonsteam; rom.source = GameSource::custom; rom.platform = L"SNES";
    rom.app_id = 0x9123u; rom.shortcut_id = (uint64_t(rom.app_id) << 32) | 0x02000000ULL; rom.rom = L"C:\\roms\\Portal 2.sfc"; rom.installed = true;
    app.settings_.enabled_pc_sources.insert("gog"); app.store_library_ = {gog}; app.custom_library_ = {rom};
    auto library = std::make_unique<LibraryMessage>(); library->account = 11; library->games = {steam};
    app.onLibrary(std::move(library));
    const auto portals = [&] {
        std::vector<Game> found;
        for (const auto& card : app.cards_) if (isGame(card.game) && foldGameTitle(card.game.name) == L"portal 2") found.push_back(card.game);
        return found;
    };
    app.openLibrary();
    if (portals().size() != 1 || portals()[0].version_group.empty() || !app.versions_.find(portals()[0]) || app.versions_.find(portals()[0])->size() != 3) {
        std::cerr << "Merged tiles: " << portals().size() << '\n';
        return 9371;
    }
    // Off: every copy is its own tile.
    app.settings_.merge_versions = false; app.buildShelves(); app.openLibrary();
    if (portals().size() != 3 || app.versions_.find(rom)) return 9372;
    app.settings_.merge_versions = true; app.buildShelves();

    // The desktop pane: Zmień wersję between Graj and the star, by mouse and by keys.
    app.desktop_mode_ = true; app.screen_ = ScreenMode::shelves; app.buildDesktopSidebar(); app.rebuildCards(true);
    const auto tile = std::find_if(app.cards_.begin(), app.cards_.end(), [](const GameCard& c) { return !c.game.version_group.empty(); });
    if (tile == app.cards_.end()) return 9373;
    app.selected_ = static_cast<size_t>(tile - app.cards_.begin());
    const auto group = tile->game.version_group;
    const auto shown = gameKey(tile->game);
    if (!app.desktopLayout().versions) return 9373;
    app.desktop_zone_ = 2; app.desktop_pane_focus_ = 0; app.desktopMove(1, 0, false);
    if (app.desktop_pane_focus_ != desktop_ui::Layout::pane_versions_focus) return 9374;
    app.desktopMove(1, 0, false);
    if (app.desktop_pane_focus_ != 1) return 9374; // The star next.
    app.desktopMove(-1, 0, false); app.desktopMove(0, 1, false);
    if (app.desktop_pane_focus_ != 4) return 9374; // Down: the quick action under it.
    app.desktopMove(0, -1, false);
    if (app.desktop_pane_focus_ != desktop_ui::Layout::pane_versions_focus) return 9374; // Up: back to it.
    if (!capture(L"versions-desktop-pane.png")) return 9380;
    app.desktopActivate({desktop_ui::Target::pane_versions, 0});
    auto& ui = app.library_dialog_;
    if (!app.libraryDialogOpen() || ui.versions.size() != 3 || !ui.remember || ui.focus < 0 || gameKey(ui.versions[static_cast<size_t>(ui.focus)]) != shown) return 9375;
    const auto snes = std::find_if(ui.versions.begin(), ui.versions.end(), [](const Game& g) { return versions::emulatedCopy(g); });
    if (snes == ui.versions.end()) return 9375;
    ui.focus = static_cast<int>(snes - ui.versions.begin());
    // The logos the badge worker would load (shipped, or this PC's cache for the capture; never fetched here):
    // the picker shows them, names otherwise.
    wchar_t local[32768]{};
    const std::filesystem::path logo_cache = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)))
        ? std::filesystem::path(local) / L"Launchme" / L"platforms" : std::filesystem::path{};
    for (const auto* platform : {L"Steam", L"GOG", L"SNES"})
        if (const auto logo = platformLogoCached(platform, logo_cache); !logo.empty()) app.renderer_.setPlatformBadge(platform, logo);
    if (!capture(L"versions-desktop-picker.png")) return 9381;
    app.libraryDialogKey(VK_RETURN, false);
    if (app.libraryDialogOpen() || app.pending_launch_ || app.library_state_.preferred_versions[group] != gameKey(rom)) return 9376;
    if (app.selected_ >= app.cards_.size() || app.cards_[app.selected_].game.version_group != group || app.cards_[app.selected_].game.platform != L"SNES" ||
        activePlatformName(app.cards_[app.selected_].game) != L"SNES") return 9376; // The tile names the picked copy's console.
    if (!capture(L"versions-desktop-switched.png")) return 9382;

    // Tryb TV: Szczegóły put Zmień wersję right after Graj; the choice is the tile's.
    app.desktop_mode_ = false; app.openLibrary();
    if (portals().size() != 1 || portals()[0].platform != L"SNES") return 9377;
    app.openDetails(portals()[0]);
    const auto view = app.renderer_.viewport();
    const auto buttons = details_layout::Layout{view.width, view.height}.buttons(app.detailsActions());
    if (buttons.size() < 2 || buttons[1].kind != details_layout::Kind::versions || buttons[1].action != app.versionsDetailAction()) return 9377;
    if (!capture(L"versions-tv-details.png")) return 9383;
    app.detail_action_ = app.versionsDetailAction(); app.launch();
    if (!app.libraryDialogOpen() || ui.versions.size() != 3) return 9378;
    const auto pc = std::find_if(ui.versions.begin(), ui.versions.end(), [](const Game& g) { return g.source == GameSource::steam; });
    if (pc == ui.versions.end()) return 9378;
    ui.focus = static_cast<int>(pc - ui.versions.begin());
    if (!capture(L"versions-tv-picker.png")) return 9384;
    app.libraryDialogKey(VK_RETURN, false);
    if (app.libraryDialogOpen() || app.detail_games_.empty() || app.detail_games_[0].source != GameSource::steam ||
        app.library_state_.preferred_versions[group] != gameKey(steam)) return 9378;
    app.back(); app.openLibrary();
    if (portals().size() != 1 || portals()[0].source != GameSource::steam || activePlatformName(portals()[0]) != L"Steam") return 9379;
    return 0;
}
