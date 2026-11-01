#pragma once
#include "settings_view.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

// Pobierania (the quick bar's tab after the plugins' pages): every game downloading, updating,
// waiting or paused in Steam, GOG, Epic and Battle.net, with its progress. The games are the
// screen's cards (covers); `rows` holds what is shown for each, in the same order
// (src/app/downloads.cpp, src/ui/downloads_renderer.cpp).
namespace downloads_ui {
struct Row {
    std::wstring store;   // "Steam", "GOG"…
    std::wstring status;  // "Pobieranie", "W kolejce", "Aktualizacja · Pobieranie"…
    std::wstring amount;  // "1,2 / 3,4 GB"; empty when the store gives no sizes.
    std::optional<float> fraction; // 0..1; none: no reliable share yet.
    bool moving = false;  // Transferring now (not queued or paused).
    std::wstring destination;
    std::string plugin, command;
};
struct State {
    std::vector<Row> rows;
    // The bar each row draws: its fraction eased (the stores report in steps), same order as rows;
    // a negative value or a missing entry: the row's own fraction.
    std::vector<float> bars;
    float bar(size_t index, const Row& row) const {
        if (index < bars.size() && bars[index] >= 0) return bars[index];
        return row.fraction.value_or(0.0f);
    }
    bool back = false;    // Back (top left) has the focus instead of a row.
    float scroll = 0;
};
struct Layout {
    float width, height;
    static constexpr float top = 166, row_height = 128, step = 140, left = 64;
    settings_ui::Rect back() const { return {64, 58, 108, 102}; }
    float right() const { return width - 64; }
    float bottom() const { return height - 72; } // Clear of the button hints.
    settings_ui::Rect row(int index, float scroll) const {
        const float y = top + index * step - scroll;
        return {left, y, right(), y + row_height};
    }
    float maxScroll(int count) const { return std::max(0.0f, count * step - (step - row_height) - (bottom() - top)); }
    // The scroll that keeps row `index` in view.
    float reveal(int index, int count, float scroll) const {
        const auto r = row(index, scroll);
        if (r.top < top) scroll -= top - r.top;
        else if (r.bottom > bottom()) scroll += r.bottom - bottom();
        return std::clamp(scroll, 0.0f, maxScroll(count));
    }
    int hit(float x, float y, int count, float scroll) const {
        if (y < top || y >= bottom()) return -1;
        for (int i = 0; i < count; ++i) if (row(i, scroll).contains(x, y)) return i;
        return -1;
    }
};
}
