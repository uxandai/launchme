#include "../src/app/hold_gesture.hpp"
#include "../src/app/pad_key_echo.hpp"
#include "../src/ui/power_menu.hpp"
#ifdef _WIN32
#include <Windows.h> // xinput.h needs it first.
#endif
#include <xinput.h>

// Win32 virtual-key codes (tests/linux_compat has no VK_ names; on Windows those are macros).
namespace {
constexpr uintptr_t key_return = 0x0D, key_up = 0x26, key_down = 0x28, key_f5 = 0x74;
}
#include <iostream>

int main() {
    using Event = HoldGesture::Event;
    HoldGesture gesture;
    // Short press: nothing on the way down, press on release.
    if (gesture.update(true, 1000) != Event::none || gesture.update(true, 1200) != Event::none ||
        gesture.update(false, 1300) != Event::press) return 1;
    // Hold: fires once at the threshold while still down; the release is silent.
    if (gesture.update(true, 2000) != Event::none || gesture.update(true, 2479) != Event::none ||
        gesture.update(true, 2480) != Event::hold || gesture.update(true, 3000) != Event::none ||
        gesture.update(false, 3100) != Event::none) return 2;
    // Idle polling produces nothing.
    if (gesture.update(false, 4000) != Event::none) return 3;
    // A button already down at reconnect/refocus never fires on its release...
    gesture.swallow(true);
    if (gesture.update(true, 5000) != Event::none || gesture.update(false, 5100) != Event::none) return 4;
    // ...and the next real press works normally.
    if (gesture.update(true, 6000) != Event::none || gesture.update(false, 6050) != Event::press) return 5;

    // Steam's echo of a pad as keys (Linux, Steam running): dropped while the pad holds a button or a
    // direction and just after a press; real keys alone, typing and keys pads never send pass.
    {
        PadKeyEcho echo;
        echo.pad(true, XINPUT_GAMEPAD_DPAD_DOWN, 0, 0, false, 10000);
        if (echo.swallow(key_down, 10010)) return 30; // Off without Steam.
        echo.enable(true);
        if (!echo.swallow(key_down, 10010) || !echo.swallow(key_down, 10500)) return 31; // Held: the press and its repeats.
        echo.pad(true, 0, 0, 0, false, 10600);
        echo.pad(true, XINPUT_GAMEPAD_A, 0, 0, false, 11000); // A tap: Steam's key may come after the pad already let go.
        echo.pad(true, 0, 0, 0, false, 11050);
        if (!echo.swallow(key_return, 11100) || echo.swallow(key_return, 11000 + PadKeyEcho::window + 1)) return 32; // Only just after the press.
        if (echo.swallow('A', 10010) || echo.swallow(key_f5, 10010)) return 33; // Not a key a pad sends.
        echo.pad(true, 0, 0, -30000, false, 20000); // The stick down.
        if (!echo.swallow(key_down, 20010)) return 34;
        echo.pad(true, 0, 0, 0, false, 20100);
        if (echo.swallow(key_up, 21000)) return 35; // A keyboard later: its own.
        echo.pad(false, XINPUT_GAMEPAD_A, 0, 0, false, 30000); // A pad gone holds nothing.
        if (echo.swallow(key_return, 31000)) return 36;
    }

    // Power menu: Tryb TV ⇄ the desktop window first, then leaving the launcher, the offered system
    // actions (red, a group of their own), Cancel last (apart again).
    const auto rows = power_menu::items({PowerAction::sleep, PowerAction::restart, PowerAction::shutdown});
    if (rows.size() != 6 || rows[0] != power_menu::Item::tv_mode || rows[1] != power_menu::Item::exit || rows[2] != power_menu::Item::sleep ||
        rows[3] != power_menu::Item::restart || rows.back() != power_menu::Item::cancel) return 6;
    if (power_menu::gaps(rows) != std::vector<bool>{false, false, true, false, false, true} ||
        !power_menu::danger(power_menu::Item::sleep) || !power_menu::danger(power_menu::Item::shutdown) || power_menu::danger(power_menu::Item::exit)) return 8;
    // The bands push the rows down and grow the panel; hits follow the rows.
    const power_menu::Geometry geometry{1600, 900, power_menu::gaps(rows)};
    const auto cancel = geometry.row(5), sleep = geometry.row(2);
    // As the large game menu: its 78 px rows where they fit, shorter on a short window.
    if (geometry.rowStep() != power_menu::Geometry::tall_step || power_menu::Geometry{1600, 600, power_menu::gaps(rows)}.rowStep() >= geometry.rowStep()) return 10;
    if (sleep.top - geometry.row(1).top != geometry.rowStep() + power_menu::Geometry::band ||
        geometry.hit(sleep.left + 4, sleep.top + 4) != 2 || geometry.hit(cancel.left + 4, cancel.bottom - 4) != 5 ||
        geometry.hit(sleep.left + 4, sleep.top - 6) != -1 || !geometry.panel().contains(cancel.left, cancel.bottom + 4)) return 9;
    // Item and PowerAction values line up for the cast in powerMenuAccept.
    if (static_cast<PowerAction>(power_menu::Item::hibernate) != PowerAction::hibernate ||
        static_cast<PowerAction>(power_menu::Item::shutdown) != PowerAction::shutdown ||
        power_menu::systemAction(power_menu::Item::exit) || power_menu::systemAction(power_menu::Item::tv_mode) ||
        !power_menu::systemAction(power_menu::Item::sleep)) return 7;
    std::cout << "Press/hold gestures, reconnect swallowing and power menu rows passed\n";
    return 0;
}
