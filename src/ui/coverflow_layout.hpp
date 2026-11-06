#pragma once
#include "../model/game_card.hpp"
#include "../library/view_options.hpp"
#include "perspective.hpp"
#include "settings_view.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>

// Półka: Cover Flow. The focused 2:3 poster faces front in the middle, the others stand turned on
// both sides in perspective, all above a glossy floor. A card's pose follows its continuous offset
// from the gliding centre (`flow`, Launcher::library_flow_), so covers turn as they pass the middle.
// Shared by drawing (shelf_renderer.cpp), picking (Launcher::cardHit) and the cards' targets.
namespace shelf_layout {
constexpr float aspect = 2.0f / 3.0f;
constexpr float turn = 1.2f;         // The side covers' turn, radians (about 69°).
constexpr float reflection = 0.32f;  // Share of a cover's height its reflection fades over.
constexpr float depth = 0.09f;       // A game case's thickness, of its width.
constexpr float corner = 0.012f;     // The case's corners: barely rounded plastic (of its width); the art is square.
struct Layout {
    float width, height;
    float coverHeight() const { return std::max(1.0f, std::min({height * 0.47f, width * 0.27f, 540.0f})); }
    float coverWidth() const { return coverHeight() * aspect; }
    // The focused cover's top: the cover, its fading reflection, the title and the facts are one
    // group, centred between the top bar (180) and the key hints.
    float top() const {
        const float h = coverHeight();
        return std::max(196.0f, 180 + (height - 72 - 180 - (h * 1.27f + 72)) * 0.5f);
    }
    float floor() const { return top() + coverHeight(); } // The plane the covers stand on.
    float centreX() const { return width * 0.5f; }
    // Centre lines, where the reflection has all but faded out.
    float titleY() const { return floor() + coverHeight() * 0.27f + 14; }
    float factsY() const { return titleY() + 38; }
    // From the centre cover: its neighbour's centre, the step between the side covers, how far back they stand.
    float gap() const { return coverWidth() * 1.02f; }
    float spacing() const { return coverWidth() * 0.44f; }
    float push() const { return coverWidth() * 0.80f; }
    // In DIPs (scale 1) or device pixels: the horizon runs through the focused cover's middle.
    perspective::Camera camera(float scale = 1) const {
        return {centreX() * scale, (top() + coverHeight() * 0.5f) * scale, coverHeight() * 2.7f * scale};
    }
    // `offset` from the gliding centre (0: facing front, ±1 and beyond: turned on its side), around
    // the camera's origin (the focused cover's centre). `lift` brings it towards the viewer (launch).
    perspective::Face face(float offset, float lift = 0, float scale = 1) const {
        using perspective::turned;
        const float w = coverWidth() * scale, h = coverHeight() * scale;
        const float t = std::clamp(offset, -1.0f, 1.0f), side = offset < 0 ? -1.0f : 1.0f, along = std::abs(offset);
        const float x = (along <= 1 ? offset * gap() : side * (gap() + (along - 1) * spacing())) * scale;
        const float z = (std::abs(t) * push() - lift) * scale;
        // A cover on the right turns its outer edge towards the viewer, facing the middle.
        const float angle = t * turn;
        const perspective::Vec3 centre{x, 0, z};
        return {centre + turned({-w * 0.5f, -h * 0.5f, 0}, angle), turned({w, 0, 0}, angle), turned({0, h, 0}, angle)};
    }
    std::array<D2D1_POINT_2F, 4> quad(float offset, float lift = 0) const {
        D2D1_POINT_2F corners[4];
        face(offset, lift).corners(camera(), corners);
        return {corners[0], corners[1], corners[2], corners[3]};
    }
    settings_ui::Rect bounds(float offset) const {
        const auto q = quad(offset);
        settings_ui::Rect r{q[0].x, q[0].y, q[0].x, q[0].y};
        for (const auto& p : q) { r.left = std::min(r.left, p.x); r.top = std::min(r.top, p.y); r.right = std::max(r.right, p.x); r.bottom = std::max(r.bottom, p.y); }
        return r;
    }
    static bool inside(const std::array<D2D1_POINT_2F, 4>& q, float x, float y) {
        bool in = false;
        for (size_t i = 0, j = 3; i < 4; j = i++)
            if ((q[i].y > y) != (q[j].y > y) && x < (q[j].x - q[i].x) * (y - q[i].y) / (q[j].y - q[i].y) + q[i].x) in = !in;
        return in;
    }
    // Side covers beyond the screen's edge are not drawn; how many fit on one side.
    size_t sideCount() const {
        const float first = bounds(1).right - centreX();
        const float step = spacing() / (1 + push() / camera().eye);
        return static_cast<size_t>(std::ceil(std::max(0.0f, centreX() - first) / std::max(1.0f, step))) + 2;
    }
    std::pair<size_t, size_t> visibleRange(size_t count, size_t centre) const {
        const size_t side = sideCount();
        return {centre > side ? centre - side : 0, std::min(count, centre + side + 1)};
    }
    // The topmost cover under a point: nearer the middle is drawn later, so it is tried first.
    std::optional<size_t> hit(size_t count, float flow, float x, float y) const {
        const auto centre = static_cast<ptrdiff_t>(std::lround(std::clamp(flow, 0.0f, static_cast<float>(count ? count - 1 : 0))));
        const auto side = static_cast<ptrdiff_t>(sideCount()) + 1;
        std::optional<size_t> best;
        float nearest = 1e9f;
        for (ptrdiff_t i = std::max<ptrdiff_t>(0, centre - side); i < std::min<ptrdiff_t>(static_cast<ptrdiff_t>(count), centre + side + 1); ++i) {
            const float offset = static_cast<float>(i) - flow;
            if (std::abs(offset) < nearest && inside(quad(offset), x, y)) { nearest = std::abs(offset); best = static_cast<size_t>(i); }
        }
        return best;
    }
};
}

// Flat portrait carousel. Shared by drawing, picking and the disc/next-poster clearance.
namespace coverflow_layout {
constexpr float aspect = 2.0f / 3.0f, compact = 0.74f, gap = 18;
constexpr float disc_radius = 0.43f, disc_exposure = 0.65f; // About one third of the disc protrudes.
inline settings_ui::Rect poster(const GameCard& card) { return {card.x, card.y, card.x + card.width(), card.y + card.size}; }
struct Disc { float x, y, radius; };
inline Disc disc(const GameCard& card) {
    const float w = card.width(), radius = w * disc_radius;
    return {card.x + w * 0.5f + card.disc * (w * 0.5f - radius * (1 - disc_exposure)),
            card.y + card.size * 0.51f, radius};
}
struct Layout {
    float width, height;
    library_view::View mode = library_view::View::coverflow;
    bool cd = false;
    float itemAspect() const {
        return mode == library_view::View::coverflow && cd ? 1.12f : aspect;
    }
    float coverHeight() const { return std::max(1.0f, std::min({height * 0.53f, width * 0.29f, 570.0f})); }
    float left() const { return std::max(88.0f, width * 0.065f); }
    float top() const { return std::max(210.0f, (height - coverHeight()) * 0.5f + 20); }
    float step() const { return coverHeight() * compact * itemAspect() + gap; }
    bool shelf() const { return mode == library_view::View::bookshelf; }
    // A card's resting rectangle; Półka: the bounds of its cover in perspective (aspect: width / height).
    void target(size_t index, size_t selected, float& x, float& y, float& size, float& item_aspect) const {
        const auto offset = static_cast<float>(static_cast<ptrdiff_t>(index) - static_cast<ptrdiff_t>(selected));
        if (shelf()) {
            const auto r = shelf_layout::Layout{width, height}.bounds(offset);
            x = r.left; y = r.top; size = std::max(1.0f, r.bottom - r.top); item_aspect = (r.right - r.left) / size;
            return;
        }
        item_aspect = itemAspect();
        size = coverHeight() * (index == selected ? 1.0f : compact);
        x = index == selected ? left() : index < selected ? left() - coverHeight() - 70 + offset * step() :
            left() + coverHeight() * itemAspect() * (1 + disc_radius * disc_exposure) +
                gap + (offset - 1) * step();
        y = top() + (coverHeight() - size) * 0.5f;
    }
    void target(size_t index, size_t selected, float& x, float& y, float& size) const {
        float item_aspect = 0;
        target(index, selected, x, y, size, item_aspect);
    }
    std::pair<size_t, size_t> visibleRange(size_t count, size_t selected) const {
        if (shelf()) return shelf_layout::Layout{width, height}.visibleRange(count, selected);
        const auto next = static_cast<size_t>(std::ceil(width / step())) + 2;
        return {selected > 2 ? selected - 2 : 0, std::min(count, selected + next)};
    }
    // A gliding case reflects a fixed lamp: while a poster travels to its place, the light
    // slides across it the other way, like tilting a steelbook in hand. In poster widths,
    // 0 at rest; bounded so a long jump only sweeps the light in from beyond the edge.
    float lightSweep(const GameCard& card, size_t index, size_t selected) const {
        float x = 0, y = 0, size = 0;
        target(index, selected, x, y, size);
        return std::clamp((x - card.x) / std::max(1.0f, card.width()) * 0.8f, -1.4f, 1.4f);
    }
};
}
