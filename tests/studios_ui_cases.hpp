#include "../src/artwork/platform_logo.hpp"
#include "../src/platform/text_encoding.hpp"

// Wydawcy and Developerzy (src/app/studio_libraries.cpp): store pages in the cache give the game
// index each game's studios; the home gets a row of logos for each, its "Wszystkie" a board of
// them, a logo the studio's library (its tabs: the other logos; Back: the board). Hidden and
// moved studios, the switch in Ustawienia, the desktop window's folded sections with the logo as
// their title, the pane's Developer link, the search and Ctrl+K by studio, the gold edge of a
// game with every achievement, and Edytuj metadane in Tryb TV with the pad and its keyboard.
// Nothing reaches the network. Captures: studios-*.png.
int LauncherUiTest::studioLibraries(HWND window) {
    using Modal = Launcher::Modal;
    const auto config = userConfigDirectory();
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.overlay_backend_.foreground = [window] { return window; };
    app.settings_.launch_animation = false; app.settings_.tour_done = true;
    app.settings_.glance = app.settings_.companion = false;
    app.settings_.shelves_category_all = true;
    app.settings_.studio_min_games = 1; // These studios have one or two games each (Najmniej gier is checked below).
    if (!app.renderer_.initialize(window)) return 9800;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) { app.paint(); return true; }
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    // Store pages as Szczegóły leave them: who made and who published each game.
    struct Fixture { uint32_t id; const wchar_t* name; const char* developer; const char* publisher; };
    const Fixture fixtures[]{
        {9800, L"Mglisty Szlak", "Capcom", "CAPCOM Co., Ltd."},
        {9801, L"Gwiezdny Kurier", "Ubisoft Montreal", "Ubisoft"},
        {9802, L"Ogród Zegarów", "Indie Krew", "Ubisoft Entertainment"},
        {9803, L"Ostatnia Latarnia", "Capcom", "Sega"},
        {9804, L"Miasto Kości", "Valve", "Devolver Digital"},
    };
    for (const auto& game : fixtures) {
        const std::string json = "{\"" + std::to_string(game.id) + R"(":{"success":true,"data":{"short_description":"Opis.","developers":[")" +
            game.developer + R"("],"publishers":[")" + game.publisher + R"("]}}})";
        atomic_file::write(config / L"metadata" / (std::to_wstring(game.id) + L".details.json"), json);
    }
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (const auto& fixture : fixtures) {
        Game game{fixture.id, fixture.name}; game.installed = true;
        game.last_played = fixture.id == 9800 || fixture.id == 9801 ? 1'760'000'000 + fixture.id : 0; // On Recent: one of them complete.
        library->games.push_back(game);
    }
    app.onLibrary(std::move(library)); app.showCategory(0);
    app.scanning_ = false;
    app.startGameIndex();
    if (!app.game_index_) return 9801;
    const auto pump = [&] {
        MSG message{};
        while (PeekMessageW(&message, window, game_index_message, game_index_message, PM_REMOVE))
            app.onGameIndex(std::unique_ptr<GameIndexMessage>(reinterpret_cast<GameIndexMessage*>(message.lParam)));
    };
    app.syncGameIndex();
    app.game_index_changed_ = std::chrono::steady_clock::now() - std::chrono::seconds(5);
    app.tickGameIndex();
    const auto& publishers = app.studio_groups_[static_cast<size_t>(studios::Role::publisher)];
    const auto& developers = app.studio_groups_[static_cast<size_t>(studios::Role::developer)];
    for (const auto end = GetTickCount64() + 5000; (publishers.size() < 3 || developers.size() < 2) && GetTickCount64() < end; Sleep(5)) pump();
    // Publishers by most games: Ubisoft (two games), then Capcom and Sega by name; Devolver has no logo.
    if (publishers.size() != 3 || publishers[0].studio->id != "ubisoft" || publishers[0].games.size() != 2 ||
        publishers[1].studio->id != "capcom" || publishers[2].studio->id != "sega") return 9802;
    if (developers.size() != 2 || developers[0].studio->id != "capcom" || developers[0].games.size() != 2 || developers[1].studio->id != "ubisoft") return 9803;

    // The home: a row of logos for each, after the others.
    const auto row = [&](const char* id) -> int {
        for (size_t i = 0; i < app.categories_.size(); ++i) if (app.categories_[i].id == id) return static_cast<int>(i);
        return -1;
    };
    const int publisher_row = row("publishers"), developer_row = row("developers");
    if (publisher_row < 0 || developer_row != publisher_row + 1 || app.categories_[static_cast<size_t>(publisher_row)].games.size() != 3) return 9804;
    const auto& tile = app.categories_[static_cast<size_t>(publisher_row)].games[0];
    if (tile.kind != GameKind::platform || tile.name != L"Ubisoft" || tile.platform != L"publisher:ubisoft") return 9805;
    // The logo ships beside the program (assets\studios); as a badge it is the tabs' and headers' mark.
    const auto logo = [](const std::wstring& key) { return platformLogoCached(key, {}); };
    if (logo(L"publisher:ubisoft").size() < 128 || !logo(L"publisher:nobody").empty()) return 9806;
    for (const auto* list : {&publishers, &developers})
        for (const auto& entry : *list)
            for (const auto role : {studios::Role::publisher, studios::Role::developer}) {
                const auto key = text_encoding::wide(studios::key(role, entry.studio->id));
                app.renderer_.setPlatformBadge(key, logo(key));
            }
    const auto bindLogos = [&] {
        for (size_t i = 0; i < app.cards_.size(); ++i)
            if (app.cards_[i].game.kind == GameKind::platform) { app.renderer_.setPlatformCover(i, logo(app.cards_[i].game.platform)); app.cards_[i].cover_opacity = 1; }
    };
    app.showCategory(static_cast<size_t>(publisher_row));
    bindLogos();
    for (int i = 0; i < 30; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
    if (!capture(L"studios-home-row.png")) return 9807;

    // "Wszystkie": the board of all the logos; its tabs Wydawcy and Developerzy.
    app.openCategoryLibrary("publishers");
    if (!app.studioBoard() || app.cards_.size() != 3 || app.cards_[0].game.platform != L"publisher:ubisoft" || app.cards_[2].game.platform != L"publisher:sega") return 9808;
    const auto tabs = app.libraryTabs();
    if (tabs.size() != 2 || tabs[0].id != "publishers" || tabs[1].id != "developers" || app.libraryTabIndex(tabs) != 0) return 9809;
    bindLogos();
    for (int i = 0; i < 20; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
    if (!capture(L"studios-board.png")) return 9810;
    // A logo: that publisher's games; the tabs are the other logos.
    app.focusCard(0); app.enterDown(false); app.enterUp();
    if (app.screen_ != ScreenMode::library || app.library_console_ != L"publisher:ubisoft" || app.cards_.size() != 2 || app.studio_board_return_ != "publishers") return 9811;
    const auto studio_tabs = app.libraryTabs();
    if (studio_tabs.size() != 3 || studio_tabs[0].mark != L"publisher:ubisoft" || studio_tabs[2].mark != L"publisher:sega") return 9812;
    for (int i = 0; i < 20; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
    if (!capture(L"studios-library.png")) return 9813;
    app.switchLibraryTab(1);
    if (app.library_console_ != L"publisher:capcom" || app.cards_.size() != 1 || app.studio_board_return_ != "publishers") return 9814;
    // Back: the board, on Capcom's logo; Back again: home.
    app.back();
    if (!app.studioBoard() || app.selected_ >= app.cards_.size() || app.cards_[app.selected_].game.platform != L"publisher:capcom") return 9815;
    app.back();
    if (app.screen_ != ScreenMode::shelves) return 9816;

    // Searching the library by studio: "devolver" finds Miasto Kości (no logo needed).
    app.openLibrary();
    app.openLibrarySearch();
    app.library_controls_.query = L"devolver";
    app.rebuildCards(false);
    if (app.cards_.size() != 1 || app.cards_[0].game.app_id != 9804) return 9817;
    app.closeLibrarySearch(); app.back();

    // Hidden, moved, switched off.
    app.hideStudio("publisher:sega");
    if (publishers.size() != 2 || !app.settings_.hidden_studios.contains("publisher:sega")) return 9818;
    app.openSettings(); app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library)); app.buildSettingsRows();
    const auto hidden_row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& r) { return r.id == settings_ui::Id::studio_hidden; });
    if (hidden_row == app.settings_rows_.end()) return 9819;
    // Its page: each hidden studio with a switch that shows it again (back to Biblioteka once none is left).
    app.settings_state_.options = true; // The rows, not the category list.
    app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(hidden_row - app.settings_rows_.begin()); app.settingsActivate();
    if (!app.settings_state_.at(settings_ui::Page::hidden_studios) || app.settings_rows_.size() != 2 ||
        app.settings_rows_[1].id != settings_ui::Id::studio_hidden_entry || app.settings_rows_[1].key != "publisher:sega" || app.settings_rows_[1].checked) return 9852;
    if (!capture(L"studios-hidden-page.png")) return 9853;
    app.settings_state_.selected[app.settings_state_.category] = 1; app.settingsActivate();
    if (!app.settings_.hidden_studios.empty() || app.settings_state_.at(settings_ui::Page::hidden_studios) || publishers.size() != 3) return 9854;
    app.hideStudio("publisher:sega");
    app.showStudios();
    if (publishers.size() != 3 || !app.settings_.hidden_studios.empty()) return 9820;
    // Tryb TV: a logo's own menu (move earlier or later, hide), not a game's.
    {
        app.back(); app.showCategory(static_cast<size_t>(row("publishers")));
        const auto ubisoft = std::find_if(app.cards_.begin(), app.cards_.end(), [](const GameCard& c) { return c.game.platform == L"publisher:ubisoft"; });
        if (ubisoft == app.cards_.end()) return 9855;
        app.focusCard(static_cast<size_t>(ubisoft - app.cards_.begin()));
        app.openContextMenu();
        const auto menu = app.menuRows();
        if (!app.menu_open_ || !menu.studio || context_menu::rows(menu) != 3 || context_menu::item(0, menu) != context_menu::Item::studio_earlier ||
            context_menu::item(2, menu) != context_menu::Item::studio_hide) return 9856;
        for (int i = 0; i < 20; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
        if (!capture(L"studios-tile-menu.png")) return 9857;
        app.menu_selected_ = 2; app.menuAccept();
        if (app.menu_open_ || !app.settings_.hidden_studios.contains("publisher:ubisoft")) return 9858;
        app.showStudios();
        app.openSettings();
    }
    app.back(); app.showCategory(0);
    app.moveStudio("publisher:sega", -1); // Before Capcom: the order is the player's from now on.
    if (publishers[1].studio->id != "sega" || app.settings_.studio_order.size() < 2 || app.settings_.studio_order[1] != "publisher:sega") return 9821;
    // Each row on its own switch: Wydawcy off keeps Developerzy.
    app.settings_.show_publishers = false; app.buildShelves();
    if (row("publishers") >= 0 || !publishers.empty() || row("developers") < 0 || developers.empty()) return 9822;
    app.settings_.show_publishers = true; app.buildShelves();
    if (row("publishers") < 0 || publishers.size() != 3) return 9823;
    // Najmniej gier: two games keep Ubisoft (it stays where the player put it), one-game studios go; the
    // rest by most games.
    app.settings_.studio_min_games = 2; app.buildShelves();
    if (std::any_of(publishers.begin(), publishers.end(), [](const studios::Entry& e) { return e.games.size() < 2; }) ||
        std::none_of(publishers.begin(), publishers.end(), [](const studios::Entry& e) { return e.studio->id == "ubisoft"; })) return 9851;
    app.settings_.studio_min_games = 1; app.buildShelves();

    // A game with every achievement: a gold edge.
    app.history_.progress["steam:9800"] = {12, 12};
    app.refreshStudios();
    if (!app.perfect_games_.contains("steam:9800") || app.perfect_games_.contains("steam:9801")) return 9824;
    app.showCategory(0);
    for (int i = 0; i < 20; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
    if (!capture(L"studios-perfect-tv.png")) return 9825;

    // ---- The desktop window: Wydawcy and Developerzy folded; a section's title is its logo.
    app.desktop_mode_ = true; app.audio_.setDesktop(true);
    app.buildShelves(); app.showCategory(0);
    using desktop_ui::Group;
    const auto rows = app.desktopRows();
    const auto heading = [&](Group group) { return std::any_of(rows.begin(), rows.end(), [&](const auto& r) { return r.heading && r.group == group; }); };
    const auto entries = [&](Group group) { return std::count_if(rows.begin(), rows.end(), [&](const auto& r) { return !r.heading && r.group == group; }); };
    if (!heading(Group::publishers) || !heading(Group::developers) || entries(Group::publishers) || entries(Group::developers)) return 9826;
    app.desktop_collapsed_[static_cast<size_t>(Group::publishers)] = false;
    if (app.desktopEntryIndex("publisher:ubisoft") < 0 || app.desktopEntryIndex("publisher:devolver") >= 0) return 9850; // No logo: no section.
    app.desktopShowSection("publisher:ubisoft");
    if (app.cards_.size() != 2) return 9827;
    desktop_ui::View view; app.desktopView(view);
    if (view.title_mark != L"publisher:ubisoft") return 9828;
    for (int i = 0; i < 30; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
    if (!capture(L"studios-desktop-section.png")) return 9829;
    app.desktopSaveState();
    if (!app.settings_.desktop_expanded.contains("publishers") || app.settings_.desktop_expanded.contains("developers")) return 9830;
    // The pane's Developer: Capcom has a logo, its section opens (unfolded); Valve has none: the search.
    app.desktopFilterStudio(L"Capcom", studios::Role::developer);
    if (app.desktop_section_ != "developer:capcom" || app.desktop_collapsed_[static_cast<size_t>(Group::developers)] || app.cards_.size() != 2) return 9831;
    app.desktopFilterStudio(L"Valve", studios::Role::developer);
    if (app.desktop_section_ != "all" || app.desktop_query_ != L"Valve" || app.cards_.size() != 1 || app.cards_[0].game.app_id != 9804) return 9832;
    app.desktop_query_.clear(); app.desktop_caret_ = 0; app.desktopApplyQuery();
    // Ctrl+K: "devolver" offers Devolver Digital; Enter shows its games.
    app.desktopQuickOpen();
    app.desktop_quick_.query = L"devolver"; app.desktopQuickUpdate();
    const auto quick = std::find_if(app.desktop_quick_.items.begin(), app.desktop_quick_.items.end(), [](const auto& item) { return item.target == "studio:Devolver Digital"; });
    if (quick == app.desktop_quick_.items.end()) return 9833;
    app.desktopQuickRun(static_cast<int>(quick - app.desktop_quick_.items.begin()), false);
    if (app.desktop_query_ != L"Devolver Digital" || app.cards_.size() != 1) return 9834;
    app.desktop_query_.clear(); app.desktop_caret_ = 0; app.desktopApplyQuery();
    // "capcom" also offers its section.
    app.desktopQuickOpen();
    app.desktop_quick_.query = L"capcom"; app.desktopQuickUpdate();
    if (std::none_of(app.desktop_quick_.items.begin(), app.desktop_quick_.items.end(), [](const auto& item) { return item.target == "publisher:capcom"; })) return 9835;
    app.desktopQuickClose();
    // The gold edge on a poster.
    app.desktopShowSection("all");
    for (int i = 0; i < 30; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
    if (!capture(L"studios-perfect-desktop.png")) return 9836;
    app.desktop_mode_ = false; app.audio_.setDesktop(false);
    app.buildShelves(); app.showCategory(0);

    // ---- Edytuj metadane in Tryb TV: the Details menu's row, the pad and its keyboard.
    const auto mist = *std::find_if(app.public_library_.begin(), app.public_library_.end(), [](const Game& game) { return game.app_id == 9803; });
    app.openDetails(mist);
    const auto menu = app.menuRows();
    if (!menu.metadata || context_menu::row(context_menu::Item::metadata, menu) < 0) return 9837;
    app.openContextMenu();
    app.menu_selected_ = context_menu::row(context_menu::Item::metadata, menu);
    app.menuAccept();
    if (!app.metadataOpen() || app.topModal() != Modal::metadata || app.menu_open_) return 9838;
    if (app.desktop_meta_.fields[2] != L"Capcom" || app.desktop_meta_.fields[3] != L"Sega") return 9839; // Developer and publisher as the index knows them.
    app.pad_input_ = true;
    app.metadataKey(VK_DOWN, false); app.metadataKey(VK_DOWN, false); // Opis, then Producent.
    if (app.desktop_meta_.focus != 2) return 9840;
    app.metadataKey(VK_RIGHT, false); // Wydawca, beside it.
    if (app.desktop_meta_.focus != 3) return 9841;
    app.metadataKey(VK_RETURN, false); // A: its keyboard.
    if (!app.keyboard_.open || app.keyboardPreview() != L"Sega") return 9842;
    for (int i = 0; i < 4; ++i) app.keyboardBackspace();
    for (const wchar_t c : std::wstring(L"Capcom")) app.onCharacter(c);
    if (app.desktop_meta_.fields[3] != L"Capcom" || app.keyboardPreview() != L"Capcom") return 9843;
    for (int i = 0; i < 20; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } // The keyboard slides up.
    if (!app.keyboard_.open || !capture(L"studios-tv-metadata.png")) return 9844;
    app.keyboardDone();
    if (app.keyboard_.open || !app.metadataOpen()) return 9845;
    for (int i = 0; i < 4; ++i) app.metadataKey(VK_DOWN, false); // Down to the buttons.
    if (app.desktop_meta_.focus != static_cast<int>(desktop_ui::MetaItem::restore)) return 9846;
    app.metadataKey(VK_RIGHT, false);
    if (app.desktop_meta_.focus != static_cast<int>(desktop_ui::MetaItem::save)) return 9847;
    app.metadataKey(VK_RETURN, false);
    const auto* own = app.metadataOverride(mist);
    if (app.metadataOpen() || !own || own->publisher != L"Capcom" || !own->developer.empty()) return 9848;
    // B closes it without saving.
    app.openMetadata(mist);
    PadReading pad{true, L"studios-pad"}; app.controller(pad);
    pad.buttons = XINPUT_GAMEPAD_B; app.controller(pad); pad.buttons = 0; app.controller(pad);
    if (app.metadataOpen() || app.topModal() != Modal::none) return 9849;
    app.back();
    return 0;
}
