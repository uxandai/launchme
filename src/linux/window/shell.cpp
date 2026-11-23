// Input state, the cursor, the clipboard, dropped files, the shell (opening links and folders,
// known folders, the notification-area icon), message boxes and the desktop's file dialog, on
// SDL3 and the XDG conventions. The few GDI calls draw nothing: Direct2D paints on Linux.
#include "window_internal.hpp"
#include "../win32/shlobj.h"
#include "../../platform/process_spawn.hpp"
#include "../../platform/text_encoding.hpp"
#include "../../i18n/i18n.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>

using namespace lm_window::detail;

// ---- Keys, mouse and cursor ----------------------------------------------------------------

SHORT GetKeyState(int key) {
    auto& s = state();
    if (key < 0 || key >= static_cast<int>(s.keys.size())) return 0;
    SHORT result = s.keys[key] ? static_cast<SHORT>(0x8000) : 0;
    if (key == VK_CAPITAL) return static_cast<SHORT>(result | ((SDL_GetModState() & SDL_KMOD_CAPS) ? 1 : 0));
    if (key == VK_NUMLOCK) return static_cast<SHORT>(result | ((SDL_GetModState() & SDL_KMOD_NUM) ? 1 : 0));
    return static_cast<SHORT>(result | (s.toggled[key] ? 1 : 0));
}

SHORT GetAsyncKeyState(int key) {
    if (key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON) {
        const SDL_MouseButtonFlags buttons = SDL_GetGlobalMouseState(nullptr, nullptr);
        const bool down = key == VK_LBUTTON ? (buttons & SDL_BUTTON_LMASK) : key == VK_RBUTTON ? (buttons & SDL_BUTTON_RMASK) : (buttons & SDL_BUTTON_MMASK);
        return down ? static_cast<SHORT>(0x8000) : 0;
    }
    return static_cast<SHORT>(GetKeyState(key) & 0x8000);
}

UINT MapVirtualKeyW(UINT code, UINT type) {
    if (type == MAPVK_VK_TO_CHAR) {
        if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) return code;
        if (code == VK_SPACE) return L' ';
        return 0;
    }
    return 0;
}

HWND SetCapture(HWND handle) {
    auto& s = state();
    const HWND previous = s.capture;
    if (window(handle)) { s.capture = handle; SDL_CaptureMouse(true); }
    return previous;
}

BOOL ReleaseCapture() {
    auto& s = state();
    const HWND previous = s.capture;
    s.capture = nullptr;
    SDL_CaptureMouse(false);
    if (Window* target = window(previous)) send(target, WM_CAPTURECHANGED, 0, 0);
    return TRUE;
}

HWND GetCapture() { return state().capture; }

BOOL GetCursorPos(POINT* point) {
    if (!point) return FALSE;
    float x = 0, y = 0;
    SDL_GetGlobalMouseState(&x, &y);
    const float d = screenDensity();
    *point = POINT{static_cast<LONG>(std::lround(x * d)), static_cast<LONG>(std::lround(y * d))};
    // Wayland hides the global position: the last one over a launcher window stands in.
    auto& s = state();
    std::lock_guard lock(s.windows_mutex);
    for (Window* target : s.live)
        if (target->last_mouse.x >= 0 && (SDL_GetWindowFlags(target->sdl) & SDL_WINDOW_MOUSE_FOCUS)) {
            POINT screen = target->last_mouse;
            ClientToScreen(static_cast<HWND>(target), &screen);
            *point = screen;
            break;
        }
    return TRUE;
}

BOOL SetCursorPos(int x, int y) {
    const float d = screenDensity();
    return SDL_WarpMouseGlobal(x / d, y / d) ? TRUE : FALSE;
}

namespace {
struct Cursor { SDL_Cursor* sdl = nullptr; };
std::map<uintptr_t, Cursor>& cursors() { static std::map<uintptr_t, Cursor> loaded; return loaded; }
HCURSOR current_cursor = nullptr;
bool cursor_shown = true;
}

HCURSOR LoadCursorW(HINSTANCE, const wchar_t* name) {
    const auto id = reinterpret_cast<uintptr_t>(name);
    auto& loaded = cursors();
    if (const auto found = loaded.find(id); found != loaded.end()) return reinterpret_cast<HCURSOR>(&found->second);
    SDL_SystemCursor shape = SDL_SYSTEM_CURSOR_DEFAULT;
    switch (id) {
    case 32513: shape = SDL_SYSTEM_CURSOR_TEXT; break;
    case 32514: shape = SDL_SYSTEM_CURSOR_WAIT; break;
    case 32515: shape = SDL_SYSTEM_CURSOR_CROSSHAIR; break;
    case 32642: shape = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
    case 32643: shape = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
    case 32644: shape = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
    case 32645: shape = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
    case 32646: shape = SDL_SYSTEM_CURSOR_MOVE; break;
    case 32648: shape = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
    case 32649: shape = SDL_SYSTEM_CURSOR_POINTER; break;
    case 32650: shape = SDL_SYSTEM_CURSOR_PROGRESS; break;
    default: break;
    }
    auto& cursor = loaded[id];
    cursor.sdl = SDL_CreateSystemCursor(shape);
    return reinterpret_cast<HCURSOR>(&cursor);
}

HCURSOR SetCursor(HCURSOR cursor) {
    const HCURSOR previous = current_cursor;
    current_cursor = cursor;
    if (!cursor) { SDL_HideCursor(); return previous; }
    if (cursor_shown) SDL_ShowCursor();
    if (auto* shape = reinterpret_cast<Cursor*>(cursor); shape->sdl) SDL_SetCursor(shape->sdl);
    return previous;
}

int ShowCursor(BOOL show) {
    static int count = 0;
    count += show ? 1 : -1;
    lm_window::showCursor(count >= 0);
    return count;
}

BOOL ClipCursor(const RECT*) { return TRUE; }

BOOL TrackMouseEvent(TRACKMOUSEEVENT* event) {
    if (!event) return FALSE;
    if (Window* target = window(event->hwndTrack)) target->track_leave = !(event->dwFlags & TME_CANCEL) && (event->dwFlags & TME_LEAVE);
    return TRUE;
}

UINT GetDoubleClickTime() { return 500; }

HICON LoadIconW(HINSTANCE, const wchar_t*) { return nullptr; }
void* LoadImageW(HINSTANCE, const wchar_t*, UINT, int, int, UINT) { return nullptr; }
BOOL DestroyIcon(HICON) { return TRUE; }

namespace lm_window {
void showCursor(bool shown) {
    cursor_shown = shown;
    if (shown && current_cursor) SDL_ShowCursor(); else if (!shown) SDL_HideCursor();
}
}

// ---- Clipboard -----------------------------------------------------------------------------

namespace {
HGLOBAL clipboard_text = nullptr; // What GetClipboardData last handed out; freed on the next call.
}

BOOL OpenClipboard(HWND) { return TRUE; }
BOOL CloseClipboard() { return TRUE; }
BOOL EmptyClipboard() { return TRUE; }
BOOL IsClipboardFormatAvailable(UINT format) { return (format == CF_UNICODETEXT || format == CF_TEXT) && SDL_HasClipboardText(); }

HGLOBAL GetClipboardData(UINT format) {
    if (format != CF_UNICODETEXT) return nullptr;
    char* text = SDL_GetClipboardText();
    if (!text) return nullptr;
    const auto wide = text_encoding::wideLenient(text);
    SDL_free(text);
    if (clipboard_text) GlobalFree(clipboard_text);
    clipboard_text = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
    if (auto* data = static_cast<wchar_t*>(GlobalLock(clipboard_text))) {
        std::wmemcpy(data, wide.c_str(), wide.size() + 1);
        GlobalUnlock(clipboard_text);
    }
    return clipboard_text;
}

HGLOBAL SetClipboardData(UINT format, HGLOBAL memory) {
    if (format != CF_UNICODETEXT || !memory) return nullptr;
    const auto* text = static_cast<const wchar_t*>(GlobalLock(memory));
    const size_t size = GlobalSize(memory) / sizeof(wchar_t);
    std::wstring value(text ? text : L"", text ? wcsnlen(text, size) : 0);
    GlobalUnlock(memory);
    if (!SDL_SetClipboardText(text_encoding::utf8Lenient(value).c_str())) return nullptr;
    GlobalFree(memory); // The system owns the data now, as Windows takes the handle.
    return memory;
}

// ---- Hot keys, menus, message boxes ---------------------------------------------------------

// Global hot keys belong to the desktop on Linux (Wayland grants none): the Guide key works
// through the launcher's window and the pads.
BOOL RegisterHotKey(HWND, int, UINT, UINT) { return FALSE; }
BOOL UnregisterHotKey(HWND, int) { return TRUE; }

namespace {
struct Menu {
    struct Item {
        UINT_PTR id = 0; // MF_POPUP: the submenu (owned, destroyed with this one, as Windows does).
        std::wstring text;
        UINT flags = 0;
    };
    std::vector<Item> items;
    UINT_PTR default_item = 0;
};
}

HMENU CreatePopupMenu() { return reinterpret_cast<HMENU>(new Menu); }
BOOL AppendMenuW(HMENU menu, UINT flags, UINT_PTR id, const wchar_t* text) {
    if (!menu) return FALSE;
    reinterpret_cast<Menu*>(menu)->items.push_back({id, (flags & MF_SEPARATOR) || !text ? std::wstring{} : std::wstring(text), flags});
    return TRUE;
}
BOOL SetMenuDefaultItem(HMENU menu, UINT item, UINT) {
    if (!menu) return FALSE;
    reinterpret_cast<Menu*>(menu)->default_item = item;
    return TRUE;
}
// Popup menus come only from the notification-area icon, whose host shows the menu handed to it
// (Shell_NotifyIconMenu); an explicit popup chooses nothing.
BOOL TrackPopupMenuEx(HMENU, UINT, int, int, HWND, void*) { return 0; }
BOOL DestroyMenu(HMENU handle) {
    auto* menu = reinterpret_cast<Menu*>(handle);
    if (!menu) return FALSE;
    for (const auto& item : menu->items) if (item.flags & MF_POPUP) DestroyMenu(reinterpret_cast<HMENU>(item.id));
    delete menu;
    return TRUE;
}

int MessageBoxW(HWND owner, const wchar_t* text, const wchar_t* caption, UINT type) {
    const SDL_MessageBoxFlags flags = (type & 0xF0) == MB_ICONERROR ? SDL_MESSAGEBOX_ERROR :
                                      (type & 0xF0) == MB_ICONWARNING ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION;
    SDL_ShowSimpleMessageBox(flags, text_encoding::utf8Lenient(caption ? caption : L"").c_str(),
                             text_encoding::utf8Lenient(text ? text : L"").c_str(), lm_window::sdlWindow(owner));
    return IDOK;
}
BOOL MessageBeep(UINT) { return TRUE; }

// ---- Dropped files -------------------------------------------------------------------------

UINT DragQueryFileW(HDROP drop, UINT index, wchar_t* file, UINT size) {
    const auto* files = reinterpret_cast<const std::vector<std::wstring>*>(drop);
    if (!files) return 0;
    if (index == 0xFFFFFFFF) return static_cast<UINT>(files->size());
    if (index >= files->size()) return 0;
    const auto& path = (*files)[index];
    if (!file) return static_cast<UINT>(path.size());
    const UINT copied = std::min<UINT>(size ? size - 1 : 0, static_cast<UINT>(path.size()));
    std::wmemcpy(file, path.c_str(), copied);
    if (size) file[copied] = L'\0';
    return copied;
}
void DragFinish(HDROP drop) { delete reinterpret_cast<std::vector<std::wstring>*>(drop); }
void DragAcceptFiles(HWND handle, BOOL accept) { if (Window* target = window(handle)) target->accept_files = accept != FALSE; }

// ---- Shell ---------------------------------------------------------------------------------

HINSTANCE ShellExecuteW(HWND, const wchar_t*, const wchar_t* file, const wchar_t* parameters, const wchar_t* directory, int) {
    if (!file || !*file) return reinterpret_cast<HINSTANCE>(static_cast<uintptr_t>(2));
    std::vector<std::string> line{"xdg-open", text_encoding::utf8Lenient(file)};
    if (parameters && *parameters) line = {text_encoding::utf8Lenient(file)};
    if (parameters && *parameters) for (auto& argument : process_spawn::splitArguments(text_encoding::utf8Lenient(parameters))) line.push_back(argument);
    pid_t pid = 0;
    process_spawn::Options options;
    options.search = true; options.session = true;
    if (directory && *directory) options.directory = std::filesystem::path(directory);
    const int error = process_spawn::spawn(line, options, pid);
    if (error) return reinterpret_cast<HINSTANCE>(static_cast<uintptr_t>(error == ENOENT ? 2 : 31));
    process_spawn::reapLater(pid);
    return reinterpret_cast<HINSTANCE>(static_cast<uintptr_t>(33));
}

namespace {
std::filesystem::path home() {
    const char* value = std::getenv("HOME");
    return value && *value ? std::filesystem::path(value) : std::filesystem::path("/");
}
std::filesystem::path xdgBase(const char* variable, const char* fallback) {
    const char* value = std::getenv(variable);
    return value && *value == '/' ? std::filesystem::path(value) : home() / fallback;
}
// XDG_<NAME>_DIR from user-dirs.dirs (what xdg-user-dir reads), else ~/<fallback>.
std::filesystem::path userDirectory(const std::string& name, const char* fallback) {
    std::ifstream dirs(xdgBase("XDG_CONFIG_HOME", ".config") / "user-dirs.dirs");
    const std::string key = "XDG_" + name + "_DIR=\"";
    for (std::string line; std::getline(dirs, line);) {
        if (!line.starts_with(key) || line.size() <= key.size() || line.back() != '"') continue;
        const auto value = line.substr(key.size(), line.size() - key.size() - 1);
        if (value.starts_with("$HOME")) return home() / std::filesystem::path(value.substr(std::min<size_t>(value.size(), 6)));
        if (value.starts_with('/')) return value;
    }
    return home() / fallback;
}
}

HRESULT SHGetKnownFolderPath(REFKNOWNFOLDERID id, DWORD flags, void*, wchar_t** path) {
    if (!path) return E_POINTER;
    *path = nullptr;
    std::filesystem::path folder;
    if (id == FOLDERID_Profile) folder = home();
    else if (id == FOLDERID_Downloads) folder = userDirectory("DOWNLOAD", "Downloads");
    else if (id == FOLDERID_Desktop) folder = userDirectory("DESKTOP", "Desktop");
    else if (id == FOLDERID_Documents) folder = userDirectory("DOCUMENTS", "Documents");
    else if (id == FOLDERID_Music) folder = userDirectory("MUSIC", "Music");
    else if (id == FOLDERID_Pictures) folder = userDirectory("PICTURES", "Pictures");
    else if (id == FOLDERID_Videos) folder = userDirectory("VIDEOS", "Videos");
    else if (id == FOLDERID_LocalAppData) folder = xdgBase("XDG_DATA_HOME", ".local/share");
    else if (id == FOLDERID_RoamingAppData) folder = xdgBase("XDG_CONFIG_HOME", ".config");
    else if (id == FOLDERID_ProgramData) folder = "/var/lib";
    else return E_INVALIDARG;
    if (flags & KF_FLAG_CREATE) { std::error_code error; std::filesystem::create_directories(folder, error); }
    const auto wide = folder.wstring();
    auto* copy = static_cast<wchar_t*>(CoTaskMemAlloc((wide.size() + 1) * sizeof(wchar_t)));
    if (!copy) return E_OUTOFMEMORY;
    std::wmemcpy(copy, wide.c_str(), wide.size() + 1);
    *path = copy;
    return S_OK;
}

// ---- Notification-area icon (SDL tray) ------------------------------------------------------

namespace {
struct TrayIcon {
    SDL_Tray* tray = nullptr;
    HWND window = nullptr;
    UINT callback = 0;
};
TrayIcon tray_icon;

// A chosen entry reaches the window as the icon's callback message: WM_COMMAND, wparam its id.
void trayActivate(void* command, SDL_TrayEntry*) {
    if (!tray_icon.window) return;
    PostMessageW(tray_icon.window, tray_icon.callback, static_cast<WPARAM>(reinterpret_cast<uintptr_t>(command)), MAKELPARAM(WM_COMMAND, 0));
}

// The Win32 menu's entries as the host's (labels as given: the launcher doubles no ampersands here).
void fillTrayMenu(SDL_TrayMenu* target, const Menu& menu) {
    for (const auto& item : menu.items) {
        if (item.flags & MF_SEPARATOR) { SDL_InsertTrayEntryAt(target, -1, nullptr, SDL_TRAYENTRY_BUTTON); continue; }
        const auto label = text_encoding::utf8Lenient(item.text);
        if (item.flags & MF_POPUP) {
            SDL_TrayEntry* entry = SDL_InsertTrayEntryAt(target, -1, label.c_str(), SDL_TRAYENTRY_SUBMENU);
            if (!entry || !item.id) continue;
            if (SDL_TrayMenu* submenu = SDL_CreateTraySubmenu(entry)) fillTrayMenu(submenu, *reinterpret_cast<const Menu*>(item.id));
            continue;
        }
        const SDL_TrayEntryFlags flags = SDL_TRAYENTRY_BUTTON | ((item.flags & (MF_GRAYED | MF_DISABLED)) ? SDL_TRAYENTRY_DISABLED : 0u);
        if (SDL_TrayEntry* entry = SDL_InsertTrayEntryAt(target, -1, label.c_str(), flags))
            SDL_SetTrayEntryCallback(entry, trayActivate, reinterpret_cast<void*>(item.id));
    }
}

void clearTrayMenu(SDL_TrayMenu* menu) {
    int count = 0;
    const SDL_TrayEntry** listed = SDL_GetTrayEntries(menu, &count);
    if (!listed || count <= 0) return;
    // The list is SDL's own and goes stale with the first removal: copied first.
    std::vector<SDL_TrayEntry*> entries;
    for (int i = 0; i < count; ++i) entries.push_back(const_cast<SDL_TrayEntry*>(listed[i]));
    for (auto* entry : entries) SDL_RemoveTrayEntry(entry);
}
}

BOOL Shell_NotifyIconW(DWORD message, NOTIFYICONDATAW* data) {
    if (!data) return FALSE;
    switch (message) {
    case NIM_ADD: {
        if (tray_icon.tray) return FALSE;
        tray_icon.tray = SDL_CreateTray(nullptr, text_encoding::utf8Lenient(data->szTip).c_str());
        if (!tray_icon.tray) return FALSE; // No StatusNotifierItem host (plain X11, gamescope): no icon.
        tray_icon.window = data->hWnd;
        tray_icon.callback = data->uCallbackMessage;
        if (SDL_TrayMenu* menu = SDL_CreateTrayMenu(tray_icon.tray)) {
            const auto add = [&](const char* label, uintptr_t command) {
                if (SDL_TrayEntry* entry = SDL_InsertTrayEntryAt(menu, -1, label, SDL_TRAYENTRY_BUTTON))
                    SDL_SetTrayEntryCallback(entry, trayActivate, reinterpret_cast<void*>(command));
            };
            // Until the launcher hands over its menu (Shell_NotifyIconMenu): show and close, with the
            // ids of src/app/tray_menu.hpp.
            add(text_encoding::utf8(tr(L"Pokaż launchme")).c_str(), 1);
            add(text_encoding::utf8(tr(L"Zamknij launchme")).c_str(), 2);
        }
        return TRUE;
    }
    case NIM_MODIFY:
        if (tray_icon.tray && (data->uFlags & NIF_TIP)) SDL_SetTrayTooltip(tray_icon.tray, text_encoding::utf8Lenient(data->szTip).c_str());
        return tray_icon.tray != nullptr;
    case NIM_DELETE:
        if (tray_icon.tray) SDL_DestroyTray(tray_icon.tray);
        tray_icon = {};
        return TRUE;
    case NIM_SETVERSION: return tray_icon.tray != nullptr;
    default: return FALSE;
    }
}

BOOL Shell_NotifyIconMenu(HMENU handle) {
    const auto* menu = reinterpret_cast<const Menu*>(handle);
    if (!tray_icon.tray || !menu) return FALSE;
    SDL_TrayMenu* target = SDL_GetTrayMenu(tray_icon.tray);
    if (!target) target = SDL_CreateTrayMenu(tray_icon.tray);
    if (!target) return FALSE;
    clearTrayMenu(target);
    fillTrayMenu(target, *menu);
    return TRUE;
}

// ---- GDI: no device contexts on Linux --------------------------------------------------------

HBRUSH CreateSolidBrush(COLORREF) { return nullptr; }
int FillRect(HDC, const RECT*, HBRUSH) { return 0; }
int SetBkMode(HDC, int) { return 0; }
COLORREF SetTextColor(HDC, COLORREF) { return 0; }
HFONT CreateFontW(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, const wchar_t*) { return nullptr; }
int DrawTextW(HDC, const wchar_t*, int, RECT*, UINT) { return 0; }
HGDIOBJ GetStockObject(int) { return nullptr; }
HGDIOBJ SelectObject(HDC, void*) { return nullptr; }
BOOL DeleteObject(void*) { return TRUE; }
int GetObjectW(void*, int, void*) { return 0; }
HGDIOBJ GetCurrentObject(HDC, UINT) { return nullptr; }
BOOL RoundRect(HDC, int, int, int, int, int, int) { return TRUE; }
HDC GetDC(HWND) { return nullptr; }
int ReleaseDC(HWND, HDC) { return 1; }

// ---- The desktop's file dialog --------------------------------------------------------------

namespace lm_window {
namespace {
bool dialog_unavailable = false; // The last pickFile found no portal, Zenity or KDialog.
}
bool fileDialogUnavailable() { return dialog_unavailable; }

std::filesystem::path pickFile(HWND owner, bool folder, const std::vector<FileFilter>& filters, const std::filesystem::path& initial) {
    struct Answer { bool done = false, failed = false; std::filesystem::path path; };
    auto answer = std::make_shared<Answer>();
    auto* keep = new std::shared_ptr<Answer>(answer); // Released by the callback, which SDL always calls once.
    const auto callback = [](void* data, const char* const* list, int) {
        auto* held = static_cast<std::shared_ptr<Answer>*>(data);
        if (list && list[0]) (*held)->path = std::filesystem::path(text_encoding::wideLenient(list[0]));
        (*held)->failed = !list; // An error, not a cancel (an empty list).
        (*held)->done = true;
        delete held;
    };
    std::vector<std::string> names, patterns;
    for (const auto& filter : filters) { names.push_back(filter.name); patterns.push_back(filter.pattern); }
    std::vector<SDL_DialogFileFilter> sdl_filters;
    for (size_t i = 0; i < filters.size(); ++i) sdl_filters.push_back(SDL_DialogFileFilter{names[i].c_str(), patterns[i].c_str()});
    std::error_code error;
    const std::string start = initial.empty() ? std::string{} :
        (std::filesystem::is_directory(initial, error) ? initial : initial.parent_path()).string();
    if (folder) SDL_ShowOpenFolderDialog(callback, keep, sdlWindow(owner), start.empty() ? nullptr : start.c_str(), false);
    else SDL_ShowOpenFileDialog(callback, keep, sdlWindow(owner), sdl_filters.empty() ? nullptr : sdl_filters.data(),
                                static_cast<int>(sdl_filters.size()), start.empty() ? nullptr : start.c_str(), false);
    // Modal, as Windows' dialog is: the launcher keeps painting and its timers run meanwhile.
    while (!answer->done) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { PostQuitMessage(static_cast<int>(message.wParam)); return {}; }
            // Input reaches only the dialog: keys, clicks and closing wait until it is answered.
            if ((message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST) ||
                (message.message >= WM_MOUSEFIRST && message.message <= WM_MOUSELAST) || message.message == WM_CLOSE) continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        waitForInput(0.05);
    }
    dialog_unavailable = answer->failed;
    return answer->path;
}
}
