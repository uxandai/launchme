#pragma once
#include <cstdint>
#include <memory>
#include <atomic>
#include <string>

// Steam's "Uruchamianie…" ("Launching…") window: when a game starts, steamwebhelper shows a small caption window
// (class SDL_app, about 600 x 286) for about three seconds, a moment before the game's own window. This finds it
// and hides it, for the few seconds after launchme started a Steam game (opt-in: AppSettings::steam_hide_launch_window).
namespace steam_launch_window {

struct Candidate {
    std::wstring window_class;
    std::wstring image;      // The owning process's file name, e.g. "steamwebhelper.exe".
    int width = 0, height = 0; // In 96-dpi units (the window's pixels scaled by its DPI): 600 x 286 at any scaling.
    bool visible = true, caption = true; // WS_VISIBLE; WS_CAPTION (the dialog has a title bar).
};

// Whether the window is Steam's launching dialog: a visible SDL_app captioned window of steam.exe or steamwebhelper.exe,
// small (the client's own window is a lot larger). Names compare without case.
bool isLaunchDialog(const Candidate& window);

// Hides every such window now; returns how many it hid. Windows only (elsewhere 0).
int hideNow();

// Hides them as they appear for `milliseconds`, on a thread of its own that ends early when `alive` turns false.
void watch(uint32_t milliseconds, std::shared_ptr<std::atomic<bool>> alive);

}
