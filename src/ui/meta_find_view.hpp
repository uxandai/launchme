#pragma once
// "Znajdź" in Edytuj metadane: the pane over the dialog where the player searches the stores and databases
// (src/library/metadata_sources.hpp), picks a game and chooses which of its details to take. Pure state and
// geometry; the Launcher side is src/app/metadata_find.cpp, the drawing src/ui/meta_find_renderer.cpp.
#include "settings_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace meta_find {
using settings_ui::Rect;

inline constexpr int take_count = 5;   // Opis, Producent, Wydawca, Data premiery, Gatunki.
inline constexpr int visible = 6;      // Results shown at once.
// The Edytuj metadane field each detail goes into (desktop_ui::MetaItem order).
inline constexpr std::array<int, take_count> target_field{1, 2, 3, 4, 5};

// What the pad and Tab walk through (one number each).
namespace item {
inline constexpr int query = 0, search = 1;
inline constexpr int result_first = 2;                    // …result_first + visible - 1
inline constexpr int take_first = result_first + visible; // …take_first + take_count - 1
inline constexpr int insert = take_first + take_count, back = insert + 1, close = back + 1;
} // namespace item

struct Result {
    std::wstring title;
    std::wstring source;                // "Nintendo eShop", "Steam"…
    std::wstring note;                  // The edition's console or store.
    std::array<std::wstring, take_count> values; // As they would be inserted.
};

struct State {
    bool open = false;
    std::wstring query;
    size_t caret = 0;
    bool selected = false;              // Ctrl+A in the query.
    bool busy = false;                  // Sources still answering.
    int waiting = 0;                    // How many.
    std::wstring status;                // "Szukanie…", or why nothing was found.
    std::vector<Result> results;        // Best first.
    std::vector<int> scores;            // Parallel to results (ordering only).
    std::wstring seed;                  // The title the pane opened with: only a search for it uses the game's ids.
    int chosen = 0;                     // The highlighted result.
    std::array<bool, take_count> take{};
    int focus = item::query;
    int hover = -1;
    uint32_t generation = 0;            // A newer search drops the older answers.

    bool has(int index) const { return index >= 0 && index < static_cast<int>(results.size()); }
    const Result* current() const { return has(chosen) ? &results[static_cast<size_t>(chosen)] : nullptr; }
    bool canTake(int slot) const { const auto* r = current(); return r && !r->values[static_cast<size_t>(slot)].empty(); }
    bool anyTaken() const { for (int i = 0; i < take_count; ++i) if (take[static_cast<size_t>(i)] && canTake(i)) return true; return false; }
};

struct Layout {
    float width = 0, height = 0;
    static constexpr float line_h = 38, row_h = 36, take_h = 32, button_h = 38, search_w = 124;
    Rect panel() const {
        const float w = std::min(700.0f, width - 48), h = std::min(660.0f, height - 48);
        const float left = std::round((width - w) * 0.5f), top = std::round((height - h) * 0.5f);
        return {left, top, left + w, top + h};
    }
    Rect close() const { const auto p = panel(); return {p.right - 46, p.top + 12, p.right - 14, p.top + 44}; }
    Rect field() const { const auto p = panel(); const float y = p.top + 76 + 22; return {p.left + 24, y, p.right - 24 - search_w - 12, y + line_h}; }
    Rect searchButton() const { const auto f = field(); const auto p = panel(); return {p.right - 24 - search_w, f.top, p.right - 24, f.bottom}; }
    float resultsTop() const { return field().bottom + 12; }
    Rect result(int slot) const { const auto p = panel(); const float y = resultsTop() + static_cast<float>(slot) * row_h; return {p.left + 24, y, p.right - 24, y + row_h - 4}; }
    float takesTop() const { return resultsTop() + visible * row_h + 22; } // Under the "Wstaw:" caption.
    Rect take(int slot) const { const auto p = panel(); const float y = takesTop() + static_cast<float>(slot) * take_h; return {p.left + 24, y, p.right - 24, y + take_h - 4}; }
    Rect insertButton() const { const auto p = panel(); const float y = p.bottom - 24 - button_h; return {p.right - 24 - 150 - 12 - 130, y, p.right - 24 - 150 - 12, y + button_h}; }
    Rect backButton() const { const auto p = panel(); const float y = p.bottom - 24 - button_h; return {p.right - 24 - 150, y, p.right - 24, y + button_h}; }
    // An Item, or -1.
    int hit(float x, float y, const State& state) const {
        if (close().contains(x, y)) return item::close;
        if (field().contains(x, y)) return item::query;
        if (searchButton().contains(x, y)) return item::search;
        for (int i = 0; i < visible; ++i) if (state.has(i) && result(i).contains(x, y)) return item::result_first + i;
        for (int i = 0; i < take_count; ++i) if (take(i).contains(x, y)) return item::take_first + i;
        if (insertButton().contains(x, y)) return item::insert;
        if (backButton().contains(x, y)) return item::back;
        return -1;
    }
};
} // namespace meta_find
