// Desktop mode, the window: switching between the desktop window and Tryb TV, the window's style
// and its own top bar (main.cpp hands WM_NCCALCSIZE / WM_NCHITTEST here: the bar drags the window,
// maximize answers as HTMAXBUTTON so Windows 11 shows its snap layouts), the remembered position,
// the minimum size, files dropped onto it and the window buttons over the TV screens it shows.
#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../library/custom_games.hpp"
#include "../ui/custom_game_view.hpp"

#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cwctype>

using namespace desktop_ui;

namespace {
float dpiOf(HWND hwnd) {
    const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 0;
    return dpi ? static_cast<float>(dpi) / 96.0f : 1.0f;
}
}

float Launcher::windowDpi() const { return dpiOf(hwnd_); }

// ---- Mode ------------------------------------------------------------------------------------

void Launcher::setDesktopMode(bool desktop) {
    if (desktop == desktop_mode_ || closing_ || gameOverlayOpen()) return;
    // Every screen, menu and drawer closes the usual way first (Settings save, panels close their services).
    closeQuick(); closeCategoryMenu(); dismissMenu(); closeFriends(); closePowerMenu();
    cancelEnterHold(); cancelTabHold();
    if (keyboard_.open) closeKeyboard();
    if (desktop && tour_.open) { // Unfinished: it runs again the next time Tryb TV starts.
        tourClosePanels();
        tour_.open = false;
        tour_.hover_button = tour_.hover_tile = -1;
    }
    desktop_menu_.reset(); desktop_downloads_open_ = false; desktop_drag_ = {}; desktop_pressed_ = {}; desktop_hover_ = {};
    if (GetCapture() == hwnd_) ReleaseCapture();
    for (int i = 0; i < 8 && screen_ != ScreenMode::shelves; ++i) back();
    if (desktop_mode_) { desktopSaveState(); rememberDesktopWindow(); }
    desktop_mode_ = desktop;
    audio_.setDesktop(desktop);
    applyWindowMode();
    // The canvas switches now, not at the next paint: the first tick lays Tryb TV's tiles out on
    // the TV canvas, so they do not glide in from the desktop window's measures.
    renderer_.setDesktopViewport(desktop);
    if (desktop) {
        desktop_search_focus_ = false; desktop_zone_ = 1;
        buildDesktopSidebar();
    } else quick_progress_ = menu_progress_ = friends_progress_ = category_menu_progress_ = 0;
    showCategory(desktop ? category_ : 0);
    if (!desktop) entrance_lead_ = true; // Tryb TV opens with the full entrance (after the switch to its home).
    prefetchPosters();
    if (!desktop && !settings_.tour_done) startTour();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::applyWindowMode() {
    if (!hwnd_) return;
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    const bool visible = IsWindowVisible(hwnd_) != FALSE;
    const UINT show = visible ? SWP_SHOWWINDOW : 0;
    if (desktop_mode_) {
        const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW | (visible ? WS_VISIBLE : 0));
        const auto r = desktopWindowRect();
        SetWindowPos(hwnd_, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | show);
        if (visible && settings_.desktop_maximized) ShowWindow(hwnd_, SW_MAXIMIZE);
    } else {
        if (IsZoomed(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
        if (settings_.windowed) {
            const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_DEFAULT;
            DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
            SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW | (visible ? WS_VISIBLE : 0));
            const auto& r = previous_rect_;
            SetWindowPos(hwnd_, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | show);
            fullscreen_ = false;
        } else {
            const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_DONOTROUND;
            DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
            SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | (visible ? WS_VISIBLE : 0));
            MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor);
            const RECT& r = monitor.rcMonitor;
            SetWindowPos(hwnd_, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED | show);
            fullscreen_ = true;
        }
    }
    DragAcceptFiles(hwnd_, TRUE);
    renderer_.resize();
}

// The last restored position while it is still on a monitor, else a large window centred on the
// work area (about 1480 × 920 DIPs).
RECT Launcher::desktopWindowRect() const {
    const auto& saved = settings_.desktop_window;
    if (saved[2] > 0 && saved[3] > 0) {
        const RECT r{saved[0], saved[1], saved[0] + saved[2], saved[1] + saved[3]};
        // Kept only when its top bar (where it is dragged from) is on a monitor's work area: a
        // monitor that went away must not leave the window off screen or with only a sliver showing.
        MONITORINFO info{sizeof(info)};
        const RECT bar{r.left + 64, r.top, r.right - 64, r.top + 32};
        if (const HMONITOR monitor = MonitorFromRect(&bar, MONITOR_DEFAULTTONULL); monitor && GetMonitorInfoW(monitor, &info)) {
            RECT visible{};
            if (IntersectRect(&visible, &bar, &info.rcWork) && visible.bottom - visible.top >= 16 && visible.right - visible.left >= 160) {
                // Larger than this work area (another monitor, another scale): it fits again.
                const auto& work = info.rcWork;
                const LONG width = std::min(r.right - r.left, work.right - work.left);
                const LONG height = std::min(r.bottom - r.top, work.bottom - work.top);
                const LONG x = std::clamp(r.left, work.left, work.right - width);
                const LONG y = std::clamp(r.top, work.top, work.bottom - height);
                return {x, y, x + width, y + height};
            }
        }
    }
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &info);
    const auto& work = info.rcWork;
    const float dpi = windowDpi();
    const int w = std::min(static_cast<int>(std::lround(1480 * dpi)), static_cast<int>((work.right - work.left) * 92 / 100));
    const int h = std::min(static_cast<int>(std::lround(920 * dpi)), static_cast<int>((work.bottom - work.top) * 92 / 100));
    const int x = work.left + (work.right - work.left - w) / 2, y = work.top + (work.bottom - work.top - h) / 2;
    return {x, y, x + w, y + h};
}

void Launcher::rememberDesktopWindow() {
    if (!desktop_mode_ || !hwnd_ || !IsWindowVisible(hwnd_) || IsIconic(hwnd_)) return;
    const bool zoomed = IsZoomed(hwnd_) != FALSE;
    auto rect = settings_.desktop_window;
    if (!zoomed) {
        RECT window{};
        if (GetWindowRect(hwnd_, &window)) rect = {static_cast<int>(window.left), static_cast<int>(window.top), static_cast<int>(window.right - window.left), static_cast<int>(window.bottom - window.top)};
    }
    if (rect != settings_.desktop_window || zoomed != settings_.desktop_maximized) {
        settings_.desktop_window = rect;
        settings_.desktop_maximized = zoomed;
        settings_dirty_ = true; settings_save_failed_ = false; settings_edited_ = std::chrono::steady_clock::now();
    }
}

void Launcher::windowPlacementChanged() {
    if (!desktop_mode_) return;
    rememberDesktopWindow();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::windowActivated(bool active) {
    desktop_window_active_ = active;
    if (!active) { desktop_hover_ = {}; desktop_strip_hover_ = -1; desktop_max_hover_ = false; }
    if (desktop_mode_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::toggleMaximized() {
    ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
}

void Launcher::minimumWindowSize(MINMAXINFO& info) const {
    if (!desktop_mode_) return;
    const float dpi = windowDpi();
    // Never more than the monitor's work area (1366 × 768 at 125%, a 1080p laptop at 175%):
    // a minimum the screen cannot hold pushed the window past it.
    MONITORINFO monitor{sizeof(monitor)};
    LONG width = std::lround(1024 * dpi), height = std::lround(640 * dpi);
    if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor)) {
        width = std::min(width, (monitor.rcWork.right - monitor.rcWork.left) * 9 / 10);
        height = std::min(height, (monitor.rcWork.bottom - monitor.rcWork.top) * 9 / 10);
    }
    info.ptMinTrackSize.x = std::max<LONG>(info.ptMinTrackSize.x, width);
    info.ptMinTrackSize.y = std::max<LONG>(info.ptMinTrackSize.y, height);
}

// ---- The top bar as non-client area ------------------------------------------------------------

LRESULT Launcher::desktopNonClientHit(int x, int y) const {
    if (!desktop_mode_) return HTCLIENT;
    const float px = static_cast<float>(x), py = static_cast<float>(y);
    if (desktopActive()) {
        // Open menus, the list under Pobierania, dialogs and the drawer take every click.
        if (desktop_menu_ || desktop_downloads_open_ || topModal() != Modal::none || friends_open_ || keyboard_.open) return HTCLIENT;
        const float scale = desktopScale();
        const auto hit = desktopHit(px / scale, py / scale);
        if (hit.target == Target::caption) return HTCAPTION;
        if (hit.target == Target::maximize) return HTMAXBUTTON;
        return HTCLIENT;
    }
    // A desktop dialog: the window's own top bar stays above it.
    if (desktopSheet() || desktopEmbedded()) {
        const float scale = desktopScale();
        const auto layout = desktopLayout();
        const auto scaled = [&](Rect r) { return Rect{r.left * scale, r.top * scale, r.right * scale, r.bottom * scale}; };
        if (scaled(layout.windowButton(1)).contains(px, py)) return HTMAXBUTTON;
        if (scaled(layout.windowButton(0)).contains(px, py) || scaled(layout.windowButton(2)).contains(px, py)) return HTCLIENT;
        if (py < title_h * scale && topModal() == Modal::none && !friends_open_ && !keyboard_.open) return HTCAPTION;
        return HTCLIENT;
    }
    // A TV screen inside the window: its buttons, and a slim band above it that drags the window.
    RECT client{};
    GetClientRect(hwnd_, &client);
    const float dpi = windowDpi();
    if (stripButton(1, static_cast<float>(client.right), dpi).contains(px, py)) return HTMAXBUTTON;
    if (stripButton(0, static_cast<float>(client.right), dpi).contains(px, py) || stripButton(2, static_cast<float>(client.right), dpi).contains(px, py)) return HTCLIENT;
    if (py < strip_caption * dpi && topModal() == Modal::none && !friends_open_ && !keyboard_.open) return HTCAPTION;
    return HTCLIENT;
}

void Launcher::desktopMaximizeHover(bool hover) {
    if (desktop_max_hover_ == hover) return;
    desktop_max_hover_ = hover;
    if (!hover) desktop_max_pressed_ = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopMaximizePress(bool down) {
    if (down) { desktop_max_pressed_ = true; InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (!desktop_max_pressed_) return;
    desktop_max_pressed_ = false;
    toggleMaximized();
}

int Launcher::windowStripHit(float mouse_x, float mouse_y) const {
    if (!desktop_mode_ || desktopActive()) return -1;
    if (desktopSheet() || desktopEmbedded()) { // The dialog's top bar: its window buttons in desktop DIPs.
        const float scale = desktopScale();
        const auto layout = desktopLayout();
        for (int i = 0; i < 3; ++i) {
            const auto r = layout.windowButton(i);
            if (Rect{r.left * scale, r.top * scale, r.right * scale, r.bottom * scale}.contains(mouse_x, mouse_y)) return i;
        }
        return -1;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    const float dpi = windowDpi();
    for (int i = 0; i < 3; ++i) if (stripButton(i, static_cast<float>(client.right), dpi).contains(mouse_x, mouse_y)) return i;
    return -1;
}

bool Launcher::windowStripClick(float mouse_x, float mouse_y) {
    const int button = windowStripHit(mouse_x, mouse_y);
    if (button < 0) return false;
    if (button == 0) ShowWindow(hwnd_, SW_MINIMIZE);
    else if (button == 1) toggleMaximized();
    else PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    return true;
}

// ---- Files dropped on the window ---------------------------------------------------------------

// A program, shortcut, script or Internet shortcut dropped on the window: "Dodaj grę" with it filled in.
void Launcher::dropFiles(HDROP drop) {
    std::filesystem::path picked;
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count && picked.empty(); ++i) {
        const UINT length = DragQueryFileW(drop, i, nullptr, 0);
        if (!length || length > 32767) continue;
        std::wstring path(length + 1, L'\0');
        if (!DragQueryFileW(drop, i, path.data(), length + 1)) continue;
        path.resize(length);
        // A program or shortcut (PC game), a disc image or ROM (emulated game), or a game's folder.
        if (addableFile(path)) picked = path;
    }
    DragFinish(drop);
    if (closing_ || pending_launch_ || launchAnimationActive() || topModal() != Modal::none) return;
    if (picked.empty()) {
        if (count) showNotification(NotificationKind::error, tr(L"Nie można dodać tego pliku"),
                                    tr(L"Upuść program (.exe, .lnk, .url, .bat), obraz płyty lub ROM albo folder gry."));
        return;
    }
    SetForegroundWindow(hwnd_);
    closeFriends(); desktopCloseMenu(); desktop_downloads_open_ = false;
    openCustomEditor({});
    if (!custom_editor_.open) return;
    // What it is decides the game: PC or emulated with its console; a folder may also be a ROM folder
    // or a folder of games for DRM-free (add_game.cpp).
    std::error_code error;
    if (std::filesystem::is_directory(picked, error)) addFolder(picked);
    else customEditorApplyFile(picked);
    if (!custom_editor_.open) return; // A ROM folder or DRM-free's new folder: nothing to edit.
    auto& ui = custom_editor_;
    ui.focus = custom_game_ui::Item::name;
    ui.caret = ui.fields[custom_game_ui::name_field].size();
    ui.selected = true; // Typing replaces the guessed name.
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The page's backdrop: the library drawn once more, as it is, and kept (Renderer::captureDesktopBackdrop)
// before the page replaces it; nothing is copied while the library is merely in use.
void Launcher::desktopLeaveLibrary() {
    if (!desktopActive() || !hwnd_ || closing_ || !IsWindowVisible(hwnd_) || IsIconic(hwnd_)) return;
    renderer_.armDesktopBackdrop();
    frame_painting_ = true; // Drawn now, not folded into the next frame (deferPaint).
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    frame_painting_ = false;
}
