// The window manager calls (user32) on SDL3: classes, windows, the message queue, timers,
// painting and presentation, placement, focus, capture and monitors. Input translation is in
// events.cpp; the clipboard, cursor, shell and dialogs in shell.cpp.
#include "window_internal.hpp"
#include "gpu.hpp"
#include "../../platform/text_encoding.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>

namespace lm_window::detail {
State& state() { static State instance; return instance; }

uint64_t now() { return SDL_GetTicks(); }

Window* window(HWND handle) {
    auto& s = state();
    std::lock_guard lock(s.windows_mutex);
    auto* target = static_cast<Window*>(handle);
    return target && s.live.contains(target) && !target->destroyed ? target : nullptr;
}

LRESULT send(Window* target, UINT message, WPARAM wparam, LPARAM lparam) {
    if (!target || !target->procedure) return 0;
    return target->procedure(static_cast<HWND>(target), message, wparam, lparam);
}

void pushInput(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
    MSG message_{handle, message, wparam, lparam, static_cast<DWORD>(now()), {}};
    // LAUNCHME_TRACE_INPUT=1: every translated input message on stderr (diagnosing a desktop's input).
    static const bool trace = [] { const char* value = std::getenv("LAUNCHME_TRACE_INPUT"); return value && *value == '1'; }();
    if (trace && message != WM_MOUSEMOVE && message != WM_SETCURSOR && message != WM_NCMOUSEMOVE)
        std::fprintf(stderr, "launchme input: %p msg 0x%04x wparam 0x%llx lparam 0x%llx\n", handle, message,
                     static_cast<unsigned long long>(wparam), static_cast<unsigned long long>(lparam));
    state().input.push_back(message_);
}

float pixelDensity(Window* target) {
    const float density = target && target->sdl ? SDL_GetWindowPixelDensity(target->sdl) : 0.0f;
    return density > 0 ? density : screenDensity();
}

float screenDensity() {
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    const float scale = primary ? SDL_GetDisplayContentScale(primary) : 1.0f;
    // Wayland reports pixels per coordinate through the window; X11 has one coordinate space.
    return scale > 0 && SDL_strcmp(SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "", "wayland") == 0 ? scale : 1.0f;
}

namespace {
RECT displayRect(SDL_DisplayID display, bool usable) {
    SDL_Rect bounds{};
    if (!(usable ? SDL_GetDisplayUsableBounds(display, &bounds) : SDL_GetDisplayBounds(display, &bounds))) return {};
    const float d = screenDensity();
    return {static_cast<LONG>(std::lround(bounds.x * d)), static_cast<LONG>(std::lround(bounds.y * d)),
            static_cast<LONG>(std::lround((bounds.x + bounds.w) * d)), static_cast<LONG>(std::lround((bounds.y + bounds.h) * d))};
}

// The display a screen rectangle fills exactly (a full-screen window's place), else 0.
SDL_DisplayID displayFilledBy(const RECT& rect) {
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    SDL_DisplayID found = 0;
    for (int i = 0; displays && i < count && !found; ++i) {
        const RECT bounds = displayRect(displays[i], false);
        if (bounds.left == rect.left && bounds.top == rect.top && bounds.right == rect.right && bounds.bottom == rect.bottom) found = displays[i];
    }
    SDL_free(displays);
    return found;
}

SDL_HitTestResult hitTest(SDL_Window* sdl, const SDL_Point* area, void*) {
    Window* target = nullptr;
    {
        auto& s = state();
        std::lock_guard lock(s.windows_mutex);
        if (const auto found = s.by_id.find(SDL_GetWindowID(sdl)); found != s.by_id.end()) target = found->second;
    }
    if (!target || !target->custom_frame) return SDL_HITTEST_NORMAL;
    POINT point{static_cast<LONG>(std::lround(area->x * pixelDensity(target))), static_cast<LONG>(std::lround(area->y * pixelDensity(target)))};
    ClientToScreen(static_cast<HWND>(target), &point);
    switch (static_cast<int>(send(target, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)))) {
    case HTCAPTION: return SDL_HITTEST_DRAGGABLE;
    case HTTOP: return SDL_HITTEST_RESIZE_TOP;
    case HTTOPLEFT: return SDL_HITTEST_RESIZE_TOPLEFT;
    case HTTOPRIGHT: return SDL_HITTEST_RESIZE_TOPRIGHT;
    case HTBOTTOM: return SDL_HITTEST_RESIZE_BOTTOM;
    case HTBOTTOMLEFT: return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    case HTBOTTOMRIGHT: return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    case HTLEFT: return SDL_HITTEST_RESIZE_LEFT;
    case HTRIGHT: return SDL_HITTEST_RESIZE_RIGHT;
    default: return SDL_HITTEST_NORMAL;
    }
}
}

void applyPlacement(Window* target) {
    if (!target || !target->sdl) return;
    // Desktop mode draws its own caption: a window whose WM_NCCALCSIZE keeps the top edge has no frame.
    // (What DefWindowProc would answer differs from the procedure's: the launcher claimed its caption,
    // whatever it then adds for a maximized window.)
    bool custom = false;
    if (target->style & WS_CAPTION) {
        RECT bounds{};
        GetWindowRect(static_cast<HWND>(target), &bounds);
        NCCALCSIZE_PARAMS params{}, standard{};
        params.rgrc[0] = standard.rgrc[0] = bounds;
        DefWindowProcW(static_cast<HWND>(target), WM_NCCALCSIZE, TRUE, reinterpret_cast<LPARAM>(&standard));
        send(target, WM_NCCALCSIZE, TRUE, reinterpret_cast<LPARAM>(&params));
        custom = params.rgrc[0].top != standard.rgrc[0].top;
    }
    target->custom_frame = custom;
    SDL_SetWindowBordered(target->sdl, (target->style & WS_CAPTION) && !custom);
    SDL_SetWindowResizable(target->sdl, (target->style & WS_THICKFRAME) != 0);
    SDL_SetWindowHitTest(target->sdl, custom ? hitTest : nullptr, nullptr);
    SDL_SetWindowAlwaysOnTop(target->sdl, (target->ex_style & WS_EX_TOPMOST) != 0);
}
}

using namespace lm_window::detail;

// ---- Classes and windows -------------------------------------------------------------------

ATOM RegisterClassExW(const WNDCLASSEXW* window_class) {
    if (!window_class || !window_class->lpszClassName) return 0;
    auto& s = state();
    std::wstring name(window_class->lpszClassName);
    if (s.classes.contains(name)) { SetLastError(ERROR_CLASS_ALREADY_EXISTS); return 0; }
    auto type = std::make_shared<WindowClass>();
    type->name = name; type->procedure = window_class->lpfnWndProc; type->style = window_class->style; type->cursor = window_class->hCursor;
    s.classes.emplace(std::move(name), std::move(type));
    return static_cast<ATOM>(0xC000 + s.classes.size());
}

ATOM RegisterClassW(const WNDCLASSW* window_class) {
    if (!window_class) return 0;
    WNDCLASSEXW extended{sizeof(extended), window_class->style, window_class->lpfnWndProc, 0, 0, window_class->hInstance,
                         window_class->hIcon, window_class->hCursor, window_class->hbrBackground, nullptr, window_class->lpszClassName, nullptr};
    return RegisterClassExW(&extended);
}

BOOL UnregisterClassW(const wchar_t* name, HINSTANCE) { return name && state().classes.erase(name) ? TRUE : FALSE; }

HWND CreateWindowExW(DWORD ex_style, const wchar_t* class_name, const wchar_t* title, DWORD style, int x, int y, int width, int height,
                     HWND parent, HMENU, HINSTANCE instance, LPVOID parameter) {
    auto& s = state();
    std::string error;
    if (!s.started && !lm_window::start(error)) return nullptr;
    const auto type = class_name ? s.classes.find(class_name) : s.classes.end();
    if (type == s.classes.end()) { SetLastError(ERROR_INVALID_PARAMETER); return nullptr; }
    auto* created = new Window;
    created->type = type->second;
    created->procedure = type->second->procedure;
    created->title = title ? title : L"";
    created->style = style;
    created->ex_style = ex_style;
    created->parent = parent;
    if (width == CW_USEDEFAULT || width <= 0) width = 1280;
    if (height == CW_USEDEFAULT || height <= 0) height = 720;
    if (const char* value = std::getenv("LAUNCHME_TRACE_WINDOW"); value && *value == '1')
        std::fprintf(stderr, "launchme window: create %d,%d %dx%d style 0x%lx\n", x, y, width, height, static_cast<unsigned long>(style));
    const float d = screenDensity();
    SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, text_encoding::utf8Lenient(created->title).c_str());
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, std::max(1, static_cast<int>(std::lround(width / d))));
    SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, std::max(1, static_cast<int>(std::lround(height / d))));
    if (x != CW_USEDEFAULT) {
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, static_cast<int>(std::lround(x / d)));
        SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, static_cast<int>(std::lround(y / d)));
    }
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, (style & WS_THICKFRAME) != 0);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, (style & WS_CAPTION) == 0);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_ALWAYS_ON_TOP_BOOLEAN, (ex_style & WS_EX_TOPMOST) != 0);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_UTILITY_BOOLEAN, (ex_style & WS_EX_TOOLWINDOW) != 0);
    // Drawn on the GPU: the window is made for Vulkan or OpenGL (gpu.cpp).
    const SDL_WindowFlags gpu_flags = gpu::windowFlags();
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, (gpu_flags & SDL_WINDOW_VULKAN) != 0);
    SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, (gpu_flags & SDL_WINDOW_OPENGL) != 0);
    created->sdl = SDL_CreateWindowWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (!created->sdl) { delete created; return nullptr; }
    gpu::created(created);
    created->id = SDL_GetWindowID(created->sdl);
    {
        std::lock_guard lock(s.windows_mutex);
        s.live.insert(created);
        s.by_id[created->id] = created;
    }
    const HWND handle = static_cast<HWND>(created);
    CREATESTRUCTW create{parameter, instance, nullptr, parent, height, width, y, x, static_cast<LONG>(style), title, class_name, ex_style};
    if (!send(created, WM_NCCREATE, 0, reinterpret_cast<LPARAM>(&create)) && created->procedure) {
        // A procedure that refuses WM_NCCREATE (DefWindowProc accepts it).
    }
    if (send(created, WM_CREATE, 0, reinterpret_cast<LPARAM>(&create)) == -1) { DestroyWindow(handle); return nullptr; }
    applyPlacement(created);
    if (style & WS_VISIBLE) ShowWindow(handle, SW_SHOW);
    return handle;
}

BOOL DestroyWindow(HWND handle) {
    Window* target = window(handle);
    if (!target) return FALSE;
    auto& s = state();
    send(target, WM_DESTROY, 0, 0);
    send(target, WM_NCDESTROY, 0, 0);
    {
        std::lock_guard lock(s.windows_mutex);
        target->destroyed = true;
        s.live.erase(target);
        s.by_id.erase(target->id);
    }
    std::erase_if(s.timers, [&](const Timer& timer) { return timer.window == handle; });
    std::erase_if(s.input, [&](const MSG& message) { return message.hwnd == handle; });
    if (s.capture == handle) s.capture = nullptr;
    if (s.focus == handle) s.focus = nullptr;
    gpu::release(target);
    if (target->sdl) SDL_DestroyWindow(target->sdl);
    delete target;
    return TRUE;
}

LRESULT DefWindowProcW(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
    Window* target = window(handle);
    switch (message) {
    case WM_NCCREATE: return TRUE;
    case WM_CLOSE: DestroyWindow(handle); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: ValidateRect(handle, nullptr); return 0;
    case WM_SETCURSOR:
        if (target && LOWORD(lparam) == HTCLIENT) { SetCursor(target->type->cursor ? target->type->cursor : LoadCursorW(nullptr, IDC_ARROW)); return TRUE; }
        return FALSE;
    case WM_NCCALCSIZE:
        // A decorated window's frame belongs to the desktop: its client area is the whole SDL window.
        if (wparam && target && (target->style & WS_CAPTION)) reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam)->rgrc[0].top += 1;
        return 0;
    case WM_NCHITTEST: {
        if (!target) return HTNOWHERE;
        POINT point{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
        ScreenToClient(handle, &point);
        RECT client{};
        GetClientRect(handle, &client);
        if (!PtInRect(&client, point)) return HTNOWHERE;
        if (!(target->style & WS_THICKFRAME) || IsZoomed(handle)) return HTCLIENT;
        const int edge = static_cast<int>(std::lround(6 * pixelDensity(target)));
        const bool left = point.x < edge, right = point.x >= client.right - edge, top = point.y < edge, bottom = point.y >= client.bottom - edge;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        return HTCLIENT;
    }
    case WM_MOUSEACTIVATE: return MA_ACTIVATE;
    case WM_GETMINMAXINFO: return 0;
    default: return 0;
    }
}

LRESULT CallWindowProcW(WNDPROC procedure, HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return procedure ? procedure(window, message, wparam, lparam) : 0;
}

// ---- Placement -----------------------------------------------------------------------------

BOOL ShowWindow(HWND handle, int command) {
    Window* target = window(handle);
    if (!target) return FALSE;
    static const bool trace = [] { const char* value = std::getenv("LAUNCHME_TRACE_WINDOW"); return value && *value == '1'; }();
    if (trace) std::fprintf(stderr, "launchme window: %p ShowWindow %d\n", handle, command);
    const bool was_visible = (target->style & WS_VISIBLE) != 0;
    switch (command) {
    case SW_HIDE:
        target->style &= ~WS_VISIBLE;
        SDL_HideWindow(target->sdl);
        break;
    case SW_MINIMIZE: case SW_SHOWMINIMIZED: case SW_SHOWMINNOACTIVE: case SW_FORCEMINIMIZE:
        target->style |= WS_VISIBLE;
        SDL_ShowWindow(target->sdl);
        SDL_MinimizeWindow(target->sdl);
        break;
    case SW_MAXIMIZE:
        target->style |= WS_VISIBLE;
        SDL_ShowWindow(target->sdl);
        if (!target->fullscreen) SDL_MaximizeWindow(target->sdl);
        break;
    case SW_RESTORE:
        target->style |= WS_VISIBLE;
        SDL_ShowWindow(target->sdl);
        SDL_RestoreWindow(target->sdl);
        break;
    default:
        target->style |= WS_VISIBLE;
        SDL_ShowWindow(target->sdl);
        if (command != SW_SHOWNA && command != SW_SHOWNOACTIVATE) SDL_RaiseWindow(target->sdl);
        break;
    }
    target->invalid = true;
    return was_visible ? TRUE : FALSE;
}

BOOL UpdateWindow(HWND handle) {
    Window* target = window(handle);
    if (!target || !target->invalid) return target != nullptr;
    send(target, WM_PAINT, 0, 0);
    return TRUE;
}

BOOL SetWindowPos(HWND handle, HWND insert_after, int x, int y, int width, int height, UINT flags) {
    Window* target = window(handle);
    if (!target) return FALSE;
    // LAUNCHME_TRACE_WINDOW=1: every placement request on stderr (diagnosing a window manager).
    static const bool trace = [] { const char* value = std::getenv("LAUNCHME_TRACE_WINDOW"); return value && *value == '1'; }();
    if (trace) std::fprintf(stderr, "launchme window: %p SetWindowPos %d,%d %dx%d flags 0x%x style 0x%lx sdl 0x%llx\n", handle, x, y, width, height,
                            flags, static_cast<unsigned long>(target->style), static_cast<unsigned long long>(SDL_GetWindowFlags(target->sdl)));
    if (insert_after == HWND_TOPMOST) target->ex_style |= WS_EX_TOPMOST;
    else if (insert_after == HWND_NOTOPMOST) target->ex_style &= ~WS_EX_TOPMOST;
    if (flags & SWP_FRAMECHANGED) applyPlacement(target);
    else if (insert_after == HWND_TOPMOST || insert_after == HWND_NOTOPMOST) SDL_SetWindowAlwaysOnTop(target->sdl, insert_after == HWND_TOPMOST);
    if (!(flags & SWP_NOMOVE) || !(flags & SWP_NOSIZE)) {
        RECT current{};
        GetWindowRect(handle, &current);
        RECT wanted{flags & SWP_NOMOVE ? current.left : x, flags & SWP_NOMOVE ? current.top : y, 0, 0};
        wanted.right = wanted.left + (flags & SWP_NOSIZE ? current.right - current.left : width);
        wanted.bottom = wanted.top + (flags & SWP_NOSIZE ? current.bottom - current.top : height);
        // A popup that fills a display is the launcher's full-screen TV mode.
        const SDL_DisplayID display = (target->style & WS_POPUP) ? displayFilledBy(wanted) : 0;
        if (display) {
            target->fullscreen = true;
            SDL_SetWindowFullscreenMode(target->sdl, nullptr); // Borderless, at the desktop's mode.
            SDL_SetWindowFullscreen(target->sdl, true);
        } else {
            // Leaving full screen or a maximized state completes asynchronously on X11 and Wayland, and
            // then restores the window's earlier size: wait for it, or that restore undoes the new size.
            if (target->fullscreen) { SDL_SetWindowFullscreen(target->sdl, false); target->fullscreen = false; SDL_SyncWindow(target->sdl); }
            const float d = screenDensity();
            if ((SDL_GetWindowFlags(target->sdl) & SDL_WINDOW_MAXIMIZED) && !(flags & SWP_NOSIZE)) { SDL_RestoreWindow(target->sdl); SDL_SyncWindow(target->sdl); }
            if (!(flags & SWP_NOSIZE))
                SDL_SetWindowSize(target->sdl, std::max(1, static_cast<int>(std::lround((wanted.right - wanted.left) / d))),
                                  std::max(1, static_cast<int>(std::lround((wanted.bottom - wanted.top) / d))));
            if (!(flags & SWP_NOMOVE))
                SDL_SetWindowPosition(target->sdl, static_cast<int>(std::lround(wanted.left / d)), static_cast<int>(std::lround(wanted.top / d)));
            // SetWindowPos is synchronous on Windows (the caller reads GetClientRect at once).
            SDL_SyncWindow(target->sdl);
        }
    }
    if (flags & SWP_SHOWWINDOW) ShowWindow(handle, flags & SWP_NOACTIVATE ? SW_SHOWNA : SW_SHOW);
    if (flags & SWP_HIDEWINDOW) ShowWindow(handle, SW_HIDE);
    if (trace) {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(target->sdl, &w, &h);
        std::fprintf(stderr, "launchme window: %p now %dx%d sdl 0x%llx\n", handle, w, h, static_cast<unsigned long long>(SDL_GetWindowFlags(target->sdl)));
    }
    target->invalid = true;
    return TRUE;
}

BOOL MoveWindow(HWND window, int x, int y, int width, int height, BOOL) {
    return SetWindowPos(window, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

BOOL GetWindowRect(HWND handle, RECT* rect) {
    Window* target = window(handle);
    if (!target || !rect) return FALSE;
    int x = 0, y = 0, width = 0, height = 0;
    SDL_GetWindowPosition(target->sdl, &x, &y);
    SDL_GetWindowSizeInPixels(target->sdl, &width, &height);
    const float d = screenDensity();
    rect->left = static_cast<LONG>(std::lround(x * d));
    rect->top = static_cast<LONG>(std::lround(y * d));
    rect->right = rect->left + width;
    rect->bottom = rect->top + height;
    return TRUE;
}

BOOL GetClientRect(HWND handle, RECT* rect) {
    Window* target = window(handle);
    if (!target || !rect) return FALSE;
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(target->sdl, &width, &height);
    *rect = RECT{0, 0, width, height};
    return TRUE;
}

BOOL AdjustWindowRectEx(RECT*, DWORD, BOOL, DWORD) { return TRUE; } // Frames belong to the desktop.
BOOL AdjustWindowRectExForDpi(RECT*, DWORD, BOOL, DWORD, UINT) { return TRUE; }

BOOL GetWindowPlacement(HWND handle, WINDOWPLACEMENT* placement) {
    Window* target = window(handle);
    if (!target || !placement) return FALSE;
    placement->flags = 0;
    placement->showCmd = IsIconic(handle) ? SW_SHOWMINIMIZED : IsZoomed(handle) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    GetWindowRect(handle, &placement->rcNormalPosition);
    return TRUE;
}

BOOL SetWindowPlacement(HWND handle, const WINDOWPLACEMENT* placement) {
    if (!placement) return FALSE;
    const RECT& r = placement->rcNormalPosition;
    SetWindowPos(handle, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
    if (placement->showCmd == SW_SHOWMAXIMIZED) ShowWindow(handle, SW_MAXIMIZE);
    else if (placement->showCmd == SW_SHOWMINIMIZED) ShowWindow(handle, SW_MINIMIZE);
    return TRUE;
}

BOOL ClientToScreen(HWND handle, POINT* point) {
    RECT bounds{};
    if (!point || !GetWindowRect(handle, &bounds)) return FALSE;
    point->x += bounds.left; point->y += bounds.top;
    return TRUE;
}

BOOL ScreenToClient(HWND handle, POINT* point) {
    RECT bounds{};
    if (!point || !GetWindowRect(handle, &bounds)) return FALSE;
    point->x -= bounds.left; point->y -= bounds.top;
    return TRUE;
}

BOOL IsWindow(HWND handle) { return window(handle) ? TRUE : FALSE; }
BOOL IsWindowVisible(HWND handle) {
    Window* target = window(handle);
    return target && (target->style & WS_VISIBLE) && !(SDL_GetWindowFlags(target->sdl) & SDL_WINDOW_HIDDEN);
}
BOOL IsIconic(HWND handle) { Window* target = window(handle); return target && (SDL_GetWindowFlags(target->sdl) & SDL_WINDOW_MINIMIZED); }
BOOL IsZoomed(HWND handle) { Window* target = window(handle); return target && (SDL_GetWindowFlags(target->sdl) & SDL_WINDOW_MAXIMIZED); }
BOOL IsChild(HWND parent, HWND child) {
    for (Window* target = window(child); target; target = window(target->parent)) if (target->parent == parent) return TRUE;
    return FALSE;
}
BOOL IsWindowEnabled(HWND handle) { Window* target = window(handle); return target && target->enabled; }
BOOL EnableWindow(HWND handle, BOOL enable) {
    Window* target = window(handle);
    if (!target) return FALSE;
    const bool was_disabled = !target->enabled;
    target->enabled = enable != FALSE;
    return was_disabled;
}

HWND SetFocus(HWND handle) {
    auto& s = state();
    const HWND previous = s.focus;
    if (Window* target = window(handle)) SDL_RaiseWindow(target->sdl);
    return previous;
}
HWND GetFocus() { return state().focus; }
HWND GetActiveWindow() { return state().focus; }
HWND GetForegroundWindow() {
    auto& s = state();
    std::lock_guard lock(s.windows_mutex);
    for (Window* target : s.live) if (SDL_GetWindowFlags(target->sdl) & SDL_WINDOW_INPUT_FOCUS) return static_cast<HWND>(target);
    return nullptr; // Another program's window: the display server does not tell which.
}
BOOL SetForegroundWindow(HWND handle) {
    Window* target = window(handle);
    if (!target) return FALSE;
    SDL_RaiseWindow(target->sdl);
    return TRUE;
}
BOOL BringWindowToTop(HWND handle) { return SetForegroundWindow(handle); }
BOOL AllowSetForegroundWindow(DWORD) { return TRUE; }
BOOL FlashWindow(HWND handle, BOOL) {
    Window* target = window(handle);
    return target && SDL_FlashWindow(target->sdl, SDL_FLASH_BRIEFLY);
}
HWND GetParent(HWND handle) { Window* target = window(handle); return target ? target->parent : nullptr; }
HWND GetAncestor(HWND handle, UINT) {
    HWND root = handle;
    for (Window* target = window(handle); target && target->parent; target = window(target->parent)) root = target->parent;
    return root;
}

LONG_PTR GetWindowLongPtrW(HWND handle, int index) {
    Window* target = window(handle);
    if (!target) return 0;
    switch (index) {
    case GWLP_USERDATA: return target->user_data;
    case GWL_STYLE: return static_cast<LONG_PTR>(target->style);
    case GWL_EXSTYLE: return static_cast<LONG_PTR>(target->ex_style);
    case GWLP_WNDPROC: return reinterpret_cast<LONG_PTR>(target->procedure);
    default: return 0;
    }
}

LONG_PTR SetWindowLongPtrW(HWND handle, int index, LONG_PTR value) {
    Window* target = window(handle);
    if (!target) return 0;
    LONG_PTR previous = GetWindowLongPtrW(handle, index);
    switch (index) {
    case GWLP_USERDATA: target->user_data = value; break;
    case GWL_STYLE:
        target->style = static_cast<DWORD>(value);
        if (!(target->style & WS_VISIBLE)) SDL_HideWindow(target->sdl);
        break;
    case GWL_EXSTYLE: target->ex_style = static_cast<DWORD>(value); break;
    case GWLP_WNDPROC: target->procedure = reinterpret_cast<WNDPROC>(value); break;
    default: break;
    }
    return previous;
}

BOOL SetWindowTextW(HWND handle, const wchar_t* text) {
    Window* target = window(handle);
    if (!target) return FALSE;
    target->title = text ? text : L"";
    SDL_SetWindowTitle(target->sdl, text_encoding::utf8Lenient(target->title).c_str());
    return TRUE;
}

int GetWindowTextW(HWND handle, wchar_t* text, int size) {
    Window* target = window(handle);
    if (!target || !text || size <= 0) return 0;
    const int copied = std::min<int>(size - 1, static_cast<int>(target->title.size()));
    std::wmemcpy(text, target->title.c_str(), copied);
    text[copied] = L'\0';
    return copied;
}

int GetClassNameW(HWND handle, wchar_t* name, int size) {
    Window* target = window(handle);
    if (!target || !name || size <= 0) return 0;
    const int copied = std::min<int>(size - 1, static_cast<int>(target->type->name.size()));
    std::wmemcpy(name, target->type->name.c_str(), copied);
    name[copied] = L'\0';
    return copied;
}

HWND FindWindowW(const wchar_t* class_name, const wchar_t* title) {
    auto& s = state();
    std::lock_guard lock(s.windows_mutex);
    for (Window* target : s.live)
        if ((!class_name || target->type->name == class_name) && (!title || target->title == title)) return static_cast<HWND>(target);
    return nullptr;
}

DWORD GetWindowThreadProcessId(HWND handle, DWORD* process_id) {
    if (!window(handle)) { if (process_id) *process_id = 0; return 0; }
    if (process_id) *process_id = GetCurrentProcessId();
    return GetCurrentThreadId();
}
BOOL AttachThreadInput(DWORD, DWORD, BOOL) { return TRUE; }

HWND WindowFromPoint(POINT point) {
    auto& s = state();
    std::lock_guard lock(s.windows_mutex);
    for (Window* target : s.live) {
        RECT bounds{};
        if (IsWindowVisible(static_cast<HWND>(target)) && GetWindowRect(static_cast<HWND>(target), &bounds) && PtInRect(&bounds, point))
            return static_cast<HWND>(target);
    }
    return nullptr;
}

// ---- Messages ------------------------------------------------------------------------------

BOOL PostMessageW(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
    auto& s = state();
    if (handle && !window(handle)) return FALSE;
    {
        std::lock_guard lock(s.posted_mutex);
        s.posted.push_back(MSG{handle, message, wparam, lparam, static_cast<DWORD>(now()), {}});
    }
    // Wakes waitForInput on the UI thread (one pending wake is enough).
    if (s.started && !s.wake_pending.exchange(true)) {
        SDL_Event wake{};
        wake.type = s.wake_event;
        SDL_PushEvent(&wake);
    }
    return TRUE;
}

BOOL PostThreadMessageW(DWORD, UINT message, WPARAM wparam, LPARAM lparam) { return PostMessageW(nullptr, message, wparam, lparam); }

LRESULT SendMessageW(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
    return send(window(handle), message, wparam, lparam);
}

LRESULT SendMessageTimeoutW(HWND handle, UINT message, WPARAM wparam, LPARAM lparam, UINT, UINT, DWORD_PTR* result) {
    Window* target = window(handle);
    if (!target) return 0;
    const LRESULT value = send(target, message, wparam, lparam);
    if (result) *result = static_cast<DWORD_PTR>(value);
    return 1;
}

void PostQuitMessage(int exit_code) {
    auto& s = state();
    s.quit = true;
    s.quit_code = exit_code;
}

UINT RegisterWindowMessageW(const wchar_t* name) {
    static std::mutex mutex;
    static std::unordered_map<std::wstring, UINT> registered;
    std::lock_guard lock(mutex);
    if (!name) return 0;
    const auto found = registered.find(name);
    if (found != registered.end()) return found->second;
    const UINT id = 0xC000 + static_cast<UINT>(registered.size());
    registered.emplace(name, id);
    return id;
}

DWORD GetMessageTime() { return static_cast<DWORD>(now()); }

namespace {
bool matches(const MSG& message, HWND filter, UINT first, UINT last) {
    if (filter && message.hwnd != filter) return false;
    if ((first || last) && (message.message < first || message.message > last)) return false;
    return true;
}

// Moves SDL's pending events into the input queue (UI thread).
void pump() {
    SDL_PumpEvents();
    SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) > 0) translate(event);
    auto& s = state();
    std::vector<std::function<void()>> work;
    { std::lock_guard lock(s.work_mutex); work.swap(s.work); }
    for (auto& item : work) item();
}

template <class Queue>
bool take(Queue& queue, MSG* message, HWND filter, UINT first, UINT last, bool remove) {
    for (auto it = queue.begin(); it != queue.end(); ++it) {
        if (!matches(*it, filter, first, last)) continue;
        *message = *it;
        if (remove) queue.erase(it);
        return true;
    }
    return false;
}
}

BOOL PeekMessageW(MSG* message, HWND filter, UINT first, UINT last, UINT remove) {
    auto& s = state();
    if (!message) return FALSE;
    if (s.started) pump();
    const bool removing = (remove & PM_REMOVE) != 0;
    // Posted messages first, then input, then painting and timers (as Windows orders them).
    {
        std::lock_guard lock(s.posted_mutex);
        if (take(s.posted, message, filter, first, last, removing)) return TRUE;
    }
    if (take(s.input, message, filter, first, last, removing)) return TRUE;
    if (s.quit && (!first && !last ? true : (WM_QUIT >= first && WM_QUIT <= last)) && !filter) {
        *message = MSG{nullptr, WM_QUIT, static_cast<WPARAM>(s.quit_code), 0, static_cast<DWORD>(now()), {}};
        if (removing) s.quit = false;
        return TRUE;
    }
    const auto in_range = [&](UINT id) { return (!first && !last) || (id >= first && id <= last); };
    if (in_range(WM_PAINT)) {
        std::lock_guard lock(s.windows_mutex);
        for (Window* target : s.live) {
            const HWND handle = static_cast<HWND>(target);
            if (!target->invalid || (filter && filter != handle) || !(target->style & WS_VISIBLE)) continue;
            *message = MSG{handle, WM_PAINT, 0, 0, static_cast<DWORD>(now()), {}};
            return TRUE;
        }
    }
    if (in_range(WM_TIMER)) {
        const uint64_t time = now();
        for (auto& timer : s.timers) {
            if (timer.due > time || (filter && filter != timer.window)) continue;
            *message = MSG{timer.window, WM_TIMER, timer.id, 0, static_cast<DWORD>(time), {}};
            if (removing) timer.due = time + std::max<UINT>(timer.interval, 10);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL GetMessageW(MSG* message, HWND filter, UINT first, UINT last) {
    while (!PeekMessageW(message, filter, first, last, PM_REMOVE)) lm_window::waitForInput(0.05);
    return message->message != WM_QUIT;
}

BOOL TranslateMessage(const MSG* message) {
    // Characters come from SDL's text input; Windows' control characters from their keys.
    if (!message || (message->message != WM_KEYDOWN && message->message != WM_SYSKEYDOWN)) return FALSE;
    wchar_t character = 0;
    switch (message->wParam) {
    case VK_BACK: character = L'\b'; break;
    case VK_TAB: character = L'\t'; break;
    case VK_RETURN: character = L'\r'; break;
    case VK_ESCAPE: character = 0x1B; break;
    default:
        if ((GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) && message->wParam >= 'A' && message->wParam <= 'Z')
            character = static_cast<wchar_t>(message->wParam - 'A' + 1);
        break;
    }
    if (!character) return FALSE;
    pushInput(message->hwnd, message->message == WM_SYSKEYDOWN ? WM_SYSCHAR : WM_CHAR, character, message->lParam);
    return TRUE;
}

LRESULT DispatchMessageW(const MSG* message) {
    if (!message) return 0;
    if (message->message == WM_TIMER && message->lParam) {
        reinterpret_cast<TIMERPROC>(message->lParam)(message->hwnd, WM_TIMER, message->wParam, message->time);
        return 0;
    }
    return send(window(message->hwnd), message->message, message->wParam, message->lParam);
}

// ---- Painting and presentation -------------------------------------------------------------

BOOL InvalidateRect(HWND handle, const RECT*, BOOL) {
    Window* target = window(handle);
    if (!target) return FALSE;
    target->invalid = true;
    return TRUE;
}
BOOL ValidateRect(HWND handle, const RECT*) {
    Window* target = window(handle);
    if (!target) return FALSE;
    target->invalid = false;
    return TRUE;
}
BOOL RedrawWindow(HWND handle, const RECT*, void*, UINT flags) {
    if (!InvalidateRect(handle, nullptr, FALSE)) return FALSE;
    if (flags & RDW_UPDATENOW) UpdateWindow(handle);
    return TRUE;
}
HDC BeginPaint(HWND handle, PAINTSTRUCT* paint) {
    if (paint) { *paint = PAINTSTRUCT{}; GetClientRect(handle, &paint->rcPaint); }
    ValidateRect(handle, nullptr);
    return nullptr; // Drawing goes through Direct2D on Linux.
}
BOOL EndPaint(HWND, const PAINTSTRUCT*) { return TRUE; }
BOOL GetUpdateRect(HWND handle, RECT* rect, BOOL) {
    Window* target = window(handle);
    if (!target || !target->invalid) { if (rect) *rect = RECT{}; return FALSE; }
    if (rect) GetClientRect(handle, rect);
    return TRUE;
}

// ---- Timers --------------------------------------------------------------------------------

UINT_PTR SetTimer(HWND handle, UINT_PTR id, UINT elapse, TIMERPROC procedure) {
    auto& s = state();
    if (handle && !window(handle)) return 0;
    if (procedure) return 0; // Timer callbacks are not used by the launcher.
    for (auto& timer : s.timers)
        if (timer.window == handle && timer.id == id) { timer.interval = elapse; timer.due = now() + std::max<UINT>(elapse, 10); return id; }
    s.timers.push_back(Timer{handle, id, elapse, now() + std::max<UINT>(elapse, 10)});
    return id ? id : 1;
}

BOOL KillTimer(HWND handle, UINT_PTR id) {
    auto& timers = state().timers;
    return std::erase_if(timers, [&](const Timer& timer) { return timer.window == handle && timer.id == id; }) ? TRUE : FALSE;
}

// ---- Monitors and DPI ----------------------------------------------------------------------

namespace {
HMONITOR monitorHandle(SDL_DisplayID display) { return reinterpret_cast<HMONITOR>(static_cast<uintptr_t>(display)); }
SDL_DisplayID displayOf(HMONITOR monitor) { return static_cast<SDL_DisplayID>(reinterpret_cast<uintptr_t>(monitor)); }
}

HMONITOR MonitorFromWindow(HWND handle, DWORD flags) {
    Window* target = window(handle);
    SDL_DisplayID display = target ? SDL_GetDisplayForWindow(target->sdl) : 0;
    if (!display && flags != MONITOR_DEFAULTTONULL) display = SDL_GetPrimaryDisplay();
    return monitorHandle(display);
}

HMONITOR MonitorFromRect(const RECT* rect, DWORD flags) {
    if (!rect) return nullptr;
    const float d = screenDensity();
    const SDL_Rect area{static_cast<int>(rect->left / d), static_cast<int>(rect->top / d),
                        std::max(1, static_cast<int>((rect->right - rect->left) / d)), std::max(1, static_cast<int>((rect->bottom - rect->top) / d))};
    SDL_DisplayID display = SDL_GetDisplayForRect(&area);
    if (display) {
        SDL_Rect bounds{}, overlap{};
        if (flags == MONITOR_DEFAULTTONULL && SDL_GetDisplayBounds(display, &bounds) && !SDL_GetRectIntersection(&bounds, &area, &overlap)) display = 0;
    }
    if (!display && flags != MONITOR_DEFAULTTONULL) display = SDL_GetPrimaryDisplay();
    return monitorHandle(display);
}

HMONITOR MonitorFromPoint(POINT point, DWORD flags) {
    RECT rect{point.x, point.y, point.x + 1, point.y + 1};
    return MonitorFromRect(&rect, flags);
}

BOOL GetMonitorInfoW(HMONITOR monitor, MONITORINFO* info) {
    const SDL_DisplayID display = displayOf(monitor);
    if (!display || !info) return FALSE;
    info->rcMonitor = displayRect(display, false);
    info->rcWork = displayRect(display, true);
    if (info->rcWork.right <= info->rcWork.left) info->rcWork = info->rcMonitor;
    info->dwFlags = display == SDL_GetPrimaryDisplay() ? MONITORINFOF_PRIMARY : 0;
    if (info->cbSize >= sizeof(MONITORINFOEXW)) {
        auto* extended = reinterpret_cast<MONITORINFOEXW*>(info);
        const char* name = SDL_GetDisplayName(display);
        const auto wide = text_encoding::wideLenient(name ? name : "");
        const auto copied = std::min<size_t>(wide.size(), CCHDEVICENAME - 1);
        std::wmemcpy(extended->szDevice, wide.c_str(), copied);
        extended->szDevice[copied] = L'\0';
    }
    return TRUE;
}

BOOL EnumDisplayMonitors(HDC, const RECT*, MONITORENUMPROC callback, LPARAM data) {
    if (!callback) return FALSE;
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    for (int i = 0; displays && i < count; ++i) {
        RECT bounds = displayRect(displays[i], false);
        if (!callback(monitorHandle(displays[i]), nullptr, &bounds, data)) break;
    }
    SDL_free(displays);
    return TRUE;
}

UINT GetDpiForWindow(HWND handle) {
    Window* target = window(handle);
    const float scale = target ? SDL_GetWindowDisplayScale(target->sdl) : 0.0f;
    return static_cast<UINT>(std::lround(USER_DEFAULT_SCREEN_DPI * (scale > 0 ? scale : 1.0f)));
}

UINT GetDpiForSystem() {
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    const float scale = primary ? SDL_GetDisplayContentScale(primary) : 1.0f;
    return static_cast<UINT>(std::lround(USER_DEFAULT_SCREEN_DPI * (scale > 0 ? scale : 1.0f)));
}

int GetSystemMetricsForDpi(int index, UINT dpi) {
    const float scale = dpi / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    switch (index) {
    case SM_CXFRAME: case SM_CYFRAME: return static_cast<int>(std::lround(4 * scale));
    case SM_CXPADDEDBORDER: return static_cast<int>(std::lround(4 * scale));
    case SM_CXSMICON: case SM_CYSMICON: return static_cast<int>(std::lround(16 * scale));
    case SM_CXICON: case SM_CYICON: return static_cast<int>(std::lround(32 * scale));
    case SM_CXVSCROLL: return static_cast<int>(std::lround(17 * scale));
    default: return GetSystemMetrics(index);
    }
}

int GetSystemMetrics(int index) {
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    switch (index) {
    case SM_CXSCREEN: case SM_CYSCREEN: {
        const RECT bounds = primary ? displayRect(primary, false) : RECT{0, 0, 1920, 1080};
        return index == SM_CXSCREEN ? bounds.right - bounds.left : bounds.bottom - bounds.top;
    }
    case SM_CXVIRTUALSCREEN: case SM_CYVIRTUALSCREEN: {
        int count = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&count);
        RECT all{};
        for (int i = 0; displays && i < count; ++i) { RECT bounds = displayRect(displays[i], false); UnionRect(&all, &all, &bounds); }
        SDL_free(displays);
        return index == SM_CXVIRTUALSCREEN ? all.right - all.left : all.bottom - all.top;
    }
    case SM_CMONITORS: { int count = 0; SDL_free(SDL_GetDisplays(&count)); return count; }
    case SM_CXDOUBLECLK: case SM_CYDOUBLECLK: return 4;
    case SM_CXDRAG: case SM_CYDRAG: return 4;
    default: return GetSystemMetricsForDpi(index, USER_DEFAULT_SCREEN_DPI);
    }
}

BOOL IntersectRect(RECT* result, const RECT* a, const RECT* b) {
    RECT r{std::max(a->left, b->left), std::max(a->top, b->top), std::min(a->right, b->right), std::min(a->bottom, b->bottom)};
    if (r.right <= r.left || r.bottom <= r.top) { *result = RECT{}; return FALSE; }
    *result = r;
    return TRUE;
}

BOOL UnionRect(RECT* result, const RECT* a, const RECT* b) {
    if (IsRectEmpty(a)) { *result = *b; return !IsRectEmpty(b); }
    if (IsRectEmpty(b)) { *result = *a; return TRUE; }
    *result = RECT{std::min(a->left, b->left), std::min(a->top, b->top), std::max(a->right, b->right), std::max(a->bottom, b->bottom)};
    return TRUE;
}

// ---- The loop's other entry points ---------------------------------------------------------

namespace lm_window {
bool start(std::string& error) {
    auto& s = state();
    if (s.started) return true;
    SDL_SetHint(SDL_HINT_APP_NAME, "launchme");
    SDL_SetHint(SDL_HINT_APP_ID, "io.github.uxandai.launchme");
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    // Pads keep their state current while a game has the focus (ControllerInput reads them).
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1", SDL_HINT_DEFAULT);
    // WSL: Mesa picks llvmpipe (OpenGL on the CPU) unless told to use the GPU through D3D12, and
    // WSLg's Wayland takes a GPU frame through a copy of a quarter second; its X server (XWayland)
    // shows the same frame in a few milliseconds.
    if (access("/usr/lib/wsl/lib/libd3d12.so", R_OK) == 0) {
        if (!std::getenv("GALLIUM_DRIVER")) setenv("GALLIUM_DRIVER", "d3d12", 0);
        if (!std::getenv("SDL_VIDEO_DRIVER") && std::getenv("DISPLAY")) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { error = SDL_GetError(); return false; }
    // Gamepads on the thread that pumps events (their hotplug becomes WM_DEVICECHANGE); a machine
    // without joystick support still starts.
    s.gamepads = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    // X11 (XWayland too): Xlib's default handler ends the process on any protocol error, and SDL
    // asks for outputs that XWayland may have removed meanwhile (BadRROutput while it handles
    // RandR's notifications). Such an error is reported instead; SDL's own temporary handlers
    // still restore this one.
    if (const char* driver = SDL_GetCurrentVideoDriver(); driver && SDL_strcmp(driver, "x11") == 0)
        if (void* xlib = dlopen("libX11.so.6", RTLD_LAZY | RTLD_NOLOAD)) {
            struct ErrorEvent { int type; void* display; unsigned long resource, serial; unsigned char error, request, minor; };
            using Handler = int (*)(void*, ErrorEvent*);
            if (auto set = reinterpret_cast<Handler (*)(Handler)>(dlsym(xlib, "XSetErrorHandler")))
                set([](void*, ErrorEvent* event) {
                    std::fprintf(stderr, "launchme: X error %u (request %u.%u), ignored\n", event->error, event->request, event->minor);
                    return 0;
                });
            dlclose(xlib);
        }
    s.wake_event = SDL_RegisterEvents(1);
    s.started = true;
    return true;
}

void stop() {
    auto& s = state();
    if (!s.started) return;
    s.started = false;
    gpu::shutdown();
    if (s.gamepads) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    s.gamepads = false;
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
}

std::string lastError() { const char* error = SDL_GetError(); return error ? error : ""; }

int refreshHz(HWND handle) {
    Window* target = detail::window(handle);
    const SDL_DisplayID display = target ? SDL_GetDisplayForWindow(target->sdl) : SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* mode = display ? SDL_GetCurrentDisplayMode(display) : nullptr;
    // Rounded up (59.94 stays 60, 60.3 is 61): frames paced at it never leave a refresh without a
    // new one, and a present that waits for the refresh takes up the difference.
    return mode && mode->refresh_rate > 1 ? static_cast<int>(std::ceil(mode->refresh_rate - 0.1f)) : 0;
}

void waitForInput(double seconds) {
    auto& s = state();
    {
        std::lock_guard lock(s.posted_mutex);
        if (!s.posted.empty()) return;
    }
    if (!s.input.empty() || !s.started) return;
    // An early timer cuts the wait short.
    const uint64_t time = now();
    for (const auto& timer : s.timers) seconds = std::min(seconds, timer.due > time ? (timer.due - time) / 1000.0 : 0.0);
    const auto milliseconds = static_cast<Sint32>(std::clamp(std::ceil(seconds * 1000.0), 0.0, 1000.0));
    if (milliseconds > 0) SDL_WaitEventTimeout(nullptr, milliseconds);
}

SDL_Window* sdlWindow(HWND handle) { Window* target = detail::window(handle); return target ? target->sdl : nullptr; }

void post(std::function<void()> work) {
    auto& s = state();
    { std::lock_guard lock(s.work_mutex); s.work.push_back(std::move(work)); }
    if (s.started && !s.wake_pending.exchange(true)) {
        SDL_Event wake{};
        wake.type = s.wake_event;
        SDL_PushEvent(&wake);
    }
}

void enableCustomFrame(HWND handle, bool enabled) {
    Window* target = detail::window(handle);
    if (!target) return;
    target->custom_frame = enabled;
    SDL_SetWindowHitTest(target->sdl, enabled ? hitTest : nullptr, nullptr);
}
}
