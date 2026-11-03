// Included after LauncherUiTest: the PSfive home layout (src/app/psfive.cpp). Recent as one row
// (row_games games, filled with unplayed installed ones, then the library tile), a category from
// the category menu (its first games, then "Wszystkie" opening its library), the geometry
// based on the PS5 (tiles enlarged 30%, fixed left margin), Enter/A and clicks down to "Graj" / "…", the game menu from "…", the top
// bar (three category tabs, overflow, Menu, the clock), the quick bar, Back, the tile size
// slider and the saved layout. Artwork comes from fixtures (the badge: the drawn white DRM-free logo).
// Captures: psfive-*.png, settings-psfive.png.
#include "../src/ui/psfive_layout.hpp"
#include "../src/ui/psfive_details_layout.hpp"
#include "../src/artwork/platform_logo.hpp"

int LauncherUiTest::psfive(HWND window) {
    namespace layout = psfive_layout;
    Launcher app;
    app.settings_.launch_animation = false;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.read_sensors_ = [] { return sensors::Reading{true, 48, 61}; };
    app.discord_.running = [] { return false; };
    if (!app.renderer_.initialize(window)) return 4900;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) return true;
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    const auto frame = [&] { app.last_frame_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(20); app.tick(); };
    app.settings_.home_layout = AppSettings::HomeLayout::psfive;
    app.settings_.psfive_size = AppSettings::XmbSize::normal;
    app.settings_.psfive_clock = true;
    app.settings_.psfive_details = false; // The quick bar under the buttons; the sheet under the row has its own block.
    app.settings_.psfive_category_tabs = false; // Its own tabs; "Pasek kategorii" has its own block.
    app.local_time_ = [] { SYSTEMTIME now{}; now.wYear = 2026; now.wMonth = 10; now.wDay = 7; now.wHour = 12; return now; };
    auto library = std::make_unique<LibraryMessage>();
    library->account = 1;
    for (uint32_t i = 0; i < 30; ++i) {
        Game game{4900 + i, L"PSfive game " + std::to_wstring(i)};
        game.last_played = i < 10 ? 100 + i : 0; // Ten played (4909 last), twenty never.
        library->games.push_back(std::move(game));
    }
    SteamCollection action{L"Akcja", {}};
    for (uint32_t i = 0; i < 18; ++i) action.app_ids.push_back(4900 + i);
    action.id = "psfive-action";
    library->collections.push_back(std::move(action));
    library->collections.push_back(SteamCollection{L"Ulubione", {4903, 4905, 4907, 4911, 4913}, "user-collections.favorite"});
    for (int i = 0; i < 20; ++i) {
        SteamCollection extra{L"Kategoria " + std::to_wstring(i), {}, "psfive-extra-" + std::to_string(i)};
        for (uint32_t j = 0; j < 18; ++j) extra.app_ids.push_back(4900 + j);
        library->collections.push_back(std::move(extra));
    }
    app.onLibrary(std::move(library));
    app.showCategory(0);
    if (!app.psfiveActive() || app.category_ != 0 || app.psfiveAllTile()) return 4901;
    // Twelve games (the ten played, latest first, then two installed ones never played) and the library.
    if (app.cards_.size() != layout::row_games + 1 || app.cards_.back().game.kind != GameKind::all) return 4902;
    if (app.cards_[0].game.app_id != 4909 || app.cards_[9].game.app_id != 4900 ||
        app.cards_[10].game.last_played || app.cards_[11].game.last_played || !isGame(app.cards_[11].game)) return 4903;
    const auto deliver = [&](const Game& game, CoverMessage::Kind kind, std::vector<uint8_t> bytes) {
        auto message = std::make_unique<CoverMessage>();
        message->key = gameKey(game); message->generation = app.jobs_->generation.load(); message->kind = kind; message->bytes = std::move(bytes);
        app.onCover(std::move(message));
    };
    for (const auto& game : app.public_library_) {
        const auto tint = static_cast<uint8_t>(40 + (game.app_id % 20) * 9);
        deliver(game, CoverMessage::Kind::cover, pngFixture(tint, 48, 48));
        deliver(game, CoverMessage::Kind::hero, pngFixture(tint, 192, 108));
        deliver(game, CoverMessage::Kind::logo, pngFixture(250, 120, 40, false, WICRect{6, 8, 108, 24}));
    }
    // The platform badge: a white logo on a transparent ground, drawn black on the white badge.
    auto badge = std::make_unique<CoverMessage>();
    badge->key = gameKey(app.cards_[0].game); badge->generation = app.jobs_->generation.load();
    badge->kind = CoverMessage::Kind::badge; badge->badge = activePlatformName(app.cards_[0].game); badge->bytes = drmFreeLogo();
    if (badge->bytes.empty()) return 4904;
    app.onCover(std::move(badge));
    settle();
    // The focused tile at the anchor, larger; the next ones after it, all on one line at the top.
    const auto view = app.renderer_.viewport();
    if (std::abs(app.cards_[0].x - layout::tabs_left) > 1 || std::abs(app.cards_[0].size - 182.0f) > 1 ||
        std::abs(app.cards_[0].y - layout::row_top) > 1) return 4905;
    if (std::abs(app.cards_[1].x - layout::tileLeft(1)) > 1 || std::abs(app.cards_[1].size - 114.4f) > 1 ||
        std::abs(app.cards_[1].y - layout::row_top) > 1 || app.cards_[0].emphasis < 0.99f) return 4906;
    // As measured on the PS5 (1920 x 1080 -> 1600 x 900): "Graj" 145 px over the bottom edge, clear
    // of the quick bar; the next tile and the name start 12 px after the focused tile.
    const auto play_rest = layout::action(0, view.height), play_raised = layout::action(0, view.height, 1, 1);
    if (std::abs(play_rest.bottom - (view.height - 145)) > 1 || play_raised.bottom > quick_menu::centerY(view.height, 1, false) - 44 ||
        std::abs(layout::labelLeft() - (layout::anchorX() + layout::focus_tile + layout::focus_gap)) > 0.5f) return 4950;
    if (!capture(L"psfive-row.png")) return 4907;
    {
        // Started on PSfive (psfive_layout::intro): an empty screen, the row sliding in from the right,
        // small, tile after tile; then the first one grows to its focused size. About 1.7 s.
        app.psfive_intro_ = true; app.arrived_ = 0;
        const auto step = [&](int frames) { for (int i = 0; i < frames; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
        step(4); // 0.2 s: still the bare background.
        if (!app.psfiveIntro() || app.cards_[0].opacity > 0.01f || app.cards_[3].opacity > 0.01f) return 4870;
        step(15); // 0.95 s: the first tile in place but small; a later one still on its way from the right.
        if (std::abs(app.cards_[0].x - layout::tabs_left) > 1 || std::abs(app.cards_[0].size - layout::tileSize(false)) > 1 ||
            app.cards_[0].emphasis > 0.01f || app.cards_[8].x < layout::tileLeft(8) + 100) return 4871;
        if (!capture(L"psfive-intro-row.png")) return 4872;
        step(8); // 1.35 s: the first growing, the row moving aside.
        if (app.cards_[0].size <= layout::tileSize(false) + 1 || !capture(L"psfive-intro-grow.png")) return 4873;
        step(10); // 1.85 s: done, as the row rests.
        if (app.psfive_intro_ || std::abs(app.cards_[0].size - 182.0f) > 1 || std::abs(app.cards_[1].x - layout::tileLeft(1)) > 1 ||
            app.cards_[0].emphasis < 0.99f) return 4874;
    }
    {
        // "Pasek kategorii" (on by default): every category on the library's strip in the middle at the
        // top, in place of the three tabs and "Więcej..."; Up from the row focuses it, LB/RB step on,
        // a click on the next category shows it.
        if (!AppSettings{}.psfive_category_tabs || app.category_ != 0) return 4875;
        app.settings_.psfive_category_tabs = true; settle(); app.paint();
        if (!app.homeTabsShown() || app.homeTabs().size() != app.categories_.size() ||
            app.psfiveTabCount() != static_cast<int>(app.categories_.size()) || app.renderer_.psfiveTabAt(layout::tabs_left + 4, layout::top_center) >= 0) return 4876;
        if (!capture(L"psfive-category-tabs.png")) return 4877;
        app.psfiveVertical(-1);
        if (!app.psfiveTopFocused() || app.psfive_top_ != 0) return 4878;
        app.homePage(1); app.homePage(1); app.homePage(1); settle();
        if (app.category_ != 3 || app.category_menu_open_ || app.psfive_top_ != 3 || !capture(L"psfive-category-tabs-focus.png")) return 4879;
        app.psfiveFocusTop(-1); app.showCategory(0); settle(); app.paint();
        const auto next = library_tabs::home::tab(1, view.width, 0);
        app.mouse_transition_until_ = 0;
        app.click((next.left + next.right) * 0.5f * view.scale, (next.top + next.bottom) * 0.5f * view.scale, false);
        if (app.category_ != 1 || app.psfiveTopFocused()) return 4880;
        app.showCategory(0); settle();
        app.settings_.psfive_category_tabs = false; settle(); app.paint();
        if (app.homeTabsShown() || app.renderer_.psfiveTabAt(layout::tabs_left + 4, layout::top_center) != 0) return 4881;
    }
    // A 12-hour clock's longest times fit its box (they were cut off, #3).
    for (const auto* time : {L"12:00 PM", L"12:59 AM", L"10:08 PM"}) {
        const float advance = app.renderer_.psfiveClockAdvanceForTest(time);
        if (advance <= 0 || advance > layout::clock_width - 4) return 4949;
    }
    if (app.psfiveTopActive() || app.psfive_top_opacity_.value != 0.66f) return 5026;
    // Capture the primary Steam status in the PSfive action area, including missing byte totals.
    {
        auto& install = app.cards_[app.selected_].game.installation;
        const auto saved = install;
        install = {InstallPhase::downloading, 37, 100, true};
        if (!capture(L"psfive-download.png")) return 5044;
        install = {InstallPhase::downloading, 0, 0, true};
        if (!capture(L"psfive-download-pending.png")) return 5045;
        install = {InstallPhase::paused, 68, 100, true};
        if (!capture(L"psfive-download-paused.png")) return 5046;
        install = saved;
    }
    // Right: the focus stays aligned with the tabs/logo/buttons. Earlier tiles leave under the
    // left clip and cannot be clicked in the margin.
    app.navigate(1); app.navigate(1); settle();
    if (app.selected_ != 2 || std::abs(app.cards_[2].x - layout::anchorX()) > 1 || std::abs(app.cards_[1].x - layout::tileLeft(-1)) > 1 ||
        std::abs(app.cards_[0].x - layout::tileLeft(-2)) > 1 || app.cards_[1].x + app.cards_[1].size >= layout::rowClipLeft() ||
        app.cardHit(1, app.cards_[1].x + app.cards_[1].size - 3, app.cards_[1].y + 10) ||
        std::abs(app.cards_[2].x - play_rest.left) > 1) return 4908;
    if (!capture(L"psfive-scrolled.png")) return 4909;
    // Enter/A on a game opens its details with "Szczegóły" as the default action (no launch); with
    // "Uruchom / instaluj" it goes down to "Graj".
    app.settings_.accept_action = AppSettings::AcceptAction::details;
    const auto picked_game = app.cards_[2].game.app_id;
    app.accept();
    if (app.screen_ != ScreenMode::details || app.pending_launch_ || app.detail_games_.empty() ||
        app.detail_games_.front().app_id != picked_game) return 4922;
    { // An installation on Szczegóły's primary button: its phase and percent over a track.
        auto& install = app.cards_[0].game.installation;
        const auto saved = install;
        install = {InstallPhase::downloading, 37, 100, true};
        if (!capture(L"details-download.png")) return 5057;
        install = saved;
    }
    app.back(); settle();
    if (app.screen_ != ScreenMode::shelves || app.selected_ != 2) return 4923;
    app.settings_.accept_action = AppSettings::AcceptAction::launch;
    app.accept();
    app.settings_.accept_action = AppSettings::AcceptAction::details;
    if (app.psfive_action_ != 0 || app.pending_launch_ || app.screen_ != ScreenMode::shelves) return 4910;
    settle();
    if (app.cards_[2].emphasis > 0.01f || std::abs(app.cards_[2].size - layout::focus_tile) > 1 || !app.psfive_action_glide_.valid) return 4911;
    if (!capture(L"psfive-play.png")) return 4912;
    { // Down from "Graj": the sheet as on Szczegóły (frosted cards, no title line), the screenshots
      // focused with the tiles' outline; Down to the similar games, Up back, Up again to "Graj".
        app.psfive_details_key_ = gameKey(app.cards_[app.selected_].game);
        app.psfive_shots_loading_ = false;
        app.psfive_shot_full_urls_.assign(5, L"https://example.invalid/shot.jpg");
        app.psfive_similar_games_.assign(app.public_library_.begin() + 12, app.public_library_.begin() + 18);
        app.renderer_.resetPsfiveDetails(5, app.psfive_similar_games_.size());
        for (size_t i = 0; i < 5; ++i) app.renderer_.setPsfiveShot(i, shareImage(pngFixture(static_cast<uint8_t>(60 + i * 30), 192, 108)));
        // Its details known (as read with the description): the facts and the achievements card.
        app.desktop_details_key_ = app.psfive_details_key_; app.desktop_details_preview_ = false; app.desktop_details_revision_ = app.game_index_revision_;
        app.desktop_details_ = {};
        app.desktop_details_.description = L"A fixture game about tiles, rows and sheets. Its description runs over several lines so the card shows how the text wraps and where it stops, the way Szczegóły does with its own Opis card.";
        app.desktop_details_.developer = L"Fixture Studio"; app.desktop_details_.genres = L"Akcja, RPG"; app.desktop_details_.players = L"1-2";
        app.desktop_details_.achievements_unlocked = 17; app.desktop_details_.achievements_total = 42;
        app.settings_.psfive_details = true;
        app.psfiveVertical(1); // Down from "Graj".
        if (app.psfive_details_ != 1 || app.psfiveActions()) return 5130;
        settle();
        if (!app.psfive_focus_glide_ || !capture(L"psfive-details.png")) return 5131;
        // Right along the screenshots: the strip slides as the tiles do (caught mid-way), the frame on its picture.
        for (int i = 0; i < 4; ++i) app.psfiveDetailsHorizontal(1);
        frame(); frame(); frame();
        if (app.psfive_shot_ != 4 || app.psfive_shots_scroll_ <= 0 || !capture(L"psfive-details-slide.png")) return 5137;
        settle();
        // Five pictures, four in view: the last one brings the strip one step along.
        if (std::abs(app.psfive_shots_scroll_ - (psfive_details::shot_width + psfive_details::shot_gap)) > 1) return 5138;
        if (!capture(L"psfive-details-scrolled.png")) return 5139;
        for (int i = 0; i < 4; ++i) app.psfiveDetailsHorizontal(-1);
        settle();
        app.psfiveDetailsVertical(1);
        if (app.psfive_details_ != 2) return 5132;
        settle();
        if (!capture(L"psfive-details-similar.png")) return 5133;
        app.psfiveDetailsVertical(-1); app.psfiveDetailsVertical(-1);
        if (app.psfive_details_ != -1 || app.psfive_action_ != 0) return 5134;
        app.settings_.psfive_details = false;
        settle();
    }
    // Right to "…", Left back to "Graj", Left again a boundary.
    app.navigate(1);
    if (app.psfive_action_ != 1) return 4913;
    app.navigate(-1); app.navigate(-1);
    if (app.psfive_action_ != 0 || app.selected_ != 2) return 4914;
    // "…" opens the game menu from the button; closing it keeps the focus there.
    app.navigate(1); app.enterDown(false);
    if (!app.menu_open_ || !app.libraryMenuAnchor() || app.enter_held_) return 4915;
    using MenuItem = context_menu::Item;
    if (context_menu::item(0, app.menuRows()) != MenuItem::details ||
        context_menu::row(MenuItem::primary, app.menuRows()) != -1 || app.menu_selected_ != 0) return 5051;
    // Details stays first with a live session and multiple plugin actions; their row offsets stay valid.
    auto session_menu = app.menuRows(); session_menu.suspend = session_menu.session = true; session_menu.plugins = 2;
    if (context_menu::item(0, session_menu) != MenuItem::details || context_menu::item(1, session_menu) != MenuItem::suspend ||
        context_menu::row(MenuItem::plugin, session_menu) != 2 || context_menu::item(3, session_menu) != MenuItem::plugin ||
        context_menu::item(context_menu::rows(session_menu) - 1, session_menu) != MenuItem::close) return 5052;
    app.menu_progress_ = 1; settle();
    const auto menu = context_menu::bounds(app.cards_[app.selected_], app.menuRows(), app.menuBeside(), view.width, view.height, app.libraryMenuAnchor());
    if (menu.bottom > layout::action(1, view.height).top || menu.left != layout::action(1, view.height).left) return 4916;
    if (!capture(L"psfive-menu.png")) return 4917;
    { // Duże menu gry: the same menu as a large dialog; the white button glides between the rows.
        app.settings_.menu_dialog = true;
        if (!capture(L"menu-dialog.png")) return 5135;
        app.menu_selected_ = 3; frame(); frame();
        if (!app.menu_glide_.valid || !capture(L"menu-dialog-glide.png")) return 5136;
        app.menu_selected_ = 0; settle();
        app.settings_.menu_dialog = false;
    }
    app.dismissMenu(); app.menu_progress_ = 0;
    if (app.psfive_action_ != 1) return 4918;
    app.enterDown(false); app.menuAccept(); // Enter/A on the initial row opens Details, never launches.
    if (app.screen_ != ScreenMode::details || app.pending_launch_ || app.menu_open_ || app.detail_games_.front().app_id != 4907) return 5053;
    app.back(); app.psfiveFocusAction(1);
    // "Graj" starts the game at once.
    app.navigate(-1); app.enterDown(false);
    if (!app.pending_launch_ || app.pending_game_.app_id != app.cards_[2].game.app_id) return 4919;
    app.pending_launch_ = false; app.launch_progress_ = 0;
    app.settings_.accept_action = AppSettings::AcceptAction::launch;
    // Up returns to the row, Down to "Graj" and again to the quick bar; Up closes the bar.
    app.changeCategory(-1);
    if (app.psfive_action_ != -1 || app.quick_open_) return 4920;
    app.changeCategory(1);
    if (app.psfive_action_ != 0) return 4921;
    app.changeCategory(1);
    if (!app.quick_open_) return 4922;
    app.quick_progress_ = 1; settle();
    if (!capture(L"psfive-quick.png")) return 4951;
    app.changeCategory(-1);
    if (app.quick_open_ || app.psfive_action_ != 0) return 4923;
    app.quick_progress_ = 0;
    // Back gives the focus back to the row. Up from the row: the top bar on the shown tab
    // (Ostatnie). Right onto a category shows its row at once; the fourth tab opens the remaining
    // categories, and the icon opens the quick bar. LB/RB change the visible category tab.
    app.back();
    if (app.psfive_action_ != -1 || app.screen_ != ScreenMode::shelves) return 4924;
    const auto tabs = app.psfiveTabs();
    if (tabs != std::vector<size_t>{0, 1, 2} || app.psfiveTabCount() != 4) return 4980;
    const int icons = app.psfiveTabCount();
    app.changeCategory(-1);
    if (app.quick_open_ || app.psfive_top_ != 0 || app.psfive_action_ != -1 || app.category_ != 0) return 4925;
    frame();
    if (!app.psfiveTopActive() || app.psfive_top_opacity_.value <= 0.66f || app.psfive_top_opacity_.value >= 1) return 5027;
    app.openContextMenu();
    if (app.menu_open_ || app.acceptHoldsForMenu()) return 4963;
    app.navigate(1);
    if (app.psfive_top_ != 1 || app.category_ != tabs[1] || app.cards_.empty()) return 4981;
    settle();
    if (!capture(L"psfive-tabs.png")) return 4982;
    if (app.psfive_top_opacity_.value != 1) return 5028;
    app.navigate(1);
    if (app.psfive_top_ != 2 || app.category_ != tabs[2]) return 4991;
    app.navigate(1);
    if (app.psfive_top_ != 3 || app.category_ != tabs[2] || !app.category_menu_open_) return 4992;
    if (!app.category_menu_open_ || app.category_cursor_ != 3) return 4993;
    app.categoryMenuMove(-1);
    if (app.category_cursor_ != 3) return 4994; // The three visible categories are never in the overflow.
    app.category_menu_progress_ = 0;
    app.last_frame_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(20); app.tick();
    const float opening = app.category_menu_progress_;
    if (opening <= 0 || opening >= 1) return 5017;
    app.paint();
    const auto unfolding = app.renderer_.psfiveMoreMenu(app.categories_.size());
    const auto revealed = unfolding.revealed(opening);
    if (revealed.top != unfolding.panel.top || revealed.bottom >= unfolding.panel.bottom ||
        unfolding.hit(unfolding.panel.left + 40, unfolding.bottom() - 2, 0, opening) >= 0) return 5018;
    if (!capture(L"psfive-more-opening.png")) return 5019;
    app.closeCategoryMenu();
    app.last_frame_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(20); app.tick();
    if (app.category_menu_progress_ >= opening) return 5020;
    app.openCategoryMenu();
    settle();
    if (!capture(L"psfive-more.png")) return 4995;
    // Selection glides even without scrolling; the list glides too when navigation reaches its edge.
    app.categoryMenuMove(1); frame();
    if (app.psfive_menu_motion_.row.value <= 3 || app.psfive_menu_motion_.row.value >= 4 || app.psfiveMenuScroll() != 0) return 5029;
    if (!capture(L"psfive-more-selection.png")) return 5030;
    app.categoryMenuMove(-1); settle();
    app.categoryMenuMove(100);
    if (app.category_cursor_ != static_cast<int>(app.categories_.size()) - 1 || app.category_menu_scroll_ <= 0) return 4996;
    const auto overflow = app.renderer_.psfiveMoreMenu(app.categories_.size());
    const float last_y = overflow.rowTop(app.category_cursor_, app.category_menu_scroll_);
    if (last_y < overflow.top() || last_y + overflow.row_height > overflow.bottom() + 0.5f) return 4997;
    frame();
    if (app.psfiveMenuScroll() <= 0 || app.psfiveMenuScroll() >= app.category_menu_scroll_ ||
        app.psfive_menu_motion_.row.value <= 3 || app.psfive_menu_motion_.row.value >= app.category_cursor_) return 5031;
    ViewContext moving;
    app.focusContext(moving);
    if (moving.category_scroll != app.psfiveMenuScroll() || !moving.psfive_menu_glide ||
        *moving.psfive_menu_glide != app.psfive_menu_motion_.row.value) return 5032;
    if (!capture(L"psfive-more-scroll.png")) return 5033;
    settle();
    if (std::abs(app.psfiveMenuScroll() - app.category_menu_scroll_) > 0.25f) return 5034;
    const float end_scroll = app.psfiveMenuScroll();
    app.categoryMenuMove(-100);
    if (app.category_cursor_ != 3 || app.category_menu_scroll_ != 0) return 4998;
    frame();
    if (app.psfiveMenuScroll() <= 0 || app.psfiveMenuScroll() >= end_scroll) return 5035;
    settle();
    app.back();
    if (app.category_menu_open_ || app.psfive_top_ != 3 || app.category_ != tabs[2]) return 4999;
    app.navigate(-1); app.navigate(1);
    if (!app.category_menu_open_) return 5010;
    app.categoryMenuHorizontal(-1, false); // Keyboard Left/A follows the top bar too.
    if (app.category_menu_open_ || app.psfive_top_ != 2 || app.category_ != tabs[2]) return 5011;
    app.controllerNavigate(1); // Pad Right opens More.
    app.categoryMenuMove(5); // Highlight a category different from the currently shown one.
    const auto before_right = app.category_;
    app.categoryMenuHorizontal(1, false); // Same entry point as WM_KEYDOWN Right/D.
    if (app.category_menu_open_ || app.psfive_top_ != icons + layout::top_widgets || app.category_ != before_right) return 5021;
    app.navigate(-1); // Back onto More; key repeat must not accept a category either.
    app.categoryMenuMove(3);
    app.categoryMenuHorizontal(1, true);
    if (app.category_menu_open_ || app.psfive_top_ != icons + layout::top_widgets || app.category_ != before_right) return 5047;
    app.enterDown(false);
    if (!app.friends_open_ || app.friends_list_ || app.quick_open_) return 5022;
    settle();
    if (!capture(L"psfive-widgets.png")) return 5023;
    if (app.psfiveTopActive() || app.psfive_top_opacity_.value != 0.66f) return 5036;
    app.back();
    if (app.friends_open_ || app.psfive_top_ != icons + layout::top_widgets) return 5024;
    app.friends_progress_ = 0;
    app.navigate(1);
    if (app.category_menu_open_ || app.psfive_top_ != icons + layout::top_menu || app.category_ != tabs[2]) return 4964;
    settle();
    if (app.cards_[app.selected_].emphasis > 0.01f || !capture(L"psfive-top.png")) return 4965;
    if (app.psfive_top_opacity_.value != 1) return 5037;
    app.enterDown(false);
    if (!app.quick_open_ || app.psfive_top_ != icons + layout::top_menu) return 4966;
    settle();
    if (app.psfiveTopActive() || app.psfive_top_opacity_.value != 0.66f) return 5038;
    app.changeCategory(-1);
    if (app.quick_open_ || app.psfive_top_ != icons + layout::top_menu) return 4967;
    app.changeCategory(-1);
    if (app.psfive_top_ != icons + layout::top_menu) return 4968; // Up from the top bar: a boundary.
    app.homePage(-1); app.homePage(-1); // LB twice: Ostatnie, remembering its tile.
    if (app.category_ != 0 || app.psfive_top_ != -1 || app.selected_ != 2) return 4983;
    app.homePage(-1);
    if (app.category_ != 0) return 4987;
    app.homePage(1); app.back(); // Back from another tab: Ostatnie.
    if (app.category_ != 0 || app.selected_ != 2) return 4984;
    app.changeCategory(-1); app.changeCategory(1);
    if (app.psfive_top_ != -1 || app.quick_open_) return 4926;
    app.changeCategory(-1); app.back();
    if (app.psfive_top_ != -1) return 4969;
    app.changeCategory(-1); app.accept(); // Enter/A on a tab: down to its row.
    if (app.psfive_top_ != -1 || app.screen_ != ScreenMode::shelves) return 4985;
    app.changeCategory(-1); app.navigate(1); app.navigate(1); app.navigate(1);
    // Merely entering "Więcej..." opens the list; no Enter/A is needed.
    if (!app.category_menu_open_ || app.psfive_top_ != 3) return 4970;
    app.categoryMenuMove(100); app.enterDown(false);
    if (app.category_menu_open_ || app.category_ != app.categories_.size() - 1 || app.psfive_top_ != -1 || app.psfiveActiveTab() != 3) return 4971;
    app.changeCategory(-1);
    if (app.psfive_top_ != 3 || !app.category_menu_open_) return 4986;
    app.back(); app.back(); app.back();
    if (app.category_ != 0 || app.selected_ != 2) return 4999;
    // A held Enter / Y on a tile: the menu beside the tile, not at "…".
    app.openContextMenu();
    if (!app.menu_open_ || app.libraryMenuAnchor()) return 4927;
    app.dismissMenu();
    // Another tile takes the focus back from the buttons.
    app.accept(); app.wheel(1, -1);
    if (app.psfive_action_ != -1 || app.selected_ != 3) return 4928;
    // The last tile is the library: no buttons, Down to the quick bar, Enter opens it at once.
    app.focusCard(app.cards_.size() - 1); settle();
    if (app.psfiveButtons() || app.cards_[app.selected_].game.kind != GameKind::all) return 4929;
    if (!capture(L"psfive-library.png")) return 4930;
    app.changeCategory(1);
    if (!app.quick_open_ || app.psfive_action_ != -1) return 4931;
    app.closeQuick(); app.quick_progress_ = 0;
    app.accept();
    if (app.screen_ != ScreenMode::library) return 4932;
    if (context_menu::item(0, app.menuRows()) != MenuItem::primary) return 5054; // PSfive selected, but outside its home view.
    app.back();
    if (!app.psfiveActive() || app.selected_ != app.cards_.size() - 1) return 4933;
    // Left at the start stays at the boundary. An overflow category shows its first games, then its
    // "Wszystkie" (a 2x2 preview of its covers), which opens the category's library.
    app.focusCard(0); app.navigate(-1);
    if (app.category_menu_open_ || app.psfive_top_ != -1 || app.selected_ != 0) return 4952;
    size_t action_category = 0;
    for (size_t c = 3; c < app.categories_.size(); ++c) if (app.categories_[c].id == "psfive-extra-0") action_category = c;
    if (!action_category) return 4953;
    app.openCategoryMenu();
    app.category_cursor_ = static_cast<int>(action_category); app.categoryMenuAccept();
    app.category_menu_progress_ = 0;
    if (app.category_ != action_category || !app.psfiveAllTile() || app.cards_.size() != layout::row_games + 1) return 4954;
    const auto& all = app.cards_.back().game;
    if (all.kind != GameKind::category || all.app_id != action_category || all.source_id != "psfive-extra-0") return 4955;
    app.focusCard(app.cards_.size() - 1); settle();
    if (!app.renderer_.hasTileThumbnail(app.cards_.size() - 1, 0) || app.psfiveButtons()) return 4956;
    if (!capture(L"psfive-category.png")) return 4957;
    app.accept();
    if (app.screen_ != ScreenMode::library || app.library_category_ != "psfive-extra-0") return 4958;
    app.back();
    if (!app.psfiveActive() || app.category_ != action_category || app.selected_ != app.cards_.size() - 1) return 4959;
    app.back();
    if (app.category_ != 0 || app.cards_.back().game.kind != GameKind::all) return 4960;
    // Mouse: a click on another tile focuses it, on the focused one goes down to "Graj"; "…" opens
    // the menu, "Graj" starts the game.
    settle();
    app.mouse_transition_until_ = 0;
    const auto clickAt = [&](float x, float y, bool double_click = false) { app.click(x * view.scale, y * view.scale, double_click); };
    const auto clickCard = [&](size_t index) { const auto& card = app.cards_[index]; clickAt(card.x + card.width() * 0.5f, card.y + card.size * 0.5f); };
    clickCard(3);
    if (app.selected_ != 3 || app.psfive_action_ != -1 || app.pending_launch_) return 4934;
    settle(); clickCard(3);
    if (app.psfive_action_ != 0 || app.pending_launch_) return 4935;
    const auto more = layout::action(1, view.height), play = layout::action(0, view.height);
    if (!app.hoverButton(((more.left + more.right) * 0.5f) * view.scale, ((more.top + more.bottom) * 0.5f) * view.scale)) return 4936;
    clickAt((more.left + more.right) * 0.5f, (more.top + more.bottom) * 0.5f);
    if (!app.menu_open_ || app.psfive_action_ != 1 || !app.libraryMenuAnchor()) return 4937;
    app.menu_progress_ = 1;
    const auto mouse_menu = context_menu::bounds(app.cards_[app.selected_], app.menuRows(), app.menuBeside(), view.width, view.height, app.libraryMenuAnchor());
    const auto details_id = app.cards_[app.selected_].game.app_id;
    clickAt(mouse_menu.left + 40, mouse_menu.top + 8 + 26); // The first visible row uses the same mapping as Enter/A.
    if (app.screen_ != ScreenMode::details || app.pending_launch_ || app.menu_open_ || app.detail_games_.front().app_id != details_id) return 5055;
    app.back(); app.menu_progress_ = 0; app.mouse_transition_until_ = 0;
    clickAt((play.left + play.right) * 0.5f, (play.top + play.bottom) * 0.5f);
    if (!app.pending_launch_ || app.pending_game_.app_id != app.cards_[3].game.app_id) return 4938;
    app.pending_launch_ = false; app.launch_progress_ = 0;
    // The Menu icon opens the quick bar; Widgets opens the right drawer.
    const auto menu_icon = layout::topItem(layout::top_menu, view.width, true);
    const auto widgets_icon = layout::topItem(layout::top_widgets, view.width, true);
    if (layout::clock(view.width).left - menu_icon.right > 30 || widgets_icon.right >= menu_icon.left) return 5025;
    const auto centre = [](const settings_ui::Rect& r) { return std::pair{(r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f}; };
    if (!app.hoverButton(centre(menu_icon).first * view.scale, centre(menu_icon).second * view.scale)) return 4972;
    app.mouse_transition_until_ = 0;
    clickAt(centre(menu_icon).first, centre(menu_icon).second);
    if (!app.quick_open_) return 4973;
    clickAt(view.width * 0.5f, view.height * 0.5f); // A click beside the bar closes it.
    if (app.quick_open_) return 4974;
    app.quick_progress_ = 0;
    clickAt(centre(widgets_icon).first, centre(widgets_icon).second);
    if (!app.psfiveActive() || app.quick_open_ || !app.friends_open_ || app.friends_list_) return 4975;
    app.back(); app.friends_progress_ = 0;
    // Mouse overflow: opens below the fourth tab, selects only later categories, dismisses outside.
    app.paint(); app.mouse_transition_until_ = 0;
    float more_x = -1;
    for (float x = layout::tabs_left; x < menu_icon.left && more_x < 0; x += 4)
        if (app.renderer_.psfiveTabAt(x, layout::top_center) == 3) more_x = x;
    if (more_x < 0) return 4976;
    app.mouseMove(more_x * view.scale, layout::top_center * view.scale);
    if (!app.category_menu_open_) return 5000;
    clickAt(more_x, layout::top_center);
    if (!app.category_menu_open_) return 5012; // Hover followed by a click does not close it.
    app.back();
    app.mouseMove((more_x + 1) * view.scale, layout::top_center * view.scale);
    if (app.category_menu_open_) return 5013; // Esc stays effective while the pointer remains on More.
    app.mouseLeave();
    app.mouseMove(more_x * view.scale, layout::top_center * view.scale);
    if (!app.category_menu_open_) return 5014;
    settle();
    const auto popup = app.renderer_.psfiveMoreMenu(app.categories_.size());
    if (popup.panel.left < more_x - 4 || popup.panel.top <= layout::top_center) return 5001;
    const float popup_x = popup.panel.left + 40, popup_y = popup.top() + popup.row_height * 0.5f;
    if (!app.hoverButton(popup_x * view.scale, popup_y * view.scale)) return 5002;
    app.mouseMove(popup_x * view.scale, (popup_y + popup.row_height) * view.scale);
    if (app.category_cursor_ != 4) return 5015;
    app.mouseMove(popup_x * view.scale, popup_y * view.scale);
    if (app.category_cursor_ != 3) return 5016;
    clickAt(popup_x, popup_y);
    if (app.category_menu_open_ || app.category_ != 3 || app.psfive_top_ != -1) return 5003;
    app.paint(); app.mouse_transition_until_ = 0;
    clickAt(more_x, layout::top_center);
    settle();
    app.scrollCategoryMenu(100);
    if (app.category_menu_scroll_ != popup.maxScroll()) return 5004;
    frame();
    // A click while the list is moving must select the visible row, not its future position.
    const auto moving_category = static_cast<size_t>(layout::visible_tabs +
        static_cast<int>((app.psfiveMenuScroll() + popup.row_height * 0.5f) / popup.row_height));
    const auto target_category = static_cast<size_t>(layout::visible_tabs +
        static_cast<int>((app.category_menu_scroll_ + popup.row_height * 0.5f) / popup.row_height));
    if (moving_category == target_category) return 5039;
    app.paint();
    clickAt(popup_x, popup_y);
    if (app.category_menu_open_ || app.category_ != moving_category) return 5040;
    app.paint(); app.mouse_transition_until_ = 0;
    clickAt(more_x, layout::top_center);
    app.scrollCategoryMenu(100);
    settle();
    clickAt(popup_x, popup.bottom() - 2);
    if (app.category_menu_open_ || app.category_ != app.categories_.size() - 1) return 5005;
    app.paint(); app.mouse_transition_until_ = 0;
    clickAt(more_x, layout::top_center);
    clickAt(10, 10);
    if (app.category_menu_open_ || app.psfive_top_ != 3 || app.category_ != app.categories_.size() - 1) return 5006;
    // A click on a tab shows its row (the tabs are where the last frame drew them).
    if (app.category_ != 0) app.showCategory(0);
    app.paint();
    float favorites_x = -1;
    for (float x = layout::tabs_left; x < view.width * 0.5f && favorites_x < 0; x += 4)
        if (app.renderer_.psfiveTabAt(x, layout::top_center) == 1) favorites_x = x;
    if (favorites_x < 0 || !app.hoverButton(favorites_x * view.scale, layout::top_center * view.scale)) return 4988;
    app.mouse_transition_until_ = 0;
    clickAt(favorites_x, layout::top_center);
    if (app.category_ != tabs[1] || app.psfive_top_ != -1) return 4989;
    app.paint(); app.mouse_transition_until_ = 0;
    clickAt(layout::tabs_left + 4, layout::top_center);
    if (app.category_ != 0) return 4990;
    // Pointer navigation lights the whole bar, including icons/clock, without taking keyboard focus.
    app.mouseMove((layout::tabs_left + 4) * view.scale, layout::top_center * view.scale); settle();
    if (!app.psfiveTopActive() || app.psfive_top_ != -1 || app.psfive_top_opacity_.value != 1) return 5041;
    app.mouseLeave(); settle();
    if (app.psfiveTopActive() || app.psfive_top_opacity_.value != 0.66f) return 5042;
    // Fewer categories: no empty overflow tab or invisible popup.
    auto saved_categories = app.categories_;
    app.categories_.resize(3);
    if (app.psfiveTabCount() != 3) return 5007;
    app.openCategoryMenu();
    if (app.category_menu_open_) return 5008;
    app.categories_.resize(1);
    if (app.psfiveTabCount() != 1 || app.psfiveTabs() != std::vector<size_t>{0}) return 5009;
    app.categories_ = std::move(saved_categories);
    // Settings → Interfejs: five layouts and, only on PSfive, the tile size slider.
    app.openSettings(); app.settings_state_.selectCategory(0); app.buildSettingsRows();
    app.openSettingsPage(settings_ui::Page::appearance_all); // The classic list: the studio has these options as pictures.
    const auto rowOf = [&](settings_ui::Id id) {
        const auto found = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const auto& row) { return row.id == id; });
        return found == app.settings_rows_.end() ? -1 : static_cast<int>(found - app.settings_rows_.begin());
    };
    // The layout's options are a tile right under its previews; their page has the slider first.
    const int layout_row = rowOf(settings_ui::Id::home_layout), options_row = layout_row + 1;
    if (rowOf(settings_ui::Id::psfive_size) >= 0 || options_row >= static_cast<int>(app.settings_rows_.size()) ||
        app.settings_rows_[options_row].control != settings_ui::Control::page || app.settings_rows_[options_row].page != settings_ui::Page::layout_options ||
        app.settings_rows_[layout_row].choice != static_cast<int>(AppSettings::HomeLayout::psfive)) return 4939;
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[0] = options_row;
    app.settingsActivate();
    const int size_row = rowOf(settings_ui::Id::psfive_size);
    if (!app.settings_state_.at(settings_ui::Page::layout_options) || size_row != 0 || app.settings_rows_[size_row].value != tr(L"Domyślny")) return 4948;
    app.settings_state_.selected[0] = size_row;
    app.settings_state_.editing = true;
    app.settingsAdjust(1);
    if (app.settings_.psfive_size != AppSettings::XmbSize::large || app.settings_rows_[size_row].value != tr(L"Duży") ||
        app.settings_.xmb_size != AppSettings::XmbSize::normal) return 4940;
    app.settingsAdjust(1);
    if (app.settings_.psfive_size != AppSettings::XmbSize::large) return 4941;
    if (!capture(L"settings-psfive.png")) return 4942;
    // "Zegar" under it: off, the icons move into the clock's place (drawing and hit tests).
    const int clock_row = rowOf(settings_ui::Id::psfive_clock);
    if (clock_row != size_row + 1 || !app.settings_rows_[clock_row].checked) return 4977;
    app.settings_state_.editing = false; app.settings_state_.selected[0] = clock_row;
    app.settingsActivate();
    if (app.settings_.psfive_clock || app.settings_rows_[clock_row].checked) return 4978;
    app.saveSettings();
    if (loadAppSettings().psfive_size != AppSettings::XmbSize::large || loadAppSettings().home_layout != AppSettings::HomeLayout::psfive ||
        loadAppSettings().psfive_clock) return 4943;
    if (layout::topItem(layout::top_menu, view.width, false).right <= layout::topItem(layout::top_menu, view.width, true).right ||
        layout::hitTop(centre(layout::topItem(layout::top_menu, view.width, false)).first, layout::top_center, view.width, false) != layout::top_menu) return 4979;
    app.settings_state_.options = app.settings_state_.editing = false; app.back(); app.showCategory(0); settle();
    // Every size is +30%: large focus 168 -> 218.4, small focus 112 -> 145.6.
    // Changing size never shifts the row/buttons from the shared left margin.
    const float large = layout::sizeScale(2);
    if (!app.psfiveActive() || std::abs(app.cards_[app.selected_].size - 218.4f) > 1 ||
        std::abs(app.cards_[app.selected_].x - layout::tabs_left) > 1 ||
        std::abs(app.cards_[app.selected_ + 1].size - 137.28f) > 1 ||
        layout::action(0, view.height, large).left != layout::tabs_left) return 4944;
    if (!capture(L"psfive-large.png")) return 4961; // Also: no clock, the icons in its place.
    app.settings_.psfive_size = AppSettings::XmbSize::compact; settle();
    if (std::abs(app.cards_[app.selected_].size - 145.6f) > 1 ||
        std::abs(app.cards_[app.selected_ + 1].size - 91.52f) > 1 ||
        std::abs(app.cards_[app.selected_].x - layout::tabs_left) > 1) return 4962;
    if (!capture(L"psfive-small.png")) return 5043;
    // Leaving PSfive: Recent is eight games again and the slider is gone.
    app.setHomeLayout(0); app.showCategory(0);
    if (app.psfiveActive() || app.cards_.size() != 9 || app.psfive_action_ != -1) return 4945;
    if (context_menu::item(0, app.menuRows()) != MenuItem::primary || app.menuRows().secondary_only) return 5056;
    app.openCategoryMenu();
    app.categoryMenuHorizontal(-1, true);
    if (!app.category_menu_open_) return 5048;
    app.categoryMenuHorizontal(-1, false);
    if (app.category_menu_open_) return 5049;
    app.openCategoryMenu(); app.categoryMenuMove(1);
    app.categoryMenuHorizontal(1, false); // The shelves' side drawer still accepts Right.
    if (app.category_menu_open_ || app.category_ != 1) return 5050;
    app.showCategory(0);
    app.openSettings(); app.settings_state_.selectCategory(0); app.buildSettingsRows();
    app.openSettingsPage(settings_ui::Page::appearance_all);
    if (rowOf(settings_ui::Id::psfive_size) >= 0) return 4946;
    app.setHomeLayout(static_cast<int>(AppSettings::HomeLayout::psfive));
    if (rowOf(settings_ui::Id::psfive_size) >= 0 || app.settings_rows_[layout_row + 1].page != settings_ui::Page::layout_options) return 4947;
    app.settings_.psfive_size = AppSettings::XmbSize::normal;
    app.settings_.psfive_clock = true;
    app.settings_.home_layout = AppSettings::HomeLayout::shelves;
    app.settings_dirty_ = true; app.saveSettings();
    return 0;
}
