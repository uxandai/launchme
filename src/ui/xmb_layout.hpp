#pragma once

#include <algorithm>

// Geometry of the XMB home layout, shared by animation targets, hit testing and labels.
// The focused column is pinned at anchor_x; the top row scrolls horizontally and the
// focused column's games are listed downward (earlier rows move above the tile row).
namespace xmb_layout {
constexpr float anchor_x = 150, row_center = 205;
constexpr float tile = 112, focus_tile = 150, gap = 22;
constexpr float item = 88, focus_item = 124, item_gap = 16;
constexpr float landscape_item = 76, focus_landscape_item = 104;
constexpr float caption_top = row_center + focus_tile * 0.5f + 10;
constexpr float list_top = row_center + focus_tile * 0.5f + 58;
constexpr float above_bottom = row_center - focus_tile * 0.5f - 26;
constexpr float label_left = anchor_x + focus_tile * 0.5f + focus_item * 0.5f + 24;
constexpr float poster_aspect = 2.0f / 3.0f, landscape_aspect = 920.0f / 430.0f;
enum class Art { square, poster, landscape };
inline float sizeScale(int size) { return 0.8f + std::clamp(size, 0, 2) * 0.2f; }
inline float artHeight(float width, bool posters) { return posters ? width / poster_aspect : width; }
inline float rowOffset(bool posters, float scale = 1) { return (artHeight(focus_tile, posters) * scale - focus_tile) * 0.5f; }
inline float columnItemHeight(bool focused, Art art, float scale = 1) {
    if (art == Art::poster) return (focused ? focus_item : item) / poster_aspect * scale;
    if (art == Art::landscape) return (focused ? focus_landscape_item : landscape_item) * scale;
    return (focused ? focus_item : item) * scale;
}
inline float columnItemWidth(bool focused, Art art, float scale = 1) {
    if (art == Art::landscape) return (focused ? focus_landscape_item : landscape_item) * landscape_aspect * scale;
    return (focused ? focus_item : item) * scale;
}

inline float tileSize(bool focused, float scale = 1) { return (focused ? focus_tile : tile) * scale; }
inline float tileLeft(int offset, float scale = 1) {
    if (offset == 0) return anchor_x;
    if (offset > 0) return anchor_x + (focus_tile + gap + (offset - 1) * (tile + gap)) * scale;
    return anchor_x + offset * (tile + gap) * scale;
}
inline float tileTop(float size) { return row_center - size * 0.5f; }
inline float itemSize(bool focused, float scale = 1) { return (focused ? focus_item : item) * scale; }
inline float itemLeft(float size, float scale = 1) { return anchor_x + (focus_tile * scale - size) * 0.5f; }
inline float labelLeft(float scale = 1) { return anchor_x + (focus_tile + focus_item) * scale * 0.5f + 24; }
inline float labelLeft(Art art, float scale) { return anchor_x + (focus_tile * scale + columnItemWidth(true, art, scale)) * 0.5f + 24; }
// focus_row < 0 previews the column under a focused tile.
// A column belongs to a square collection/console tile, even when its games are posters or landscape tiles.
inline float itemTop(int row, int focus_row, Art art, float scale = 1) {
    const float top = list_top + rowOffset(false, scale), height = columnItemHeight(false, art, scale);
    const float spacing = item_gap * scale;
    if (focus_row < 0) return top + row * (height + spacing);
    const int offset = row - focus_row;
    if (offset == 0) return top;
    if (offset > 0) return top + columnItemHeight(true, art, scale) + spacing + (offset - 1) * (height + spacing);
    return above_bottom - rowOffset(false, scale) - height + (offset + 1) * (height + spacing);
}
inline float itemTop(int row, int focus_row, bool posters = false, float scale = 1) {
    return itemTop(row, focus_row, posters ? Art::poster : Art::square, scale);
}
// Enough rows around the focus to cover a tall window, without binding a whole collection.
inline int firstVisibleRow(int focus_row) { return std::max(0, focus_row - 3); }
inline int lastVisibleRow(int focus_row, float height, Art art, float scale = 1) {
    return std::max(0, focus_row) + static_cast<int>((height - list_top - rowOffset(false, scale)) / (columnItemHeight(false, art, scale) + item_gap * scale)) + 2;
}
inline int lastVisibleRow(int focus_row, float height, bool posters = false, float scale = 1) {
    return lastVisibleRow(focus_row, height, posters ? Art::poster : Art::square, scale);
}
} // namespace xmb_layout
