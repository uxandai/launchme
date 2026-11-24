#pragma once
// The grid screens' columns, shared by the library grids and the stores' frame (no other includes).
#include <algorithm>
#include <cstddef>

namespace grid_layout {
// requested: the library's Siatka option (3..10 columns filling the width); 0 by the width, at most 186 wide.
inline int columns(float width, int requested = 0) {
    return requested ? std::clamp(requested, 3, 10) : std::clamp(static_cast<int>((width - 120) / 202), 3, 8);
}
inline float cell(float width, int requested = 0) {
    const int count = columns(width, requested);
    const float fit = (width - 112.0f - (count - 1) * 25.0f) / count;
    return requested ? fit : std::min(186.0f, fit);
}
inline float left(float width, size_t index, int requested = 0) {
    const int count = columns(width, requested);
    const float size = cell(width, requested), gap = 25.0f;
    const float total = count * size + (count - 1) * gap;
    return (width - total) * 0.5f + (index % count) * (size + gap);
}
} // namespace grid_layout
