#pragma once
// The desktop window's colour theme (Ustawienia → Interfejs → Okno desktopowe → Motyw okna): its
// surfaces, from the window's background up to popups, all dark with a tint of their own. Grafit is
// the original cool grey; the others keep its steps between layers in their own hue.
#include <d2d1.h>

#include <algorithm>
#include <array>

namespace desktop_theme {
struct Surfaces {
    D2D1_COLOR_F app, side, panel, menu, card, popup;
};
// Indexed like AppSettings::desktop_theme (desktop_themes in settings_view.hpp names them).
inline constexpr int count = 9;
inline Surfaces make(int theme) {
    // The window's background and the tint each lighter layer is lifted along.
    struct Base { float r, g, b, tr, tg, tb; };
    constexpr std::array<Base, count> bases{{
        {0.043f, 0.047f, 0.055f, 0.93f, 1.00f, 1.17f}, // Grafit: the original.
        {0.026f, 0.044f, 0.092f, 0.74f, 1.00f, 1.60f}, // Granat: deep blue.
        {0.027f, 0.064f, 0.050f, 0.78f, 1.32f, 1.02f}, // Las: dark green.
        {0.080f, 0.061f, 0.038f, 1.36f, 1.10f, 0.74f}, // Piasek: brown, sand-lit.
        {0.064f, 0.038f, 0.076f, 1.22f, 0.86f, 1.34f}, // Śliwka: plum.
        {0.020f, 0.068f, 0.078f, 0.72f, 1.25f, 1.35f}, // Morze: deep teal.
        {0.084f, 0.030f, 0.040f, 1.45f, 0.78f, 0.90f}, // Wino: burgundy.
        {0.044f, 0.036f, 0.098f, 1.00f, 0.88f, 1.60f}, // Indygo: violet blue.
        {0.000f, 0.000f, 0.000f, 1.00f, 1.00f, 1.05f}, // Czerń: true black (OLED).
    }};
    const auto& b = bases[static_cast<size_t>(std::clamp(theme, 0, count - 1))];
    const auto layer = [&](float step) { return D2D1::ColorF(b.r + step * b.tr, b.g + step * b.tg, b.b + step * b.tb, 1.0f); };
    // Grafit's steps: sidebar +0.014, panels +0.029, menus +0.04, cards and dialogs +0.044, popups +0.052.
    return {layer(0), layer(0.0135f), layer(0.0285f), layer(0.0395f), layer(0.0435f), layer(0.0515f)};
}
// The theme in use (the renderer sets it from the settings); every desktop surface reads it.
inline Surfaces& current() { static Surfaces value = make(0); return value; }
}
