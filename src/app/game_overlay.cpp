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
        if (guide || settings_.game_fps) overlay_monitor_.start(work_);
    }
    overlay_monitor_.configure(guide || settings_.game_fps, settings_.game_fps, overlay_target_ ? overlay_target_->window : nullptr);
    if (game_overlay_) game_overlay_->appearance(settings_);
    dashboard_tile_ = std::min(dashboard_tile_, dashboardTileCount() - 1);
    next_overlay_poll_ = 0;
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
    // Shift+Tab is ordinary backwards navigation outside a game. Do not reserve
    // it on the desktop, in other applications, or in Launchme's own settings.
    const bool wanted = runtime_started_ && guideHotkeyWanted();
    if (!wanted) {
        if (guide_hotkey_active_) UnregisterHotKey(hwnd_, guide_hotkey);
        guide_hotkey_active_ = false; next_guide_hotkey_retry_ = 0;
    } else if (!guide_hotkey_active_ && GetTickCount64() >= next_guide_hotkey_retry_) {
        guide_hotkey_active_ = RegisterHotKey(hwnd_, guide_hotkey, MOD_SHIFT | MOD_NOREPEAT, VK_TAB) != FALSE;
        next_guide_hotkey_retry_ = GetTickCount64() + 1000;
    }
}

void Launcher::guideKeyboardHotkey() {
    if (!guideHotkeyWanted()) { updateGuideHotkey(); return; } // Ignore a queued hotkey after Alt+Tab.
    if (gameOverlayOpen()) dismissGameOverlay(); // The keyboard chord always closes the entire overlay.
    else toggleGameOverlay();
}

void Launcher::overlayKeyboard(WPARAM key, bool repeated, bool shift_tab) {
    if (key == VK_TAB && shift_tab) {
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
        context.fps_widget = settings_.game_fps; context.fps_low = settings_.game_fps_low;
        context.game_fps = overlay_snapshot_.fps;
        context.dashboard.sensors = sensors_;
        context.hud_pins = settings_.hud_pins;
        hud_window_->renderer().paintHud(context);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void Launcher::updateHud() {
    // Over the game in front (Guide closed): the pinned widgets in a corner. Never interactive.
    const auto target = overlay_snapshot_.target;
    const bool want = runtime_started_ && guideOverlayOn() && !settings_.hud_pins.empty() && !gameOverlayOpen() && target &&
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
    const bool sensors = std::find(pins.begin(), pins.end(), "cpu") != pins.end() || std::find(pins.begin(), pins.end(), "gpu") != pins.end();
    if (sensors && !hud_polling_) { hud_polling_ = true; startDashboardPoll(); }
    else if (!sensors && hud_polling_) { hud_polling_ = false; if (!friends_open_) stopDashboardPoll(); }
    std::wstring signature;
    for (const auto& pin : pins) signature += std::wstring(pin.begin(), pin.end()) + L',';
    signature += std::to_wstring(static_cast<int>(overlay_snapshot_.fps.status)) + L',' +
        std::to_wstring(static_cast<int>(overlay_snapshot_.fps.fps + 0.5)) + L',' + std::to_wstring(static_cast<int>(overlay_snapshot_.fps.low + 0.5)) + L',' +
        std::to_wstring(sensors_.cpu.value_or(-1)) + L',' + std::to_wstring(sensors_.gpu.value_or(-1));
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
    const float x = quick_menu::center(drawerViewport().width, overlay_selected_, static_cast<int>(plugin_menu_.size()), true);
    const bool moved = overlay_motion_.step(seconds, overlay_closing_, friends_open_, x);
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
    overlay_close_confirm_.reset();
    closeFriends(); closePowerMenu();
    invalidateGameOverlay();
}

void Launcher::toggleGameOverlay() {
    if (!settings_.guide_overlay || !settings_.guide_menu) return;
    if (gameOverlayOpen()) { overlayKey(VK_HOME); return; }
    if (overlay_backend_.foreground() == hwnd_) { openGuideMenu(); return; }
    const auto snapshot = overlay_backend_.read ? overlay_backend_.read() : overlay_monitor_.snapshot();
    if (snapshot.target && overlay_backend_.foreground() == snapshot.target->window) openGameOverlay(*snapshot.target);
}

void Launcher::openOverlayDrawer() {
    friends_open_ = true; friends_list_ = false; plugin_drawer_active_ = false;
    dashboard_tile_ = 0; drawer_glide_.valid = false; clock_check_at_ = 0;
    dashboard_scroll_ = 0;
    if (const auto layout = dashboardLayout(); !layout.items.empty()) dashboard_tile_ = layout.items.front().source;
    if (runtime_started_) { loadFriends(); startDashboardPoll(); }
    audio_.drawer();
}

bool Launcher::openGameOverlay(const GameWindow& target) {
    if (!settings_.guide_overlay || !settings_.guide_menu || !overlay_backend_.valid(target)) return false;
    cancelGameFocus();
    if (!game_overlay_) {
        auto overlay = std::make_unique<OverlayWindow>();
        if (!overlay->create(false, [this](HWND w, UINT m, WPARAM p, LPARAM l) { return overlayMessage(w, m, p, l); })) return false;
        game_overlay_ = std::move(overlay);
        game_overlay_->appearance(settings_);
    }
    closeFriends(); friends_progress_ = 0;
    overlay_close_confirm_.reset();
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
    overlay_motion_.open(quick_menu::center(drawerViewport().width, 0, static_cast<int>(plugin_menu_.size()), true));
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
    if (settings_.guide_open_widgets) { openOverlayDrawer(); invalidateGameOverlay(); }
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
    overlay_close_confirm_.reset();
    closeFriends(); friends_progress_ = 0;
    closePowerMenu();
    // Transfer while our overlay still owns foreground permission. Hiding it
    // first can give foreground to another process and make this request fail.
    if (return_to_game && overlay_backend_.valid(target)) overlay_backend_.focus(target.window);
    if (game_overlay_) game_overlay_->hide();
    pad_start_.swallow(false); controller_direction_ = 0;
    overlay_monitor_.configure((settings_.guide_overlay && settings_.guide_menu) || settings_.game_fps, settings_.game_fps);
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
    if (settingsOpen()) saveSettings();
    closeQuick(); quick_progress_ = 0; closeCategoryMenu(); dismissMenu();
    showCategory(0);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::overlayAccept() {
    if (!gameOverlayOpen()) return;
    const auto action = quick_menu::action(overlay_selected_, static_cast<int>(plugin_menu_.size()), true);
    const int plugin = quick_menu::pluginIndex(overlay_selected_, true);
    if (action == quick_menu::Action::widgets) { openOverlayDrawer(); invalidateGameOverlay(); return; }
    if (action == quick_menu::Action::close_game) {
        overlay_close_confirm_ = 1; // Cancel is the initial action, even for a held A button.
        audio_.context(); invalidateGameOverlay(); return;
    }
    if (action == quick_menu::Action::power) { openPowerMenu(); invalidateGameOverlay(); return; }
    overlayDashboard();
    switch (action) {
    case quick_menu::Action::search: openSearch(); break;
    case quick_menu::Action::library: openLibrary(); break;
    case quick_menu::Action::store:
        if (plugin >= 0 && plugin < static_cast<int>(plugin_menu_.size())) pluginAction(plugin_menu_[plugin]);
        break;
    case quick_menu::Action::controllers: openDevices(devices::Panel::controllers); break;
    case quick_menu::Action::audio: openDevices(devices::Panel::audio); break;
    case quick_menu::Action::settings: openSettings(); break;
    case quick_menu::Action::downloads: openDownloads(); break;
    default: break;
    }
}

void Launcher::confirmOverlayClose() {
    if (!overlay_target_ || !overlay_close_confirm_) return;
    const bool close = *overlay_close_confirm_ == 0;
    overlay_close_confirm_.reset();
    if (!close) { invalidateGameOverlay(); return; }
    const auto target = *overlay_target_; // The game behind Guide, never the selected dashboard card.
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

void Launcher::overlayKey(WPARAM key, bool repeated) {
    if (!gameOverlayOpen() || overlay_closing_) return;
    // Plain Tab may be a Steam Input mapping of Options. Widgets open only from the bar.
    if (key == VK_TAB) return;
    if (overlay_close_confirm_) {
        if (key == VK_ESCAPE || key == VK_BACK || key == VK_HOME) overlay_close_confirm_.reset();
        else if ((key == VK_RETURN || key == VK_SPACE) && !repeated) confirmOverlayClose();
        else if (key == VK_LEFT || key == VK_UP) overlay_close_confirm_ = 0;
        else if (key == VK_RIGHT || key == VK_DOWN) overlay_close_confirm_ = 1;
        invalidateGameOverlay(); return;
    }
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
            quick_menu::count(static_cast<int>(plugin_menu_.size()), true) - 1);
        if (next != overlay_selected_) { overlay_selected_ = next; audio_.navigate(); }
    }
    invalidateGameOverlay();
}

void Launcher::overlayClick(float x, float y) {
    if (!gameOverlayOpen() || overlay_closing_) return;
    const auto view = drawerViewport();
    x /= view.scale; y /= view.scale;
    if (overlay_close_confirm_) {
        const settings_ui::Layout layout{view.width, view.height};
        const int row = layout.hitChoice(x, y, 2);
        if (row >= 0) { overlay_close_confirm_ = row; confirmOverlayClose(); }
        else if (!layout.picker(2).contains(x, y)) overlay_close_confirm_.reset();
    } else if (power_menu_) {
        const power_menu::Geometry layout{view.width, view.height, power_menu::gaps(power_items_)};
        const int row = layout.hit(x, y);
        if (row >= 0) { power_menu_ = row; powerMenuAccept(); } else closePowerMenu();
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
        const int hit = quick_menu::hit(view.width, view.height, x, y, static_cast<int>(plugin_menu_.size()), true, overlay_motion_.bar);
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
        overlayKeyboard(wparam, (lparam & (1LL << 30)) != 0,
            (GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000));
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
        if (dashboardEditing() && !power_menu_ && !overlay_close_confirm_) {
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
}
