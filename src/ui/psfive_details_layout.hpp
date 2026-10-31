#pragma once
#include "settings_view.hpp"

#include <algorithm>
#include <cstddef>

// PSfive → Szczegóły pod rzędem: a sheet that rises over the full-screen background when Down
// leaves "Graj" / "…". Sections: the information (description and facts), the screenshots and
// the similar games. 1600×900 canvas; `progress` 0..1 is the sheet's rise, `scroll` the sections'
// own offset (the focused one kept in view).
namespace psfive_details {
inline constexpr float left = 77, open_top = 92;
inline constexpr float info_top = 92, info_height = 330;          // Under the sheet's title.
inline constexpr float shots_title = info_top + info_height + 28;  // "Zrzuty ekranu".
inline constexpr float shot_width = 352, shot_height = 198, shot_gap = 18;
inline constexpr float similar_title = shots_title + 46 + shot_height + 44; // "Podobne gry".
inline constexpr float similar_size = 172, similar_gap = 18;
inline constexpr float content_height = similar_title + 46 + similar_size + 70;
inline float sheetTop(float height, float progress) { return height - (height - open_top) * std::clamp(progress, 0.0f, 1.0f); }
// The vertical offset that shows the focused section (0 information, 1 screenshots, 2 similar).
inline float scrollFor(int section, float height) {
    const float visible = height - open_top;
    const float wanted = section <= 0 ? 0.0f : section == 1 ? shots_title - 60 : similar_title - 60;
    return std::clamp(wanted, 0.0f, std::max(0.0f, content_height - visible));
}
// The first item shown in a strip with `count` items, the focused one kept in view.
inline size_t firstShown(size_t focus, size_t count, float width, float step) {
    const auto visible = static_cast<size_t>(std::max(1.0f, (width - 2 * left + 18) / step));
    if (count <= visible || focus < visible - 1) return 0;
    return std::min(focus + 2 - visible, count - visible);
}
inline settings_ui::Rect shot(size_t index, size_t first, float top) {
    const float x = left + (static_cast<float>(index) - static_cast<float>(first)) * (shot_width + shot_gap);
    return {x, top + shots_title + 46, x + shot_width, top + shots_title + 46 + shot_height};
}
inline settings_ui::Rect similar(size_t index, size_t first, float top) {
    const float x = left + (static_cast<float>(index) - static_cast<float>(first)) * (similar_size + similar_gap);
    return {x, top + similar_title + 46, x + similar_size, top + similar_title + 46 + similar_size};
}
}
