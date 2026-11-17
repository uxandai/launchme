#include "frame_clock.hpp"
#ifdef _WIN32
#include <dwmapi.h>
#endif
#include <chrono>
#include <cmath>

namespace {
double seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}
FrameClock::FrameClock() {
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer_) timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    schedule_.rate(60, seconds());
}
FrameClock::~FrameClock() { if (timer_) { CancelWaitableTimer(timer_); CloseHandle(timer_); } }
void FrameClock::update(HWND window) {
    update(window, GetForegroundWindow() == window);
}
void FrameClock::update(HWND window, bool foreground) {
    const auto now = seconds();
    if (now >= next_probe_) {
        next_probe_ = now + 1;
        MONITORINFOEXW monitor{}; monitor.cbSize = sizeof(monitor);
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        refresh_hz_ = 0;
        if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) &&
            EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1)
            refresh_hz_ = static_cast<int>(mode.dmDisplayFrequency);
        if (!refresh_hz_) {
            DWM_TIMING_INFO timing{}; timing.cbSize = sizeof(timing);
            if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &timing)) && timing.rateRefresh.uiDenominator)
                refresh_hz_ = static_cast<int>(std::round(static_cast<double>(timing.rateRefresh.uiNumerator) / timing.rateRefresh.uiDenominator));
        }
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
void FrameClock::wait() {
    const auto delay = schedule_.delay(seconds());
    if (timer_) {
        LARGE_INTEGER due{}; due.QuadPart = -std::max<LONGLONG>(1, static_cast<LONGLONG>(delay * 10000000));
        if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
            MsgWaitForMultipleObjectsEx(1, &timer_, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            return;
        }
    }
    MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(std::ceil(delay * 1000)), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
}
