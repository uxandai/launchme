#pragma once

#include <algorithm>
#include <cstddef>

// Flix home layout (src/app/flix.cpp, src/ui/flix_renderer.cpp). The first row holds the
// recently played games as landscape capsules, every other row one category of vertical
// posters. Each row scrolls sideways on its own; the page scrolls vertically. While the
// first row has focus it rests near the bottom edge, so the focused game's background shows
// above it; moving down slides the rows up, the focused row just under the top edge.
// Shared by the animation targets, art binding, hit testing and drawing.
namespace flix_layout {
constexpr float left = 68, right_margin = 68;
// Pasek boczny (src/ui/flix_rail.hpp): its width while it is shown, else 0; set by the app
// (Launcher::rebuildFlix and each frame) before targets, hit tests and drawing read it.
inline float rail = 0;
inline float leftEdge() { return left + rail; } // Where the rows, their titles and the logo start.
constexpr float header = 48;    // Row title band above the tiles.
constexpr float gap = 22;       // Between the tiles of a row.
constexpr float row_gap = 30;   // Below a row's tiles.
constexpr float landscape_height = 196, landscape_aspect = 920.0f / 430.0f;
constexpr float poster_height = 258, poster_aspect = 2.0f / 3.0f;
constexpr float focus_top = 92;   // Header top of a focused row other than the first.
constexpr float rest_bottom = 80; // Gap under the first row's tiles while it has focus.
constexpr float quick_lift = 110; // The first row rises while the quick bar is open, like the shelves.

// Tile size setting (AppSettings::FlixSize): `scale` multiplies the tiles and the gap between
// them; the row titles, margins and the resting/focused positions stay put.
inline float sizeScale(int size) { return size > 0 ? 1.2f : 1.0f; }
inline float tileHeight(bool landscape, float scale = 1) { return (landscape ? landscape_height : poster_height) * scale; }
inline float aspect(bool landscape) { return landscape ? landscape_aspect : poster_aspect; }
inline float tileWidth(bool landscape, float scale = 1) { return tileHeight(landscape, scale) * aspect(landscape); }
inline float pitch(bool landscape, float scale = 1) { return tileWidth(landscape, scale) + gap * scale; }
inline float rowHeight(bool landscape, float scale = 1) { return header + tileHeight(landscape, scale) + row_gap; }
// Distance from the first row's header to row `row`'s header (only row 0 is landscape).
inline float offset(size_t row, float scale = 1) {
    return row ? rowHeight(true, scale) + static_cast<float>(row - 1) * rowHeight(false, scale) : 0.0f;
}
// Header top of the first row while it has focus.
inline float restTop(float height, float scale = 1) { return height - rest_bottom - tileHeight(true, scale) - header; }
// Header top of `row` while `focus` has focus.
inline float rowTop(size_t row, size_t focus, float height, float scale = 1) {
    // At rest, keep the next title and all of its tile shadows below the viewport, even
    // when the quick bar lifts the page. Browsing lower rows uses the normal compact gaps.
    const float below = !focus && row ? rest_bottom + quick_lift + 20 - row_gap : 0.0f;
    return (focus ? focus_top : restTop(height, scale)) + offset(row, scale) - offset(focus, scale) + below;
}
// 0 while the first row rests at the bottom, 1 once a lower row has moved to the top;
// measured from the first row's actual (animated) header position.
inline float lift(float first_row_top, float height, float scale = 1) {
    const float rest = restTop(height, scale), raised = rowTop(0, 1, height, scale);
    return std::clamp((rest - first_row_top) / std::max(1.0f, rest - raised), 0.0f, 1.0f);
}
// Tiles that fit completely; the next one peeks in at the right edge.
inline size_t visibleCount(bool landscape, float width, float scale = 1) {
    return std::max<size_t>(1, static_cast<size_t>((width - leftEdge() - right_margin + gap * scale) / pitch(landscape, scale)));
}
inline float tileLeft(bool landscape, size_t column, size_t first, float scale = 1) {
    return leftEdge() + (static_cast<float>(column) - static_cast<float>(first)) * pitch(landscape, scale);
}
// Resting on a game of a poster row unfolds its tile sideways into a wide card (its background,
// logo and facts) after `unfold_rest` seconds without input; the tiles after it make room.
constexpr float unfold_rest = 3.0f;
constexpr float unfold_aspect = 16.0f / 9.0f;
constexpr float unfold_image = 0.60f; // The picture's share of the unfolded card; the facts below it.
inline float unfoldWidth(float scale = 1) { return tileHeight(false, scale) * unfold_aspect; }
inline float unfoldExtra(float scale = 1) { return unfoldWidth(scale) - tileWidth(false, scale); }
// First visible column after focusing `column`: the row scrolls only when the focus would leave it.
inline size_t firstVisible(size_t first, size_t column, size_t count, bool landscape, float width, float scale = 1) {
    const size_t visible = visibleCount(landscape, width, scale);
    if (column < first) first = column;
    else if (column >= first + visible) first = column + 1 - visible;
    return std::min(first, count > visible ? count - visible : size_t{0});
}
} // namespace flix_layout
