// Included after LauncherUiTest: the library grid/list views, filter menu, search from the
// library, quick scroll letter, and the Flix home layout. Artwork comes from fixtures;
// nothing is downloaded and no description worker runs (the preview is injected).
#include "../src/ui/flix_layout.hpp"
#include "../src/ui/category_rail.hpp"
#include "../src/ui/media_layout.hpp"
#include "../src/artwork/platform_logo.hpp"
#include "disc_fixture.hpp"
#include <set>

int LauncherUiTest::libraryViews(HWND window) {
    Launcher app;
    app.settings_.menu_dialog = false; // The anchored menu's geometry is checked here (the dialog has its own cases).
    app.settings_.launch_animation = false; // Cancelled synthetic launches must not leave the cosmetic overlay running.
    app.settings_.accept_action = AppSettings::AcceptAction::launch; // Picking a card launches here; Szczegóły is checked on its own below.
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 900;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) return true;
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] {
            app.paint();
            if (app.libraryPreview()) { Sleep(360); app.paint(); } // Capture the settled backdrop, not the first cross-fade frame.
        });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    const auto frame = [&] { app.last_frame_ -= std::chrono::milliseconds(16); app.tick(); };
    const auto chooseView = [&](library_view::View mode) {
        app.openLibraryPanel(library_ui::Panel::views); app.libraryAccept();
        app.library_controls_.choice = library_view::drawerRow(mode); app.libraryAccept();
        app.closeLibraryPanel(); app.library_controls_.focus = -1; app.focusCard(app.selected_);
    };
    const wchar_t* titles[] = {L"Alan Wake", L"Bastion", L"Celeste", L"Dead Cells", L"Elden Ring", L"Factorio", L"Gris", L"Hades",
                               L"Inside", L"Journey", L"Katana Zero", L"Limbo", L"Minecraft", L"Noita", L"Outer Wilds", L"Portal 2",
                               L"Quake", L"Rimworld", L"Stardew Valley", L"Tunic", L"Undertale", L"Valheim", L"Wizard of Legend",
                               L"Xenonauts", L"Yooka-Laylee", L"Zelda Fan Game", L"2064: Read Only Memories", L"Łódź Story"};
    auto library = std::make_unique<LibraryMessage>();
    library->account = 1;
    for (uint32_t i = 0; i < std::size(titles); ++i) {
        Game game{3000 + i, titles[i]};
        game.last_played = 100;
        game.installed = i % 5 != 3;
        library->games.push_back(std::move(game));
    }
    app.onLibrary(std::move(library));
    app.openLibrary();
    for (size_t i = 0; i < app.cards_.size(); ++i) {
        auto cover = std::make_unique<CoverMessage>();
        cover->key = gameKey(app.cards_[i].game); cover->generation = app.jobs_->generation.load();
        cover->bytes = pngFixture(static_cast<uint8_t>(30 + i * 7), 48, 48);
        app.onCover(std::move(cover));
    }
    const auto columns = static_cast<size_t>(app.libraryColumns());
    if (app.libraryList() || columns < 3) return 901;
    // Left/Right stay in their row: Right at its end does nothing, Left at its start opens the view drawer.
    app.focusCard(columns - 1); app.navigate(1);
    if (app.selected_ != columns - 1) return 902;
    app.focusCard(columns); app.navigate(-1);
    if (app.selected_ != columns || app.library_controls_.panel != library_ui::Panel::views) return 903;
    app.library_views_progress_ = 1;
    if (!capture(L"library-views-drawer.png")) return 904;
    const auto focused = library_view::selectionKey(app.cards_[app.selected_].game);
    // Repeated Left stays on the selector, rather than repeatedly opening and closing the panel.
    for (int repeat = 0; repeat < 20; ++repeat) app.navigate(-1);
    if (app.library_controls_.panel != library_ui::Panel::views || app.library_controls_.view_dropdown) return 2830;
    for (int repeat = 0; repeat < 20; ++repeat) app.navigate(1);
    if (app.library_controls_.view_dropdown || app.library_controls_.panel != library_ui::Panel::views) return 2860;
    app.libraryAccept();
    frame();
    if (app.library_dropdown_progress_ <= 0 || app.library_dropdown_progress_ >= 1) return 2861;
    if (!capture(L"library-view-select-opening.png")) return 2862;
    const float drawer_height = app.renderer_.viewport().height;
    const float last_choice_y = library_ui::views::top(drawer_height) + (library_view::view_count - 0.5f) * library_ui::views::rowHeight(drawer_height);
    if (library_ui::views::hit(120, last_choice_y, drawer_height, app.library_dropdown_progress_) >= 0) return 2863;
    app.libraryClick(120, last_choice_y);
    if (!app.library_controls_.view_dropdown || app.library_controls_.choice != 0) return 2864;
    // A close/reopen during the reveal reverses from the currently visible extent.
    const float opening = app.library_dropdown_progress_;
    app.back(); frame();
    if (app.library_dropdown_progress_ <= 0 || app.library_dropdown_progress_ >= opening) return 2865;
    const float closing = app.library_dropdown_progress_;
    app.libraryAccept(); frame();
    if (app.library_dropdown_progress_ <= closing || app.library_dropdown_progress_ >= 1) return 2866;
    settle();
    if (!capture(L"library-view-select-expanded.png")) return 2859;
    app.changeCategory(1);
    if (!app.library_choice_glide_.valid || app.library_choice_glide_.value.value != 0) return 2600;
    frame();
    if (app.library_choice_glide_.value.value <= 0 || app.library_choice_glide_.value.value >= 1) return 2601;
    app.navigate(1); // Right neither opens the selector nor commits an expanded choice.
    if (!app.library_controls_.view_dropdown || app.libraryView() != library_view::View::grid) return 2867;
    app.libraryAccept(); // Enter/A commits "Lista".
    if (!app.library_controls_.open() || app.library_controls_.view_dropdown) return 2831;
    frame();
    if (app.library_dropdown_progress_ <= 0 || app.library_dropdown_progress_ >= 1) return 2868;
    if (!capture(L"library-view-select-closing.png")) return 2869;
    app.back();
    if (app.library_controls_.open() || !app.libraryList() || app.libraryColumns() != 1 ||
        library_view::selectionKey(app.cards_[app.selected_].game) != focused) return 905;
    app.saveLibraryPreferences();
    const auto saved_view = loadLibraryState(userConfigDirectory() / L"library-state.ini");
    if (!saved_view || saved_view->options.at("1:all").view != library_view::View::list) return 906;
    // List: Up/Down move one row, Right does nothing, Left opens the drawer again.
    const auto at = app.selected_;
    app.changeCategory(1);
    if (app.selected_ != at + 1) return 907;
    app.navigate(1);
    if (app.selected_ != at + 1 || app.library_controls_.open()) return 908;
    app.navigate(-1);
    if (app.library_controls_.panel != library_ui::Panel::views) return 909;
    app.changeCategory(-1); app.changeCategory(-1); // Up past the first view: over to the top bar's magnifier.
    if (app.library_controls_.open() || app.library_controls_.focus != library_ui::search_button || !app.libraryList()) return 910;
    app.changeCategory(1); // Down to the games again.
    if (app.library_controls_.focus != -1 || app.selected_ != at + 1) return 910;
    settle();
    if (std::abs(app.cards_[app.selected_].x - list_layout::thumbLeft()) > 1 ||
        std::abs(app.cards_[app.selected_].size - list_layout::thumb) > 1) return 911;
    {
        auto message = std::make_unique<PreviewMessage>();
        message->generation = app.preview_.generation();
        message->app_id = app.cards_[app.selected_].game.app_id;
        auto& preview = message->details;
        preview = GameDetails{
            L"A fixture description: two lines of store text shown under the cover of the focused game in the list view.",
            L"12 Nov 2020", L"Fixture Studio", L"Fixture Publisher", L"Action, Roguelike"};
        preview.playtime_minutes = 754; preview.achievements_unlocked = 7; preview.achievements_total = 51;
        app.onPreview(std::move(message));
    }
    auto hero = std::make_unique<CoverMessage>();
    hero->key = gameKey(app.cards_[app.selected_].game); hero->generation = app.jobs_->generation.load();
    hero->kind = CoverMessage::Kind::hero; hero->bytes = pngFixture(120, 192, 62);
    app.onCover(std::move(hero));
    // Rows carry their own metadata, including missing values; they are independent of the selection.
    for (size_t i = 0; i < app.cards_.size(); ++i) {
        if (i != app.selected_ && i % 4 != 0) {
            auto message = std::make_unique<PreviewMessage>();
            message->generation = app.preview_.generation(); message->app_id = app.cards_[i].game.app_id;
            message->details.release_date = std::to_wstring(1996 + i % 28) + L"-09-17";
            message->details.playtime_minutes = static_cast<uint32_t>(i * 137);
            app.onPreview(std::move(message));
        }
        if (i % 3 != 0) app.ratings_[gameKey(app.cards_[i].game)] = {ratings::Source::steam, 80.0 + i % 20, 100};
    }
    settle();
    if (!capture(L"library-list.png")) return 912;
    {
        const auto view = app.renderer_.viewport();
        const library_ui::Layout bar{view.width};
        const float scale = view.scale;
        app.focusCard(app.cards_.size() - 1); settle();
        if (app.grid_scroll_ < 165 || !library_view::pinnedHeader(app.libraryView()) ||
            app.cardHit(0, list_layout::left + 20, list_layout::list_top - 20) ||
            app.cardHit(app.selected_, list_layout::left + 20, view.height - 20)) return 2800;
        if (!capture(L"library-list-scrolled.png")) return 2801;
        for (const auto index : {library_ui::filters_button, library_ui::views_button}) {
            const auto button = bar.button(index);
            if (!app.hoverButton((button.left + 20) * scale, (button.top + 20) * scale)) return 2802;
            app.mouse_transition_until_ = 0;
            app.click((button.left + 20) * scale, (button.top + 20) * scale, false);
            if (app.library_controls_.panel != (index == library_ui::views_button ? library_ui::Panel::views : library_ui::Panel::filters)) return 2803;
            app.back(); app.back();
        }
        const auto search = bar.search();
        app.click((search.left + 20) * scale, (search.top + 20) * scale, false);
        if (!app.libraryTyping()) return 2804;
        app.back(); app.back();
        app.library_controls_.focus = library_ui::search_button;
        app.navigate(1);
        if (app.library_controls_.focus != library_ui::views_button) return 2805;
        app.launch();
        if (app.library_controls_.panel != library_ui::Panel::views) return 2806;
        app.library_views_progress_ = 1;
        if (!capture(L"library-views-icon-drawer.png")) return 2807;
        app.back(); app.back();
        app.grid_scroll_ = 0;
        app.focusCard(at + 1); settle();
    }
    const float old_highlight = app.library_row_glide_.box.top.value;
    app.changeCategory(1); frame();
    if (app.library_row_glide_.box.top.value <= old_highlight ||
        app.library_row_glide_.box.top.value >= list_layout::rowTop(app.cards_[app.selected_].y)) return 2602;
    if (!capture(L"library-list-glide.png")) return 2603;
    app.focusCard(at + 1); settle();
    // A click on another row focuses it.
    const auto scale = app.renderer_.viewport().scale;
    const auto& row = app.cards_[at];
    app.mouse_transition_until_ = 0;
    app.click((list_layout::left + 220) * scale, (row.y + row.size * 0.5f) * scale, false);
    if (app.selected_ != at) return 913;
    // An initial is useful only in alphabetical order. Other sortings never show it,
    // even if an older scroll run was already long enough to activate the overlay.
    for (const auto order : {library_view::Order::last_played, library_view::Order::added}) {
        app.library_controls_.options.order = order; app.applyLibraryOptions();
        app.fast_scroll_started_ = std::chrono::steady_clock::now() - std::chrono::seconds(2);
        app.fast_scroll_at_ = std::chrono::steady_clock::now();
        app.changeCategory(1); app.last_frame_ -= std::chrono::milliseconds(50); app.tick();
        if (app.fast_scroll_alpha_ != 0 || !app.scrollLetter().empty()) return 948;
    }
    app.library_controls_.options.order = library_view::Order::alphabetical; app.applyLibraryOptions();
    // Quick alphabetical scrolling: a held run dims the screen under the focused initial.
    app.changeCategory(1); app.changeCategory(1); app.changeCategory(1);
    app.last_frame_ -= std::chrono::milliseconds(50); app.tick();
    if (app.fast_scroll_alpha_ > 0) return 914; // A few quick presses are not enough.
    app.fast_scroll_started_ -= std::chrono::milliseconds(2100);
    app.changeCategory(1);
    app.last_frame_ -= std::chrono::milliseconds(50); app.tick();
    const auto& name = app.cards_[app.selected_].game.name;
    if (app.fast_scroll_alpha_ <= 0 || app.scrollLetter() != std::wstring(1, name[0])) return 914;
    app.fast_scroll_alpha_ = 1;
    if (!capture(L"library-fast-scroll.png")) return 915;
    app.fast_scroll_at_ -= std::chrono::seconds(1);
    settle();
    if (app.fast_scroll_alpha_ > 0.01f) return 916;
    const auto numbered = app.findCard("steam:3026", false), accented = app.findCard("steam:3027", false);
    if (!numbered || !accented) return 949;
    app.focusCard(*numbered);
    if (app.scrollLetter() != L"#") return 917;
    app.focusCard(*accented);
    if (app.scrollLetter() != L"Ł") return 918;
    app.fast_scroll_alpha_ = 1;
    app.library_controls_.options.order = library_view::Order::last_played; app.applyLibraryOptions();
    if (app.fast_scroll_alpha_ != 0 || !app.scrollLetter().empty()) return 950;
    // The top bar: Up from the first row, Right to the magnifier (after the filters), Enter unfolds
    // the library's own search field; typing filters the list in place (the home screen's search
    // stays untouched). Left from the filters is Back.
    app.focusCard(0); app.changeCategory(-1);
    if (app.library_controls_.focus != library_ui::filters_button) return 919;
    app.navigate(-1);
    if (app.library_controls_.focus != library_ui::back_button) return 936;
    app.navigate(-1); // Left past Back opens the views without leaving the library.
    if (app.screen_ != ScreenMode::library || app.library_controls_.panel != library_ui::Panel::views ||
        app.library_controls_.choice != library_view::drawerRow(library_view::View::list)) return 2707;
    app.back(); // Closing the panel restores Back and the same game.
    if (app.library_controls_.open() || app.library_controls_.focus != library_ui::back_button || app.selected_ != 0) return 2708;
    app.navigate(1); app.navigate(1); app.launch();
    if (app.screen_ != ScreenMode::library || !app.library_controls_.search || !app.libraryTyping() || !app.searching()) return 920;
    for (const wchar_t ch : std::wstring(L"CE")) app.onCharacter(ch); // Case-insensitive: Celeste, Dead Cells.
    app.library_query_edited_ -= std::chrono::milliseconds(200);
    app.last_frame_ -= std::chrono::milliseconds(50); app.tick();
    const auto names = [&] { std::vector<std::wstring> result; for (const auto& card : app.cards_) result.push_back(card.game.name); return result; };
    if (names() != std::vector<std::wstring>{L"Celeste", L"Dead Cells"} || !app.search_query_.empty() || !app.libraryList()) return 921;
    app.library_search_progress_ = 1; settle();
    if (!capture(L"library-search.png")) return 926;
    // Enter goes down to the results; Up returns to the field; Backspace edits it.
    app.enterDown(false);
    if (app.libraryTyping() || app.library_controls_.focus != -1) return 927;
    app.changeCategory(-1);
    if (!app.libraryTyping()) return 928;
    app.eraseSearchCharacter(); app.eraseSearchCharacter(); // Empty field keeps the full list, still open.
    app.library_query_edited_ -= std::chrono::milliseconds(200); app.tick();
    if (app.cards_.size() != std::size(titles) || !app.library_controls_.search) return 929;
    app.onCharacter(L'z'); app.applyLibraryQuery();
    // Accents fold: "z" also finds Łódź (ź), which sorts with L, as Polish does.
    if (names() != std::vector<std::wstring>{L"Katana Zero", L"Łódź Story", L"Wizard of Legend", L"Zelda Fan Game"}) return 930;
    // Back: the field closes with its filter (around the same game); the next Back leaves the bar.
    app.back();
    if (app.library_controls_.search || app.cards_.size() != std::size(titles) || app.library_controls_.focus != library_ui::search_button) return 931;
    app.back();
    if (app.screen_ != ScreenMode::library || !app.libraryList() || app.library_controls_.focus != -1 ||
        app.cards_[app.selected_].game.name != L"Katana Zero") return 932;
    // Typing on the library screen opens the field at once; a lone space does nothing.
    app.onCharacter(L' ');
    if (app.library_controls_.search || !app.libraryTypeAhead()) return 933;
    app.onCharacter(L'k');
    if (!app.libraryTyping() || app.library_controls_.query != L"k") return 934;
    app.back(); app.back();
    if (app.library_controls_.search || app.library_controls_.focus != -1 || app.screen_ != ScreenMode::library) return 935;
    // The filter menu by mouse: each choice applies and closes it; reopening shows the active choice.
    app.focusCard(0);
    const library_ui::Layout bar{app.renderer_.viewport().width};
    const auto button = bar.filters();
    app.mouse_transition_until_ = 0;
    app.click((button.left + 20) * scale, (button.top + 10) * scale, false);
    if (app.library_controls_.panel != library_ui::Panel::filters) return 922;
    const auto installed = bar.choice(library_view::order_count);
    app.click((installed.left + 20) * scale, (installed.top + 10) * scale, false);
    if (!app.library_controls_.options.installed_only || app.library_controls_.open() ||
        std::any_of(app.cards_.begin(), app.cards_.end(), [](const GameCard& card) { return !card.game.installed; })) return 923;
    for (const auto order : {library_view::Order::added, library_view::Order::alphabetical, library_view::Order::last_played}) {
        app.click((button.left + 20) * scale, (button.top + 10) * scale, false);
        const auto choice = bar.choice(static_cast<int>(order));
        app.click((choice.left + 20) * scale, (choice.top + 10) * scale, false);
        if (app.library_controls_.open() || app.libraryOptions().order != order) return 2713;
    }
    app.click((button.left + 20) * scale, (button.top + 10) * scale, false);
    app.library_menu_progress_ = 1;
    if (!capture(L"library-filter-menu.png")) return 924;
    app.click(40 * scale, 600 * scale, false);
    if (app.library_controls_.open()) return 925;
    // Szczegóły: the achievements row (with its bar) is the last action; it opens the list over the details.
    app.openDetails(app.cards_[app.selected_].game);
    app.details_.achievements_unlocked = 2; app.details_.achievements_total = 5; app.details_.playtime_minutes = 95;
    app.details_loading_ = false;
    const int achievements_row = app.achievementsDetailAction();
    if (achievements_row < 0) return 940;
    app.navigate(10);
    if (app.detail_action_ != achievements_row) return 941;
    if (!capture(L"details-achievement-row.png")) return 946;
    app.launch();
    if (!app.achievementsOpen() || app.achievement_jobs_->jobs.size() != 1) return 942;
    auto list = std::make_unique<AchievementsMessage>();
    list->generation = app.achievement_jobs_->generation.load();
    list->list.total = 5; list->list.unlocked = 2;
    list->list.items = {{"A", L"Pierwsza krew", L"Wygraj pierwszą walkę.", L"", true, false, 1700000000, 61.3f},
                        {"B", L"Kolekcjoner", L"Zbierz wszystkie karty z pierwszego rozdziału.", L"", false, false, 0, 12.5f},
                        {"C", L"", L"", L"", false, true, 0, 1.1f}};
    app.onAchievements(std::move(list));
    if (app.achievements_.items.size() != 3 || app.achievements_.items[2].name != L"Ukryte osiągnięcie" ||
        app.achievements_.rows() != 4 || app.achievements_.loading) return 943;
    if (!capture(L"details-achievements.png")) return 947;
    app.changeCategory(1);
    if (app.achievements_.selected != 1) return 944;
    app.back();
    if (app.achievementsOpen() || app.screen_ != ScreenMode::details) return 945;
    app.back();
    // The two additional presentations share filtering, saved scopes and game identity,
    // but use their own geometry; only Detailed has an inline action strip.
    app.library_controls_.options = {};
    app.applyLibraryOptions(); app.focusCard(0);
    app.library_controls_.focus = -1; // The games (the filter button kept it after its menu).
    app.navigate(-1); // Left in the first column: the view drawer (the top bar has no views button).
    if (app.library_controls_.panel != library_ui::Panel::views) return 1975;
    app.library_views_progress_ = 1;
    const auto viewport = app.renderer_.viewport();
    app.libraryAccept(); // Expand the view selector.
    settle();
    const float cinema_y = library_ui::views::top(viewport.height) + (library_view::drawerRow(library_view::View::cinematic) + 0.5f) * library_ui::views::rowHeight(viewport.height);
    app.click(120 * scale, cinema_y * scale, false);
    app.closeLibraryPanel(); app.library_controls_.focus = -1;
    if (!app.libraryCinematic() || app.library_controls_.open() || app.selected_ != 0) return 1950;
    for (size_t i = 0; i < app.cards_.size(); ++i) {
        auto image = std::make_unique<CoverMessage>();
        image->key = gameKey(app.cards_[i].game); image->generation = app.jobs_->generation.load();
        image->kind = CoverMessage::Kind::landscape;
        image->bytes = i == 1 ? pngFixture(45, 160, 90) : pngFixture(static_cast<uint8_t>(40 + i * 5), 460, 215);
        app.onCover(std::move(image));
    }
    auto cinematic_hero = std::make_unique<CoverMessage>();
    cinematic_hero->key = gameKey(app.cards_[0].game); cinematic_hero->generation = app.jobs_->generation.load();
    cinematic_hero->kind = CoverMessage::Kind::hero; cinematic_hero->bytes = pngFixture(70, 320, 180);
    app.onCover(std::move(cinematic_hero));
    settle();
    if (std::abs(app.cards_[0].aspect - 920.0f / 430.0f) > 0.001f || !app.renderer_.hasPoster(0) || app.libraryColumns() < 2) return 1951;
    if (!capture(L"library-cinematic.png")) return 1952;
    {
        // Kinowy: a playing video (Filmy w tle) fills the focused tile, not the whole screen.
        std::vector<uint8_t> video(64 * 36 * 4, 0); for (size_t p = 0; p < video.size(); p += 4) { video[p] = 40; video[p + 1] = 200; video[p + 2] = 90; video[p + 3] = 255; }
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        // The capture's own target: the frame is uploaded there, already faded in.
        if (count && count < 4096 && !app.renderer_.captureForTest(std::filesystem::path(folder) / L"library-cinematic-video.png", [&] {
                app.renderer_.setVideoFrame(video.data(), 64, 36); app.renderer_.settleVideoForTest(); app.paint(); })) return 2789;
        app.renderer_.clearVideo();
    }
    // A queued logo suppresses the title before the renderer even receives bytes. A known
    // miss permits the fallback; a later successful lookup binds the logo to the same game.
    if (!app.selectedLogoPending()) return 2631;
    const auto logo_request = gameKey(app.cards_[0].game) + ':' + std::to_string(static_cast<int>(CoverMessage::Kind::logo));
    {
        std::lock_guard lock(app.jobs_->mutex);
        app.jobs_->requested.erase(logo_request);
        app.jobs_->blocked_until[logo_request] = std::chrono::steady_clock::now() + std::chrono::minutes(1);
    }
    if (app.selectedLogoPending()) return 2632;
    if (!capture(L"library-cinematic-title-fallback.png")) return 2633;
    auto logo = std::make_unique<CoverMessage>();
    logo->key = gameKey(app.cards_[0].game); logo->generation = app.jobs_->generation.load();
    logo->kind = CoverMessage::Kind::logo; logo->bytes = pngFixture(180, 320, 80, true);
    // Optional reproduction with the exact selected local logo; no network or user-cache writes.
    wchar_t logo_file[4096]{};
    if (GetEnvironmentVariableW(L"LAUNCHME_UI_LOGO_IMAGE", logo_file, 4096)) {
        std::ifstream input(std::filesystem::path(logo_file), std::ios::binary);
        logo->bytes = std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
        Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> source;
        if (SUCCEEDED(wic->CreateDecoderFromFilename(logo_file, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf())) &&
            SUCCEEDED(decoder->GetFrame(0, source.GetAddressOf()))) {
            UINT w = 0, h = 0; source->GetSize(&w, &h);
            std::cout << "Selected logo source: " << w << 'x' << h << ", bytes=" << logo->bytes.size() << '\n';
        }
        const auto decoded = decodeImage(wic.Get(), logo->bytes, {false, false, true});
        std::cout << "Selected logo decode: " << decoded.width << 'x' << decoded.height << ", ink=" << decoded.ink.valid << '\n';
        if (!decoded.valid()) return 2635;
    }
    app.onCover(std::move(logo));
    if (app.selectedLogoPending() || !capture(L"library-cinematic-logo.png")) return 2634;
    app.focusCard(1);
    if (app.cards_[0].emphasis < 0.99f || app.cards_[1].emphasis > 0.01f) return 2604; // No one-frame zoom jump.
    frame();
    if (app.cards_[0].emphasis >= 1 || app.cards_[0].emphasis <= 0 || app.cards_[1].emphasis <= 0 || app.cards_[1].emphasis >= 1) return 2605;
    app.focusCard(0); frame(); settle(); // Reversing an unfinished zoom still settles.
    const auto cinema_columns = static_cast<size_t>(app.libraryColumns());
    app.focusCard(cinema_columns - 1); app.navigate(1);
    if (app.selected_ != cinema_columns - 1) return 1953;
    app.changeCategory(1);
    if (app.selected_ != 2 * cinema_columns - 1) return 1954;
    app.focusCard(app.cards_.size() - 1); settle();
    const auto& bottom = app.cards_[app.selected_];
    if (bottom.y < cinematic_layout::list_top - 1 || bottom.y + bottom.size + 48 > viewport.height - 29) return 1955;
    const float rows_scrolled = app.grid_scroll_ / cinematic_layout::step(viewport.width);
    if (std::abs(rows_scrolled - std::round(rows_scrolled)) > 0.001f) return 2606;
    if (app.cardHit(0, app.cards_[0].x + 10, 200)) return 1956; // Header occludes scrolled tiles.
    if (!capture(L"library-cinematic-scrolled.png")) return 1957;
    const auto cinema_bar = library_ui::Layout{viewport.width}.filters();
    if (!app.hoverButton((cinema_bar.left + 20) * scale, (cinema_bar.top + 20) * scale)) return 1976;
    app.click((cinema_bar.left + 20) * scale, (cinema_bar.top + 20) * scale, false);
    if (app.library_controls_.panel != library_ui::Panel::filters) return 1977;
    app.back(); app.back();
    app.focusCard(0); settle(); app.changeCategory(-1);
    if (app.library_controls_.action >= 0 || app.library_controls_.focus != library_ui::filters_button) return 1958;
    app.changeCategory(1);
    // The old cinematic buttons leave no invisible click targets or keyboard focus stops.
    for (const float x : {177.0f, 408.0f, 637.0f}) {
        if (app.hoverButton(x * scale, 334 * scale)) return 1978;
        app.click(x * scale, 334 * scale, false);
        if (app.pending_launch_ || app.menu_open_ || app.screen_ != ScreenMode::library || app.library_controls_.action >= 0) return 1979;
    }
    if (std::abs(app.cards_[0].y - 390.0f) > 1) return 1980; // Keep the space above the tiles.
    // Enter on the tile still uses the normal launch animation; no game is executed.
    app.enterDown(false); app.enterUp();
    if (!app.pending_launch_ || gameKey(app.pending_game_) != gameKey(app.cards_[0].game)) return 1959;
    app.pending_launch_ = false; app.launch_progress_ = 0;
    if (app.screen_ != ScreenMode::library || app.library_controls_.action >= 0) return 1960;
    app.openDetails(app.cards_[app.selected_].game);
    if (app.screen_ != ScreenMode::details) return 1961;
    app.back();
    if (!app.libraryCinematic() || app.selected_ != 0) return 1962;
    // A PC library has its own remembered view; the all-games choice stays cinematic.
    app.openPcLibrary(GameSource::steam);
    chooseView(library_view::View::detailed);
    if (app.libraryView() != library_view::View::detailed || app.libraryColumns() != 1) return 1963;
    app.changeCategory(1);
    if (app.selected_ != 1 || app.library_controls_.action >= 0) return 1964;
    app.focusCard(0);
    auto detailed_preview = std::make_unique<PreviewMessage>();
    detailed_preview->generation = app.preview_.generation(); detailed_preview->app_id = app.cards_[0].game.app_id;
    detailed_preview->details = {L"A larger inline preview keeps the game library visible while showing its artwork, description and actions.",
        L"12 Nov 2020", L"Fixture Studio", L"Fixture Publisher", L"Action, Adventure"};
    detailed_preview->details.playtime_minutes = 754;
    detailed_preview->details.achievements_total = 51; detailed_preview->details.achievements_unlocked = 7;
    app.onPreview(std::move(detailed_preview));
    settle();
    if (!capture(L"library-detailed.png")) return 1965;
    {
        // A name too long for its row fades out on the right; the focused one glides (and asks for frames).
        const auto saved = app.cards_[0].game.name, other = app.cards_[2].game.name;
        app.cards_[0].game.name = app.cards_[2].game.name = L"The Legend of Heroes: Trails through Daybreak II Deluxe Edition";
        app.paint();
        if (!app.renderer_.marqueeActive()) return 2786;
        if (!capture(L"library-detailed-long-title.png")) return 2787;
        app.cards_[0].game.name = saved; app.cards_[2].game.name = other;
        app.paint();
        if (app.renderer_.marqueeActive()) return 2788;
    }
    // A very long title and missing art/data still leave the actions, description and facts in their cards.
    {
        app.focusCard(1); settle();
        const auto other_name = app.cards_[1].game.name;
        app.cards_[1].game.name = L"A very long game title — The Complete Definitive Anniversary Edition with Additional Chapters";
        if (!capture(L"library-detailed-title-fallback.png")) return 2808;
        app.cards_[1].game.name = other_name;
        app.focusCard(0); settle();
    }
    app.navigate(1);
    if (app.library_controls_.action != 0) return 1966;
    app.navigate(1); app.navigate(1);
    if (app.library_controls_.action != 1) return 1998; // No hidden third action past the ellipsis.
    app.launch();
    if (!app.menu_open_ || app.library_controls_.action >= 0) return 1967;
    if (!app.libraryMenuAnchor()) return 2607;
    app.menu_progress_ = 0.5f;
    if (!capture(L"library-detailed-menu-opening.png")) return 2608;
    app.menu_progress_ = 1;
    if (!capture(L"library-detailed-menu.png")) return 2609;
    app.back();
    const library_presentation::Layout detail_layout{viewport.width, viewport.height};
    const auto more_button = detail_layout.action(1);
    const auto detailed_game = app.cards_[app.selected_].game.app_id;
    app.mouse_transition_until_ = 0;
    app.click((more_button.left + 20) * scale, (more_button.top + 20) * scale, false);
    if (app.screen_ != ScreenMode::library || !app.menu_open_ || app.cards_[app.selected_].game.app_id != detailed_game) return 1968;
    const auto menu = context_menu::bounds(app.cards_[app.selected_], app.menuRows(), app.menuBeside(), viewport.width, viewport.height, app.libraryMenuAnchor());
    if (std::abs(menu.left - more_button.left) > 1 || menu.top < 8 || menu.bottom > viewport.height - 8) return 2610;
    const float details_y = menu.top + 8 + context_menu::row(context_menu::Item::details, app.menuRows()) * 53 + 24;
    app.click((menu.left + 40) * scale, details_y * scale, false);
    if (app.screen_ != ScreenMode::details) return 2611; // Anchored menu uses the same geometry for picking.
    app.back(); settle();
    const auto row_y = app.cards_[1].y + 20;
    app.click((library_presentation::listRight(viewport.width) - 15) * scale, row_y * scale, false);
    if (app.selected_ != 1 || app.library_controls_.action >= 0) return 1969;
    app.openLibrarySearch(); app.library_controls_.query = L"no matching game"; app.applyLibraryQuery();
    if (!app.cards_.empty() || app.library_controls_.action >= 0) return 1970;
    settle(); if (!capture(L"library-detailed-empty.png")) return 1971;
    app.back(); app.back();
    app.saveLibraryPreferences();
    const auto presentations = loadLibraryState(userConfigDirectory() / L"library-state.ini");
    if (!presentations || presentations->options.at("1:all").view != library_view::View::cinematic ||
        presentations->options.at("1:steam").view != library_view::View::detailed) return 1972;
    app.openLibrary();
    if (!app.libraryCinematic()) return 1973;
    app.openPcLibrary(GameSource::steam);
    if (app.libraryView() != library_view::View::detailed) return 1974;
    // Plakat (saved as 4) was removed: such a library opens in Lista+ (library_state_tests).
    if (library_view::viewFromPersisted(4) != library_view::View::list_plus || library_view::drawerView(library_view::view_count - 1) != library_view::View::cartridge_switch) return 1981;
    const auto deliver_poster = [&](size_t index) {
        auto image = std::make_unique<CoverMessage>();
        image->generation = app.jobs_->generation.load(); image->kind = CoverMessage::Kind::poster;
        image->key = gameKey(app.cards_[index].game); image->bytes = pngFixture(85, 200, 300);
        // Optional real posters for the captures (a folder of image files, used in turn).
        wchar_t posters[4096]{};
        if (const DWORD length = GetEnvironmentVariableW(L"LAUNCHME_UI_POSTER_DIR", posters, 4096); length && length < 4096) {
            std::vector<std::filesystem::path> files;
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(posters, error)) if (entry.is_regular_file()) files.push_back(entry.path());
            std::sort(files.begin(), files.end());
            if (!files.empty()) {
                std::ifstream input(files[index % files.size()], std::ios::binary);
                image->bytes = std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
            }
        }
        if (index == 2) {
            wchar_t file[4096]{};
            if (GetEnvironmentVariableW(L"LAUNCHME_UI_STEELBOOK_IMAGE", file, 4096)) {
                std::ifstream input(std::filesystem::path(file), std::ios::binary);
                image->bytes = std::vector<uint8_t>(std::istreambuf_iterator<char>(input), {});
            }
        }
        app.onCover(std::move(image));
    };
    // Coverflow keeps identity and loads only nearby, flat portrait posters.
    app.focusCard(2);
    const auto flow_key = gameKey(app.cards_[2].game);
    chooseView(library_view::View::coverflow);
    if (!app.libraryCoverflow() || gameKey(app.cards_[app.selected_].game) != flow_key) return 2612;
    for (size_t i = 0; i < app.cards_.size(); ++i) deliver_poster(i);
    for (int i = 0; i < 8; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
    if (app.cards_[app.selected_].disc != 0) return 2613; // No disc during the initial settle delay.
    if (!capture(L"library-coverflow-arriving.png")) return 2614;
    settle();
    const coverflow_layout::Layout flow{viewport.width, viewport.height};
    if (std::abs(app.cards_[2].x - flow.left()) > 1 || app.cards_[2].size <= app.cards_[3].size || app.cards_[2].disc < 0.99f) return 2615;
    const auto exposed_disc = coverflow_layout::disc(app.cards_[2]);
    const auto active_poster = coverflow_layout::poster(app.cards_[2]);
    if (exposed_disc.x >= active_poster.right || exposed_disc.x + exposed_disc.radius <= active_poster.right ||
        app.cards_[3].x - (exposed_disc.x + exposed_disc.radius) < 16 ||
        app.cards_[3].x - (exposed_disc.x + exposed_disc.radius) > 20) return 2636;
    app.paint(); // Always execute the renderer, even without optional captures.
    if (!app.renderer_.steelbookReadyForTest(2)) return 2640;
    const bool polished = app.renderer_.steelbookGlossForTest(2);
    std::cout << "Coverflow native printed-metal material=" << app.renderer_.steelbookShaderForTest(2) << ", polished gloss=" << polished
              << ", embossed edges=" << app.renderer_.steelbookReliefForTest(2) << '\n';
    const float material_clock = app.ambient_time_;
    app.ambient_time_ = 0; // Reproducible light positions, independent of earlier test timings.
    app.renderer_.steelbook_for_test = false;
    if (!capture(L"library-coverflow-print.png")) return 2641;
    app.renderer_.steelbook_for_test = true;
    if (!capture(L"library-coverflow.png")) return 2616;
    app.ambient_time_ += 5;
    if (!capture(L"library-coverflow-foil-light.png")) return 2646;
    app.ambient_time_ = material_clock;
    wchar_t material_captures[4096]{};
    if (GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", material_captures, 4096)) {
        Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.GetAddressOf()));
        const auto load = [&](const wchar_t* name) {
            std::ifstream input(std::filesystem::path(material_captures) / name, std::ios::binary);
            const std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
            return decodeImage(wic.Get(), bytes, {});
        };
        const auto print = load(L"library-coverflow-print.png"), finish = load(L"library-coverflow.png");
        const auto shifted = load(L"library-coverflow-foil-light.png");
        if (!print.valid() || !finish.valid() || print.width != finish.width || print.height != finish.height) return 2644;
        if (!shifted.valid() || shifted.width != finish.width || shifted.height != finish.height) return 2647;
        size_t samples = 0, changed = 0, motion = 0, visible_motion = 0; double total = 0; int largest = 0, motion_peak = 0;
        size_t dark = 0, lifted = 0; int sheen_peak = 0;
        // Only the printed area, excluding the pre-existing outer frame and the new inner lip.
        for (int y = int((active_poster.top + 8) * scale); y < int((active_poster.bottom - 8) * scale); ++y)
            for (int x = int((active_poster.left + 8) * scale); x < int((active_poster.right - 8) * scale); ++x) {
                const size_t pixel = (size_t(y) * print.width + x) * 4;
                if (std::max({print.pixels[pixel], print.pixels[pixel + 1], print.pixels[pixel + 2]}) <= 12) {
                    // The darkest printed ink: only the polished oval sheen may lift it.
                    const int lift = int(finish.pixels[pixel + 1]) - int(print.pixels[pixel + 1]);
                    ++dark; lifted += lift >= 4; sheen_peak = std::max(sheen_peak, lift);
                }
                for (int c = 0; c < 3; ++c) {
                    const size_t p = (size_t(y) * print.width + x) * 4 + c;
                    const int delta = std::abs(int(print.pixels[p]) - int(finish.pixels[p]));
                    total += delta; largest = std::max(largest, delta); changed += delta != 0; ++samples;
                    const int light_delta = std::abs(int(shifted.pixels[p]) - int(finish.pixels[p]));
                    motion += light_delta != 0; motion_peak = std::max(motion_peak, light_delta);
                    visible_motion += light_delta >= 8;
                }
            }
        const double mean = total / std::max<size_t>(1, samples);
        std::cout << "Steelbook print delta: mean=" << mean << ", max=" << largest << ", changed=" << changed << '/' << samples << '\n';
        std::cout << "Foil light motion: pixels=" << motion << ", visible=" << visible_motion << ", peak=" << motion_peak << '\n';
        std::cout << "Polished sheen: dark=" << dark << ", lifted=" << lifted << ", peak=" << sheen_peak << '\n';
        if (!changed || largest > 200 || mean > 25.0) return 2645; // Strong local reflections, while retaining the printed artwork.
        if (motion_peak < 20 || visible_motion < samples / 1000) return 2648; // A few 1-3 level changes were not visibly reflecting.
        // Visible, but only lightly. Dark ink outside the current oval (e.g. thin outlines) stays unlit.
        if (polished && lifted && (sheen_peak < 10 || sheen_peak > 60)) return 2649;
    }
    // The material belongs to its image, and is rebuilt after replacing art or losing a target.
    app.renderer_.setPoster(2, shareImage(pngFixture(24, 120, 180)));
    if (app.renderer_.steelbookReadyForTest(2)) return 2642;
    app.paint();
    if (!app.renderer_.steelbookReadyForTest(2)) return 2643;
    deliver_poster(2); app.paint();
    app.navigate(1); frame();
    if (app.selected_ != 3 || app.cards_[3].x <= flow.left() || app.cards_[3].disc != 0 || app.cards_[2].disc >= 0.99f) return 2617;
    // Gliding left, the fixed lamp's reflection slides the other way across the case.
    if (flow.lightSweep(app.cards_[3], 3, 3) > -0.1f || flow.lightSweep(app.cards_[4], 4, 3) > -0.1f) return 2651;
    // The steelbook finish replays on every selection: the new poster starts as plain print,
    // the previous one is already fading back to it.
    if (app.cards_[3].steelbook != 0 || app.cards_[2].steelbook >= 1 || app.cards_[2].steelbook <= 0) return 2653;
    if (!capture(L"library-coverflow-glide.png")) return 2618;
    for (int i = 0; i < 10; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); }
    if (app.cards_[3].steelbook < 0.15f || app.cards_[3].steelbook > 0.7f || app.cards_[2].steelbook != 0) return 2654; // Mid-way.
    app.navigate(-1); settle();
    if (app.selected_ != 2 || std::abs(app.cards_[2].x - flow.left()) > 1) return 2619;
    if (app.cards_[2].steelbook != 1 || app.cards_[3].steelbook != 0) return 2655;
    if (std::abs(flow.lightSweep(app.cards_[2], 2, 2)) > 0.01f || std::abs(flow.lightSweep(app.cards_[3], 3, 2)) > 0.01f) return 2652;
    const auto next_poster = coverflow_layout::poster(app.cards_[3]);
    const auto centre = D2D1::Point2F((next_poster.left + next_poster.right) * 0.5f, (next_poster.top + next_poster.bottom) * 0.5f);
    if (!app.cardHit(3, centre.x, centre.y) || app.cardHit(3, app.cards_[3].x - 1, app.cards_[3].y + 1)) return 2620;
    app.mouse_transition_until_ = 0;
    app.click(centre.x * scale, centre.y * scale, false);
    if (app.selected_ != 3) return 2621;
    app.changeCategory(-1);
    if (app.library_controls_.focus != library_ui::filters_button) return 2622;
    app.changeCategory(1); app.wheel(1);
    if (app.selected_ != 4 || app.library_controls_.focus >= 0) return 2623;
    app.focusCard(app.cards_.size() - 1); app.navigate(1); settle();
    if (app.selected_ != app.cards_.size() - 1 || app.renderer_.hasPoster(0)) return 2624;
    if (!capture(L"library-coverflow-last.png")) return 2625;
    app.focusCard(0); settle(); app.navigate(-1);
    if (app.library_controls_.panel != library_ui::Panel::views) return 2626;
    app.back();
    app.openLibrarySearch(); app.library_controls_.query = L"no coverflow matches"; app.applyLibraryQuery(); settle(); app.paint();
    if (!app.cards_.empty()) return 2627;
    app.back(); app.back(); app.focusCard(0); settle();
    app.mouse_transition_until_ = 0;
    const auto first_poster = coverflow_layout::poster(app.cards_[0]);
    const auto active_centre = D2D1::Point2F((first_poster.left + first_poster.right) * 0.5f, (first_poster.top + first_poster.bottom) * 0.5f);
    app.click(active_centre.x * scale, active_centre.y * scale, true);
    if (!app.pending_launch_ || gameKey(app.pending_game_) != gameKey(app.cards_[0].game)) return 2628;
    app.pending_launch_ = false; app.launch_progress_ = 0;
    {
        // Appearance → "Domyślna akcja gry" = Szczegóły: every pick of a game opens its details;
        // the game menu and the details screen still launch.
        app.settings_.accept_action = AppSettings::AcceptAction::details;
        const auto key = gameKey(app.cards_[0].game);
        app.mouse_transition_until_ = 0;
        app.click(active_centre.x * scale, active_centre.y * scale, true);
        if (app.pending_launch_ || app.screen_ != ScreenMode::details || gameKey(app.cards_[0].game) != key) return 2660;
        app.launch(); // Details: the primary button.
        if (!app.pending_launch_) return 2661;
        app.pending_launch_ = false; app.launch_progress_ = 0;
        app.back();
        if (app.screen_ != ScreenMode::library || !app.libraryCoverflow()) return 2662;
        app.enterDown(false); app.enterUp();
        if (app.pending_launch_ || app.screen_ != ScreenMode::details) return 2663;
        app.back(); app.accept(); // Pad A / Space.
        if (app.pending_launch_ || app.screen_ != ScreenMode::details) return 2664;
        app.back(); app.openContextMenu(); app.menu_selected_ = 0; app.menuAccept(); // Menu: primary action.
        if (!app.pending_launch_ || app.screen_ != ScreenMode::library) return 2665;
        app.pending_launch_ = false; app.launch_progress_ = 0;
        app.focusLibraryControls(); app.accept(); // The filter choice is handled without opening game details.
        if (app.screen_ != ScreenMode::library || app.pending_launch_ || app.library_controls_.open()) return 2666;
        app.settings_.accept_action = AppSettings::AcceptAction::launch;
        app.closeLibraryPanel(); app.library_controls_.focus = -1; settle();
    }
    app.saveLibraryPreferences();
    const auto flow_saved = loadLibraryState(userConfigDirectory() / L"library-state.ini");
    if (!flow_saved || flow_saved->options.at("1:steam").view != library_view::View::coverflow ||
        flow_saved->options.at("1:all").view != library_view::View::cinematic) return 2629;
    app.openLibrary(); app.openPcLibrary(GameSource::steam);
    if (!app.libraryCoverflow()) return 2630;
    const std::pair<library_view::View, const wchar_t*> media_views[] = {
        {library_view::View::floppy, L"library-floppy.png"},
        {library_view::View::cartridge_switch, L"library-cartridge-switch.png"}};
    for (const auto& [mode, screenshot] : media_views) {
        app.focusCard(2);
        const auto key = gameKey(app.cards_[2].game);
        chooseView(mode);
        if (app.libraryView() != mode || !app.libraryCarousel() || app.libraryCoverflow() || gameKey(app.cards_[app.selected_].game) != key) return 2650;
        settle(); app.paint();
        const auto& selected = app.cards_[2];
        const auto body = coverflow_layout::poster(selected);
        const auto label = media_layout::label(mode, body);
        const float expected_aspect = mode == library_view::View::floppy ? 1.0f : 2.0f / 3.0f;
        if (std::abs((label.right - label.left) / (label.bottom - label.top) - expected_aspect) > 0.001f ||
            selected.size <= app.cards_[3].size || selected.disc != 0 || app.renderer_.steelbookReadyForTest(2) ||
            std::abs(app.cards_[3].x - body.right - coverflow_layout::gap) > 1) return 2651;
        if (mode == library_view::View::floppy ? app.renderer_.hasPoster(2) || !app.renderer_.hasCover(2) : !app.renderer_.hasPoster(2)) return 2652;
        if (!app.cardHit(2, (label.left + label.right) * 0.5f, (label.top + label.bottom) * 0.5f) ||
            app.cardHit(2, body.right - 1, body.top + 1)) return 2653;
        if (!capture(screenshot)) return 2654;
        const auto next = media_layout::label(mode, coverflow_layout::poster(app.cards_[3]));
        app.mouse_transition_until_ = 0;
        app.click((next.left + next.right) * 0.5f * scale, (next.top + next.bottom) * 0.5f * scale, false);
        if (app.selected_ != 3) return 2655;
        app.navigate(-1); settle();
        app.openArtworkBrowser(app.cards_[2].game);
        if (app.artwork_state_.kind() != (mode == library_view::View::floppy ? ArtworkKind::cover : ArtworkKind::poster)) return 2656;
        app.closeArtworkBrowser(); settle();
        if (app.libraryView() != mode || gameKey(app.cards_[app.selected_].game) != key) return 2657;
        app.saveLibraryPreferences();
        const auto saved = loadLibraryState(userConfigDirectory() / L"library-state.ini");
        if (!saved || saved->options.at("1:steam").view != mode || saved->options.at("1:all").view != library_view::View::cinematic) return 2658;
        app.openLibrary(); if (!app.libraryCinematic()) return 2659;
        app.openPcLibrary(GameSource::steam); if (app.libraryView() != mode) return 2660;
    }
    app.openLibraryPanel(library_ui::Panel::views); app.library_views_progress_ = 1; settle();
    if (!capture(L"library-views-media-drawer.png")) return 2661;
    app.closeLibraryPanel(); app.library_views_progress_ = 0;
    // The drawer lists Przegląd beside Kinowy and Półka under Coverflow; the saved numbers stay.
    if (library_view::drawerView(library_view::drawerRow(library_view::View::cinematic) + 1) != library_view::View::overview ||
        library_view::drawerView(library_view::drawerRow(library_view::View::coverflow) + 1) != library_view::View::bookshelf ||
        static_cast<int>(library_view::View::overview) != 9 || static_cast<int>(library_view::View::bookshelf) != 10) return 2670;
    const auto screen = app.renderer_.viewport();
    // Półka in the steam library: Cover Flow. The focused poster faces front in the middle with
    // its title, year and developer under it; the others stand turned on both sides in perspective
    // and turn through the middle while the centre glides.
    {
        app.focusCard(3);
        const auto key = gameKey(app.cards_[3].game);
        chooseView(library_view::View::bookshelf);
        if (!app.libraryBookshelf() || !app.libraryCarousel() || gameKey(app.cards_[app.selected_].game) != key ||
            !library_view::metadata(app.libraryPresentation()) || !library_view::posterArt(app.libraryPresentation())) return 2671;
        frame(); // Entering, the covers sweep in from the right.
        if (!app.library_flow_.valid || app.library_flow_.value.value >= 2.9f) return 2678;
        for (size_t i = 0; i < app.cards_.size(); ++i) deliver_poster(i);
        auto shelf_preview = std::make_unique<PreviewMessage>();
        shelf_preview->generation = app.preview_.generation(); shelf_preview->app_id = app.cards_[3].game.app_id;
        shelf_preview->details = {L"", L"17 Sep 2015", L"Fixture Studio", L"Fixture Publisher", L"Action"};
        app.onPreview(std::move(shelf_preview));
        settle();
        if (std::abs(app.library_flow_.value.value - 3) > 0.001f || !app.renderer_.hasPoster(3) || !app.renderer_.hasPoster(4)) return 2679;
        const shelf_layout::Layout shelf{screen.width, screen.height};
        const auto front = shelf.quad(0), right = shelf.quad(1), left = shelf.quad(-1);
        // Facing front in the middle; each neighbour turned towards it, its outer edge nearer
        // (taller) than its inner one, both behind the focused cover (shorter than it).
        const auto tall = [](const std::array<D2D1_POINT_2F, 4>& q, int a, int b) { return q[b].y - q[a].y; };
        if (std::abs(front[0].x + front[1].x - screen.width) > 1 || std::abs(front[0].y - front[1].y) > 0.5f ||
            right[0].x <= front[1].x || left[1].x >= front[0].x ||
            tall(right, 1, 2) <= tall(right, 0, 3) || tall(left, 0, 3) <= tall(left, 1, 2) ||
            tall(right, 1, 2) >= tall(front, 1, 2) || std::abs(front[0].y - shelf.top()) > 0.5f) return 2672;
        // Resting card rectangles are the covers' bounds (the game menu opens beside them).
        const auto bounds = shelf.bounds(0);
        if (std::abs(app.cards_[3].x - bounds.left) > 1 || std::abs(app.cards_[3].y - bounds.top) > 1 ||
            std::abs(app.cards_[3].width() - (bounds.right - bounds.left)) > 1) return 2673;
        // Picking follows the drawing: the focused cover on top, a neighbour by its visible outer part.
        const float cx = (front[0].x + front[1].x) * 0.5f, cy = (front[0].y + front[3].y) * 0.5f;
        const float rx = (right[1].x + right[2].x) * 0.5f - 4, ry = (right[1].y + right[2].y) * 0.5f;
        if (!app.cardHit(3, cx, cy) || app.cardHit(4, cx, cy) || !app.cardHit(4, rx, ry) || app.cardHit(3, rx, ry) ||
            app.cardHit(3, cx, shelf.floor() + 30)) return 2674;
        if (!app.preview_.find(app.cards_[3].game) || app.libraryButtons()) return 2675;
        if (!capture(L"library-bookshelf.png")) return 2676;
        // Its two options in the view drawer: grey side cases (on) and the platform band atop each spine (on).
        if (library_ui::views::optionCount(library_view::View::bookshelf) != 2 || !app.library_controls_.options.bookshelf_gray ||
            !app.library_controls_.options.bookshelf_platform) return 2780;
        app.library_controls_.view_row = 1; app.adjustLibraryView(0); settle();
        if (app.library_controls_.options.bookshelf_gray || !app.libraryPresentation().bookshelf_platform) return 2781;
        if (!capture(L"library-bookshelf-colour.png")) return 2782;
        app.library_controls_.view_row = 2; app.adjustLibraryView(0); settle();
        if (app.libraryPresentation().bookshelf_platform || app.libraryPresentation().bookshelf_gray) return 2783;
        if (!capture(L"library-bookshelf-plain-spines.png")) return 2784;
        app.library_controls_.view_row = 1; app.adjustLibraryView(1); app.library_controls_.view_row = 2; app.adjustLibraryView(1);
        app.library_controls_.view_row = 0; settle();
        if (!app.libraryPresentation().bookshelf_gray || !app.libraryPresentation().bookshelf_platform) return 2785;
        // A click on a turned cover glides it to the middle; halfway it is turning.
        app.mouse_transition_until_ = 0;
        app.click(rx * scale, ry * scale, false);
        if (app.selected_ != 4) return 2716;
        frame(); frame();
        const float gliding = app.library_flow_.value.value;
        if (!(gliding > 3.02f && gliding < 3.98f)) return 2717;
        if (!capture(L"library-bookshelf-glide.png")) return 2718;
        settle();
        if (std::abs(app.library_flow_.value.value - 4) > 0.001f || !app.cardHit(4, cx, cy)) return 2719;
        // A long jump glides over the last few covers only, and still lands exactly.
        app.focusCard(app.cards_.size() - 1); frame();
        if (app.library_flow_.value.value < static_cast<float>(app.cards_.size() - 1) - 6.01f) return 2720;
        settle();
        if (!capture(L"library-bookshelf-last.png")) return 2721;
        app.focusCard(3); settle();
        // The focused cover is a launch target (double click).
        app.mouse_transition_until_ = 0;
        app.click(cx * scale, cy * scale, true);
        if (!app.pending_launch_ || gameKey(app.pending_game_) != key) return 2722;
        app.pending_launch_ = false; app.launch_progress_ = 0;
        app.saveLibraryPreferences();
        const auto saved = loadLibraryState(userConfigDirectory() / L"library-state.ini");
        if (!saved || saved->options.at("1:steam").view != library_view::View::bookshelf) return 2677;
    }
    // Przegląd in "Wszystkie gry": the focused game's panel beside landscape tiles, the category
    // tabs above (LB/RB, PgUp/PgDn, a click on a neighbour).
    app.openLibrary();
    app.focusCard(4);
    chooseView(library_view::View::overview);
    if (!app.libraryOverview() || app.libraryList() || !app.libraryTabsShown() ||
        app.libraryColumns() != overview_layout::columns(screen.width)) return 2680;
    settle();
    {
        const auto& card = app.cards_[app.selected_];
        if (std::abs(card.aspect - overview_layout::aspect) > 0.001f || card.x < overview_layout::gridLeft(screen.width) - 1 ||
            card.y < overview_layout::list_top - 1 || !app.cardHit(app.selected_, card.x + 4, card.y + 4) ||
            app.cardHit(app.selected_, overview_layout::left + 10, card.y + 4)) return 2681;
    }
    auto overview_preview = std::make_unique<PreviewMessage>();
    overview_preview->generation = app.preview_.generation(); overview_preview->app_id = app.cards_[app.selected_].game.app_id;
    overview_preview->details.description = L"Panel z opisem gry obok kafli biblioteki.";
    overview_preview->details.playtime_minutes = 754; overview_preview->details.achievements_total = 50; overview_preview->details.achievements_unlocked = 18;
    app.onPreview(std::move(overview_preview));
    const auto tabs = app.libraryTabs();
    if (tabs.size() < 2 || !tabs[0].id.empty() || app.libraryTabIndex(tabs) != 0) return 2682;
    if (!capture(L"library-overview.png")) return 2683;
    // Search has its own clear row; even the last selected tile stays below it after scrolling.
    app.openLibrarySearch(); settle();
    const auto search_field = library_ui::Layout{screen.width}.field();
    const auto overview_cover = overview_layout::Panel{screen.width, screen.height}.cover();
    if (std::abs((overview_cover.right - overview_cover.left) - (overview_cover.bottom - overview_cover.top)) > 0.01f ||
        overview_cover.top <= search_field.bottom || app.cards_[0].y <= search_field.bottom) return 2709;
    if (!capture(L"library-overview-search.png")) return 2710;
    app.changeCategory(1); app.focusCard(app.cards_.size() - 1); settle();
    const auto& last_tile = app.cards_[app.selected_];
    if (last_tile.y < overview_layout::list_top - 1 ||
        last_tile.y + last_tile.size > screen.height - overview_layout::bottom_margin + 1 ||
        app.cardHit(0, app.cards_[0].x + 10, search_field.bottom - 10)) return 2711;
    if (!capture(L"library-overview-search-scrolled.png")) return 2712;
    app.closeLibrarySearch(); app.focusCard(4); settle();
    // LB/RB: the next category in the same view, from its first game; the strip glides.
    app.switchLibraryTab(1);
    if (app.library_category_ != tabs[1].id || !app.libraryOverview() || app.selected_ != 0 || app.cards_.empty() ||
        app.libraryTabIndex(app.libraryTabs()) != 1) return 2684;
    frame();
    if (!app.library_tab_glide_.valid || app.library_tab_glide_.value.value <= 0.01f || app.library_tab_glide_.value.value >= 0.99f) return 2685;
    settle();
    if (!capture(L"library-overview-tab.png")) return 2686;
    app.switchLibraryTab(-1);
    if (!app.library_category_.empty() || !app.libraryOverview()) return 2687;
    app.switchLibraryTab(-1); // The first tab: an edge.
    if (!app.library_category_.empty()) return 2688;
    // A click on the right neighbour; Kinowy and the grid keep the strip.
    {
        const auto first = library_tabs::tab(0, screen.width, 0);
        if (std::abs(first.left - library_tabs::left - 10) > 0.01f) return 2714;
        const auto right = library_tabs::tab(1, screen.width, 0);
        app.mouse_transition_until_ = 0;
        app.click((first.left + first.right) * 0.5f * scale, (first.top + first.bottom) * 0.5f * scale, false);
        if (!app.library_category_.empty()) return 2715;
        app.click((right.left + right.right) * 0.5f * scale, (right.top + right.bottom) * 0.5f * scale, false);
        if (app.library_category_ != tabs[1].id) return 2689;
    }
    app.library_controls_.options.view = library_view::View::cinematic; app.applyLibraryOptions();
    if (!app.libraryTabsShown() || app.library_category_ != tabs[1].id) return 2690;
    settle();
    if (!capture(L"library-cinematic-tabs.png")) return 2691;
    app.library_controls_.options.view = library_view::View::grid; app.applyLibraryOptions();
    if (!app.libraryTabsShown()) return 2692; // Every view has the tabs, in the top bar.
    settle();
    if (!capture(L"library-grid-tabs.png")) return 2706;
    // The panel's "Szczegóły" opens the game's details.
    app.library_controls_.options.view = library_view::View::overview; app.applyLibraryOptions(); settle();
    {
        const auto details = overview_layout::Panel{screen.width, screen.height}.button(0);
        app.mouse_transition_until_ = 0;
        app.click((details.left + details.right) * 0.5f * scale, (details.top + details.bottom) * 0.5f * scale, false);
        if (app.screen_ != ScreenMode::details) return 2693;
        app.back();
        if (app.screen_ != ScreenMode::library || !app.libraryOverview()) return 2694;
    }
    // Keys: Left from the first column focuses the primary action (beside the tiles), then
    // "Szczegóły"; Left past them opens the view drawer, Right returns to the same tile.
    app.focusCard(0);
    app.navigate(-1);
    if (app.library_controls_.action != 1 || app.library_controls_.open() || app.cardFocusVisible()) return 2695;
    settle();
    if (!capture(L"library-overview-focus.png")) return 2696;
    app.navigate(-1);
    if (app.library_controls_.action != 0) return 2697;
    app.navigate(1); app.navigate(1);
    if (app.library_controls_.action != -1 || app.selected_ != 0 || app.library_controls_.open()) return 2698;
    app.navigate(-1); app.navigate(-1); app.navigate(-1);
    if (app.library_controls_.panel != library_ui::Panel::views || app.library_controls_.action != -1) return 2699;
    app.closeLibraryPanel();
    // Up from the buttons: the top bar. Enter on "Szczegóły": the game's details.
    app.navigate(-1);
    app.changeCategory(-1);
    if (app.library_controls_.action != -1 || app.library_controls_.focus != library_ui::filters_button) return 2703;
    app.library_controls_.focus = -1; app.focusCard(0);
    app.navigate(-1); app.navigate(-1);
    app.enterDown(false);
    if (app.screen_ != ScreenMode::details) return 2704;
    app.back();
    if (app.screen_ != ScreenMode::library || !app.libraryOverview()) return 2705;
    // Lista+: three independent columns; the poster and rows select the same game, and
    // the view icon works even after scrolling to the bottom of the central browser.
    app.openLibrary(); app.focusCard(2);
    const auto plus_key = gameKey(app.cards_[app.selected_].game);
    chooseView(library_view::View::list_plus);
    if (!app.libraryListPlus() || !app.libraryList() || app.libraryColumns() != 1 || app.libraryActions() ||
        gameKey(app.cards_[app.selected_].game) != plus_key || static_cast<int>(library_view::View::list_plus) != 11) return 2810;
    deliver_poster(2);
    auto plus_hero = std::make_unique<CoverMessage>();
    plus_hero->key = plus_key; plus_hero->generation = app.jobs_->generation.load();
    plus_hero->kind = CoverMessage::Kind::hero; plus_hero->bytes = pngFixture(72, 320, 180); app.onCover(std::move(plus_hero));
    auto plus_preview = std::make_unique<PreviewMessage>();
    plus_preview->generation = app.preview_.generation(); plus_preview->app_id = app.cards_[2].game.app_id;
    plus_preview->details = {L"Explore a world full of secrets, discover new paths and build your own adventure. A longer description exercises the right-hand column while the poster and game list stay visible.",
        L"17 Sep 2020", L"Fixture Studio", L"Fixture Publisher", L"Action, Adventure"};
    plus_preview->details.playtime_minutes = 754; plus_preview->details.achievements_total = 51; plus_preview->details.achievements_unlocked = 7;
    app.onPreview(std::move(plus_preview)); settle();
    const list_plus_layout::Layout plus{screen.width, screen.height};
    if (plus.poster().right >= plus.listLeft() || plus.listRight() >= plus.infoLeft() ||
        !app.renderer_.hasPoster(2) || !app.cardHit(2, plus.poster().left + 10, plus.poster().top + 10) ||
        app.cardHit(1, plus.poster().left + 10, plus.poster().top + 10) ||
        app.cardHit(2, plus.infoLeft() + 20, plus.hero().top + 20)) return 2811;
    if (!capture(L"library-list-plus.png")) return 2812;
    {
        // Lista+: the video plays in its panel's picture.
        std::vector<uint8_t> video(64 * 36 * 4, 0); for (size_t p = 0; p < video.size(); p += 4) { video[p] = 40; video[p + 1] = 200; video[p + 2] = 90; video[p + 3] = 255; }
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        // The capture's own target: the frame is uploaded there, already faded in.
        if (count && count < 4096 && !app.renderer_.captureForTest(std::filesystem::path(folder) / L"library-list-plus-video.png", [&] {
                app.renderer_.setVideoFrame(video.data(), 64, 36); app.renderer_.settleVideoForTest(); app.paint(); })) return 2790;
        app.renderer_.clearVideo();
    }
    {
        // Its one option: Coverflow's steelbook finish on the poster, without the disc. It shows at
        // once behind the open drawer, and is saved with the view.
        if (library_ui::views::optionCount(library_view::View::list_plus) != 1 || app.library_controls_.options.list_plus_steelbook) return 2824;
        app.openLibraryPanel(library_ui::Panel::views); app.changeCategory(1); app.navigate(1);
        if (!app.library_controls_.options.list_plus_steelbook || !app.library_controls_.open() || !app.libraryListPlus() ||
            gameKey(app.cards_[app.selected_].game) != plus_key) return 2825;
        settle(); app.paint();
        if (app.cards_[app.selected_].steelbook < 0.99f || app.cards_[app.selected_].disc != 0 || !app.renderer_.steelbookReadyForTest(app.selected_)) return 2826;
        if (!capture(L"library-options-list-plus-steelbook.png")) return 2827;
        app.closeLibraryPanel(); app.library_controls_.focus = -1; settle();
        if (!capture(L"library-list-plus-steelbook.png")) return 2828;
        app.saveLibraryPreferences();
        const auto steelbook_saved = loadLibraryState(userConfigDirectory() / L"library-state.ini");
        if (!steelbook_saved || !steelbook_saved->options.at("1:all").list_plus_steelbook) return 2829;
    }
    app.mouse_transition_until_ = 0;
    app.click((plus.listLeft() + 40) * scale, (app.cards_[3].y + 20) * scale, false);
    if (app.selected_ != 3 || app.renderer_.hasPoster(2)) return 2813;
    settle(); if (!capture(L"library-list-plus-fallback.png")) return 2814;
    app.focusCard(app.cards_.size() - 1); settle();
    if (app.grid_scroll_ < 165 || app.cards_[app.selected_].y + list_layout::thumb > screen.height - list_layout::bottom_margin + 1) return 2815;
    if (!capture(L"library-list-plus-scrolled.png")) return 2816;
    const auto icon = library_ui::Layout{screen.width}.views();
    app.click((icon.left + 20) * scale, (icon.top + 20) * scale, false);
    if (app.library_controls_.panel != library_ui::Panel::views || app.library_controls_.choice != library_view::drawerRow(library_view::View::list_plus)) return 2817;
    app.library_views_progress_ = 1;
    if (!capture(L"library-list-plus-drawer.png")) return 2818;
    app.back(); app.back();
    app.focusCard(2); settle();
    app.openDetails(app.cards_[2].game); app.back();
    if (!app.libraryListPlus() || gameKey(app.cards_[app.selected_].game) != plus_key) return 2819;
    app.openArtworkBrowser(app.cards_[app.selected_].game);
    if (!app.artworkOpen() || app.artwork_state_.kind() != ArtworkKind::poster) return 2820;
    app.closeArtworkBrowser();
    app.saveLibraryPreferences();
    const auto plus_saved = loadLibraryState(userConfigDirectory() / L"library-state.ini");
    if (!plus_saved || plus_saved->options.at("1:all").view != library_view::View::list_plus) return 2821;
    // A taller viewport and a small 720p window retain all three panes and all twelve drawer entries.
    for (const auto [w, h] : {std::pair{1280, 720}, std::pair{1024, 768}}) {
        SetWindowPos(window, nullptr, 0, 0, w, h, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
        app.renderer_.resize(); settle();
        const auto v = app.renderer_.viewport();
        const list_plus_layout::Layout compact{v.width, v.height};
        if (compact.poster().bottom > compact.bottom() + 1 || compact.infoWidth() < 280 || compact.listRight() - compact.listLeft() < 280 ||
            library_ui::views::hit(120, library_ui::views::top(v.height) + library_ui::views::rowHeight(v.height) * (library_view::view_count - 0.5f), v.height) != library_view::view_count - 1) return 2822;
        if (!capture(w == 1280 ? L"library-list-plus-720p.png" : L"library-list-plus-4x3.png")) return 2823;
    }
    SetWindowPos(window, nullptr, 0, 0, 1600, 900, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    app.renderer_.resize();
    // Per-view options are live, stay open, preserve game identity, and persist together.
    chooseView(library_view::View::grid); app.focusCard(2);
    app.openLibraryPanel(library_ui::Panel::views); app.changeCategory(1); app.navigate(1);
    if (!app.libraryGridPosters() || !app.library_controls_.open() || gameKey(app.cards_[app.selected_].game) != plus_key) return 2832;
    settle();
    if (std::abs(app.cards_[2].aspect - 2.0f / 3.0f) > 0.001f || !app.renderer_.hasPoster(2)) return 2833;
    if (!capture(L"library-options-grid-posters.png")) return 2834;
    app.closeLibraryPanel(); app.library_controls_.focus = -1;
    app.focusCard(app.cards_.size() - 1); settle();
    if (app.cards_[app.selected_].y < 154 || app.cards_[app.selected_].y + app.cards_[app.selected_].size + 48 > screen.height - 29) return 2835;
    if (!capture(L"library-grid-posters-scrolled.png")) return 2836;
    chooseView(library_view::View::list);
    app.openLibraryPanel(library_ui::Panel::views); app.changeCategory(1); app.navigate(-1);
    app.changeCategory(2); app.navigate(-1);
    if (app.library_controls_.options.list_year || app.library_controls_.options.list_rating || !app.library_controls_.options.list_playtime) return 2837;
    settle(); if (!capture(L"library-options-list-columns.png")) return 2838;
    chooseView(library_view::View::cinematic); app.focusCard(0);
    if (library_view::metadata(app.libraryPresentation())) return 2900; // Vertical cinematic does not fetch descriptions.
    app.openLibraryPanel(library_ui::Panel::views); app.changeCategory(1); app.navigate(1);
    if (!app.libraryHorizontalCinematic() || !library_view::metadata(app.libraryPresentation())) return 2839;
    settle(); if (!capture(L"library-options-cinematic-horizontal.png")) return 2840;
    app.closeLibraryPanel(); app.library_controls_.focus = -1;
    app.navigate(1); app.navigate(1);
    if (app.selected_ != 2) return 2841;
    {
        auto description = std::make_unique<PreviewMessage>();
        description->generation = app.preview_.generation(); description->app_id = app.cards_[2].game.app_id;
        description->details = {L"Po katastrofie, która rozbiła świat na unoszące się w powietrzu wyspy, wyrusz w podróż i odbuduj ostatnie schronienie. "
            L"Odkrywaj kolejne krainy, rozwijaj swój arsenał i poznawaj historię opowiadaną przez narratora reagującego na twoje działania.",
            L"16 Aug 2011", L"Supergiant Games", L"", L"Action, RPG, Indie"};
        app.onPreview(std::move(description)); settle();
        const auto summary = cinematic_row::summary(screen.width, screen.height);
        if (summary.bottom >= app.cards_[2].y - 24 || summary.right > screen.width * 0.5f ||
            !app.preview_.find(app.cards_[2].game) || gameReleaseYear(app.preview_.find(app.cards_[2].game)->release_date) != L"2011") return 2901;
        if (!capture(L"library-cinematic-horizontal-description.png")) return 2902;
    }
    app.focusCard(app.cards_.size() - 1); settle();
    const auto& capsule = app.cards_[app.selected_];
    if (capsule.x < 67 || capsule.x + capsule.width() > screen.width - 67 || std::abs(capsule.y - cinematic_row::top(screen.width, screen.height)) > 1 ||
        capsule.y + capsule.size + 48 > screen.height - 55 || capsule.y < screen.height * 0.6f) return 2842;
    if (!capture(L"library-cinematic-horizontal-last.png")) return 2843;
    const float cinema_scroll = app.grid_scroll_, cinema_x = capsule.x;
    const auto cinema_selected = app.selected_;
    app.changeCategory(-1);
    if (app.library_controls_.focus != library_ui::filters_button) return 2844;
    settle();
    if (cinema_scroll <= 0 || app.grid_scroll_ != cinema_scroll || app.selected_ != cinema_selected ||
        std::abs(app.cards_[cinema_selected].x - cinema_x) > 1) return 2903;
    if (!capture(L"library-cinematic-horizontal-filter-focus.png")) return 2904;
    app.launch(); // Opening and dismissing the filter menu also preserves the row.
    if (app.library_controls_.panel != library_ui::Panel::filters || app.grid_scroll_ != cinema_scroll) return 2905;
    app.back();
    app.changeCategory(1);
    if (app.library_controls_.focus != -1 || app.grid_scroll_ != cinema_scroll || app.selected_ != cinema_selected) return 2845;
    app.focusLibraryControls(); // F6 / Y uses the same pinned toolbar.
    if (app.grid_scroll_ != cinema_scroll || app.selected_ != cinema_selected) return 2906;
    app.focusLibraryControls();
    if (app.library_controls_.open() || app.grid_scroll_ != cinema_scroll) return 2907;
    app.openLibraryPanel(library_ui::Panel::views); app.changeCategory(-1);
    if (app.library_controls_.focus != library_ui::search_button || app.grid_scroll_ != cinema_scroll) return 2908;
    app.changeCategory(1);
    chooseView(library_view::View::overview);
    app.openLibraryPanel(library_ui::Panel::views);
    settle();
    const auto track = library_ui::views::track();
    app.mouse_transition_until_ = 0;
    app.click(track.left * scale, track.top * scale, false);
    if (!app.library_view_drag_ || app.libraryColumns() != 2) return 2846;
    app.mouseMove(track.right * scale, track.top * scale);
    if (app.libraryColumns() != 5) return 2847;
    app.mouseMove(track.left * scale, track.top * scale); app.pointerUp();
    if (app.library_view_drag_ || app.libraryColumns() != 2) return 2848;
    settle(); if (!capture(L"library-options-overview-columns.png")) return 2849;
    chooseView(library_view::View::coverflow); app.focusCard(2);
    app.openLibraryPanel(library_ui::Panel::views); app.changeCategory(1); app.navigate(1);
    if (!app.library_controls_.options.coverflow_cd || !app.library_controls_.open()) return 2851;
    settle(); if (!capture(L"library-options-coverflow-cd.png")) return 2852;
    app.closeLibraryPanel(); app.library_controls_.focus = -1; settle();
    const auto& cd = app.cards_[app.selected_];
    const auto disc = coverflow_layout::disc(cd);
    if (std::abs(cd.aspect - 1.12f) > 0.001f || cd.disc < 0.99f || app.renderer_.hasPoster(app.selected_) ||
        app.cards_[app.selected_ + 1].x < disc.x + disc.radius + coverflow_layout::gap - 1) return 2853;
    app.ambient_time_ = 0;
    if (!capture(L"library-coverflow-cd.png")) return 2854;
    app.ambient_time_ = 2.4f;
    if (!capture(L"library-coverflow-cd-spinning.png")) return 2855;
    app.openArtworkBrowser(app.cards_[app.selected_].game);
    if (app.artwork_state_.kind() != ArtworkKind::cover) return 2856;
    app.closeArtworkBrowser();
    {
        // Exactly the selected game and +1 are requested; stale navigation responses cannot replace either.
        app.focusCard(2); app.requestVisibleArt();
        const auto requests = [&] {
            std::set<std::string> keys;
            for (const auto* queue : {&app.jobs_->jobs, &app.jobs_->secondary, &app.jobs_->prefetch})
                for (const auto& job : *queue) if (job.kind == CoverMessage::Kind::disc) keys.insert(gameKey(job.game));
            return keys;
        };
        if (requests() != std::set<std::string>{gameKey(app.cards_[2].game), gameKey(app.cards_[3].game)}) return 2890;
        auto scan = discFixture();
        wchar_t disc_file[4096]{};
        if (GetEnvironmentVariableW(L"LAUNCHME_UI_DISC_IMAGE", disc_file, 4096)) {
            std::ifstream input(std::filesystem::path(disc_file), std::ios::binary);
            scan.assign(std::istreambuf_iterator<char>(input), {});
        }
        const auto deliver_disc = [&](size_t index, uint32_t revision = 0) {
            auto image = std::make_unique<CoverMessage>(); image->kind = CoverMessage::Kind::disc;
            image->generation = app.jobs_->generation.load(); image->revision = revision;
            image->key = gameKey(app.cards_[index].game); image->bytes = scan; app.onCover(std::move(image));
        };
        const float before_detail = app.detail_;
        deliver_disc(2); deliver_disc(3);
        if (!app.renderer_.hasDisc(2) || !app.renderer_.hasDisc(3) || app.detail_ != before_detail) return 2891;
        settle(); if (!capture(L"library-coverflow-disc-scan.png")) return 2892;
        app.focusCard(8); settle();
        deliver_disc(2);
        if (app.renderer_.hasDisc(2) || app.renderer_.hasDisc(8) ||
            requests() != std::set<std::string>{gameKey(app.cards_[8].game), gameKey(app.cards_[9].game)}) return 2893;
        app.focusCard(2); settle();
        if (!app.renderer_.hasDisc(2)) return 2894; // Cached bytes rebound after navigating back.
        const auto game = app.cards_[2].game;
        app.openArtworkBrowser(game);
        app.artwork_state_.category = artwork_ui::category(ArtworkKind::disc); app.loadArtworkPage();
        if (app.artwork_jobs_->jobs.empty() || app.artwork_jobs_->jobs.back().kind != ArtworkKind::disc ||
            app.artwork_jobs_->jobs.back().provider != ArtworkProvider::screenscraper) return 2895;
        auto page = std::make_unique<ArtworkBrowserMessage>(); page->generation = app.artwork_jobs_->generation.load();
        page->type = ArtworkBrowserMessage::Type::list; page->page.provider = ArtworkProvider::screenscraper;
        page->page.assets.push_back({777, L"https://api.screenscraper.fr/api2/mediaJeu.php?media=support-2D(eu)", L"", L"ScreenScraper · support-2D (eu)"});
        app.onArtworkBrowser(std::move(page));
        app.renderer_.setArtworkThumbnail(0, scan, false);
        if (!capture(L"artwork-disc-screenscraper.png")) return 2896;
        if (!cacheArtworkVariant(game, ArtworkKind::disc, 777, scan, coverCacheDirectory())) return 2897;
        app.artwork_state_.gallery = true; app.artworkAccept();
        auto applied = std::make_unique<ArtworkBrowserMessage>(); applied->generation = app.artwork_jobs_->generation.load();
        applied->type = ArtworkBrowserMessage::Type::apply; applied->asset_id = 777; applied->kind = ArtworkKind::disc; applied->bytes = scan;
        app.onArtworkBrowser(std::move(applied)); app.closeArtworkBrowser(); settle();
        if (!app.renderer_.hasDisc(app.selected_) || selectedArtworkId(game, ArtworkKind::disc, coverCacheDirectory()) != 777) return 2898;
        deliver_disc(2); // Pre-edit revision: ignored by the same artwork revision guard.
        if (!app.renderer_.hasDisc(app.selected_)) return 2899;
    }
    chooseView(library_view::View::grid);
    if (!app.libraryGridPosters()) return 2857; // Each view retains its own options.
    app.saveLibraryPreferences();
    const auto options_saved = loadLibraryState(userConfigDirectory() / L"library-state.ini");
    if (!options_saved || options_saved->options.at("1:all") != app.library_controls_.options ||
        !options_saved->options.at("1:all").coverflow_cd || options_saved->options.at("1:all").overview_columns != 2) return 2858;
    // The pinned footer is reachable with/without options and from the expanded list.
    for (const auto mode : {library_view::View::list, library_view::View::detailed}) {
        chooseView(mode); app.openLibraryPanel(library_ui::Panel::views); settle();
        const auto before = app.library_controls_.options;
        app.changeCategory(20);
        if (app.library_controls_.view_row != library_ui::views::closeRow(mode)) return 2870;
        app.navigate(-1); app.navigate(1);
        if (app.library_controls_.options != before || app.library_controls_.view_dropdown) return 2871;
        if (!capture(mode == library_view::View::list ? L"library-view-close-focused.png" : L"library-view-no-options.png")) return 2872;
        app.changeCategory(-1);
        if (app.library_controls_.view_row != library_ui::views::optionCount(mode)) return 2873;
        app.changeCategory(1); app.libraryAccept();
        if (app.library_controls_.open() || app.library_controls_.focus != -1) return 2874;
    }
    app.openLibraryPanel(library_ui::Panel::views); app.libraryAccept(); settle();
    const auto footer = library_ui::views::close(screen.height);
    if (library_ui::views::revealedBottom(screen.height, 1) > footer.top - 20 ||
        library_ui::views::panel(screen.height).top != 0 || library_ui::views::panel(screen.height).bottom != screen.height) return 2875;
    app.changeCategory(20);
    if (app.library_controls_.view_row != library_ui::views::closeRow(app.libraryView()) || !app.library_controls_.view_dropdown) return 2876;
    app.changeCategory(-1);
    if (app.library_controls_.choice != library_view::view_count - 1 || app.library_controls_.view_row != 0) return 2877;
    settle();
    if (!capture(L"library-view-last-choice.png")) return 2878;
    app.mouse_transition_until_ = 0;
    if (!app.hoverButton((footer.left + 40) * scale, (footer.top + 24) * scale)) return 2879;
    app.click((footer.left + 40) * scale, (footer.top + 24) * scale, false);
    if (app.library_controls_.open()) return 2880;
    frame();
    if (app.library_views_progress_ <= 0 || app.library_views_progress_ >= 1) return 2881;
    if (!capture(L"library-view-drawer-closing.png")) return 2882;
    {
        // Late acquisition metadata must reorder an open library, not wait for another launch.
        Launcher dates; dates.hwnd_ = window; dates.execute_ = fakeExecute;
        dates.settings_.enabled_pc_sources = {"gog"}; dates.settings_.show_uninstalled = true;
        dates.settings_.glance = dates.settings_.companion = false;
        if (!dates.renderer_.initialize(window)) return 8600;
        Game older{7901, L"Zulu"}, newer{7902, L"Alpha"};
        older.added_at = 100; newer.added_at = 200;
        dates.library_state_.first_seen = {{gameKey(older), 900}, {gameKey(newer), 900}, {"gog:74123", 400}};
        auto snapshot = std::make_unique<LibraryMessage>(); snapshot->account = 1; snapshot->games = {older, newer};
        dates.onLibrary(std::move(snapshot)); dates.openLibrary();
        dates.library_controls_.options.order = library_view::Order::added; dates.applyLibraryOptions();
        if (dates.cards_.size() != 2 || dates.cards_[0].game.app_id != newer.app_id) return 8601;
        older.added_at = 300;
        LibraryMessage update; update.account = 1; update.games = {older, newer}; update.installation_update = true;
        dates.updateSteamInstallations(update);
        if (dates.cards_[0].game.app_id != older.app_id || dates.library_state_.first_seen.at(gameKey(older)) != 900) return 8602;
        Game gog; gog.kind = GameKind::store; gog.source = GameSource::gog; gog.source_id = "74123"; gog.name = L"GOG fixture";
        const auto pc = [&](const Game& game) {
            auto message = std::make_unique<PcLibraryMessage>(); message->generation = dates.pc_token_->load();
            message->library.visible = {game}; dates.onPcLibrary(std::move(message));
        };
        pc(gog);
        if (dates.cards_[0].game.source != GameSource::gog) return 8603; // Undated: first detection = 400.
        gog.added_at = 50; pc(gog);
        if (dates.cards_.back().game.source != GameSource::gog) return 8604; // Actual old acquisition arrives.
        const auto history = dates.library_state_.first_seen;
        pc(gog); dates.buildShelves(); dates.rebuildCards();
        if (dates.library_state_.first_seen != history || dates.cards_.back().game.source != GameSource::gog) return 8605;
        const auto indexed = dates.gameIndexGames();
        const auto entry = std::find_if(indexed.begin(), indexed.end(), [](const auto& game) { return game.key == "gog:74123"; });
        if (entry == indexed.end() || entry->first_seen != 50) return 8606;
        {
            // First detection is tied to the scan, not to enabling the uninstalled-games filter.
            Game undated{7903, L"Undated uninstalled"}; undated.installed = false;
            dates.settings_.show_uninstalled = false;
            update.games.push_back(undated); dates.updateSteamInstallations(update);
            if (!dates.library_state_.first_seen.contains(gameKey(undated)) || dates.findCard(gameKey(undated), false)) return 8610;
            dates.library_state_.first_seen[gameKey(undated)] = 25; // A previous scan, before the user changes the filter.
            dates.settings_.show_uninstalled = true; dates.buildShelves(); dates.rebuildCards();
            if (dates.library_state_.first_seen.at(gameKey(undated)) != 25 || dates.cards_.back().game.app_id != undated.app_id) return 8611;
            update.games.pop_back(); dates.updateSteamInstallations(update);
        }
        // Group order is independent of the representative version, including on a progress-only update.
        gog.name = older.name; gog.added_at = 600; pc(gog);
        if (dates.cards_.size() != 2 || dates.cards_[0].game.added_at != 600 || dates.cards_[0].game.version_group.empty()) return 8607;
        update.games[0].installation.done = 1; // Same idle phase: a non-structural progress snapshot.
        dates.updateSteamInstallations(update);
        if (dates.cards_[0].game.added_at != 600) return 8608;
        // Capture the straightforward mixed-store ordering, newest acquisition first.
        gog.name = L"GOG fixture"; gog.added_at = 50; pc(gog);
        dates.library_controls_.options.view = library_view::View::list; dates.applyLibraryOptions();
        for (int i = 0; i < 40; ++i) { dates.last_frame_ -= std::chrono::milliseconds(50); dates.tick(); }
        wchar_t folder[4096]{};
        const auto length = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (length && length < 4096 && !dates.renderer_.captureForTest(std::filesystem::path(folder) / L"library-acquisition-order.png", [&] { dates.paint(); })) return 8609;
    }
    return 0;
}

int LauncherUiTest::libraryStateRecovery(HWND window) {
    const auto file = userConfigDirectory() / L"library-state.ini";
    const auto previous = loadLibraryState(file);
    const std::string corrupt = "launchme-library 1\nplayed \"737465616d3a31\" 123\nunknown row\n";
    if (!atomic_file::write(file, corrupt)) return 1750;
    const auto aside = file.parent_path() / L"library-state.damaged.ini";
    {
        Launcher app; app.hwnd_ = window;
        app.initializeLibraryPreferences();
        // No dead end: the readable row is kept, the original set aside, and history is written again.
        if (!app.library_state_readable_ || !app.library_writer_ || app.notifications_.empty() ||
            app.library_state_.last_played.size() != 1) return 1751;
        app.library_state_.last_played["new history"] = 456; app.library_state_dirty_ = true;
        app.saveLibraryPreferences(); // Normal shutdown retries this path as well.
    }
    std::ifstream in(aside, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>{in}, {}}; in.close();
    if (bytes != corrupt) return 1752; // The damaged original, untouched.
    const auto salvaged = loadLibraryState(file);
    if (!salvaged || salvaged->last_played.size() != 2 || !salvaged->last_played.contains("new history")) return 1754;
    std::error_code ec; std::filesystem::remove(aside, ec);
    if (!saveLibraryState(previous.value_or(LibraryState{}), file)) return 1753;
    return 0;
}

int LauncherUiTest::categoryRail(HWND window) {
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 1600;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        if (!GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096)) return true;
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 16; ++i) {
        Game game{7000 + i, L"Category game " + std::to_wstring(i)}; game.last_played = 100 + i;
        library->games.push_back(game);
    }
    for (const auto* title : {L"Przygodowe", L"RPG", L"Wyścigowe", L"Kooperacja", L"Ulubione"}) {
        SteamCollection collection{title, {}};
        collection.id = "rail-" + std::to_string(library->collections.size());
        for (uint32_t i = 0; i < 16; ++i) collection.app_ids.push_back(7000 + i);
        library->collections.push_back(std::move(collection));
    }
    app.onLibrary(std::move(library));
    for (const auto& game : app.public_library_) {
        auto cover = std::make_unique<CoverMessage>();
        cover->key = gameKey(game); cover->generation = app.jobs_->generation.load();
        cover->bytes = pngFixture(static_cast<uint8_t>(40 + (game.app_id % 16) * 10), 48, 48);
        app.onCover(std::move(cover));
    }
    app.openSettings(); app.settings_state_.selectCategory(0); app.buildSettingsRows();
    const auto rowIndex = [&](settings_ui::Id id) {
        const auto found = std::find_if(app.settings_rows_.begin(), app.settings_rows_.end(), [&](const auto& row) { return row.id == id; });
        return found == app.settings_rows_.end() ? -1 : static_cast<int>(found - app.settings_rows_.begin());
    };
    // The default layout's options page (Interfejs → "Opcje układu") starts with it.
    if (!revealClassicSetting(app, settings_ui::Id::show_categories) || !app.settings_state_.at(settings_ui::Page::layout_options)) return 1601;
    const int option = rowIndex(settings_ui::Id::show_categories);
    if (app.settings_.show_categories || option != 0) return 1601;
    app.settings_state_.enter(static_cast<int>(app.settings_rows_.size()));
    app.settings_state_.selected[0] = option; app.settingsActivate(); app.saveSettings();
    if (!loadAppSettings().show_categories || !capture(L"settings-visible-categories.png")) return 1602;
    // Conditional row disappears in both other layouts, retaining the preference.
    for (const int layout : {1, 2}) {
        app.setHomeLayout(layout);
        if (rowIndex(settings_ui::Id::show_categories) != -1 || !app.settings_.show_categories) return 1603;
        app.showCategory(0);
        if (app.categoryRailActive()) return 1604;
        app.openSettings();
    }
    app.setHomeLayout(0);
    if (!revealClassicSetting(app, settings_ui::Id::show_categories) || rowIndex(settings_ui::Id::show_categories) != option) return 1605;
    app.settings_state_.closeAll(); app.settings_state_.options = false; app.back(); settle();
    const auto view = app.renderer_.viewport();
    const auto rail = [&] { return category_rail::Layout{view.width, view.height, app.categories_.size(), app.category_}; };
    if (!app.categoryRailActive() || app.category_menu_open_ || app.category_rail_focus_ ||
        app.cards_[app.selected_].x < rail().contentClip()) return 1606;
    if (!capture(L"home-visible-categories.png")) return 1607;
    // Keyboard and pad: Left enters the rail, Up/Down change its shelf immediately;
    // Right/Enter/A return to the remembered game without launching it.
    app.focusCard(0); app.controllerNavigate(-1);
    if (!app.category_rail_focus_ || app.category_menu_open_ || app.category_menu_progress_ != 0) return 1608;
    app.controllerNavigate(2);
    if (app.category_ != 1 || !app.category_rail_focus_ || app.cards_.size() != 16) return 1609;
    app.openContextMenu();
    if (app.menu_open_) return 1610;
    app.launch(); // Pad A.
    if (app.category_rail_focus_ || app.pending_launch_) return 1611;
    app.navigate(4); const auto selected = app.selected_;
    app.changeCategory(1); app.changeCategory(-1);
    if (app.selected_ != selected) return 1612;
    app.back(); app.enterDown(false); app.enterUp();
    if (app.category_rail_focus_ || app.selected_ != selected || app.pending_launch_) return 1613;
    settle();
    const auto& hidden = app.cards_[selected - 1];
    if (app.cardHit(selected - 1, rail().contentClip() - 1, hidden.y + hidden.size / 2)) return 1614;
    if (!capture(L"home-categories-cards-scrolled.png")) return 1628;
    // Capture the slide itself: the outgoing cover must fade, not expose a hard clipped edge.
    app.navigate(1);
    for (int frame = 0; frame < 3; ++frame) {
        app.last_frame_ = std::chrono::steady_clock::now() - std::chrono::milliseconds(25); app.tick();
        if (!capture((L"home-categories-slide-" + std::to_wstring(frame) + L".png").c_str())) return 1629;
    }
    settle(); app.navigate(-1); settle();
    // Clicking a label switches shelves, clicking a cover restores card focus.
    const float x = rail().right() - 20, y = rail().rowTop(3) + 34;
    if (!app.hoverButton(x * view.scale, y * view.scale)) return 1615;
    app.click(x * view.scale, y * view.scale, false);
    if (app.category_ != 3 || !app.category_rail_focus_ || app.category_menu_open_) return 1616;
    settle();
    if (!capture(L"home-categories-focused.png")) return 1617;
    const auto card = app.cards_[app.selected_];
    app.click((card.x + card.size / 2) * view.scale, (card.y + card.size / 2) * view.scale, false);
    if (app.category_rail_focus_ || app.pending_launch_) return 1618;
    app.wheel(1, x * view.scale);
    if (app.category_ != 4 || !app.category_rail_focus_) return 1619;
    app.navigate(1);
    if (app.category_rail_focus_) return 1620;
    app.openLibrary();
    if (app.categoryRailActive()) return 1621;
    app.back();
    if (!app.categoryRailActive() || app.category_ != 4) return 1622;
    // Long lists keep the active category in view, including the last row and long titles.
    for (int i = 0; i < 20; ++i) {
        SteamCollection collection{L"Bardzo długa nazwa kolekcji z polskimi znakami ŁĄĆ " + std::to_wstring(i), {7000, 7001}};
        collection.id = "long-rail-" + std::to_string(i); app.collections_source_.push_back(collection);
    }
    app.buildShelves(); app.showCategory(0); app.openCategoryMenu(); app.changeCategory(100); settle();
    if (app.category_ != app.categories_.size() - 1 || app.category_menu_open_ || !app.category_rail_focus_ ||
        rail().first() == 0 || rail().hit(x, rail().rowTop(app.category_) + 34) != static_cast<int>(app.category_)) return 1623;
    if (!capture(L"home-categories-scrolled.png")) return 1624;
    app.showCategory(0); app.changeCategory(-1);
    if (!app.quick_open_) return 1625;
    app.closeQuick();
    app.private_category_.games.push_back(app.public_library_.front()); app.togglePrivateCategory();
    // Ctrl+H: the hidden and private games become one more category at the bottom, focused.
    if (!app.categoryRailActive() || app.categories_.back().id != "private" || app.category_ != app.categories_.size() - 1 ||
        app.cards_.size() != app.private_category_.games.size()) return 1626;
    app.togglePrivateCategory();
    if (app.private_shelf_ || app.categories_.back().id == "private" || app.category_ != 0) return 1628;
    // Disabling restores the original left drawer.
    app.openSettings(); app.settings_state_.selectCategory(0); app.buildSettingsRows();
    revealClassicSetting(app, settings_ui::Id::show_categories); app.settingsActivate();
    app.settings_state_.closeAll(); app.settings_state_.options = false; app.back(); app.focusCard(0); app.navigate(-1);
    if (app.categoryRailActive() || !app.category_menu_open_ || loadAppSettings().show_categories) return 1627;
    return 0;
}

int LauncherUiTest::flix(HWND window) {
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 950;
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        if (!count || count >= 4096) return true;
        app.notifications_.clear();
        return app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) { app.last_frame_ -= std::chrono::milliseconds(50); app.tick(); } };
    app.settings_.home_layout = AppSettings::HomeLayout::flix;
    auto library = std::make_unique<LibraryMessage>();
    library->account = 1;
    for (uint32_t i = 0; i < 14; ++i) {
        Game game{4000 + i, L"Flix game " + std::to_wstring(i)};
        game.last_played = 100 + i;
        library->games.push_back(std::move(game));
    }
    for (uint32_t c = 0; c < 2; ++c) {
        SteamCollection collection{L"Flix collection " + std::to_wstring(c), {}};
        for (uint32_t i = 0; i < 10; ++i) collection.app_ids.push_back(4000 + (i + c * 3) % 14);
        collection.id = "flix-" + std::to_string(c);
        library->collections.push_back(std::move(collection));
    }
    app.onLibrary(std::move(library));
    app.showCategory(0);
    // "Ostatnio grane" (8 games and "Wszystkie gry"), then one row per collection.
    if (!app.flixActive() || app.flix_rows_.size() != 3 || app.flix_row_ != 0 || app.flix_rows_[0].count != 9 ||
        app.cards_[8].game.kind != GameKind::all || app.flix_rows_[1].count != 10) return 951;
    const auto deliver = [&](const Game& game, CoverMessage::Kind kind, std::vector<uint8_t> bytes) {
        auto message = std::make_unique<CoverMessage>();
        message->key = gameKey(game); message->generation = app.jobs_->generation.load(); message->kind = kind; message->bytes = std::move(bytes);
        app.onCover(std::move(message));
    };
    for (size_t i = 0; i < app.cards_.size(); ++i) {
        if (!isGame(app.cards_[i].game)) continue;
        const auto tint = static_cast<uint8_t>(40 + (app.cards_[i].game.app_id % 14) * 12);
        deliver(app.cards_[i].game, app.flix_card_rows_[i] ? CoverMessage::Kind::poster : CoverMessage::Kind::landscape,
                app.flix_card_rows_[i] ? pngFixture(tint, 40, 60) : pngFixture(tint, 92, 43));
    }
    deliver(app.cards_[0].game, CoverMessage::Kind::hero, pngFixture(90, 192, 62));
    if (!app.renderer_.hasPoster(0) || !app.renderer_.hasPoster(app.flix_rows_[1].begin)) return 952;
    // The first row rests low, the background showing above it.
    settle();
    const auto view = app.renderer_.viewport();
    if (std::abs(app.cards_[0].y - (flix_layout::restTop(view.height) + flix_layout::header)) > 1 ||
        std::abs(app.cards_[0].aspect - flix_layout::landscape_aspect) > 0.001f) return 953;
    if (!capture(L"flix-home.png")) return 954;
    if (app.cards_[app.flix_rows_[1].begin].y - flix_layout::header < view.height) return 994;
    app.renderer_.setGlow(1, false, true);
    if (!capture(L"flix-travelling-outline.png")) return 995;
    app.renderer_.setGlow(1, true);
    // Right moves along the row, which scrolls once the focus would leave the screen.
    for (int i = 0; i < 5; ++i) app.navigate(1);
    if (app.selected_ != 5 || app.flix_rows_[0].first == 0) return 955;
    // Down: the next row, from its own position; each row remembers where it was.
    app.changeCategory(1);
    if (app.flix_row_ != 1 || app.selected_ != app.flix_rows_[1].begin || app.category_ != app.flix_rows_[1].category) return 956;
    app.navigate(1); app.navigate(1);
    app.changeCategory(1); app.changeCategory(-1);
    if (app.selected_ != app.flix_rows_[1].begin + 2) return 957;
    app.changeCategory(-1);
    if (app.selected_ != 5) return 958;
    app.changeCategory(1);
    settle();
    // The focused row now sits under the top edge, posters and all.
    if (std::abs(app.cards_[app.selected_].y - (flix_layout::focus_top + flix_layout::header)) > 1 ||
        std::abs(app.cards_[app.selected_].aspect - flix_layout::poster_aspect) > 0.001f) return 959;
    if (!capture(L"flix-rows.png")) return 960;
    {
        // "Pasek kategorii" (on by default): the rows' categories at the top in the middle; a click on
        // the next one goes to its row, LB/RB as before.
        if (!app.homeTabsShown() || app.homeTabs().size() != app.flix_rows_.size() || app.homeTabIndex() != static_cast<int>(app.flix_row_)) return 2792;
        const auto row = app.flix_row_, at = app.selected_;
        const auto next = library_tabs::home::tab(1, view.width, static_cast<float>(row));
        app.mouse_transition_until_ = 0;
        app.click((next.left + next.right) * 0.5f * view.scale, (next.top + next.bottom) * 0.5f * view.scale, false);
        if (app.flix_row_ != row + 1) return 2793;
        app.homePage(-1); settle();
        if (app.flix_row_ != row || app.selected_ != at) return 2794;
        app.settings_.flix_category_tabs = false;
        if (app.homeTabsShown()) return 2795;
        app.settings_.flix_category_tabs = true;
    }
    // Resting a few seconds on a poster unfolds it into the wide card (background, logo, facts); the
    // tiles after it make room, and moving on folds it back.
    {
        const auto watched = app.foreground_;
        app.foreground_ = [](HWND) { return true; };
        const auto at = app.selected_;
        deliver(app.cards_[at].game, CoverMessage::Kind::hero, pngFixture(150, 192, 62));
        const float poster_w = app.cards_[at].width(), next_x = app.cards_[at + 1].x, previous_x = app.cards_[at - 1].x, own_x = app.cards_[at].x;
        const auto rested = std::chrono::steady_clock::now() - std::chrono::seconds(4);
        app.tick(); // The window comes to the front: its idle period starts over (screensaver.cpp).
        app.flix_focus_at_ = rested; app.last_input_ = rested;
        settle();
        // It grows to both sides (it is not the first tile in view): half the extra width each way.
        const float half = flix_layout::unfoldExtra() * 0.5f;
        if (app.flix_unfold_index_ != at || app.flix_unfold_ < 0.99f || app.cards_[at].width() < poster_w * 2 ||
            std::abs(app.cards_[at + 1].x - (next_x + half)) > 2 || std::abs(app.cards_[at - 1].x - (previous_x - half)) > 2 ||
            std::abs(app.cards_[at].x - (own_x - half)) > 2) return 997;
        if (!capture(L"flix-unfolded.png")) return 998;
        app.navigate(1); settle();
        if (app.flix_unfold_ > 0.001f || app.flix_unfold_index_ != SIZE_MAX || std::abs(app.cards_[at].width() - poster_w) > 1) return 999;
        app.navigate(-1); settle();
        app.foreground_ = watched;
    }
    // The game menu opens beside the focused poster.
    app.openContextMenu(); app.menu_progress_ = 1;
    if (!app.menu_open_ || !app.menuBeside() || !capture(L"flix-menu.png")) return 961;
    app.dismissMenu(); app.menu_progress_ = 0;
    // "Zmień grafikę" from a poster row opens on posters; the chosen one replaces the row's image.
    {
        const auto chosen = app.cards_[app.selected_].game;
        const auto at = app.selected_;
        app.openArtworkBrowser(chosen);
        if (!app.artworkOpen() || app.artwork_state_.kind() != ArtworkKind::poster) return 970;
        auto posters = std::make_unique<ArtworkBrowserMessage>();
        posters->generation = app.artwork_jobs_->generation.load();
        posters->page = parseGridPage(R"({"data":[{"id":501,"width":600,"height":900,"url":"https://example.invalid/p.png"}]})");
        app.onArtworkBrowser(std::move(posters));
        app.artworkAccept(); app.artworkAccept(); // Into the gallery, then apply.
        const auto picked = pngFixture(220, 40, 60);
        if (!cacheArtworkVariant(chosen, ArtworkKind::poster, 501, picked, coverCacheDirectory())) return 971;
        auto applied = std::make_unique<ArtworkBrowserMessage>();
        applied->generation = app.artwork_jobs_->generation.load(); applied->type = ArtworkBrowserMessage::Type::apply;
        applied->asset_id = 501; applied->bytes = picked;
        app.onArtworkBrowser(std::move(applied));
        const auto* art = app.art_.peek(gameKey(chosen));
        if (selectedArtworkId(chosen, ArtworkKind::poster, coverCacheDirectory()) != 501 || !art || !art->poster || *art->poster != picked) return 972;
        if (!capture(L"flix-artwork-posters.png")) return 973;
        app.back(); app.back(); // Gallery, then back to Flix.
        if (!app.flixActive() || app.selected_ != at || !app.renderer_.hasPoster(at)) return 974;
        // The first row opens on wide tiles.
        app.flixFocus(0, 5); app.openArtworkBrowser(app.cards_[5].game);
        if (app.artwork_state_.kind() != ArtworkKind::landscape) return 975;
        app.back();
        app.flixFocus(1, 2);
    }
    // Left at the start of a row lists the categories; picking one scrolls to its row.
    app.flixFocus(1, 0); app.navigate(-1);
    if (!app.category_menu_open_) return 962;
    app.category_cursor_ = static_cast<int>(app.flix_rows_[2].category); app.categoryMenuAccept();
    if (app.flix_row_ != 2 || app.screen_ != ScreenMode::shelves || !app.flixActive()) return 963;
    // Back returns to the first row; Up from there opens the quick bar.
    app.back();
    if (app.flix_row_ != 0 || app.selected_ != 5) return 964;
    app.changeCategory(-1);
    if (!app.quick_open_) return 965;
    settle();
    if (app.cards_[app.flix_rows_[1].begin].y - flix_layout::header < view.height) return 996;
    app.closeQuick();
    // A click on another tile of the row focuses it.
    settle();
    const auto scale = view.scale;
    const auto& next = app.cards_[4];
    app.mouse_transition_until_ = 0;
    app.click((next.x + next.width() * 0.5f) * scale, (next.y + next.size * 0.5f) * scale, false);
    if (app.selected_ != 4 || app.flix_row_ != 0) return 966;
    // "Wszystkie gry" opens the library; Back returns to the same tile.
    app.flixFocusCard(8); app.launch();
    if (app.screen_ != ScreenMode::library) return 967;
    app.back();
    if (!app.flixActive() || app.flix_row_ != 0 || app.selected_ != 8) return 968;
    {
        // A rebuild of the same rows (library refresh, installs) keeps the decoded tiles in place:
        // no wave of posters fading in one after another.
        settle(); app.paint(); // A capture dropped the target; painting decodes the bound art again.
        const size_t poster = app.flix_rows_[1].begin + 1;
        if (!app.renderer_.posterReadyForTest(poster)) return 979;
        app.bindFlixArt(poster, true); // Another row's copy of the game rebinds the same bytes.
        if (!app.renderer_.posterReadyForTest(poster)) return 980;
        app.rebuildCards();
        if (!app.renderer_.posterReadyForTest(poster)) return 981;
        if (app.cards_[poster].opacity < 0.99f || !app.renderer_.posterReadyForTest(app.flix_rows_[0].first)) return 991;
        // Optional "Wszystkie" first in the game rows (not Recent): a random background of the
        // category; it opens the library of that category, with its own view options.
        for (uint32_t i = 0; i < 14; ++i) deliver(Game{4000 + i, L"Flix game " + std::to_wstring(i)}, CoverMessage::Kind::hero, pngFixture(static_cast<uint8_t>(30 + i * 9), 96, 31));
        app.settings_.flix_category_all = true;
        app.rebuildCards();
        const auto row = app.flix_rows_[1];
        const auto tile = app.cards_[row.begin].game; // Copies: the library replaces cards_ and rows.
        if (app.flix_rows_[0].count != 9 || app.cards_[8].game.kind != GameKind::all || row.count != 11 || app.flix_rows_[2].count != 11 ||
            tile.kind != GameKind::category || tile.source_id != app.categories_[row.category].id) return 982;
        const auto chosen = app.flix_backdrops_[tile.source_id];
        const auto& games = app.categories_[row.category].games;
        if (std::none_of(games.begin(), games.end(), [&](const Game& game) { return gameKey(game) == chosen; }) ||
            !app.renderer_.posterReadyForTest(row.begin)) return 983;
        app.flixFocus(1, 0); settle();
        if (!capture(L"flix-category-all.png")) return 984;
        app.launch();
        if (app.screen_ != ScreenMode::library || app.cards_.size() != 10 || app.category_library_title_ != app.categories_[row.category].title ||
            app.libraryScope() != "1:category:" + tile.source_id) return 985;
        for (const auto& card : app.cards_)
            if (std::none_of(games.begin(), games.end(), [&](const Game& game) { return gameKey(game) == gameKey(card.game); })) return 986;
        app.library_controls_.options.view = library_view::View::list; app.applyLibraryOptions();
        app.rememberPlace();
        if (!app.library_state_.bookmark || app.library_state_.bookmark->source != "category" ||
            app.library_state_.bookmark->category != tile.source_id || app.library_state_.options.at("1:category:" + tile.source_id).view != library_view::View::list ||
            (app.library_state_.options.contains("1:all") && app.library_state_.options.at("1:all").view == library_view::View::list)) return 987;
        app.back();
        if (!app.flixActive() || app.selected_ != app.flix_rows_[1].begin) return 988;
        app.openLibrary(); // All games again: not limited to the category any more.
        if (app.cards_.size() != app.public_library_.size()) return 989;
        app.back();
        app.settings_.flix_category_all = false;
        app.rebuildCards();
        if (app.flix_rows_[1].count != 10 || app.cards_[app.flix_rows_[1].begin].game.kind == GameKind::category) return 990;
        app.flixFocus(0, 8);
        // Ctrl+H adds the hidden and private games as the last row and focuses it; again removes it.
        Game hidden{4100, L"Hidden Flix game"}; hidden.visibility = GameVisibility::private_game;
        app.private_category_.games.push_back(hidden);
        app.togglePrivateCategory();
        if (!app.flixActive() || app.flix_rows_.back().id != "private" || app.flix_row_ + 1 != app.flix_rows_.size() ||
            app.cards_[app.selected_].game.app_id != 4100) return 992;
        // With "Wszystkie" on, that row has its tile too, and its library lists the hidden games.
        app.settings_.flix_category_all = true;
        app.rebuildCards();
        const auto hidden_row = app.flix_rows_.back();
        if (hidden_row.id != "private" || app.cards_[hidden_row.begin].game.kind != GameKind::category) return 994;
        app.flixFocus(app.flix_rows_.size() - 1, 0);
        app.launch();
        if (app.screen_ != ScreenMode::library || app.cards_.size() != app.private_category_.games.size() ||
            std::none_of(app.cards_.begin(), app.cards_.end(), [](const GameCard& card) { return card.game.app_id == 4100; })) return 995;
        app.back();
        app.settings_.flix_category_all = false;
        app.rebuildCards();
        app.flixFocus(app.flix_rows_.size() - 1, 0);
        app.togglePrivateCategory();
        if (app.flix_rows_.back().id == "private" || app.flix_row_ != 0) return 993;
        app.flixFocus(0, 8);
    }
    // The large tile size: bigger capsules and posters, fewer in a row; the rows keep their places.
    app.settings_.flix_size = AppSettings::FlixSize::large;
    {
        const float large = flix_layout::sizeScale(1);
        app.flixFocus(0, 0);
        settle();
        if (std::abs(app.cards_[0].size - flix_layout::landscape_height * large) > 1 ||
            std::abs(app.cards_[0].y - (flix_layout::restTop(view.height, large) + flix_layout::header)) > 1) return 976;
        if (app.cards_[app.flix_rows_[1].begin].y - flix_layout::header < view.height) return 997;
        app.flixFocus(1, 0);
        settle();
        const auto& poster = app.cards_[app.selected_];
        if (std::abs(poster.size - flix_layout::poster_height * large) > 1 ||
            std::abs(poster.y - (flix_layout::focus_top + flix_layout::header)) > 1 ||
            std::abs(app.cards_[app.selected_ + 1].x - poster.x - flix_layout::pitch(false, large)) > 1 ||
            flix_layout::visibleCount(false, view.width, large) >= flix_layout::visibleCount(false, view.width)) return 977;
        if (!capture(L"flix-large.png")) return 978;
    }
    app.settings_.flix_size = AppSettings::FlixSize::normal;
    app.setHomeLayout(0);
    app.showCategory(0);
    if (app.flixActive() || app.cards_.size() != app.categories_[0].games.size()) return 969;
    return 0;
}
