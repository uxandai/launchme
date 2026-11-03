#pragma once
// Glance and Towarzyszka on the home screen (Settings → System), placed by each layout:
// - default (shelves): the right column, her tile on top and Glance's shortcut under it;
// - PSfive: the same two tiles side by side in the bottom-right corner, above the hints;
// - Flix: two slim pills beside the platform badge in the top-right corner (no figure, no bubble);
// - Metro and XMB: tiles of the layout itself (src/app/metro.cpp, src/app/xmb.cpp), so nothing floats.
// The keys (G or R2/RT, C or L2/LT) open them on every layout. Shared by drawing
// (glance_renderer.cpp, companion_renderer.cpp) and the pointer (src/app).
#include "companion_view.hpp"
#include "glance_view.hpp"

#include <optional>

namespace home_widgets {
using settings_ui::Rect;
enum class Placement { column, corner, pills, tiles };
// Free-standing tiles (not one of the layout's own): Glance's shortcut and her tile with its head above it.
inline bool floating(Placement placement) { return placement == Placement::column || placement == Placement::corner; }
// Her speech bubble has somewhere to point: everywhere but the Flix pills.
inline bool speaks(Placement placement) { return placement != Placement::pills; }

struct Spots {
    Placement placement = Placement::column;
    std::optional<Rect> glance;    // Glance's tile or pill, at rest (no hover).
    std::optional<Rect> companion; // Her tile or pill.
};

// PSfive: right-aligned with its clock, clear of the hints bar under the corner.
inline constexpr float corner_right = 72, corner_bottom = 64, corner_gap = 18, tile_width = 300;
inline constexpr float companion_height = companion_ui::tile_bottom - companion_ui::tile_top;
// Flix: in the platform badge's band (home_chrome_renderer.cpp drawPlatformBadge: top 42, 40 tall, its name
// in the last 110 px before 57 px from the edge), left of the badge's slot.
inline constexpr float pill_top = 42, pill_height = 40, pill_gap = 10, badge_slot = 57 + 110 + 16;
inline constexpr float glance_pill_width = 132, companion_pill_width = 142;

inline Spots spots(Placement placement, float width, float height, bool glance, bool companion) {
    Spots result;
    result.placement = placement;
    switch (placement) {
    case Placement::column:
        if (companion) result.companion = companion_ui::tile(width);
        if (glance) result.glance = glance_ui::shortcut(width, 0, companion ? companion_ui::glance_top : glance_ui::shortcut_top);
        break;
    case Placement::corner: {
        // Glance in the corner, her tile left of it: her bubble opens further left, over the picture.
        const float bottom = height - corner_bottom;
        float right = width - corner_right;
        if (glance) {
            result.glance = Rect{right - tile_width, bottom - glance_ui::shortcut_height, right, bottom};
            right -= tile_width + corner_gap;
        }
        if (companion) result.companion = Rect{right - tile_width, bottom - companion_height, right, bottom};
        break;
    }
    case Placement::pills: {
        float right = width - badge_slot;
        if (glance) {
            result.glance = Rect{right - glance_pill_width, pill_top, right, pill_top + pill_height};
            right -= glance_pill_width + pill_gap;
        }
        if (companion) result.companion = Rect{right - companion_pill_width, pill_top, right, pill_top + pill_height};
        break;
    }
    case Placement::tiles: break;
    }
    return result;
}

// Flix once had (an option, now gone) every quick-bar icon as
// a small circle in the pills' band, Glance's icon after them and her avatar nearest the badge.
// `total` counts them all; the quick bar's own order comes first.
namespace icons {
inline constexpr float size = 40, gap = 8;
inline Rect at(int index, int total, float width) {
    const float right = width - badge_slot - static_cast<float>(total - 1 - index) * (size + gap);
    return {right - size, pill_top, right, pill_top + size};
}
inline int hit(float x, float y, int total, float width) {
    for (int i = 0; i < total; ++i) {
        const auto r = at(i, total, width);
        const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f, radius = size * 0.5f + 3;
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius) return i;
    }
    return -1;
}
// The same spots as icons: Glance after the quick ones, her avatar last.
inline void place(Spots& spots, int quick, float width) {
    const int total = quick + (spots.glance ? 1 : 0) + (spots.companion ? 1 : 0);
    if (spots.glance) spots.glance = at(quick, total, width);
    if (spots.companion) spots.companion = at(total - 1, total, width);
}
}

// Hover lifts a tile a little (as glance_ui::shortcut); a pill stays put.
inline Rect glanceArea(const Spots& spots, float hover) {
    if (!spots.glance) return {};
    auto r = *spots.glance;
    if (floating(spots.placement)) { r.left -= hover * 3; r.top -= hover * 4; r.right += hover * 3; }
    return r;
}
// Her tile with the head above it, or her pill.
inline Rect companionArea(const Spots& spots) {
    if (!spots.companion) return {};
    return floating(spots.placement) ? companion_ui::hitArea(*spots.companion) : *spots.companion;
}
}
