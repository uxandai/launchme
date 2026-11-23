// The notification-area icon. Two settings in System put the window there:
// - "Chowaj do zasobnika podczas gry" (AppSettings::hide_while_playing): while a game started here runs,
//   the window is hidden (no taskbar button, no Alt+Tab entry) and the icon stands in for it. The window
//   comes back when the last game ends.
// - "Zamknięcie okna chowa do zasobnika" (AppSettings::close_to_tray): the icon stays while the launcher
//   runs; the window's ✕, Alt+F4 and the taskbar's Close hide the window instead of ending the launcher,
//   and it waits there whatever the games do. Zamknij launchme (the icon's menu, the power menu) quits.
//   Started with the system (--minimized), the desktop window starts there.
// Either way the icon brings the window back (click: show; right click: the menu of tray_menu.hpp), so
// do a second instance and Guide (bringToFront). Linux tray hosts show the menu themselves: it is sent
// to them (Shell_NotifyIconMenu, src/linux/window/shell.cpp) whenever it changes.
#include "launcher.hpp"

#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr UINT tray_id = 1;
constexpr uint64_t tray_refresh_ms = 1000;

// The Win32 menu of the entries (on Linux the shim's, which goes to the tray host).
HMENU menuHandle(const std::vector<tray_menu::Entry>& entries) {
    const HMENU menu = CreatePopupMenu();
    if (!menu) return nullptr;
    for (const auto& entry : entries) {
        std::wstring label = entry.label;
#ifdef _WIN32
        // A single ampersand would underline the next letter ("Ratchet & Clank").
        for (size_t at = label.find(L'&'); at != std::wstring::npos; at = label.find(L'&', at + 2)) label.insert(at, 1, L'&');
#endif
        if (entry.separator()) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        else if (!entry.children.empty()) {
            const HMENU submenu = menuHandle(entry.children);
            if (submenu && !AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), label.c_str())) DestroyMenu(submenu);
        } else AppendMenuW(menu, MF_STRING, entry.command, label.c_str());
        if (entry.bold) SetMenuDefaultItem(menu, entry.command, FALSE);
    }
    return menu;
}

NOTIFYICONDATAW iconData(HWND window) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data); data.hWnd = window; data.uID = tray_id;
    return data;
}
} // namespace

bool Launcher::addTrayIcon() {
    if (!hwnd_) return false;
    auto data = iconData(hwnd_);
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = tray_message;
    // The executable's own icon (resource 1, as the window class); shared, so never destroyed here.
    data.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!data.hIcon) data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    tray_tip_ = trayTip();
    wcsncpy_s(data.szTip, tray_tip_.c_str(), _TRUNCATE);
    if (!tray_call_(NIM_ADD, &data)) { tray_unavailable_ = true; return false; }
    tray_unavailable_ = false;
    data.uVersion = NOTIFYICON_VERSION_4; // Clicks arrive as NIN_SELECT / WM_CONTEXTMENU with the anchor point.
    tray_call_(NIM_SETVERSION, &data);
    tray_icon_ = true;
    tray_menu_shown_.clear(); tray_refresh_at_ = 0; // The new icon's menu (Linux) at the next refresh.
    return true;
}

void Launcher::removeTrayIcon() {
    if (!tray_icon_) return;
    auto data = iconData(hwnd_);
    tray_call_(NIM_DELETE, &data);
    tray_icon_ = false;
}

void Launcher::hideToTray(const std::wstring& game) {
    if (!settings_.hide_while_playing || guideOverlayOn() || in_tray_ || closing_ || !hwnd_) return;
    tray_game_ = game;
    if (!tray_icon_ && !addTrayIcon()) return; // No icon, no way back: the window stays where it is.
    in_tray_ = true;
    cancelEnterHold(); pointerUp(true);
    ShowWindow(hwnd_, SW_HIDE);
}

bool Launcher::closeToTray() {
    if (!settings_.close_to_tray || !hwnd_ || closing_) return false;
    if (!tray_icon_ && !addTrayIcon()) return false; // No notification area: it closes, or it could never come back.
    const bool hint = !settings_.close_to_tray_told;
    tray_closed_ = true;
    if (in_tray_) return true; // Already hidden behind a game: it stays when the game ends.
    in_tray_ = true;
    cancelEnterHold(); pointerUp(true);
    audio_.stopMusic();
    ShowWindow(hwnd_, SW_HIDE);
    if (hint) {
        // Once: where the launcher went, so that a closed window that keeps running is no surprise.
        settings_.close_to_tray_told = true;
        settingsChanged();
        trayBalloon(tr(L"launchme działa w zasobniku"), tr(L"Kliknij ikonę, żeby wrócić. Zamknij launchme: prawy klik na ikonie."));
    }
    return true;
}

bool Launcher::startInTray() {
    if (!settings_.close_to_tray || !desktop_mode_ || !hwnd_ || in_tray_) return false;
    if (!tray_icon_ && !addTrayIcon()) return false;
    in_tray_ = tray_closed_ = tray_never_shown_ = true;
    audio_.stopMusic();
    return true;
}

void Launcher::leaveTray(HWND behind) {
    if (!settings_.close_to_tray) removeTrayIcon(); // Otherwise the icon stays.
    const bool closed = std::exchange(tray_closed_, false);
    if (!in_tray_) return;
    in_tray_ = false;
    if (closed && !anyActiveSession() && !pending_launch_) audio_.resumeMusic();
    if (!hwnd_ || IsWindowVisible(hwnd_)) return;
    if (std::exchange(tray_never_shown_, false)) {
        // Started in the tray: the first show in the window's own state (maximized); the foreground is the caller's.
        ShowWindow(hwnd_, startMaximized() ? SW_SHOWMAXIMIZED : SW_SHOWNOACTIVATE);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    // In its own state (maximized, full screen, minimized) and without taking the foreground: the
    // caller decides that (bringToFront, returnFromGame). SW_SHOWNA puts it on top of every other
    // window, so the one that keeps the screen goes right in front of it (a topmost one is above it
    // anyway; placed after one, the launcher would become topmost too).
    if (!behind || behind == hwnd_ || (GetWindowLongPtrW(behind, GWL_EXSTYLE) & WS_EX_TOPMOST) ||
        !SetWindowPos(hwnd_, behind, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW))
        ShowWindow(hwnd_, SW_SHOWNA);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::pollTray() {
    if (!hwnd_ || closing_) return;
    // Zamknięcie okna chowa do zasobnika: the icon is there as long as the setting is on.
    if (settings_.close_to_tray && !tray_icon_ && !tray_unavailable_) addTrayIcon();
    else if (!settings_.close_to_tray && tray_icon_ && !in_tray_) removeTrayIcon();
    if (tray_icon_) refreshTray();
    if (tray_closed_) return; // Closed into the tray: it waits there, whatever the games do.
    if (in_tray_) {
        // The game's end has its own return (onGameActivity → returnFromGame); this catches the rest:
        // a game suspended from Guide, a start that failed, the setting turned off meanwhile.
        if (!settings_.hide_while_playing || guideOverlayOn() || (!anyActiveSession() && !pending_launch_)) leaveTray();
        return;
    }
    // With Guide over games the launcher stays a normal window: Library/Settings from the Guide must
    // land in a live launcher, not in a hidden or throttled one.
    if (!settings_.hide_while_playing || guideOverlayOn() || pending_launch_ || gameOverlayOpen() || exit_prompt_) return;
    // A game started here, confirmed running: once it (or anything else) holds the foreground, so the
    // launcher's hand-off of the focus to the game window is over.
    const auto playing = std::find_if(launched_here_.begin(), launched_here_.end(), [&](const std::string& key) {
        const auto session = game_sessions_.find(key);
        return session != game_sessions_.end() && session->second.running() && !session->second.frozen();
    });
    if (playing == launched_here_.end()) return;
    const HWND front = foreground_window_ ? foreground_window_() : GetForegroundWindow();
    if (front == hwnd_ || (browser_ && front && browser_->owns(front))) return;
    std::wstring name;
    {
        std::lock_guard lock(game_activity_jobs_->mutex);
        if (const auto tracked = game_activity_jobs_->active.find(*playing); tracked != game_activity_jobs_->active.end())
            name = tracked->second.game.name;
    }
    hideToTray(name);
}

std::wstring Launcher::trayTip() const {
    const auto game = trayPlayingGame();
    const auto name = game ? game->name : in_tray_ ? tray_game_ : std::wstring{};
    return name.empty() ? std::wstring(L"launchme") : L"launchme · " + name;
}

void Launcher::refreshTray() {
    const auto now = GetTickCount64();
    if (now < tray_refresh_at_) return;
    tray_refresh_at_ = now + tray_refresh_ms;
    if (const auto tip = trayTip(); tip != tray_tip_) {
        auto data = iconData(hwnd_);
        data.uFlags = NIF_TIP | NIF_SHOWTIP;
        wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
        if (tray_call_(NIM_MODIFY, &data)) tray_tip_ = tip;
    }
#ifndef _WIN32
    // The host shows the menu by itself: it gets the new one when anything in it changed.
    auto menu = trayMenu();
    if (menu == tray_menu_shown_) return;
    if (const HMENU handle = menuHandle(menu)) {
        if (Shell_NotifyIconMenu(handle)) tray_menu_shown_ = std::move(menu);
        DestroyMenu(handle);
    }
#endif
}

void Launcher::trayBalloon(const std::wstring& title, const std::wstring& text) {
    if (!tray_icon_) return;
    auto data = iconData(hwnd_);
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_INFO;
    wcsncpy_s(data.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(data.szInfo, text.c_str(), _TRUNCATE);
    tray_call_(NIM_MODIFY, &data);
}

std::vector<Game> Launcher::trayRecentGames() const {
    // As the taskbar's Jump List: public games, last played first, without those taken off Ostatnio grane.
    std::vector<const Game*> played;
    for (const auto& game : grouped_library_)
        if (isGame(game) && game.last_played && game.visibility == GameVisibility::public_game && !recentHidden(game)) played.push_back(&game);
    const size_t count = std::min<size_t>(tray_menu::recent_limit, played.size());
    std::partial_sort(played.begin(), played.begin() + static_cast<std::ptrdiff_t>(count), played.end(),
                      [](const Game* a, const Game* b) { return a->last_played > b->last_played; });
    std::vector<Game> recent;
    for (size_t i = 0; i < count; ++i) recent.push_back(*played[i]);
    return recent;
}

std::optional<Game> Launcher::trayPlayingGame() const {
    for (const auto& [key, session] : game_sessions_) {
        if (!session.running() || session.frozen()) continue;
        auto game = trackedGame(key);
        if (!game.name.empty()) return game;
    }
    return std::nullopt;
}

std::optional<std::wstring> Launcher::trayDownloads() const {
    std::vector<downloads_ui::Row> rows;
    collectDownloads(rows);
    bool moving = false;
    float sum = 0;
    int known = 0;
    for (const auto& row : rows) {
        if (!row.moving) continue;
        moving = true;
        if (row.fraction) { sum += std::clamp(*row.fraction, 0.0f, 1.0f); ++known; }
    }
    if (!moving) return std::nullopt;
    std::wstring label = tr(L"Pobierania");
    if (known) label += L" (" + std::to_wstring(std::lround(sum / static_cast<float>(known) * 100.0f)) + L"%)";
    return label;
}

std::vector<tray_menu::Entry> Launcher::trayMenu() {
    using tray_menu::Entry;
    std::vector<Entry> menu;
    menu.push_back({tray_menu::show, tr(L"Pokaż launchme"), true, {}});
    tray_recent_ = trayRecentGames();
    if (!tray_recent_.empty()) {
        Entry recent{tray_menu::none, tr(L"Ostatnio grane"), false, {}};
        for (size_t i = 0; i < tray_recent_.size(); ++i)
            recent.children.push_back({tray_menu::recent_first + static_cast<unsigned>(i), tray_menu::label(tray_recent_[i].name), false, {}});
        menu.push_back(std::move(recent));
    }
    menu.push_back({tray_menu::switch_mode, desktop_mode_ ? tr(L"Tryb TV") : tr(L"Okno desktopowe"), false, {}});
    if (auto downloads = trayDownloads()) menu.push_back({tray_menu::downloads, std::move(*downloads), false, {}});
    tray_playing_ = trayPlayingGame();
    if (tray_playing_) menu.push_back({tray_menu::end_game, std::wstring(tr(L"Zamknij grę")) + L": " + tray_menu::label(tray_playing_->name), false, {}});
    menu.push_back({tray_menu::settings, tr(L"Ustawienia"), false, {}});
    menu.push_back({});
    menu.push_back({tray_menu::close, tr(L"Zamknij launchme"), false, {}});
    return menu;
}

void Launcher::trayCommand(unsigned command) {
    if (closing_ || !hwnd_) return;
    switch (command) {
    case tray_menu::show: bringToFront(); return;
    case tray_menu::switch_mode: {
        const bool desktop = !desktop_mode_;
        bringToFront();
        setDesktopMode(desktop);
        return;
    }
    case tray_menu::downloads: bringToFront(); openDownloads(); return;
    case tray_menu::settings: bringToFront(); if (!settingsOpen()) openSettings(); return;
    case tray_menu::end_game:
        if (tray_playing_) { const auto game = *tray_playing_; closeGame(game); }
        return;
    case tray_menu::close:
        // Quits even with "Zamknięcie okna chowa do zasobnika"; suspended games still ask first (requestClose).
        quit_requested_ = true;
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return;
    default: break;
    }
    if (command < tray_menu::recent_first || command - tray_menu::recent_first >= tray_recent_.size()) return;
    // Ostatnio grane: the game starts and the window stays where it is. Nothing starting (an install,
    // a question, a missing emulator, an error): the window comes forward with the reason.
    const auto game = tray_recent_[command - tray_menu::recent_first];
    launchGame(game);
    if (!pending_launch_ && !launch_script_wait_ && !anyActiveSession()) bringToFront();
}

void Launcher::trayIconEvent(WPARAM wparam, LPARAM lparam) {
    switch (LOWORD(lparam)) {
    case NIN_SELECT: case NIN_KEYSELECT: case WM_LBUTTONDBLCLK:
        bringToFront();
        break;
    case WM_COMMAND: // Linux: an entry of the menu the tray host showed.
        trayCommand(static_cast<unsigned>(wparam));
        break;
    case WM_CONTEXTMENU: {
        const HMENU menu = menuHandle(trayMenu());
        if (!menu) break;
        // The menu closes on a click elsewhere only when its owner is the foreground window.
        SetForegroundWindow(hwnd_);
        const UINT command = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                                                                GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam), hwnd_, nullptr));
        DestroyMenu(menu);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        if (command) trayCommand(command);
        break;
    }
    default: break;
    }
}

void Launcher::taskbarCreated() {
    // Explorer restarted: its notification area forgot the icon (and may be there again for one that failed).
    tray_icon_ = false;
    tray_unavailable_ = false;
    if ((in_tray_ || settings_.close_to_tray) && !addTrayIcon() && in_tray_) leaveTray();
}
