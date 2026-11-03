#pragma once

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <optional>
#include <utility>
#include "../library/view_options.hpp"
#include "settings_view.hpp"
#include "coverflow_layout.hpp"
#include "grid_columns.hpp"

namespace grid_layout {
inline bool hitBack(float x, float y) { return x >= 16 && x < 52 && y >= 35 && y < 71; }
inline float top(size_t index, float width, float scroll, bool posters = false) {
    return 190.0f + (index / columns(width)) * (cell(width) * (posters ? 1.5f : 1) + 64.0f) - scroll;
}
inline float maxScroll(size_t count, float width, float height, bool posters = false) {
    if (!count) return 0;
    const size_t rows = (count + columns(width) - 1) / columns(width);
    return std::max(0.0f, 190.0f + rows * (cell(width) * (posters ? 1.5f : 1) + 64.0f) - height + 56.0f); // Clear of the button hints.
}
inline std::pair<size_t, size_t> visibleRange(size_t count, float width, float height, float scroll, bool posters = false) {
    const auto cols = static_cast<size_t>(columns(width));
    const float step = cell(width) * (posters ? 1.5f : 1) + 64.0f;
    const size_t first_row = static_cast<size_t>(std::max(0.0f, (scroll - 190.0f) / step));
    const size_t last_row = static_cast<size_t>(std::max(0.0f, (scroll + height - 190.0f) / step)) + 1;
    return {std::min(count, first_row * cols), std::min(count, last_row * cols)};
}
} // namespace grid_layout

// Library list view: one column of rows on the left, the focused game's cover and
// description in a panel on the right. A card's geometry (x, y, size) is its row thumbnail,
// so rows scroll with the same card easing as the grid.
namespace list_layout {
constexpr float list_top = 190, step = 78, row_height = 70, thumb = 54, left = 68, bottom_margin = 56;
inline float right(float width) { return std::clamp(width * 0.54f, 760.0f, 1080.0f); }
// Three right-aligned metadata columns; the title uses the remaining space after its thumbnail.
struct Columns {
    float width;
    library_view::Options options;
    settings_ui::Rect rating() const { return {right(width) - (options.list_rating ? 82 : 18), 0, right(width) - 18, 0}; }
    settings_ui::Rect playtime() const { const float end = rating().left - (options.list_rating ? 12 : 0); return {end - (options.list_playtime ? 94 : 0), 0, end, 0}; }
    settings_ui::Rect year() const { const float end = playtime().left - (options.list_playtime ? 12 : 0); return {end - (options.list_year ? 60 : 0), 0, end, 0}; }
    float titleRight() const { return year().left - (options.list_year || options.list_playtime || options.list_rating ? 20 : 0); }
};
inline float top(size_t index, float scroll) { return list_top + index * step - scroll; }
inline float thumbLeft() { return left + (row_height - thumb) * 0.5f; }
inline float thumbTop(float row_top) { return row_top + (row_height - thumb) * 0.5f; }
inline float rowTop(float thumb_top) { return thumb_top - (row_height - thumb) * 0.5f; }
inline bool hit(float x, float y, float thumb_top, float width) {
    const float top = rowTop(thumb_top);
    return x >= left && x < right(width) && y >= top && y < top + row_height;
}
inline float maxScroll(size_t count, float height) {
    return count ? std::max(0.0f, list_top + (count - 1) * step + row_height - height + bottom_margin) : 0.0f;
}
inline std::pair<size_t, size_t> visibleRange(size_t count, float height, float scroll) {
    const size_t first = static_cast<size_t>(std::max(0.0f, (scroll - list_top) / step));
    const size_t last = static_cast<size_t>(std::max(0.0f, (scroll + height - list_top) / step)) + 1;
    return {std::min(count, first), std::min(count, last)};
}
// Detail panel beside the list.
inline float panelLeft(float width) { return right(width) + 56; }
inline float panelRight(float width) { return width - 68; }
} // namespace list_layout

// Cinematic library: a pinned spotlight over native SteamGridDB landscape capsules.
namespace cinematic_layout {
constexpr float left = 68, gap = 30, list_top = 390, aspect = 920.0f / 430.0f;
inline int columns(float width) { return std::clamp(static_cast<int>((width - 106) / 340), 2, 6); }
inline float cellWidth(float width) { return (width - 2 * left - (columns(width) - 1) * gap) / columns(width); }
inline float cell(float width) { return cellWidth(width) / aspect; }
inline float step(float width) { return cell(width) + 66; }
inline float x(size_t index, float width) { return left + (index % columns(width)) * (cellWidth(width) + gap); }
inline float top(size_t index, float width, float scroll) { return list_top + (index / columns(width)) * step(width) - scroll; }
inline float maxScroll(size_t count, float width, float height) {
    const auto rows = (count + columns(width) - 1) / columns(width);
    const auto visible = static_cast<size_t>(std::max(1.0f, std::floor((height - list_top - 24) / step(width))));
    return rows > visible ? (rows - visible) * step(width) : 0;
}
inline std::pair<size_t, size_t> visibleRange(size_t count, float width, float height, float scroll) {
    const auto cols = static_cast<size_t>(columns(width));
    const auto first = static_cast<size_t>(std::max(0.0f, scroll / step(width)));
    const auto last = static_cast<size_t>(std::max(0.0f, (scroll + height - list_top) / step(width))) + 1;
    return {std::min(count, first * cols), std::min(count, last * cols)};
}
}

namespace cinematic_row {
constexpr float left = 68, gap = 30;
inline float cellWidth(float width) { return std::clamp(width * 0.26f, 340.0f, 480.0f); }
inline float top(float width, float height) { return std::max(cinematic_layout::list_top, height - 104 - cellWidth(width) / cinematic_layout::aspect); }
inline settings_ui::Rect summary(float width, float height) { return {left, 324, left + std::min(680.0f, width * 0.44f), top(width, height) - 48}; }
inline float step(float width) { return cellWidth(width) + gap; }
inline float x(size_t index, float width, float scroll) { return left + index * step(width) - scroll; }
inline float maxScroll(size_t count, float width) { return count ? std::max(0.0f, count * step(width) - gap - (width - 2 * left)) : 0; }
}

// Kinowy in one column: the focused game's logo, facts and description on the left, one column of
// capsules on the right under the category tabs; the focused capsule scrolls to the middle.
namespace cinematic_column {
constexpr float right_margin = 68, list_top = 108, gap = 22, bottom_margin = 40;
inline float cellWidth(float width) { return std::clamp(width * 0.3f, 380.0f, 560.0f); }
inline float cell(float width) { return cellWidth(width) / cinematic_layout::aspect; }
inline float step(float width) { return cell(width) + gap; }
inline float x(float width) { return width - right_margin - cellWidth(width); }
inline float top(size_t index, float width, float scroll) { return list_top + index * step(width) - scroll; }
inline float maxScroll(size_t count, float width, float height) {
    return count ? std::max(0.0f, list_top + count * step(width) - gap - (height - bottom_margin)) : 0.0f;
}
inline std::pair<size_t, size_t> visibleRange(size_t count, float width, float height, float scroll) {
    const auto first = static_cast<size_t>(std::max(0.0f, (scroll - 12) / step(width)));
    const auto last = static_cast<size_t>(std::max(0.0f, (scroll + height - list_top) / step(width))) + 1;
    return {std::min(count, first), std::min(count, last)};
}
// The focused capsule in the middle of the column's visible height (clamped at both ends).
inline float reveal(size_t index, size_t count, float width, float height) {
    const float middle = (height - bottom_margin - list_top) * 0.5f;
    return std::clamp(index * step(width) + cell(width) * 0.5f - middle, 0.0f, maxScroll(count, width, height));
}
// The synopsis beside the column: logo or title, facts, description.
inline settings_ui::Rect summary(float width, float height) { return {68, 132, x(width) - 64, height - 56}; }
}

// Siatka with landscape tiles (the library's option, search results): 920:430 capsules with their titles.
namespace landscape_grid {
constexpr float list_top = 190, gap = 25, caption = 64, aspect = 920.0f / 430.0f;
inline int columns(float width) { return std::clamp(static_cast<int>((width - 120) / 360), 2, 5); }
inline float cellWidth(float width) { const int n = columns(width); return std::min(420.0f, (width - 112.0f - (n - 1) * gap) / n); }
inline float cell(float width) { return cellWidth(width) / aspect; }
inline float step(float width) { return cell(width) + caption; }
inline float left(float width, size_t index) {
    const int n = columns(width);
    const float w = cellWidth(width), total = n * w + (n - 1) * gap;
    return (width - total) * 0.5f + (index % n) * (w + gap);
}
inline float top(size_t index, float width, float scroll) { return list_top + (index / columns(width)) * step(width) - scroll; }
inline float maxScroll(size_t count, float width, float height) {
    if (!count) return 0;
    const size_t rows = (count + columns(width) - 1) / columns(width);
    return std::max(0.0f, list_top + rows * step(width) - height + 56.0f); // Clear of the button hints.
}
inline std::pair<size_t, size_t> visibleRange(size_t count, float width, float height, float scroll) {
    const auto cols = static_cast<size_t>(columns(width));
    const size_t first_row = static_cast<size_t>(std::max(0.0f, (scroll - list_top) / step(width)));
    const size_t last_row = static_cast<size_t>(std::max(0.0f, (scroll + height - list_top) / step(width))) + 1;
    return {std::min(count, first_row * cols), std::min(count, last_row * cols)};
}
}

// Category tabs in the library's top bar, in place of its title (every view): the shown category
// (in a store's or a console's library: the store or console) and its neighbours. The first tab
// starts at the left; later selections sit in the middle. Between Back and the filters
// (library_ui::Layout: filters().left - 24 = width - 504), with room on both sides for the small
// LB/RB (PgUp/PgDn) hints (hint_room: Renderer::drawCategoryStrip).
namespace library_tabs {
constexpr float top = 35, height = 40, hint_room = 64, left = 72 + hint_room;
inline float right(float width) { return width - 504 - hint_room; }
inline float slot(float width) { return std::clamp((right(width) - left) / 3, 190.0f, 320.0f); } // Between tab centres.
inline float center(float width) { return left + 1.5f * slot(width); }
inline float focusOffset(float current) { return std::clamp(current, 0.0f, 1.0f) - 1; }
// The tab `offset` places from the shown one (fractional while gliding).
inline settings_ui::Rect tab(float offset, float width, float current) {
    const float c = center(width) + (offset + focusOffset(current)) * slot(width), half = slot(width) * 0.5f - 10;
    return {c - half, top, c + half, top + height};
}
// Offset from the active tab; the first selection exposes two following tabs.
inline std::optional<int> hit(float x, float y, float width, int current, float dy = 0) {
    if (y < top + dy || y >= top + dy + height) return std::nullopt;
    const float offset = (x - center(width)) / slot(width);
    if (std::abs(offset) > 1.5f) return std::nullopt;
    return static_cast<int>(std::lround(offset - focusOffset(static_cast<float>(current))));
}
// The same strip on the Metro, Flix and PSfive home screens (AppSettings::*_category_tabs): at the
// top in the middle of the screen, three tabs as in the library (the first one at the left, later
// selections in the middle).
namespace home {
inline float slot(float width) { return std::clamp(width * 0.11f, 160.0f, 210.0f); }
inline float center(float width) { return width * 0.5f; }
inline settings_ui::Rect tab(float offset, float width, float current) {
    const float c = center(width) + (offset + focusOffset(current)) * slot(width), half = slot(width) * 0.5f - 10;
    return {c - half, top, c + half, top + height};
}
// Offset from the active category.
inline std::optional<int> hit(float x, float y, float width, int current, float dy = 0) {
    if (y < top + dy || y >= top + dy + height) return std::nullopt;
    const float offset = (x - center(width)) / slot(width);
    if (std::abs(offset) > 1.5f) return std::nullopt;
    return static_cast<int>(std::lround(offset - focusOffset(static_cast<float>(current))));
}
}
}

// Przegląd: the focused game's panel on the left (square cover, name, platform, facts, description, two
// buttons), landscape tiles in a grid beside it, under the category tabs.
namespace overview_layout {
constexpr float left = 68, list_top = 190, gap = 14, aspect = 920.0f / 430.0f, min_tile = 172, bottom_margin = 56; // Clear of the search field and button hints.
inline float panelWidth(float width) { return std::clamp(width * 0.18f, 250.0f, 340.0f); }
inline float gridLeft(float width) { return left + panelWidth(width) + 36; }
inline float gridRight(float width) { return width - 68; }
inline int columns(float width, int requested = 0) {
    return requested ? std::clamp(requested, 2, 5) : std::clamp(static_cast<int>((gridRight(width) - gridLeft(width) + gap) / (min_tile + gap)), 2, 5);
}
inline float cellWidth(float width, int n = 0) { return (gridRight(width) - gridLeft(width) - (columns(width, n) - 1) * gap) / columns(width, n); }
inline float cell(float width, int n = 0) { return cellWidth(width, n) / aspect; }
inline float step(float width, int n = 0) { return cell(width, n) + gap; }
inline float x(size_t index, float width, int n = 0) { return gridLeft(width) + (index % columns(width, n)) * (cellWidth(width, n) + gap); }
inline float top(size_t index, float width, float scroll, int n = 0) { return list_top + (index / columns(width, n)) * step(width, n) - scroll; }
inline float maxScroll(size_t count, float width, float height, int n = 0) {
    if (!count) return 0;
    const auto rows = (count + columns(width, n) - 1) / columns(width, n);
    return std::max(0.0f, rows * step(width, n) - gap - (height - bottom_margin - list_top));
}
inline std::pair<size_t, size_t> visibleRange(size_t count, float width, float height, float scroll, int n = 0) {
    const auto cols = static_cast<size_t>(columns(width, n));
    const auto first = static_cast<size_t>(std::max(0.0f, scroll / step(width, n)));
    const auto last = static_cast<size_t>(std::max(0.0f, (scroll + height - list_top) / step(width, n))) + 1;
    return {std::min(count, first * cols), std::min(count, last * cols)};
}
struct Panel {
    float width, height;
    float right() const { return left + panelWidth(width); }
    // Square art, leaving room for the facts and actions on shorter screens.
    settings_ui::Rect cover() const {
        const float size = std::min(panelWidth(width), std::max(1.0f, (height - list_top - bottom_margin) * 0.56f));
        return {left, list_top, left + size, list_top + size};
    }
    // "Szczegóły" (0) and the primary action (1) at the bottom.
    settings_ui::Rect button(int index) const {
        const float w = (panelWidth(width) - 10) * 0.5f, x = left + index * (w + 10), bottom = height - bottom_margin;
        return {x, bottom - 44, x + w, bottom};
    }
    int hitButton(float x, float y) const { for (int i = 0; i < 2; ++i) if (button(i).contains(x, y)) return i; return -1; }
};
}

// Lista+: a fixed poster, a central title/playtime browser, and the game's artwork and metadata.
namespace list_plus_layout {
struct Layout {
    float width, height;
    float bottom() const { return height - list_layout::bottom_margin; }
    settings_ui::Rect poster() const {
        const float w = std::min((bottom() - list_layout::list_top) * (2.0f / 3.0f), (width - 136) * 0.28f);
        return {68, list_layout::list_top, 68 + w, list_layout::list_top + w * 1.5f};
    }
    float listLeft() const { return poster().right + 32; }
    float listRight() const { return listLeft() + (width - 68 - listLeft() - 32) * 0.48f; }
    float infoLeft() const { return listRight() + 32; }
    float infoWidth() const { return width - 68 - infoLeft(); }
    settings_ui::Rect hero() const {
        return {infoLeft(), list_layout::list_top, width - 68,
            list_layout::list_top + std::min(infoWidth() * (9.0f / 16.0f), (bottom() - list_layout::list_top) * 0.4f)};
    }
};
}

// Shared drawing/hit-test geometry of the larger inline detail panel and its actions.
namespace library_presentation {
constexpr int action_count = 2; // Primary action and the compact game menu.
inline float listRight(float width) { return std::clamp(width * 0.33f, 420.0f, 600.0f); } // Room for longer titles.
struct Layout {
    float width, height;
    float left() const { return listRight(width) + 44; }
    float right() const { return width - 68; }
    float heroHeight() const { return std::clamp((height - list_layout::list_top - list_layout::bottom_margin) * 0.46f, 250.0f, 340.0f); }
    settings_ui::Rect hero() const { return {left(), 190, right(), 190 + heroHeight()}; }
    settings_ui::Rect logo() const { return {left() + 28, hero().top + 28, left() + std::min(600.0f, (right() - left()) * 0.72f), hero().bottom - 130}; }
    float factsCenter() const { return hero().bottom - 104; }
    float actionTop() const { return hero().bottom - 76; }
    settings_ui::Rect action(int index) const {
        const float primary_width = std::min(218.0f, right() - left() - 116);
        const float x = left() + 28 + (index == 0 ? 0 : primary_width + 12);
        return {x, actionTop(), x + (index == 0 ? primary_width : 48), actionTop() + 48};
    }
    settings_ui::Rect description() const { return {left(), hero().bottom + 18, left() + (right() - left() - 18) * 0.55f, height - list_layout::bottom_margin}; }
    settings_ui::Rect facts() const { return {description().right + 18, description().top, right(), description().bottom}; }
    int hitAction(float x, float y) const {
        for (int i = 0; i < action_count; ++i) if (action(i).contains(x, y)) return i;
        return -1;
    }
};
}

// Geometry of every library view (other grid screens always pass View::grid).
namespace library_layout {
using library_view::View;
inline float rowLeft(View view, float width, float height) {
    return view == View::list_plus ? list_plus_layout::Layout{width, height}.listLeft() : list_layout::left;
}
inline float rowRight(View view, float width, float height) {
    return view == View::list_plus ? list_plus_layout::Layout{width, height}.listRight() :
        view == View::detailed ? library_presentation::listRight(width) : list_layout::right(width);
}
inline float rowThumbLeft(View view, float width, float height) {
    return rowLeft(view, width, height) + (list_layout::row_height - list_layout::thumb) * 0.5f;
}
inline bool cinematicColumn(View view, const library_view::Options& options) { return view == View::cinematic && options.cinematic_column; }
inline bool gridLandscape(View view, const library_view::Options& options) { return view == View::grid && options.grid_landscape; }
inline int columns(View view, float width, const library_view::Options& options = {}) {
    if (view == View::cinematic && (options.cinematic_horizontal || options.cinematic_column)) return 1;
    if (library_view::carousel(view)) return 1;
    if (gridLandscape(view, options)) return landscape_grid::columns(width);
    return library_view::rows(view) ? 1 : view == View::cinematic ? cinematic_layout::columns(width) :
        view == View::overview ? overview_layout::columns(width, options.overview_columns) : grid_layout::columns(width);
}
// Top of the item's box that must stay on screen and its height (a grid cell with its title).
inline float top(View view, size_t index, float width, float scroll, const library_view::Options& options = {}) {
    if (cinematicColumn(view, options)) return cinematic_column::top(index, width, scroll);
    if (gridLandscape(view, options)) return landscape_grid::top(index, width, scroll);
    return library_view::rows(view) ? list_layout::top(index, scroll) :
        view == View::cinematic ? cinematic_layout::top(index, width, scroll) :
        view == View::overview ? overview_layout::top(index, width, scroll, options.overview_columns) : grid_layout::top(index, width, scroll, view == View::grid && options.grid_posters);
}
inline float extent(View view, float width, const library_view::Options& options = {}) {
    if (view == View::overview) return overview_layout::cell(width, options.overview_columns);
    if (cinematicColumn(view, options)) return cinematic_column::cell(width);
    if (gridLandscape(view, options)) return landscape_grid::cell(width) + 48;
    return library_view::rows(view) ? list_layout::row_height : (view == View::cinematic ? cinematic_layout::cell(width) : grid_layout::cell(width) * (view == View::grid && options.grid_posters ? 1.5f : 1)) + 48;
}
inline float maxScroll(View view, size_t count, float width, float height, const library_view::Options& options = {}) {
    if (view == View::cinematic && options.cinematic_horizontal) return cinematic_row::maxScroll(count, width);
    if (cinematicColumn(view, options)) return cinematic_column::maxScroll(count, width, height);
    if (gridLandscape(view, options)) return landscape_grid::maxScroll(count, width, height);
    if (library_view::carousel(view)) return 0;
    return library_view::rows(view) ? list_layout::maxScroll(count, height) :
        view == View::cinematic ? cinematic_layout::maxScroll(count, width, height) :
        view == View::overview ? overview_layout::maxScroll(count, width, height, options.overview_columns) : grid_layout::maxScroll(count, width, height, view == View::grid && options.grid_posters);
}
// Kinowy's capsule targets for the three scrolling options (x, y and the height; 920:430).
inline void cinematicTarget(size_t index, float width, float height, float scroll, const library_view::Options& options, float& x, float& y, float& size) {
    if (options.cinematic_column) {
        size = cinematic_column::cell(width); x = cinematic_column::x(width); y = cinematic_column::top(index, width, scroll);
    } else if (options.cinematic_horizontal) {
        size = cinematic_row::cellWidth(width) / cinematic_layout::aspect; x = cinematic_row::x(index, width, scroll); y = cinematic_row::top(width, height);
    } else {
        size = cinematic_layout::cell(width); x = cinematic_layout::x(index, width); y = cinematic_layout::top(index, width, scroll);
    }
}
inline std::pair<size_t, size_t> visibleRange(View view, size_t count, float width, float height, float scroll, const library_view::Options& options = {}) {
    if (cinematicColumn(view, options)) return cinematic_column::visibleRange(count, width, height, scroll);
    if (gridLandscape(view, options)) return landscape_grid::visibleRange(count, width, height, scroll);
    if (view == View::cinematic && options.cinematic_horizontal) {
        const auto first = static_cast<size_t>(std::max(0.0f, std::floor((scroll - cinematic_row::left) / cinematic_row::step(width))));
        const auto end = static_cast<size_t>(std::max(0.0f, (scroll + width) / cinematic_row::step(width))) + 1;
        return {std::min(first, count), std::min(end, count)};
    }
    return library_view::rows(view) ? list_layout::visibleRange(count, height, scroll) :
        view == View::cinematic ? cinematic_layout::visibleRange(count, width, height, scroll) :
        view == View::overview ? overview_layout::visibleRange(count, width, height, scroll, options.overview_columns) : grid_layout::visibleRange(count, width, height, scroll, view == View::grid && options.grid_posters);
}
// Scroll that keeps `index` visible below the header.
inline float reveal(View view, size_t index, size_t count, float width, float height, float scroll, const library_view::Options& options = {}) {
    if (view == View::cinematic && options.cinematic_horizontal) {
        const float x = cinematic_row::x(index, width, scroll), w = cinematic_row::cellWidth(width);
        if (x < cinematic_row::left) scroll += x - cinematic_row::left;
        else if (x + w > width - cinematic_row::left) scroll += x + w - width + cinematic_row::left;
        return std::clamp(scroll, 0.0f, cinematic_row::maxScroll(count, width));
    }
    if (cinematicColumn(view, options)) return cinematic_column::reveal(index, count, width, height);
    if (library_view::carousel(view)) return 0;
    if (library_view::rows(view)) {
        // Rows scroll by whole rows (no half row cut under the column headings) and keep one row of
        // look-ahead above and below the focused one, so the list shows what comes next.
        const float view_height = height - list_layout::bottom_margin - list_layout::list_top;
        const int visible = std::max(1, static_cast<int>(std::floor((view_height - list_layout::row_height) / list_layout::step)) + 1);
        const int at = static_cast<int>(index);
        int first = static_cast<int>(std::lround(std::max(0.0f, scroll) / list_layout::step));
        const int margin = visible > 2 ? 1 : 0;
        if (at < first + margin) first = at - margin;
        if (at > first + visible - 1 - margin) first = at - visible + 1 + margin;
        first = std::clamp(first, 0, std::max(0, static_cast<int>(count) - visible));
        return static_cast<float>(first) * list_layout::step;
    }
    const float item = top(view, index, width, scroll, options), size = extent(view, width, options);
    const float edge = view == View::cinematic ? cinematic_layout::list_top : view == View::overview ? overview_layout::list_top : 155;
    // Clear of the button hints along the bottom edge, as the lists.
    const float bottom = height - (view == View::overview ? overview_layout::bottom_margin : view == View::cinematic ? 30 : list_layout::bottom_margin);
    if (item < edge) scroll = std::max(0.0f, scroll + item - edge);
    else if (item + size > bottom) {
        scroll += item + size - bottom;
        // Whole rows keep captions together with their capsules below the spotlight.
        if (view == View::cinematic) scroll = std::ceil(scroll / cinematic_layout::step(width)) * cinematic_layout::step(width);
    }
    return std::clamp(scroll, 0.0f, maxScroll(view, count, width, height, options));
}
} // namespace library_layout
