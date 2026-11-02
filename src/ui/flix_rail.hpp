#pragma once

// Flix → Pasek boczny (AppSettings::flix_rail): a permanent column of icons at the left edge, as in
// streaming apps. At the top Start (the first row), one icon for each default category the page has
// (Platformy PC, Emulacja, Wydawcy, Developerzy, Ulubione: its row is scrolled to), Szukaj and
// Wszystkie gry; at the bottom the profile (her face, or a profile mark), the menu and the widgets.
// The rows start right of it (flix_layout::rail). Shared by the app (src/app/flix_rail.cpp: focus,
// actions, the pointer) and the renderer (src/ui/flix_rail_renderer.cpp).
#include "settings_view.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace flix_rail {
enum class Kind { home, category, search, library, profile, menu, widgets };
struct Item {
    Kind kind = Kind::home;
    size_t category = 0; // Kind::category: the category whose row it scrolls to.
    wchar_t glyph = 0;   // Segoe Fluent / launchme-icons code point; the profile draws her face when she is there.
    std::wstring label;  // Beside the icon while it has the focus or the pointer.
    bool operator==(const Item&) const = default;
};
inline bool bottom(Kind kind) { return kind == Kind::profile || kind == Kind::menu || kind == Kind::widgets; }

constexpr float width = 92;     // The column; flix_layout::rail moves the rows right by it.
constexpr float size = 46;      // An icon's disc.
constexpr float pitch = 60;     // From one icon to the next.
constexpr float top = 112;      // The first icon's top.
constexpr float bottom_gap = 44; // Under the last icon.

struct Layout {
    float height = 720;
    const std::vector<Item>* items = nullptr;
    size_t count() const { return items ? items->size() : 0; }
    // Top group from `top` down; the bottom group stacked up from the bottom edge.
    settings_ui::Rect item(size_t index) const {
        const auto& list = *items;
        size_t after = 0; // Bottom icons after this one.
        for (size_t i = index + 1; i < list.size(); ++i) after += bottom(list[i].kind);
        const float cx = width * 0.5f;
        const float cy = bottom(list[index].kind) ? height - bottom_gap - size * 0.5f - static_cast<float>(after) * pitch
                                                  : top + size * 0.5f + static_cast<float>(index) * pitch;
        return {cx - size * 0.5f, cy - size * 0.5f, cx + size * 0.5f, cy + size * 0.5f};
    }
    int hit(float x, float y) const {
        if (x < 0 || x > width) return -1;
        for (size_t i = 0; i < count(); ++i) {
            auto r = item(i);
            r.left -= 7; r.right += 7; r.top -= (pitch - size) * 0.5f; r.bottom += (pitch - size) * 0.5f;
            if (r.contains(x, y)) return static_cast<int>(i);
        }
        return -1;
    }
};
} // namespace flix_rail
