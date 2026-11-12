#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// An achievement just unlocked in a game, over it: a card that slides in at the bottom right of the
// game's screen for a few seconds (src/app/achievement_toasts.cpp, achievement_toast_renderer.cpp).
namespace achievement_toast {
// The card in DIPs; the window is the card plus its margin, scaled to the screen (Layout::scale).
inline constexpr float card_width = 400, card_height = 92, margin = 28;
// Slide in, rest, slide out (milliseconds).
inline constexpr uint64_t slide_in_ms = 320, shown_ms = 5000, slide_out_ms = 260;

struct View {
    uint64_t id = 0;                                   // A new toast (the icon is decoded again).
    std::wstring heading;                              // "Osiągnięcie zdobyte".
    std::wstring name, description;
    std::shared_ptr<const std::vector<uint8_t>> icon;  // Encoded picture; empty: a trophy is drawn.
    float scale = 1;                                   // DIPs to pixels for this screen.
    float slide = 1;                                   // 0 off screen to the right, 1 in place.
    // The screen under the window, blurred and shrunk (BGRA, opaque): drawn stretched over
    // `backdrop_rect` (pixels, from the window's top left) inside the card, as frosted glass.
    std::shared_ptr<const std::vector<uint8_t>> backdrop;
    uint32_t backdrop_width = 0, backdrop_height = 0;
    float backdrop_left = 0, backdrop_top = 0, backdrop_right = 0, backdrop_bottom = 0;
};

// How far in the card is `elapsed` ms after it was shown (0 hidden, 1 in place); past the end: < 0.
inline float slideAt(uint64_t elapsed) {
    const auto ease = [](float t) { t = t < 0 ? 0 : t > 1 ? 1 : t; return 1 - (1 - t) * (1 - t) * (1 - t); };
    if (elapsed < slide_in_ms) return ease(static_cast<float>(elapsed) / slide_in_ms);
    if (elapsed < slide_in_ms + shown_ms) return 1;
    const uint64_t out = elapsed - slide_in_ms - shown_ms;
    if (out < slide_out_ms) return 1 - ease(static_cast<float>(out) / slide_out_ms);
    return -1;
}
} // namespace achievement_toast
