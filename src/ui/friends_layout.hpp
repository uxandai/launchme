#pragma once

#include "../friends/steam_friends.hpp"
#include "../model/widget_layout.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace friends_layout {
// Right drawer: the dashboard (clock + tiles) and, inside it, the Steam friends list.
constexpr float drawer_width = 440.0f;
// The desktop window's Znajomi page: drawn at the window's scale times this, as the docked Profil.
constexpr float docked_scale = 0.86f;
constexpr float section_height = 44.0f;
constexpr float row_height = 80.0f;
constexpr float row_padding = 5.0f;
inline int group(const SteamFriend& person) {
    return !person.game.empty() ? 0 : person.status > 0 ? 1 : 2;
}

inline float rowTop(const std::vector<SteamFriend>& people, size_t index) {
    float y = 115;
    for (int category = 0; category < 3; ++category) {
        size_t count = 0;
        for (const auto& person : people) if (group(person) == category) ++count;
        if (!count) continue;
        y += section_height;
        for (size_t i = 0; i < people.size(); ++i) {
            if (group(people[i]) != category) continue;
            if (i == index) return y;
            y += row_height;
        }
    }
    return y;
}

inline float bottom(const std::vector<SteamFriend>& people) { return rowTop(people, people.size()); }

inline std::optional<size_t> hit(const std::vector<SteamFriend>& people, float y) {
    for (size_t i = 0; i < people.size(); ++i) {
        const float top = rowTop(people, i);
        if (y >= top + row_padding && y < top + row_height - row_padding) return i;
    }
    return std::nullopt;
}

struct Rect {
    float left = 0, top = 0, right = 0, bottom = 0;
    bool contains(float x, float y) const { return x >= left && x < right && y >= top && y < bottom; }
};

// Back button left of the list title (same shape as the grid screens' back button).
// page: the desktop window's Znajomi page, whose Back is the docked Profil's (a 48 px square, the title beside it).
inline Rect listBack(float drawer_left, bool page = false) {
    return page ? Rect{drawer_left + 24, 28, drawer_left + 72, 76} : Rect{drawer_left + 24, 38, drawer_left + 60, 74};
}

// Dashboard: clock header, then a 2-column grid of tiles in Tile order.
namespace dashboard {
// profile: the player's page (src/app/profile.cpp), a wide tile under the others by default.
enum class Tile { steam, discord, cpu, gpu, profile };
constexpr int tile_count = 5;
constexpr int columns = 2;
constexpr float margin = 24.0f, gap = 14.0f;
constexpr float grid_top = 172.0f;
constexpr float tile_width = (drawer_width - 2 * margin - gap) / columns;
// "Kafel" tiles are squares: two icons side by side and two above each other fill one exactly, so
// icons are squares too and every tile keeps the same gap and the drawer's margin.
constexpr float tile_height = tile_width;

inline Rect tile(float drawer_left, int index) {
    const float left = drawer_left + margin + (index % columns) * (tile_width + gap);
    const float top = grid_top + (index / columns) * (tile_height + gap);
    return {left, top, left + tile_width, top + tile_height};
}

inline int hit(float drawer_left, float x, float y, int count = tile_count) {
    for (int i = 0; i < count; ++i) if (tile(drawer_left, i).contains(x, y)) return i;
    return -1;
}

// D-pad/arrow movement in the grid; stops at the edges (no wrapping).
inline int move(int index, int horizontal, int vertical, int count = tile_count) {
    const int rows = (count + columns - 1) / columns;
    const int column = std::clamp(index % columns + horizontal, 0, columns - 1);
    const int row = std::clamp(index / columns + vertical, 0, rows - 1);
    return std::min(row * columns + column, count - 1);
}

constexpr int reorganize = -1, no_hit = -2;
inline Rect organizeButton(float left = 0) { return {left + drawer_width - margin - 44, 40, left + drawer_width - margin, 84}; }
struct Item {
    std::string id;
    int source = 0;
    widgets::Size size = widgets::Size::square;
    Rect box;
};
// Four half-columns/half-rows: icons occupy one cell, existing tiles 2x2,
// horizontal tiles 4x2. A forward-only first-fit keeps visual reading order.
struct Layout {
    std::vector<Item> items;
    float bottom = grid_top;
    Layout() = default;
    Layout(const std::vector<std::string>& available, const std::vector<widgets::Preference>& saved) {
        const auto add = [&](const std::string& id, widgets::Size size) {
            if (id.empty()) return; // A slot not offered here (the profile over a game).
            const auto found = std::find(available.begin(), available.end(), id);
            if (found != available.end() && std::none_of(items.begin(), items.end(), [&](const auto& i) { return i.id == id; }))
                items.push_back({id, static_cast<int>(found - available.begin()), size, {}});
        };
        for (const auto& p : saved) add(p.id, p.size);
        for (const auto& id : available) add(id, widgets::Size::square);
        constexpr float cw = (tile_width - gap) / 2, ch = (tile_height - gap) / 2;
        std::vector<bool> occupied(items.size() * 16 + 16);
        size_t cursor = 0;
        for (auto& item : items) {
            const int w = item.size == widgets::Size::wide ? 4 : item.size == widgets::Size::square ? 2 : 1;
            const int h = item.size == widgets::Size::icon ? 1 : 2;
            for (;; ++cursor) {
                if (cursor % 4 + w > 4) continue;
                bool free = true;
                for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) if (occupied[cursor + y * 4 + x]) free = false;
                if (free) break;
            }
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) occupied[cursor + y * 4 + x] = true;
            const float x = margin + static_cast<float>(cursor % 4) * (cw + gap);
            const float y = grid_top + static_cast<float>(cursor / 4) * (ch + gap);
            item.box = {x, y, x + w * (cw + gap) - gap, y + h * (ch + gap) - gap};
            bottom = std::max(bottom, item.box.bottom);
            cursor += w;
        }
    }
    Rect tile(int source, float left = 0, float scroll = 0) const {
        if (source == reorganize) return organizeButton(left);
        for (const auto& i : items) if (i.source == source)
            return {left + i.box.left, i.box.top - scroll, left + i.box.right, i.box.bottom - scroll};
        return {};
    }
    float maxScroll(float height) const { return std::max(0.0f, bottom - height + 20); }
    int hit(float left, float x, float y, float scroll, float height) const {
        if (organizeButton(left).contains(x, y)) return reorganize;
        if (y < grid_top - 8 || y >= height - 12) return no_hit;
        for (const auto& i : items) if (tile(i.source, left, scroll).contains(x, y)) return i.source;
        return no_hit;
    }
    int move(int source, int horizontal, int vertical) const {
        if (source == reorganize) return vertical > 0 && !items.empty() ? items.front().source : source;
        const auto from = tile(source);
        const float fx = (from.left + from.right) / 2, fy = (from.top + from.bottom) / 2;
        float best = std::numeric_limits<float>::max();
        int next = source;
        for (const auto& i : items) {
            if (i.source == source) continue;
            const auto& r = i.box;
            const float dx = (r.left + r.right) / 2 - fx, dy = (r.top + r.bottom) / 2 - fy;
            if ((horizontal && dx * horizontal <= 1) || (vertical && dy * vertical <= 1)) continue;
            const bool overlap = horizontal ? r.top < from.bottom && r.bottom > from.top : r.left < from.right && r.right > from.left;
            const float score = (overlap ? 0 : 10000) + (horizontal ? std::abs(dx) + std::abs(dy) * 2 : std::abs(dy) + std::abs(dx) * 2);
            if (score < best) { best = score; next = i.source; }
        }
        return next == source && vertical < 0 ? reorganize : next;
    }
};
} // namespace dashboard
} // namespace friends_layout
