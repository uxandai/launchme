#pragma once
#include "../platform/power.hpp"
#include "../i18n/i18n.hpp"
#include "settings_view.hpp"

#include <algorithm>
#include <vector>

// Guide hold and "Zasilanie" in the quick bar. Three groups with an empty band between them: the
// launcher (entering or leaving Tryb TV first, then closing the launcher), the PC's own power
// actions offered on this machine (in red: sleep, hibernate, restart, shut down), Cancel last.
// The rows follow the Settings choice list's measures (Layout::picker / choice).
namespace power_menu {
// The first four follow PowerAction (static_cast between them).
enum class Item { sleep, hibernate, restart, shutdown, exit, tv_mode, cancel };

inline bool systemAction(Item item) { return item != Item::exit && item != Item::tv_mode && item != Item::cancel; }
// 0: the launcher, 1: the PC's power, 2: Cancel.
inline int group(Item item) { return systemAction(item) ? 1 : item == Item::cancel ? 2 : 0; }
// Drawn in red: the actions that put the PC to sleep, restart it or turn it off.
inline bool danger(Item item) { return systemAction(item); }

inline std::vector<Item> items(const std::vector<PowerAction>& available) {
    std::vector<Item> result{Item::tv_mode, Item::exit};
    for (const auto action : available) result.push_back(static_cast<Item>(action));
    result.push_back(Item::cancel);
    return result;
}
// `desktop`: the launcher shows the desktop window, so the mode row enters Tryb TV.
inline const wchar_t* label(Item item, bool desktop = false) {
    switch (item) {
    case Item::sleep: return tr(L"Uśpij");
    case Item::hibernate: return tr(L"Hibernuj");
    case Item::restart: return tr(L"Uruchom ponownie");
    case Item::shutdown: return tr(L"Wyłącz PC");
    case Item::exit: return tr(L"Zamknij launcher");
    case Item::tv_mode: return desktop ? tr(L"Przejdź do trybu TV") : tr(L"Wyjdź z trybu TV");
    case Item::cancel: return tr(L"Anuluj");
    }
    return L"";
}
// Its icon beside the row (Segoe Fluent / MDL2 code points, as the large game menu's).
inline wchar_t icon(Item item) {
    switch (item) {
    case Item::sleep: return 0xE708;     // A moon.
    case Item::hibernate: return 0xE823; // A clock.
    case Item::restart: return 0xE72C;   // Refresh.
    case Item::shutdown: return 0xE7E8;  // Power.
    case Item::exit: return 0xE8BB;      // The window's close.
    case Item::tv_mode: return 0xE7F4;   // A TV.
    case Item::cancel: return 0xE72B;    // Back.
    }
    return 0xE712;
}
// gaps[i]: row i starts a new group (an empty band above it).
inline std::vector<bool> gaps(const std::vector<Item>& items) {
    std::vector<bool> result(items.size(), false);
    for (size_t i = 1; i < items.size(); ++i) result[i] = group(items[i]) != group(items[i - 1]);
    return result;
}

// Tryb TV's menu, as the large game menu (context_menu::Dialog): a wide panel with a header (its
// icon and "Zasilanie") and tall rows with an icon each, the groups apart by a band. Rows shrink
// to fit a short window. Drawing, clicks and the pointer's hand all measure here.
struct Geometry {
    float width = 1600, height = 900;
    std::vector<bool> gaps;
    static constexpr float max_width = 760, padding = 26, band = 18, header = 132;
    static constexpr float tall_step = 78, tall_height = 70;
    int count() const { return static_cast<int>(gaps.size()); }
    float bandsAbove(int row) const {
        float total = 0;
        for (int i = 1; i <= std::min(row, count() - 1); ++i) if (gaps[static_cast<size_t>(i)]) total += band;
        return total;
    }
    // The large menu's 78 px rows where they fit, else down to the picker's 54.
    float rowStep() const {
        if (count() <= 0) return tall_step;
        const float room = (height - 80 - header - padding - bandsAbove(count() - 1)) / count();
        return std::clamp(room, 54.0f, tall_step);
    }
    float rowHeight() const { return rowStep() - (tall_step - tall_height); }
    settings_ui::Rect panel() const {
        const float h = header + count() * rowStep() + bandsAbove(count() - 1) + padding;
        const float w = std::min(max_width, width - 96);
        return {(width - w) * 0.5f, (height - h) * 0.5f, (width + w) * 0.5f, (height + h) * 0.5f};
    }
    settings_ui::Rect row(int index) const {
        const auto p = panel();
        const float top = p.top + header + index * rowStep() + bandsAbove(index);
        return {p.left + padding, top, p.right - padding, top + rowHeight()};
    }
    // A fractional row (the highlight gliding between two rows).
    settings_ui::Rect rowAt(float position) const {
        if (count() <= 0) return {};
        position = std::clamp(position, 0.0f, static_cast<float>(count() - 1));
        const int below = static_cast<int>(position), above = std::min(below + 1, count() - 1);
        const float t = position - below;
        const auto a = row(below), b = row(above);
        return {a.left, a.top + (b.top - a.top) * t, a.right, a.bottom + (b.bottom - a.bottom) * t};
    }
    int hit(float x, float y) const {
        for (int i = 0; i < count(); ++i) if (row(i).contains(x, y)) return i;
        return -1;
    }
};
} // namespace power_menu
