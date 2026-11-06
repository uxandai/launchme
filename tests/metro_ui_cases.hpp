// Included after LauncherUiTest: the Metro home layout (src/app/metro.cpp). The start page and
// its shortcuts, one page per category, arrows across page edges, LB/RB (homePage), Back, the
// quick bar and category menu at the edges, clicks, artwork binding and the saved setting.
// Artwork comes from fixtures. Captures: metro-*.png.
#include "../src/ui/metro_layout.hpp"

int LauncherUiTest::metro(HWND window) {
    using metro_layout::Role;
    Launcher app;
    app.settings_.launch_animation = false;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 2700;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) return true;
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    app.settings_.home_layout = AppSettings::HomeLayout::metro;
    // Metro's own lower-case tabs first; the "Pasek kategorii" strip (on by default) is checked below.
    app.settings_.metro_category_tabs = false;
    // The store and console shortcuts' cells; Glance and Towarzyszka there: home_widgets_ui_cases.hpp.
    app.settings_.glance = app.settings_.companion = false;
    app.settings_.enabled_pc_sources.insert("gog"); // A Platformy PC page: Steam and GOG.
    auto library = std::make_unique<LibraryMessage>();
    library->account = 1;
    for (uint32_t i = 0; i < 20; ++i) {
        Game game{4000 + i, L"Metro game " + std::to_wstring(i)};
        game.last_played = i < 6 ? 100 + i : 0;
        library->games.push_back(std::move(game));
    }
    Game download{4100, L"Metro download"};
    download.installed = false;
    download.installation = {InstallPhase::downloading, 30, 100, true};
    library->games.push_back(download);
    for (uint32_t c = 0; c < 2; ++c) {
        SteamCollection collection{c ? L"Logiczne" : L"Akcja", {}};
        for (uint32_t i = 0; i < (c ? 6u : 18u); ++i) collection.app_ids.push_back(4000 + i);
        collection.id = "metro-" + std::to_string(c);
        library->collections.push_back(std::move(collection));
    }
    app.onLibrary(std::move(library));
    app.showCategory(0);
    if (!app.metroActive() || app.metro_pages_.size() < 4 || app.metro_page_ != 0 || app.metro_tiles_.size() != app.cards_.size()) return 2701;
    const auto role = [&](size_t i) { return app.metro_tiles_[i].role; };
    const auto find = [&](size_t page, Role wanted) -> std::optional<size_t> {
        const auto& p = app.metro_pages_[page];
        for (size_t i = p.begin; i < p.begin + p.count; ++i) if (role(i) == wanted) return i;
        return std::nullopt;
    };
    // Start page: the latest game large, two more, the shortcuts and the download.
    const auto start = app.metro_pages_[0]; // A copy: rebuilds replace metro_pages_.
    if (role(start.begin) != Role::recent_large || app.cards_[start.begin].game.app_id != 4005 || app.selected_ != start.begin) return 2702;
    for (const auto wanted : {Role::recent, Role::library, Role::settings, Role::pc_platforms, Role::install})
        if (!find(0, wanted)) return 2703;
    if (find(0, Role::emulation) || app.cards_[*find(0, Role::install)].game.app_id != 4100) return 2704;
    // Category pages: stores as logos, collections as posters after their "Wszystkie" tile (capped).
    std::optional<size_t> pcs_page, action_page;
    for (size_t p = 1; p < app.metro_pages_.size(); ++p) {
        const auto& id = app.categories_[app.metro_pages_[p].category].id;
        if (id == "pc-platforms") pcs_page = p;
        if (id == "metro-0") action_page = p;
    }
    if (!pcs_page || !action_page || role(app.metro_pages_[*pcs_page].begin) != Role::logo ||
        role(app.metro_pages_[*action_page].begin) != Role::all ||
        app.metro_pages_[*action_page].count != metro_layout::category_tiles) return 2705;
    for (size_t p = 1; p < app.metro_pages_.size(); ++p)
        if (app.metro_pages_[p].left < app.metro_pages_[p - 1].right + metro_layout::page_gap - 0.5f) return 2706;
    // Artwork: wide art for the recent tiles, posters elsewhere, covers for the store logos.
    const auto deliver = [&](const Game& game, CoverMessage::Kind kind, std::vector<uint8_t> bytes) {
        auto message = std::make_unique<CoverMessage>();
        message->key = gameKey(game); message->generation = app.jobs_->generation.load(); message->kind = kind; message->bytes = std::move(bytes);
        app.onCover(std::move(message));
    };
    for (const auto& game : app.public_library_) {
        const auto tint = static_cast<uint8_t>(40 + (game.app_id % 20) * 9);
        deliver(game, CoverMessage::Kind::landscape, pngFixture(tint, 92, 43));
        deliver(game, CoverMessage::Kind::poster, pngFixture(tint, 40, 60));
        deliver(game, CoverMessage::Kind::hero, pngFixture(tint, 192, 62));
    }
    for (size_t i = app.metro_pages_[*pcs_page].begin; i < app.metro_pages_[*pcs_page].begin + app.metro_pages_[*pcs_page].count; ++i)
        deliver(app.cards_[i].game, CoverMessage::Kind::cover, pngFixture(250, 64, 32));
    if (!app.renderer_.hasPoster(start.begin) || !app.renderer_.hasPoster(*find(0, Role::install))) return 2707;
    settle();
    const auto view = app.renderer_.viewport();
    const auto& big = app.cards_[start.begin];
    if (std::abs(big.x - metro_layout::left - metro_layout::cell_w - metro_layout::gap) > 1 ||
        std::abs(big.y - metro_layout::top(view.height)) > 1 || std::abs(big.width() - (2 * metro_layout::cell_w + metro_layout::gap)) > 1) return 2708;
    // Tiles shaped like their art: capsules for the recent games, a poster for the installation,
    // almost square shortcuts; the large tile's corner is no rounder than the small ones'.
    const auto aspect = [&](Role wanted) { return app.cards_[*find(0, wanted)].aspect; };
    if (std::abs(aspect(Role::recent) - 920.0f / 430.0f) > 0.05f || std::abs(aspect(Role::recent_large) - 920.0f / 430.0f) > 0.12f ||
        std::abs(aspect(Role::install) - 2.0f / 3.0f) > 0.02f || std::abs(aspect(Role::library) - 1) > 0.06f || std::abs(aspect(Role::settings) - 1) > 0.06f) return 2734;
    if (!capture(L"metro-start.png")) return 2709;
    // Right: the installation (the tall tile beside the large one), then over the edge to the next page.
    app.navigate(1);
    if (role(app.selected_) != Role::install) return 2710;
    app.navigate(1);
    if (app.metro_page_ != 1 || app.category_ != app.metro_pages_[1].category || app.metro_scroll_ != app.metro_pages_[1].left) return 2711;
    settle();
    if (std::abs(app.cards_[app.selected_].x - metro_layout::left) > 1 || std::abs(app.metro_tab_ - 1) > 0.01f) return 2712;
    if (!capture(L"metro-page.png")) return 2713;
    // Left over the edge returns to the start page's right column.
    app.navigate(-1);
    if (app.metro_page_ != 0 || role(app.selected_) != Role::install) return 2714;
    // LB/RB: whole pages; each page keeps its focused tile.
    app.homePage(1); app.changeCategory(-1); // GOG (entered at the tall tile's height) up to Steam.
    const auto page_one = app.selected_;
    app.homePage(-1);
    if (role(app.selected_) != Role::install) return 2715;
    app.homePage(1);
    if (app.selected_ != page_one) return 2716;
    // The shortcut slides to its page; "Wszystkie" opens the category library and Back returns there.
    app.metroFocus(*find(0, Role::pc_platforms)); app.launch();
    if (app.metro_page_ != *pcs_page || app.screen_ != ScreenMode::shelves) return 2717;
    app.metroFocus(app.metro_pages_[*action_page].begin); app.launch();
    if (app.screen_ != ScreenMode::library || app.library_category_ != "metro-0") return 2718;
    app.back();
    if (!app.metroActive() || app.metro_page_ != *action_page || role(app.selected_) != Role::all) return 2719;
    // Twelve tiles: six columns of posters, all on screen; the next Right is the next page.
    for (int i = 0; i < 5; ++i) app.navigate(1);
    if (app.metro_page_ != *action_page || app.selected_ != app.metro_pages_[*action_page].begin + 10) return 2720;
    settle();
    const auto& rightmost = app.cards_[app.selected_];
    if (rightmost.x + rightmost.width() > view.width - metro_layout::right_margin + 1) return 2721;
    if (!capture(L"metro-category.png")) return 2722;
    app.navigate(1);
    if (app.metro_page_ != *action_page + 1) return 2735;
    app.back();
    if (app.metro_page_ != 0 || app.category_ != 0) return 2723;
    // Up from the top row: the tab strip. Left/Right change the page there, Down returns to its tile.
    app.metroFocus(start.begin); app.changeCategory(-1);
    if (!app.metroTabsFocused() || app.quick_open_) return 2724;
    settle();
    if (app.cards_[start.begin].emphasis > 0.01f || !capture(L"metro-tabs.png")) return 2736;
    app.openContextMenu();
    if (app.menu_open_) return 2737;
    app.navigate(1);
    if (!app.metroTabsFocused() || app.metro_page_ != 1) return 2738;
    app.changeCategory(1);
    if (app.metroTabsFocused() || app.metro_page_ != 1) return 2739;
    app.changeCategory(-1); app.changeCategory(-1); app.launch(); // Enter/A also returns to the tile.
    if (app.metroTabsFocused() || app.pending_launch_ || app.metro_page_ != 1) return 2740;
    app.changeCategory(-1); app.back();
    if (app.metroTabsFocused() || app.metro_page_ != 1) return 2741;
    app.back();
    // Down from the bottom row opens the quick bar, Up closes it; Left at the start is a boundary.
    app.metroFocus(*find(0, Role::library)); app.changeCategory(1);
    if (!app.quick_open_) return 2725;
    app.quick_progress_ = 1; settle();
    if (std::abs(app.cards_[start.begin].y - (metro_layout::top(view.height) - metro_layout::quick_lift)) > 1 || !capture(L"metro-quick.png")) return 2742;
    app.changeCategory(1);
    if (!app.quick_open_) return 2743;
    app.changeCategory(-1);
    if (app.quick_open_) return 2744;
    app.quick_progress_ = 0; settle();
    const auto library_tile = *find(0, Role::library);
    app.metroFocus(library_tile); app.navigate(-1);
    if (app.category_menu_open_ || app.selected_ != library_tile) return 2726;
    app.openCategoryMenu();
    if (app.category_menu_open_) return 2727;
    // A click on a tab goes to its page.
    app.paint();
    const auto tab = app.renderer_.metroTabAt(metro_layout::left + 4, metro_layout::tabs_center);
    if (tab != 0) return 2745;
    for (size_t p = 1; p < app.metro_pages_.size(); ++p) {
        int found = -1;
        for (float x = metro_layout::left; x < view.width && found < 0; x += 8)
            if (app.renderer_.metroTabAt(x, metro_layout::tabs_center) == static_cast<int>(p)) found = static_cast<int>(x);
        if (found < 0) continue;
        app.mouse_transition_until_ = 0;
        app.click(found * view.scale, metro_layout::tabs_center * view.scale, false);
        if (app.metro_page_ != p || app.metroTabsFocused()) return 2746;
        break;
    }
    app.back();
    {
        // "Pasek kategorii" (on by default): the library's strip at the top in the middle, in place of
        // the lower-case tabs; a click on the next category goes to its page.
        if (!AppSettings{}.metro_category_tabs) return 2776;
        app.settings_.metro_category_tabs = true; settle(); app.paint();
        if (!app.homeTabsShown() || app.homeTabs().size() != app.metro_pages_.size() ||
            app.renderer_.metroTabAt(metro_layout::left + 4, metro_layout::tabs_center) >= 0) return 2777;
        const auto next = library_tabs::home::tab(1, view.width, static_cast<float>(app.metro_page_));
        const size_t before = app.metro_page_;
        app.mouse_transition_until_ = 0;
        app.click((next.left + next.right) * 0.5f * view.scale, (next.top + next.bottom) * 0.5f * view.scale, false);
        if (app.metro_page_ != before + 1 || app.metroTabsFocused()) return 2778;
        settle();
        if (!capture(L"metro-category-tabs.png")) return 2779;
        app.homePage(-1); settle();
        app.settings_.metro_category_tabs = false;
    }
    // Mouse: a click on Biblioteka opens the library; on a game it only focuses it.
    settle();
    app.mouse_transition_until_ = 0;
    const auto click = [&](size_t index) {
        const auto& card = app.cards_[index];
        app.click((card.x + card.width() * 0.5f) * view.scale, (card.y + card.size * 0.5f) * view.scale, false);
    };
    const auto recent = *find(0, Role::recent);
    click(recent);
    if (app.selected_ != recent || app.pending_launch_ || app.screen_ != ScreenMode::shelves) return 2728;
    click(*find(0, Role::library));
    if (app.screen_ != ScreenMode::library || !app.library_category_.empty()) return 2729;
    app.back();
    if (!app.metroActive() || app.metro_page_ != 0) return 2730;
    // The game menu opens beside the tile; the layout is saved.
    app.metroFocus(start.begin); settle();
    if (app.menu_open_ || app.quick_open_ || app.friends_open_ || app.pending_launch_ || app.screen_ != ScreenMode::shelves) return 2750 + (app.menu_open_ ? 1 : 0) + (app.quick_open_ ? 2 : 0) + (app.friends_open_ ? 4 : 0) + (app.pending_launch_ ? 8 : 0);
    app.openContextMenu(); app.menu_progress_ = 1;
    if (!app.menu_open_) return 2731;
    if (!app.menuBeside() || !capture(L"metro-menu.png")) return 2770;
    app.dismissMenu(); app.menu_progress_ = 0;
    app.settings_dirty_ = true; app.saveSettings();
    if (loadAppSettings().home_layout != AppSettings::HomeLayout::metro) return 2732;
    // "Efekt 3D": the other pages stand back at 80% and come forward when focused.
    app.settings_.metro_3d = true; settle();
    const auto& next_tile = app.cards_[app.metro_pages_[1].begin];
    const auto next_rect = app.metro_tiles_[app.metro_pages_[1].begin].rect;
    if (std::abs(next_tile.size - next_rect.height() * metro_layout::depth_scale) > 1 ||
        std::abs(next_tile.x - (metro_layout::left + app.metro_pages_[1].left - app.metro_scroll_)) > 1) return 2747;
    if (!capture(L"metro-3d.png")) return 2748;
    app.homePage(1); settle();
    if (std::abs(app.cards_[app.metro_pages_[1].begin].size - next_rect.height()) > 1 ||
        std::abs(app.cards_[start.begin].size - app.metro_tiles_[start.begin].rect.height() * metro_layout::depth_scale) > 1) return 2749;
    app.homePage(-1); app.settings_.metro_3d = false;
    // Switching away leaves no Metro state behind.
    app.setHomeLayout(0); app.showCategory(0);
    if (app.metroActive() || app.metro_page_ != 0) return 2733;
    // Default layout, "Pionowe plakaty": game shelves become 2:3 posters side by side (the large
    // focused one too); the store shelf keeps its square logo tiles.
    app.openSettings(); app.settings_state_.selectCategory(0); app.buildSettingsRows();
    revealClassicSetting(app, settings_ui::Id::shelves_posters); // Wszystkie opcje wyglądu → "Opcje układu".
    const auto posters_row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(),
        [](const auto& row) { return row.id == settings_ui::Id::shelves_posters; });
    if (posters_row == app.settings_rows_.end() || posters_row->checked) return 2760;
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[0] = static_cast<int>(posters_row - app.settings_rows_.begin());
    app.settingsActivate(); app.saveSettings();
    if (!app.settings_.shelves_posters || !loadAppSettings().shelves_posters) return 2761;
    app.settings_state_.closeAll(); app.settings_state_.options = false; app.back(); app.showCategory(0); settle();
    if (!app.shelfPosters() || app.cards_.size() < 3) return 2762;
    // As wide as the square covers would be (so 1.5x as tall), side by side.
    const float square = carousel::smallSize(app.cards_.size(), app.renderer_.viewport().width, true);
    for (size_t i = 0; i < app.cards_.size(); ++i) {
        const auto& card = app.cards_[i];
        if (std::abs(card.aspect - 2.0f / 3.0f) > 0.001f || std::abs(card.width() - (i == app.selected_ ? 2 : 1) * square) > 1 ||
            (i + 1 < app.cards_.size() && app.cards_[i + 1].x < card.x + card.width() - 0.5f)) return 2763;
    }
    if (!app.renderer_.hasPoster(app.selected_) || !capture(L"shelves-posters.png")) return 2764;
    for (size_t c = 0; c < app.categories_.size(); ++c) if (app.categories_[c].id == "pc-platforms") {
        app.showCategory(c); settle();
        if (app.shelfPosters() || std::abs(app.cards_[0].aspect - 1) > 0.001f) return 2765;
    }
    app.settings_.shelves_posters = false; app.showCategory(0); settle();
    if (std::abs(app.cards_[0].aspect - 1) > 0.001f) return 2766;
    // "Wygląd Steam": posters, and Ostatnio grane as one row of one height with a wide first tile; the focus does not grow it.
    app.settings_.shelves_posters = app.settings_.shelves_steam = true; app.showCategory(0); settle();
    if (!app.shelfSteamRow() || app.cards_.size() < 3 || !isGame(app.cards_[0].game)) return 8340;
    if (!app.shelfWideCard(0) || app.shelfWideCard(1)) return 8341;
    for (const size_t focus : {size_t{0}, size_t{2}}) {
        app.selected_ = focus; settle();
        for (size_t i = 0; i < app.cards_.size(); ++i) {
            const auto& card = app.cards_[i];
            const float aspect = i == 0 ? 460.0f / 215.0f : 2.0f / 3.0f;
            if (std::abs(card.size - carousel::steam_row::height) > 1 || std::abs(card.aspect - aspect) > 0.001f) return 8342;
            if (i + 1 < app.cards_.size() && app.cards_[i + 1].x < card.x + card.width() - 0.5f) return 8343;
        }
    }
    if (!capture(L"shelves-steam.png")) return 8344;
    app.settings_.shelves_steam = false; app.showCategory(0); settle();
    if (app.shelfSteamRow() || app.shelfWideCard(0) || std::abs(app.cards_[0].aspect - 2.0f / 3.0f) > 0.001f) return 8345;
    app.settings_.shelves_posters = false; app.showCategory(0); settle();
    // "Kafelek „Wszystkie” w rzędach" on the default layout: first on game shelves (not Recent or
    // stores), a 2x2 preview of the covers (posters: a game's background); it opens the library.
    app.openSettings(); app.settings_state_.selectCategory(0); app.buildSettingsRows();
    revealClassicSetting(app, settings_ui::Id::shelves_category_all);
    const auto all_row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(),
        [](const auto& row) { return row.id == settings_ui::Id::shelves_category_all; });
    if (all_row == app.settings_rows_.end() || all_row->checked) return 2767;
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[0] = static_cast<int>(all_row - app.settings_rows_.begin());
    app.settingsActivate(); app.saveSettings();
    if (!app.settings_.shelves_category_all || !loadAppSettings().shelves_category_all) return 2768;
    app.settings_state_.closeAll(); app.settings_state_.options = false; app.back(); app.showCategory(0);
    if (app.cards_[0].game.kind == GameKind::category) return 2769;
    size_t action = 0;
    for (size_t c = 0; c < app.categories_.size(); ++c) if (app.categories_[c].id == "metro-0") action = c;
    app.categories_[action].selected = 0; app.showCategory(action);
    if (app.cards_.size() != app.categories_[action].games.size() + 1 || app.cards_[0].game.kind != GameKind::category || app.selected_ != 0) return 2771;
    deliver(app.cards_[1].game, CoverMessage::Kind::cover, pngFixture(120, 48, 48));
    settle();
    if (!app.renderer_.hasTileThumbnail(0, 0) || !capture(L"shelves-all-tile.png")) return 2772;
    app.launch();
    if (app.screen_ != ScreenMode::library || app.library_category_ != "metro-0") return 2773;
    app.back();
    if (app.screen_ != ScreenMode::shelves || app.selected_ != 0 || app.cards_[0].game.kind != GameKind::category) return 2774;
    app.settings_.shelves_posters = true; app.showCategory(action); settle();
    if (!app.renderer_.hasPoster(0) || !capture(L"shelves-all-tile-posters.png")) return 2775;
    app.settings_.shelves_posters = app.settings_.shelves_category_all = false;
    app.settings_dirty_ = true; app.saveSettings();
    return 0;
}
