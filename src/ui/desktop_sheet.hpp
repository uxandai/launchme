#pragma once
// Desktop mode's dialogs (src/app/desktop_settings.cpp, src/ui/desktop_settings_renderer.cpp): the
// screens Tryb TV shows full screen (Ustawienia, …) open in the desktop window as a compact sheet
// over the frosted backdrop, below the window's own top bar. The same state and actions as the TV
// screens (settings_ui::State, Launcher::settingsActivate…); only geometry, drawing and the mouse
// differ. Geometry here is shared by drawing and hit tests, in desktop DIPs.
#include "desktop_view.hpp"
#include "settings_view.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace desktop_ui {

inline constexpr float sheet_header = 56;

// The sheet: centred under the top bar, as large as the window allows up to its own size. Docked
// (Ustawienia: dock = the sidebar's right edge) it takes the library's place beside the sidebar.
struct Sheet {
    float width = 1280, height = 800;
    float max_width = 1160, max_height = 860;
    float dock = 0;
    Rect panel() const {
        if (dock > 0) return {dock, title_h, width, height};
        const float w = std::min(max_width, width - 48), h = std::min(max_height, height - title_h - 28);
        const float left = std::round((width - w) * 0.5f);
        const float top = std::round(title_h + std::max(12.0f, (height - title_h - h) * 0.5f));
        return {left, top, left + w, top + h};
    }
    Rect header() const { const auto p = panel(); return {p.left, p.top, p.right, p.top + sheet_header}; }
    Rect close() const { const auto p = panel(); return {p.right - 48, p.top + 10, p.right - 12, p.top + 46}; }
    Rect back() const { const auto p = panel(); return {p.left + 12, p.top + 10, p.left + 48, p.top + 46}; }
    // A small dialog inside the sheet (a key, a colour), centred.
    Rect dialog(float w, float h) const {
        const auto p = panel();
        const float cx = (p.left + p.right) * 0.5f, cy = (p.top + p.bottom) * 0.5f;
        w = std::min(w, p.right - p.left - 32); h = std::min(h, p.bottom - p.top - 32);
        return {std::round(cx - w * 0.5f), std::round(cy - h * 0.5f), std::round(cx + w * 0.5f), std::round(cy + h * 0.5f)};
    }
};

// What the pointer is over in a sheet. index: a row, category, option or tile; sub: within it.
struct SheetHit {
    enum class Kind {
        none, close, back, nav, row, field, toggle, track, action, cog, up, down, layout, avatar, tile, tile_action,
        dropdown, dropdown_item, dialog, dialog_field, dialog_save, dialog_cancel, dialog_browse, color_track, save_status,
        badge // A row's badge with Row::reset: back to the default.
    } kind = Kind::none;
    int index = -1, sub = -1;
    bool operator==(const SheetHit&) const = default;
};

// The frame's state of a sheet (ViewContext::desktop_sheet).
struct SheetView {
    float scroll = 0;
    std::optional<float> nav_glide; // Fractional category/tab index gliding on a spring.
    SheetHit hover, pressed;
    bool keyboard = false; // Keys or the pad moved the focus: its ring shows.
    float dropdown_scroll = 0;
    bool maximized = false, active_window = true;
    int window_hover = -1; // Minimize, maximize, close of the top bar.
    bool max_hover = false;
    // Szczegóły: what the page offers beyond ViewContext's details fields.
    std::wstring playtime, last_played;
    bool folder = false, hideable = false, hidden = false;
    // Pobierania: each row's button ("Anuluj" for the launcher's own installs, else "Otwórz") and
    // the rows "Wyczyść listę" put away.
    std::vector<std::wstring> download_actions;
    std::vector<char> download_hidden;
    bool store_loading = false; // Sklep: a search is under way.
    float dock = 0; // Ustawienia in the library's place: the sidebar's right edge (Sheet::dock).
};
// Pages measured while drawn (Szczegóły, Zmień grafikę): every control the renderer drew with the
// area that answers for it, topmost last (Renderer::desktopHotspots).
struct SheetHotspot { SheetHit hit; Rect rect; };

// Szczegóły's and Zmień grafikę's sheets (their content is measured while drawn).
inline Sheet detailsSheet(float width, float height) { Sheet sheet; sheet.width = width; sheet.height = height; sheet.max_width = 1040; sheet.max_height = 860; return sheet; }
inline Sheet artworkSheet(float width, float height) { Sheet sheet; sheet.width = width; sheet.height = height; sheet.max_width = 1160; sheet.max_height = 880; return sheet; }

// Sklep (the store's search, in Zmień grafikę's sheet): the field and its state line, then the
// results as posters with their names, scrolling below them.
struct StoreGrid {
    static constexpr float gap = 18, caption = 34;
    float left = 0, top = 0;
    int columns = 2;
    float poster_w = 0, poster_h = 0, row_h = 0;
    Rect poster(size_t index, float scroll) const {
        const float x = left + static_cast<float>(index % static_cast<size_t>(columns)) * (poster_w + gap);
        const float y = top + static_cast<float>(index / static_cast<size_t>(columns)) * row_h - scroll;
        return {x, y, x + poster_w, y + poster_h};
    }
    float height(size_t count) const { return count ? std::ceil(static_cast<float>(count) / static_cast<float>(columns)) * row_h + 8 : 0; }
};
inline StoreGrid storeGrid(const Sheet& sheet) {
    const auto p = sheet.panel();
    StoreGrid grid;
    grid.left = p.left + 24;
    grid.top = sheet.header().bottom + 92;
    const float width = p.right - p.left - 48;
    grid.columns = std::max(2, static_cast<int>((width + StoreGrid::gap) / (150 + StoreGrid::gap)));
    grid.poster_w = std::floor((width - StoreGrid::gap * static_cast<float>(grid.columns - 1)) / static_cast<float>(grid.columns));
    grid.poster_h = std::round(grid.poster_w * 1.5f);
    grid.row_h = grid.poster_h + StoreGrid::caption + StoreGrid::gap;
    return grid;
}
// Tryb TV's Urządzenia, kadrowanie and plugin pages inside a desktop sheet: its body below the header
// holds the screen's own layout on a compact canvas (Renderer::desktopEmbedViewport).
inline Sheet embedSheet(float width, float height) { Sheet sheet; sheet.width = width; sheet.height = height; sheet.max_width = 1240; sheet.max_height = 900; return sheet; }
inline Rect embedBody(const Sheet& sheet) { const auto p = sheet.panel(); return {p.left + 1, p.top + sheet_header, p.right - 1, p.bottom - 1}; }
// Extra hit indices of Szczegóły's buttons (SheetHit::Kind::tile, sub).
enum class DetailsExtra { artwork, folder, hide };
// Tryb TV's dialogs drawn as desktop dialogs over desktop pages (desktop_dialogs_renderer.cpp).
enum class DialogKind { none, collections, custom_editor, identify, power_menu, exit_prompt, gog_install, aurelia };

namespace settings_sheet {
using settings_ui::Control;
using settings_ui::Row;

inline constexpr float nav_width = 232, nav_row = 36, row_gap = 8, section_gap = 42, pad = 28;
inline constexpr float dropdown_row = 32, dropdown_width = 280;
inline constexpr int dropdown_visible = 10;
inline constexpr float pad_tile = 196, tile_gap = 12;

// Segoe Fluent / MDL2 glyphs of the categories.
inline wchar_t categoryGlyph(int category) {
    using settings_ui::Category;
    switch (static_cast<Category>(category)) {
    case Category::appearance: return 0xE771;
    case Category::library: return 0xE8F1;
#ifndef _WIN32
    case Category::compat: return 0xE923; // Windows on top of each other: Windows programs here.
#endif
    case Category::media: return 0xE8B9;
    case Category::audio: return 0xE767;
    case Category::controllers: return 0xE7FC;
    case Category::plugins: return 0xEA86;
    case Category::companion: return 0xE8F2;
    default: return 0xE770;
    }
}

// focused: a dense row (Row::dense) shows its description only then.
inline float rowHeight(const Row& row, bool compact, bool focused = true) {
    switch (row.control) {
    case Control::layout: return 200;
    case Control::avatar: return 292;
    case Control::sources: return 182;
    case Control::pads: return 216;
    default:
        if (compact) return 50;
        if (row.dense && !focused) return 46;
        if (row.lines > 2) return 74 + static_cast<float>(row.lines - 2) * 16; // A longer description (Row::lines).
        return row.description.empty() ? 56 : 74;
    }
}
inline bool tall(Control control) { return control == Control::layout || control == Control::avatar || settings_ui::tiled(control); }

struct Layout : Sheet {
    std::vector<float> tops, heights;
    std::vector<std::wstring> labels; // A heading above the row, or empty.
    float content_height = 0;
    bool import_help = false;
    // System's closing logo below its last row (list units, scrolls with the rows), -1 on other pages.
    float footer_top = -1;
    static constexpr float footer_gap = 40, footer_height = 72, footer_logo = 30;

    static Layout of(float width, float height, const settings_ui::State& state, const std::vector<Row>& rows, float dock = 0) {
        Layout layout;
        layout.width = width; layout.height = height; layout.dock = dock;
        layout.import_help = state.row() >= 0 && state.row() < static_cast<int>(rows.size()) && rows[state.row()].id == settings_ui::Id::companion_import_folder;
        const bool compact = state.at(settings_ui::Page::drm_games);
        float y = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
            std::wstring label = rows[i].section;
            if (label.empty() && state.pluginList() && i == 1) label = tr(L"Dostępne pluginy");
            if (!label.empty()) y += i ? section_gap : section_gap - 12;
            layout.labels.push_back(std::move(label));
            layout.tops.push_back(y);
            const float h = rowHeight(rows[i], compact, state.options && state.row() == static_cast<int>(i));
            layout.heights.push_back(h);
            // Dense rows sit closer together.
            y += h + (compact || (rows[i].dense && i + 1 < rows.size() && rows[i + 1].dense && rows[i + 1].section.empty()) ? 4 : row_gap);
        }
        layout.content_height = rows.empty() ? 0 : y + 8;
        if (!rows.empty() && state.category == static_cast<int>(settings_ui::Category::system) && !state.subpage()) {
            layout.footer_top = y + footer_gap;
            layout.content_height = layout.footer_top + footer_height;
        }
        return layout;
    }
    // The closing logo's centre line (System only; see footer_top).
    float footerY(float scroll) const { return listTop() + footer_top + footer_height * 0.5f - scroll; }

    float navRight() const { return panel().left + nav_width; }
    Rect navRow(int i) const {
        const auto p = panel();
        const float y = p.top + sheet_header + 8 + static_cast<float>(i) * (nav_row + 2);
        return {p.left + 12, y, navRight() - 12, y + nav_row};
    }
    int hitNav(float x, float y) const {
        for (int i = 0; i < static_cast<int>(settings_ui::categories.size()); ++i) if (navRow(i).contains(x, y)) return i;
        return -1;
    }
    float contentLeft() const { return navRight() + pad; }
    static constexpr float content_max = 940; // A docked page in a wide window keeps its rows readable.
    float contentRight() const { return std::min(panel().right - pad, contentLeft() + content_max); }
    float headingY() const { return panel().top + sheet_header + 24; }
    float listTop() const { return panel().top + sheet_header + 76; }
    float listBottom() const { return panel().bottom - 12; }
    float viewHeight() const { return listBottom() - listTop(); }
    float maxScroll() const { return std::max(0.0f, content_height - viewHeight()); }
    Rect row(int i, float scroll) const {
        const float y = listTop() + tops[static_cast<size_t>(i)] - scroll;
        return {contentLeft(), y, contentRight(), y + heights[static_cast<size_t>(i)]};
    }
    int count() const { return static_cast<int>(tops.size()); }
    int hitRow(float x, float y, float scroll) const {
        if (y < listTop() || y >= listBottom()) return -1;
        for (int i = 0; i < count(); ++i) if (row(i, scroll).contains(x, y)) return i;
        return -1;
    }
    float ensureVisible(int i, float scroll) const {
        if (i < 0 || i >= count()) return scroll;
        auto r = row(i, scroll);
        if (!labels[static_cast<size_t>(i)].empty()) r.top -= section_gap;
        if (i == count() - 1 && footer_top >= 0) r.bottom = listTop() + footer_top + footer_height - scroll; // The last row brings the logo.
        if (r.top < listTop()) scroll -= listTop() - r.top;
        else if (r.bottom > listBottom()) scroll += r.bottom - listBottom() + 8;
        return std::clamp(scroll, 0.0f, maxScroll());
    }
    // The line the controls sit on: a row's centre, a tall row's title line.
    float controlY(int i, float scroll) const {
        const auto r = row(i, scroll);
        return heights[static_cast<size_t>(i)] > 90 ? r.top + 30 : (r.top + r.bottom) * 0.5f;
    }
    float right() const { return contentRight() - 16; }
    Rect field(int i, float s) const { const float y = controlY(i, s); return {right() - 260, y - 17, right(), y + 17}; }
    Rect action(int i, float s) const { const float y = controlY(i, s); return {right() - 172, y - 17, right(), y + 17}; }
    Rect toggle(int i, float s, float right_edge) const { const float y = controlY(i, s); return {right_edge - 40, y - 11, right_edge, y + 11}; }
    Rect track(int i, float s) const { const float y = controlY(i, s); return {right() - 64 - 200, y - 10, right() - 64, y + 10}; }
    Rect cog(int i, float s) const { const float y = controlY(i, s); return {right() - 34, y - 17, right(), y + 17}; }
    // The switch of each kind of row, at its right edge.
    float toggleRight(const Row& item) const {
        if (item.control == Control::platform) return settings_ui::hasOptionsButton(item.id) ? cog(0, 0).left - 14 : action(0, 0).left - 16;
        return right();
    }
    // Where a row's controls begin: its text (and badge) end before them.
    float controlsLeft(const Row& item, int i, float s) const {
        switch (item.control) {
        case Control::choice: case Control::secret: case Control::text: return field(i, s).left;
        case Control::action: case Control::count: return action(i, s).left;
        case Control::page: case Control::fold: return action(i, s).left - 60;
        case Control::slider: return track(i, s).left - 16;
        case Control::toggle: case Control::platform: return toggleRight(item) - 40;
        default: return right() - 16;
        }
    }
    // A row's badge (Row::badge) before its controls; a button when the row has Row::reset.
    static constexpr float badge_width = 96;
    Rect badge(const Row& item, int i, float s) const {
        const float y = controlY(i, s), r = controlsLeft(item, i, s) - 14;
        return {r - badge_width, y - 12, r, y + 12};
    }
    // Home layout previews under the row's title: a slider of layout_visible at a time, `first` its
    // leftmost slot (settings_ui::layoutFirst of the chosen one); the band clips them.
    static constexpr float layout_visible = 5.5f;
    Rect layoutTile(int i, float s, int option, float first = 0) const {
        const auto r = row(i, s);
        const float pitch = (r.right - r.left - 32 + 12) / layout_visible;
        const float left = r.left + 16 + (static_cast<float>(option) - first) * pitch;
        return {left, r.top + 60, left + pitch - 12, r.bottom - 34};
    }
    Rect layoutBand(int i, float s) const { const auto r = row(i, s); return {r.left + 10, r.top + 52, r.right - 10, r.bottom - 4}; }
    // Companion cards: up to four at a time, `first` the leftmost.
    static constexpr int avatar_visible = 4;
    static int avatarFirst(int choice, int count) { return std::clamp(choice - 1, 0, std::max(0, count - avatar_visible)); }
    Rect avatarCard(int i, float s, int option, int count, int first) const {
        const auto r = row(i, s);
        const int visible = std::clamp(count, 1, avatar_visible), slot = option - first;
        if (slot < 0 || slot >= visible) return {};
        const float w = (r.right - r.left - 32 - 12.0f * static_cast<float>(visible - 1)) / static_cast<float>(visible);
        const float left = r.left + 16 + static_cast<float>(slot) * (w + 12);
        return {left, r.top + 76, left + w, r.bottom - 16};
    }
    // Sound sources (two halves) or the controller gallery (fixed tiles, the focused one in view).
    int visibleTiles(Control control, int count) const {
        if (control == Control::sources) return count;
        return std::max(1, static_cast<int>((contentRight() - contentLeft() - 32 + tile_gap) / (pad_tile + tile_gap)));
    }
    int firstTile(Control control, int count, int focus) const {
        const int visible = visibleTiles(control, count);
        return count <= visible ? 0 : std::clamp(focus - visible / 2, 0, count - visible);
    }
    Rect tile(int i, float s, Control control, int which, int count, int focus) const {
        const auto r = row(i, s);
        const float top = r.top + 56, bottom = r.bottom - 16;
        if (control == Control::sources) {
            const int n = std::max(1, count);
            const float w = (r.right - r.left - 32 - tile_gap * static_cast<float>(n - 1)) / static_cast<float>(n);
            const float left = r.left + 16 + static_cast<float>(which) * (w + tile_gap);
            return {left, top, left + w, bottom};
        }
        const float left = r.left + 16 + static_cast<float>(which - firstTile(control, count, focus)) * (pad_tile + tile_gap);
        return {left, top, left + pad_tile, bottom};
    }
    int hitTile(int i, float s, Control control, int count, int focus, float x, float y) const {
        const int first = firstTile(control, count, focus), end = std::min(count, first + visibleTiles(control, count));
        for (int t = first; t < end; ++t) if (tile(i, s, control, t, count, focus).contains(x, y)) return t;
        return -1;
    }
    static Rect tileAction(Rect t) { return {t.left + 12, t.bottom - 42, t.right - 12, t.bottom - 12}; }
    // A picker's list under (or above) the control that opened it.
    Rect dropdown(Rect anchor, int count) const {
        const int shown = std::clamp(count, 1, dropdown_visible);
        const float w = std::max(anchor.right - anchor.left, dropdown_width), h = 10 + dropdown_row * static_cast<float>(shown);
        const float left = std::max(panel().left + 8, anchor.right - w);
        float top = anchor.bottom + 6;
        if (top + h > height - 8) top = std::max(title_h + 4, anchor.top - 6 - h);
        return {left, top, left + w, top + h};
    }
    static int firstChoice(int count, int selected) {
        return count <= dropdown_visible || selected < 0 ? 0 : std::clamp(selected - dropdown_visible / 2, 0, count - dropdown_visible);
    }
    Rect dropdownItem(Rect list, int slot) const {
        const float y = list.top + 5 + static_cast<float>(slot) * dropdown_row;
        return {list.left + 5, y, list.right - 5, y + dropdown_row};
    }
    // The key / text dialog.
    Rect secretDialog() const { return import_help ? dialog(660, 460) : dialog(540, 224); }
    Rect secretField() const { const auto d = secretDialog(); return {d.left + 24, d.top + 92, d.right - 24, d.top + 130}; }
    Rect secretSave() const { const auto d = secretDialog(); return {d.right - 24 - 120, d.bottom - 24 - 36, d.right - 24, d.bottom - 24}; }
    Rect secretCancel() const { const auto s = secretSave(); return {s.left - 10 - 110, s.top, s.left - 10, s.bottom}; }
    Rect secretBrowse() const { const auto d = secretDialog(), s = secretSave(); return {d.left + 24, s.top, d.left + 184, s.bottom}; }
    // "Inny…" colour.
    Rect colorDialog() const { return dialog(620, 340); }
    Rect colorSwatch() const { const auto d = colorDialog(); return {d.left + 24, d.top + 72, d.left + 164, d.top + 212}; }
    Rect colorTrack(int slider) const {
        const auto d = colorDialog(); const float y = d.top + 96 + static_cast<float>(slider) * 52;
        return {d.left + 280, y - 9, d.right - 80, y + 9};
    }
    Rect colorSave() const { const auto d = colorDialog(); return {d.right - 24 - 120, d.bottom - 24 - 36, d.right - 24, d.bottom - 24}; }
    Rect colorCancel() const { const auto s = colorSave(); return {s.left - 10 - 110, s.top, s.left - 10, s.bottom}; }
    Rect saveStatus() const { const auto h = header(); return {h.right - 60 - 300, h.top + 12, h.right - 60, h.bottom - 12}; }
};
} // namespace settings_sheet
} // namespace desktop_ui
