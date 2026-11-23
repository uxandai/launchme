#pragma once
// Linux only: the window manager calls the launcher's UI code makes (user32), on SDL3
// (src/linux/window). One SDL window per top-level window; messages, timers, painting, input
// state, the clipboard and the cursor behave as the launcher expects from Windows.
#include "win32_base.hpp"

using WNDPROC = LRESULT (*)(HWND, UINT, WPARAM, LPARAM);
using TIMERPROC = void (*)(HWND, UINT, UINT_PTR, DWORD);

// ---- Messages ------------------------------------------------------------------------------
#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ENABLE 0x000A
#define WM_SETTEXT 0x000C
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUERYENDSESSION 0x0011
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SHOWWINDOW 0x0018
#define WM_ACTIVATEAPP 0x001C
#define WM_SETCURSOR 0x0020
#define WM_MOUSEACTIVATE 0x0021
#define WM_GETMINMAXINFO 0x0024
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_COPYDATA 0x004A
#define WM_CONTEXTMENU 0x007B
#define WM_DISPLAYCHANGE 0x007E
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCCALCSIZE 0x0083
#define WM_NCHITTEST 0x0084
#define WM_NCMOUSEMOVE 0x00A0
#define WM_NCLBUTTONDOWN 0x00A1
#define WM_NCLBUTTONUP 0x00A2
#define WM_NCLBUTTONDBLCLK 0x00A3
#define WM_NCMOUSELEAVE 0x02A2
#define WM_INPUT_DEVICE_CHANGE 0x00FE
#define WM_INPUT 0x00FF
#define WM_KEYFIRST 0x0100
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_DEADCHAR 0x0103
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_SYSDEADCHAR 0x0107
#define WM_UNICHAR 0x0109
#define WM_KEYLAST 0x0109
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_MOUSEFIRST 0x0200
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_MOUSEWHEEL 0x020A
#define WM_XBUTTONDOWN 0x020B
#define WM_XBUTTONUP 0x020C
#define WM_XBUTTONDBLCLK 0x020D
#define WM_MOUSEHWHEEL 0x020E
#define WM_MOUSELAST 0x020E
#define WM_CAPTURECHANGED 0x0215
#define WM_POWERBROADCAST 0x0218
#define WM_DEVICECHANGE 0x0219
#define WM_ENTERSIZEMOVE 0x0231
#define WM_EXITSIZEMOVE 0x0232
#define WM_DROPFILES 0x0233
#define WM_MOUSELEAVE 0x02A3
#define WM_DPICHANGED 0x02E0
#define WM_PASTE 0x0302
#define WM_HOTKEY 0x0312
#define WM_ENDSESSION 0x0016
#define WM_USER 0x0400
#define WM_APP 0x8000

#define WA_INACTIVE 0
#define WA_ACTIVE 1
#define WA_CLICKACTIVE 2
#define SIZE_RESTORED 0
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2
#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_SHIFT 0x0004
#define MK_CONTROL 0x0008
#define MK_MBUTTON 0x0010
#define MK_XBUTTON1 0x0020
#define MK_XBUTTON2 0x0040
#define XBUTTON1 0x0001
#define XBUTTON2 0x0002
#define WHEEL_DELTA 120
#define GET_WHEEL_DELTA_WPARAM(w) ((short)HIWORD(w))
#define GET_KEYSTATE_WPARAM(w) (LOWORD(w))
#define GET_XBUTTON_WPARAM(w) (HIWORD(w))
#define PM_NOREMOVE 0x0000
#define PM_REMOVE 0x0001
#define PM_NOYIELD 0x0002
#define SC_SIZE 0xF000
#define SC_MOVE 0xF010
#define SC_MINIMIZE 0xF020
#define SC_MAXIMIZE 0xF030
#define SC_CLOSE 0xF060
#define SC_KEYMENU 0xF100
#define SC_RESTORE 0xF120
#define PBT_APMSUSPEND 0x0004
#define PBT_APMRESUMEAUTOMATIC 0x0012
#define PBT_POWERSETTINGCHANGE 0x8013
#define MA_ACTIVATE 1
#define MA_NOACTIVATE 3

// Hit-test results (WM_NCHITTEST).
#define HTERROR (-2)
#define HTTRANSPARENT (-1)
#define HTNOWHERE 0
#define HTCLIENT 1
#define HTCAPTION 2
#define HTSYSMENU 3
#define HTMINBUTTON 8
#define HTMAXBUTTON 9
#define HTLEFT 10
#define HTRIGHT 11
#define HTTOP 12
#define HTTOPLEFT 13
#define HTTOPRIGHT 14
#define HTBOTTOM 15
#define HTBOTTOMLEFT 16
#define HTBOTTOMRIGHT 17
#define HTBORDER 18
#define HTCLOSE 20

// ---- Virtual keys --------------------------------------------------------------------------
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SELECT 0x29
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_HELP 0x2F
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D
#define VK_SLEEP 0x5F
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_BROWSER_BACK 0xA6
#define VK_BROWSER_FORWARD 0xA7
#define VK_VOLUME_MUTE 0xAD
#define VK_VOLUME_DOWN 0xAE
#define VK_VOLUME_UP 0xAF
#define VK_MEDIA_NEXT_TRACK 0xB0
#define VK_MEDIA_PREV_TRACK 0xB1
#define VK_MEDIA_STOP 0xB2
#define VK_MEDIA_PLAY_PAUSE 0xB3
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_OEM_8 0xDF
#define VK_OEM_102 0xE2
#define VK_GAMEPAD_A 0xC3
#define VK_GAMEPAD_B 0xC4
#define MAPVK_VK_TO_VSC 0
#define MAPVK_VSC_TO_VK 1
#define MAPVK_VK_TO_CHAR 2

// ---- Windows -------------------------------------------------------------------------------
#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP 0x80000000L
#define WS_CHILD 0x40000000L
#define WS_MINIMIZE 0x20000000L
#define WS_VISIBLE 0x10000000L
#define WS_DISABLED 0x08000000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_MAXIMIZE 0x01000000L
#define WS_CAPTION 0x00C00000L
#define WS_BORDER 0x00800000L
#define WS_DLGFRAME 0x00400000L
#define WS_SYSMENU 0x00080000L
#define WS_THICKFRAME 0x00040000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_EX_TOPMOST 0x00000008L
#define WS_EX_ACCEPTFILES 0x00000010L
#define WS_EX_TRANSPARENT 0x00000020L
#define WS_EX_TOOLWINDOW 0x00000080L
#define WS_EX_APPWINDOW 0x00040000L
#define WS_EX_LAYERED 0x00080000
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#define WS_EX_NOACTIVATE 0x08000000L
#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define CS_DBLCLKS 0x0008
#define CW_USEDEFAULT ((int)0x80000000)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWLP_WNDPROC (-4)
#define GWLP_HINSTANCE (-6)
#define GWLP_USERDATA (-21)
#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_NORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_MAXIMIZE 3
#define SW_SHOWNOACTIVATE 4
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_SHOWMINNOACTIVE 7
#define SW_SHOWNA 8
#define SW_RESTORE 9
#define SW_SHOWDEFAULT 10
#define SW_FORCEMINIMIZE 11
#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOREDRAW 0x0008
#define SWP_NOACTIVATE 0x0010
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW 0x0040
#define SWP_HIDEWINDOW 0x0080
#define SWP_NOOWNERZORDER 0x0200
#define SWP_ASYNCWINDOWPOS 0x4000
#define HWND_TOP ((HWND)0)
#define HWND_BOTTOM ((HWND)1)
#define HWND_TOPMOST ((HWND)-1)
#define HWND_NOTOPMOST ((HWND)-2)
#define HWND_MESSAGE ((HWND)-3)
#define RDW_INVALIDATE 0x0001
#define RDW_INTERNALPAINT 0x0002
#define RDW_ERASE 0x0004
#define RDW_NOERASE 0x0020
#define RDW_ALLCHILDREN 0x0080
#define RDW_UPDATENOW 0x0100
#define RDW_FRAME 0x0400
#define TME_LEAVE 0x00000002
#define TME_NONCLIENT 0x00000010
#define TME_CANCEL 0x80000000

struct MSG { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; };
using LPMSG = MSG*;
struct WNDCLASSW {
    UINT style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra; HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor;
    HBRUSH hbrBackground; const wchar_t* lpszMenuName; const wchar_t* lpszClassName;
};
struct WNDCLASSEXW {
    UINT cbSize; UINT style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra; HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor;
    HBRUSH hbrBackground; const wchar_t* lpszMenuName; const wchar_t* lpszClassName; HICON hIconSm;
};
struct CREATESTRUCTW {
    LPVOID lpCreateParams; HINSTANCE hInstance; HMENU hMenu; HWND hwndParent; int cy, cx, y, x; LONG style;
    const wchar_t* lpszName; const wchar_t* lpszClass; DWORD dwExStyle;
};
struct MINMAXINFO { POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize; };
struct WINDOWPOS { HWND hwnd, hwndInsertAfter; int x, y, cx, cy; UINT flags; };
struct NCCALCSIZE_PARAMS { RECT rgrc[3]; WINDOWPOS* lppos; };
struct PAINTSTRUCT { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore, fIncUpdate; BYTE rgbReserved[32]; };
struct TRACKMOUSEEVENT { DWORD cbSize; DWORD dwFlags; HWND hwndTrack; DWORD dwHoverTime; };
struct COPYDATASTRUCT { ULONG_PTR dwData; DWORD cbData; PVOID lpData; };
struct WINDOWPLACEMENT { UINT length, flags, showCmd; POINT ptMinPosition, ptMaxPosition; RECT rcNormalPosition; RECT rcDevice; };
#define WPF_RESTORETOMAXIMIZED 0x0002
struct POWERBROADCAST_SETTING { GUID PowerSetting; DWORD DataLength; BYTE Data[1]; };

// ---- Monitors ------------------------------------------------------------------------------
#define MONITOR_DEFAULTTONULL 0x00000000
#define MONITOR_DEFAULTTOPRIMARY 0x00000001
#define MONITOR_DEFAULTTONEAREST 0x00000002
#define MONITORINFOF_PRIMARY 0x00000001
#define CCHDEVICENAME 32
struct MONITORINFO { DWORD cbSize; RECT rcMonitor; RECT rcWork; DWORD dwFlags; };
struct MONITORINFOEXW { DWORD cbSize; RECT rcMonitor; RECT rcWork; DWORD dwFlags; wchar_t szDevice[CCHDEVICENAME]; };
using MONITORENUMPROC = BOOL (*)(HMONITOR, HDC, RECT*, LPARAM);

#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXVSCROLL 2
#define SM_CXICON 11
#define SM_CYICON 12
#define SM_CXFRAME 32
#define SM_CYFRAME 33
#define SM_CXSMICON 49
#define SM_CYSMICON 50
#define SM_CXVIRTUALSCREEN 78
#define SM_CYVIRTUALSCREEN 79
#define SM_CMONITORS 80
#define SM_CXPADDEDBORDER 92
#define SM_CXDOUBLECLK 36
#define SM_CYDOUBLECLK 37
#define SM_CXDRAG 68
#define SM_CYDRAG 69
#define USER_DEFAULT_SCREEN_DPI 96

// ---- Cursors, icons, menus, clipboard, message boxes ---------------------------------------
#define IDC_ARROW ((const wchar_t*)32512)
#define IDC_IBEAM ((const wchar_t*)32513)
#define IDC_WAIT ((const wchar_t*)32514)
#define IDC_CROSS ((const wchar_t*)32515)
#define IDC_SIZENWSE ((const wchar_t*)32642)
#define IDC_SIZENESW ((const wchar_t*)32643)
#define IDC_SIZEWE ((const wchar_t*)32644)
#define IDC_SIZENS ((const wchar_t*)32645)
#define IDC_SIZEALL ((const wchar_t*)32646)
#define IDC_NO ((const wchar_t*)32648)
#define IDC_HAND ((const wchar_t*)32649)
#define IDC_APPSTARTING ((const wchar_t*)32650)
#define IDI_APPLICATION ((const wchar_t*)32512)
#define MAKEINTRESOURCEW(i) ((LPWSTR)((ULONG_PTR)((WORD)(i))))
#define IS_INTRESOURCE(r) ((((ULONG_PTR)(r)) >> 16) == 0)
#define IMAGE_ICON 1
#define IMAGE_CURSOR 2
#define LR_DEFAULTCOLOR 0x00000000
#define LR_DEFAULTSIZE 0x00000040
#define LR_SHARED 0x00008000

#define MF_STRING 0x00000000L
#define MF_GRAYED 0x00000001L
#define MF_DISABLED 0x00000002L
#define MF_CHECKED 0x00000008L
#define MF_POPUP 0x00000010L
#define MF_SEPARATOR 0x00000800L
#define MF_BYCOMMAND 0x00000000L
#define TPM_LEFTALIGN 0x0000L
#define TPM_RIGHTBUTTON 0x0002L
#define TPM_BOTTOMALIGN 0x0020L
#define TPM_NONOTIFY 0x0080L
#define TPM_RETURNCMD 0x0100L

#define CF_TEXT 1
#define CF_UNICODETEXT 13
#define CF_HDROP 15

#define MB_OK 0x00000000L
#define MB_OKCANCEL 0x00000001L
#define MB_YESNO 0x00000004L
#define MB_ICONERROR 0x00000010L
#define MB_ICONWARNING 0x00000030L
#define MB_ICONINFORMATION 0x00000040L
#define MB_TOPMOST 0x00040000L
#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7

#define MOD_ALT 0x0001
#define MOD_CONTROL 0x0002
#define MOD_SHIFT 0x0004
#define MOD_WIN 0x0008
#define MOD_NOREPEAT 0x4000

// ---- Functions -----------------------------------------------------------------------------
ATOM RegisterClassW(const WNDCLASSW* window_class);
ATOM RegisterClassExW(const WNDCLASSEXW* window_class);
BOOL UnregisterClassW(const wchar_t* name, HINSTANCE instance);
HWND CreateWindowExW(DWORD ex_style, const wchar_t* class_name, const wchar_t* title, DWORD style, int x, int y, int width, int height,
                     HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter);
inline HWND CreateWindowW(const wchar_t* class_name, const wchar_t* title, DWORD style, int x, int y, int width, int height,
                          HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter) {
    return CreateWindowExW(0, class_name, title, style, x, y, width, height, parent, menu, instance, parameter);
}
BOOL DestroyWindow(HWND window);
LRESULT DefWindowProcW(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT CallWindowProcW(WNDPROC procedure, HWND window, UINT message, WPARAM wparam, LPARAM lparam);
BOOL ShowWindow(HWND window, int command);
BOOL UpdateWindow(HWND window);
BOOL SetWindowPos(HWND window, HWND insert_after, int x, int y, int width, int height, UINT flags);
BOOL MoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint);
BOOL GetWindowRect(HWND window, RECT* rect);
BOOL GetClientRect(HWND window, RECT* rect);
BOOL AdjustWindowRectEx(RECT* rect, DWORD style, BOOL menu, DWORD ex_style);
BOOL AdjustWindowRectExForDpi(RECT* rect, DWORD style, BOOL menu, DWORD ex_style, UINT dpi);
BOOL GetWindowPlacement(HWND window, WINDOWPLACEMENT* placement);
BOOL SetWindowPlacement(HWND window, const WINDOWPLACEMENT* placement);
BOOL ClientToScreen(HWND window, POINT* point);
BOOL ScreenToClient(HWND window, POINT* point);
BOOL IsWindow(HWND window);
BOOL IsWindowVisible(HWND window);
BOOL IsIconic(HWND window);
BOOL IsZoomed(HWND window);
BOOL IsChild(HWND parent, HWND window);
BOOL IsWindowEnabled(HWND window);
BOOL EnableWindow(HWND window, BOOL enable);
HWND SetFocus(HWND window);
HWND GetFocus();
HWND GetActiveWindow();
HWND GetForegroundWindow();
BOOL SetForegroundWindow(HWND window);
BOOL BringWindowToTop(HWND window);
BOOL AllowSetForegroundWindow(DWORD process_id);
#define ASFW_ANY ((DWORD)-1)
BOOL FlashWindow(HWND window, BOOL invert);
HWND GetParent(HWND window);
HWND GetAncestor(HWND window, UINT flags);
#define GA_ROOT 2
LONG_PTR GetWindowLongPtrW(HWND window, int index);
LONG_PTR SetWindowLongPtrW(HWND window, int index, LONG_PTR value);
inline LONG GetWindowLongW(HWND window, int index) { return static_cast<LONG>(GetWindowLongPtrW(window, index)); }
inline LONG SetWindowLongW(HWND window, int index, LONG value) { return static_cast<LONG>(SetWindowLongPtrW(window, index, value)); }
BOOL SetWindowTextW(HWND window, const wchar_t* text);
int GetWindowTextW(HWND window, wchar_t* text, int size);
int GetClassNameW(HWND window, wchar_t* name, int size);
HWND FindWindowW(const wchar_t* class_name, const wchar_t* title);
DWORD GetWindowThreadProcessId(HWND window, DWORD* process_id);
BOOL AttachThreadInput(DWORD attach, DWORD attach_to, BOOL attached);
HWND WindowFromPoint(POINT point);

BOOL PostMessageW(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT SendMessageW(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
#define SMTO_ABORTIFHUNG 0x0002
LRESULT SendMessageTimeoutW(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT flags, UINT timeout, DWORD_PTR* result);
BOOL PostThreadMessageW(DWORD thread, UINT message, WPARAM wparam, LPARAM lparam);
BOOL PeekMessageW(MSG* message, HWND window, UINT first, UINT last, UINT remove);
BOOL GetMessageW(MSG* message, HWND window, UINT first, UINT last);
BOOL TranslateMessage(const MSG* message);
LRESULT DispatchMessageW(const MSG* message);
void PostQuitMessage(int exit_code);
UINT RegisterWindowMessageW(const wchar_t* name);
DWORD GetMessageTime();

BOOL InvalidateRect(HWND window, const RECT* rect, BOOL erase);
BOOL ValidateRect(HWND window, const RECT* rect);
BOOL RedrawWindow(HWND window, const RECT* rect, void* region, UINT flags);
HDC BeginPaint(HWND window, PAINTSTRUCT* paint);
BOOL EndPaint(HWND window, const PAINTSTRUCT* paint);
BOOL GetUpdateRect(HWND window, RECT* rect, BOOL erase);

UINT_PTR SetTimer(HWND window, UINT_PTR id, UINT elapse, TIMERPROC procedure);
BOOL KillTimer(HWND window, UINT_PTR id);

SHORT GetKeyState(int key);
SHORT GetAsyncKeyState(int key);
UINT MapVirtualKeyW(UINT code, UINT type);
HWND SetCapture(HWND window);
BOOL ReleaseCapture();
HWND GetCapture();
BOOL GetCursorPos(POINT* point);
BOOL SetCursorPos(int x, int y);
HCURSOR SetCursor(HCURSOR cursor);
HCURSOR LoadCursorW(HINSTANCE instance, const wchar_t* name);
int ShowCursor(BOOL show);
BOOL ClipCursor(const RECT* rect);
BOOL TrackMouseEvent(TRACKMOUSEEVENT* event);
UINT GetDoubleClickTime();
HICON LoadIconW(HINSTANCE instance, const wchar_t* name);
void* LoadImageW(HINSTANCE instance, const wchar_t* name, UINT type, int width, int height, UINT flags);
BOOL DestroyIcon(HICON icon);

int GetSystemMetrics(int index);
int GetSystemMetricsForDpi(int index, UINT dpi);
UINT GetDpiForWindow(HWND window);
UINT GetDpiForSystem();
HMONITOR MonitorFromWindow(HWND window, DWORD flags);
HMONITOR MonitorFromRect(const RECT* rect, DWORD flags);
HMONITOR MonitorFromPoint(POINT point, DWORD flags);
BOOL GetMonitorInfoW(HMONITOR monitor, MONITORINFO* info);
inline BOOL GetMonitorInfoW(HMONITOR monitor, MONITORINFOEXW* info) { return GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(info)); }
BOOL EnumDisplayMonitors(HDC dc, const RECT* clip, MONITORENUMPROC callback, LPARAM data);

BOOL OpenClipboard(HWND owner);
BOOL CloseClipboard();
BOOL EmptyClipboard();
// CF_UNICODETEXT: the system clipboard's text as a NUL-terminated wchar_t block.
HGLOBAL GetClipboardData(UINT format);
HGLOBAL SetClipboardData(UINT format, HGLOBAL memory);
BOOL IsClipboardFormatAvailable(UINT format);

BOOL RegisterHotKey(HWND window, int id, UINT modifiers, UINT key);
BOOL UnregisterHotKey(HWND window, int id);

HMENU CreatePopupMenu();
BOOL AppendMenuW(HMENU menu, UINT flags, UINT_PTR id, const wchar_t* text);
BOOL SetMenuDefaultItem(HMENU menu, UINT item, UINT by_position);
BOOL TrackPopupMenuEx(HMENU menu, UINT flags, int x, int y, HWND window, void* parameters);
BOOL DestroyMenu(HMENU menu);

int MessageBoxW(HWND owner, const wchar_t* text, const wchar_t* caption, UINT type);
BOOL MessageBeep(UINT type);

// Rectangles.
inline BOOL SetRect(RECT* r, int left, int top, int right, int bottom) { *r = RECT{left, top, right, bottom}; return TRUE; }
inline BOOL SetRectEmpty(RECT* r) { *r = RECT{}; return TRUE; }
inline BOOL IsRectEmpty(const RECT* r) { return r->right <= r->left || r->bottom <= r->top; }
inline BOOL PtInRect(const RECT* r, POINT p) { return p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom; }
inline BOOL EqualRect(const RECT* a, const RECT* b) { return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom; }
inline BOOL OffsetRect(RECT* r, int dx, int dy) { r->left += dx; r->right += dx; r->top += dy; r->bottom += dy; return TRUE; }
inline BOOL InflateRect(RECT* r, int dx, int dy) { r->left -= dx; r->right += dx; r->top -= dy; r->bottom += dy; return TRUE; }
BOOL IntersectRect(RECT* result, const RECT* a, const RECT* b);
BOOL UnionRect(RECT* result, const RECT* a, const RECT* b);

// GDI: the window layer has no device contexts; these exist for the rare GDI fallback paint and
// do nothing visible (Direct2D draws everything on Linux).
#define TRANSPARENT 1
#define OPAQUE 2
#define BLACK_BRUSH 4
#define DEFAULT_GUI_FONT 17
#define DT_LEFT 0x00000000
#define DT_CENTER 0x00000001
#define DT_VCENTER 0x00000004
#define DT_WORDBREAK 0x00000010
#define DT_SINGLELINE 0x00000020
#define DT_NOPREFIX 0x00000800
#define DT_END_ELLIPSIS 0x00008000
#define OBJ_FONT 6
#define FW_NORMAL 400
#define FW_SEMIBOLD 600
#define FW_BOLD 700
#define DEFAULT_CHARSET 1
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define CLEARTYPE_QUALITY 5
#define DEFAULT_PITCH 0
#define FF_DONTCARE 0
HBRUSH CreateSolidBrush(COLORREF color);
int FillRect(HDC dc, const RECT* rect, HBRUSH brush);
int SetBkMode(HDC dc, int mode);
COLORREF SetTextColor(HDC dc, COLORREF color);
HFONT CreateFontW(int height, int width, int escapement, int orientation, int weight, DWORD italic, DWORD underline, DWORD strike,
                  DWORD charset, DWORD out_precision, DWORD clip_precision, DWORD quality, DWORD pitch, const wchar_t* face);
int DrawTextW(HDC dc, const wchar_t* text, int length, RECT* rect, UINT format);
HGDIOBJ GetStockObject(int object);
HGDIOBJ SelectObject(HDC dc, void* object);
BOOL DeleteObject(void* object);
int GetObjectW(void* object, int size, void* buffer);
HGDIOBJ GetCurrentObject(HDC dc, UINT type);
BOOL RoundRect(HDC dc, int left, int top, int right, int bottom, int width, int height);
HDC GetDC(HWND window);
int ReleaseDC(HWND window, HDC dc);
