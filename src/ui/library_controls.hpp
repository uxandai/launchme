#pragma once
#include "category_menu.hpp"
#include "grid_layout.hpp"
#include "settings_view.hpp"
#include "../library/view_options.hpp"
#include <cmath>

// Library screen controls (src/app/library_view.cpp, src/ui/library_renderer.cpp).
// Top bar: filters, search (magnifier), and an icon opening the left view drawer.
// Left in the first column also opens that drawer, styled like the shelves' collection menu.
namespace library_ui {
enum class Panel { none, filters, views };
// back_button: the glass circle at the top left.
constexpr int search_button = 0, filters_button = 1, views_button = 2, back_button = 3;
// The order's menu, two columns: Sortuj (the orders) and Filtry: Tylko zainstalowane, the game's
// modes (library_view::Filter) as check boxes, then Wyczyść filtry.
constexpr int installed_row = library_view::order_count;
constexpr int first_filter_row = installed_row + 1;
constexpr int filter_rows = first_filter_row + library_view::filter_count + 1;
constexpr int clear_row = filter_rows - 1;
// Zaawansowany filtr…: under the orders in the first column (the rule editor, smart_filter.hpp).
constexpr int advanced_row = filter_rows;
constexpr int menu_rows = advanced_row + 1;
inline int filterColumn(int row) { return row < library_view::order_count || row == advanced_row ? 0 : 1; }
// A row's place in its column, from the top.
inline int filterPlace(int row) { return row == advanced_row ? library_view::order_count : filterColumn(row) ? row - installed_row : row; }
// Up/Down within a column (no wrapping), Left/Right across to the row at the same height.
inline int filterMove(int row, int dx, int dy) {
    const int column = filterColumn(row), place = filterPlace(row);
    if (dx) {
        if ((dx > 0) == (column == 1)) return row;
        if (column == 0) return installed_row + place;
        return place >= library_view::order_count ? advanced_row : place;
    }
    if (column) return std::clamp(row + dy, installed_row, filter_rows - 1);
    const int next = std::clamp(place + dy, 0, library_view::order_count);
    return next == library_view::order_count ? advanced_row : next;
}
struct State {
    library_view::Options options;
    int advanced_rules = 0;   // Zaawansowany filtr: its rules in force (drawn beside its row, counted on the button).
    int focus = -1;           // Top bar button; -1 = the games have focus.
    int action = -1;          // Inline presentation buttons; -1 = focus is outside the preview.
    Panel panel = Panel::none;
    int choice = 0;           // Highlighted row of the open panel.
    bool view_dropdown = false;
    int view_row = 0;         // 0: selector; options follow; closeRow(view): pinned footer.
    // Library screen only: the magnifier opens a search field under the title that filters
    // the games below as it is typed (the home screen's search stays separate). Typing goes
    // to the field while the magnifier has focus.
    bool search = false;
    std::wstring query;
    size_t after = 0;         // Characters after the insertion point (LB/RB on the on-screen keyboard).
    size_t caret() const { return query.size() - std::min(after, query.size()); }
    bool open() const { return panel != Panel::none; }
    bool typing() const { return search && focus == search_button && !open(); }
};
// The top bar, left to right: Back (grid_layout::hitBack), the category tabs in place of the
// title (library_tabs, grid_layout.hpp), the filters, the magnifier, the view icon.
struct Layout {
    float width;
    static constexpr float row_step = 53, toggle_gap = 12, filters_width = 300, column_width = 300, column_gap = 12, heading_height = 42;
    settings_ui::Rect views() const { return {width - 64 - 46, 32, width - 64, 78}; }
    settings_ui::Rect search() const { const auto v = views(); return {v.left - 12 - 46, 32, v.left - 12, 78}; }
    settings_ui::Rect filters() const { const auto s = search(); return {s.left - 12 - library_tabs::filtersWidth(width), 32, s.left - 12, 78}; }
    settings_ui::Rect back() const { return {16, 35, 52, 71}; }
    settings_ui::Rect button(int index) const { return index == back_button ? back() : index == search_button ? search() : index == views_button ? views() : filters(); }
    // The search field unfolds into the free line under the top bar.
    settings_ui::Rect field() const { return {60, 92, filters().left - 24, 172}; }
    // The menu under its button, right-aligned with it: Sortuj on the left, Filtry (the taller) on the right.
    settings_ui::Rect menu() const {
        const auto f = filters();
        const float width = 2 * column_width + column_gap + 24;
        return {f.right - width, 90, f.right, 90 + 16 + heading_height + (filter_rows - installed_row) * row_step + toggle_gap};
    }
    settings_ui::Rect heading(int column) const {
        const auto m = menu();
        const float left = m.left + 12 + column * (column_width + column_gap);
        return {left, m.top + 8, left + column_width, m.top + 8 + heading_height};
    }
    settings_ui::Rect choice(int index) const {
        const auto m = menu();
        const int column = filterColumn(index), at = filterPlace(index);
        const float left = m.left + 12 + column * (column_width + column_gap);
        const float top = m.top + 8 + heading_height + at * row_step + (index == clear_row || index == advanced_row ? toggle_gap : 0);
        return {left, top, left + column_width, top + 48};
    }
    int hitChoice(float x, float y) const {
        for (int i = 0; i < menu_rows; ++i) if (choice(i).contains(x, y)) return i;
        return -1;
    }
};
// Full-height drawer. The selector expands above a pinned close button.
namespace views {
constexpr float width = 400, padding = 28;
inline int optionCount(library_view::View view) {
    using library_view::View;
    // Kinowy: its scrolling, then the platform, last played and playtime beside the year. Coverflow:
    // its variant; on Półka also the grey side cases and the platform band.
    // Siatka: its artwork, its number of columns, the collections as tiles above the games. Scena: its
    // short summary, then how dark its background is (one setting for every Scena).
    return view == View::cinematic ? 4 : view == View::list ? 3 : view == View::bookshelf ? 3 : view == View::grid ? 3 : view == View::stage ? 2 :
        view == View::overview || view == View::coverflow || view == View::list_plus ? 1 : 0;
}
inline int closeRow(library_view::View view) { return optionCount(view) + 1; }
inline float eased(float progress) { const float t = std::clamp(progress, 0.0f, 1.0f); return t * t * (3 - 2 * t); }
inline float offset(float progress) { return width * (eased(progress) - 1); }
inline settings_ui::Rect close(float height) { return {padding, height - 88, width - padding, height - 32}; }
inline float top(float) { return 152; }
inline float rowHeight(float height) { return std::min(54.0f, (close(height).top - 24 - top(height)) / library_view::view_count); }
inline float revealedBottom(float height, float progress) { return top(height) + rowHeight(height) * library_view::view_count * eased(progress); }
inline settings_ui::Rect selector() { return {padding, 64, width - padding, 128}; }
inline settings_ui::Rect option(int index) { return {padding, 160.0f + index * 92, width - padding, 240.0f + index * 92}; }
inline settings_ui::Rect panel(float height) { return {0, 0, width, height}; }
inline settings_ui::Rect track() { const auto r = option(0); return {r.left + 16, r.bottom - 20, r.right - 16, r.bottom - 16}; }
inline int columnsAt(float x) { const auto r = track(); return std::clamp(2 + static_cast<int>(std::lround((x - r.left) / (r.right - r.left) * 3)), 2, 5); }
inline int hit(float x, float y, float height, float progress = 1) {
    if (x < padding || x >= width - padding || y < top(height) || y >= revealedBottom(height, progress)) return -1;
    const float at = (y - top(height)) / rowHeight(height);
    const int row = static_cast<int>(at);
    const float inset = (at - row) * rowHeight(height);
    return inset >= 4 && inset < rowHeight(height) - 4 ? row : -1;
}
inline int hitOption(float x, float y, const State& state, float height, float dropdown_progress = 0) {
    if (close(height).contains(x, y)) return closeRow(state.options.view);
    if (selector().contains(x, y)) return 0;
    if (!state.view_dropdown && dropdown_progress < 0.01f)
        for (int i = 0; i < optionCount(state.options.view); ++i) if (option(i).contains(x, y)) return i + 1;
    return -1;
}
}
}
