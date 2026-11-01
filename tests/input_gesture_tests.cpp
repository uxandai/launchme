#include "../src/app/hold_gesture.hpp"
#include "../src/ui/power_menu.hpp"
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
