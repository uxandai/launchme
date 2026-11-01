#pragma once

#include "settings_view.hpp"

#include <algorithm>

// First-run walkthrough (src/app/tour.cpp, src/ui/tour_renderer.cpp). A card with a stepper at
// the top of the screen; each step opens the real panel it describes and dims everything else.
// The last step offers the home layouts as large animated tiles. Drawing and hit testing
// share this geometry.
namespace tour {
enum class Step { welcome, categories, drawer, quick_bar, menu, suspend, layout };
inline constexpr int step_count = static_cast<int>(Step::layout) + 1;
// Card buttons, left to right: Back (a frame with a dot, from the second step), Skip, Next/Done.
enum Button { back, skip, next };
inline constexpr int button_count = 3;

struct Layout {
    float width, height;
    // Card at the bottom edge instead of the top: XMB keeps its top row (and the game menu
    // beside it) up there.
    bool bottom = false;
    // The layout tiles of the last step are a slider of tile_visible at a time: its leftmost slot
    // (firstFor the chosen layout; fractional while the renderer glides it).
    float first = 0;
    static constexpr float card_width = 700, edge = 24, padding = 26, bottom_padding = 22;
    static constexpr float stepper_top = 22, stepper_height = 4, stepper_gap = 6;
    static constexpr float row_top = 44, row_height = settings_ui::Layout::button_height;
    static constexpr float body_top = row_top + row_height + 14;
    static constexpr int body_lines = 3, bottom_body_lines = 2;
    static constexpr float body_line = 22; // Reserved per line of a bottom card; the body font is 16 px.
    static constexpr float next_width = 150, skip_width = 120, button_gap = 10;
    // Large layout tiles of the last step: picture, then the name and one or two lines under it.
    static constexpr float tile_gap = 28, tile_caption = 92, tile_aspect = 2.5f, tile_visible = 4.5f;
    static float firstFor(int choice) { return settings_ui::layoutFirst(choice, tile_visible); }

    float cardLeft() const { return (width - std::min(card_width, width - 2 * edge)) * 0.5f; }
    float cardRight() const { return width - cardLeft(); }
    // A bottom card reserves its largest body so the buttons stay where hit testing expects them.
    float cardTop() const { return bottom ? height - edge - (body_top + bottom_body_lines * body_line + bottom_padding) : edge; }
    // `body` is the measured height of the step text (0 without one).
    settings_ui::Rect card(float body) const {
        const float top = cardTop();
        return {cardLeft(), top, cardRight(), top + (body > 0 ? body_top + body + bottom_padding : row_top + row_height + bottom_padding)};
    }
    settings_ui::Rect segment(int index, int count) const {
        const float left = cardLeft() + padding, right = cardRight() - padding;
        const float w = (right - left - (count - 1) * stepper_gap) / static_cast<float>(std::max(1, count));
        const float x = left + index * (w + stepper_gap), y = cardTop() + stepper_top;
        return {x, y, x + w, y + stepper_height};
    }
    float rowMiddle() const { return cardTop() + row_top + row_height * 0.5f; }
    settings_ui::Rect button(int index) const {
        const float right = cardRight() - padding, top = cardTop() + row_top;
        const float next_left = right - next_width, skip_left = next_left - button_gap - skip_width;
        if (index == next) return {next_left, top, right, top + row_height};
        if (index == skip) return {skip_left, top, skip_left + skip_width, top + row_height};
        return {skip_left - button_gap - row_height, top, skip_left - button_gap, top + row_height};
    }
    int hitButton(float x, float y, bool with_back) const {
        for (int i = with_back ? back : skip; i < button_count; ++i) if (button(i).contains(x, y)) return i;
        return -1;
    }
    float tilesWidth() const { return std::min(1380.0f, width - 160); }
    settings_ui::Rect tile(int option) const {
        const float total = tilesWidth();
        const float pitch = (total + tile_gap) / tile_visible, w = pitch - tile_gap;
        const float h = 16 + (w - 32) / tile_aspect + tile_caption;
        // Centred in the space under the tallest card.
        const float below = edge + body_top + body_lines * body_line + bottom_padding;
        const float top = below + std::max(40.0f, (height - below - h) * 0.5f);
        const float left = (width - total) * 0.5f + (static_cast<float>(option) - first) * pitch;
        return {left, top, left + w, top + h};
    }
    // Where the slider shows its tiles: drawing clips to it, a pointer outside it hits none.
    settings_ui::Rect tileBand() const {
        const auto t = tile(0);
        return {(width - tilesWidth()) * 0.5f - 10, t.top - 10, (width + tilesWidth()) * 0.5f + 10, t.bottom + 10};
    }
    int hitTile(float x, float y) const {
        if (!tileBand().contains(x, y)) return -1;
        for (int option = 0; option < settings_ui::home_layout_count; ++option) if (tile(option).contains(x, y)) return option;
        return -1;
    }
};
} // namespace tour
