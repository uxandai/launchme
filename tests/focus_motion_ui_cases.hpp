// Focus motion (src/app/focus_motion.cpp): Settings Back by keyboard, the gliding Settings
// pill/row and quick bar circle, the experimental spring outline with its edge glow, and the
// XMB corner logo, gliding menu/panel/drawer highlights and the friends list's Back. Frames are
// simulated by moving last_frame_ back before each tick().
#include "../src/ui/friends_layout.hpp"

int LauncherUiTest::focusMotion(HWND window) {
    Launcher app;
    app.hwnd_ = window; app.execute_ = fakeExecute;
    if (!app.renderer_.initialize(window)) return 1600;
    app.boundary_feedback_ = [] {};
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 12; ++i) {
        Game game{7000 + i, L"Motion fixture " + std::to_wstring(i)}; game.last_played = 100 + i;
        library->games.push_back(game);
    }
    app.onLibrary(std::move(library));
    const auto frame = [&](int ms) { app.last_frame_ -= std::chrono::milliseconds(ms); app.tick(); };
    const auto settle = [&] { for (int i = 0; i < 40; ++i) frame(50); };
    wchar_t folder[4096]{};
    const auto folder_length = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
    const auto capture = [&](const std::wstring& name) {
        app.notifications_.clear();
        return !folder_length || folder_length >= 4096 ||
            app.renderer_.captureForTest(std::filesystem::path(folder) / name, [&] { app.paint(); });
    };
    const auto view = app.renderer_.viewport();
    const int last_category = static_cast<int>(settings_ui::categories.size()) - 1;

    // Settings: Up past the first category focuses Back (a dot by the heading), Down returns, Enter/A leaves Settings.
    app.openSettings();
    for (int i = 0; i < last_category + 3; ++i) app.changeCategory(-1);
    if (!app.settings_state_.backFocused() || app.settings_state_.category != 0) return 1601;
    settle();
    const auto back = settings_ui::Layout::of(view.width, view.height, 0).backButton();
    if (std::abs(app.settings_motion_.pill_top.value - back.top) > 0.5f) return 1602; // The pill sits on Back.
    if (!capture(L"settings-back-focus.png")) return 1603;
    app.changeCategory(1);
    if (app.settings_state_.backFocused() || app.settings_state_.category != 0) return 1604;    app.changeCategory(-1); app.settingsAdjust(1); // Right on Back does not enter the rows.
    if (!app.settings_state_.backFocused()) return 1605;
    app.settingsActivate();
    if (app.settingsOpen()) return 1606;
    app.openSettings();
    if (app.settings_state_.backFocused()) return 1607; // Reopening starts on the rail.

    // Settings pill and row highlight glide; mid-move they are between items, then settle on target.
    app.settings_state_.selectCategory(0); app.buildSettingsRows(); settle();
    const auto layout = settings_ui::Layout::of(view.width, view.height, 0);
    app.changeCategory(1); frame(33);
    const float pill = app.settings_motion_.pill_top.value;
    if (!(pill > layout.rail(0).top + 1 && pill < layout.rail(1).top - 1)) return 1608;
    if (!capture(L"settings-pill-glide.png")) return 1609;
    settle();
    if (std::abs(app.settings_motion_.pill_top.value - layout.rail(1).top) > 0.5f) return 1610;
    app.settingsAdjust(1); // Right: into the rows.
    if (!app.settings_state_.options || app.settings_rows_.size() < 2) return 1611;
    settle();
    // Rows under section headings: the glide runs between the two rows' offsets.
    const auto rows = settings_ui::Layout::of(view.width, view.height, app.settings_state_, app.settings_rows_);
    const float from = rows.rowOffset(0), to = rows.rowOffset(1);
    app.changeCategory(1); frame(33);
    const float row = app.settings_motion_.row.value;
    if (!(row > from + 1 && row < to - 1)) return 1612;
    if (!capture(L"settings-row-glide.png")) return 1613;
    settle();
    if (std::abs(app.settings_motion_.row.value - to) > 0.5f) return 1614;
    app.back(); app.back();
    if (app.settingsOpen()) return 1615;

    // Quick bar: the circle glides from the first icon to the second.
    app.showCategory(0); app.openQuick();
    if (!app.quick_open_) return 1616;
    settle();
    const float first = quick_menu::center(view.width, 0, quick_menu::store_slots);
    const float second = quick_menu::center(view.width, 1, quick_menu::store_slots);
    if (std::abs(app.quick_circle_.value - first) > 0.5f) return 1617;
    app.quickMove(1); frame(33);
    if (!(app.quick_circle_.value > first + 1 && app.quick_circle_.value < second - 1)) return 1618;
    if (!capture(L"quick-circle-glide.png")) return 1619;
    settle();
    if (std::abs(app.quick_circle_.value - second) > 0.5f) return 1620;
    if (!capture(L"quick-circle-rest.png")) return 1639; // White circle, dark icon.
    app.closeQuick(); settle();

    // Spring outline, off by default: the card keeps its own edge and nothing springs.
    for (size_t i = 0; i < app.cards_.size(); ++i) {
        app.renderer_.setCover(i, pngFixture(static_cast<uint8_t>(60 + i * 12), 48, 48), {});
        app.cards_[i].cover_opacity = 1;
    }
    app.focusCard(0); settle();
    if (app.focus_ring_.valid) return 1621;
    app.navigate(-1); app.closeCategoryMenu(); settle(); // First card: the drawer, not an edge.
    if (app.focus_ring_.glow > 0) return 1622;
    // On: the outline has its own rectangle; moving right it arrives, swings past and settles.
    app.settings_.focus_spring = true;
    app.focusCard(0); settle();
    const auto& card = app.cards_[app.selected_];
    if (!app.focus_ring_.valid || std::abs(app.focus_ring_.x.value - (card.x + card.width() * 0.5f)) > 1 ||
        std::abs(app.focus_ring_.width.value - card.width()) > 1) return 1623;
    const float start = app.focus_ring_.x.value;
    app.navigate(1);
    float furthest = start;
    for (int i = 0; i < 40; ++i) {
        frame(16);
        furthest = std::max(furthest, app.focus_ring_.x.value);
        if (i == 3 && !capture(L"focus-spring-move.png")) return 1624;
    }
    settle();
    const float rest = app.focus_ring_.x.value;
    if (rest <= start + 20 || furthest < rest + 2) return 1625; // Moved right and overshot.
    if (std::abs(rest - (app.cards_[app.selected_].x + app.cards_[app.selected_].width() * 0.5f)) > 1) return 1626;
    // List edge: the outline bumps toward it with an inner glow on that side, then calms down.
    app.focusCard(app.cards_.size() - 1); settle();
    app.navigate(1);
    if (app.focus_ring_.glow < 0.99f || app.focus_ring_.glow_direction != 1 || app.focus_ring_.x.velocity <= 0) return 1627;
    frame(16); frame(16);
    if (!capture(L"focus-spring-edge.png")) return 1628;
    settle();
    if (app.focus_ring_.glow > 0) return 1629;
    // Menus over the cards are not card edges.
    app.openContextMenu(); app.menuMove(-10);
    if (app.focus_ring_.glow > 0) return 1630;
    app.dismissMenu(); settle();
    // Every key/pad move glows at half strength on the side it goes toward (here left)...
    app.navigate(-1); frame(16);
    if (app.focus_ring_.glow_peak != 0.5f || app.focus_ring_.glow_direction != -1 || app.focus_ring_.glow < 0.9f) return 1640;
    settle();
    // ...also up, in the library grid; a mouse-picked card does not glow.
    app.openLibrary(); settle();
    const auto columns = static_cast<size_t>(app.libraryColumns());
    if (app.cards_.size() <= columns) return 1641;
    app.focusCard(columns); settle();
    app.changeCategory(-1); frame(16);
    if (app.selected_ != 0 || app.focus_ring_.glow_direction != -2 || app.focus_ring_.glow_peak != 0.5f) return 1642;
    if (!capture(L"focus-spring-up.png")) return 1643;
    settle();
    app.nav_direction_at_ = 0; app.focusCard(1); frame(16);
    if (app.focus_ring_.glow > 0) return 1644;
    app.back(); settle();

    // Context menu, power menu: the highlight glides between rows.
    app.openContextMenu(); settle();
    app.menuMove(1); frame(33);
    if (!(app.menu_glide_.value.value > 0.05f && app.menu_glide_.value.value < 0.95f)) return 1645;
    if (!capture(L"context-menu-glide.png")) return 1646;
    settle();
    if (std::abs(app.menu_glide_.value.value - 1) > 0.01f) return 1647;
    app.dismissMenu(); settle();
    app.openPowerMenu(); settle();
    app.powerMenuMove(1); frame(33);
    if (!(app.power_glide_.value.value > 0.05f && app.power_glide_.value.value < 0.95f)) return 1648;
    if (!capture(L"power-menu-glide.png")) return 1649;
    app.closePowerMenu(); settle();

    // Devices: one frame glides between Back and the items (Controllers, Audio).
    app.openDevices(devices::Panel::controllers); settle();
    const device_ui::Layout devices_layout{view.width, view.height, devices::Panel::controllers};
    app.deviceMove(-1); frame(33);
    if (!app.device_glide_.valid || !(app.device_glide_.box.top.value > devices_layout.back().top + 1)) return 1650;
    if (!capture(L"devices-glide.png")) return 1651;
    settle();
    if (std::abs(app.device_glide_.box.top.value - devices_layout.back().top) > 0.5f) return 1652;
    app.back();
    app.openDevices(devices::Panel::audio); settle();
    app.deviceMove(1); frame(33);
    if (!capture(L"audio-glide.png")) return 1653;
    settle();
    app.back(); settle();

    // Drawer: the focus frame glides between tiles; in the friends list between friends and Back.
    app.friends_open_ = true; app.friends_list_ = false; app.dashboard_tile_ = 0;
    settle();
    const auto tile0 = friends_layout::dashboard::tile(0, 0), tile1 = friends_layout::dashboard::tile(0, 1);
    app.dashboardMove(1, 0); frame(33);
    const float tile_left = app.drawer_glide_.box.left.value;
    if (!(tile_left > tile0.left + 1 && tile_left < tile1.left + 30)) return 1654;
    if (!capture(L"drawer-tiles-glide.png")) return 1655;
    settle();
    app.dashboard_tile_ = 0;
    app.friends_.friends = {{1, L"Ala", L"Hades", L"", 1}, {2, L"Bartek", L"", L"", 1}, {3, L"Celina", L"", L"", 0}};
    app.dashboardActivate(0);
    if (!app.friends_list_) return 1656;
    settle();
    app.friendsMove(1); frame(33);
    if (!capture(L"friends-glide.png")) return 1657;
    settle();
    app.friendsMove(-1); app.friendsMove(-1); // First friend, then Back (top left).
    if (!app.friends_back_) return 1658;
    app.friendsMove(-1);
    if (!app.friends_back_) return 1659; // Nothing above Back.
    settle();
    const auto list_back = friends_layout::listBack(0);
    if (std::abs(app.drawer_glide_.box.top.value - list_back.top) > 0.5f) return 1660;
    if (!capture(L"friends-back-focus.png")) return 1661;
    app.friendsMove(1);
    if (app.friends_back_ || app.friends_cursor_ != 0) return 1662; // Down: the first friend again.
    app.friendsMove(-1); app.friendsAccept(); // Enter/A on Back: the tiles.
    if (app.friends_list_ || !app.friends_open_) return 1663;
    app.friends_open_ = false; settle();

    // XMB: the focused game's logo in the bottom-right corner; the spring outline in XMB too.
    app.settings_.home_layout = AppSettings::HomeLayout::xmb; app.buildShelves(); app.showCategory(0);
    if (!app.xmbActive()) return 1631;
    app.xmbFocus(0, -1);
    for (size_t i = 0; i < app.cards_.size(); ++i) {
        app.renderer_.setCover(i, pngFixture(static_cast<uint8_t>(60 + i * 12), 48, 48), {});
        app.cards_[i].cover_opacity = 1;
    }
    auto logo = std::make_unique<CoverMessage>();
    logo->key = gameKey(app.cards_[app.selected_].game); logo->generation = app.jobs_->generation.load();
    logo->kind = CoverMessage::Kind::logo; logo->bytes = pngFixture(200, 240, 80, true);
    app.onCover(std::move(logo));
    settle();
    if (!capture(L"xmb-logo.png")) return 1632;
    app.navigate(1); frame(16); frame(16);
    if (!capture(L"xmb-focus-spring.png")) return 1633;
    settle();
    app.settings_.focus_spring = false; settle();
    if (app.focus_ring_.valid) return 1634;
    return 0;
}
