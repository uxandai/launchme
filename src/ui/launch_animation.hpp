#pragma once
#include <algorithm>
#include <cmath>

// Starting a game: its background, a touch dimmed and never moving, with its logo (its title without one)
// resting in the middle and, beneath it, a thin ring with a white dot bouncing calmly off its edge; the game
// opens at once underneath. The picture stays until the game is there: once the game's window takes the
// foreground it rests, still, behind the game (no repaints), and the launcher fades it out when it is seen
// again or the game ends. Left in front, it lasts at least `seconds`, longer while the game still starts (up
// to `longest`). Back (Esc, B, the mouse's back button) hides it after `dismiss_after`; no hint says so.
namespace launch_animation {
inline constexpr float seconds = 10.0f;      // The shortest run in front of the user.
inline constexpr float longest = 120.0f;     // A game that never shows (the session's start-up limit).
inline constexpr float bounce_seconds = 1.1f; // One flight of the dot, from one touch of the ring to the next.
inline constexpr float fade_in = 0.45f, fade_out = 0.7f;
inline constexpr float dismiss_after = 0.6f; // The press that started the game is not a Back.
inline constexpr float dim = 0.40f;          // Black over the background, before the bottom shade.
inline float ease(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
// The whole picture: fades in over the screen; `ending` (seconds since its fade-out began, < 0
// while it stays) fades it out (the screen returns beneath).
inline float opacity(float age, float ending = -1.0f) {
    return ease(age / fade_in) * (ending < 0 ? 1.0f : ease((fade_out - ending) / fade_out));
}
// The dot's place in the ring, as a fraction of the radius it travels (|x, y| <= 1): it flies along straight
// chords at one speed and turns at the ring (every bounce advances the touching point by the same angle, so
// the path draws a slowly turning star and never repeats exactly).
struct Dot { float x, y; };
inline Dot dot(float age) {
    constexpr float turn = 2.399963f; // The golden angle, in radians.
    const float flights = std::max(0.0f, age) / bounce_seconds;
    const float whole = std::floor(flights), t = flights - whole;
    const float from = whole * turn, to = (whole + 1) * turn;
    return {std::cos(from) * (1 - t) + std::cos(to) * t, std::sin(from) * (1 - t) + std::sin(to) * t};
}
// The logo (or title) a beat after the background, settling from slightly smaller.
inline float logo(float age) { return ease((age - 0.2f) / 0.6f); }
inline float logoScale(float age) { return 0.94f + 0.06f * logo(age); }
}
