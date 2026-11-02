// Included after LauncherUiTest: the right drawer's dashboard (clock, Steam friends,
// Discord, CPU and GPU tiles) and the Steam friends list inside it. The clock, sensors
// and Discord lookups are injected fixtures and the executor is a fake, so this test
// never starts or focuses anything.
#include "../src/ui/drawer_clock.hpp"
#include "../src/ui/friends_layout.hpp"
#include <xinput.h>

namespace {
std::vector<GameAction> dashboard_launches;
INT_PTR dashboardExecute(HWND, const GameAction& action) {
    dashboard_launches.push_back(action);
    return 33;
}
} // namespace

int LauncherUiTest::dashboard(HWND window) {
    namespace board = friends_layout::dashboard;
    const SYSTEMTIME moment{2026, 9, 2, 29, 14, 5, 12, 0}; // Tuesday, 29 September 2026, 14:05.
    int focus_requests = 0;
    bool discord_window = false, discord_installed = true;
    Launcher app;
    app.hwnd_ = window; app.execute_ = dashboardExecute;
    app.local_time_ = [moment] { return moment; };
    app.read_sensors_ = [] { return sensors::Reading{true, 48, 61}; }; // Polled on the worker.
    app.discord_.running = [] { return false; };
    app.discord_.focus = [&] { ++focus_requests; return discord_window; };
    app.discord_.locate = [&]() -> std::optional<GameAction> {
        if (!discord_installed) return std::nullopt;
        return GameAction{L"C:\\Fixture\\Discord\\Update.exe", L"--processStart Discord.exe", L"C:\\Fixture\\Discord", ActionKind::open_client};
    };
    if (!app.renderer_.initialize(window)) return 700;
    // Always painted (into the config folder unless LAUNCHME_UI_CAPTURE_DIR is set), so the
    // dashboard drawing code runs in every test run.
    const auto capture = [&](const wchar_t* name) {
        wchar_t folder[4096]{};
        const auto count = GetEnvironmentVariableW(L"LAUNCHME_UI_CAPTURE_DIR", folder, 4096);
        const auto target = count && count < 4096 ? std::filesystem::path(folder) : userConfigDirectory();
        app.notifications_.clear();
        app.friends_progress_ = app.friends_open_ ? 1.0f : 0.0f;
        return app.renderer_.captureForTest(target / name, [&] { app.paint(); });
    };
    const auto open = [&] {
        app.tabDown(false); app.tabUp();
        return app.friendsOpen() && !app.friends_list_ && app.dashboard_tile_ == 0;
    };
    const auto friends = [&](std::wstring error = {}) {
        auto message = std::make_unique<FriendsMessage>();
        message->generation = app.friends_generation_;
        message->result.error = std::move(error);
        if (message->result.error.empty()) message->result.friends = {
            {11, L"Ania", L"Cyberpunk 2077", {}, 1}, {12, L"Bartek", L"Hades II", {}, 1}, {13, L"Celina", {}, {}, 1},
            {14, L"Darek", {}, {}, 3}, {15, L"Ewa", {}, {}, 1}, {16, L"Filip", {}, {}, 1}, {17, L"Gosia", {}, {}, 0}};
        app.onFriends(std::move(message));
        for (uint64_t id = 11; id <= 17; ++id) app.renderer_.setFriendAvatar(id, pngFixture(static_cast<uint8_t>(id * 13)));
    };
    const auto poll = [&](sensors::Reading reading, bool discord_running, uint32_t generation) {
        auto message = std::make_unique<DashboardMessage>();
        message->generation = generation;
        message->sensors = reading;
        message->discord_running = discord_running;
        app.onDashboard(std::move(message));
    };
    auto library = std::make_unique<LibraryMessage>(); library->account = 1;
    for (uint32_t i = 0; i < 4; ++i) {
        Game game{7000 + i, L"Dashboard fixture " + std::to_wstring(i)};
        game.last_played = 100 + i;
        library->games.push_back(std::move(game));
    }
    app.onLibrary(std::move(library));
    app.showCategory(0);

    // Short TAB/Start opens the tiles with the Steam friends tile focused and starts polling.
    if (!open()) return 701;
    friends();
    const auto generation = app.dashboard_jobs_->generation.load();
    poll({true, 48, 61}, false, generation);
    if (app.sensors_ != sensors::Reading{true, 48, 61} || app.discord_running_) return 702;
    poll({true, 99, 99}, true, generation - 1); // A reading from an earlier opening is ignored.
    if (app.sensors_.cpu != 48 || app.discord_running_) return 703;
    if (!capture(L"drawer-dashboard-pl.png")) return 704;
    if (app.clock_minute_ != drawer_clock::minute(moment)) return 705; // Painted at the fixture's minute.

    // D-pad/arrows move in 2D and stop at the edges.
    app.navigate(1);
    if (app.dashboard_tile_ != static_cast<int>(board::Tile::discord)) return 706;
    app.friendsMove(1);
    if (app.dashboard_tile_ != static_cast<int>(board::Tile::gpu)) return 707;
    app.navigate(-1);
    if (app.dashboard_tile_ != static_cast<int>(board::Tile::cpu)) return 708;
    app.friendsMove(1); // The profile: wide, under the temperatures, the last row.
    if (app.dashboard_tile_ != static_cast<int>(board::Tile::profile)) return 709;
    app.navigate(-1); app.friendsMove(1);
    if (app.dashboard_tile_ != static_cast<int>(board::Tile::profile)) return 754;
    app.friendsMove(-1);
    if (app.dashboard_tile_ != static_cast<int>(board::Tile::cpu)) return 755;
    app.friendsMove(-1);
    if (app.dashboard_tile_ != static_cast<int>(board::Tile::steam)) return 710;
    // Read-only tiles do nothing; the list is not open and nothing was launched.
    app.dashboardActivate(static_cast<int>(board::Tile::gpu));
    if (app.friends_list_ || !dashboard_launches.empty() || !app.friendsOpen()) return 711;
    if (!app.hud_menu_ || !app.powerMenuOpen()) return 9874; // ...but the widget's options open.
    app.closePowerMenu();
    app.dashboard_tile_ = static_cast<int>(board::Tile::steam);

    // A/Enter on Steam friends opens the list inside the drawer; Back returns, Back closes.
    app.enterDown(false); app.enterUp();
    if (!app.friendsOpen() || !app.friends_list_ || app.friends_cursor_ != 0) return 712;
    app.friendsMove(1); // Down now moves in the list, not between tiles.
    if (app.friends_cursor_ != 1 || app.dashboard_tile_ != static_cast<int>(board::Tile::steam)) return 713;
    app.friends_cursor_ = 0;
    if (!capture(L"drawer-friends-list-pl.png")) return 714;
    app.friendsBack(); // Esc/Backspace.
    if (!app.friendsOpen() || app.friends_list_) return 715;
    app.back(); // Pad B.
    if (app.friendsOpen()) return 716;
    // Closing stops the poll: its messages no longer apply.
    poll({true, 70, 70}, true, app.dashboard_jobs_->generation.load());
    if (app.sensors_.cpu != 48) return 717;

    // Mouse: tile click opens the list, the list's back button returns, outside closes.
    if (!open()) return 718;
    friends();
    const auto view = app.renderer_.viewport();
    const float left = view.width - friends_layout::drawer_width;
    const auto center = [&](friends_layout::Rect r, float& x, float& y) {
        x = (r.left + r.right) * 0.5f * view.scale; y = (r.top + r.bottom) * 0.5f * view.scale;
    };
    float x = 0, y = 0;
    center(board::tile(left, static_cast<int>(board::Tile::discord)), x, y);
    if (!app.hoverButton(x, y)) return 719;
    center(board::tile(left, static_cast<int>(board::Tile::cpu)), x, y);
    if (app.hoverButton(x, y)) return 720; // Temperatures are values, not buttons.
    center(board::tile(left, static_cast<int>(board::Tile::steam)), x, y);
    if (!app.hoverButton(x, y)) return 721;
    app.click(x, y, false);
    app.click(x, y, true); // The double-click's second press must not open a chat beneath.
    if (!app.friends_list_ || std::any_of(dashboard_launches.begin(), dashboard_launches.end(),
            [](const GameAction& action) { return action.target.starts_with(L"steam://"); })) return 722;
    center(friends_layout::listBack(left), x, y);
    if (!app.hoverButton(x, y)) return 723;
    app.click(x, y, false);
    if (app.friends_list_ || !app.friendsOpen()) return 724;
    app.click(40 * view.scale, 400 * view.scale, false);
    if (app.friendsOpen()) return 725;

    // Discord: no visible window -> Update.exe --processStart through the executor, drawer closes.
    if (!open()) return 726;
    app.navigate(1); app.friendsAccept();
    if (focus_requests != 1 || dashboard_launches.size() != 1 ||
        dashboard_launches.back().target != L"C:\\Fixture\\Discord\\Update.exe" ||
        dashboard_launches.back().arguments != L"--processStart Discord.exe" || app.friendsOpen()) return 727;
    // A visible Discord window is focused instead of launching again.
    discord_window = true;
    if (!open()) return 728;
    app.navigate(1); app.friendsAccept();
    if (focus_requests != 2 || dashboard_launches.size() != 1 || app.friendsOpen()) return 729;
    // Not installed: a notification, no launch, the drawer stays open.
    discord_window = false; discord_installed = false;
    if (!open()) return 730;
    app.navigate(1); app.notifications_.clear(); app.friendsAccept();
    if (dashboard_launches.size() != 1 || app.notifications_.empty() ||
        app.notifications_.back().title != L"Nie znaleziono Discorda" || !app.friendsOpen()) return 731;
    app.closeFriends();

    // Steam error (no Web API key) wraps inside the tile; a missing CPU sensor says so.
    if (!open()) return 732;
    friends(tr(L"Skonfiguruj klucz Steam Web API w ustawieniach użytkownika"));
    poll({true, std::nullopt, 87}, true, app.dashboard_jobs_->generation.load());
    if (app.sensors_.cpu || app.sensors_.gpu != 87 || !app.discord_running_) return 733;
    app.navigate(1);
    if (!capture(L"drawer-dashboard-pl-error.png")) return 734;
    app.closeFriends();

    // English: the same drawer, clock formatted as en-US.
    i18n::setLanguage(i18n::Language::en);
    if (!open()) { i18n::setLanguage(i18n::Language::pl); return 735; }
    friends();
    poll({true, std::nullopt, 87}, true, app.dashboard_jobs_->generation.load());
    const bool english = capture(L"drawer-dashboard-en.png");
    app.dashboardActivate(static_cast<int>(board::Tile::steam));
    const bool english_list = app.friends_list_ && capture(L"drawer-friends-list-en.png");
    i18n::setLanguage(i18n::Language::pl);
    if (!english || !english_list) return 736;
    app.closeFriends();

    // Reorganization: real input routes, no widget actions while editing.
    if (!open()) return 737;
    app.friendsMove(-1);
    if (app.dashboard_tile_ != board::reorganize) return 738;
    app.enterDown(false); app.enterUp();
    if (!app.dashboardEditing()) return 739;
    app.friendsMove(1);
    app.enterDown(false); app.enterDown(true);
    if (app.dashboardLayout().items.front().size != widgets::Size::square) return 740; // Deferred until release.
    app.enterUp();
    if (app.dashboardLayout().items.front().size != widgets::Size::wide || app.friends_list_) return 741;
    app.enterDown(false); app.enterUp();
    if (app.dashboardLayout().items.front().size != widgets::Size::icon) return 742;
    app.enterDown(false); app.cancelEnterHold(); app.enterUp();
    if (app.dashboardLayout().items.front().size != widgets::Size::icon) return 743; // Focus loss cannot resize.
    app.enterDown(false); app.dashboard_pressed_at_ -= 500; app.pollHoldGestures();
    app.navigate(1); app.enterUp();
    if (app.dashboardLayout().items.front().id != "discord" || app.dashboard_tile_ != 0 || app.dashboard_grabbed_ ||
        app.settings_.widget_layout[1].size != widgets::Size::icon) return 744;
    // Pad: press/release cycles the size; holding A + direction moves the same identity.
    app.overlay_backend_.foreground = [&] { return window; };
    PadReading pad{true, L"widget-fixture"}; app.controller(pad);
    pad.buttons = XINPUT_GAMEPAD_A; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    if (app.settings_.widget_layout[1].size != widgets::Size::square) return 745;
    pad.buttons = XINPUT_GAMEPAD_A; app.controller(pad);
    app.dashboard_pressed_at_ -= 500;
    pad.buttons |= XINPUT_GAMEPAD_DPAD_LEFT; app.controller(pad);
    pad.buttons = 0; app.controller(pad);
    if (app.dashboardLayout().items.front().id != "steam" || app.dashboardLayout().items.front().size != widgets::Size::square) return 746;
    // Mouse click vs drag, including cancellation on capture loss.
    center(app.dashboardLayout().tile(0, left, app.dashboard_scroll_), x, y);
    app.click(x, y, false); app.pointerUp();
    if (app.dashboardLayout().items.front().size != widgets::Size::wide) return 747;
    center(app.dashboardLayout().tile(0, left, app.dashboard_scroll_), x, y);
    app.click(x, y, false); app.pointerUp(true);
    if (app.dashboardLayout().items.front().size != widgets::Size::wide) return 748;
    app.click(x, y, false);
    center(app.dashboardLayout().tile(1, left, app.dashboard_scroll_), x, y);
    app.mouseMove(x, y); app.pointerUp();
    if (app.dashboardLayout().items.front().id != "discord" || app.settings_.widget_layout[1].size != widgets::Size::wide) return 749;
    if (!capture(L"drawer-widgets-reorganize.png")) return 750;
    app.friendsBack(); // Exit editing and persist, keeping the drawer open.
    if (app.dashboardEditing() || !app.friendsOpen() || loadAppSettings().widget_layout != app.settings_.widget_layout) return 751;
    app.closeFriends();
    app.tabDown(false); app.tabUp();
    if (app.dashboard_tile_ != 1 || app.dashboardEditing()) return 752;
    // A long layout scrolls, and navigation brings the focused tile fully on-screen.
    app.dashboardEdit(true);
    for (auto& p : app.settings_.widget_layout) p.size = widgets::Size::wide;
    app.settings_dirty_ = true;
    app.dashboard_tile_ = 3; app.dashboardEnsureVisible();
    const auto visible = app.dashboardLayout().tile(3, 0, app.dashboard_scroll_);
    if (visible.bottom > view.height - 19 || (app.dashboardLayout().maxScroll(view.height) > 0 && app.dashboard_scroll_ <= 0)) return 753;
    app.closeFriends();
    app.settings_.widget_layout.clear(); app.settings_dirty_ = true; app.saveSettings(); // Isolate later UI fixtures.
    return 0;
}
