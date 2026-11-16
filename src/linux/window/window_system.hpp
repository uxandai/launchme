#pragma once
// Linux only: what the launcher's Linux-specific code (main.cpp's Linux branch, frame_clock_linux,
// file dialogs, the browser window) asks of the SDL3 window layer beyond the Win32 surface.
#include "../win32/Windows.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;

namespace lm_window {
// SDL's video, events and gamepads; false with SDL's error in `error`. Once, on the main thread.
bool start(std::string& error);
void stop();
// The window layer's (SDL's) last error, for a start that failed.
std::string lastError();
// The window's display refresh rate in whole Hz, rounded up (0: unknown).
int refreshHz(HWND window);
// Sleeps until an event, a posted message or a timer is due, at most `seconds`.
void waitForInput(double seconds);
// The SDL window behind an HWND (nullptr for none): for SDL's own dialogs.
SDL_Window* sdlWindow(HWND window);
// The desktop's primary window for files and folders (SDL_ShowOpenFileDialog, a portal on
// Wayland), run modally: the event loop keeps running until the user answers. Empty: cancelled.
struct FileFilter { std::string name, pattern; }; // pattern: "png;jpg" or "*".
std::filesystem::path pickFile(HWND owner, bool folder, const std::vector<FileFilter>& filters, const std::filesystem::path& initial);
// The last pickFile could not show a dialog at all (no portal, Zenity or KDialog): the launcher's own
// file browser stands in.
bool fileDialogUnavailable();
// Runs `work` on the UI thread from the event loop (another thread's request).
void post(std::function<void()> work);
// Mouse cursor shown over the window: false hides it.
void showCursor(bool shown);
// The window title bar is drawn by the launcher (desktop mode): which part of a point is the
// caption, a resize edge or the client area (HT* values), from WM_NCHITTEST.
void enableCustomFrame(HWND window, bool enabled);
}
