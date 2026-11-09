// Included after LauncherUiTest: the home screen's 0.5.8 options. Flix → Pasek boczny (its icons, the
// pad's way in and out, an icon scrolling to its row, the mouse), Ikony zamiast nazw on the native PSfive
// and Metro tabs, Ulubione switched off (no shelf, desktop section, star or details button), and the
// Steam rows' switches. Nothing reaches the network. Captures: home-options-*.png.
int LauncherUiTest::homeOptions(HWND window) {
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 7900;
    app.settings_.launch_animation = false; app.settings_.tour_done = true;
    app.settings_.glance = app.settings_.companion = false;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) return true;
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    app.settings_.home_layout = AppSettings::HomeLayout::flix;
    app.settings_.enabled_pc_sources = {"gog"}; // Platformy PC: Steam and GOG.
    auto library = std::make_unique<LibraryMessage>();
    library->account = 1;
    for (uint32_t i = 0; i < 14; ++i) {
        Game game{7900 + i, L"Option game " + std::to_wstring(i)};
        game.last_played = 100 + i; game.installed = true;
        library->games.push_back(std::move(game));
    }
    library->collections.push_back(SteamCollection{L"Ulubione", {7900, 7901, 7902}, "user-collections.favorite"});
    library->collections.push_back(SteamCollection{L"Weekend", {7903, 7904, 7905}, "options-weekend"});
    app.onLibrary(std::move(library));
    // An emulated game: the Emulacja row.
    Game rom{0x90007900u, L"Emulated option game"}; rom.kind = GameKind::nonsteam; rom.platform = L"PS2"; rom.installed = true;
    app.shortcut_library_ = {rom};
    app.buildShelves(); app.showCategory(0);
    const auto rowOf = [&](const char* id) -> int {
        for (size_t i = 0; i < app.flix_rows_.size(); ++i) if (app.categories_[app.flix_rows_[i].category].id == id) return static_cast<int>(i);
        return -1;
    };
    if (!app.flixActive() || rowOf("pc-platforms") < 0 || rowOf("emulation") < 0 || rowOf("user-collections.favorite") < 0) return 7901;

    // ---- Flix → Pasek boczny: off by default; on, the rows move right of it.
    if (app.flixRailShown() || flix_layout::rail != 0) return 7902;
    app.settings_.flix_rail = true; app.buildShelves(); app.showCategory(0); settle();
    using flix_rail::Kind;
    const auto items = app.flixRailItems();
    const auto kindAt = [&](size_t i) { return i < items.size() ? items[i].kind : Kind::home; };
    if (!app.flixRailShown() || flix_layout::rail != flix_rail::width || items.size() < 8 || kindAt(0) != Kind::home ||
        kindAt(items.size() - 3) != Kind::profile || kindAt(items.size() - 2) != Kind::menu || kindAt(items.size() - 1) != Kind::widgets) return 7903;
    // Its categories in the page's order: Platformy PC, Emulacja, Ulubione; Weekend (no icon) is not there.
    std::vector<std::string> categories;
    for (const auto& item : items) if (item.kind == Kind::category) categories.push_back(app.categories_[item.category].id);
    if (categories != std::vector<std::string>{"pc-platforms", "emulation", "user-collections.favorite"}) return 7904;
    if (std::abs(app.cards_[0].x - flix_layout::leftEdge()) > 1) return 7905;
    if (!capture(L"home-options-flix-rail.png")) return 7906;
    // The pad: Left at a row's first tile enters it on the row in view's icon (Start); Down walks it;
    // A on Emulacja scrolls the page to its row; B goes back to the tile.
    app.navigate(-1);
    if (!app.flixRailFocused() || app.flix_rail_focus_ != 0 || app.flixRailActive(items) != 0) return 7907;
    const auto emulation_icon = static_cast<int>(std::find_if(items.begin(), items.end(), [&](const auto& item) {
        return item.kind == Kind::category && app.categories_[item.category].id == "emulation"; }) - items.begin());
    for (int i = 0; i < emulation_icon; ++i) app.changeCategory(1);
    if (app.flix_rail_focus_ != emulation_icon) return 7908;
    settle();
    if (!capture(L"home-options-flix-rail-focus.png")) return 7909;
    app.enterDown(false); app.enterUp();
    if (app.flixRailFocused() || static_cast<int>(app.flix_row_) != rowOf("emulation") || app.flixRailActive(app.flixRailItems()) != emulation_icon) return 7910;
    app.navigate(-1); app.back();
    if (app.flixRailFocused() || static_cast<int>(app.flix_row_) != rowOf("emulation")) return 7911;
    // The rail's Menu icon opens the quick bar from any row, not only the first (its row is Emulacja here).
    app.flixRailAccept(static_cast<int>(items.size()) - 2);
    if (!app.quick_open_ || app.flix_row_ == 0) return 8349;
    app.closeQuick(); settle();
    // The mouse: Start's icon scrolls back to the first row.
    {
        const auto view = app.renderer_.viewport();
        const auto rails = app.flixRailItems();
        const auto home = flix_rail::Layout{view.height, &rails}.item(0);
        const float x = (home.left + home.right) * 0.5f * view.scale, y = (home.top + home.bottom) * 0.5f * view.scale;
        app.mouse_transition_until_ = 0;
        app.click(x, y, false);
        if (app.flix_row_ != 0) return 7912;
    }
    app.settings_.flix_rail = false; app.buildShelves(); app.showCategory(0); app.tick();
    if (flix_layout::rail != 0) return 7913;

    // ---- Ikony zamiast nazw: native PSfive tabs (and Metro's) draw icons for the default categories.
    app.settings_.home_layout = AppSettings::HomeLayout::psfive; app.settings_.psfive_category_tabs = false;
    app.buildShelves(); app.showCategory(0);
    const auto pc = [&] { for (size_t i = 0; i < app.categories_.size(); ++i) if (app.categories_[i].id == "pc-platforms") return i; return size_t{0}; }();
    if (app.homeTabGlyph(pc) != 0) return 7914; // Off by default.
    app.settings_.home_tab_icons = true;
    if (app.homeTabGlyph(pc) != L'\uE770' || app.categoryGlyph(0) != 0) return 7915;
    settle();
    if (!capture(L"home-options-psfive-icons.png")) return 7916;
    app.settings_.home_layout = AppSettings::HomeLayout::metro; app.settings_.metro_category_tabs = false;
    app.buildShelves(); app.showCategory(0); settle();
    if (!capture(L"home-options-metro-icons.png")) return 7917;
    app.settings_.home_tab_icons = false;

    // ---- Biblioteka → Ekran główny i kolekcje → the order: a collection first, Platformy PC after it.
    // Every layout draws categories_ in that order (Metro's pages too).
    app.settings_.home_row_order = {"user-collections.favorite", "pc-platforms", "emulation"};
    const auto collection = [&](size_t i) {
        return i < app.categories_.size() && std::any_of(app.settings_collections_.begin(), app.settings_collections_.end(),
                                                         [&](const auto& c) { return c.id == app.categories_[i].id; });
    };
    for (const auto layout : {AppSettings::HomeLayout::metro, AppSettings::HomeLayout::xmb, AppSettings::HomeLayout::psfive}) {
        app.settings_.home_layout = layout;
        app.buildShelves(); app.showCategory(0); settle();
        // Weekend, not in the saved order, keeps following Ulubione (its row before it).
        const auto index = [&](const char* id) {
            for (size_t i = 0; i < app.categories_.size(); ++i) if (app.categories_[i].id == id) return i;
            return app.categories_.size();
        };
        if (app.categories_.size() < 5 || app.categories_[0].id != "recent" || app.categories_[1].id != "user-collections.favorite" ||
            !collection(2) || index("pc-platforms") != 3 || index("emulation") != 4) return 7952;
        if (layout == AppSettings::HomeLayout::metro) {
            // Metro's pages follow the rows (its start page's shortcut cells stay where they are).
            std::vector<size_t> pages;
            for (const auto& page : app.metro_pages_) pages.push_back(page.category);
            if (!std::is_sorted(pages.begin(), pages.end()) || std::find(pages.begin(), pages.end(), size_t{1}) == pages.end() ||
                std::find(pages.begin(), pages.end(), size_t{1}) > std::find(pages.begin(), pages.end(), size_t{3})) return 7953;
        }
        const auto name = layout == AppSettings::HomeLayout::metro ? L"home-options-order-metro.png"
                        : layout == AppSettings::HomeLayout::xmb ? L"home-options-order-xmb.png" : L"home-options-order-psfive.png";
        if (!capture(name)) return 7954;
    }
    app.settings_.home_row_order.clear();
    app.settings_.home_layout = AppSettings::HomeLayout::shelves; app.buildShelves();
    if (collection(1)) return 7955;

    // ---- Ulubione switched off (hidden like a collection): no shelf, no desktop section, no star.
    app.settings_.home_layout = AppSettings::HomeLayout::shelves;
    app.buildShelves(); app.showCategory(0);
    const Game starred{7900, L"Option game 0"};
    if (!app.favoritesShown() || !app.isFavorite(starred)) return 7918;
    app.settings_.hidden_collections.insert(collectionKey(app.account_, app.desktopFavorites()));
    app.buildShelves();
    // Off the home screen only: still a tab of Wszystkie gry, so still a collection (the star stays).
    if (!app.favoritesShown()) return 7584;
    app.settings_.hidden_library_tabs.insert(app.desktopFavorites().id); // Out of the tabs too: off everywhere.
    app.buildShelves();
    if (app.favoritesShown() || app.isFavorite(starred) ||
        std::any_of(app.categories_.begin(), app.categories_.end(), [](const auto& c) { return c.id == "user-collections.favorite"; })) return 7919;
    app.openDetails(starred);
    if (app.favoriteDetailAction() != -2 || app.collectionsDetailAction() < 0) return 7920;
    app.back();
    app.desktop_mode_ = true; app.audio_.setDesktop(true);
    app.buildDesktopSidebar();
    if (app.desktopEntryIndex("favorites") >= 0 || app.desktopLayout().favorite) return 7921;
    app.settings_.hidden_collections.clear(); app.settings_.hidden_library_tabs.clear();
    app.buildDesktopSidebar();
    if (app.desktopEntryIndex("favorites") < 0 || !app.desktopLayout().favorite) return 7922;
    app.desktop_mode_ = false; app.audio_.setDesktop(false);

    // ---- The Steam rows of an account with no collections: each on its own switch.
    app.settings_.show_updated_row = false; app.settings_.show_discovery_row = false;
    app.buildShelves();
    if (std::any_of(app.categories_.begin(), app.categories_.end(), [](const auto& c) { return c.id == "updated" || c.id == "discovery"; })) return 7923;
    return 0;
}
