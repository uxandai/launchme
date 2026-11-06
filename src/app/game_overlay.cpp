#include "launcher.hpp"
#include "../ui/quick_menu.hpp"
#include "../ui/friends_layout.hpp"
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace {
bool windowMatches(const GameWindow& target) {
    DWORD pid = 0;
    return IsWindow(target.window) && GetWindowThreadProcessId(target.window, &pid) && pid == target.process.pid &&
        process_freeze::alive({target.process});
}
// The keyboard shortcut of the in-game overlay (Settings → Kontrolery): modifiers and key, or none.
bool overlayChord(AppSettings::OverlayKey key, UINT& modifiers, UINT& vk) {
    using Key = AppSettings::OverlayKey;
    switch (key) {
    case Key::shift_tab: modifiers = MOD_SHIFT; vk = VK_TAB; return true;
    case Key::ctrl_tab: modifiers = MOD_CONTROL; vk = VK_TAB; return true;
    case Key::shift_f1: modifiers = MOD_SHIFT; vk = VK_F1; return true;
    case Key::ctrl_shift_o: modifiers = MOD_CONTROL | MOD_SHIFT; vk = 'O'; return true;
    case Key::none: break;
    }
    return false;
}
RECT monitorRect(HWND window) {
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
    return monitor.rcMonitor;
}
}

GameOverlayBackend::GameOverlayBackend() {
    valid = windowMatches;
    foreground = GetForegroundWindow;
    show = [](OverlayWindow& window, const RECT& rect) { return window.show(rect, true); };
    focus = [](HWND window) {
        if (IsIconic(window) || !IsWindowVisible(window)) ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
    };
}

int Launcher::dashboardTileCount() const { return friends_layout::dashboard::tile_count + static_cast<int>(plugin_widgets_.size()) + settings_.game_fps; }

void Launcher::configureGameOverlay() {
    const bool guide = settings_.guide_overlay && settings_.guide_menu;
    if (!guide && gameOverlayOpen()) closeGameOverlay(true);
    if (runtime_started_) {
        if (guide && !guide_timer_active_) guide_timer_active_ = SetTimer(hwnd_, guide_timer, 16, nullptr) != 0;
        else if (!guide && guide_timer_active_) { KillTimer(hwnd_, guide_timer); guide_timer_active_ = false; }
        if (guide || settings_.game_fps || settings_.fps_limiter) overlay_monitor_.start(work_);
    }
    overlay_monitor_.configure(guide || settings_.game_fps || settings_.fps_limiter, settings_.game_fps, overlay_target_ ? overlay_target_->window : nullptr);
    if (game_overlay_) game_overlay_->appearance(settings_);
    dashboard_tile_ = std::min(dashboard_tile_, dashboardTileCount() - 1);
    next_overlay_poll_ = 0;
    // A changed shortcut registers again from scratch.
    if (guide_hotkey_active_) { UnregisterHotKey(hwnd_, guide_hotkey); guide_hotkey_active_ = false; }
    next_guide_hotkey_retry_ = 0;
    updateGuideHotkey();
}

bool Launcher::guideHotkeyWanted() const {
    if (closing_ || !settings_.guide_overlay || !settings_.guide_menu) return false;
    const auto foreground = overlay_backend_.foreground();
    if (!foreground || foreground == hwnd_) return false;
    return gameOverlayOpen() ? overlayForeground(foreground) :
        overlay_snapshot_.target && foreground == overlay_snapshot_.target->window;
}

void Launcher::updateGuideHotkey() {
    // The shortcut (Shift+Tab by default) is ordinary input outside a game. Do not
    // reserve it on the desktop, in other applications, or in Launchme's own settings.
    UINT modifiers = 0, vk = 0;
    const bool wanted = runtime_started_ && overlayChord(settings_.overlay_key, modifiers, vk) && guideHotkeyWanted();
    if (!wanted) {
        if (guide_hotkey_active_) UnregisterHotKey(hwnd_, guide_hotkey);
        guide_hotkey_active_ = false; next_guide_hotkey_retry_ = 0;
    } else if (!guide_hotkey_active_ && GetTickCount64() >= next_guide_hotkey_retry_) {
        guide_hotkey_active_ = RegisterHotKey(hwnd_, guide_hotkey, modifiers | MOD_NOREPEAT, vk) != FALSE;
        next_guide_hotkey_retry_ = GetTickCount64() + 1000;
    }
}

void Launcher::guideKeyboardHotkey() {
    if (!guideHotkeyWanted()) { updateGuideHotkey(); return; } // Ignore a queued hotkey after Alt+Tab.
    if (gameOverlayOpen()) dismissGameOverlay(); // The keyboard chord always closes the entire overlay.
    else toggleGameOverlay();
}

bool Launcher::overlayChordModifiers() const {
    UINT modifiers = 0, vk = 0;
    if (!overlayChord(settings_.overlay_key, modifiers, vk)) return false;
    const auto held = [](int key) { return (GetKeyState(key) & 0x8000) != 0; };
    return held(VK_SHIFT) == ((modifiers & MOD_SHIFT) != 0) && held(VK_CONTROL) == ((modifiers & MOD_CONTROL) != 0) && !held(VK_MENU);
}

void Launcher::overlayKeyboard(WPARAM key, bool repeated, bool chord) {
    UINT modifiers = 0, vk = 0;
    if (chord && overlayChord(settings_.overlay_key, modifiers, vk) && key == vk) {
        // Registered chords are handled once by WM_HOTKEY on the main HWND.
        // If registration was unavailable, the focused overlay can still close.
        if (!repeated && !guide_hotkey_active_) guideKeyboardHotkey();
        return;
    }
    overlayKey(key, repeated);
}

void Launcher::pollGuideInput() {
    if (closing_ || !guide_timer_active_) return;
    updateGuideHotkey();
    controller();
    tickGameOverlay(GetTickCount64());
    updateHud();
}

void Launcher::toggleHudPin(const std::string& id) {
    auto& pins = settings_.hud_pins;
    const auto found = std::find(pins.begin(), pins.end(), id);
    if (found != pins.end()) pins.erase(found); else pins.push_back(id);
    settings_dirty_ = true; next_hud_at_ = 0;
    audio_.context(); invalidateGameOverlay();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT Launcher::hudMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_PAINT && hud_window_) {
        ViewContext context;
        context.fps_widget = settings_.game_fps; context.fps_low = settings_.game_fps_low; context.fps_graph = settings_.fps_graph;
        context.game_fps = overlay_snapshot_.fps;
        context.dashboard.sensors = sensors_;
        context.hud_pins = settings_.hud_pins;
        context.hud_badges = hudBadges();
        context.hud_layout = settings_.hud_layout;
        context.hud_label_colors = settings_.hud_label_colors;
        context.hud_scale = settings_.hud_size / 100.0f;
        hud_window_->renderer().paintHud(context);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

// "LIMIT 60" and the mode, while the limiter really works in the game in front.
std::vector<std::wstring> Launcher::hudBadges() const {
    std::vector<std::wstring> badges;
    const auto& target = overlay_snapshot_.target;
    if (!settings_.fps_badges || !target) return badges;
    const int limit = gameFpsLimit(target->key);
    if (!limit || limiter_.state(target->process.pid) != frame_limiter::State::active) return badges;
    badges.push_back(L"LIMIT " + std::to_wstring(limit));
    badges.push_back(settings_.fps_limit_mode == 1 ? L"LOW LATENCY" : L"SMOOTH");
    return badges;
}

void Launcher::updateHud() {
    // Over the game in front (Guide closed): the pinned widgets in a corner. Never interactive.
    const auto target = overlay_snapshot_.target;
    const bool want = runtime_started_ && guideOverlayOn() && settings_.hud_enabled && (!settings_.hud_pins.empty() || !hudBadges().empty()) && !gameOverlayOpen() && target &&
        overlay_backend_.foreground() == target->window;
    if (!want) {
        if (hud_window_ && hud_visible_) hud_window_->hide();
        hud_visible_ = false;
        if (hud_polling_) { hud_polling_ = false; if (!friends_open_) stopDashboardPoll(); }
        return;
    }
    const auto now = GetTickCount64();
    if (now < next_hud_at_) return;
    next_hud_at_ = now + 500;
    if (!hud_window_) {
        auto window = std::make_unique<OverlayWindow>();
        if (!window->create(true, [this](HWND w, UINT m, WPARAM p, LPARAM l) { return hudMessage(w, m, p, l); })) return;
        hud_window_ = std::move(window);
        hud_window_->appearance(settings_);
    }
    const auto& pins = settings_.hud_pins;
    const bool sensors = std::any_of(pins.begin(), pins.end(), [](const std::string& pin) { return pin == "cpu" || pin == "gpu" || pin == "ram" || pin == "vram"; });
    if (sensors && !hud_polling_) { hud_polling_ = true; startDashboardPoll(); }
    else if (!sensors && hud_polling_) { hud_polling_ = false; if (!friends_open_) stopDashboardPoll(); }
    std::wstring signature;
    for (const auto& pin : pins) signature += std::wstring(pin.begin(), pin.end()) + L',';
    signature += std::to_wstring(static_cast<int>(overlay_snapshot_.fps.status)) + L',' +
        std::to_wstring(static_cast<int>(overlay_snapshot_.fps.fps + 0.5)) + L',' + std::to_wstring(static_cast<int>(overlay_snapshot_.fps.low + 0.5)) + L',' +
        std::to_wstring(sensors_.cpu.value_or(-1)) + L',' + std::to_wstring(sensors_.gpu.value_or(-1)) + L',' +
        std::to_wstring(sensors_.ram_used.value_or(-1) / 64) + L',' + std::to_wstring(sensors_.vram_used.value_or(-1) / 64);
    for (const auto& badge : hudBadges()) signature += L',' + badge;
    signature += L';' + std::to_wstring(settings_.hud_layout) + std::to_wstring(settings_.hud_label_colors) + L',' + std::to_wstring(settings_.hud_size);
    if (settings_.fps_graph) { // A new curve every half second.
        uint32_t hash = 17;
        for (const float value : overlay_snapshot_.fps.graph) hash = hash * 31 + static_cast<uint32_t>(value * 4);
        signature += L',' + std::to_wstring(hash);
    }
    if (!hud_visible_) {
        hud_signature_ = signature;
        hud_visible_ = hud_window_->show(monitorRect(target->window), false);
    } else if (signature != hud_signature_) {
        hud_signature_ = signature;
        hud_window_->invalidate();
    }
}

void Launcher::tickGameOverlay(uint64_t now) {
    if (!gameOverlayOpen() || now <= overlay_tick_at_) return;
    const float seconds = static_cast<float>(now - overlay_tick_at_) / 1000;
    overlay_tick_at_ = now;
    const float x = quick_menu::center(drawerViewport().width, overlay_selected_, quick_menu::store_slots, true);
    bool moved = overlay_motion_.step(seconds, overlay_closing_, friends_open_, x);
    animateMenuFocus(std::min(seconds, 0.05f), moved); // The drawer's focus frame and the dialog's highlight glide here too.
    if (overlay_closing_ && overlay_motion_.hidden()) { closeGameOverlay(true); return; }
    dashboardPollHold(now);
    // The focus ring's light keeps circling while the bar is shown (wrapped: float precision).
    const bool ring = overlay_motion_.bar > 0.003f && !overlay_closing_;
    if (ring) overlay_ring_time_ = std::fmod(overlay_ring_time_ + seconds, 400.0f);
    if (moved || ring || dashboardEditing() || game_overlay_->renderer().animating()) invalidateGameOverlay();
}

void Launcher::dismissGameOverlay() {
    if (!gameOverlayOpen() || overlay_closing_) return;
    overlay_closing_ = true;
    closeFriends(); closePowerMenu();
    invalidateGameOverlay();
}

void Launcher::toggleGameOverlay(bool widgets_first) {
    if (!settings_.guide_overlay || !settings_.guide_menu) return;
    if (gameOverlayOpen()) { overlayKey(VK_HOME); return; }
    if (overlay_backend_.foreground() == hwnd_) { openGuideMenu(); return; }
    const auto snapshot = overlay_backend_.read ? overlay_backend_.read() : overlay_monitor_.snapshot();
    if (snapshot.target && overlay_backend_.foreground() == snapshot.target->window) openGameOverlay(*snapshot.target, widgets_first);
}

void Launcher::openOverlayDrawer() {
    friends_open_ = true; friends_list_ = false; plugin_drawer_active_ = false;
    dashboard_tile_ = 0; drawer_glide_.valid = false; clock_check_at_ = 0;
    dashboard_scroll_ = 0;
    if (const auto layout = dashboardLayout(); !layout.items.empty()) dashboard_tile_ = layout.items.front().source;
    if (runtime_started_) { loadFriends(); startDashboardPoll(); }
    audio_.drawer();
}

bool Launcher::openGameOverlay(const GameWindow& target, bool widgets_first) {
    if (!settings_.guide_overlay || !settings_.guide_menu || !overlay_backend_.valid(target)) return false;
    cancelGameFocus();
    if (!game_overlay_) {
        auto overlay = std::make_unique<OverlayWindow>();
        if (!overlay->create(false, [this](HWND w, UINT m, WPARAM p, LPARAM l) { return overlayMessage(w, m, p, l); })) return false;
        game_overlay_ = std::move(overlay);
        game_overlay_->appearance(settings_);
    }
    closeFriends(); friends_progress_ = 0;
    overlay_selected_ = 0; overlay_target_ = target;
    overlay_closing_ = false;
    overlay_tick_at_ = GetTickCount64();
    pollBackgroundMode(false); // The overlay is drawn by this process: its normal priority back at once.
    frame_limit_ = 240;
    // Size the hidden window before the first frame, so the spring starts at
    // Home on the game's monitor rather than the old 1x1/minimized viewport.
    const auto rect = monitorRect(target.window);
    SetWindowPos(game_overlay_->handle(), nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
    overlay_motion_.open(quick_menu::center(drawerViewport().width, 0, quick_menu::store_slots, true));
    updateGuideHotkey();
    overlay_monitor_.configure(true, settings_.game_fps, target.window);
    bool shown = false;
    {
        struct Opening { bool& flag; Opening(bool& value) : flag(value) { flag = true; } ~Opening() { flag = false; } } opening{overlay_opening_};
        shown = overlay_backend_.show(*game_overlay_, rect);
    }
    if (!shown) { closeGameOverlay(false); return false; }
    // A window may be recognized before the periodic running-game scan. Adopt it
    // through the existing session path so Home immediately offers "Wróć".
    if (!game_sessions_.contains(target.key)) {
        std::optional<Game> game;
        for (const auto* games : {&public_library_, &private_category_.games}) {
            const auto found = std::find_if(games->begin(), games->end(), [&](const Game& value) { return gameKey(value) == target.key; });
            if (found != games->end()) { game = *found; break; }
        }
        if (game) {
            auto message = std::make_unique<RunningGamesMessage>();
            message->version = running_version_; message->games.push_back(*game);
            onRunningGames(std::move(message));
            // Exact process identity also works for Steam executables started
            // directly, before Steam reports them through its registry state.
            trackSession(target.key, *game, {target.process});
        }
    }
    // The widgets are what Guide is opened for in a game: shown at once, Back leaves them for the bar.
    if (widgets_first && settings_.guide_open_widgets) { openOverlayDrawer(); invalidateGameOverlay(); }
    // Opening Guide never freezes the game. Some titles keep reading XInput in
    // the background; focus alone cannot universally block their controller.
    audio_.quick();
    return true;
}

void Launcher::closeGameOverlay(bool return_to_game) {
    if (!overlay_target_) return;
    const auto target = *overlay_target_;
    overlay_target_.reset(); // Hide may synchronously send WM_ACTIVATE.
    overlay_closing_ = false;
    overlay_motion_ = {};
    closeFriends(); friends_progress_ = 0;
    closePowerMenu();
    // Transfer while our overlay still owns foreground permission. Hiding it
    // first can give foreground to another process and make this request fail.
    if (return_to_game && overlay_backend_.valid(target)) overlay_backend_.focus(target.window);
    if (game_overlay_) game_overlay_->hide();
    pad_start_.swallow(false); controller_direction_ = 0;
    overlay_monitor_.configure((settings_.guide_overlay && settings_.guide_menu) || settings_.game_fps || settings_.fps_limiter, settings_.game_fps);
    next_overlay_poll_ = 0;
    updateGuideHotkey();
}

void Launcher::overlayDashboard() {
    // Whatever the launcher was doing behind the game (tray, background priority, released graphics)
    // ends before it is shown, so the page opens live instead of as a frozen frame.
    leaveTray(); pollBackgroundMode(false); restoreMemory();
    still_since_ = std::chrono::steady_clock::now(); frame_limit_ = 240;
    overlay_backend_.focus(hwnd_);
    closeGameOverlay(false);
    // Hiding the overlay hands the foreground back to the game: the launcher takes it again (with the
    // retries of a refused return), or its page would open behind the game.
    bringToFront();
    if (settingsOpen()) saveSettings();
    closeQuick(); quick_progress_ = 0; closeCategoryMenu(); dismissMenu();
    showCategory(0);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::overlayAccept() {
    if (!gameOverlayOpen()) return;
    const auto action = quick_menu::action(overlay_selected_, 0, true);
    if (action == quick_menu::Action::widgets) { openOverlayDrawer(); invalidateGameOverlay(); return; }
    if (action == quick_menu::Action::close_game) { openGameCloseMenu(); return; }
    if (action == quick_menu::Action::power) { openPowerMenu(); invalidateGameOverlay(); return; }
    const auto target = overlay_target_;
    overlayDashboard();
    switch (action) {
    case quick_menu::Action::controllers: openDevices(devices::Panel::controllers); overlay_return_ = target; break;
    case quick_menu::Action::audio: openDevices(devices::Panel::audio); overlay_return_ = target; break;
    case quick_menu::Action::settings: openSettings(); overlay_return_ = target; break;
    default: break;
    }
}

// Back out of the screen Guide opened (to the home screen): the game it came from gets the foreground again.
void Launcher::pollOverlayReturn() {
    if (!overlay_return_) return;
    const auto target = *overlay_return_;
    // Guide opened again, the game is gone or another one starts: nothing to return to.
    if (gameOverlayOpen() || pending_launch_ || !game_sessions_.contains(target.key) || !overlay_backend_.valid(target)) { overlay_return_.reset(); return; }
    if (screen_ != ScreenMode::shelves || settingsOpen() || devicesOpen()) return; // Still in it (or in a page it opened).
    overlay_return_.reset();
    overlay_backend_.focus(target.window);
}

// The game behind Guide, never the selected dashboard card: its processes end, and Guide returns to the launcher.
void Launcher::closeOverlayGame() {
    if (!overlay_target_) return;
    const auto target = *overlay_target_;
    if (!overlay_backend_.valid(target)) { overlayDashboard(); return; }
    const auto game = trackedGame(target.key);
    if (gameKey(game) != target.key) {
        showNotification(NotificationKind::error, tr(L"Nie można zamknąć gry"), tr(L"Nie znaleziono procesu gry"));
        invalidateGameOverlay(); return;
    }
    closeGame(game);
    if (!game_sessions_.contains(target.key)) overlayDashboard();
    else invalidateGameOverlay(); // A denied close remains visible, including its notification.
}

// "Wstrzymaj": the game is suspended in RAM (quick resume) and Guide returns to the launcher; "Wróć" brings it back.
void Launcher::suspendOverlayGame() {
    if (!overlay_target_) return;
    const auto key = overlay_target_->key;
    if (freezeGame(key, true)) overlayDashboard();
    else invalidateGameOverlay(); // Denied: its notification stays visible over the game.
}

void Launcher::overlayKey(WPARAM key, bool repeated) {
    if (!gameOverlayOpen() || overlay_closing_) return;
    // Plain Tab may be a Steam Input mapping of Options. Widgets open only from the bar.
    if (key == VK_TAB) return;
    if (key == VK_HOME && !repeated) {
        dismissGameOverlay();
        return;
    }
    if (power_menu_) { powerMenuKey(key, repeated); invalidateGameOverlay(); return; }
    if (friendChatOpen()) { friendChatKey(key, repeated); invalidateGameOverlay(); return; } // Typing a message over the game.
    if (key == VK_ESCAPE || key == VK_BACK) {
        if (friends_open_) friendsBack();
        else { dismissGameOverlay(); return; }
    } else if ((key == VK_RETURN || key == VK_SPACE) && !repeated) {
        if (friends_open_) friendsAccept(); else overlayAccept();
    } else if (friends_open_) {
        if (key == VK_LEFT) dashboardMove(-1, 0);
        else if (key == VK_RIGHT) dashboardMove(1, 0);
        else if (key == VK_UP) friendsMove(-1);
        else if (key == VK_DOWN) friendsMove(1);
    } else if (key == VK_LEFT || key == VK_RIGHT) {
        const int next = std::clamp(overlay_selected_ + (key == VK_LEFT ? -1 : 1), 0,
            quick_menu::count(quick_menu::store_slots, true) - 1);
        if (next != overlay_selected_) { overlay_selected_ = next; audio_.navigate(); }
    }
    invalidateGameOverlay();
}

void Launcher::overlayClick(float x, float y) {
    if (!gameOverlayOpen() || overlay_closing_) return;
    const auto view = drawerViewport();
    x /= view.scale; y /= view.scale;
    if (power_menu_) {
        const auto layout = powerMenuGeometryFor(view.width, view.height);
        const int row = layout.hit(x, y);
        if (row >= 0) { power_menu_ = row + (hud_menu_ ? hud_menu_first_ : 0); powerMenuAccept(); } else closePowerMenu();
    } else if (friends_open_) {
        const float left = view.width - friends_layout::drawer_width;
        if (x < left) closeFriends();
        else if (plugin_drawer_active_) pluginPageClick(x - left, y);
        else if (!friends_list_) {
            const int tile = dashboardLayout().hit(left, x, y, dashboard_scroll_, view.height);
            if (tile != friends_layout::dashboard::no_hit) dashboardActivate(tile);
        } else if (friend_chat_.open) friendChatClick(x - left, y);
        else if (friends_layout::listBack(left).contains(x, y)) friendsBack();
        else if (y >= 104) {
            if (const auto friend_index = friends_layout::hit(friends_.friends, y + friends_scroll_)) {
                friends_cursor_ = *friend_index; friendsChat();
            }
        }
    } else {
        const int hit = quick_menu::hit(view.width, view.height, x, y, quick_menu::store_slots, true, overlay_motion_.bar);
        if (hit >= 0) { overlay_selected_ = hit; overlayAccept(); }
        else if (y < view.height - 160) dismissGameOverlay(); // The group spacer is not a button or an outside click.
    }
    invalidateGameOverlay();
}

LRESULT Launcher::overlayMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT: paint(true); return 0;
    case WM_CLOSE: dismissGameOverlay(); return 0;
    case WM_ACTIVATE:
        // The game may keep/reclaim foreground after Guide was shown by a pad.
        // Close for another app, not for a denied foreground request or a
        // transient null HWND while Windows is switching activation.
        if (LOWORD(wparam) == WA_INACTIVE && gameOverlayOpen() && !overlay_opening_ &&
            lparam && !overlayForeground(reinterpret_cast<HWND>(lparam))) closeGameOverlay(false);
        // DefWindowProc sets keyboard focus on activation. Swallowing this
        // message left mapped Tab/Options routed to the hidden dashboard.
        return DefWindowProcW(window, message, wparam, lparam);
    case WM_KEYDOWN:
        if (wparam == VK_RETURN && dashboardEditing() && dashboard_tile_ >= 0) {
            if (!(lparam & (1LL << 30))) dashboardPress(1, true, GetTickCount64());
            return 0;
        }
        overlayKeyboard(wparam, (lparam & (1LL << 30)) != 0, overlayChordModifiers());
        return 0;
    case WM_CHAR:
        if (friendChatOpen()) friendChatCharacter(static_cast<wchar_t>(wparam));
        return 0;
    case WM_KEYUP:
        if (wparam == VK_RETURN) dashboardPress(1, false, GetTickCount64());
        return 0;
    case WM_KILLFOCUS:
        if (dashboard_press_source_ == 1 || dashboard_press_source_ == 3) dashboardCancelPress();
        return 0;
    case WM_LBUTTONDOWN:
        dashboard_pointer_release_ = false;
        if (dashboardEditing() && !power_menu_) {
            const auto view = drawerViewport();
            const float x = static_cast<float>(GET_X_LPARAM(lparam)) / view.scale, y = static_cast<float>(GET_Y_LPARAM(lparam)) / view.scale;
            const int hit = dashboardLayout().hit(view.width - friends_layout::drawer_width, x, y, dashboard_scroll_, view.height);
            if (hit >= 0) dashboardPointerDown(x, y);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (dashboard_press_source_ == 3) {
            const auto view = drawerViewport();
            dashboardPointerMove(static_cast<float>(GET_X_LPARAM(lparam)) / view.scale, static_cast<float>(GET_Y_LPARAM(lparam)) / view.scale);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (dashboard_press_source_ == 3) dashboardCancelPress();
        return 0;
    case WM_LBUTTONUP:
        if (dashboard_pointer_release_) { dashboard_pointer_release_ = false; dashboardPress(3, false, GetTickCount64()); }
        else overlayClick(static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam)));
        return 0;
    case WM_SIZE:
        if (friends_open_) { dashboardEnsureVisible(); drawer_glide_.valid = false; }
        break;
    case WM_MOUSEWHEEL:
        if (friends_open_) { scrollFriends(GET_WHEEL_DELTA_WPARAM(wparam) < 0 ? 1 : -1); invalidateGameOverlay(); }
        return 0;
    case WM_DISPLAYCHANGE:
        closeGameOverlay(true); return 0;
    case WM_DPICHANGED:
        if (gameOverlayOpen() && game_overlay_) game_overlay_->show(monitorRect(overlay_target_->window), false);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

// The limit for one game: its own (0: none), else the global one. 0 also while the limiter is off.
int Launcher::gameFpsLimit(const std::string& game_key) const {
    if (!settings_.fps_limiter) return 0;
    const auto own = settings_.fps_game_limits.find(game_key);
    return own != settings_.fps_game_limits.end() ? own->second : settings_.fps_limit;
}

std::wstring Launcher::libraryGameName(const std::string& game_key) const {
    for (const auto* games : {&public_library_, &private_category_.games}) {
        const auto found = std::find_if(games->begin(), games->end(), [&](const Game& value) { return gameKey(value) == game_key; });
        if (found != games->end()) return found->name;
    }
    return std::wstring(game_key.begin(), game_key.end());
}

void Launcher::updateLimiter() {
    const auto& target = overlay_snapshot_.target;
#ifndef LAUNCHME_UI_TEST // The tests' windows have invented process IDs: nothing may be loaded into a real process.
    if (target) limiter_.apply(target->process.pid, gameFpsLimit(target->key), settings_.fps_limit_precise, static_cast<uint32_t>(settings_.fps_limit_mode));
#endif
    limiter_.heartbeat();
    // The page reports the state of the game in front: rebuilt when it moves on (loading -> active).
    const auto state = target ? limiter_.state(target->process.pid) : frame_limiter::State::off;
    if (state != limiter_seen_) {
        limiter_seen_ = state;
        if (settingsOpen() && settings_state_.at(settings_ui::Page::fps_limiter)) { buildSettingsRows(); InvalidateRect(hwnd_, nullptr, FALSE); }
    }
}

std::wstring Launcher::limiterStatus() const {
    using frame_limiter::State;
    if (!settings_.fps_limiter) return tr(L"Wyłączony.");
    const auto& target = overlay_snapshot_.target;
    if (!target) return tr(L"Brak uruchomionej gry z biblioteki. Limit zadziała po jej starcie.");
    const int limit = gameFpsLimit(target->key);
    const std::wstring name = libraryGameName(target->key);
    switch (limiter_.state(target->process.pid)) {
    case State::active: return name + L": " + (limit ? tr(L"limit ") + std::to_wstring(limit) + L" FPS" : std::wstring(tr(L"bez limitu")));
    case State::starting: return name + L": " + (limit ? tr(L"ładowanie…") : std::wstring(tr(L"bez limitu")));
    case State::no_dxgi: return name + tr(L": gra nie używa DXGI (Vulkan, OpenGL lub Direct3D 9), limit nie działa.");
    case State::unsupported: return name + tr(L": gra 32-bitowa albo brak biblioteki limitera.");
    case State::anticheat: return name + tr(L": gra ma anti-cheat, limiter jej nie rusza.");
    case State::denied: return name + tr(L": gra działa z wyższymi uprawnieniami niż launchme.");
    case State::failed: return name + tr(L": nie udało się załadować biblioteki.");
    default: return name + L": " + (limit ? tr(L"ładowanie…") : std::wstring(tr(L"bez limitu")));
    }
}

void Launcher::pollGameOverlay() {
    const auto now = GetTickCount64();
    if (now < next_overlay_poll_) return;
    next_overlay_poll_ = now + 250;
    auto snapshot = overlay_backend_.read ? overlay_backend_.read() : overlay_monitor_.snapshot();
    if (snapshot.revision != overlay_snapshot_.revision) {
        overlay_snapshot_ = std::move(snapshot);
        if (friends_open_) { InvalidateRect(hwnd_, nullptr, FALSE); invalidateGameOverlay(); }
    }
    if (gameOverlayOpen()) {
        const auto foreground = overlay_backend_.foreground();
        if (!overlay_backend_.valid(*overlay_target_) || (foreground && !overlayForeground(foreground))) closeGameOverlay(false);
    }
    updateGuideHotkey();
    updateLimiter();
}
