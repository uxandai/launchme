#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "tour_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>

// Home layout miniatures: Settings → Interfejs and the last step of the walkthrough.
// Each scene follows its real layout (carousel.hpp, xmb_layout.hpp, flix_layout.hpp, metro_layout.hpp,
// psfive_layout.hpp): the focus
// rests where the real one rests and the content moves under it, so no highlighted tile drifts
// on its own. Scene space is 240 x 96. Horizontal movement first, then a row/category change,
// then a return to the resting composition at 2 s.
namespace {
constexpr float scene_width = 240, scene_height = 96;
constexpr std::array<float, 5> name_widths{62, 46, 70, 54, 66};

float transition(float time, float start, float end) {
    const float t = std::clamp((time - start) / (end - start), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
using render_detail::smoothFade;
D2D1_COLOR_F mix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return D2D1::ColorF(std::lerp(a.r, b.r, t), std::lerp(a.g, b.g, t), std::lerp(a.b, b.b, t), std::lerp(a.a, b.a, t));
}
D2D1_COLOR_F faded(D2D1_COLOR_F color, float alpha) { color.a *= std::clamp(alpha, 0.0f, 1.0f); return color; }
// Position along a row with a wider focused slot: offset 0 is the slot, fractional offsets glide
// between neighbours. `after` is the distance from the slot to the next item, `pitch` the step
// beyond it and `before` the step of the items that left the slot.
float along(float offset, float slot, float after, float pitch, float before) {
    if (offset >= 1) return slot + after + (offset - 1) * pitch;
    if (offset >= 0) return slot + offset * after;
    return slot + offset * before;
}
float closeness(float offset) { return std::max(0.0f, 1.0f - std::abs(offset)); }
}

void Renderer::drawLayoutScene(int option, D2D1_RECT_F picture, float time, float emphasis) {
    const float scale = std::min((picture.right - picture.left) / scene_width, (picture.bottom - picture.top) / scene_height);
    if (scale <= 0) return;
    const float left = (picture.left + picture.right - scene_width * scale) * 0.5f;
    const float top = (picture.top + picture.bottom - scene_height * scale) * 0.5f;
    const float horizontal = transition(time, 0.15f, 0.85f) - transition(time, 1.6f, 2.0f);
    const float vertical = transition(time, 0.85f, 1.5f) - transition(time, 1.6f, 2.0f);
    // Metro's light theme (Jasny motyw): its miniature on a white page, the tiles darker than it.
    const bool light_page = option == 3 && metro_light_;
    const auto background = light_page ? D2D1::ColorF(0.93f, 0.935f, 0.94f) : D2D1::ColorF(0.035f, 0.045f, 0.055f);
    const auto white = [&](float amount) { return mix(background, light_page ? D2D1::ColorF(0, 0, 0) : D2D1::ColorF(1, 1, 1), amount); };
    const auto muted = white(0.16f), dim = white(0.08f), label = white(0.34f), light = white(0.44f);
    auto ink = uiAccent();
    ink.r *= 0.72f; ink.g *= 0.72f; ink.b *= 0.72f;
    const auto focus_ink = mix(muted, ink, 0.45f + 0.55f * emphasis);
    const auto ring = uiAccent(0.5f + 0.4f * emphasis);
    const auto shape = [&](float x, float y, float w, float h, float radius) {
        return D2D1::RoundedRect(D2D1::RectF(left + x * scale, top + y * scale, left + (x + w) * scale, top + (y + h) * scale),
                                 radius * scale, radius * scale);
    };
    const auto rect = [&](float x, float y, float w, float h, D2D1_COLOR_F color, float radius = 2.0f) {
        if (color.a < 0.004f || w <= 0 || h <= 0) return;
        brush(color);
        target_->FillRoundedRectangle(shape(x, y, w, h, radius), solid_.Get());
    };
    const auto frame = [&](float x, float y, float w, float h, D2D1_COLOR_F color, float radius) {
        if (color.a < 0.004f) return;
        brush(color);
        target_->DrawRoundedRectangle(shape(x, y, w, h, radius), solid_.Get(), std::max(1.0f, 0.9f * scale));
    };
    const auto clip = [&](float from, float to) {
        target_->PushAxisAlignedClip(D2D1::RectF(left, top + from * scale, left + scene_width * scale, top + to * scale),
                                     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    };
    clip(0, scene_height);
    rect(0, 0, scene_width, scene_height, background, 4);
    if (option == 0) {
        // Shelves: the focused card is the large one in the slot on the left. Moving right, the
        // row slides under the slot and the next card grows into it. A category change replaces
        // the row: the old cards go, the new ones rise in one after another (rebuildCards).
        constexpr float inset = 10, big = 42, compact = 26, gap = 6, bottom = 86;
        const auto shelf = [&](float focus, bool next) {
            for (int card = 0; card < 9; ++card) {
                const float offset = static_cast<float>(card) - focus;
                const float size = std::lerp(big, compact, std::min(1.0f, std::abs(offset)));
                const float x = along(offset, inset, big + gap, compact + gap, big + gap);
                if (x > scene_width || x + size < 0) continue;
                float alpha = 1, rise = 0;
                if (next) {
                    const float shown = smoothFade((vertical - 0.15f - card * 0.07f) / 0.45f);
                    alpha = shown; rise = (1 - shown) * 8;
                } else alpha = 1 - smoothFade(vertical / 0.35f);
                if (alpha < 0.004f) continue;
                const float focused = closeness(offset);
                const auto base = mix(muted, dim, next ? 0.3f + 0.2f * (card % 2) : 0.15f * (card % 3));
                const float y = bottom - size + rise;
                rect(x, y, size, size, faded(mix(base, focus_ink, focused), alpha), 4 + 3 * focused);
                rect(x + size * 0.14f, y + size * 0.62f, size * 0.42f, std::max(2.0f, size * 0.09f),
                     faded(mix(dim, white(0.3f), focused), alpha), 1);
            }
        };
        // The focused game's title above the row changes with the focus.
        const float first_title = (1 - smoothFade(vertical / 0.35f)) * smoothFade(std::abs(horizontal - 0.5f) * 2);
        rect(14, 18, name_widths[horizontal < 0.5f ? 0 : 1], 7, faded(label, first_title), 2);
        rect(14, 18, name_widths[3], 7, faded(label, smoothFade((vertical - 0.5f) / 0.3f)), 2);
        if (vertical < 0.999f) shelf(horizontal, false);
        if (vertical > 0.001f) shelf(0, true);
    } else if (option == 1) {
        // XMB: the focused column is pinned at the anchor and the top row slides under it; its
        // games are listed below, the focused one pinned under the tile. The next column brings
        // its own list (the games rise in); moving down slides the list up under the pinned slot.
        constexpr float anchor = 30, center = 17, tile = 16, focus_tile = 22, gap = 5;
        constexpr float item = 13, focus_item = 18, item_gap = 4, list_top = 36;
        constexpr float column = anchor + focus_tile * 0.5f, labels = column + focus_item * 0.5f + 6;
        const float focus = 1 + horizontal;
        for (int index = 0; index < 11; ++index) {
            const float offset = static_cast<float>(index) - focus;
            const float size = std::lerp(focus_tile, tile, std::min(1.0f, std::abs(offset)));
            const float x = along(offset, anchor, focus_tile + gap, tile + gap, tile + gap);
            if (x > scene_width || x + size < 0) continue;
            const float focused = closeness(offset);
            const float y = center - size * 0.5f;
            rect(x, y, size, size, mix(index < 2 ? muted : dim, light, focused), 3 + focused);
            if (index < 2) continue;
            // Collections and consoles: a 2x2 preview of their games inside the tile.
            const float cell = size * 0.3f, pad = size * 0.14f, between = size - 2 * pad - 2 * cell;
            for (int slot = 0; slot < 4; ++slot)
                rect(x + pad + (slot % 2) * (cell + between), y + pad + (slot / 2) * (cell + between), cell, cell,
                     mix(muted, white(0.3f), focused * 0.6f), 1);
        }
        rect(column - 9, center + focus_tile * 0.5f + 3, 18, 2.5f, label, 1);
        clip(31, scene_height);
        const auto games = [&](float focus_row, bool next) {
            for (int game = 0; game < 6; ++game) {
                const float offset = static_cast<float>(game) - focus_row;
                const float size = std::lerp(focus_item, item, std::min(1.0f, std::abs(offset)));
                float y = along(offset, list_top, focus_item + item_gap, item + item_gap, item + item_gap + 8);
                float alpha = 1;
                if (next) {
                    const float shown = smoothFade((horizontal - 0.3f - game * 0.06f) / 0.4f);
                    alpha = shown; y += (1 - shown) * 7;
                } else alpha = 1 - smoothFade(horizontal * 2.5f);
                if (offset < 0) alpha *= std::max(0.0f, 1 + offset); // Earlier games leave above the tiles.
                if (alpha < 0.004f || y > scene_height) continue;
                const float focused = closeness(offset);
                rect(column - size * 0.5f, y, size, size, faded(mix(next ? mix(muted, dim, 0.3f) : muted, focus_ink, focused), alpha), 3);
                const float middle = y + size * 0.5f;
                rect(labels, middle - 1.5f - 2.5f * focused, name_widths[(game + (next ? 2 : 0)) % name_widths.size()] * 0.8f, 3 + focused,
                     faded(mix(muted, label, focused), alpha), 1);
                rect(labels, middle + 3, 30, 2, faded(dim, alpha * focused), 1);
            }
        };
        games(0, false);
        games(vertical, true);
        target_->PopAxisAlignedClip();
    } else if (option == 3) {
        // Metro: tabs over a start page (a large recent game among small tiles and accent
        // shortcuts) with the next page peeking in. The focus moves to a tile on the right,
        // then the pages slide left and the next page's first poster takes it.
        constexpr float edge = 14, top_row = 24, cell_w = 28, cell_h = 20, gap = 3, page_gap = 14;
        constexpr float next_left = edge + 4 * (cell_w + gap) + page_gap;
        const float slide = vertical * (next_left - edge);
        const auto cell = [&](int column, int row, int columns, int rows, D2D1_COLOR_F color, float focused) {
            const float x = edge + column * (cell_w + gap) - slide, y = top_row + row * (cell_h + gap);
            const float w = columns * cell_w + (columns - 1) * gap, h = rows * cell_h + (rows - 1) * gap;
            rect(x, y, w, h, mix(color, focus_ink, focused), 1.5f);
            frame(x, y, w, h, faded(ring, focused), 1.5f);
        };
        const auto system = mix(dim, uiAccent(), 0.35f);
        // Shortcut tiles in the same neutral tones as the rest of the miniatures (no brand colours).
        const auto shortcut = mix(muted, dim, 0.45f);
        const float on_page = 1 - vertical;
        // Tabs: the active one bright, sliding left with the pages.
        rect(edge - vertical * 34, 9, 20, 5, mix(muted, label, on_page), 1.5f);
        rect(edge + 26 - vertical * 34, 9, 24, 5, mix(muted, label, vertical), 1.5f);
        rect(edge + 56 - vertical * 34, 9, 18, 5, muted, 1.5f);
        // As metro_layout: two recent games, Biblioteka and Ustawienia halves, the large game, the
        // store and console shortcuts, and the tall installation poster.
        cell(0, 0, 1, 1, muted, 0);
        cell(0, 1, 1, 1, dim, 0);
        {
            const float x = edge - slide, y = top_row + 2 * (cell_h + gap), w = (cell_w - gap) * 0.5f;
            rect(x, y, w, cell_h, shortcut, 1.5f);
            rect(x + w + gap, y, w, cell_h, shortcut, 1.5f);
        }
        cell(1, 0, 2, 2, mix(muted, white(0.22f), 0.5f), on_page * (1 - horizontal));
        cell(1, 2, 1, 1, shortcut, 0);
        cell(2, 2, 1, 1, shortcut, 0);
        cell(3, 0, 1, 3, dim, on_page * horizontal);
        // The next page: two rows of posters after its "Wszystkie" tile.
        constexpr float poster_h = (3 * cell_h + 2 * gap - gap) * 0.5f, poster_w = poster_h * 2 / 3;
        for (int poster = 0; poster < 12; ++poster) {
            const float x = next_left + (poster / 2) * (poster_w + gap) - slide, y = top_row + (poster % 2) * (poster_h + gap);
            if (x > scene_width) break;
            const float focused = poster == 0 ? vertical : 0.0f;
            rect(x, y, poster_w, poster_h, mix(poster ? mix(muted, dim, 0.2f * (poster % 3)) : system, focus_ink, focused), 1.5f);
            frame(x, y, poster_w, poster_h, faded(ring, focused), 1.5f);
        }
    } else if (option == 5) {
        // snitch: equal covers, selected title above, compact circular shortcuts below.
        rect(0, 0, scene_width, scene_height, white(0.14f), 4);
        rect(12, 8, 9, 9, light, 4.5f);
        rect(196, 10, 30, 3, label, 1);
        rect(20, 23, name_widths[horizontal < 0.5f ? 0 : 1], 4, label, 1);
        clip(30, 72);
        for (int card = 0; card < 8; ++card) {
            const float offset = card - horizontal;
            const float x = 20 + offset * 46;
            const float focus = closeness(offset) * (1 - vertical);
            rect(x, 32, 42, 38, mix(muted, focus_ink, focus), 4);
            frame(x, 32, 42, 38, faded(ring, focus), 4);
        }
        target_->PopAxisAlignedClip();
        for (int icon = 0; icon < 8; ++icon) {
            const float x = 62 + icon * 16.0f;
            rect(x, 80, 10, 10, light, 5);
            rect(x + 3, 83, 4, 4, white(0.85f), 1);
            if (!icon) frame(x - 1, 79, 12, 12, faded(ring, vertical), 6);
        }
    } else if (option == 4) {
        // PSfive: the focused game's background fills the scene. The row of tiles at the top slides
        // under the left-aligned focus (the library tile last), its badge
        // and name beside it; then the focus goes down to "Graj" low on the left, the logo in the corner.
        constexpr float tile = 15.6f, focus_tile = 24.7f, gap = 2.5f, row_top = 13;
        constexpr float anchor = 8, buttons = scene_height - 15;
        const float settled = smoothFade(std::abs(horizontal - 0.5f) * 2); // Dips while the focus changes.
        const float down = vertical;
        rect(0, 0, scene_width, scene_height, white(0.05f + 0.05f * settled), 4);
        rect(112, 22, 116, 58, faded(white(0.15f), settled), 12); // The background's art.
        const float focus = 2 + horizontal;
        target_->PushAxisAlignedClip(D2D1::RectF(left + (anchor - 1.5f) * scale, top, left + scene_width * scale, top + scene_height * scale),
                                     D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (int index = 0; index < 16; ++index) {
            const float offset = static_cast<float>(index) - focus;
            const float size = std::lerp(focus_tile, tile, std::min(1.0f, std::abs(offset)));
            const float x = along(offset, anchor, focus_tile + gap, tile + gap, tile + gap);
            if (x > scene_width || x + size < anchor) continue;
            const float focused = closeness(offset) * (1 - down);
            rect(x, row_top, size, size, mix(index == 15 ? dim : mix(muted, dim, 0.15f * (index % 3)), focus_ink, focused), 2.5f);
            frame(x, row_top, size, size, faded(ring, focused), 2.5f);
        }
        target_->PopAxisAlignedClip();
        const float label_y = row_top + tile + (focus_tile - tile) * 0.5f;
        rect(anchor + focus_tile + 4, label_y - 2.5f, 9, 5, faded(white(0.85f), settled), 1.5f);
        rect(anchor + focus_tile + 16, label_y - 1.5f, name_widths[horizontal < 0.5f ? 0 : 1] * 0.7f, 3, faded(label, settled), 1);
        rect(anchor, buttons, 36, 9, mix(muted, white(0.85f), down), 4.5f);
        rect(anchor + 39, buttons, 9, 9, muted, 4.5f);
        rect(anchor, buttons - 20, 42, 13, faded(light, settled), 2); // The logo over the buttons.
        // The top bar: three categories, More, Widgets, the dotted Menu icon and the clock.
        rect(8, 5, 15, 3.5f, label, 1.5f);
        rect(28, 5, 15, 3.5f, muted, 1.5f);
        rect(48, 5, 15, 3.5f, muted, 1.5f);
        rect(68, 5, 12, 3.5f, muted, 1.5f);
        rect(scene_width - 48, 4, 2, 5, label, 0.5f);
        for (int row = 0; row < 2; ++row) rect(scene_width - 44, 4 + row * 3.5f, 2, 2, label, 0.5f);
        frame(scene_width - 39, 4, 6, 6, label, 2);
        rect(scene_width - 36.8f, 6.2f, 1.6f, 1.6f, label, 0.8f);
        rect(scene_width - 30, 5, 20, 3, label, 1.5f);
    } else {
        // Flix: the first row (landscape capsules) rests near the bottom under the focused game's
        // logo. The focus moves along the row while the tiles stay put (a row scrolls only at its
        // edge). Moving down lifts the page: the next row of posters goes to the top.
        constexpr float edge = 10, header = 7, gap = 4, row_gap = 5;
        constexpr float capsule_h = 22, capsule_w = 46, poster_h = 30, poster_w = 20;
        constexpr float rest = scene_height - 8 - capsule_h - header, raised = 6;
        const auto offset = [](int row) {
            return row ? header + capsule_h + row_gap + static_cast<float>(row - 1) * (header + poster_h + row_gap) : 0.0f;
        };
        const float lift = vertical;
        const float name = name_widths[horizontal < 0.5f ? 0 : 1];
        // The focused game's background and logo above the resting row, gone once the page rises.
        rect(0, 0, scene_width, rest, faded(white(0.035f), 1 - lift), 0);
        rect(edge, rest - 14, name * 0.8f, 8, faded(label, (1 - smoothFade(lift * 3)) * smoothFade(std::abs(horizontal - 0.5f) * 2)), 2);
        for (int row = 0; row < 4; ++row) {
            const float header_top = std::lerp(rest + offset(row) + (row ? 6.0f : 0.0f), raised + offset(row) - offset(1), lift);
            if (header_top > scene_height) continue;
            const bool landscape = row == 0;
            const float h = landscape ? capsule_h : poster_h, w = landscape ? capsule_w : poster_w;
            const float tiles = header_top + header;
            const float alpha = smoothFade((tiles + h - 2) / 16); // Rows above the focused one fade under the top edge.
            if (alpha < 0.01f) continue;
            const float focus_row = row == 0 ? 1 - lift : row == 1 ? lift : 0.0f;
            const float title = 22.0f + row * 4.0f;
            rect(edge, header_top + 2, title, 3, faded(mix(muted, label, focus_row), alpha), 1);
            if (focus_row > 0.01f) {
                rect(edge + title + 4, header_top + 2.5f, 2, 2, faded(uiAccent(), alpha * focus_row), 1);
                rect(edge + title + 8, header_top + 2, row == 0 ? name * 0.4f : 20.0f, 3, faded(label, alpha * focus_row), 1);
            }
            for (int column = 0; column < 10; ++column) {
                const float x = edge + column * (w + gap);
                if (x > scene_width) break;
                const float focused = row == 0 ? closeness(column - horizontal) * (1 - lift) : row == 1 && column == 0 ? lift : 0.0f;
                rect(x, tiles, w, h, faded(mix(mix(muted, dim, 0.12f * ((column + row) % 3)), focus_ink, focused), alpha), 3);
                frame(x, tiles, w, h, faded(ring, alpha * focused), 3);
            }
        }
    }
    target_->PopAxisAlignedClip();
}

void Renderer::drawLayoutTile(int option, D2D1_RECT_F box, bool active, bool focus, float time, const std::wstring& description) {
    using settings_ui::home_layouts;
    const bool large = !description.empty();
    const float radius = large ? 14.0f : 10.0f;
    brush(D2D1::ColorF(1, 1, 1, focus ? 0.065f : active ? 0.045f : 0.02f));
    target_->FillRoundedRectangle(D2D1::RoundedRect(box, radius, radius), solid_.Get());
    brush(focus || active ? uiAccent(focus ? 0.9f : 0.5f) : D2D1::ColorF(1, 1, 1, 0.14f));
    target_->DrawRoundedRectangle(D2D1::RoundedRect(box, radius, radius), solid_.Get(), focus ? 1.8f : 1.0f);
    const float side = large ? 16.0f : 12.0f;
    const float caption = large ? tour::Layout::tile_caption : 38.0f;
    drawLayoutScene(option, D2D1::RectF(box.left + side, box.top + (large ? 16.0f : 10.0f), box.right - side, box.bottom - caption),
                    time, focus || active ? 1.0f : 0.0f);
    float check_y = box.bottom - 18;
    if (large) {
        const float caps = box.bottom - caption + 22;
        textGroup(tr(home_layouts[option]), description, box.left + side + 2, caps, box.right - box.left - 2 * side - 44,
                  category_font_.Get(), D2D1::ColorF(0.94f, 0.94f, 0.94f), settings_body_font_.Get(), D2D1::ColorF(0.58f, 0.58f, 0.58f), 2, true);
        check_y = caps + 8;
    } else textLine(tr(home_layouts[option]), box.left + 14, check_y, box.right - box.left - 54,
                    settings_body_font_.Get(), D2D1::ColorF(0.88f, 0.88f, 0.88f));
    if (active) {
        const float x = box.right - (large ? 30.0f : 24.0f);
        line(x - 6, check_y, x - 1, check_y + 5, uiAccent(), 2);
        line(x - 1, check_y + 5, x + 8, check_y - 5, uiAccent(), 2);
    }
}

void Renderer::drawSettingsLayouts(const settings_ui::Layout& layout, int row, float scroll, bool focused) {
    const auto& item = (*context_->settings_rows)[row];
    for (int option = 0; option < settings_ui::home_layout_count; ++option) {
        const auto tile = layout.layoutOption(row, scroll, option);
        const bool active = item.choice == option;
        const bool preview = context_->settings_preview_option == option;
        const bool focus = preview || (focused && active && context_->settings_preview_option < 0);
        drawLayoutTile(option, D2D1::RectF(tile.left, tile.top, tile.right, tile.bottom), active, focus,
                       preview ? context_->settings_preview_seconds : 0, {});
    }
}
