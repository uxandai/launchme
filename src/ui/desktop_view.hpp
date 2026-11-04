#pragma once
// Desktop mode (src/app/desktop.cpp, desktop_window.cpp, src/ui/desktop_renderer.cpp): the launcher
// as a resizable window for the mouse and keyboard. The top bar holds search, the shortcuts (Glance,
// Towarzyszka, Sklep, Widżety, Pobierania, Tryb TV, Ustawienia) and the window buttons; the sidebar
// lists the library's sections, platforms, consoles and collections; the middle shows the section's
// games as 2:3 posters; the right pane is the selected game's card. The TV layouts (Domyślny, XMB,
// Flix, Metro, PSfive) are "Tryb TV". Geometry here is shared by drawing and hit tests.
#include "settings_view.hpp"
#include "downloads_view.hpp"
#include "../model/game.hpp"
#include "../model/game_details.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace desktop_ui {
using settings_ui::Rect;

// Icon font glyphs: Segoe Fluent Icons (Windows 11) keeps the code points of Segoe MDL2 Assets.
namespace glyph {
inline constexpr wchar_t library = 0xE8F1, history = 0xE81C, star = 0xE734, star_fill = 0xE735, game = 0xE7FC,
    hide = 0xED1A, view = 0xE890, shop = 0xE719, folder = 0xE8B7, folder_open = 0xE838, settings = 0xE713,
    download = 0xE896, tv = 0xE7F4, people = 0xE716, search = 0xE721, close = 0xE711, sort = 0xE8CB, check = 0xE73E,
    add = 0xE710, chevron_down = 0xE70D, chevron_right = 0xE76C, play = 0xE768, pause = 0xE769, more = 0xE712,
    info = 0xE946, picture = 0xE8B9, refresh = 0xE72C, edit = 0xE70F, remove = 0xE74D, open = 0xE8A7, tag = 0xE8EC,
    minimize = 0xE921, maximize = 0xE922, restore = 0xE923, window_close = 0xE8BB, power = 0xE7E8, sync = 0xE895,
    stop = 0xE71A, copy = 0xE8C8, filter = 0xE71C;
}

// ---- Sidebar ----------------------------------------------------------------------------------

// publishers, developers: the studios with a logo (src/app/studio_libraries.cpp), folded by default.
enum class Group { library, platforms, emulation, collections, publishers, developers };
inline constexpr int group_count = 6;
inline bool studioGroup(Group group) { return group == Group::publishers || group == Group::developers; }
enum class EntryKind { all, recent, favorites, hidden, source, console, collection, studio };
struct Entry {
    EntryKind kind = EntryKind::all;
    Group group = Group::library;
    std::string id;        // "all", "recent", "favorites", "hidden", "source:gog", "console:PS2", "collection:<id>", "publisher:capcom".
    std::wstring title;
    size_t count = 0;
    GameSource source = GameSource::steam;
    std::wstring console;
    std::string collection; // SteamCollection::id.
    wchar_t icon = glyph::library;
    std::string brand; // The store's or console's mark (src/ui/brand_icons.hpp) instead of the icon; empty: the icon.
    bool droppable = false;   // Games dragged here join it (Ulubione, collections).
    bool reorderable = false; // Dragged within its group: the collection order.
};
// A sidebar row: a group heading or an entry; `top` in the list's own space.
struct Row {
    bool heading = false;
    Group group = Group::library;
    int entry = -1;
    float top = 0, height = 0;
};
inline constexpr float heading_h = 30, row_h = 32, heading_gap = 12, companion_h = 74;

// Headings and entries in order; a collapsed group keeps only its heading. Collections always keep
// their heading (its "+" makes a new one), the others only when they have entries. The narrow
// sidebar (icons only) has no headings: every entry, a gap between the groups, but no
// collections or studios (one icon each, nothing to tell them apart).
inline std::vector<Row> layoutRows(const std::vector<Entry>& entries, const std::array<bool, group_count>& collapsed, bool narrow = false) {
    std::vector<Row> rows;
    float y = 2;
    for (int g = 0; g < group_count; ++g) {
        const auto group = static_cast<Group>(g);
        const bool any = std::any_of(entries.begin(), entries.end(), [&](const Entry& e) { return e.group == group; });
        if (narrow && (!any || group == Group::collections || studioGroup(group))) continue;
        if (!any && group != Group::collections) continue;
        if (!rows.empty()) y += heading_gap;
        if (!narrow) {
            rows.push_back({true, group, -1, y, heading_h});
            y += heading_h;
            if (collapsed[g]) continue;
        }
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            if (entries[i].group != group) continue;
            rows.push_back({false, group, i, y, row_h});
            y += row_h + 2;
        }
    }
    return rows;
}
inline float rowsHeight(const std::vector<Row>& rows) { return rows.empty() ? 0 : rows.back().top + rows.back().height + 8; }

// ---- Menus -----------------------------------------------------------------------------------

enum class Action {
    none, play, suspend, close_game, details, favorite, collections, collection_toggle, collection_new, collection_manage,
    versions, artwork, retry_art, missing_art, identify, folder, hide, exclude, plugin, sort, section_rename, section_games,
    section_options, section_delete, section_up, section_down, section_tv, section_hide, clear_marks, select_all, store_page, downloads, metadata, companion_change, companion_mind,
    companion_settings, uninstall, launch_options, aurelia, steam_account, emulator, remove_game,
    compat_options, // Linux: the game's Proton / Wine options (Launcher::openGameCompatSettings).
    shortcuts,      // Skróty ▸: the game's shortcuts outside Launchme (Launcher::setGameShortcut).
    shortcut_toggle, // One of them (value: ShortcutPlace), checked when it exists.
    saves,          // Zapisy ▸: Ludusavi's backup and restore of the game's saves.
    save_backup, save_restore, save_ludusavi
};
struct MenuItem {
    Action action = Action::none;
    std::wstring label, shortcut;
    wchar_t icon = 0;
    bool enabled = true, checked = false, danger = false, separator = false, submenu = false;
    std::string key; // A collection, a plugin action, a sort order…
    int value = 0;
};
struct Menu {
    enum class Kind { game, section, sort, collections, store, companion } kind = Kind::game;
    std::vector<MenuItem> items;
    float x = 0, y = 0;      // Top-left wanted (right edge with align_right).
    bool align_right = false;
    int hover = -1;          // Highlighted row: the mouse, or keys (keyboard = true).
    bool keyboard = false;
    int sub_parent = -1;     // The item whose submenu is open.
    std::vector<MenuItem> sub;
    int sub_hover = -1;
    bool in_sub = false;     // Keys move in the submenu.
    std::vector<std::string> games; // Selection keys the game menu acts on.
    std::string entry;       // The sidebar entry of a section menu.
};
inline constexpr float menu_row = 32, menu_separator = 9, menu_padding = 5, menu_width = 256, submenu_width = 248;
inline float menuHeight(const std::vector<MenuItem>& items) {
    float h = 2 * menu_padding;
    for (const auto& item : items) h += item.separator ? menu_separator : menu_row;
    return h;
}
inline Rect menuRect(const Menu& menu, float width, float height) {
    const float w = menu_width, h = std::min(menuHeight(menu.items), height - 16);
    float x = menu.align_right ? menu.x - w : menu.x, y = menu.y;
    if (x + w > width - 8) x = std::max(8.0f, (menu.align_right ? width - 8 : menu.x) - w);
    x = std::clamp(x, 8.0f, std::max(8.0f, width - w - 8));
    // Below the pointer, else above it; fitting neither way it moves up just enough (not to the top).
    if (y + h > height - 8) { y = menu.y - h; if (y < 8) y = std::max(8.0f, height - 8 - h); }
    return {x, y, x + w, y + h};
}
inline Rect itemRect(Rect panel, const std::vector<MenuItem>& items, int index) {
    float y = panel.top + menu_padding;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const float h = items[i].separator ? menu_separator : menu_row;
        if (i == index) return {panel.left + menu_padding, y, panel.right - menu_padding, y + h};
        y += h;
    }
    return {};
}
inline int hitItem(Rect panel, const std::vector<MenuItem>& items, float x, float y) {
    if (!panel.contains(x, y)) return -1;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        if (!items[i].separator && itemRect(panel, items, i).contains(x, y)) return i;
    return -1;
}
// Beside its parent row, on the right unless the window ends there.
inline Rect subRect(const Menu& menu, float width, float height) {
    const auto panel = menuRect(menu, width, height);
    const auto parent = itemRect(panel, menu.items, menu.sub_parent);
    const float w = submenu_width, h = std::min(menuHeight(menu.sub), height - 16);
    float x = panel.right - 4;
    if (x + w > width - 8) x = panel.left - w + 4;
    const float y = std::clamp(parent.top - menu_padding, 8.0f, std::max(8.0f, height - h - 8));
    return {x, y, x + w, y + h};
}
// The next enabled row from `from` in `direction` (wrapping); -1 without one.
inline int stepItem(const std::vector<MenuItem>& items, int from, int direction) {
    const int n = static_cast<int>(items.size());
    for (int step = 1; step <= n; ++step) {
        const int i = ((from < 0 ? (direction > 0 ? -1 : 0) : from) + direction * step % n + n * 2) % n;
        if (!items[i].separator && items[i].enabled) return i;
    }
    return -1;
}

// ---- Hit targets and the top bar's shortcuts ---------------------------------------------------

enum class Target {
    none, caption, minimize, maximize, close, search, search_clear, tool, sidebar_row, sidebar_add, companion_card,
    companion_bubble, card, card_play, card_check, popover_clear, popover_action, scrollbar, sort, installed, size_slider, pane_primary, pane_favorite,
    pane_more, pane_quick, pane_chip, pane_chip_add, pane_rating, pane_studio, bulk, menu_item, submenu_item, popover_row, popover_all,
    empty_primary, empty_secondary, grid, library_add, library_refresh, companion_more,
    sidebar_toggle, pane_resize, pane_achievements, pane_cloud,
    pane_protondb, // Linux: ProtonDB and Steam Deck ratings, opening ProtonDB's page.
    pane_hardware  // The hardware check above Opis: unfolds this PC against the game's requirements.
};
struct Hit {
    Target target = Target::none;
    int index = -1;
    bool operator==(const Hit&) const = default;
};
enum class Tool { glance, companion, store, friends, downloads, tv, settings, tarot, update }; // update: a newer Launchme waits.

// ---- What the renderer draws -------------------------------------------------------------------

// "Czy to u mnie zadziała": a PC game's requirements against this PC (src/app/hardware_check.cpp).
struct Hardware {
    bool shown = false, open = false, loading = false;
    int level = 0; // 0 grey (unknown), 1 green, 2 yellow, 3 red.
    std::wstring summary;
    struct Row { int level = 0; std::wstring label, yours, minimum, recommended; };
    std::vector<Row> rows;
};
struct Pane {
    bool game = false;
    std::wstring name, platform, status, primary;
    bool primary_enabled = true, running = false, frozen = false, installed = true, launching = false;
    std::optional<float> progress; // An installation's share, inside the primary button.
    bool favorite = false;
    float favorite_pulse = 0; // 1 → 0 after toggling favourite: the star pops.
    std::wstring playtime, last_played, achievements, rating;
    Hardware hardware;
    std::wstring protondb; // Linux: "ProtonDB: Gold · Steam Deck: Verified"; empty: unknown or not asked.
    float achievements_share = -1;
    bool achievements_link = false; // The number opens Szczegóły → Osiągnięcia.
    std::vector<std::pair<std::wstring, std::string>> collections; // Name, sidebar entry id.
    bool folder = false, hideable = false, hidden = false;
    GameDetails details;
    bool details_loading = false;
    size_t marked = 0; // More than one: the bulk actions.
};
struct EmptyState {
    wchar_t icon = 0;
    std::wstring title, body, primary, secondary;
};
// ---- Ctrl+K: quick search over the library window (src/app/desktop_quick.cpp) ----------------

struct QuickItem {
    enum class Kind { game, section, setting, action } kind = Kind::game;
    std::wstring title, detail; // Its name; what it is ("Steam", "Kolekcja", "Ustawienia", ...).
    wchar_t icon = 0;
    std::string brand;          // A store's or console's mark instead of the icon.
    std::string target;         // Game key, section id, settings category or action name.
};
struct QuickView {
    bool open = false;
    std::wstring query;
    size_t caret = 0;
    int selected = 0, hover = -1;
    std::vector<QuickItem> items;
};
inline constexpr float quick_width = 640, quick_field = 58, quick_row = 46, quick_foot = 38;
inline constexpr int quick_rows = 9;
struct QuickLayout {
    float width = 0, height = 0;
    int count = 0;
    Rect panel() const {
        const float w = std::min(quick_width, width - 48);
        const float h = quick_field + 12 + static_cast<float>(std::max(1, std::min(count, quick_rows))) * quick_row + quick_foot;
        const float left = std::round((width - w) * 0.5f), top = std::max(72.0f, std::round(height * 0.12f));
        return {left, top, left + w, top + h};
    }
    Rect field() const { const auto p = panel(); return {p.left, p.top, p.right, p.top + quick_field}; }
    Rect row(int index) const {
        const auto p = panel();
        const float y = p.top + quick_field + 6 + static_cast<float>(index) * quick_row;
        return {p.left + 8, y, p.right - 8, y + quick_row};
    }
    int hit(float x, float y) const {
        for (int i = 0; i < std::min(count, quick_rows); ++i) if (row(i).contains(x, y)) return i;
        return -1;
    }
};

// ---- Edytuj metadane: the player's own title and details (src/app/desktop_metadata.cpp) -----

inline constexpr int meta_fields = 8; // Tytuł, Opis, Producent, Wydawca, Data premiery, Gatunki, Czas przejścia, Czas gry.
// identify: Zidentyfikuj, the game's match in RAWG (and its artwork) over the dialog.
enum class MetaItem { title, description, developer, publisher, release_date, genres, hours, playtime, save, restore, cancel, close, identify };
struct MetaView {
    bool open = false;
    std::wstring game;                           // Its name when the dialog opened.
    std::wstring playtime_note;                  // Beside Czas gry: where the automatic value comes from.
    std::array<std::wstring, meta_fields> fields;
    std::array<std::wstring, meta_fields> shown; // As they were shown: unchanged ones keep their state.
    int focus = 0;                               // A MetaItem.
    size_t caret = 0;
    bool selected = false;                       // Ctrl+A: typing replaces the field.
    int hover = -1;
    bool overridden = false;                     // Przywróć automatyczne has something to undo.
    bool identify = false;                       // Zidentyfikuj is offered (not for Steam's own games).
    bool pad = false;                            // Tryb TV: the pad's hint under the heading.
};
struct MetaLayout {
    float width = 0, height = 0;
    static constexpr float label_h = 22, line_h = 38, text_h = 136, gap = 14, button_h = 38;
    Rect panel() const {
        const float w = std::min(700.0f, width - 48), h = std::min(660.0f, height - 48);
        const float left = std::round((width - w) * 0.5f), top = std::round((height - h) * 0.5f);
        return {left, top, left + w, top + h};
    }
    Rect close() const { const auto p = panel(); return {p.right - 46, p.top + 12, p.right - 14, p.top + 44}; }
    Rect field(int index) const {
        const auto p = panel();
        const float left = p.left + 24, right = p.right - 24, half = (right - left - 16) * 0.5f;
        float y = p.top + 76 + label_h;               // Tytuł (under the heading and its hint).
        if (index == 0) return {left, y, right, y + line_h};
        y += line_h + gap + label_h;                  // Opis.
        if (index == 1) return {left, y, right, y + text_h};
        y += text_h + gap + label_h;                  // Producent | Wydawca.
        if (index == 2) return {left, y, left + half, y + line_h};
        if (index == 3) return {right - half, y, right, y + line_h};
        y += line_h + gap + label_h;                  // Data premiery | Czas przejścia | Gatunki.
        if (index == 4) return {left, y, left + 170, y + line_h};
        if (index == 6) return {left + 186, y, left + 316, y + line_h};
        if (index == 5) return {left + 332, y, right, y + line_h};
        y += line_h + gap + label_h;                  // Czas gry (its note beside it).
        return {left, y, left + 170, y + line_h};
    }
    Rect button(MetaItem item) const {
        const auto p = panel();
        const float y = p.bottom - 24 - button_h;
        if (item == MetaItem::restore) return {p.left + 24, y, p.left + 24 + 230, y + button_h};
        if (item == MetaItem::identify) return {p.left + 24 + 230 + 12, y, p.left + 24 + 230 + 12 + 132, y + button_h};
        if (item == MetaItem::cancel) return {p.right - 24 - 116, y, p.right - 24, y + button_h};
        return {p.right - 24 - 116 - 12 - 132, y, p.right - 24 - 116 - 12, y + button_h}; // Zapisz.
    }
    int hit(float x, float y, bool identify = false) const {
        if (close().contains(x, y)) return static_cast<int>(MetaItem::close);
        for (int i = 0; i < meta_fields; ++i) if (field(i).contains(x, y)) return i;
        for (const auto item : {MetaItem::save, MetaItem::restore, MetaItem::cancel}) if (button(item).contains(x, y)) return static_cast<int>(item);
        if (identify && button(MetaItem::identify).contains(x, y)) return static_cast<int>(MetaItem::identify);
        return -1;
    }
};

struct View {
    const std::vector<Entry>* entries = nullptr;
    bool companies = false; // Firmy: the publishers' group holds both, and is named so.
    std::vector<Row> rows;
    std::array<bool, group_count> collapsed{};
    std::array<float, group_count> collapse_progress{}; // 0 open … 1 folded: the heading chevron turns.
    int active = -1;          // Entry shown.
    std::optional<float> active_y; // Gliding list-space top of the active entry's indicator.
    int hover_row = -1, focus_row = -1, drop_row = -1;
    int drop_flash_entry = -1; // Entry that just received a drop…
    float drop_flash = 0;      // …and its 1 → 0 confirmation pulse.
    int reorder_row = -1;     // Collection being dragged within the sidebar…
    float reorder_y = -1;     // …and the line where it would land.
    float sidebar_scroll = 0;
    bool refreshing = false;  // Biblioteka's refresh arrows spin while libraries rescan.
    float refresh_angle = 0;  // Radians.
    std::vector<Tool> tools;  // Left to right.
    std::wstring title;
    std::wstring title_mark; // A studio's section: its logo (a platform badge) in place of the title.
    size_t count = 0;
    std::wstring query;
    size_t caret = 0;
    bool search_focus = false, caret_on = false;
    std::wstring sort_label;
    bool installed_only = false;
    float poster = 156;
    float scroll = 0, scrollbar = 0; // Scrollbar visibility 0..1.
    Hit hover, pressed;
    bool max_hover = false;   // The maximize button is hovered as non-client (snap layouts).
    std::vector<float> card_hover; // Per card, 0..1.
    QuickView quick; // Ctrl+K, over everything.
    MetaView meta;   // Edytuj metadane.
    bool platform_badges = false;  // Ustawienia: the store's or console's mark top right on each poster.
    bool gray_uninstalled = false; // Ustawienia: posters of games not installed without colour.
    std::vector<char> marked;
    std::vector<float> card_mark; // Per card, 1 → 0 pop after toggling mark.
    size_t marked_count = 0;
    bool keyboard = false;    // Keys moved the focus: rings show (focus-visible).
    int zone = 1;             // 0 sidebar, 1 grid, 2 pane, 3 search.
    int pane_focus = -1;
    Pane pane;
    float pane_scroll = 0;
    bool maximized = false, active_window = true;
    int downloads_moving = 0, downloads_total = 0;
    float downloads_fraction = -1;
    float update_progress = -1; // Tool::update: the package's share while it downloads; -1 otherwise.
    bool downloads_open = false;
    std::vector<downloads_ui::Row> download_rows;
    std::vector<std::wstring> download_names;
    std::vector<std::wstring> download_actions; // Each row's button: Anuluj (Launchme's own install) or Otwórz.
    bool downloads_clearable = false;          // Wyczyść listę: something is not moving.
    const Menu* menu = nullptr;
    std::wstring tooltip;
    Rect tooltip_anchor{};
    float tooltip_alpha = 0;
    bool dragging = false, drag_valid = false;
    float drag_x = 0, drag_y = 0;
    int drag_card = -1;
    size_t drag_count = 0;
    std::wstring drag_label;
    bool scanning = false;
    bool library_empty = false; // No games at all: the pane says so instead of "Wybierz grę".
    EmptyState empty;
    float launch = 0;
    bool companion_card = false;   // Her card at the sidebar's foot (Layout::companion_card).
    float sidebar_narrow = 0;      // 0 wide … 1 narrow (icons only): the toggle's animation (Layout::narrow).
    float pane_width = 0;          // The pane's width the user dragged it to; 0 automatic (Layout::pane_width).
    bool companion_bubble = false; // Her line pops out of her card.
};

// ---- The window strip over the TV screens ----------------------------------------------------
// Settings, Glance, Szczegóły… keep their TV look inside the desktop window; its buttons (minimize,
// maximize, close) stay in the top-right corner and a slim band above the screens drags the window.
// Client pixels; `dpi` = the window's DPI / 96.
inline constexpr float strip_button = 46, strip_height = 32, strip_caption = 24;
inline Rect stripButton(int i, float client_width, float dpi) {
    const float w = strip_button * dpi, right = client_width - static_cast<float>(2 - i) * w;
    return {right - w, 0, right, strip_height * dpi};
}

// ---- Geometry --------------------------------------------------------------------------------

inline constexpr float title_h = 48, grid_pad = 28, caption_h = 44, gap_x = 20, gap_y = 18;
inline constexpr float logo_height = 18; // launchme's logo in the window's top bar (Renderer::drawLaunchmeLogo).
inline constexpr float poster_min = 112, poster_max = 240, poster_default = 156;
inline constexpr float tool_size = 36, tool_gap = 2, window_button = 46;
// The narrow sidebar (its icons only), the narrowest pane and the narrowest the grid may get by it.
inline constexpr float sidebar_narrow = 60, pane_min = 300, main_min = 540, toggle_size = 32; // main_min: the header's controls still fit.
inline constexpr float popover_width = 392, popover_row = 64, popover_head = 50, popover_foot = 50;
inline constexpr int popover_rows = 5;
inline float posterFromLevel(float level) { return poster_min + std::clamp(level, 0.0f, 1.0f) * (poster_max - poster_min); }
inline float levelFromPoster(float poster) { return std::clamp((poster - poster_min) / (poster_max - poster_min), 0.0f, 1.0f); }

struct Layout {
    float width = 1280, height = 800;
    float poster_width = poster_default; // The narrowest a poster may be; each row stretches its posters.
    int tools = 0;
    bool companion_card = false;
    float narrow = 0;     // 0 wide … 1 narrow sidebar, animated between them.
    float pane_width = 0; // Dragged by its left edge; 0: a quarter of the window.
    bool favorite = true; // The pane's star beside Graj (none while Ulubione is switched off).

    float sidebarWide() const { return std::clamp(std::round(width * 0.17f), 228.0f, 272.0f); }
    float sidebarRight() const { return std::round(sidebarWide() + (sidebar_narrow - sidebarWide()) * std::clamp(narrow, 0.0f, 1.0f)); }
    float paneWidth() const {
        const float automatic = std::clamp(std::round(width * 0.25f), 340.0f, 420.0f);
        if (pane_width <= 0) return automatic;
        return std::clamp(std::round(pane_width), pane_min, std::max(pane_min, width - sidebarRight() - main_min));
    }
    float paneLeft() const { return width - paneWidth(); }
    // The grab zone of the pane's left edge (the pointer turns into ↔).
    Rect paneEdge() const { return {paneLeft() - 4, title_h, paneLeft() + 4, height}; }
    // The sidebar's toggle, then the logo: whole while it is wide, its mark alone when narrow.
    Rect sidebarToggle() const { const float top = (title_h - toggle_size) * 0.5f; return {12, top, 12 + toggle_size, top + toggle_size}; }
    float logoLeft() const { return sidebarToggle().right + 12; }
    Rect titlebar() const { return {0, 0, width, title_h}; }
    Rect sidebar() const { return {0, title_h, sidebarRight(), height}; }
    Rect main() const { return {sidebarRight(), title_h, paneLeft(), height}; }
    Rect pane() const { return {paneLeft(), title_h, width, height}; }

    // Minimize, maximize/restore, close: the full height of the top bar.
    Rect windowButton(int i) const {
        const float right = width - static_cast<float>(2 - i) * window_button;
        return {right - window_button, 0, right, title_h};
    }
    // The shortcuts, `i` from the left of `tools`.
    Rect tool(int i) const {
        const float right = width - 3 * window_button - 10 - static_cast<float>(tools - 1 - i) * (tool_size + tool_gap);
        const float top = (title_h - tool_size) * 0.5f;
        return {right - tool_size, top, right, top + tool_size};
    }
    float toolsLeft() const { return tools ? tool(0).left : width - 3 * window_button - 10; }
    Rect brand() const { return {0, 0, sidebarRight(), title_h}; }
    Rect search() const {
        const auto m = main();
        const float w = std::min(480.0f, m.right - m.left - 48);
        float left = (m.left + m.right - w) * 0.5f, right = left + w;
        const float limit = toolsLeft() - 16;
        if (right > limit) { right = limit; left = std::max(m.left + 20, right - w); }
        return {left, 7, std::max(left + 120, right), title_h - 7};
    }
    Rect searchClear() const { const auto s = search(); return {s.right - 32, s.top + 4, s.right - 4, s.bottom - 4}; }

    // The section's heading with its controls; narrow, they move to a second line.
    float mainWidth() const { return paneLeft() - sidebarRight(); }
    bool compactHeader() const { return mainWidth() < 700; }
    float headerHeight() const { return compactHeader() ? 112 : 76; }
    float headerTitleY() const { return title_h + 38; }
    float controlsY() const { return compactHeader() ? title_h + 82 : headerTitleY(); }
    Rect sizeSlider() const { const float r = main().right - grid_pad, y = controlsY(); return {r - 104, y - 14, r, y + 14}; }
    Rect installedChip() const { const auto s = sizeSlider(); return {s.left - 18 - 136, s.top - 2, s.left - 18, s.bottom + 2}; }
    Rect sortButton() const { const auto c = installedChip(); return {c.left - 8 - 176, c.top, c.left - 8, c.bottom}; }
    float titleRight() const { return compactHeader() ? main().right - grid_pad : sortButton().left - 16; }

    // The poster grid: columns around `poster` wide (down to 88% of it), stretched to the row; a
    // strict minimum left narrow windows with a few oversized posters.
    float gridTop() const { return title_h + headerHeight(); }
    float gridLeft() const { return main().left + grid_pad; }
    float gridRight() const { return main().right - grid_pad; }
    int columns() const {
        const float w = gridRight() - gridLeft();
        return std::max(1, static_cast<int>(std::floor((w + gap_x) / (poster_width * 0.88f + gap_x))));
    }
    float cellWidth() const { const int c = columns(); return (gridRight() - gridLeft() - gap_x * (c - 1)) / c; }
    float posterHeight() const { return std::round(cellWidth() * 1.5f); }
    float rowStep() const { return posterHeight() + caption_h + gap_y; }
    Rect poster(size_t index, float scroll) const {
        const auto c = static_cast<size_t>(columns());
        const float x = gridLeft() + static_cast<float>(index % c) * (cellWidth() + gap_x);
        const float y = gridTop() + 8 + static_cast<float>(index / c) * rowStep() - scroll;
        return {x, y, x + cellWidth(), y + posterHeight()};
    }
    Rect cell(size_t index, float scroll) const { auto r = poster(index, scroll); r.bottom += caption_h; return r; }
    float contentHeight(size_t count) const {
        const size_t rows = (count + columns() - 1) / columns();
        return rows ? 8 + rows * rowStep() - gap_y + 32 : 0;
    }
    float viewHeight() const { return height - gridTop(); }
    float maxScroll(size_t count) const { return std::max(0.0f, contentHeight(count) - viewHeight()); }
    std::pair<size_t, size_t> visibleRange(size_t count, float scroll, size_t extra_rows = 1) const {
        const auto c = static_cast<size_t>(columns());
        const float step = rowStep();
        const auto first_row = static_cast<size_t>(std::max(0.0f, std::floor((scroll - 8) / step)));
        const auto last_row = static_cast<size_t>(std::max(0.0f, std::floor((scroll + viewHeight()) / step)));
        const size_t first = (first_row > extra_rows ? first_row - extra_rows : 0) * c;
        const size_t end = std::min(count, (last_row + 1 + extra_rows) * c);
        return {std::min(first, count), end};
    }
    int hitCard(size_t count, float scroll, float x, float y) const {
        const auto m = main();
        if (x < gridLeft() || x >= gridRight() || y < gridTop() || y >= m.bottom) return -1;
        const auto c = static_cast<size_t>(columns());
        const auto column = static_cast<size_t>((x - gridLeft()) / (cellWidth() + gap_x));
        const float local = y - gridTop() - 8 + scroll;
        if (local < 0 || column >= c) return -1;
        const auto index = static_cast<size_t>(local / rowStep()) * c + column;
        return index < count && cell(index, scroll).contains(x, y) ? static_cast<int>(index) : -1;
    }
    static Rect playButton(Rect p) {
        const float s = std::clamp((p.right - p.left) * 0.26f, 34.0f, 46.0f);
        return {p.right - s - 10, p.bottom - s - 10, p.right - 10, p.bottom - 10};
    }
    static Rect checkBox(Rect p) { return {p.left + 9, p.top + 9, p.left + 33, p.top + 33}; }
    float reveal(size_t index, size_t count, float scroll) const {
        const auto r = cell(index, scroll);
        if (r.top < gridTop() + 4) scroll -= gridTop() + 4 - r.top;
        else if (r.bottom > height - 12) scroll += r.bottom - (height - 12);
        return std::clamp(scroll, 0.0f, maxScroll(count));
    }
    Rect scrollTrack() const { const auto m = main(); return {m.right - 12, gridTop() + 6, m.right - 3, height - 6}; }
    Rect scrollThumb(size_t count, float scroll) const {
        const auto track = scrollTrack();
        const float content = contentHeight(count), view = viewHeight();
        if (content <= view + 1) return {};
        const float length = std::max(36.0f, (track.bottom - track.top) * view / content);
        const float top = track.top + (track.bottom - track.top - length) * (scroll / std::max(1.0f, maxScroll(count)));
        return {track.left, top, track.right, top + length};
    }
    float scrollFromThumb(size_t count, float thumb_top) const {
        const auto track = scrollTrack();
        const auto thumb = scrollThumb(count, 0);
        const float travel = track.bottom - track.top - (thumb.bottom - thumb.top);
        return travel > 0 ? std::clamp((thumb_top - track.top) / travel, 0.0f, 1.0f) * maxScroll(count) : 0;
    }

    // Sidebar: its rows scroll above her card.
    float sidebarTop() const { return title_h + 8; }
    float sidebarBottom() const { return height - (companion_card ? companion_h + 20 : 8); }
    Rect sidebarRow(const Row& row, float scroll) const {
        const float y = sidebarTop() + row.top - scroll;
        return {10, y, sidebarRight() - 10, y + row.height};
    }
    Rect headingAdd(const Row& row, float scroll) const {
        const auto r = sidebarRow(row, scroll);
        return {r.right - 28, r.top + 2, r.right - 2, r.bottom - 2};
    }
    // Biblioteka's heading: the refresh button left of its "+".
    Rect headingRefresh(const Row& row, float scroll) const {
        const auto add = headingAdd(row, scroll);
        return {add.left - 28, add.top, add.left - 2, add.bottom};
    }
    float sidebarMaxScroll(const std::vector<Row>& rows) const { return std::max(0.0f, rowsHeight(rows) - (sidebarBottom() - sidebarTop())); }
    int hitRow(const std::vector<Row>& rows, float scroll, float x, float y) const {
        if (x < 0 || x >= sidebarRight() || y < sidebarTop() || y >= sidebarBottom()) return -1;
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) if (sidebarRow(rows[i], scroll).contains(x, y)) return i;
        return -1;
    }
    Rect companionCard() const { return {12, height - companion_h - 12, sidebarRight() - 12, height - 12}; }
    Rect companionMore() const { const auto r = companionCard(); const float y = (r.top + r.bottom) * 0.5f; return {r.right - 38, y - 16, r.right - 6, y + 16}; }

    // The right pane, scrolled as one page: its picture, name, buttons, numbers and quick actions
    // have fixed places; collections, description and facts follow (measured while drawing).
    float bannerHeight() const { return std::clamp(std::round(paneWidth() * 0.52f), 170.0f, 220.0f); }
    float paneTop(float scroll) const { return title_h - scroll; }
    Rect banner(float scroll) const { const float t = paneTop(scroll); return {paneLeft(), t, width, t + bannerHeight()}; }
    float paneY(float offset, float scroll) const { return paneTop(scroll) + bannerHeight() + offset; }
    // Graj at most 320 wide (a dragged-wide pane stretched it over 600), the star and "…" beside it.
    Rect panePrimary(float scroll) const {
        const float y = paneY(74, scroll), left = paneLeft() + 20;
        return {left, y, left + std::min(320.0f, width - 20 - 2 * 48 - left), y + 42};
    }
    Rect paneFavorite(float scroll) const { const auto p = panePrimary(scroll); return {p.right + 6, p.top, p.right + 48, p.bottom}; }
    Rect paneMore(float scroll) const { const auto f = favorite ? paneFavorite(scroll) : panePrimary(scroll); return {f.right + 6, f.top, f.right + 48, f.bottom}; }
    Rect paneStats(float scroll) const { const float y = paneY(132, scroll); return {paneLeft() + 20, y, width - 20, y + 66}; }
    static constexpr int quick_count = 4; // Szczegóły, Grafika, Folder, Ukryj.
    Rect paneQuick(int i, float scroll) const {
        const float y = paneY(214, scroll), left = paneLeft() + 20, w = (width - 20 - left - 3 * 8) / quick_count;
        const float x = left + static_cast<float>(i) * (w + 8);
        return {x, y, x + w, y + 54};
    }
    float paneContentTop(float scroll) const { return paneY(288, scroll); }
    // Several games marked: their actions in a column.
    static constexpr int bulk_count = 5; // Dodaj do kolekcji, Ulubione, Pobierz grafiki ponownie, Ukryj, Wyczyść zaznaczenie.
    Rect paneBulk(int i) const { const float y = title_h + 132 + static_cast<float>(i) * 50; return {paneLeft() + 20, y, width - 20, y + 42}; }

    // Pobierania under its shortcut.
    Rect popover(int rows, Rect anchor) const {
        const float h = popover_head + std::max(1, std::min(rows, popover_rows)) * popover_row + popover_foot;
        const float right = std::min(width - 8, anchor.right + 6), left = std::max(8.0f, right - popover_width);
        return {left, anchor.bottom + 6, right, anchor.bottom + 6 + h};
    }
    // Wyczyść listę in the head; a row's own button (Anuluj, Otwórz) at its right, while hovered.
    Rect popoverClear(int rows, Rect anchor) const {
        const auto p = popover(rows, anchor);
        return {p.right - 14 - 116, p.top + 11, p.right - 14, p.top + popover_head - 11};
    }
    static Rect popoverAction(Rect row) { return {row.right - 12 - 88, row.top + 17, row.right - 12, row.bottom - 17}; }
    Rect popoverRow(int i, int rows, Rect anchor) const {
        const auto p = popover(rows, anchor);
        const float y = p.top + popover_head + static_cast<float>(i) * popover_row;
        return {p.left + 6, y, p.right - 6, y + popover_row};
    }
    Rect popoverAll(int rows, Rect anchor) const {
        const auto p = popover(rows, anchor);
        return {p.left + 12, p.bottom - popover_foot + 8, p.right - 12, p.bottom - 10};
    }
    // The empty grid's message line (its icon sits 40 px above it): the whole group, from the icon's
    // circle to the buttons or the description, is centred in the view.
    float emptyMessageY(bool buttons, bool body) const {
        return gridTop() + std::max(150.0f, viewHeight() * 0.5f + (buttons ? -34.0f : body ? 0.0f : 24.0f));
    }
    // The empty grid's buttons, centred under its message: below room for the description's three
    // lines (it starts 32 px under the message line), so a long one never runs into them.
    Rect emptyButton(int i, bool two) const {
        const auto m = main();
        const float cx = (m.left + m.right) * 0.5f, y = emptyMessageY(true, true) + 102;
        const float w = 190;
        const float left = two ? (i == 0 ? cx - w - 6 : cx + 6) : cx - w * 0.5f;
        return {left, y, left + w, y + 38};
    }
};
} // namespace desktop_ui
