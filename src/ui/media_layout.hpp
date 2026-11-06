#pragma once
// The physical media drawn in Półka's open case (game_media_renderer.cpp): their outlines and
// labels as fractions of their own rectangles.
#include "settings_view.hpp"
#include <array>

namespace media_layout {
enum class Kind { floppy, cartridge };
// Width : height. A 3.5" diskette is 90 x 94 mm; a Switch game card 21 x 31 mm.
inline float aspect(Kind kind) { return kind == Kind::floppy ? 0.957f : 0.68f; }
struct Point { float x, y; };
struct Outline { std::array<Point, 10> points{}; size_t count = 0; };
// The diskette's outline (its cut corner at the top right).
inline Outline floppyOutline(settings_ui::Rect r) {
    Outline shape{{{{0.02f, 0}, {0.92f, 0}, {1, 0.07f}, {1, 0.98f}, {0.98f, 1}, {0.02f, 1}, {0, 0.98f}, {0, 0.02f}}}, 8};
    for (size_t i = 0; i < shape.count; ++i) {
        auto& p = shape.points[i]; p.x = r.left + p.x * (r.right - r.left); p.y = r.top + p.y * (r.bottom - r.top);
    }
    return shape;
}
// A game card as Nintendo moulds it (both Switches; the DS, 3DS and Vita cards drawn alike): a
// rounded shell, a little narrower along its top (its shoulders), a slot moulded at the top's middle,
// the label over most of its face, a small arrow under it pointing to the contacts. Of the width
// (corner, shoulder) and of the height (the rest).
namespace card {
constexpr float corner = 0.075f, top_corner = 0.05f;
constexpr float shoulder = 0.024f, shoulder_depth = 0.075f;    // The top's inset at each side, how far down.
constexpr float slot_left = 0.36f, slot_right = 0.64f, slot_top = 0.014f, slot_bottom = 0.052f;
constexpr float band = 0.24f, code = 0.10f;                    // The label's console band and its code strip, of the label's height.
constexpr float arrow_y = 0.94f, arrow_size = 0.032f;
}
inline settings_ui::Rect label(Kind kind, settings_ui::Rect r) {
    const float w = r.right - r.left, h = r.bottom - r.top;
    if (kind == Kind::floppy) return {r.left + w * 0.165f, r.top + h * 0.375f, r.left + w * 0.835f, r.top + h * 0.985f};
    return {r.left + w * 0.085f, r.top + h * 0.085f, r.left + w * 0.915f, r.top + h * 0.865f};
}
}
