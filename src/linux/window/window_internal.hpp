#pragma once
// Linux only: the SDL3 window layer's shared state (window.cpp, events.cpp, shell.cpp).
#include "window_system.hpp"
#include "../win32/shellapi.h"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lm_window::detail {
struct WindowClass {
    std::wstring name;
    WNDPROC procedure = nullptr;
    UINT style = 0;
    HCURSOR cursor = nullptr;
};


struct Window {
    std::shared_ptr<WindowClass> type;
    WNDPROC procedure = nullptr;
    std::wstring title;
    DWORD style = 0, ex_style = 0;
    LONG_PTR user_data = 0;
    HWND parent = nullptr;
    SDL_Window* sdl = nullptr;
    SDL_WindowID id = 0;
    bool invalid = true, enabled = true, accept_files = false;
    bool custom_frame = false;   // The launcher draws its caption (desktop mode): SDL hit testing.
    bool fullscreen = false;     // WS_POPUP covering a display: SDL full screen on it.
    bool track_leave = false;    // TrackMouseEvent(TME_LEAVE) armed.
    bool nonclient_hover = false;
    bool destroyed = false;
    POINT last_mouse{-1, -1};
    float wheel_x = 0, wheel_y = 0; // Fractions of a notch from smooth scrolling.
};

struct Timer { HWND window; UINT_PTR id; UINT interval; uint64_t due; };

struct State {
    bool started = false;
    bool gamepads = false;       // SDL_INIT_GAMEPAD held by start().
    std::recursive_mutex windows_mutex; // Guards `live` (PostMessageW checks it from other threads).
    std::unordered_set<Window*> live;
    std::unordered_map<SDL_WindowID, Window*> by_id;
    std::unordered_map<std::wstring, std::shared_ptr<WindowClass>> classes;
    std::mutex posted_mutex;
    std::deque<MSG> posted;      // PostMessageW, any thread.
    std::deque<MSG> input;       // Translated SDL events, the UI thread only.
    std::atomic<bool> wake_pending{false};
    Uint32 wake_event = 0;       // SDL user event that wakes waitForInput for posted messages.
    std::vector<Timer> timers;
    bool quit = false; int quit_code = 0;
    std::array<bool, 256> keys{};  // GetKeyState.
    std::array<bool, 256> toggled{};
    HWND capture = nullptr;
    HWND focus = nullptr;
    std::vector<std::wstring> dropping; // Files of a drop in progress.
    HWND drop_target = nullptr;
    std::mutex work_mutex;
    std::vector<std::function<void()>> work; // lm_window::post.
};
State& state();
Window* window(HWND handle);          // nullptr for a stale or foreign handle.
LRESULT send(Window* target, UINT message, WPARAM wparam, LPARAM lparam);
void pushInput(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
void translate(const SDL_Event& event); // events.cpp
float pixelDensity(Window* target);     // Pixels per SDL window coordinate.
float screenDensity();                  // Pixels per SDL global coordinate (the main display's).
void applyPlacement(Window* target);    // Re-evaluates full screen and frame after style changes.
uint64_t now();
}
