#pragma once
// The grid screens' columns, shared by the library grids and the stores' frame (no other includes).
#include <algorithm>
#include <cstddef>

namespace grid_layout {
inline int columns(float width) { return std::clamp(static_cast<int>((width - 120) / 202), 3, 8); }
inline float cell(float width) {
    const int count = columns(width);
    return std::min(186.0f, (width - 112.0f - (count - 1) * 25.0f) / count);
}
inline float left(float width, size_t index) {
    const int count = columns(width);
    const float size = cell(width), gap = 25.0f;
    const float total = count * size + (count - 1) * gap;
    return (width - total) * 0.5f + (index % count) * (size + gap);
}
} // namespace grid_layout
