#pragma once
// Blades: an Xbox 360-style home, a mix of the 2005 blades and the 2008 panels. The categories are
// tabs fanned along an arc on the left (the channel in focus sticks out, in the accent); the games
// of the channel are panels receding to the right along a second, rising arc, the focused one in front.
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace blades_layout {
// The arc of the channel tabs: a circle whose centre lies far left of the screen.
inline float arcRadius(float height) { return height * 1.05f; }
inline float arcCenterX(float height) { return 330 - arcRadius(height); }
inline float arcCenterY(float height) { return height * 0.47f; }
inline constexpr float tab_step = 0.085f; // Radians between neighbouring tabs.
inline constexpr float tab_width = 300, tab_height = 52, focus_tab_height = 64;

// The panels.
inline float focusSize(float height) { return std::clamp(height * 0.40f, 260.0f, 460.0f); }
inline float panelsLeft(float height) { return arcCenterX(height) + arcRadius(height) + 70; }
inline float panelsCenterY(float height) { return arcCenterY(height); }
inline constexpr float shrink = 0.80f; // Each panel further back is this much smaller.
inline float panelSize(int offset, float height) {
    const float base = focusSize(height);
    if (offset <= 0) return offset == 0 ? base : base * shrink;
    return base * std::pow(shrink, static_cast<float>(offset));
}
inline float panelLeft(int offset, float height) {
    const float base = focusSize(height), left = panelsLeft(height);
    if (offset == 0) return left;
    if (offset < 0) return left + offset * (base * shrink + 28);
    float x = left + base + 34;
    for (int k = 1; k < offset; ++k) x += panelSize(k, height) * 0.86f;
    return x;
}
// Panels further back rise along the arc: their centres climb as they shrink.
inline float panelTop(int offset, float height) {
    const float size = panelSize(offset, height), base = focusSize(height);
    const float rise = offset > 0 ? (base - size) * 0.62f : 0.0f;
    return panelsCenterY(height) - size * 0.5f - rise;
}
// Panels fade with depth; those that passed the front go quickly.
inline float panelAlpha(int offset) {
    if (offset < 0) return std::max(0.0f, 0.55f + offset * 0.45f);
    return std::max(0.0f, 1.0f - offset * 0.11f);
}
} // namespace blades_layout
