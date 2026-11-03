#pragma once
#include "../i18n/i18n.hpp"
#include <array>
#include <algorithm>
#include <cmath>

namespace quick_menu {
// Order: Szukaj, Wszystkie gry, the plugins' pages (Sklep), Pobierania, Kontrolery, Dźwięk,
// Widgety, Ustawienia, Zasilanie. The Guide overlay (over a game) has its own, shorter bar: Start, "Zamknij grę",
// Kontrolery, Dźwięk, Widgety, Ustawienia, Zasilanie (no search, library, store or downloads in front of a game).
enum class Action { search, library, store, controllers, audio, settings, power, dashboard, close_game, downloads, widgets };
// All store plugins (the "menu" contributions) share ONE entry, always shown: its tabs switch between them.
inline constexpr int store_slots = 1;
inline int prefix(bool guide) { return guide ? 2 : 0; }
inline constexpr int guide_count = 7;
inline int count(int plugins, bool guide = false) { return guide ? guide_count : 8 + plugins; }
inline int pluginIndex(int index, bool guide = false) { return guide ? -1 : index - 2; }
inline Action action(int index, int plugins, bool guide = false) {
    if (guide) {
        if (index == 0) return Action::dashboard;
        if (index == 1) return Action::close_game;
        constexpr std::array rest{Action::controllers, Action::audio, Action::widgets, Action::settings, Action::power};
        return rest[static_cast<size_t>(std::clamp(index - prefix(guide), 0, 4))];
    }
    constexpr std::array base{Action::search, Action::library, Action::downloads, Action::controllers, Action::audio, Action::widgets, Action::settings, Action::power};
    if (index >= 2 && index < 2 + plugins) return Action::store; // Generic extension glyph; label comes from its manifest.
    return base[index >= 2 + plugins ? index - plugins : index];
}
inline constexpr float step = 116, guide_gap = 32;
inline float center(float width, int index, int plugins, bool guide = false) {
    return width * 0.5f + (index - (count(plugins, guide) - 1) * 0.5f) * step +
        (guide ? (index < prefix(guide) ? -guide_gap * 0.5f : guide_gap * 0.5f) : 0);
}
inline float centerY(float height, float progress, bool guide) {
    const float p = std::clamp(progress, 0.0f, 1.0f);
    return height - 65 + (guide ? (1 - p * p * (3 - 2 * p)) * 72 : 0);
}
inline int hit(float width, float height, float x, float y, int plugins, bool guide, float progress = 1) {
    if (std::abs(y - centerY(height, progress, guide)) > 48) return -1;
    for (int i = 0; i < count(plugins, guide); ++i) if (std::abs(x - center(width, i, plugins, guide)) < 43) return i;
    return -1;
}
inline const wchar_t* caption(Action action) {
    switch (action) {
    case Action::search: return tr(L"Szukaj");
    case Action::library: return tr(L"Wszystkie gry");
    case Action::store: return tr(L"Sklepy");
    case Action::settings: return tr(L"Ustawienia");
    case Action::controllers: return tr(L"Kontrolery");
    case Action::dashboard: return tr(L"Start");
    case Action::close_game: return tr(L"Zamknij grę");
    case Action::audio: return tr(L"Dźwięk");
    case Action::downloads: return tr(L"Pobierania");
    case Action::widgets: return tr(L"Widgety");
    default: return tr(L"Zasilanie");
    }
}
}
