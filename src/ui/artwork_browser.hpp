#pragma once
#include "settings_view.hpp"
#include "../artwork/artwork_sources.hpp"
#include <algorithm>
#include <array>
#include <unordered_set>

namespace artwork_ui {
// Rail order. Poster (600x900) and wide tile (920x430 / 460x215) are the Flix rows' artwork
// (and the screensaver's boxes); cover is the square card image.
inline constexpr std::array<ArtworkKind, 6> kinds{ArtworkKind::cover, ArtworkKind::hero, ArtworkKind::logo,
                                                   ArtworkKind::poster, ArtworkKind::landscape, ArtworkKind::disc};
inline constexpr std::array<const wchar_t*, 6> names{trKey(L"Kwadrat"), trKey(L"Tło"), trKey(L"Logo"),
                                                      trKey(L"Plakat"), trKey(L"Kafel poziomy"), trKey(L"Płyta")};
// The wallpaper picker (Interfejs → Tapeta) in the same screen: pictures, then videos.
inline constexpr std::array<const wchar_t*, 2> wallpaper_names{trKey(L"Zdjęcia"), trKey(L"Animowane")};
inline const wchar_t* providerName(ArtworkProvider provider) { return artwork_sources::name(provider); }
inline int category(ArtworkKind kind) {
    for (int i = 0; i < static_cast<int>(kinds.size()); ++i) if (kinds[i] == kind) return i;
    return 0;
}
struct Layout;
struct State {
    Game game;
    int category = 0, page = 0;
    size_t selected = 0;
    float scroll = 0;
    bool gallery = false, loading = false, applying = false, more = false;
    uint32_t applied_id = 0;
    std::wstring status;
    GridSource source;
    std::vector<GridAsset> assets;
    std::unordered_set<uint32_t> requested;
    std::wstring query;
    bool search_focus = false, query_selected = false, results_mode = false;
    std::vector<GridGame> games;
    GridSource override_source;
    // "Z dysku…" under the rail, then its panel: the artwork type (local_kind) and "Wskaż plik…".
    bool local_focus = false, local_open = false;
    int local_kind = 0;
    int local_row = 0; // Focused panel row: a type (0..kinds-1) or local_pick_row.
    static constexpr int local_pick_row = static_cast<int>(kinds.size());
    // "Źródło: …" right of the search: the sources of this game switched on in Media i dane →
    // Źródła grafik (choices), or the wallpaper sources of the type in view.
    // Right from the field focuses it; A opens its list (provider_row), A again picks one.
    ArtworkProvider provider = ArtworkProvider::steamgriddb;
    std::vector<ArtworkProvider> choices;
    bool provider_focus = false, provider_open = false;
    int provider_row = 0;
    // "Kadruj" right of "Szukaj" (square artwork and backgrounds); Back is the top-left circle.
    bool crop_focus = false, back_focus = false;
    // Interfejs → Tapeta: the same screen picks the wallpaper; its rail holds Zdjęcia and Animowane.
    bool wallpaper = false;
    // The rail has the focus on one of its buttons rather than on a type.
    bool railButton() const { return local_focus || back_focus; }
    // Neither the gallery nor the search row (field, "Kadruj" or "Źródło") has the focus: the rail has it.
    bool railFocus() const { return !gallery && !search_focus && !provider_focus && !crop_focus; }
    bool emulated() const { return game.kind == GameKind::nonsteam && game.platform != L"PC"; }
    int railCount() const { return wallpaper ? static_cast<int>(wallpaper_names.size()) : static_cast<int>(kinds.size()); }
    const wchar_t* railName(int index) const {
        return wallpaper ? wallpaper_names[static_cast<size_t>(std::clamp(index, 0, 1))] : names[static_cast<size_t>(std::clamp(index, 0, 5))];
    }
    ArtworkKind kind() const { return wallpaper ? ArtworkKind::hero : kinds[static_cast<size_t>(std::clamp(category, 0, 5))]; }
    bool videoWallpapers() const { return wallpaper && category == 1; }
    bool providerChoice() const { return (wallpaper || kind() != ArtworkKind::disc) && choices.size() > 1; }
    // The gallery's source: Płyta always ScreenScraper; otherwise the chosen one while it is offered.
    ArtworkProvider activeProvider() const {
        if (!wallpaper && kind() == ArtworkKind::disc) return ArtworkProvider::screenscraper;
        if (std::find(choices.begin(), choices.end(), provider) != choices.end()) return provider;
        return choices.empty() ? ArtworkProvider::steamgriddb : choices.front();
    }
    // "Kadruj": the cover's square or the part of the background a screen shows (src/app/crop.cpp).
    bool croppable() const { return !wallpaper && (kind() == ArtworkKind::cover || kind() == ArtworkKind::hero); }
    Layout layout(float width, float height) const;
};
struct Layout {
    float width, height;
    ArtworkKind kind;
    bool providers = false; // The "Źródło" dropdown is shown (State::providerChoice()).
    int provider_count = 3; // Rows of its list (State::choices).
    int rail_count = static_cast<int>(kinds.size()); // Rail entries above "Z dysku…" (State::railCount()).
    bool crop = true;       // "Kadruj" is shown (State::croppable()); off in the wallpaper picker.
    static constexpr float provider_width = 270, provider_gap = 12, provider_row_step = 54, crop_width = 150;
    bool cropping() const { return crop && (kind == ArtworkKind::cover || kind == ArtworkKind::hero); } // State::croppable().
    settings_ui::Layout base() const { return {width, height}; }
    float left() const { return base().contentLeft(); }
    float right() const { return base().contentRight(); }
    static constexpr float top = 260, gap = 18;
    // The search field starts level with the first rail tab.
    // Then "Szukaj", "Kadruj" (covers and backgrounds) and, for emulated games, the "Źródło: …" dropdown at the row's right end.
    float searchRight() const {
        return right() - (providers ? provider_width + provider_gap : 0.0f) - (cropping() ? crop_width + provider_gap : 0.0f);
    }
    settings_ui::Rect cropButton() const { return {searchRight() + provider_gap, settings_ui::Layout::rail_top, searchRight() + provider_gap + crop_width, settings_ui::Layout::rail_top + 55}; }
    settings_ui::Rect searchBox() const { return {left(), settings_ui::Layout::rail_top, searchRight() - 100, settings_ui::Layout::rail_top + 55}; }
    settings_ui::Rect searchButton() const { return {searchRight() - 88, settings_ui::Layout::rail_top, searchRight(), settings_ui::Layout::rail_top + 55}; }
    settings_ui::Rect providerButton() const { return {right() - provider_width, settings_ui::Layout::rail_top, right(), settings_ui::Layout::rail_top + 55}; }
    // Its open list under it: one row per offered source (State::choices order).
    settings_ui::Rect providerList() const {
        const auto b = providerButton();
        return {b.left, b.bottom + 8, b.right, b.bottom + 8 + 12 + static_cast<float>(provider_count) * provider_row_step};
    }
    settings_ui::Rect providerRow(int index) const {
        const auto l = providerList();
        return {l.left + 6, l.top + 6 + index * provider_row_step, l.right - 6, l.top + 6 + index * provider_row_step + 48};
    }
    int hitProvider(float x, float y) const {
        for (int i = 0; i < provider_count; ++i) if (providerRow(i).contains(x, y)) return i;
        return -1;
    }
    // "Wyczyść" inside the field's right end, shown while it holds text.
    settings_ui::Rect clearButton() const { const auto s = searchBox(); return {s.right - 112, s.top + 9, s.right - 9, s.bottom - 9}; }
    settings_ui::Rect gameRow(size_t index, float scroll) const { return {left(), top + index * 58 - scroll, right(), top + index * 58 + 50 - scroll}; }
    float bottom() const { return height - 78; }
    int columns() const {
        switch (kind) {
        case ArtworkKind::cover: case ArtworkKind::disc: return 4;
        case ArtworkKind::hero: return 2;
        case ArtworkKind::poster: return 5;
        default: return 3; // Logo, wide tile.
        }
    }
    float cellWidth() const { return (right() - left() - (columns() - 1) * gap) / columns(); }
    float imageHeight() const {
        switch (kind) {
        case ArtworkKind::cover: case ArtworkKind::disc: return cellWidth();
        case ArtworkKind::poster: return cellWidth() * 1.5f;
        case ArtworkKind::landscape: return cellWidth() * 430.0f / 920.0f;
        default: return cellWidth() * 0.5625f;
        }
    }
    float step() const { return imageHeight() + 44; }
    settings_ui::Rect cell(size_t index, float scroll) const {
        const float x = left() + (index % columns()) * (cellWidth() + gap);
        const float y = top + (index / columns()) * step() - scroll;
        return {x, y, x + cellWidth(), y + imageHeight()};
    }
    int hit(float x, float y, size_t count, float scroll) const {
        if (y < top || y >= bottom()) return -1;
        for (size_t i = 0; i < count; ++i) if (cell(i, scroll).contains(x, y)) return static_cast<int>(i);
        return -1;
    }
    float maxScroll(size_t count) const {
        if (!count) return 0;
        return std::max(0.0f, ((count + columns() - 1) / columns()) * step() - 8 - (bottom() - top));
    }
    float ensureVisible(size_t selected, size_t count, float scroll) const {
        const auto r = cell(selected, scroll);
        if (r.top < top) scroll -= top - r.top;
        else if (r.bottom + 28 > bottom()) scroll += r.bottom + 28 - bottom();
        return std::clamp(scroll, 0.0f, maxScroll(count));
    }
    // Directly under the last type, a little more compact than the types: "Z dysku…".
    settings_ui::Rect railButton(int index) const {
        const auto first = base().rail(rail_count);
        const float y = first.top + index * 62;
        return {first.left, y, first.right, y + 52};
    }
    settings_ui::Rect localButton() const { return railButton(0); }
    // Back: the small glass circle with a dot at the top left, as on Szczegóły, Osiągnięcia and Kadr
    // (game_details_layout.hpp); its hit rectangle is the circle.
    settings_ui::Rect backButton() const { return {56, 40, 100, 84}; }
    settings_ui::Rect pageButton(bool next) const {
        const float center = (56 + base().railRight() - 22) * 0.5f, y = localButton().bottom + 24;
        return next ? settings_ui::Rect{center + 30, y, base().railRight() - 22, y + 48} :
                      settings_ui::Rect{56, y, center - 30, y + 48};
    }
    // Panel: the type list, then "Wskaż plik…".
    static constexpr float local_row_step = 54;
    settings_ui::Rect localPanel() const {
        const float w = std::min(620.0f, width - 96), h = 83 + static_cast<float>(kinds.size()) * local_row_step + 16 + 44 + 28;
        return {(width - w) * 0.5f, (height - h) * 0.5f, (width + w) * 0.5f, (height + h) * 0.5f};
    }
    settings_ui::Rect localChoice(int index) const {
        const auto p = localPanel();
        return {p.left + 18, p.top + 83 + index * local_row_step, p.right - 18, p.top + 133 + index * local_row_step};
    }
    settings_ui::Rect localPick() const {
        const auto p = localPanel();
        return {p.left + 18, p.bottom - 72, p.right - 18, p.bottom - 28};
    }
    // Panel row under the pointer: a type, State::local_pick_row, or -1.
    int hitLocal(float x, float y) const {
        for (int i = 0; i < static_cast<int>(kinds.size()); ++i) if (localChoice(i).contains(x, y)) return i;
        return localPick().contains(x, y) ? State::local_pick_row : -1;
    }
};
inline Layout State::layout(float width, float height) const {
    return {width, height, kind(), providerChoice(), static_cast<int>(choices.size()), railCount(), !wallpaper};
}
}
