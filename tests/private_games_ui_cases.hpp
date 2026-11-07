// Included after LauncherUiTest: Settings → Library → "Pokaż ukryte/prywatne gry". No worker
// runs here: the test hands onLibrary the snapshots the Steam worker would post.
#include "../src/library/search.hpp"

int LauncherUiTest::privateGames(HWND window) {
    Launcher app;
    app.settings_.menu_dialog = false; // Clicks land on the anchored menu's rows (the dialog has its own cases).
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 2700;
    constexpr uint32_t shortcut_id = 0x80000101u;
    // As read from disk: the split into public and private games comes from the scan.
    const auto snapshot = [](bool refresh) {
        auto library = std::make_unique<LibraryMessage>();
        library->account = 1; library->refresh = refresh;
        Game shown{5000, L"Public game"}; shown.last_played = 300;
        Game secret{5001, L"Private game"}; secret.last_played = 200; secret.visibility = GameVisibility::private_game;
        library->games = {shown}; library->private_games = {secret};
        library->excluded = {5001, 5002}; // 5002: hidden, not installed, still in a collection.
        library->collections.push_back({L"Fixture", {5000, 5001, 5002, shortcut_id}, "user-collections.fixture"});
        ShortcutGame rom; rom.game = Game{shortcut_id, L"Hidden shortcut"}; rom.game.kind = GameKind::nonsteam; rom.hidden = true;
        library->shortcuts.push_back(rom);
        return library;
    };
    const auto listed = [](const std::vector<Game>& games, uint32_t id) {
        return std::any_of(games.begin(), games.end(), [&](const Game& game) { return game.app_id == id; });
    };
    const auto shelf = [&]() -> const Launcher::Category* {
        const auto found = std::find_if(app.categories_.begin(), app.categories_.end(), [](const auto& item) { return item.id == "user-collections.fixture"; });
        return found == app.categories_.end() ? nullptr : &*found;
    };
    const auto toggle = [&] {
        app.openSettings();
        app.settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library));
        app.buildSettingsRows();
        app.openSettingsPage(settings_ui::Page::library_visibility); // Biblioteka → Co pokazywać.
        const auto row = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [](const auto& item) { return item.id == settings_ui::Id::show_private; });
        if (row == app.settings_rows_.end() || row->control != settings_ui::Control::toggle || row->checked != app.settings_.show_private) return false;
        app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
        app.settings_state_.selected[app.settings_state_.category] = static_cast<int>(row - app.settings_rows_.begin());
        const bool before = app.settings_.show_private;
        app.settingsActivate();
        return app.settings_.show_private != before && app.steam_rescan_->take() && app.settingsOpen();
    };

    // Off by default: hidden and private games stay in the Ctrl+H view only.
    app.onLibrary(snapshot(false));
    if (app.settings_.show_private || listed(app.public_library_, 5001) || listed(app.public_library_, 5002) ||
        listed(app.public_library_, shortcut_id) || !listed(app.private_category_.games, 5001) ||
        !listed(app.private_category_.games, shortcut_id) || !shelf() || listed(shelf()->games, 5001)) return 2701;
    {
        // Ctrl+H: one more category at the bottom, and the games join collections, grids and search
        // for this session only. They stay private: no first-seen record, no running-scan duplicate.
        const auto cards = [&](uint32_t id) {
            return std::any_of(app.cards_.begin(), app.cards_.end(), [&](const GameCard& card) { return card.game.app_id == id; });
        };
        app.library_state_.first_seen.erase(gameKey(Game{5001, L"Private game"}));
        app.togglePrivateCategory();
        const auto revealed = std::find_if(app.public_library_.begin(), app.public_library_.end(), [](const Game& game) { return game.app_id == 5001; });
        if (!app.private_shelf_ || app.categories_.back().id != "private" || app.categories_.back().title != L"Ukryte" || app.category_ + 1 != app.categories_.size() ||
            revealed == app.public_library_.end() || revealed->visibility != GameVisibility::private_game ||
            !listed(app.public_library_, shortcut_id) || !shelf() || !listed(shelf()->games, 5001) ||
            app.library_state_.first_seen.contains(gameKey(*revealed)) ||
            std::count_if(app.running_scan_->candidates.begin(), app.running_scan_->candidates.end(), [](const Game& game) { return game.app_id == 5001; }) != 1) return 2710;
        app.openLibrary();
        if (app.screen_ != ScreenMode::library || !cards(5001) || !cards(shortcut_id)) return 2711;
        // Hidden again from the library: it stays open, without them.
        app.togglePrivateCategory();
        if (app.private_shelf_ || app.screen_ != ScreenMode::library || cards(5001) || cards(shortcut_id) ||
            listed(app.public_library_, 5001) || app.categories_.back().id == "private") return 2712;
        app.togglePrivateCategory();
        if (!app.private_shelf_ || app.screen_ != ScreenMode::library || !cards(5001)) return 2713;
        app.openSearch(); app.search_query_ = L"private"; app.updateSearch();
        if (!cards(5001)) return 2714;
        app.back();
        app.togglePrivateCategory(); // From the shelves: back to the first category.
        app.openSearch(); app.search_query_ = L"private"; app.updateSearch();
        if (app.private_shelf_ || cards(5001)) return 2715;
        app.showCategory(0);
    }
    // The Library row switches it and asks the Steam worker for a new snapshot.
    if (!toggle()) return 2702;
    // The worker's refresh keeps Settings open and shows the games like any other.
    app.onLibrary(snapshot(true));
    if (!app.settingsOpen() || !listed(app.public_library_, 5001) || !listed(app.public_library_, 5002) ||
        !listed(app.public_library_, shortcut_id) || !app.private_category_.games.empty() || !app.excluded_ids_.empty() ||
        !shelf() || !listed(shelf()->games, 5001) || !listed(shelf()->games, 5002)) return 2703;
    if (searchPublicGames(app.public_library_, L"Private game").empty()) return 2704;
    // Installation updates also arrive as read from disk.
    auto progress = snapshot(false); progress->installation_update = true;
    progress->private_games[0].installation = {InstallPhase::downloading, 10, 100};
    app.onLibrary(std::move(progress));
    const auto revealed = std::find_if(app.public_library_.begin(), app.public_library_.end(), [](const Game& game) { return game.app_id == 5001; });
    if (revealed == app.public_library_.end() || revealed->visibility != GameVisibility::public_game ||
        !revealed->installation.active() || !app.private_category_.games.empty()) return 2705;

    // Switched off in Settings, the refresh lands on the shelves after leaving them: same row, same focus.
    if (!toggle()) return 2706;
    for (int i = 0; i < 4 && app.settingsOpen(); ++i) app.back();
    const auto index = shelf() ? static_cast<size_t>(shelf() - app.categories_.data()) : 0;
    if (app.settingsOpen() || !shelf()) return 2707;
    app.showCategory(index);
    const auto card = std::find_if(app.cards_.begin(), app.cards_.end(), [](const GameCard& item) { return item.game.app_id == 5000; });
    if (card == app.cards_.end()) return 2708;
    app.focusCard(static_cast<size_t>(card - app.cards_.begin()), false);
    app.onLibrary(snapshot(true));
    if (app.screen_ != ScreenMode::shelves || app.categories_[app.category_].id != "user-collections.fixture" ||
        app.cards_.empty() || app.cards_[app.selected_].game.app_id != 5000 ||
        std::any_of(app.cards_.begin(), app.cards_.end(), [](const GameCard& item) { return item.game.app_id == 5001 || item.game.app_id == 5002; }) ||
        listed(app.public_library_, 5001) || listed(app.public_library_, shortcut_id) ||
        !listed(app.private_category_.games, 5001) || !app.excluded_ids_.contains(5001)) return 2709;

    // Szczegóły → "…" → "Ukryj": it joins the Ctrl+H games, remembered by its key.
    {
        const auto capture = [&](const wchar_t* name) {
            wchar_t folder[4096]{};
            const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
            if (!count || count >= 4096) return true;
            InvalidateRect(window, nullptr, FALSE);
            return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
        };
        app.settings_.enabled_pc_sources.insert("gog");
        Game store; store.source = GameSource::gog; store.kind = GameKind::store; store.source_id = "4242";
        store.name = L"Store game"; store.last_played = 400;
        app.store_library_ = {store}; app.buildShelves();
        const auto key = gameKey(store);
        const auto find = [&](const std::vector<Game>& games) {
            return std::find_if(games.begin(), games.end(), [&](const Game& game) { return gameKey(game) == key; });
        };
        const auto in = [&](const std::vector<Game>& games) { return find(games) != games.end(); };
        if (!in(app.public_library_) || !in(app.categories_[0].games)) return 2716;
        app.showCategory(0);
        app.openDetails(store);
        const int hide = context_menu::row(context_menu::Item::hide, app.menuRows());
        if (hide < 0 || app.lastDetailAction() != app.collectionsDetailAction() || app.hiddenInLauncher(app.cards_[0].game)) return 2717;
        app.detail_action_ = 1; app.enterDown(false); app.enterUp();
        if (!app.menu_open_) return 2717;
        app.menu_selected_ = hide; app.menu_progress_ = 1;
        if (!capture(L"details-hide.png")) return 2724;
        app.notifications_.clear();
        app.enterDown(false); app.enterUp();
        const auto hidden = find(app.private_category_.games);
        if (app.screen_ == ScreenMode::details || !app.library_state_.hidden.contains(key) || in(app.public_library_) ||
            in(app.categories_[0].games) || hidden == app.private_category_.games.end() || hidden->visibility != GameVisibility::hidden ||
            !searchPublicGames(app.public_library_, L"Store game").empty() || app.notifications_.empty() ||
            app.notifications_.back().title != L"Gra ukryta") return 2718;
        // Ctrl+H shows it; its details offer "Pokaż" (here clicked), and it is public again.
        app.togglePrivateCategory();
        const auto unhidden = find(app.public_library_);
        if (!app.private_shelf_ || unhidden == app.public_library_.end() || unhidden->visibility != GameVisibility::hidden) return 2719;
        app.openDetails(*unhidden);
        if (context_menu::row(context_menu::Item::hide, app.menuRows()) < 0 || !app.hiddenInLauncher(app.cards_[0].game)) return 2720;
        app.detail_action_ = 1; app.enterDown(false); app.enterUp(); app.menu_progress_ = 1;
        if (!capture(L"details-show-hidden.png")) return 2725;
        const auto view = app.renderer_.viewport();
        const auto menu = context_menu::bounds(app.cards_[0], app.menuRows(), app.menuBeside(), view.width, view.height, app.libraryMenuAnchor());
        const float show_y = menu.top + 8 + context_menu::row(context_menu::Item::hide, app.menuRows()) * 53 + 24;
        app.mouse_transition_until_ = 0;
        app.click((menu.left + 30) * view.scale, show_y * view.scale, false);
        const auto shown = find(app.public_library_);
        if (app.screen_ != ScreenMode::details || app.library_state_.hidden.contains(key) || app.cards_[0].game.visibility != GameVisibility::public_game ||
            shown == app.public_library_.end() || shown->visibility != GameVisibility::public_game || in(app.private_category_.games)) return 2721;
        app.back(); app.togglePrivateCategory();
        if (app.private_shelf_ || !in(app.public_library_)) return 2722;
        // Public Steam games can now be hidden locally too.
        app.openDetails(Game{5000, L"Public game"});
        if (context_menu::row(context_menu::Item::hide, app.menuRows()) < 0) return 2723;
        app.back();
    }
    // Client-owned, never installed and not assigned to a collection (AION 2).
    app.settings_.show_uninstalled = true;
    app.settings_.show_private = false;
    auto owned = std::make_unique<LibraryMessage>(); owned->account = 2;
    Game aion{3393110, L"AION 2"}; aion.installed = false;
    owned->games = {aion};
    app.onLibrary(std::move(owned));
    if (!listed(app.public_library_, aion.app_id) || searchPublicGames(app.public_library_, L"AION").size() != 1 ||
        app.actionLabel(aion) != L"Instaluj") return 2726;
    app.settings_.show_uninstalled = false; app.buildShelves();
    if (listed(app.public_library_, aion.app_id)) return 2727;
    for (const auto& category : app.categories_) if (listed(category.games, aion.app_id)) return 2728;
    app.settings_.show_uninstalled = true; app.buildShelves();
    LibraryMessage owned_progress; owned_progress.account = 2; aion.installation = {InstallPhase::downloading, 42, 100}; owned_progress.games = {aion};
    app.updateSteamInstallations(owned_progress);
    const auto aionCount = [&](bool installed, uint64_t done) {
        return std::count_if(app.public_library_.begin(), app.public_library_.end(), [&](const Game& game) {
            return game.app_id == 3393110 && game.installed == installed && game.installation.done == done;
        });
    };
    if (aionCount(false, 42) != 1) return 2729;
    aion.installation = {}; aion.installed = true; owned_progress.games = {aion}; app.updateSteamInstallations(owned_progress);
    aion.installed = false; owned_progress.games = {aion}; app.updateSteamInstallations(owned_progress);
    if (aionCount(false, 0) != 1) return 2730;
    return 0;
}
