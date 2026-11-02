#include "steam_launch_window.hpp"

#include <algorithm>
#include <cwctype>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace steam_launch_window {

namespace {
bool sameText(const std::wstring& a, const wchar_t* b) {
    const std::wstring other = b;
    return a.size() == other.size() && std::equal(a.begin(), a.end(), other.begin(), [](wchar_t x, wchar_t y) { return std::towlower(x) == std::towlower(y); });
}
}

bool isLaunchDialog(const Candidate& window) {
    if (!window.visible || !window.caption || !sameText(window.window_class, L"SDL_app")) return false;
    if (!sameText(window.image, L"steamwebhelper.exe") && !sameText(window.image, L"steam.exe")) return false;
    return window.width >= 300 && window.width <= 900 && window.height >= 150 && window.height <= 500;
}

#ifdef _WIN32
int hideNow() {
    struct Context { int hidden = 0; };
    Context context;
    EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
        if (!IsWindowVisible(window)) return TRUE;
        wchar_t window_class[64]{};
        GetClassNameW(window, window_class, 64);
        if (std::wstring_view(window_class) != L"SDL_app") return TRUE; // Cheap checks first: this runs every few milliseconds.
        Candidate candidate;
        candidate.window_class = window_class;
        RECT rect{};
        GetWindowRect(window, &rect);
        // In 96-dpi units: at 250% scaling the 600 x 286 dialog measures 1500 x 715 pixels.
        // The window's own DPI; a window that reports none (96 on a scaled screen) is measured by the system's.
        const auto measure = [&](UINT dpi) {
            dpi = std::max<UINT>(96, dpi);
            candidate.width = static_cast<int>((rect.right - rect.left) * 96LL / dpi);
            candidate.height = static_cast<int>((rect.bottom - rect.top) * 96LL / dpi);
        };
        measure(GetDpiForWindow(window));
        candidate.visible = true;
        candidate.caption = (GetWindowLongW(window, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        if (const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
            wchar_t path[MAX_PATH]{};
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(process, 0, path, &size)) {
                const std::wstring_view full(path, size);
                const auto slash = full.find_last_of(L"\\/");
                candidate.image = std::wstring(slash == full.npos ? full : full.substr(slash + 1));
            }
            CloseHandle(process);
        }
        if (!isLaunchDialog(candidate)) { measure(GetDpiForSystem()); if (!isLaunchDialog(candidate)) return TRUE; }
        // Asynchronous: Steam's own window procedure is never waited for. No activation, so no focus moves either.
        SetWindowPos(window, nullptr, 0, 0, 0, 0,
                     SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
        ++reinterpret_cast<Context*>(parameter)->hidden;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    return context.hidden;
}

void watch(uint32_t milliseconds, std::shared_ptr<std::atomic<bool>> alive) {
    std::thread([milliseconds, alive = std::move(alive)] {
        const ULONGLONG end = GetTickCount64() + milliseconds;
        while (GetTickCount64() < end && (!alive || alive->load())) {
            hideNow();
            Sleep(30);
        }
    }).detach();
}
#else
int hideNow() { return 0; }
void watch(uint32_t, std::shared_ptr<std::atomic<bool>>) {}
#endif

}
