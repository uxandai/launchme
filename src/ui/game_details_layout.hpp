#pragma once
#include "settings_view.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

// The game's page (Szczegóły, src/ui/details_renderer.cpp), after the PS5 game hub: the game's
// picture over the whole screen; Back at the top left; on the left its logo (or title) and one
// line of facts ending in the rating; under them one row of pill buttons; at the bottom the
// cards: the description, the facts table, and the achievements and the plugin's comparison.
// Drawing, clicks and the keys share this geometry.
//
// With Zrzuty ekranu i podobne gry (Interfejs) the page goes on below the cards: a strip of the
// game's screenshots and one of similar games; Down from the cards scrolls the page a section at a
// time, the focused strip's heading near the top (Layout::scroll moves everything; the background
// stays, blurred). Cards and strips measure as PSfive's details (psfive_details_layout.hpp).
//
// Action indices (Launcher::detail_action_): -1 Back, 0 the primary action, 1 "…",
// then the ones a game may have (identify, favourite, collections, versions, Zamknij grę beside
// Wróć while it runs, the cloud after "…"), then the achievements card, the rating and the plugin card.
namespace details_layout {
enum class Kind { back, rating, primary, close, more, cloud, collections, favorite, versions, identify, achievements, plugin, description, shot, similar };
// The strips' targets: a screenshot's and a similar game's action, by index; the Opis card's.
inline constexpr int shot_action = 100000, similar_action = 200000, description_action = 90000;
inline bool shotAction(int action) { return action >= shot_action && action < similar_action; }
inline bool similarAction(int action) { return action >= similar_action && action < similar_action + 100000; }
// The actions shown for this game (-2 or false: none).
struct Actions {
    int identify = -2, favorite = -2, collections = -2, versions = -2;
    int close = -2, cloud = -2; // ✕ "Zamknij grę" beside Wróć; the cloud saves after "…" (Aurelia).
    int achievements = -2, rating = -2, plugin = -2;
    int description = -2;   // The Opis card (description_action): Enter shows the whole description.
    bool playtime = false;  // A Czas gry card (play time, last played, the plugin's comparison) even without the plugin.
    bool badges = false;    // The game's mods: a line of badges under the facts (the facts and logo move up).
    // The rating pill ends the line of facts, after text only the renderer measures: its
    // rectangle as last drawn (Renderer::ratingLink); empty before the first frame.
    settings_ui::Rect rating_rect{};
    // Below the cards (0 and not loading: no strip): screenshots, similar games, how far each strip slid.
    size_t shots = 0, similar = 0;
    bool shots_loading = false;
    float shots_scroll = 0, similar_scroll = 0;
    bool media() const { return shots || shots_loading || similar; }
};
struct Item {
    int action;
    Kind kind;
    settings_ui::Rect rect;
    bool card() const { return kind == Kind::achievements || kind == Kind::plugin || kind == Kind::description; }
    bool media() const { return kind == Kind::shot || kind == Kind::similar; }
};
// Pill widths: wide enough for the longest label (Polish and English).
inline float buttonWidth(Kind kind) {
    switch (kind) {
    case Kind::primary: return 250;
    case Kind::more: return 64;
    case Kind::close: return 54;
    case Kind::cloud: return 54;
    case Kind::collections: return 180;
    case Kind::favorite: return 54;
    case Kind::versions: return 240;
    case Kind::identify: return 240;
    default: return 0;
    }
}
struct Layout {
    float width, height;
    float scroll = 0; // The page moved up to show the strips under the cards.
    static constexpr float margin = 77, button_height = 54, gap = 12, card_gap = 18;
    static constexpr float shot_width = 400, shot_height = 225, similar_size = 170, strip_gap = 18;
    static constexpr float section_top = 110; // A focused strip's heading once scrolled to it.
    float left() const { return margin; }
    float right() const { return width - margin; }
    // The achievements list (achievements_view.hpp) over the page: a centred panel.
    float panelWidth() const { return std::min(1280.0f, width - 128); }
    float origin() const { return (width - panelWidth()) * 0.5f; }
    float top() const { return std::max(120.0f, (height - 630) * 0.5f); }
    settings_ui::Rect back() const { return {margin, 40 - scroll, margin + 44, 84 - scroll}; }
    // Cards along the bottom, clear of the button hints.
    float cardsBottom() const { return height - 64 - scroll; }
    float cardsTop() const { return cardsBottom() - std::clamp(height * 0.29f, 236.0f, 330.0f); }
    // The buttons in their order on screen, wrapped onto a second line when they do not fit.
    std::vector<Item> buttons(const Actions& a) const {
        std::vector<std::pair<int, Kind>> list{{0, Kind::primary}};
        if (a.versions >= 0) list.push_back({a.versions, Kind::versions}); // Zmień wersję beside Graj.
        if (a.close >= 0) list.push_back({a.close, Kind::close});
        if (a.collections >= 0) list.push_back({a.collections, Kind::collections});
        if (a.favorite >= 0) list.push_back({a.favorite, Kind::favorite});
        if (a.identify >= 0) list.push_back({a.identify, Kind::identify});
        list.push_back({1, Kind::more});
        if (a.cloud >= 0) list.push_back({a.cloud, Kind::cloud});
        std::vector<Item> items;
        int line = 0;
        float x = left();
        for (const auto& [action, kind] : list) {
            const float w = buttonWidth(kind);
            if (x > left() && x + w > right()) { ++line; x = left(); }
            items.push_back({action, kind, {x, static_cast<float>(line), x + w, 0}});
            x += w + gap;
        }
        const int lines = line + 1;
        const float bottom = cardsTop() - 40;
        for (auto& item : items) {
            const float row = item.rect.top;
            item.rect.top = bottom - (lines - row) * button_height - (lines - 1 - row) * gap;
            item.rect.bottom = item.rect.top + button_height;
        }
        return items;
    }
    float buttonsTop(const Actions& a) const {
        const auto list = buttons(a);
        float top = cardsTop() - 40 - button_height;
        for (const auto& item : list) top = std::min(top, item.rect.top);
        return top;
    }
    // The line of facts (platform, year, genre, playtime, then the rating) on its centre line.
    float factsCenter(const Actions& a) const { return buttonsTop(a) - 44 - (a.badges ? 46.0f : 0.0f); }
    float badgesCenter(const Actions& a) const { return buttonsTop(a) - 44; }
    settings_ui::Rect rating(const Actions& a) const {
        if (a.rating_rect.right > a.rating_rect.left) return a.rating_rect;
        const float cy = factsCenter(a);
        return {left(), cy - 17, left() + 300, cy + 17};
    }
    // The logo or title over the facts, the picture clear on its right.
    settings_ui::Rect logo(const Actions& a) const {
        const float bottom = factsCenter(a) - 40;
        return {left(), std::max(104.0f - scroll, bottom - 180), std::min(left() + 720, right()), bottom};
    }
    // The strips under the cards: each a heading (its centre line) over a row of pictures.
    bool hasShots(const Actions& a) const { return a.shots || a.shots_loading; }
    float shotsLabel() const { return cardsBottom() + 80; } // Below the screen until the page moves.
    float shotsTop() const { return shotsLabel() + 26; }
    float similarLabel(const Actions& a) const { return hasShots(a) ? shotsTop() + shot_height + 52 : shotsLabel(); }
    float similarTop(const Actions& a) const { return similarLabel(a) + 26; }
    settings_ui::Rect shot(const Actions& a, size_t index) const {
        const float x = left() + static_cast<float>(index) * (shot_width + strip_gap) - a.shots_scroll;
        return {x, shotsTop(), x + shot_width, shotsTop() + shot_height};
    }
    settings_ui::Rect similar(const Actions& a, size_t index) const {
        const float x = left() + static_cast<float>(index) * (similar_size + strip_gap) - a.similar_scroll;
        return {x, similarTop(a), x + similar_size, similarTop(a) + similar_size};
    }
    // How far the page moves up while a strip has the focus: a section at a time, its heading near
    // the top (as PSfive's details scroll).
    float mediaScroll(const Actions& a, bool similar_focus) const {
        if (!a.media()) return 0;
        const Layout top{width, height};
        return std::max(0.0f, (similar_focus ? top.similarLabel(a) : top.shotsLabel()) - section_top);
    }
    // Where a strip slides to keep its focused item in view (the one after it peeking in).
    float stripScroll(size_t focus, size_t count, float step) const {
        const auto visible = static_cast<size_t>(std::max(1.0f, (right() - left() + strip_gap) / step));
        if (count <= visible || focus + 1 < visible) return 0;
        return static_cast<float>(std::min(focus + 2 - visible, count - visible)) * step;
    }
    // Cards, as PSfive's activity cards: one width each (a quarter of the row), in order the
    // achievements, the plugin's comparison, the description, the facts.
    bool progress(const Actions& a) const { return a.achievements >= 0 || a.plugin >= 0 || a.playtime; }
    settings_ui::Rect slot(int index) const {
        const float w = (right() - left() - 3 * card_gap) / 4, x = left() + static_cast<float>(index) * (w + card_gap);
        return {x, cardsTop(), x + w, cardsBottom()};
    }
    // The Czas gry card stands where the plugin's comparison stood (it holds that comparison too).
    bool timeCard(const Actions& a) const { return a.plugin >= 0 || a.playtime; }
    int leading(const Actions& a) const { return (a.achievements >= 0) + timeCard(a); }
    settings_ui::Rect description(const Actions& a) const { return slot(leading(a)); }
    settings_ui::Rect facts(const Actions& a) const { return slot(leading(a) + 1); }
    settings_ui::Rect progressCard(const Actions& a, bool plugin) const { return slot(plugin && a.achievements >= 0 ? 1 : 0); }
    // Every focus target: Back, the rating, the buttons, the cards.
    std::vector<Item> items(const Actions& a) const {
        std::vector<Item> all{{-1, Kind::back, back()}};
        if (a.rating >= 0) all.push_back({a.rating, Kind::rating, rating(a)});
        const auto row = buttons(a);
        all.insert(all.end(), row.begin(), row.end());
        if (a.achievements >= 0) all.push_back({a.achievements, Kind::achievements, progressCard(a, false)});
        if (a.plugin >= 0) all.push_back({a.plugin, Kind::plugin, progressCard(a, true)});
        if (a.description >= 0) all.push_back({a.description, Kind::description, description(a)});
        for (size_t i = 0; i < a.shots; ++i) all.push_back({shot_action + static_cast<int>(i), Kind::shot, shot(a, i)});
        for (size_t i = 0; i < a.similar; ++i) all.push_back({similar_action + static_cast<int>(i), Kind::similar, similar(a, i)});
        return all;
    }
    const Item* find(const std::vector<Item>& all, int action) const {
        for (const auto& item : all) if (item.action == action) return &item;
        return nullptr;
    }
    // The rectangle of one target (empty when the game does not have it).
    settings_ui::Rect rect(const Actions& a, int action) const {
        const auto all = items(a);
        const auto* item = find(all, action);
        return item ? item->rect : settings_ui::Rect{};
    }
    int hit(const Actions& a, float x, float y) const {
        for (const auto& item : items(a))
            if (item.rect.contains(x, y) && (!item.media() || item.rect.left >= left() - 1)) return item.action; // Not one slid past the margin.
        return -3;
    }
};
// The next focus from `action` in a direction, by position on screen: the nearest target whose
// centre lies that way (straight lines count more than diagonals); `action` itself at an edge.
inline int move(const std::vector<Item>& all, int action, int dx, int dy) {
    const Item* from = nullptr;
    for (const auto& item : all) if (item.action == action) from = &item;
    if (!from) return all.size() > 1 ? all[1].action : action;
    const float fx = (from->rect.left + from->rect.right) * 0.5f, fy = (from->rect.top + from->rect.bottom) * 0.5f;
    int best = action;
    float best_score = 1e9f;
    for (const auto& item : all) {
        if (item.action == action) continue;
        const float cx = (item.rect.left + item.rect.right) * 0.5f, cy = (item.rect.top + item.rect.bottom) * 0.5f;
        // Along the move: how far the target's near edge is; across it: how far its span is.
        float along = 0, across = 0;
        if (dx) {
            along = dx > 0 ? item.rect.left - from->rect.right : from->rect.left - item.rect.right;
            if ((dx > 0 ? cx <= fx : cx >= fx) || along < -4) continue;
            // Left and right keep to the line: only targets beside this one.
            if (item.rect.top >= from->rect.bottom || item.rect.bottom <= from->rect.top) continue;
            across = std::abs(cy - fy);
        } else {
            along = dy > 0 ? item.rect.top - from->rect.bottom : from->rect.top - item.rect.bottom;
            if ((dy > 0 ? cy <= fy : cy >= fy) || along < -4) continue;
            // Up and down keep to the column: the horizontal distance between the centres.
            across = std::abs(cx - fx) * 0.5f + std::max({0.0f, item.rect.left - from->rect.right, from->rect.left - item.rect.right});
        }
        const float score = std::max(0.0f, along) + across * 3;
        if (score < best_score) { best_score = score; best = item.action; }
    }
    return best;
}
inline settings_ui::Rect back(float width, float height) { return Layout{width, height}.back(); }
}
