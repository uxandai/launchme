#pragma once

#include "settings_view.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// Settings → Interfejs → Dostosuj wygląd: the theme studio. A full-screen layer: tabs along the top
// (LB / RB), the options of the open tab on the left (every choice is a picture, applied at once), the
// live preview on the right and the saved looks along the bottom. Pure state and geometry; the
// launcher's part is src/app/theme_studio*.cpp, the drawing src/ui/theme_studio_renderer.cpp.
namespace theme_ui {
using Rect = settings_ui::Rect;
enum class Tab { layout, wallpaper, tiles, colors, fonts, other, looks };
inline constexpr int tab_count = 7;
inline constexpr const wchar_t* tab_names[tab_count] = {trKey(L"Układ"), trKey(L"Tapeta"), trKey(L"Kafelki"), trKey(L"Kolory"),
                                                        trKey(L"Czcionki"), trKey(L"Inne"), trKey(L"Wyglądy")};

// cards: a strip of pictures, one chosen; toggle: a switch; slider: − track +; action: a button.
enum class Kind { cards, toggle, slider, action };
// How a card is drawn.
enum class Look { text, layout, size, swatch, shape, entrance, gap, motion, grid, font, hint, art, place, idle };

struct Card { std::wstring label; uint32_t color = 0; };
struct Option {
    settings_ui::Id id = settings_ui::Id::font;
    Kind kind = Kind::cards;
    Look look = Look::text;
    std::wstring title, note, value; // value: a slider's readout or an action's caption.
    std::vector<Card> cards;
    int current = 0;
    bool on = false;
    float level = 0; // A slider, 0..1.
    float on_shown = 0, level_shown = 0; // What is drawn: they glide to on and level.
    bool trash = false; // An action with a bin beside it (Tapeta: take the wallpaper off).
    float strip_shown = 0; // A strip of cards wider than the column: its scroll as drawn.
};

struct Hit {
    enum Kind { none, close, tab, option, card, toggle, minus, plus, track, action, trash, revert, save, chip, chip_delete, name_ok, name_cancel, name_field };
    Kind kind = none;
    int index = 0; // The tab, option or chip.
    int slot = 0;   // card: which card of the option.
    float level = 0; // track: where on it.
    bool operator==(const Hit& other) const { return kind == other.kind && index == other.index && slot == other.slot; }
};

struct State {
    bool open = false;
    int tab = 0;
    int row = 0;            // The focused option of the tab.
    int card = 0;           // Its focused card (the chosen one while moving).
    bool bar = false;       // The Wyglądy tab: its buttons and the saved looks have the focus (no options there).
    int item = 0;           // Wyglądy: 0 undo, 1 save, 2.. the saved looks.
    float scroll = 0;
    std::vector<Option> options;
    std::vector<std::wstring> saved;
    int active = -1;        // The saved look the current one equals.
    int layout = 0;         // The home layout in use (the preview's scene).
    bool naming = false;
    std::wstring name;
    bool delete_armed = false;
    std::wstring status;
    Hit hover;
    Hit pressed;
    bool dragging = false;
    bool from_settings = false; // Opened from Ustawienia: closing goes back there.
    bool resume = false;        // Away picking a wallpaper: the studio comes back with the home.
    float appear = 0;           // The opening, 0..1.
    float tab_pos = 0;          // The tab pill, gliding.
    float tab_fade = 1;         // The options of a new tab arriving.
    float shown_scroll = 0;     // The scroll as drawn.
    int example = 0;            // What the preview shows: 0 the home, 1 another row, 2 the library, 3 the end of the first row.
    float scene_in = 1;         // The preview coming back from black after the home was built again.
    // The preview is not navigated by the user: after a few quiet seconds it shows itself off.
    float idle = 0, demo_clock = 0;
    bool demoing = false;
    size_t demo_origin = 0;     // Where the home's focus was before the demo.
    int demo_dir = 1;
    // Which tab each option came from, for the preview's emphasis (e.g. fonts show the text sample larger).
    Tab current() const { return static_cast<Tab>(std::clamp(tab, 0, tab_count - 1)); }
    static constexpr int undo_item = 0, save_item = 1, first_saved = 2;
    int items() const { return first_saved + static_cast<int>(saved.size()); }
};

struct Layout {
    float width, height;
    static constexpr float side = 56, inner = 14, title_h = 30, option_gap = 18, card_gap = 10, toggle_h = 62, slider_h = 66, action_h = 62;

    // The heading as on the other screens: the back dot at the top left, the title beside it, the tabs centred.
    Rect backGlass() const { return {56, 86, 92, 122}; }
    Rect close() const { return {50, 80, 98, 128}; }
    Rect title() const { return {108, 84, 108 + 330, 128}; }
    // The tabs sit at the right (the title has the left), with the RB cap at the right margin.
    float tabWidth() const { return std::clamp((width - 700) / tab_count, 80.0f, 108.0f); }
    float tabsLeft() const { return width - side - 52 - tabWidth() * tab_count; }
    Rect tab(int index) const { const float x = tabsLeft() + index * tabWidth(); return {x, 84, x + tabWidth(), 124}; }
    Rect capLeft() const { const auto t = tab(0); return {t.left - 52, t.top + 5, t.left - 10, t.bottom - 5}; }
    Rect capRight() const { const auto t = tab(tab_count - 1); return {t.right + 10, t.top + 5, t.right + 52, t.bottom - 5}; }
    float bodyTop() const { return 160; }
    float bodyBottom() const { return height - 36; }
    float leftWidth() const { return std::clamp(width * 0.40f, 460.0f, 700.0f); }
    // The options column: items sit `inner` inside the clip so their focus frames are not cut off.
    Rect listClip() const { return {side, bodyTop() - 8, side + leftWidth(), bodyBottom()}; }
    Rect list() const { const auto c = listClip(); return {c.left + inner, bodyTop(), c.right - inner, bodyBottom()}; }
    // The preview keeps the screen's own proportions, with the saved looks under it.
    Rect preview() const {
        const float left = side + leftWidth() + 40, right = width - side;
        float w = right - left, h = w * height / width;
        const float room = (bodyBottom() - bodyTop()) - 44; // A line for the status under it.
        if (h > room) { h = room; w = h * width / height; }
        return {right - w, bodyTop(), right, bodyTop() + h};
    }
    Rect statusLine() const { const auto p = preview(); return {p.left, p.bottom + 12, p.right, p.bottom + 40}; }

    // Cards: width and height by look; they wrap to more lines when the list is too narrow.
    static float cardWidth(Look look) {
        switch (look) {
        case Look::layout: return 140;
        case Look::swatch: return 56;
        case Look::font: return 140;
        default: return 140;
        }
    }
    static float cardHeight(Look look) {
        switch (look) {
        case Look::layout: return 100;
        case Look::swatch: return 56;
        case Look::font: return 84;
        default: return 88;
        }
    }
    // More cards than fit the column (not colours): one line, a card and a half peeking out (3.5 in view), scrolling.
    bool strip(const Option& option) const {
        const float w = list().right - list().left, base = cardWidth(option.look);
        return option.kind == Kind::cards && option.look != Look::swatch &&
               static_cast<float>(option.cards.size()) * (base + card_gap) - card_gap > w + 0.5f;
    }
    float cardW(const Option& option) const {
        return strip(option) ? (list().right - list().left - 3 * card_gap) / 3.5f : cardWidth(option.look);
    }
    float stripMax(const Option& option) const {
        const float w = list().right - list().left;
        return std::max(0.0f, static_cast<float>(option.cards.size()) * (cardW(option) + card_gap) - card_gap - w);
    }
    float stripTarget(const Option& option) const {
        const float w = list().right - list().left, cw = cardW(option);
        return std::clamp(static_cast<float>(option.current) * (cw + card_gap) + cw * 0.5f - w * 0.5f, 0.0f, stripMax(option));
    }
    int perLine(const Option& option) const {
        if (strip(option)) return std::max(1, static_cast<int>(option.cards.size()));
        const float w = list().right - list().left;
        return std::max(1, static_cast<int>((w + card_gap) / (cardW(option) + card_gap)));
    }
    float optionHeight(const Option& option) const {
        switch (option.kind) {
        case Kind::cards: {
            const int lines = (static_cast<int>(option.cards.size()) + perLine(option) - 1) / perLine(option);
            return title_h + 8 + lines * cardHeight(option.look) + (lines - 1) * card_gap;
        }
        case Kind::toggle: return toggle_h;
        case Kind::slider: return slider_h;
        default: return action_h;
        }
    }
    float optionTop(const std::vector<Option>& options, int index) const {
        float y = 0;
        for (int i = 0; i < index && i < static_cast<int>(options.size()); ++i) y += optionHeight(options[static_cast<size_t>(i)]) + option_gap;
        return y;
    }
    float total(const std::vector<Option>& options) const {
        return options.empty() ? 0.0f : optionTop(options, static_cast<int>(options.size())) - option_gap;
    }
    float maxScroll(const std::vector<Option>& options) const { return std::max(0.0f, total(options) - (list().bottom - list().top)); }
    Rect option(const std::vector<Option>& options, int index, float scroll) const {
        const auto l = list();
        const float y = l.top + optionTop(options, index) - scroll;
        return {l.left, y, l.right, y + optionHeight(options[static_cast<size_t>(index)])};
    }
    Rect card(const std::vector<Option>& options, int index, int card, float scroll) const {
        const auto& o = options[static_cast<size_t>(index)];
        const auto r = option(options, index, scroll);
        const int per = perLine(o);
        const float w = cardW(o), h = cardHeight(o.look);
        const float x = r.left + (card % per) * (w + card_gap) - (strip(o) ? std::clamp(o.strip_shown, 0.0f, stripMax(o)) : 0.0f), y = r.top + title_h + 8 + (card / per) * (h + card_gap);
        return {x, y, x + w, y + h};
    }
    // Toggle: the switch on the right; slider: − track + on the right half; action: the button.
    Rect toggleSwitch(const Rect& r) const { return {r.right - 76, (r.top + r.bottom) * 0.5f - 15, r.right - 12, (r.top + r.bottom) * 0.5f + 15}; }
    Rect minus(const Rect& r) const { const float y = (r.top + r.bottom) * 0.5f; return {r.right - 330, y - 18, r.right - 294, y + 18}; }
    Rect track(const Rect& r) const { const float y = (r.top + r.bottom) * 0.5f; return {r.right - 284, y - 14, r.right - 76, y + 14}; }
    Rect plus(const Rect& r) const { const float y = (r.top + r.bottom) * 0.5f; return {r.right - 66, y - 18, r.right - 30, y + 18}; }
    Rect trashButton(const Rect& r) const { const auto b = actionButton(r); return {b.left - 12 - 42, b.top, b.left - 12, b.bottom}; }
    Rect actionButton(const Rect& r) const { const float y = (r.top + r.bottom) * 0.5f; return {r.right - 214, y - 21, r.right - 12, y + 21}; }

    // The Wyglądy tab: Cofnij zmiany and Zapisz wygląd side by side, the saved looks as rows under them. It all
    // scrolls with `scroll`; while naming, the name field takes the buttons' place.
    static constexpr float chip_gap = 10, button_w = 110, row_h = 52;
    float lookTop(int item) const { return item < State::first_saved ? 0.0f : row_h + chip_gap + static_cast<float>(item - State::first_saved) * (row_h + chip_gap); }
    float looksTotal(const State& state) const { return state.saved.empty() ? row_h : lookTop(State::first_saved + static_cast<int>(state.saved.size()) - 1) + row_h; }
    Rect undo(float scroll) const { const auto l = list(); const float half = (l.right - l.left - chip_gap) * 0.5f, y = l.top - scroll; return {l.left, y, l.left + half, y + row_h}; }
    Rect saveButton(float scroll) const { const auto l = list(), u = undo(scroll); return {u.right + chip_gap, u.top, l.right, u.bottom}; }
    Rect chip(int saved_index, float scroll) const {
        const auto l = list(); const float y = l.top + lookTop(State::first_saved + saved_index) - scroll;
        return {l.left, y, l.right, y + row_h};
    }
    Rect chipDelete(const Rect& chip) const { return {chip.right - 46, chip.top + 10, chip.right - 12, chip.bottom - 10}; }
    // Naming a look: a field with OK and Cancel at its end.
    Rect nameField(float scroll) const { const auto l = list(), u = undo(scroll); return {l.left, u.top, l.right - 2 * (button_w + chip_gap), u.bottom}; }
    Rect nameOk(float scroll) const { const auto l = list(), u = undo(scroll); return {l.right - 2 * button_w - chip_gap, u.top, l.right - button_w - chip_gap, u.bottom}; }
    Rect nameCancel(float scroll) const { const auto l = list(), u = undo(scroll); return {l.right - button_w, u.top, l.right, u.bottom}; }
    // How far a tab scrolls: its options, or the Wyglądy rows.
    float scrollLimit(const State& state) const {
        if (state.current() == Tab::looks) return std::max(0.0f, looksTotal(state) - (list().bottom - list().top));
        return maxScroll(state.options);
    }

    Hit hit(const State& state, float x, float y) const {
        Hit h;
        if (close().contains(x, y)) { h.kind = Hit::close; return h; }
        for (int i = 0; i < tab_count; ++i) if (tab(i).contains(x, y)) { h.kind = Hit::tab; h.index = i; return h; }
        if (capLeft().contains(x, y)) { h.kind = Hit::tab; h.index = std::max(0, state.tab - 1); return h; }
        if (capRight().contains(x, y)) { h.kind = Hit::tab; h.index = std::min(tab_count - 1, state.tab + 1); return h; }
        if (state.current() == Tab::looks) {
            if (!list().contains(x, y)) return h;
            if (state.naming) {
                if (nameOk(state.scroll).contains(x, y)) h.kind = Hit::name_ok;
                else if (nameCancel(state.scroll).contains(x, y)) h.kind = Hit::name_cancel;
                else if (nameField(state.scroll).contains(x, y)) h.kind = Hit::name_field;
                return h;
            }
            if (undo(state.scroll).contains(x, y)) { h.kind = Hit::revert; return h; }
            if (saveButton(state.scroll).contains(x, y)) { h.kind = Hit::save; return h; }
            for (int i = 0; i < static_cast<int>(state.saved.size()); ++i) {
                const auto c = chip(i, state.scroll);
                if (!c.contains(x, y)) continue;
                h.kind = chipDelete(c).contains(x, y) ? Hit::chip_delete : Hit::chip; h.index = i;
                return h;
            }
            return h;
        }
        if (!list().contains(x, y)) return h;
        for (int i = 0; i < static_cast<int>(state.options.size()); ++i) {
            const auto& o = state.options[static_cast<size_t>(i)];
            const auto r = option(state.options, i, state.scroll);
            if (y < r.top - option_gap * 0.5f || y >= r.bottom + option_gap * 0.5f) continue;
            h.index = i;
            switch (o.kind) {
            case Kind::cards:
                for (int c = 0; c < static_cast<int>(o.cards.size()); ++c)
                    if (card(state.options, i, c, state.scroll).contains(x, y)) { h.kind = Hit::card; h.slot = c; return h; }
                h.kind = Hit::option; return h;
            case Kind::toggle: h.kind = toggleSwitch(r).contains(x - 8, y) || r.contains(x, y) ? Hit::toggle : Hit::option; return h;
            case Kind::slider: {
                const auto t = track(r);
                if (minus(r).contains(x, y)) h.kind = Hit::minus;
                else if (plus(r).contains(x, y)) h.kind = Hit::plus;
                else if (x >= t.left - 8 && x <= t.right + 8 && y >= t.top - 8 && y <= t.bottom + 8) {
                    h.kind = Hit::track; h.level = std::clamp((x - t.left) / (t.right - t.left), 0.0f, 1.0f);
                } else h.kind = Hit::option;
                return h;
            }
            default: h.kind = actionButton(r).contains(x, y) ? Hit::action : o.trash && trashButton(r).contains(x, y) ? Hit::trash : Hit::option; return h;
            }
        }
        return h;
    }
};

// A new name for a look: "Wygląd 1", "Wygląd 2"… the first not taken.
inline std::wstring freshName(const std::vector<std::wstring>& taken, const std::wstring& stem) {
    for (int n = 1;; ++n) {
        const auto candidate = stem + L" " + std::to_wstring(n);
        if (std::find(taken.begin(), taken.end(), candidate) == taken.end()) return candidate;
    }
}
} // namespace theme_ui
