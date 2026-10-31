#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

// Metro home layout (src/app/metro.cpp, src/ui/metro_renderer.cpp), after the 2011 Xbox 360
// dashboard: pages of tiles side by side with their names as tabs above. Page 0 ("start")
// holds the three most recently played games, Biblioteka, Ustawienia, two cells for
// Towarzyszka and Glance (while on) or else the shortcuts to the Platformy PC and Emulacja
// pages, and the game being installed (or the newest one); every further page is one home
// category. Tile rectangles are in page space: x from the first page's left edge, y from the
// grid top. Shared by the animation targets, navigation, art binding, hit testing and drawing.
namespace metro_layout {
// What a tile is; the card's Game says which game, category or store.
enum class Role { recent_large, recent, library, settings, pc_platforms, emulation, install, all, game, logo, glance, companion, tarot, add };
// Solid tiles of their own colour, without artwork.
inline bool systemTile(Role role) {
    return role == Role::library || role == Role::settings || role == Role::pc_platforms || role == Role::emulation ||
           role == Role::glance || role == Role::companion || role == Role::tarot || role == Role::add;
}
inline bool landscape(Role role) { return role == Role::recent_large || role == Role::recent; }

struct Rect {
    float left = 0, top = 0, right = 0, bottom = 0;
    float width() const { return right - left; }
    float height() const { return bottom - top; }
    float centerX() const { return (left + right) * 0.5f; }
    float centerY() const { return (top + bottom) * 0.5f; }
};

// Tile shapes follow the artwork so nothing is cropped much: a cell has the 920x430 capsule's
// proportions (recent games, store/console logos), 2x2 cells about the same, a column of three
// cells a 2:3 poster (the installation tile), half a cell is about square (Biblioteka,
// Ustawienia), and category pages show 2:3 posters in two rows.
constexpr float left = 120, right_margin = 120;
constexpr float tabs_center = 118;          // Centre line of the tab labels.
constexpr float tab_gap = 36;               // Between two tab labels.
constexpr float grid_top = 206;             // Grid top on a 900 px tall screen.
constexpr float cell_h = 150, cell_w = 321; // 2.14:1, as the landscape capsules.
constexpr float gap = 16;
constexpr float half_w = (cell_w - gap) * 0.5f; // Half a cell: an almost square tile.
constexpr float grid_height = 3 * cell_h + 2 * gap;
constexpr float poster_h = (grid_height - gap) * 0.5f, poster_w = poster_h * 2.0f / 3.0f; // Two rows of posters.
constexpr float page_gap = 112;             // Between the last tile of a page and the next page.
constexpr float quick_lift = 60;            // The tiles rise while the quick bar is open.
constexpr size_t category_tiles = 12;       // Tiles on a category page, its "Wszystkie" tile included.
constexpr float depth_scale = 0.8f;         // "Efekt 3D": the pages beside the focused one.

// Grid top: centred a little lower on screens taller than 16:9.
inline float top(float height) { return grid_top + std::max(0.0f, (height - 900.0f) * 0.5f); }
// A start page cell spanning `columns` x `rows`.
inline Rect cell(float origin, int column, int row, int columns = 1, int rows = 1) {
    const float x = origin + column * (cell_w + gap), y = row * (cell_h + gap);
    return {x, y, x + columns * cell_w + (columns - 1) * gap, y + rows * cell_h + (rows - 1) * gap};
}
// The left (0) or right (1) half of a cell.
inline Rect half(float origin, int column, int row, int side) {
    const float x = origin + column * (cell_w + gap) + side * (half_w + gap), y = row * (cell_h + gap);
    return {x, y, x + half_w, y + cell_h};
}
// "Efekt 3D": a tile of a page beside the focused one, shrunk towards that page's edge nearest
// the focused page (screen x `anchor_x`) and the grid's middle line.
inline Rect shrink(const Rect& tile, float anchor_x, float anchor_y, float scale) {
    return {anchor_x + (tile.left - anchor_x) * scale, anchor_y + (tile.top - anchor_y) * scale,
            anchor_x + (tile.right - anchor_x) * scale, anchor_y + (tile.bottom - anchor_y) * scale};
}
// Category pages fill their columns top to bottom: posters two per column, logos three.
inline Rect poster(float origin, size_t index) {
    const float x = origin + static_cast<float>(index / 2) * (poster_w + gap), y = static_cast<float>(index % 2) * (poster_h + gap);
    return {x, y, x + poster_w, y + poster_h};
}
inline Rect logo(float origin, size_t index) { return cell(origin, static_cast<int>(index / 3), static_cast<int>(index % 3)); }
// Page-space scroll that shows `tile`: the page's left edge at `left`, moving on only when the
// tile would cross the right margin (a page wider than the screen) or the left edge.
inline float reveal(float scroll, float page_left, const Rect& tile, float width) {
    const float visible = std::max(cell_w, width - left - right_margin);
    scroll = std::clamp(scroll, std::min(tile.left, tile.right - visible), tile.left);
    return std::max(page_left, scroll);
}
// Distance score of `to` seen from `from` moving by (dx, dy): negative when `to` is not in that
// direction. Tiles overlapping `from` across the movement come first, then the nearest edge.
inline float score(const Rect& from, const Rect& to, int dx, int dy) {
    const float primary = dx > 0 ? to.left - from.right : dx < 0 ? from.left - to.right :
                          dy > 0 ? to.top - from.bottom : from.top - to.bottom;
    if (primary < -gap * 0.5f) return -1;
    const float cross_gap = dx ? std::max(0.0f, std::max(from.top, to.top) - std::min(from.bottom, to.bottom)) :
                                 std::max(0.0f, std::max(from.left, to.left) - std::min(from.right, to.right));
    const float cross = dx ? std::abs(to.centerY() - from.centerY()) : std::abs(to.centerX() - from.centerX());
    return std::max(0.0f, primary) + cross_gap * 4 + cross * 0.25f;
}
} // namespace metro_layout
