// Ctrl+K in the desktop window: one field over the library finding games, its sections, the pages of
// Ustawienia and the window's actions. Enter shows a game in the library (Ctrl+Enter plays it), opens
// a section or page, or runs the action; Esc or a click outside closes it.
#include "launcher.hpp"
#include "../library/search.hpp"
#include "../ui/brand_icons.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>
#include <cwctype>

using namespace desktop_ui;

namespace {
// The library's own matching (library/search.hpp): the same accents, punctuation and word
// order rules as every other search field. `query` is already folded.
std::wstring lower(const std::wstring& text) { return foldGameTitle(text); }
int matchScore(const std::wstring& text, const std::wstring& query) {
    return query.empty() ? 1 : titleMatch(foldGameTitle(text), query);
}
constexpr size_t max_games = 6, max_sections = 4, max_settings = 3;
} // namespace

void Launcher::desktopQuickOpen() {
    if (!desktopActive()) return;
    desktopCloseMenu();
    desktop_downloads_open_ = false;
    desktopSearchFocus(false);
    desktop_quick_ = {};
    desktop_quick_.open = true;
    desktopQuickUpdate();
}

void Launcher::desktopQuickClose() {
    if (!desktop_quick_.open) return;
    desktop_quick_ = {};
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopQuickUpdate() {
    auto& quick = desktop_quick_;
    const auto query = lower(quick.query);
    std::vector<QuickItem> items;
    // Games: the best matches (most recently played first among equals); with no query, the latest played.
    std::vector<std::pair<int, const Game*>> games;
    for (const auto& game : grouped_library_) {
        if (!isGame(game) || (game.visibility != GameVisibility::public_game && !private_shelf_)) continue;
        if (query.empty() && !game.last_played) continue;
        if (const int score = matchScore(game.name, query)) games.push_back({score, &game});
    }
    std::sort(games.begin(), games.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second->last_played > b.second->last_played;
    });
    for (size_t i = 0; i < games.size() && i < (query.empty() ? 4 : max_games); ++i) {
        const auto& game = *games[i].second;
        QuickItem item;
        item.kind = QuickItem::Kind::game;
        item.title = game.name;
        item.detail = game.installed ? activePlatformName(game) : std::wstring(tr(L"Niezainstalowana"));
        if (item.detail.empty()) item.detail = tr(L"Gra");
        item.icon = glyph::game;
        item.brand = game.platform.empty() ? std::string{} : std::string(brand_icons::consoleKey(game.platform));
        if (item.brand.empty()) item.brand = brand_icons::sourceKey(game.source);
        item.target = gameKey(game);
        items.push_back(std::move(item));
    }
    if (!query.empty()) {
        // The sidebar's sections and collections.
        size_t sections = 0;
        for (const auto& entry : desktop_entries_) {
            if (sections >= max_sections || !matchScore(entry.title, query)) continue;
            QuickItem item;
            item.kind = QuickItem::Kind::section;
            item.title = entry.title;
            item.detail = entry.kind == EntryKind::collection ? std::wstring(tr(L"Kolekcja")) :
                          entry.group == Group::publishers ? std::wstring(tr(L"Wydawca")) : entry.group == Group::developers ? std::wstring(tr(L"Developer")) :
                          std::wstring(tr(L"Biblioteka"));
            item.icon = entry.icon;
            item.brand = entry.brand;
            item.target = entry.id;
            items.push_back(std::move(item));
            ++sections;
        }
        // Who made them, with a logo or not: every game of that developer or publisher (the search field).
        std::vector<std::pair<std::wstring, size_t>> studios_found;
        studioSearch(quick.query, &studios_found);
        size_t studios_shown = 0;
        for (const auto& [name, count] : studios_found) {
            if (studios_shown >= max_sections) break;
            if (std::any_of(items.begin(), items.end(), [&](const QuickItem& item) { return item.kind == QuickItem::Kind::section && lower(item.title) == lower(name); })) continue;
            items.push_back({QuickItem::Kind::section, name, tr(L"Wydawca lub developer") + std::wstring(L" · ") + std::to_wstring(count), glyph::tag, {},
                             "studio:" + text_encoding::utf8(name)});
            ++studios_shown;
        }
        // The pages of Ustawienia (by their own name, or all of them for "ustawienia").
        const bool all_settings = matchScore(tr(L"Ustawienia"), query) != 0;
        size_t pages = 0;
        for (size_t i = 0; i < settings_ui::categories.size() && pages < max_settings; ++i) {
            const std::wstring title = tr(settings_ui::categories[i].title);
            if (!all_settings && !matchScore(title, query)) continue;
            items.push_back({QuickItem::Kind::setting, title, tr(L"Ustawienia"), glyph::settings, {}, std::to_string(i)});
            ++pages;
        }
    }
    // The window's actions.
    const struct { const char* name; const wchar_t* title; wchar_t icon; } actions[] = {
        {"add", L"Dodaj grę z dysku", glyph::add},
        {"refresh", L"Odśwież biblioteki", glyph::refresh},
        {"downloads", L"Pobierania", glyph::download},
        {"store", L"Sklepy", glyph::shop},
        {"settings", L"Ustawienia", glyph::settings},
        {"tv", L"Przejdź do trybu TV", glyph::tv},
    };
    for (const auto& action : actions) {
        const std::wstring title = tr(action.title);
        if (!query.empty() && !matchScore(title, query)) continue;
        items.push_back({QuickItem::Kind::action, title, tr(L"Polecenie"), action.icon, {}, action.name});
    }
    if (items.size() > static_cast<size_t>(quick_rows)) items.resize(static_cast<size_t>(quick_rows));
    quick.items = std::move(items);
    quick.selected = std::clamp(quick.selected, 0, std::max(0, static_cast<int>(quick.items.size()) - 1));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopQuickRun(int index, bool play) {
    if (index < 0 || static_cast<size_t>(index) >= desktop_quick_.items.size()) return;
    const auto item = desktop_quick_.items[static_cast<size_t>(index)];
    desktopQuickClose();
    switch (item.kind) {
    case QuickItem::Kind::game: {
        const auto shown = [&]() -> std::optional<size_t> {
            for (size_t i = 0; i < cards_.size(); ++i) if (gameKey(cards_[i].game) == item.target) return i;
            return std::nullopt;
        };
        auto at = shown();
        if (!at) { // Not in this section or filter: Wszystkie gry, unfiltered.
            if (!desktop_query_.empty()) { desktop_query_.clear(); desktop_caret_ = 0; desktopApplyQuery(); }
            desktopShowSection("all");
            at = shown();
        }
        if (!at && settings_.desktop_installed) {
            // A game to install while the view shows installed ones only: the filter gives way,
            // as its own "Pokaż wszystkie" would, instead of Enter doing nothing.
            settings_.desktop_installed = false; settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
            rebuildCards(true);
            at = shown();
        }
        if (!at) return;
        desktopFocus(*at);
        if (play) launchGame(cards_[*at].game);
        return;
    }
    case QuickItem::Kind::section:
        if (item.target.starts_with("studio:")) { desktopFilterStudio(text_encoding::wide(item.target.substr(7))); return; }
        desktopShowSection(item.target); return;
    case QuickItem::Kind::setting: {
        openSettings();
        if (!settingsOpen()) return;
        settings_state_.selectCategory(std::stoi(item.target));
        buildSettingsRows();
        settingsPreviewFocus();
        desktop_sheet_scroll_ = desktop_sheet_scroll_target_ = 0;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case QuickItem::Kind::action:
        if (item.target == "add") openCustomEditor({});
        else if (item.target == "refresh") desktopRefreshLibraries();
        else if (item.target == "downloads") openDownloads();
        else if (item.target == "store") desktopToolPick(Tool::store);
        else if (item.target == "settings") openSettings();
        else if (item.target == "tv") setDesktopMode(false);
        return;
    }
}

bool Launcher::desktopQuickKey(WPARAM key) {
    auto& quick = desktop_quick_;
    const int count = static_cast<int>(quick.items.size());
    quick.caret = std::min(quick.caret, quick.query.size());
    switch (key) {
    case VK_ESCAPE: desktopQuickClose(); return true;
    case VK_RETURN: desktopQuickRun(quick.selected, GetKeyState(VK_CONTROL) < 0); return true;
    case VK_UP: if (count) quick.selected = (quick.selected + count - 1) % count; break;
    case VK_DOWN: if (count) quick.selected = (quick.selected + 1) % count; break;
    case VK_LEFT: if (quick.caret) --quick.caret; break;
    case VK_RIGHT: if (quick.caret < quick.query.size()) ++quick.caret; break;
    case VK_HOME: quick.caret = 0; break;
    case VK_END: quick.caret = quick.query.size(); break;
    case VK_BACK:
        if (!quick.caret) break;
        quick.query.erase(--quick.caret, 1);
        quick.selected = 0;
        desktopQuickUpdate();
        return true;
    case VK_DELETE:
        if (quick.caret >= quick.query.size()) break;
        quick.query.erase(quick.caret, 1);
        quick.selected = 0;
        desktopQuickUpdate();
        return true;
    default: return true; // Its own keys only while it is open; text comes as WM_CHAR.
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void Launcher::desktopQuickChar(wchar_t character) {
    auto& quick = desktop_quick_;
    if (character < L' ' || character == 127 || quick.query.size() >= 80) return;
    quick.caret = std::min(quick.caret, quick.query.size());
    quick.query.insert(quick.caret++, 1, character);
    quick.selected = 0;
    desktopQuickUpdate();
}

bool Launcher::desktopQuickPointer(float mouse_x, float mouse_y, bool press, bool double_click) {
    if (!desktop_quick_.open) return false;
    const auto view = renderer_.desktopViewport();
    const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
    const QuickLayout layout{view.width, view.height, static_cast<int>(desktop_quick_.items.size())};
    const int row = layout.hit(x, y);
    if (!press) {
        if (row != desktop_quick_.hover) { desktop_quick_.hover = row; InvalidateRect(hwnd_, nullptr, FALSE); }
        return true;
    }
    if (layout.field().contains(x, y)) { desktopTextPress(layout.field().left + 50, x, scale, double_click, true); return true; }
    if (row >= 0) desktopQuickRun(row, false);
    else if (!layout.panel().contains(x, y)) desktopQuickClose();
    return true;
}
