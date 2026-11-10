// Tryb TV's pad cursor: L3 + R3 (Ustawienia → Interfejs → Kursor na padzie) turns the pad into a mouse
// over the launcher. The left stick moves the pointer, R2 clicks (as the left button: press and release),
// L2 goes back, the right stick scrolls. The D-pad and the face buttons keep working as before.
// The pointer goes through the same paths as a real mouse (mouseMove, click, the wheel message), and the
// renderer draws a small pad mark at its bottom right while the mode is on.

#include "launcher.hpp"

#include <windowsx.h>
#include <xinput.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr WORD sticks = XINPUT_GAMEPAD_LEFT_THUMB | XINPUT_GAMEPAD_RIGHT_THUMB;

// A stick's axis past its dead zone, curved: fine aim near the centre, speed at the edge.
float axis(int value) {
    const float f = std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
    const float a = std::abs(f);
    constexpr float dead = 0.18f;
    if (a < dead) return 0.0f;
    const float t = (a - dead) / (1.0f - dead);
    return std::copysign(t * t * (0.35f + 0.65f * t), f);
}

} // namespace

void Launcher::padCursorOff() {
    if (!pad_cursor_.on) return;
    if (pad_cursor_.clicking) pointerUp(true);
    // The pair and the buttons still held stay known: a pair held on past this frame must not turn it on again.
    const bool latched = pad_cursor_.sticks_latched;
    const WORD buttons = pad_cursor_.buttons;
    const ULONGLONG tick = pad_cursor_.tick;
    pad_cursor_ = {};
    pad_cursor_.sticks_latched = latched; pad_cursor_.buttons = buttons; pad_cursor_.tick = tick;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

PadReading Launcher::padCursor(const PadReading& state) {
    const auto now = GetTickCount64();
    const WORD previous = pad_cursor_.buttons;
    const WORD buttons = state.connected ? state.buttons : 0;
    pad_cursor_.buttons = buttons;
    const float dt = pad_cursor_.tick ? std::min(0.05f, static_cast<float>(now - pad_cursor_.tick) / 1000.0f) : 0.0f;
    pad_cursor_.tick = now;
    PadReading rest = state;
    // The pair that toggled the mode stays the cursor's until both are let go (R3 alone opens a case).
    if (pad_cursor_.sticks_latched) {
        if (!(buttons & sticks)) pad_cursor_.sticks_latched = false;
        rest.buttons &= static_cast<WORD>(~sticks);
    }
    // Only over the launcher in Tryb TV: a game in front, its start, the screensaver or the desktop window end it.
    const bool front = overlay_backend_.foreground() == hwnd_;
    if (!settings_.pad_cursor || desktop_mode_ || !front || launchAnimationActive() || gameOverlayOpen()) {
        padCursorOff();
        return rest;
    }
    if (!state.connected || screensaverActive()) return rest;
    RECT client{};
    GetClientRect(hwnd_, &client);
    const float width = static_cast<float>(std::max<LONG>(1, client.right - client.left));
    const float height = static_cast<float>(std::max<LONG>(1, client.bottom - client.top));
#if defined(_WIN32) && !defined(LAUNCHME_UI_TEST)
    // Where the system pointer is: a real mouse moves it too, and the mark follows.
    if (pad_cursor_.on) {
        POINT point{};
        if (GetCursorPos(&point) && ScreenToClient(hwnd_, &point)) {
            const float x = std::clamp(static_cast<float>(point.x), 0.0f, width - 1), y = std::clamp(static_cast<float>(point.y), 0.0f, height - 1);
            if (std::abs(x - pad_cursor_.x) >= 1 || std::abs(y - pad_cursor_.y) >= 1) {
                pad_cursor_.x = x; pad_cursor_.y = y;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
        }
    }
#endif
    // The launcher's own point under the pointer (through the CRT filter's bend when it is on).
    const auto at = [&](float& x, float& y) { x = pad_cursor_.x; y = pad_cursor_.y; crtPoint(x, y); };
    if ((buttons & sticks) == sticks && (previous & sticks) != sticks) {
        pad_cursor_.sticks_latched = true;
        rest.buttons &= static_cast<WORD>(~sticks);
        noteUserInput();
        if (pad_cursor_.on) { padCursorOff(); audio_.navigate(); return rest; }
        pad_cursor_.on = true;
        pad_cursor_.x = width / 2; pad_cursor_.y = height / 2;
#ifdef _WIN32
        // Where the mouse already is, when it is over the launcher.
        POINT point{};
        if (GetCursorPos(&point) && ScreenToClient(hwnd_, &point) && point.x >= 0 && point.y >= 0 && point.x < client.right && point.y < client.bottom) {
            pad_cursor_.x = static_cast<float>(point.x); pad_cursor_.y = static_cast<float>(point.y);
        }
#endif
        pad_cursor_.back_held = state.left_trigger;
        pad_cursor_.clicking = false;
        audio_.navigate();
        float x = 0, y = 0; at(x, y);
        mouseMove(x, y);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (!pad_cursor_.on) return rest;
    // The rest of the pad sees no sticks, no triggers (Glance, Towarzyszka) while the cursor has them.
    rest.x = rest.y = rest.rx = rest.ry = 0;
    rest.left_trigger = false;
    rest.buttons &= static_cast<WORD>(~pad_right_trigger);

    // Left stick: the pointer; full tilt crosses the screen's width in about a second.
    const float sx = axis(state.x), sy = axis(state.y);
    if (sx != 0.0f || sy != 0.0f) {
        const float speed = 1.1f * std::max(width, height * 16.0f / 9.0f);
        const float x = std::clamp(pad_cursor_.x + sx * speed * dt, 0.0f, width - 1);
        const float y = std::clamp(pad_cursor_.y - sy * speed * dt, 0.0f, height - 1);
        if (x != pad_cursor_.x || y != pad_cursor_.y) {
            noteUserInput();
#ifdef _WIN32
#ifndef LAUNCHME_UI_TEST
            POINT point{static_cast<LONG>(std::lround(x)), static_cast<LONG>(std::lround(y))};
            if (ClientToScreen(hwnd_, &point)) SetCursorPos(point.x, point.y); // The system pointer (and its hand) follows.
#endif
#endif
            pad_cursor_.x = x; pad_cursor_.y = y;
            float ux = 0, uy = 0; at(ux, uy);
            mouseMove(ux, uy);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
    // R2: the left button. A second press soon after on the same spot is a double click.
    const bool right_trigger = buttons & pad_right_trigger;
    if (right_trigger && !pad_cursor_.clicking) {
        pad_cursor_.clicking = true;
        noteUserInput(); noteDesktopInput();
        const bool twice = now - pad_cursor_.clicked_at <= GetDoubleClickTime() &&
                           std::abs(pad_cursor_.x - pad_cursor_.click_x) < 8 && std::abs(pad_cursor_.y - pad_cursor_.click_y) < 8;
        pad_cursor_.clicked_at = twice ? 0 : now;
        pad_cursor_.click_x = pad_cursor_.x; pad_cursor_.click_y = pad_cursor_.y;
        float x = 0, y = 0; at(x, y);
        click(x, y, twice);
    } else if (!right_trigger && pad_cursor_.clicking) {
        pad_cursor_.clicking = false;
        float x = 0, y = 0; at(x, y);
        desktopPointerUp(x, y);
        pointerUp(false);
    }
    if (!pad_cursor_.on) return rest; // The click closed something that ended it.
    // L2: back, as B.
    if (state.left_trigger && !pad_cursor_.back_held) { noteUserInput(); back(); }
    pad_cursor_.back_held = state.left_trigger;
    // Right stick: the wheel, about ten notches a second at full tilt, at the pointer.
    const float scroll = axis(state.ry);
    if (scroll == 0.0f) pad_cursor_.wheel = 0;
    else {
        pad_cursor_.wheel += scroll * 1200.0f * dt;
        const int delta = static_cast<int>(pad_cursor_.wheel);
        if (delta) {
            pad_cursor_.wheel -= static_cast<float>(delta);
            noteUserInput();
            POINT point{static_cast<LONG>(std::lround(pad_cursor_.x)), static_cast<LONG>(std::lround(pad_cursor_.y))};
            ClientToScreen(hwnd_, &point);
            PostMessageW(hwnd_, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<short>(delta))), MAKELPARAM(point.x, point.y));
        }
    }
    return rest;
}
