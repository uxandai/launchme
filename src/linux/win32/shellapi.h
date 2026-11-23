#pragma once
// Linux only: the shell calls the launcher's UI code makes. Dropped files come from SDL's drop
// events; ShellExecuteW opens through the desktop (xdg-open); the notification-area icon is an
// SDL tray where the desktop offers one (StatusNotifierItem), and fails cleanly where none exists.
#include "Windows.h"

#include <algorithm>

// Files dropped on a window (WM_DROPFILES' wparam).
UINT DragQueryFileW(HDROP drop, UINT index, wchar_t* file, UINT size);
void DragFinish(HDROP drop);
void DragAcceptFiles(HWND window, BOOL accept);

HINSTANCE ShellExecuteW(HWND window, const wchar_t* verb, const wchar_t* file, const wchar_t* parameters, const wchar_t* directory, int show);

#define NIM_ADD 0x00000000
#define NIM_MODIFY 0x00000001
#define NIM_DELETE 0x00000002
#define NIM_SETFOCUS 0x00000003
#define NIM_SETVERSION 0x00000004
#define NIF_MESSAGE 0x00000001
#define NIF_ICON 0x00000002
#define NIF_TIP 0x00000004
#define NIF_STATE 0x00000008
#define NIF_INFO 0x00000010
#define NIF_SHOWTIP 0x00000080
#define NOTIFYICON_VERSION_4 4
#define NIN_SELECT (WM_USER + 0)
#define NIN_KEYSELECT (NIN_SELECT | 0x1)
struct NOTIFYICONDATAW {
    DWORD cbSize;
    HWND hWnd;
    UINT uID;
    UINT uFlags;
    UINT uCallbackMessage;
    HICON hIcon;
    wchar_t szTip[128];
    DWORD dwState;
    DWORD dwStateMask;
    wchar_t szInfo[256];
    union { UINT uTimeout; UINT uVersion; };
    wchar_t szInfoTitle[64];
    DWORD dwInfoFlags;
    GUID guidItem;
    HICON hBalloonIcon;
};
// The icon's clicks reach hWnd as uCallbackMessage (NOTIFYICON_VERSION_4: LOWORD(lparam) is
// NIN_SELECT or WM_CONTEXTMENU, wparam the anchor point in screen coordinates).
BOOL Shell_NotifyIconW(DWORD message, NOTIFYICONDATAW* data);
#define NIIF_NONE 0x00000000
#define NIIF_INFO 0x00000001
// Launchme's own: Linux tray hosts show the icon's menu themselves, so it is handed over beforehand
// (copied; the caller still destroys it). A chosen entry reaches hWnd as uCallbackMessage with
// LOWORD(lparam) WM_COMMAND and wparam the entry's id. Balloons (NIF_INFO) are not shown.
BOOL Shell_NotifyIconMenu(HMENU menu);

// MSVC's bounded copies, used next to the shell structures' fixed buffers.
#define _TRUNCATE ((size_t)-1)
template <size_t size>
inline int wcsncpy_s(wchar_t (&destination)[size], const wchar_t* source, size_t count) {
    if (!source) { destination[0] = L'\0'; return 0; }
    const size_t length = std::wcslen(source);
    const size_t copied = std::min(count == _TRUNCATE ? length : std::min(count, length), size - 1);
    std::wmemcpy(destination, source, copied);
    destination[copied] = L'\0';
    return 0;
}
template <size_t size>
inline int wcscpy_s(wchar_t (&destination)[size], const wchar_t* source) { return wcsncpy_s(destination, source, _TRUNCATE); }
