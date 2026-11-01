#pragma once

#include "../model/game_card.hpp"
#include "settings_view.hpp"

#include <algorithm>
#include <array>
#include <optional>

namespace context_menu {
// Display order; hidden rows are skipped and the remaining ones shift up.
// Experimental quick resume: "suspend" for a running game; while a game runs or
// is suspended, "close" takes the place of "retry". Plugin rows follow the primary actions.
// The crop editor opens from "Zmień grafikę" (the artwork browser's "Kadruj").
// "launch": Details' "…" → Opcje uruchamiania (a game's file, options and folder; Steam: its options).
// "metadata": Details' "…" → Edytuj metadane (the player's own title, studios, dates and play time).
// "remove": Usuń z biblioteki, a game added by hand (only its entry goes, after a question), red and last like Odinstaluj.
// "compat": Details' "…" → Proton / Wine (Linux: a Windows game's Wine/Proton options).
// "shortcuts": Details' "…" → Skróty (the game's desktop, menu and Steam shortcuts).
// "identify": Zidentyfikuj grę… on a game no RAWG or ScreenScraper match names yet (Launcher::unidentifiedGame).
enum class Item { primary, suspend, plugin, details, artwork, retry, close, folder, hide, exclude, uninstall, launch, metadata, aurelia, steam_account, remove, compat, shortcuts, identify };
struct Rows {
    bool suspend = false;
    int plugins = 0;
    bool session = false;
    bool detail_options = false, folder = false, exclude = false, hide = false;
    bool secondary_only = false; // PSfive has a separate primary button; Details leads its menu.
    bool uninstall = false;      // Details' "…" and the game's menu: an installed game Launchme knows how to remove.
    bool launch = false;         // Details' "…": Launcher::launchEditable.
    bool metadata = false;       // Details' "…": a game with its own key.
    bool aurelia = false;
    bool steam_account = false;  // Details' "…": a Steam game and more than one remembered account to launch it from.
    bool remove = false;         // Launcher::canRemoveFromLibrary (never while the game runs).
    bool compat = false;         // Details' "…": Launcher::compatGame (Linux).
    bool identify = false;       // The game's menu: Launcher::unidentifiedGame.
    bool shortcuts = false;      // Details' "…": the game can have shortcuts (Launcher::shortcutAvailable).
};
constexpr auto order(Rows options) {
    return options.secondary_only ?
        std::array{Item::details, Item::primary, Item::suspend, Item::aurelia, Item::plugin, Item::identify, Item::artwork, Item::retry, Item::close, Item::folder, Item::launch, Item::compat, Item::shortcuts, Item::steam_account, Item::metadata, Item::hide, Item::exclude, Item::uninstall, Item::remove} :
        std::array{Item::primary, Item::suspend, Item::aurelia, Item::plugin, Item::details, Item::identify, Item::artwork, Item::retry, Item::close, Item::folder, Item::launch, Item::compat, Item::shortcuts, Item::steam_account, Item::metadata, Item::hide, Item::exclude, Item::uninstall, Item::remove};
}
constexpr bool visible(Item entry, Rows options) {
    if (entry == Item::aurelia) return options.aurelia;
    if (entry == Item::identify) return options.identify && !options.detail_options;
    if (entry == Item::steam_account) return options.detail_options && options.steam_account;
    if (options.detail_options) return entry == Item::artwork || (entry == Item::launch && options.launch) || (entry == Item::compat && options.compat) ||
        (entry == Item::shortcuts && options.shortcuts) ||
        (entry == Item::metadata && options.metadata) || (entry == Item::folder && options.folder) ||
        (entry == Item::hide && options.hide) || (entry == Item::exclude && options.exclude) || (entry == Item::uninstall && options.uninstall) ||
        (entry == Item::remove && options.remove);
    switch (entry) {
    case Item::primary: return !options.secondary_only;
    case Item::suspend: return options.suspend;
    case Item::plugin: return options.plugins > 0;
    case Item::retry: return !options.session;
    case Item::close: return options.session;
    case Item::uninstall: return options.uninstall && !options.session; // Last and red in the game's menu too ("…" on PSfive); never while it runs.
    case Item::remove: return options.remove && !options.session;
    case Item::folder: case Item::hide: case Item::exclude: case Item::launch: case Item::metadata: case Item::compat: case Item::shortcuts: return false;
    default: return true;
    }
}
constexpr int rows(Rows options) {
    int count = 0;
    for (const auto entry : order(options)) count += visible(entry, options);
    return count + (visible(Item::plugin, options) ? std::max(0, options.plugins - 1) : 0);
}
constexpr Item item(int row, Rows options) {
    Item last = Item::primary;
    int shown = 0;
    for (const auto entry : order(options)) {
        if (!visible(entry, options)) continue;
        const int count = entry == Item::plugin ? options.plugins : 1;
        if (std::max(row, 0) < shown + count) return entry;
        shown += count;
        last = entry;
    }
    return last;
}
constexpr int row(Item entry, Rows options) {
    if (!visible(entry, options)) return -1;
    int result = 0;
    for (const auto current : order(options)) {
        if (current == entry) break;
        result += !visible(current, options) ? 0 : current == Item::plugin ? options.plugins : 1;
    }
    return result;
}
constexpr float height(Rows options) { return 16.0f + rows(options) * 53.0f; }
inline float left(const GameCard& card) { return card.x; }
inline float top(const GameCard& card, Rows options) { return std::max(8.0f, card.y - height(options) - 14.0f); }
inline float width(const GameCard& card) { return card.width(); }
// XMB tiles, Flix rows and list rows sit near the top edge or are small, so the menu opens
// beside the card instead of above it (on its left when the right side has no room).
inline float width(const GameCard& card, bool beside) { return beside ? std::clamp(card.size * 2.0f, 260.0f, 320.0f) : width(card); }
inline float left(const GameCard& card, bool beside, float view_width = 1e9f) {
    if (!beside) return left(card);
    const float right = card.x + card.width() + 18.0f;
    return right + width(card, true) <= view_width - 8.0f ? right : std::max(8.0f, card.x - 18.0f - width(card, true));
}
inline float top(const GameCard& card, Rows options, bool beside, float view_height) {
    return beside ? std::clamp(card.y, 8.0f, std::max(8.0f, view_height - height(options) - 8.0f)) : top(card, options);
}
inline settings_ui::Rect bounds(const GameCard& card, Rows rows, bool beside, float view_width, float view_height,
                                const std::optional<settings_ui::Rect>& anchor = {}) {
    float w = anchor ? 300.0f : width(card, beside);
    const float h = height(rows);
    if (anchor) {
        const float x = std::clamp(anchor->left, 8.0f, std::max(8.0f, view_width - w - 8));
        const float y = anchor->bottom + 10 + h <= view_height - 8 ? anchor->bottom + 10 : std::max(8.0f, anchor->top - h - 10);
        return {x, y, x + w, y + h};
    }
    float x = left(card, beside, view_width);
    const float y = top(card, rows, beside, view_height);
    if (rows.remove && !beside && w < 270.0f) { // "Usuń z biblioteki" needs more than a small card's width.
        w = 270.0f;
        x = std::min(x, std::max(8.0f, view_width - w - 8));
    }
    return {x, y, x + w, y + h};
}
// Okna i podpowiedzi → Duże menu gry: the same rows as a large dialog in the middle of the screen,
// an icon beside each, the game's name and cover in its header; long menus scroll.
struct Dialog {
    static constexpr float max_width = 760, row_height = 70, row_step = 78, header = 150, padding = 26;
    settings_ui::Rect panel{};
    int rows = 0, visible = 0;
    Dialog(float view_width, float view_height, int count) : rows(count) {
        const float w = std::min(max_width, view_width - 96);
        const float room = std::max(row_step, view_height - 120 - header - padding);
        visible = std::clamp(static_cast<int>(room / row_step), 1, std::max(1, count));
        const float h = header + visible * row_step + padding;
        panel = {(view_width - w) * 0.5f, (view_height - h) * 0.5f, (view_width + w) * 0.5f, (view_height + h) * 0.5f};
    }
    // The first row shown: the selected one stays in view, a row of context above and below it.
    int first(int selected) const {
        if (rows <= visible) return 0;
        return std::clamp(selected - visible / 2, 0, rows - visible);
    }
    settings_ui::Rect row(int index, int first_row) const {
        const float y = panel.top + header + (index - first_row) * row_step;
        return {panel.left + padding, y, panel.right - padding, y + row_height};
    }
    int hit(float x, float y, int first_row) const {
        for (int i = first_row; i < std::min(rows, first_row + visible); ++i) if (row(i, first_row).contains(x, y)) return i;
        return -1;
    }
};
} // namespace context_menu
