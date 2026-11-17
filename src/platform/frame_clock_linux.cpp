// FrameClock on Linux: the display's refresh rate from SDL, and waits that end early on input or a
// posted message (as MsgWaitForMultipleObjectsEx does on Windows).
#include "frame_clock.hpp"
#include "../linux/window/window_system.hpp"

#include <chrono>

namespace {
double seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}
FrameClock::FrameClock() { schedule_.rate(60, seconds()); }
FrameClock::~FrameClock() = default;
void FrameClock::update(HWND window) { update(window, GetForegroundWindow() == window); }
void FrameClock::update(HWND window, bool foreground) {
    const auto now = seconds();
    if (now >= next_probe_) {
        next_probe_ = now + 1;
        refresh_hz_ = lm_window::refreshHz(window);
    }
    const int active = std::clamp(refresh_hz_ ? refresh_hz_ : 60, 30, 240);
    // A visible window keeps its full rate in the background too (the tray, a window beside it):
    // what it shows still moves. Only a minimized or hidden one drops to 5 Hz; behind a game the
    // launcher bounds itself (Launcher::frameLimit, System → Oszczędzaj zasoby za grą).
    (void)foreground;
    schedule_.rate(std::min(limit_, IsIconic(window) || !IsWindowVisible(window) ? 5 : active), now);
}
bool FrameClock::ready() const { return schedule_.ready(seconds()); }
void FrameClock::consume() { schedule_.consume(seconds()); }
void FrameClock::wait() { lm_window::waitForInput(schedule_.delay(seconds())); }
