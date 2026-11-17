#pragma once
#include "settings_view.hpp"

#include <algorithm>
#include <cstddef>

// PSfive → Szczegóły pod rzędem: a sheet that peeks from the bottom while "Graj" / "…" have the
// focus (the hub) and rises over the full-screen background when Down leaves them, as the PS5's
// game hub: a row of frosted activity cards (Osiągnięcia, Czas gry, Opis, Informacje), then a strip
// of screenshots and one of similar games, scrolled a section at a time. 1600×900 canvas; `hub`
// 0..1 is the peek, `progress` 0..1 the sheet's rise. y values are from its top. `left` is the
// margin on both sides (the tabs' margin).
namespace psfive_details {
inline constexpr float open_top = 96;
// Sections, as on the PS5's game hub: a row of activity cards, the screenshots, the similar games.
// Down / Up move by a section and the page scrolls to put it under the corner (sectionScroll).
inline constexpr float cards_top = 18, cards_height = 230, card_gap = 18;
inline constexpr size_t card_slots = 4;                              // Cards share the width of four.
inline constexpr float shots_label = 292, shots_top = 320;           // "Zrzuty ekranu" (its centre line), the strip.
inline constexpr float shot_width = 400, shot_height = 225, shot_gap = 18;
inline constexpr float similar_label = 591, similar_top = 619;       // "Podobne gry", the strip.
inline constexpr float similar_size = 170, similar_gap = 18;
inline constexpr float section_margin = 34;                          // Over a section's heading once scrolled to it.
inline constexpr float peek = 196; // psfive_layout::hub_peek
inline float sheetTop(float height, float hub, float progress) {
    const float peeked = height - peek * std::clamp(hub, 0.0f, 1.0f);
    return peeked - (peeked - open_top) * std::clamp(progress, 0.0f, 1.0f);
}
// The activity cards (Osiągnięcia, Czas gry, Opis, Informacje: those the game has), one width each.
enum class Card { achievements, playtime, description, facts };
inline float cardWidth(float width, float left) {
    return (width - 2 * left - card_gap * static_cast<float>(card_slots - 1)) / static_cast<float>(card_slots);
}
inline settings_ui::Rect card(size_t index, float width, float top, float left) {
    const float w = cardWidth(width, left), x = left + static_cast<float>(index) * (w + card_gap);
    return {x, top + cards_top, x + w, top + cards_top + cards_height};
}
// Without screenshots the similar games take their place: `up` 0..1 moves them there (eased by the launcher).
inline float similarShift(float up) { return (similar_top - shots_top) * std::clamp(up, 0.0f, 1.0f); }
// The page's scroll that brings a section (0 cards, 1 screenshots, 2 similar games) under the corner.
inline float sectionScroll(int section, float up) {
    if (section == 1) return shots_label - section_margin;
    if (section == 2) return similar_label - similarShift(up) - section_margin;
    return 0;
}
// Content scrolled up past the sheet's top fades out under the corner (no clip: the glass copies the target).
inline float topFade(float y) { return std::clamp((y - (open_top - 70)) / 70, 0.0f, 1.0f); }
// The first item shown in a strip with `count` items, the focused one kept in view.
inline size_t firstShown(size_t focus, size_t count, float width, float step, float left) {
    const auto visible = static_cast<size_t>(std::max(1.0f, (width - 2 * left + 18) / step));
    if (count <= visible || focus < visible - 1) return 0;
    return std::min(focus + 2 - visible, count - visible);
}
// Where a strip scrolls to keep its focused item in view, in pixels (eased by tickPsfiveDetails, as tiles slide).
inline float scrollFor(size_t focus, size_t count, float width, float step, float left) { return static_cast<float>(firstShown(focus, count, width, step, left)) * step; }
inline settings_ui::Rect shot(size_t index, float scroll, float top, float left) {
    const float x = left + static_cast<float>(index) * (shot_width + shot_gap) - scroll;
    return {x, top + shots_top, x + shot_width, top + shots_top + shot_height};
}
inline settings_ui::Rect similar(size_t index, float scroll, float top, float left, float up = 0) {
    const float x = left + static_cast<float>(index) * (similar_size + similar_gap) - scroll;
    const float y = top + similar_top - similarShift(up);
    return {x, y, x + similar_size, y + similar_size};
}
// An item sliding out past the left margin fades over one step.
inline float edgeFade(float x, float step, float left) { return std::clamp((x - (left - step)) / step, 0.0f, 1.0f); }
}
