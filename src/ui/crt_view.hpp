#pragma once

// Tryb TV's CRT filter (Dostosuj wygląd → Inne → Filtr CRT): its settings as the shader takes them, and
// the screen's curvature as a pure function, so clicks on the bent picture land where they appear.
// The shader is src/ui/crt_filter.cpp (Timothy Lottes' public domain CRT shader, adapted).

#include "../i18n/i18n.hpp"

#include <algorithm>
#include <cmath>

namespace crt_view {

// Mask patterns (Lottes): none (scanlines only), a TV's compressed shadow mask, an aperture grille
// (Trinitron), a stretched VGA shadow mask (arcade monitors) and a VGA shadow mask.
enum class Mask { none, tv, grille, arcade, vga };
inline constexpr int mask_count = 5;
inline constexpr const wchar_t* mask_names[mask_count] = {trKey(L"Same linie"), trKey(L"Telewizor"), trKey(L"Trinitron"),
                                                                  trKey(L"Automat"), trKey(L"Monitor VGA")};

struct Settings {
    bool on = false;
    Mask mask = Mask::arcade;
    float warp_x = 0.25f, warp_y = 0.33f; // 0..1 of the strongest bend (Lottes' 0.125).
    float mask_level = 0.7f;              // 0..1: the mask's contrast (1: Lottes' maskDark 0.5, maskLight 1.5).
    float scanlines = 0.6f;               // 0..1: how dark the gaps between lines are.
    float glow = 0.3f;                    // 0..1: the bloom around bright lines (1: 0.5).
    float phosphor = 0.0f;                // 0..1: how long the picture glows on after it changes.
    bool operator==(const Settings&) const = default;
};

inline constexpr float max_warp = 0.125f;

// Where the picture drawn at window pixel (x, y) comes from (Lottes' Warp): the same mapping the shader
// samples with. Outside the window: the black border of the bent screen.
inline void warp(const Settings& s, float width, float height, float& x, float& y) {
    if (!s.on || width < 1 || height < 1) return;
    float u = x / width * 2 - 1, v = y / height * 2 - 1;
    const float bent_u = u * (1 + v * v * s.warp_x * max_warp), bent_v = v * (1 + u * u * s.warp_y * max_warp);
    x = (bent_u * 0.5f + 0.5f) * width;
    y = (bent_v * 0.5f + 0.5f) * height;
}

// Window pixels per scanline: about 360 lines on any screen (3 px at 1080p).
inline float linePitch(float height) { return std::max(2.0f, std::round(height / 360.0f)); }

// How long a change glows on (the phosphor), in seconds; 0 when it does not.
inline float phosphorDecay(float level) { return level <= 0.001f ? 0.0f : 0.55f + 0.37f * std::clamp(level, 0.0f, 1.0f); } // Kept per 1/60 s.
inline float phosphorSeconds(float level) {
    const float decay = phosphorDecay(level);
    return decay <= 0 ? 0.0f : std::log(1.0f / 255.0f) / std::log(decay) / 60.0f;
}

} // namespace crt_view
