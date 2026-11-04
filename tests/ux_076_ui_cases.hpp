// Included after LauncherUiTest: the desktop window's 0.7.6 changes. The game panel closes with its ✕ and returns
// with a chosen game; the grid's cells (posters, squares, landscape) come from a dropdown; the title bar keeps
// four tools and the rest sits behind "…"; the game's menu is grouped; "Jak zacząć" shows the first steps with
// their progress above the companion; "Zidentyfikuj grę" starts at once and takes the best match, falling back
// to the stores' text details. Captures: ux076-*.png.
int LauncherUiTest::ux076(HWND window) {
    using namespace desktop_ui;
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.settings_.launch_animation = false; app.settings_.tour_done = true;
    app.settings_.glance = false; app.settings_.companion = true;
    app.boundary_feedback_ = [] {};
    if (!app.renderer_.initialize(window)) return 12000;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) { app.paint(); return true; }
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 6; ++i) {
        Game game; game.kind = GameKind::nonsteam; game.shortcut_id = 9200 + i; game.name = L"Game " + std::to_wstring(i);
        game.platform = L"Nintendo Switch";
        library->games.push_back(game);
    }
    app.onLibrary(std::move(library)); app.showCategory(0);
    app.desktop_mode_ = true; app.audio_.setDesktop(true);
    app.buildShelves(); app.showCategory(0);
    const auto settle = [&] { for (int i = 0; i < 20; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    settle();
    app.settings_.start_hidden = true;

    // ---- The game panel: shown for the selected game, closed by ✕, back with another selection.
    if (!app.desktopPaneOpen() || app.desktopLayout().paneWidth() <= 0) return 12001;
    if (!capture(L"ux076-pane-open.png")) return 12002;
    app.desktopActivate({Target::pane_close, 0});
    if (app.desktopPaneOpen() || app.desktopLayout().paneWidth() != 0 || app.desktopLayout().paneClose().right != 0) return 12003;
    app.paint();
    if (!capture(L"ux076-pane-closed.png")) return 12004;
    app.desktopFocus(1);
    if (!app.desktopPaneOpen() || app.desktopLayout().paneWidth() <= 0) return 12005;

    // ---- The cells: a dropdown of three shapes; squares are as high as wide.
    app.desktopOpenViewMenu();
    if (!app.desktop_menu_ || app.desktop_menu_->kind != Menu::Kind::view || app.desktop_menu_->items.size() != 3) return 12010;
    app.desktopMenuPick(app.desktop_menu_->items[1], false);
    if (app.settings_.desktop_view != 1 || std::abs(app.desktopLayout().cell_aspect - 1.0f) > 0.01f) return 12011;
    settle();
    if (!capture(L"ux076-view-squares.png")) return 12012;
    app.desktopOpenViewMenu();
    app.desktopMenuPick(app.desktop_menu_->items[2], false);
    if (app.settings_.desktop_view != 2 || app.desktopLayout().cell_aspect >= 1.0f) return 12013;
    settle();
    if (!capture(L"ux076-view-landscape.png")) return 12014;
    app.desktopOpenViewMenu();
    app.desktopMenuPick(app.desktop_menu_->items[0], false);
    if (app.settings_.desktop_view != 0 || std::abs(app.desktopLayout().cell_aspect - 1.5f) > 0.01f) return 12015;
    settle();

    // ---- The title bar: "…" holds the rest.
    const auto tools = app.desktopTools();
    if (tools.empty() || tools.back() != Tool::more || tools.size() > 5) return 12020;
    app.desktopActivate({Target::tool, static_cast<int>(tools.size()) - 1});
    if (!app.desktop_menu_ || app.desktop_menu_->kind != Menu::Kind::tools || app.desktop_menu_->items.empty()) return 12021;
    if (!capture(L"ux076-more-menu.png")) return 12022;
    app.desktop_menu_.reset();

    // ---- The game's menu: the common actions, then three groups behind submenus.
    app.desktopFocus(0);
    app.desktopOpenGameMenu(300, 300, true);
    if (!app.desktop_menu_ || app.desktop_menu_->kind != Menu::Kind::game) return 12030;
    int groups = 0;
    for (const auto& item : app.desktop_menu_->items) groups += item.action == Action::group ? 1 : 0;
    if (groups < 2 || app.desktop_menu_->items.size() > 14) return 12031;
    for (size_t i = 0; i < app.desktop_menu_->items.size(); ++i)
        if (app.desktop_menu_->items[i].action == Action::group) { app.desktopOpenSubmenu(static_cast<int>(i)); break; }
    if (app.desktop_menu_->sub.empty()) return 12032;
    if (!capture(L"ux076-game-menu.png")) return 12033;
    app.desktop_menu_.reset();

    // ---- Jak zacząć: the card, its progress, the list, a step, hiding, and the reset in Ustawienia.
    app.settings_.start_hidden = false; app.settings_.start_done = 0; app.settings_.emulation_roms.clear();
    if (!app.desktopStartShown() || !app.desktopLayout().start_card || app.desktopStartDone() != 0) return 12040;
    app.paint();
    if (!capture(L"ux076-getting-started.png")) return 12041;
    const auto card = app.desktopLayout().startCard();
    if (app.desktopHit((card.left + card.right) * 0.5f, (card.top + card.bottom) * 0.5f).target != Target::start_card) return 12042;
    app.desktopActivate({Target::start_card, 0});
    if (!app.desktop_menu_ || app.desktop_menu_->kind != Menu::Kind::start || app.desktop_menu_->items.size() != static_cast<size_t>(Launcher::desktopStartCount()) + 2) return 12043;
    if (!capture(L"ux076-getting-started-menu.png")) return 12044;
    app.desktopMenuPick(app.desktop_menu_->items[0], false); // Platformy i konta: Ustawienia → Biblioteka.
    if (!app.settingsOpen() || app.settings_state_.category != static_cast<int>(settings_ui::Category::library) || app.desktopStartDone() != 1 || !app.desktopStartStepDone(0)) return 12045;
    app.back(); settle();
    app.desktop_mode_ = true;
    app.desktopActivate({Target::start_card, 0});
    if (!app.desktop_menu_ || app.desktop_menu_->items.back().action != Action::start_hide) return 12046;
    app.desktopMenuPick(app.desktop_menu_->items.back(), false);
    if (!app.settings_.start_hidden || app.desktopStartShown()) return 12046;
    app.startReset();
    if (app.settings_.start_hidden || app.settings_.start_done != 0 || !app.desktopStartShown()) return 12047;
    app.settings_.start_done = (1u << Launcher::desktopStartCount()) - 1; // Everything done: the card goes.
    if (app.desktopStartShown()) return 12048;
    app.settings_.start_done = 0; app.settings_.start_hidden = true;
    // The reset row is in Ustawienia → System.
    app.openSettings();
    app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::system));
    app.buildSettingsRows();
    if (std::none_of(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& row) { return row.id == settings_ui::Id::start_reset; })) return 12049;
    app.back(); settle();

    // ---- Zidentyfikuj grę: it starts at once, the best match is taken; no match: the player types; the stores' text as the last resort.
    app.desktop_mode_ = true;
    const Game game = app.cards_.empty() ? Game{} : app.cards_[0].game;
    if (game.name.empty()) return 12050;
    app.openIdentifyFor(game);
    auto& state = app.identify_state_;
    if (!state.open || state.manual || !state.busy || state.source != identify_ui::State::Source::all) return 12051;
    app.paint();
    if (!capture(L"ux076-identify-lookup.png")) return 12052;
    auto answer = std::make_unique<IdentifyMessage>(); answer->generation = app.identify_token_->load();
    answer->search.games = {{501, L"Game 0", L"2020-01-01", L"Switch"}, {502, L"Game 0 Deluxe", L"2021-01-01", L"Switch"}};
    app.onIdentify(std::move(answer));
    if (!state.applying || state.selected != 0 || state.manual) return 12053; // No click: the first match is downloaded.
    app.paint();
    if (!capture(L"ux076-identify-applying.png")) return 12054;
    app.closeIdentify();
    if (!state.applying) app.closeIdentify(); // (An application in flight cannot be closed: the next block starts over.)
    state = {};
    app.openIdentifyFor(game);
    answer = std::make_unique<IdentifyMessage>(); answer->generation = app.identify_token_->load();
    app.onIdentify(std::move(answer)); // Nothing found anywhere.
    if (!state.manual || state.applying || state.status.empty()) return 12055;
    app.paint();
    if (!capture(L"ux076-identify-manual.png")) return 12056;
    state = {};
    app.openIdentifyFor(game);
    answer = std::make_unique<IdentifyMessage>(); answer->generation = app.identify_token_->load();
    metadata_sources::Offer offer;
    offer.source = metadata_sources::Source::nintendo; offer.title = L"Game 0"; offer.score = 1000;
    offer.details.description = L"A description from a store."; offer.details.developer = L"Studio"; offer.details.release_date = L"2020-01-01";
    answer->texts = {offer};
    app.onIdentify(std::move(answer));
    const auto* own = app.metadataOverride(game);
    if (app.identifyOpen() || !own || own->description != L"A description from a store." || own->developer != L"Studio" || own->release_date != L"2020-01-01") return 12057;
    // "Dopasuj ręcznie" while the lookup runs: the field and the provider choice, the lookup's answer is a list to pick from.
    app.openIdentifyFor(game);
    app.identifyManual();
    if (!state.manual || !state.query_focus) return 12058;
    answer = std::make_unique<IdentifyMessage>(); answer->generation = app.identify_token_->load();
    answer->search.games = {{601, L"Game 0", L"2020-01-01", L"Switch"}};
    app.onIdentify(std::move(answer));
    if (state.applying || state.games.size() != 1 || state.query_focus) return 12059;
    app.identifySwitchSource();
    if (state.source != identify_ui::State::Source::rawg) return 12060;
    app.identifySwitchSource();
    if (state.source != identify_ui::State::Source::screenscraper) return 12061; // Emulated games: the third provider.
    for (const auto expected : {identify_ui::State::Source::nintendo, identify_ui::State::Source::steam, identify_ui::State::Source::gog,
                                identify_ui::State::Source::xbox, identify_ui::State::Source::titledb, identify_ui::State::Source::all}) {
        app.identifySwitchSource();
        if (state.source != expected) return 12062; // Then each store on its own, and round to Wszyscy dostawcy.
    }
    app.closeIdentify();
    app.desktop_mode_ = false; app.audio_.setDesktop(false);
    return 0;
}
