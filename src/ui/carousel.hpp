#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace carousel {
inline int distance(size_t index, size_t selected, size_t count) {
    (void)count;
    return static_cast<int>(index) - static_cast<int>(selected);
}

inline float smallSize(size_t count, float width, bool fixed_row) {
    constexpr float inset = 56.0f, gap = 12.0f;
    if (!fixed_row || !count) return 140.0f;
    return std::min(140.0f, (width - 2 * inset - (count - 1) * gap) /
                            static_cast<float>(count + 1));
}

// Ostatnio grane as one fixed line: up to its usual 9 tiles (8 games and Wszystkie), or more while
// they keep that row's size; past it the row scrolls instead of every tile shrinking.
inline constexpr size_t fixed_row_cards = 9;
inline bool fitsFixedRow(size_t count, float width) {
    return count <= fixed_row_cards || smallSize(count, width, true) >= smallSize(fixed_row_cards, width, true) - 0.5f;
}

inline float left(size_t index, size_t selected, size_t count, float width, bool fixed_row) {
    constexpr float inset = 56.0f, gap = 12.0f;
    const float compact_size = smallSize(count, width, fixed_row);
    if (fixed_row)
        return inset + static_cast<float>(index) * (compact_size + gap) +
               (index > selected ? compact_size : 0);
    const int offset = distance(index, selected, count);
    if (offset == 0) return inset;
    if (offset > 0) return inset + 2 * compact_size + gap + (offset - 1) * (compact_size + gap);
    return inset - compact_size - gap + (offset + 1) * (compact_size + gap);
}

// One past the last card of a scrolling row that starts on screen (`width` wide, from its
// left edge): every card the row shows, however wide the screen.
inline size_t visibleEnd(size_t selected, size_t count, float width) {
    size_t end = selected + 1;
    while (end < count && left(end, selected, count, width, false) < width) ++end;
    return std::min(end, count);
}

inline float bottom(float height) { return height - 86.0f; }
} // namespace carousel
