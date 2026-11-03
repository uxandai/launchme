// Included after LauncherUiTest: the interface round of 2026-10-23. The default home's facts under
// the logo (the corner badge gives way), search results as posters and landscape tiles, Kinowy in
// one column with its facts, Flix's quick icons at the top (clicks open their screens), the power
// menu's groups, the PS5-like tile entrance, button colours, the focus gap (outline out or artwork
// in) and the home without games (why, and its three buttons). Artwork comes from
// fixtures; nothing is downloaded, launched or powered off. Captures: refresh-*.png.
#include "../src/ui/carousel.hpp"
#include "../src/ui/grid_layout.hpp"
#include "../src/ui/home_widgets.hpp"
#include "../src/ui/power_menu.hpp"

int LauncherUiTest::uiRefresh(HWND window) {
    Launcher app;
    app.settings_.launch_animation = false;
    app.settings_.glance = app.settings_.companion = false; // Their icons: checked with both on below.
    app.hwnd_ = window; app.execute_ = fakeExecute;
    app.power_actions_ = [] { return std::vector<PowerAction>{PowerAction::sleep, PowerAction::restart, PowerAction::shutdown}; };
    std::vector<PowerAction> requested;
    app.power_ = [&](PowerAction action) { requested.push_back(action); return true; };
    if (!app.renderer_.initialize(window)) return 7700;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) { app.paint(); return true; }
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    const auto deliver = [&](const Game& game, CoverMessage::Kind kind, std::vector<uint8_t> bytes) {
        auto message = std::make_unique<CoverMessage>();
        message->key = gameKey(game); message->generation = app.jobs_->generation.load(); message->kind = kind; message->bytes = std::move(bytes);
        app.onCover(std::move(message));
    };
    auto library = std::make_unique<LibraryMessage>();
    library->account = 1;
    for (uint32_t i = 0; i < 14; ++i) {
        Game game{7700 + i, L"Refresh game " + std::to_wstring(i)};
        game.last_played = i < 6 ? 1700000000 + i : 0;
        library->games.push_back(std::move(game));
    }
    app.onLibrary(std::move(library));
    for (const auto& game : app.public_library_) {
        const auto tint = static_cast<uint8_t>(40 + (game.app_id % 14) * 12);
        deliver(game, CoverMessage::Kind::cover, pngFixture(tint, 48, 48));
        deliver(game, CoverMessage::Kind::poster, pngFixture(tint, 40, 60));
        deliver(game, CoverMessage::Kind::landscape, pngFixture(tint, 92, 43));
        deliver(game, CoverMessage::Kind::hero, pngFixture(tint, 192, 108));
    }
    const auto view = app.renderer_.viewport();

    // The PS5-like entrance (the default): a fresh home grows its tiles from their centres, the
    // focused one first and already at its full focus size; afterwards every tile rests in place.
    if (app.settings_.tile_entrance != AppSettings::TileEntrance::grow) return 7701;
    app.settings_.home_layout = AppSettings::HomeLayout::shelves;
    app.showCategory(0);
    app.arrived_ = 0; app.entrance_lead_ = true;
    for (auto& card : app.cards_) { GameCard fresh; fresh.game = card.game; card = fresh; }
    const auto step = [&](int milliseconds) { app.last_frame_ -= std::chrono::milliseconds(milliseconds); app.tick(); };
    step(40); step(40);
    // A fresh screen first stands empty for a moment (its background only).
    if (app.cards_.size() < 4) return 7702;
    for (const auto& card : app.cards_) if (card.opacity != 0) return 7702;
    while (app.arrived_ < 0.5f) step(40);
    if (app.cards_[app.selected_].opacity <= 0 || app.cards_[app.selected_].emphasis < 0.99f || app.cards_.back().opacity != 0) return 7703;
    {
        // Mid-growth a tile is smaller than its rest size, still centred on its resting place.
        const auto& growing = app.cards_[app.selected_];
        const auto view_now = app.renderer_.viewport();
        const float rest = 2 * carousel::smallSize(app.cards_.size(), view_now.width, true);
        if (growing.size >= rest) return 7706;
    }
    settle();
    if (!capture(L"refresh-home-entrance-settled.png")) return 7704;
    for (const auto& card : app.cards_) if (card.opacity < 0.99f) return 7705;
    // The wave has played: another shelf's tiles start without the pause.
    if (app.entrance_lead_) return 7707;

    // The default home's facts under the logo: shown only with the option, only for games.
    if (app.heroFactsShown()) return 7710;
    app.settings_.shelves_hero_facts = true;
    if (!app.heroFactsShown()) return 7711;
    settle();
    if (!capture(L"refresh-home-facts.png")) return 7712;
    app.settings_.shelves_hero_facts = false;

    // Search results as 2:3 posters, then landscape tiles: the grid's geometry and artwork follow.
    app.settings_.search_art = AppSettings::SearchArt::poster;
    app.openSearch(); app.search_query_ = L"Refresh"; app.updateSearch(); settle();
    if (app.cards_.empty() || !app.libraryGridPosters() || std::abs(app.cards_[0].aspect - 2.0f / 3.0f) > 0.01f) return 7720;
    if (!capture(L"refresh-search-posters.png")) return 7721;
    app.back(); settle();
    app.settings_.search_art = AppSettings::SearchArt::landscape;
    app.openSearch(); app.search_query_ = L"Refresh"; app.updateSearch(); settle();
    if (app.cards_.empty() || !app.libraryGridLandscape() || app.libraryColumns() != landscape_grid::columns(view.width) ||
        std::abs(app.cards_[0].aspect - landscape_grid::aspect) > 0.01f || !app.renderer_.hasPoster(0)) return 7722;
    if (!capture(L"refresh-search-landscape.png")) return 7723;
    app.back(); settle();
    app.settings_.search_art = AppSettings::SearchArt::square;

    // Kinowy in one column: one capsule per row on the right, the focused one scrolled to the middle,
    // the synopsis with the platform, last played and playtime on the left.
    app.openLibrary(); settle();
    auto& options = app.library_controls_.options;
    options.view = library_view::View::cinematic;
    library_view::setCinematicScroll(options, 2);
    options.cinematic_platform = options.cinematic_played = options.cinematic_playtime = true;
    app.applyLibraryOptions(); settle();
    if (app.libraryColumns() != 1 || app.libraryHorizontalCinematic()) return 7730;
    app.focusCard(5); settle();
    {
        const auto& card = app.cards_[5];
        const float middle = (cinematic_column::list_top + view.height - cinematic_column::bottom_margin) * 0.5f;
        if (std::abs(card.x - cinematic_column::x(view.width)) > 1 || std::abs(card.y + card.size * 0.5f - middle) > 2) return 7731;
    }
    if (!capture(L"refresh-cinematic-column.png")) return 7732;
    // The scrolling option cycles down → sideways → one column → down.
    app.openLibraryPanel(library_ui::Panel::views); app.library_controls_.view_row = 1;
    app.adjustLibraryView(0);
    if (library_view::cinematicScroll(app.library_controls_.options) != 0) return 7733;
    app.adjustLibraryView(1);
    if (!app.library_controls_.options.cinematic_horizontal) return 7734;
    settle();
    if (!capture(L"refresh-cinematic-horizontal-facts.png")) return 7735;
    app.closeLibraryPanel();
    app.back(); settle();

    // The power menu: Tryb TV, then closing the launcher; the PC's actions apart and red; Anuluj last.
    app.openPowerMenu(); settle();
    if (app.power_items_.front() != power_menu::Item::tv_mode || app.power_items_[1] != power_menu::Item::exit) return 7740;
    {
        const auto geometry = app.powerMenuGeometry();
        const auto sleep = geometry.row(2);
        if (geometry.hit(sleep.left + 10, (sleep.top + sleep.bottom) * 0.5f) != 2) return 7741;
        if (!capture(L"refresh-power.png")) return 7742;
        app.click((sleep.left + 10) * view.scale, (sleep.top + sleep.bottom) * 0.5f * view.scale, false);
        if (app.powerMenuOpen() || requested != std::vector<PowerAction>{PowerAction::sleep}) return 7743;
    }

    // Buttons: the theme's fill and an ink that keeps the contrast; Szczegóły' three drawn dots.
    app.openDetails(app.cards_[app.selected_].game); settle();
    if (!capture(L"refresh-details.png")) return 7750;
    app.settings_.button_rgb = 0x0070D1; app.settings_.button_idle = AppSettings::ButtonIdle::tinted;
    app.settingsChanged(); settle();
    if (app.renderer_.buttonInkForTest(1).r < 0.9f || app.renderer_.buttonInkForTest(0).r < 0.9f) return 7751; // Light ink on blue.
    if (!capture(L"refresh-details-blue.png")) return 7752;
    app.settings_.button_rgb = 0xF5F5F5; app.settings_.button_idle = AppSettings::ButtonIdle::gray;
    app.settingsChanged();
    if (app.renderer_.buttonInkForTest(1).r > 0.1f || app.renderer_.buttonInkForTest(0).r < 0.9f) return 7753; // Dark on white, light on grey.
    app.back(); settle();

    // The focus gap: the outline an empty ring away from its tile (a visual-only switch).
    app.settings_.focus_gap = AppSettings::FocusGap::outside; app.settingsChanged(true); settle();
    if (!capture(L"refresh-focus-gap.png")) return 7760;
    // ...or the picture pulled inside its outline, the band empty.
    app.settings_.focus_gap = AppSettings::FocusGap::inside; app.settingsChanged(true); settle();
    if (!capture(L"refresh-focus-gap-inside.png")) return 7761;
    {
        // A new focus: its cover shrinks in while the previous one grows back, both eased (they
        // snapped in one frame on square tiles, e.g. in the search results).
        const size_t before = app.selected_;
        app.navigate(1, false);
        app.last_frame_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(30); app.tick();
        if (app.selected_ == before || app.cards_[before].emphasis < 0.05f || app.cards_[before].emphasis > 0.95f ||
            app.cards_[app.selected_].emphasis < 0.05f || app.cards_[app.selected_].emphasis > 0.95f) return 7763;
        if (!capture(L"refresh-focus-gap-inside-moving.png")) return 7764;
        settle();
    }
    app.settings_.home_layout = AppSettings::HomeLayout::flix; app.showCategory(0); settle();
    if (!capture(L"refresh-focus-gap-inside-flix.png")) return 7762;
    app.settings_.home_layout = AppSettings::HomeLayout::shelves; app.showCategory(0); settle();
    app.settings_.focus_gap = AppSettings::FocusGap::none; app.settingsChanged(true);

    // Flix with the menu's icons at the top: Glance and her avatar join the row; a click on an icon
    // opens its screen like the bottom bar.
    app.settings_.glance = app.settings_.companion = true;
    app.settings_.home_layout = AppSettings::HomeLayout::flix;
    app.settings_.flix_top_icons = true;
    app.showCategory(0); settle();
    if (!app.flixIconsShown()) return 7770;
    const int quick = quick_menu::count(static_cast<int>(app.plugin_menu_.size()));
    if (app.flixIconCount() != quick + 2) return 7771;
    {
        const auto spots = app.homeWidgetSpots();
        const auto glance = home_widgets::icons::at(quick, quick + 2, view.width), her = home_widgets::icons::at(quick + 1, quick + 2, view.width);
        if (!spots.glance || !spots.companion || spots.glance->left != glance.left || spots.companion->left != her.left ||
            her.right != view.width - home_widgets::badge_slot) return 7772;
    }
    if (!capture(L"refresh-flix-icons.png")) return 7773;
    // The pad: Up from the first row focuses the icons (no second bar at the bottom), Right walks them, the
    // focused one's name stands left of the row; Down returns to the tiles.
    app.changeCategory(-1);
    if (!app.flixIconsFocused() || app.quick_open_ || app.flix_icon_focus_ != 0) return 7776;
    app.navigate(1); settle();
    if (app.flix_icon_focus_ != 1 || !capture(L"refresh-flix-icons-focus.png")) return 7777;
    app.changeCategory(1);
    if (app.flixIconsFocused() || app.quick_open_) return 7778;
    {
        const auto search = home_widgets::icons::at(0, quick + 2, view.width);
        app.click((search.left + 20) * view.scale, (search.top + 20) * view.scale, false);
        if (app.screen_ != ScreenMode::search) return 7774;
        app.back(); settle();
    }
    app.settings_.flix_top_icons = false;
    if (app.homeWidgetSpots().glance->right - app.homeWidgetSpots().glance->left != home_widgets::glance_pill_width) return 7775;
    app.settings_.home_layout = AppSettings::HomeLayout::shelves;
    app.showCategory(0); settle();

    // No games at all: the home says why (no Steam on this PC, then Steam with nobody signed in)
    // and offers Dodaj grę, Platformy and Odśwież; Left/Right walk them, a click opens Platformy.
    {
        Launcher empty;
        empty.settings_.launch_animation = false;
        empty.settings_.glance = empty.settings_.companion = false;
        empty.hwnd_ = window; empty.execute_ = fakeExecute;
        empty.steam_executable_ = [] { return std::filesystem::path{}; };
        if (!empty.renderer_.initialize(window)) return 7780;
        auto nothing = std::make_unique<LibraryMessage>();
        nothing->account = 0;
        empty.onLibrary(std::move(nothing));
        empty.scanning_ = false; empty.pc_initial_ready_ = true;
        for (int i = 0; i < 40; ++i) { empty.last_frame_ -= std::chrono::milliseconds(50); empty.tick(); }
        if (!empty.cards_.empty() || !empty.emptyHomeActive()) return 7781;
        if (empty.emptyLibraryReason() != empty_library::Reason::no_steam || empty.empty_focus_ != 0) return 7782;
        for (const auto& category : empty.categories_)
            for (const auto& game : category.games)
                if (game.kind == GameKind::pc_platform && game.source == GameSource::steam) return 7783; // No Steam tile to an empty library.
        empty.enterDown(false); // Enter presses the focused button on key-down (Dodaj grę), as Space does.
        if (!empty.customEditorOpen()) return 7790;
        empty.back();
        if (empty.customEditorOpen() || !empty.emptyHomeActive()) return 7791;
        empty.navigate(1, true); empty.navigate(1, true); empty.navigate(1, true);
        if (empty.empty_focus_ != empty_library::action_count - 1) return 7784;
        empty.navigate(-1, true);
        if (empty.empty_focus_ != empty_library::platforms) return 7785;
        const auto shoot = [&](const wchar_t* name) {
            wchar_t folder[4096]{};
            const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
            if (!count || count >= 4096) { empty.paint(); return true; }
            empty.notifications_.clear();
            return empty.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { empty.paint(); });
        };
        if (!shoot(L"refresh-empty-home.png")) return 7786;
        empty.steam_executable_ = [] { return std::filesystem::path(L"C:\\Steam\\steam.exe"); };
        if (empty.emptyLibraryReason() != empty_library::Reason::no_account) return 7787;
        if (!shoot(L"refresh-empty-home-no-account.png")) return 7788;
        const auto empty_view = empty.renderer_.viewport();
        const auto button = empty.emptyHomeLayout().button(empty_library::platforms);
        empty.click((button.left + button.right) * 0.5f * empty_view.scale, (button.top + button.bottom) * 0.5f * empty_view.scale, false);
        if (!empty.settingsOpen()) return 7789;
    }
    return 0;
}
