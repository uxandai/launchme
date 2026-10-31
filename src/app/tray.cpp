// Biblioteka → Uruchamianie → "Chowaj do zasobnika podczas gry" (AppSettings::hide_while_playing):
// while a game started here runs, the window is hidden (no taskbar button, no Alt+Tab entry) and an
// icon in the notification area stands in for it. The window comes back when the last game ends,
// from the icon (click: show; right click: Show / Close), a second instance or Guide (bringToFront).
#include "launcher.hpp"

#include <windowsx.h>

#include <algorithm>

namespace {
constexpr UINT tray_id = 1;
constexpr UINT_PTR tray_show = 1, tray_close = 2;
}

bool Launcher::addTrayIcon() {
    if (!hwnd_) return false;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data); data.hWnd = hwnd_; data.uID = tray_id;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    data.uCallbackMessage = tray_message;
    // The executable's own icon (resource 1, as the window class); shared, so never destroyed here.
    data.hIcon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!data.hIcon) data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    const auto tip = tray_game_.empty() ? std::wstring(L"launchme") : L"launchme · " + tray_game_;
    wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
    if (!tray_call_(NIM_ADD, &data)) return false;
    data.uVersion = NOTIFYICON_VERSION_4; // Clicks arrive as NIN_SELECT / WM_CONTEXTMENU with the anchor point.
    tray_call_(NIM_SETVERSION, &data);
    tray_icon_ = true;
    return true;
}

void Launcher::removeTrayIcon() {
    if (!tray_icon_) return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data); data.hWnd = hwnd_; data.uID = tray_id;
    tray_call_(NIM_DELETE, &data);
    tray_icon_ = false;
}

void Launcher::hideToTray(const std::wstring& game) {
    if (!settings_.hide_while_playing || in_tray_ || closing_ || !hwnd_) return;
    tray_game_ = game;
    if (!tray_icon_ && !addTrayIcon()) return; // No icon, no way back: the window stays where it is.
    in_tray_ = true;
    cancelEnterHold(); pointerUp(true);
    ShowWindow(hwnd_, SW_HIDE);
}

void Launcher::leaveTray(HWND behind) {
    removeTrayIcon();
    if (!in_tray_) return;
    in_tray_ = false;
    // In its own state (maximized, full screen, minimized) and without taking the foreground: the
    // caller decides that (bringToFront, returnFromGame). SW_SHOWNA puts it on top of every other
    // window, so the one that keeps the screen goes right in front of it (a topmost one is above it
    // anyway; placed after one, the launcher would become topmost too).
    if (!hwnd_ || IsWindowVisible(hwnd_)) return;
    if (!behind || behind == hwnd_ || (GetWindowLongPtrW(behind, GWL_EXSTYLE) & WS_EX_TOPMOST) ||
        !SetWindowPos(hwnd_, behind, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW))
        ShowWindow(hwnd_, SW_SHOWNA);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::pollTray() {
    if (!hwnd_ || closing_) return;
    if (in_tray_) {
        // The game's end has its own return (onGameActivity → returnFromGame); this catches the rest:
        // a game suspended from Guide, a start that failed, the setting turned off meanwhile.
        if (!settings_.hide_while_playing || (!anyActiveSession() && !pending_launch_)) leaveTray();
        return;
    }
    if (!settings_.hide_while_playing || pending_launch_ || gameOverlayOpen() || exit_prompt_) return;
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

void Launcher::trayIconEvent(WPARAM wparam, LPARAM lparam) {
    switch (LOWORD(lparam)) {
    case NIN_SELECT: case NIN_KEYSELECT: case WM_LBUTTONDBLCLK:
        bringToFront();
        break;
    case WM_CONTEXTMENU: {
        const HMENU menu = CreatePopupMenu();
        if (!menu) break;
        AppendMenuW(menu, MF_STRING, tray_show, tr(L"Pokaż launchme"));
        AppendMenuW(menu, MF_STRING, tray_close, tr(L"Zamknij launchme"));
        SetMenuDefaultItem(menu, static_cast<UINT>(tray_show), FALSE);
        // The menu closes on a click elsewhere only when its owner is the foreground window.
        SetForegroundWindow(hwnd_);
        const UINT command = static_cast<UINT>(TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                                                                GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam), hwnd_, nullptr));
        DestroyMenu(menu);
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        if (command == tray_show) bringToFront();
        else if (command == tray_close) { bringToFront(); PostMessageW(hwnd_, WM_CLOSE, 0, 0); }
        break;
    }
    default: break;
    }
}

void Launcher::taskbarCreated() {
    // Explorer restarted: its notification area forgot the icon.
    tray_icon_ = false;
    if (in_tray_ && !addTrayIcon()) leaveTray();
}
