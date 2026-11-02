// Flix → Pasek boczny (src/ui/flix_rail.hpp): the icons at the left edge. Left from a row's first
// tile (or a click) gives them the focus; Up/Down walk them, Right, B and Esc go back to the tile,
// A/Enter acts: Start and a category scroll the page to their row, the others open their screen.
#include "launcher.hpp"
#include "../ui/flix_layout.hpp"

bool Launcher::flixRailShown() const { return settings_.flix_rail && flixActive(); }
bool Launcher::flixRailFocused() const { return flix_rail_focus_ >= 0 && flixRailShown(); }

void Launcher::flixRailInset() const { flix_layout::rail = flixRailShown() ? flix_rail::width : 0.0f; }

std::vector<flix_rail::Item> Launcher::flixRailItems() const {
    using flix_rail::Kind;
    std::vector<flix_rail::Item> items;
    if (!flixRailShown() || flix_rows_.empty()) return items;
    items.push_back({Kind::home, 0, L'\uE81C', category_titles_.empty() ? std::wstring(tr(L"Ostatnie")) : category_titles_[0]}); // A clock: the recent games.
    for (const auto& row : flix_rows_) // The default categories the page has, in its order.
        if (const auto glyph = categoryGlyph(row.category); glyph && row.category < categories_.size())
            items.push_back({Kind::category, row.category, glyph, categories_[row.category].title});
    items.push_back({Kind::search, 0, L'\uE721', tr(L"Szukaj")});
    items.push_back({Kind::library, 0, L'\uE8F1', tr(L"Wszystkie gry")});
    items.push_back({Kind::profile, 0, L'\uE716', companionAvailable() ? std::wstring(tr(L"Towarzyszka")) : std::wstring(tr(L"Profil"))});
    items.push_back({Kind::menu, 0, L'\uE712', tr(L"Menu")});
    items.push_back({Kind::widgets, 0, L'\uEA86', tr(L"Widżety")});
    return items;
}

// The icon of the row in view (Start for the first row, a category for its own; -1 for the others).
int Launcher::flixRailActive(const std::vector<flix_rail::Item>& items) const {
    if (flix_rows_.empty()) return -1;
    if (flix_row_ == 0) return 0;
    const auto category = flix_rows_[flix_row_].category;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].kind == flix_rail::Kind::category && items[i].category == category) return static_cast<int>(i);
    return -1;
}

void Launcher::flixRailFocus(int index) {
    const auto items = flixRailItems();
    if (index >= 0 && items.empty()) return;
    flix_rail_focus_ = index < 0 ? -1 : std::clamp(index, 0, static_cast<int>(items.size()) - 1);
    if (flix_rail_focus_ >= 0) { flix_icon_focus_ = -1; dismissMenu(); }
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::flixRailMove(int amount) {
    const auto items = flixRailItems();
    if (items.empty() || !amount) return;
    const int next = std::clamp(flix_rail_focus_ + amount, 0, static_cast<int>(items.size()) - 1);
    if (next == flix_rail_focus_) { navigationBoundary(); return; }
    flix_rail_focus_ = next;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::flixRailAccept(int index) {
    using flix_rail::Kind;
    const auto items = flixRailItems();
    if (index < 0 || index >= static_cast<int>(items.size())) return;
    const auto item = items[static_cast<size_t>(index)];
    flix_rail_focus_ = -1;
    switch (item.kind) {
    case Kind::home: flixFocus(0, categories_.empty() ? 0 : categories_[0].selected); audio_.navigate(); break;
    case Kind::category:
        for (size_t row = 0; row < flix_rows_.size(); ++row)
            if (flix_rows_[row].category == item.category) { flixFocus(row, categories_[item.category].selected); audio_.navigate(); break; }
        break;
    case Kind::search: openSearch(); break;
    case Kind::library: openLibrary(); break;
    case Kind::profile: if (profileAvailable()) openProfile(); else if (companionAvailable()) openCompanion(); break;
    case Kind::menu: openQuick(); break;
    case Kind::widgets: toggleFriendsDrawer(); break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

int Launcher::flixRailAt(float x, float y) const {
    if (!flixRailShown()) return -1;
    const auto items = flixRailItems();
    return flix_rail::Layout{renderer_.viewport().height, &items}.hit(x, y);
}
