#pragma once
#include "settings_view.hpp"

#include <algorithm>
#include <cstddef>

// PSfive → Szczegóły pod rzędem: a sheet that rises over the full-screen background when Down
// leaves "Graj" / "…", laid out like Szczegóły: frosted cards (Opis, Informacje, Osiągnięcia),
// then a strip of screenshots and one of similar games. 1600×900 canvas; everything fits on the
// screen at once (no scrolling); `progress` 0..1 is the sheet's rise. y values are from its top.
namespace psfive_details {
inline constexpr float left = 77, open_top = 84;
inline constexpr float cards_top = 18, cards_height = 262, card_gap = 18;
inline constexpr float shots_label = 312, shots_top = 340;           // "Zrzuty ekranu" (its centre line), the strip.
inline constexpr float shot_width = 320, shot_height = 180, shot_gap = 18;
inline constexpr float similar_label = 558, similar_top = 586;       // "Podobne gry", the strip.
inline constexpr float similar_size = 150, similar_gap = 18;
inline float sheetTop(float height, float progress) { return height - (height - open_top) * std::clamp(progress, 0.0f, 1.0f); }
// The cards' row: Opis, Informacje and (when the game has achievements) Osiągnięcia.
struct Cards {
    settings_ui::Rect description, facts, achievements;
    Cards(float width, float top, bool with_achievements) {
        const float right = width - left, y = top + cards_top, bottom = y + cards_height;
        const float total = right - left - card_gap * (with_achievements ? 2 : 1);
        const float description_width = total * (with_achievements ? 0.46f : 0.6f);
        const float facts_width = with_achievements ? total * 0.31f : total - description_width;
        description = {left, y, left + description_width, bottom};
        facts = {description.right + card_gap, y, description.right + card_gap + facts_width, bottom};
        achievements = {facts.right + card_gap, y, right, bottom};
    }
};
// The first item shown in a strip with `count` items, the focused one kept in view.
inline size_t firstShown(size_t focus, size_t count, float width, float step) {
    const auto visible = static_cast<size_t>(std::max(1.0f, (width - 2 * left + 18) / step));
    if (count <= visible || focus < visible - 1) return 0;
    return std::min(focus + 2 - visible, count - visible);
}
// Where a strip scrolls to keep its focused item in view, in pixels (eased by tickPsfiveDetails, as tiles slide).
inline float scrollFor(size_t focus, size_t count, float width, float step) { return static_cast<float>(firstShown(focus, count, width, step)) * step; }
inline settings_ui::Rect shot(size_t index, float scroll, float top) {
    const float x = left + static_cast<float>(index) * (shot_width + shot_gap) - scroll;
    return {x, top + shots_top, x + shot_width, top + shots_top + shot_height};
}
inline settings_ui::Rect similar(size_t index, float scroll, float top) {
    const float x = left + static_cast<float>(index) * (similar_size + similar_gap) - scroll;
    return {x, top + similar_top, x + similar_size, top + similar_top + similar_size};
}
// An item sliding out past the left margin fades over one step.
inline float edgeFade(float x, float step) { return std::clamp((x - (left - step)) / step, 0.0f, 1.0f); }
}
