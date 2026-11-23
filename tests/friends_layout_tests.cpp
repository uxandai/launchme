#include "../src/ui/friends_layout.hpp"
#include "../src/ui/drawer_clock.hpp"

#include <iostream>

int main() {
    const std::vector<SteamFriend> people{
        SteamFriend{1, L"Gra", L"W grze", {}, 1},
        SteamFriend{2, L"Online", {}, {}, 1},
        SteamFriend{3, L"Offline", {}, {}, 0}
    };
    if (friends_layout::rowTop(people, 0) != 159 || friends_layout::rowTop(people, 1) != 283 ||
        friends_layout::rowTop(people, 2) != 407 || friends_layout::bottom(people) != 487 ||
        friends_layout::hit(people, 185) != 0 || friends_layout::hit(people, 310) != 1 ||
        friends_layout::hit(people, 433) != 2 || friends_layout::hit(people, 252)) {
        std::cerr << "Friend highlight / chat click offsets do not match grouped rows\n";
        return 1;
    }

    // Dashboard grid: 2 columns, 2D moves stop at the edges (the profile alone in the last row), hits match the drawn tiles.
    namespace dashboard = friends_layout::dashboard;
    if (dashboard::move(0, 1, 0) != 1 || dashboard::move(1, 1, 0) != 1 || dashboard::move(0, 0, 1) != 2 ||
        dashboard::move(3, 0, 1) != 4 || dashboard::move(4, 1, 1) != 4 || dashboard::move(3, -1, 0) != 2 || dashboard::move(2, 0, -1) != 0 ||
        dashboard::move(1, -1, 1) != 2 || dashboard::move(0, -1, -1) != 0) {
        std::cerr << "Dashboard tile navigation is wrong\n";
        return 2;
    }
    const float left = 1600 - friends_layout::drawer_width;
    for (int i = 0; i < dashboard::tile_count; ++i) {
        const auto tile = dashboard::tile(left, i);
        if (dashboard::hit(left, (tile.left + tile.right) / 2, (tile.top + tile.bottom) / 2) != i ||
            tile.left < left || tile.right > 1600 || tile.bottom > 900) {
            std::cerr << "Dashboard tile " << i << " is misplaced\n";
            return 3;
        }
    }
    if (dashboard::hit(left, left + 5, dashboard::grid_top + 5) != -1 ||
        dashboard::hit(left, left + 200, dashboard::grid_top - 2) != -1 ||
        !friends_layout::listBack(left).contains(left + 40, 56)) return 4;

    // Clock header in the UI language, at a fixed moment: Tuesday, 29 September 2026, 14:05.
    const SYSTEMTIME moment{2026, 9, 2, 29, 14, 5, 42, 250};
    const auto time_pl = drawer_clock::time(moment, L"pl-PL"), time_en = drawer_clock::time(moment, L"en-US");
    const auto date_pl = drawer_clock::date(moment, L"pl-PL"), date_en = drawer_clock::date(moment, L"en-US");
    if (time_pl.find(L"14:05") == std::wstring::npos || time_en.find(L"2:05") == std::wstring::npos ||
        date_pl != L"wtorek, 29 wrze\u015Bnia" || date_en != L"Tuesday, September 29") {
        std::cerr << "Clock text is not formatted for the UI language\n";
        return 5;
    }
    SYSTEMTIME next = moment;
    next.wMinute = 6;
    if (drawer_clock::minute(next) == drawer_clock::minute(moment) ||
        drawer_clock::millisecondsToNextMinute(moment) != 17750) return 6;
    next = moment;
    next.wSecond = 59;
    if (drawer_clock::minute(next) != drawer_clock::minute(moment)) return 7;
    // Mixed sizes, stable IDs, no collisions, scroll-aware hit tests and header access.
    const std::vector<std::string> ids{"steam", "discord", "cpu", "gpu", "plugin/widget", "fps"};
    using Size = widgets::Size;
    const dashboard::Layout defaults{ids, {}};
    for (int i = 0; i < 6; ++i) {
        const auto a = defaults.tile(i), b = dashboard::tile(0, i);
        if (a.left != b.left || a.top != b.top || a.right != b.right || a.bottom != b.bottom) return 8;
    }
    for (int combination = 0; combination < 729; ++combination) { // Every combination for six widgets.
        std::vector<widgets::Preference> saved;
        int value = combination;
        for (const auto& id : ids) { saved.push_back({id, static_cast<Size>(value % 3)}); value /= 3; }
        const dashboard::Layout layout{ids, saved};
        for (const auto& a : layout.items) {
            if (a.box.left < dashboard::margin || a.box.right > friends_layout::drawer_width - dashboard::margin) return 9;
            if (a.size == Size::wide && a.box.right - a.box.left != 2 * dashboard::tile_width + dashboard::gap) return 10;
            const float scroll = a.box.top - dashboard::grid_top;
            if (layout.hit(left, left + (a.box.left + a.box.right) / 2, dashboard::grid_top + (a.box.bottom - a.box.top) / 2, scroll, 900) != a.source) return 11;
            for (const auto& b : layout.items) if (a.source != b.source &&
                a.box.left < b.box.right && a.box.right > b.box.left && a.box.top < b.box.bottom && a.box.bottom > b.box.top) return 12;
        }
    }
    const std::vector<widgets::Preference> saved{{"plugin/widget", Size::wide}, {"cpu", Size::icon}, {"steam", Size::square}};
    const dashboard::Layout mixed{ids, saved}, disabled{{"steam", "discord", "cpu", "gpu"}, saved};
    if (mixed.items.front().source != 4 || disabled.items.front().source != 2 || disabled.items.front().size != Size::icon ||
        mixed.move(4, 0, -1) != dashboard::power || mixed.move(dashboard::reorganize, 0, 1) != 4 ||
        mixed.move(dashboard::power, 0, 1) != 4 || mixed.move(dashboard::power, 1, 0) != dashboard::reorganize ||
        mixed.move(dashboard::reorganize, -1, 0) != dashboard::power || mixed.move(dashboard::power, -1, 0) != dashboard::power ||
        mixed.move(dashboard::power, 0, -1) != dashboard::power ||
        mixed.hit(left, left + 390, 60, 0, 900) != dashboard::reorganize || mixed.hit(left, left + 340, 60, 0, 900) != dashboard::power ||
        mixed.tile(dashboard::power, left).right >= mixed.tile(dashboard::reorganize, left).left || mixed.maxScroll(450) <= 0) return 13;
    return 0;
}
