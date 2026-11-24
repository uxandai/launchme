#pragma once
// "Identyfikuj grę" in Edytuj metadane (desktop window and Tryb TV, the same pane): one list of what the
// databases (RAWG; ScreenScraper for an emulated game) and the stores (src/library/metadata_sources.hpp)
// know of a title, best first, each with its picture. A database's game identifies the game (its details,
// rating and artwork: src/app/identify.cpp); a store's details go into the dialog's fields, as chosen.
// Pure state and geometry; the Launcher side is src/app/metadata_find.cpp, the drawing
// src/ui/meta_find_renderer.cpp. The panel is Edytuj metadane's own (dialogPanel).
#include "settings_view.hpp"
#include "../library/screenscraper.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace meta_find {
using settings_ui::Rect;

inline constexpr int take_count = 5;   // Opis, Producent, Wydawca, Data premiery, Gatunki.
inline constexpr int visible = 6;      // Results shown at once.
// The Edytuj metadane field each detail goes into (desktop_ui::MetaItem order).
inline constexpr std::array<int, take_count> target_field{1, 2, 3, 4, 5};

// What the pad and Tab walk through (one number each).
namespace item {
inline constexpr int query = 0, search = 1;
inline constexpr int result_first = 2;                    // …result_first + visible - 1
inline constexpr int take_first = result_first + visible; // …take_first + take_count - 1
inline constexpr int insert = take_first + take_count, back = insert + 1, close = back + 1;
} // namespace item

struct Result {
    enum class Kind { store, rawg, screenscraper };
    Kind kind = Kind::store;
    std::wstring title;
    std::wstring source;                // "RAWG", "Nintendo eShop", "Steam"…
    std::wstring note;                  // The year and platforms, the edition's console or store.
    std::array<std::wstring, take_count> values; // As they would be inserted (a store's).
    std::wstring image;                 // Its picture's address; `thumb` once downloaded.
    std::shared_ptr<const std::vector<uint8_t>> thumb;
    uint32_t rawg_id = 0;               // Kind::rawg.
    std::optional<screenscraper::GameInfo> info; // Kind::screenscraper: the game it identifies as.
    bool database() const { return kind != Kind::store; }
};

struct State {
    bool open = false;
    std::wstring query;
    size_t caret = 0;
    bool selected = false;              // Ctrl+A in the query.
    bool busy = false;                  // Sources still answering.
    int waiting = 0;                    // How many.
    std::wstring status;                // "Szukanie…", or why nothing was found.
    std::vector<Result> results;        // Best first.
    std::vector<int> scores;            // Parallel to results (ordering only).
    std::wstring seed;                  // The title the pane opened with: only a search for it uses the game's ids.
    int chosen = 0;                     // The highlighted result.
    std::array<bool, take_count> take{};
    int focus = item::query;
    int hover = -1;
    uint32_t generation = 0;            // A newer search drops the older answers.
    bool direct = false;                // Opened as "Identyfikuj grę" from the game's menu: Back closes the dialog too.
    bool databases = true;              // RAWG (and ScreenScraper) are asked: not for a Steam game, its AppID identifies it.
    bool emulated = false;              // ScreenScraper is asked too, when it is set up.
    bool applying = false;              // An identification on its way: the pane waits for it.
    std::vector<std::wstring> fetching; // Pictures asked for (each once).
    std::vector<std::wstring> errors;   // Why a source did not answer (shown when nothing was found).

    bool has(int index) const { return index >= 0 && index < static_cast<int>(results.size()); }
    const Result* current() const { return has(chosen) ? &results[static_cast<size_t>(chosen)] : nullptr; }
    bool canTake(int slot) const { const auto* r = current(); return r && !r->database() && !r->values[static_cast<size_t>(slot)].empty(); }
    bool anyTaken() const { for (int i = 0; i < take_count; ++i) if (take[static_cast<size_t>(i)] && canTake(i)) return true; return false; }
    // The primary button: Zidentyfikuj (a database's game) or Wstaw (a store's chosen details).
    bool canApply() const { const auto* r = current(); return r && !applying && (r->database() || anyTaken()); }
};

// Edytuj metadane's panel, the form's and this pane's: Tryb TV's as wide as the launch options' panel,
// the window's smaller.
inline Rect dialogPanel(float width, float height, bool tv) {
    const float w = tv ? std::min(1180.0f, width - 120) : std::min(940.0f, width - 48);
    const float h = tv ? std::min(780.0f, height - 40) : std::min(660.0f, height - 48);
    const float left = std::round((width - w) * 0.5f), top = std::round((height - h) * 0.5f);
    return {left, top, left + w, top + h};
}
// The measures both share: the margin, where the body starts under the heading, a field's label and
// line, the buttons at the bottom.
struct Metrics {
    float pad, head, label, line, button_h, button_w, bottom, gap;
};
inline Metrics metrics(bool tv) {
    return tv ? Metrics{40, 116, 26, 50, 52, 170, 32, 16} : Metrics{24, 84, 22, 38, 38, 132, 24, 14};
}

struct Layout {
    float width = 0, height = 0;
    bool tv = false;
    Rect panel() const { return dialogPanel(width, height, tv); }
    Metrics m() const { return metrics(tv); }
    float searchWidth() const { return tv ? 150.0f : 112.0f; }
    float takeHeight() const { return tv ? 40.0f : 32.0f; }
    // The chosen game on the right: its picture, title and what it gives.
    Rect detail() const {
        const auto p = panel(); const auto k = m();
        return {p.right - k.pad - (tv ? 340.0f : 290.0f), p.top + k.head, p.right - k.pad, buttonsTop() - 20};
    }
    float listRight() const { return detail().left - (tv ? 32.0f : 24.0f); }
    Rect field() const {
        const auto p = panel(); const auto k = m(); const float y = p.top + k.head + k.label;
        return {p.left + k.pad, y, listRight() - searchWidth() - 12, y + k.line};
    }
    Rect searchButton() const { const auto f = field(); return {listRight() - searchWidth(), f.top, listRight(), f.bottom}; }
    float buttonsTop() const { const auto k = m(); return panel().bottom - k.bottom - k.button_h; }
    float resultsTop() const { return field().bottom + (tv ? 18.0f : 14.0f); }
    float rowHeight() const { return std::min(tv ? 80.0f : 64.0f, (buttonsTop() - 20 - resultsTop()) / static_cast<float>(visible)); }
    Rect result(int slot) const {
        const auto p = panel(); const float y = resultsTop() + static_cast<float>(slot) * rowHeight();
        return {p.left + m().pad, y, listRight(), y + rowHeight() - 6};
    }
    // The row's picture, 16:9 at its start.
    Rect thumb(int slot) const {
        const auto r = result(slot); const float h = r.bottom - r.top - 12;
        return {r.left + 6, r.top + 6, r.left + 6 + std::round(h * 16 / 9), r.bottom - 6};
    }
    Rect picture() const { const auto d = detail(); return {d.left, d.top, d.right, d.top + std::round((d.right - d.left) * 9 / 16)}; }
    float infoTop() const { return picture().bottom + 14; }
    float takesTop() const { return infoTop() + (tv ? 64.0f : 52.0f) + (tv ? 26.0f : 22.0f); } // Under the "Pola do wstawienia" caption.
    Rect take(int slot) const {
        const auto d = detail(); const float y = takesTop() + static_cast<float>(slot) * takeHeight();
        return {d.left, y, d.right, y + takeHeight() - 4};
    }
    // Wstaw / Zidentyfikuj at the right end, Wróć before it (the launch options' places of Zapisz and Anuluj).
    Rect insertButton() const {
        const auto p = panel(); const auto k = m(); const float y = buttonsTop();
        return {p.right - k.pad - k.button_w, y, p.right - k.pad, y + k.button_h};
    }
    Rect backButton() const { const auto r = insertButton(); const float w = r.right - r.left; return {r.left - 10 - w, r.top, r.left - 10, r.bottom}; }
    Rect close() const { const auto p = panel(); return {p.right - 48, p.top + 10, p.right - 12, p.top + 46}; } // The window's only (desktopDialogSheet).
    // An Item, or -1.
    int hit(float x, float y, const State& state) const {
        if (!tv && close().contains(x, y)) return item::close;
        if (field().contains(x, y)) return item::query;
        if (searchButton().contains(x, y)) return item::search;
        for (int i = 0; i < visible; ++i) if (state.has(i) && result(i).contains(x, y)) return item::result_first + i;
        for (int i = 0; i < take_count; ++i) if (state.canTake(i) && take(i).contains(x, y)) return item::take_first + i;
        if (insertButton().contains(x, y)) return item::insert;
        if (backButton().contains(x, y)) return item::back;
        return -1;
    }
    // Tryb TV: where the focus ring stands (it springs between the items: ViewContext::meta_glide).
    std::optional<Rect> focusRect(const State& state) const {
        const int focus = state.focus;
        if (focus == item::query) return field();
        if (focus == item::search) return searchButton();
        if (focus >= item::result_first && focus < item::take_first) return result(focus - item::result_first);
        if (focus >= item::take_first && focus < item::insert) return take(focus - item::take_first);
        if (focus == item::insert) return insertButton();
        if (focus == item::back) return backButton();
        return std::nullopt;
    }
};
} // namespace meta_find
