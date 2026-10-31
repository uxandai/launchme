#pragma once

#include "settings_view.hpp"

#include <algorithm>
#include <cstddef>

// Geometry of the PSfive home layout (src/app/psfive.cpp, src/ui/psfive_renderer.cpp), measured on
// the PS5 home screen (1920 x 1080 scaled to the 1600 x 900 viewport): the top bar (tabs on the
// left; Menu and the clock on the right), then one row of square tiles over the focused
// game's full-screen background. The focused tile is larger, framed and pinned at the fixed
// left margin shared with the tabs, logo and buttons; earlier tiles slide out under that edge. Its name (and platform
// badge) starts where the next tile starts, under the small ones. Low on the left the game's logo
// over "Graj" and "…"; they rise over the quick bar. Tile sizes follow the size setting (`scale`,
// 80 / 100 / 120% of a base enlarged by 30%). Shared by animation targets, hit testing and drawing.
namespace psfive_layout {
constexpr float tile = 114.4f, focus_tile = 182; // All three settings: 30% larger than the original 88 / 140.
constexpr float gap = 7.5f;                  // Between two small tiles.
constexpr float focus_gap = 12;              // Between the focused tile and its neighbours (its frame sits there).
constexpr float frame_gap = 2.5f, frame_width = 2.6f; // The focus frame around the tile, with a dark gap.
constexpr float top_center = 52;             // The top bar's line: tabs, icons and the clock.
constexpr float row_top = 105;
constexpr size_t row_games = 12; // Games in a row before its "Wszystkie" tile.
constexpr float badge_height = 28;
constexpr int action_count = 2; // "Graj", "…".
constexpr float button_height = 59, play_width = 278, more_width = 59, button_gap = 14;
constexpr float bottom_margin = 145; // Under the buttons.
constexpr float quick_lift = 0;      // The buttons sit clear of the quick bar: nothing has to rise.
// The logo's box at most (≤ 40% of the screen width), its bottom logo_gap over the buttons.
constexpr float logo_width = 420, logo_height = 150, logo_gap = 56;
// Top bar. Tabs from the left margin; right to left the clock (a reserved box, right-aligned
// text), then Menu and Widgets. Three categories and an optional overflow tab on the left.
constexpr float tabs_left = 77, tab_gap = 48;
constexpr size_t visible_tabs = 3;
enum TopItem { top_widgets, top_menu };
constexpr int top_count = 2;
// The clock's box fits a 12-hour "12:00 PM" (#3), right-aligned, so a 24-hour "14:05" sits at its end.
constexpr float bar_margin = 72, clock_width = 150, clock_gap = 40, icon_radius = 26, icon_step = 70;

inline float sizeScale(int size) { return 0.8f + std::clamp(size, 0, 2) * 0.2f; }
inline float tileSize(bool focused, float scale = 1) { return (focused ? focus_tile : tile) * scale; }
inline float anchorX(float /*scale*/ = 1) { return tabs_left; } // Tile size never changes the left alignment.
inline float rowClipLeft() { return tabs_left - frame_gap - frame_width; } // Keep the focus frame visible.
// Offset from the focused tile: 0 at the anchor, the next ones after the large tile.
inline float tileLeft(int offset, float scale = 1) {
    const float anchor = anchorX(scale);
    if (offset == 0) return anchor;
    if (offset > 0) return anchor + (focus_tile + focus_gap + (offset - 1) * (tile + gap)) * scale;
    return anchor - (focus_gap + tile) * scale + (offset + 1) * (tile + gap) * scale;
}
// Badge and name: where the next tile starts, centred in the band under the small tiles.
inline float labelCenter(float scale = 1) { return row_top + (tile + (focus_tile - tile) * 0.5f) * scale; }
inline float labelLeft(float scale = 1) { return tileLeft(1, scale); }
// The focused tile starts the visible row; earlier tiles are clipped at its left margin.
constexpr int visible_before = 0;
inline int visibleAfter(float width, float scale = 1) {
    return std::max(1, static_cast<int>((width - tileLeft(1, scale)) / ((tile + gap) * scale)) + 2);
}
// At rest; `lift` (0..1, the quick bar's progress) raises them by quick_lift.
inline float buttonsTop(float height, float lift = 0) { return height - bottom_margin - button_height - quick_lift * lift; }
inline settings_ui::Rect action(int index, float height, float scale = 1, float lift = 0) {
    const float top = buttonsTop(height, lift), left = anchorX(scale);
    if (index <= 0) return {left, top, left + play_width, top + button_height};
    const float more = left + play_width + button_gap;
    return {more, top, more + more_width, top + button_height};
}
inline int hitAction(float x, float y, float height, float scale = 1) {
    for (int i = 0; i < action_count; ++i) if (action(i, height, scale).contains(x, y)) return i;
    return -1;
}
inline float logoBottom(float height, float lift = 0) { return buttonsTop(height, lift) - logo_gap; }
inline settings_ui::Rect clock(float width) { return {width - bar_margin - clock_width, top_center - 22, width - bar_margin, top_center + 22}; }
// A top bar icon's circle; without the clock the icons move into its place.
inline settings_ui::Rect topItem(int index, float width, bool with_clock) {
    const float menu = with_clock ? width - bar_margin - clock_width - clock_gap : width - bar_margin - icon_radius;
    const float cx = menu - static_cast<float>(top_count - 1 - index) * icon_step;
    return {cx - icon_radius, top_center - icon_radius, cx + icon_radius, top_center + icon_radius};
}
inline int hitTop(float x, float y, float width, bool with_clock) {
    for (int i = 0; i < top_count; ++i) if (topItem(i, width, with_clock).contains(x, y)) return i;
    return -1;
}

// The category overflow opens below "Więcej...", using the same geometry for drawing,
// scrolling and pointer input. Row indices are category indices (the first three are tabs).
struct MoreMenu {
    static constexpr float row_height = 52, padding = 8, menu_width = 350;
    settings_ui::Rect panel;
    size_t count;
    MoreMenu(float width, float height, size_t categories, float anchor) : count(categories > visible_tabs ? categories - visible_tabs : 0) {
        const float left = std::clamp(anchor, 24.0f, std::max(24.0f, width - menu_width - 24));
        const float top = top_center + 34;
        // Clear of the button hints along the bottom edge (Wybierz / Zamknij while it is open).
        panel = {left, top, left + menu_width, top + 2 * padding + std::min(static_cast<float>(count) * row_height, std::max(row_height, height - top - 72))};
    }
    float top() const { return panel.top + padding; }
    float bottom() const { return panel.bottom - padding; }
    float maxScroll() const { return std::max(0.0f, static_cast<float>(count) * row_height - (bottom() - top())); }
    float rowTop(int category, float scroll) const { return top() + (category - static_cast<int>(visible_tabs)) * row_height - scroll; }
    settings_ui::Rect revealed(float progress) const {
        const float t = std::clamp(progress, 0.0f, 1.0f), eased = t * t * (3 - 2 * t);
        const float inset = 12 * (1 - eased);
        return {panel.left + inset, panel.top, panel.right - inset,
                panel.top + 2 * padding + (panel.bottom - panel.top - 2 * padding) * eased};
    }
    int hit(float x, float y, float scroll, float progress = 1) const {
        const auto visible = revealed(progress);
        if (!visible.contains(x, y) || y < top() || y >= visible.bottom - padding) return -1;
        const int row = static_cast<int>((y - top() + scroll) / row_height);
        return row >= 0 && row < static_cast<int>(count) ? row + static_cast<int>(visible_tabs) : -1;
    }
};
} // namespace psfive_layout
