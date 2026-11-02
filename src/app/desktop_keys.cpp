// Desktop mode, the keyboard: shortcuts, arrows over the sections and the grid, Enter and Back,
// type-to-search and the search field.
#include "launcher.hpp"
#include "launcher_internal.hpp"

#include <algorithm>

using namespace desktop_ui;
using library_manager_ui::Page;

// ---- Keys ------------------------------------------------------------------------------------

bool Launcher::desktopKey(WPARAM key, bool repeated) {
    if (desktopTextKey(key)) return true; // Selection, words and the clipboard in the focused field.
    if (desktop_mode_ && screen_ == ScreenMode::store && topModal() == Modal::none && !friends_open_ && !keyboard_.open && !launchAnimationActive() && !keyDown(VK_MENU))
        return desktopStoreKey(key);
    if (desktop_meta_.open) {
        if (desktopActive() && topModal() == Modal::none && !friends_open_) return desktopMetaKey(key);
        desktopMetaClose();
    }
    if (desktop_quick_.open) {
        if (desktopActive() && topModal() == Modal::none && !friends_open_) return desktopQuickKey(key);
        desktopQuickClose();
    }
    if (!desktopActive() || topModal() != Modal::none || friends_open_ || keyboard_.open || launchAnimationActive()) return false;
    const bool control = keyDown(VK_CONTROL), shift = keyDown(VK_SHIFT);
    if (keyDown(VK_MENU)) return false;
    if (control && key == 'K') { if (!repeated) desktopQuickOpen(); return true; } // Quick search over the library.
    if (pending_launch_) return key != VK_F8;
    if (desktop_menu_) { desktopMenuKey(key); return true; }
    if (desktop_downloads_open_ && (key == VK_ESCAPE || key == VK_BACK)) { desktop_downloads_open_ = false; InvalidateRect(hwnd_, nullptr, FALSE); return true; }
    if (key == VK_F11) { if (!repeated) toggleMaximized(); return true; }
    if (key == VK_F5) {
        if (!repeated) {
            desktopRefreshLibraries();
        }
        return true;
    }
    if (control && key == 'F') { desktopSearchFocus(true); return true; }
    // The title bar's tools from the keyboard: Ctrl+, Ustawienia, Ctrl+J Pobierania.
    if (control && key == VK_OEM_COMMA) { if (!repeated) openSettings(); return true; }
    if (control && key == 'J') { if (!repeated) desktopToggleDownloads(); return true; }
    if (control && key == 'H') { if (!repeated) togglePrivateCategory(); return true; }
    if (control && (key == VK_OEM_PLUS || key == VK_ADD)) { desktopSetPoster(static_cast<float>(settings_.desktop_poster) + 16); return true; }
    if (control && (key == VK_OEM_MINUS || key == VK_SUBTRACT)) { desktopSetPoster(static_cast<float>(settings_.desktop_poster) - 16); return true; }
    if (control && (key == '0' || key == VK_NUMPAD0)) { desktopSetPoster(poster_default); return true; }
    if (desktop_search_focus_) {
        auto& query = desktop_query_;
        auto& caret = desktop_caret_;
        caret = std::min(caret, query.size());
        const auto edited = [&] { desktop_query_dirty_ = true; desktop_query_edited_ = std::chrono::steady_clock::now(); InvalidateRect(hwnd_, nullptr, FALSE); };
        switch (key) {
        case VK_ESCAPE:
            if (!query.empty()) { query.clear(); caret = 0; desktopApplyQuery(); }
            else desktopSearchFocus(false);
            return true;
        case VK_RETURN: case VK_DOWN: case VK_TAB:
            desktopSearchFocus(false);
            desktop_zone_ = 1; desktop_keyboard_ = true;
            if (desktop_query_dirty_) desktopApplyQuery();
            if (!cards_.empty()) desktopFocus(0);
            if (key == VK_RETURN && cards_.size() == 1) desktopPickSelected(); // One result: Enter picks it.
            return true;
        case VK_LEFT: if (caret) --caret; InvalidateRect(hwnd_, nullptr, FALSE); return true;
        case VK_RIGHT: if (caret < query.size()) ++caret; InvalidateRect(hwnd_, nullptr, FALSE); return true;
        case VK_HOME: caret = 0; InvalidateRect(hwnd_, nullptr, FALSE); return true;
        case VK_END: caret = query.size(); InvalidateRect(hwnd_, nullptr, FALSE); return true;
        case VK_BACK:
            if (!caret) return true;
            if (control) { // The word before the caret.
                size_t start = caret;
                while (start && query[start - 1] == L' ') --start;
                while (start && query[start - 1] != L' ') --start;
                query.erase(start, caret - start); caret = start;
            } else { query.erase(caret - 1, 1); --caret; }
            edited();
            return true;
        case VK_DELETE: if (caret < query.size()) { query.erase(caret, 1); edited(); } return true;
        case 'V':
            if (control && OpenClipboard(hwnd_)) {
                if (const auto handle = GetClipboardData(CF_UNICODETEXT)) {
                    if (const auto* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
                        const size_t limit = std::min<size_t>(GlobalSize(handle) / sizeof(wchar_t), 80);
                        for (size_t i = 0; i < limit && text[i] && query.size() < 80; ++i)
                            if (text[i] >= L' ') query.insert(caret++, 1, text[i]);
                        GlobalUnlock(handle);
                    }
                }
                CloseClipboard();
                edited();
            }
            return true;
        default: return !control; // WM_CHAR types the rest.
        }
    }
    desktop_keyboard_ = true;
    switch (key) {
    case VK_LEFT: desktopMove(-1, 0, shift); return true;
    case VK_RIGHT: desktopMove(1, 0, shift); return true;
    case VK_UP: desktopMove(0, -1, shift); return true;
    case VK_DOWN: desktopMove(0, 1, shift); return true;
    case VK_HOME: case VK_END:
        if (desktop_zone_ == 1 && !cards_.empty()) {
            const size_t target = key == VK_HOME ? 0 : cards_.size() - 1;
            if (shift) desktopMarkRange(desktop_anchor_, target); else { desktop_marked_.clear(); desktop_anchor_ = target; }
            desktopFocus(target);
        }
        return true;
    case VK_PRIOR: case VK_NEXT:
        if (desktop_zone_ == 1 && !cards_.empty()) {
            const auto L = desktopLayout();
            const int rows = std::max(1, static_cast<int>(L.viewHeight() / L.rowStep()));
            desktopMove(0, key == VK_PRIOR ? -rows : rows, shift);
        }
        return true;
    case VK_RETURN:
        // Ctrl+Enter plays (or installs) the focused game at once, whatever Enter's default action.
        if (!repeated && control && desktop_zone_ == 1 && !cards_.empty() && selected_ < cards_.size() && isGame(cards_[selected_].game)) desktopLaunchSelected();
        else if (!repeated) desktopAccept();
        return true;
    case VK_DELETE:
        // Del hides the focused game (as the menu's Ukryj; again shows it).
        if (!repeated && desktop_zone_ == 1 && !cards_.empty() && selected_ < cards_.size() && !hideableCopies(cards_[selected_].game).empty()) {
            const auto game = cards_[selected_].game; // A copy: hiding rebuilds the cards.
            toggleHidden(game);
        }
        return true;
    case VK_SPACE:
        if (!repeated) {
            if (control && desktop_zone_ == 1 && !cards_.empty()) desktopToggleMark(selected_);
            else desktopAccept();
        }
        return true;
    case VK_APPS: case VK_F10:
        if (key == VK_F10 && !shift) return false;
        if (!cards_.empty()) {
            const auto r = desktopLayout().poster(selected_, desktop_scroll_);
            desktopOpenGameMenu(r.left + 16, r.top + 16, true);
        }
        return true;
    case VK_ESCAPE: case VK_BACK: desktopBack(); return true;
    case VK_TAB: {
        // Sidebar → grid → pane → search, Shift backwards.
        constexpr int order[] = {0, 1, 2, 3};
        int at = desktop_zone_;
        at = (at + (shift ? 3 : 1)) % 4;
        if (order[at] == 3) desktopSearchFocus(true);
        else {
            desktop_zone_ = order[at];
            if (desktop_zone_ == 0) {
                const auto rows = desktopRows();
                for (int i = 0; i < static_cast<int>(rows.size()); ++i)
                    if (!rows[static_cast<size_t>(i)].heading && rows[static_cast<size_t>(i)].entry == desktopEntryIndex(desktop_section_)) desktop_sidebar_focus_ = i;
            }
            if (desktop_zone_ == 2) desktop_pane_focus_ = 0;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    case VK_F2: {
        const auto* entry = desktopEntry();
        if (entry && entry->kind == EntryKind::collection) desktopCollectionDialog(entry->collection, Page::name);
        return true;
    }
    case 'A':
        if (control) { desktopMarkRange(0, cards_.empty() ? 0 : cards_.size() - 1); return true; }
        break;
    default: break;
    }
    if (control) return false;
    // Letters and digits: WM_CHAR starts the search with them.
    return (key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z') || (key >= VK_OEM_1 && key <= VK_OEM_8) || key == VK_OEM_102 ||
           (key >= VK_NUMPAD0 && key <= VK_DIVIDE);
}

void Launcher::desktopMove(int dx, int dy, bool extend) {
    if (!desktopActive()) return;
    desktop_keyboard_ = true;
    if (desktop_menu_) { desktopMenuKey(dy < 0 ? VK_UP : dy > 0 ? VK_DOWN : dx < 0 ? VK_LEFT : VK_RIGHT); return; }
    const auto L = desktopLayout();
    if (desktop_zone_ == 3) { if (dy > 0) { desktopSearchFocus(false); desktop_zone_ = 1; if (!cards_.empty()) desktopFocus(selected_); } return; }
    if (desktop_zone_ == 0) {
        const auto rows = desktopRows();
        if (rows.empty()) return;
        if (dx > 0) { desktop_zone_ = 1; if (!cards_.empty()) desktopFocus(selected_); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (dx < 0) {
            // Left on a heading folds its group, as in a tree.
            if (desktop_sidebar_focus_ >= 0 && desktop_sidebar_focus_ < static_cast<int>(rows.size()) && rows[static_cast<size_t>(desktop_sidebar_focus_)].heading) {
                desktop_collapsed_[static_cast<size_t>(rows[static_cast<size_t>(desktop_sidebar_focus_)].group)] = true;
                desktopSaveState();
            } else navigationBoundary();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        const int current = std::clamp(desktop_sidebar_focus_, 0, static_cast<int>(rows.size()) - 1);
        const int next = std::clamp(current + dy, 0, static_cast<int>(rows.size()) - 1);
        if (next == current && desktop_sidebar_focus_ >= 0) { navigationBoundary(); return; }
        desktop_sidebar_focus_ = next;
        const auto& row = rows[static_cast<size_t>(next)];
        if (!row.heading) desktopShowSection(desktop_entries_[static_cast<size_t>(row.entry)].id);
        // Keep the row in view.
        const auto r = L.sidebarRow(row, desktop_sidebar_scroll_);
        if (r.top < L.sidebarTop()) desktop_sidebar_scroll_ -= L.sidebarTop() - r.top;
        else if (r.bottom > L.sidebarBottom()) desktop_sidebar_scroll_ += r.bottom - L.sidebarBottom();
        desktop_sidebar_scroll_ = std::clamp(desktop_sidebar_scroll_, 0.0f, L.sidebarMaxScroll(rows));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (desktop_zone_ == 2) {
        const bool bulk = desktopTargets().size() > 1;
        int focus = desktop_pane_focus_;
        if (bulk) focus = std::clamp(focus + dy + dx, 0, Layout::bulk_count - 1);
        else if (dy) focus = dy > 0 ? (focus < 3 ? 3 + std::min(focus, 2) : focus) : (focus >= 3 ? std::min(focus - 3, 2) : focus);
        else if (dx < 0 && (focus == 0 || focus == 3)) { desktop_zone_ = 1; InvalidateRect(hwnd_, nullptr, FALSE); return; }
        else {
            focus = std::clamp(focus + dx, focus >= 3 ? 3 : 0, focus >= 3 ? 3 + Layout::quick_count - 1 : 2);
            if (focus == 1 && !desktopLayout().favorite) focus = std::clamp(focus + dx, 0, 2); // No star: Graj and "…" side by side.
        }
        if (focus == desktop_pane_focus_) navigationBoundary();
        desktop_pane_focus_ = focus;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    // The grid.
    if (cards_.empty()) {
        if (dx < 0) desktop_zone_ = 0;
        else if (dy < 0) desktopSearchFocus(true);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const int columns = L.columns();
    const int count = static_cast<int>(cards_.size()), current = static_cast<int>(selected_);
    const int column = current % columns;
    if (dx < 0 && column == 0 && !extend) {
        desktop_zone_ = 0;
        const auto rows = desktopRows();
        for (int i = 0; i < static_cast<int>(rows.size()); ++i)
            if (!rows[static_cast<size_t>(i)].heading && rows[static_cast<size_t>(i)].entry == desktopEntryIndex(desktop_section_)) desktop_sidebar_focus_ = i;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (dx > 0 && (column == columns - 1 || current + 1 >= count) && !extend) { desktop_zone_ = 2; desktop_pane_focus_ = 0; InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (dy < 0 && current < columns && !extend) { desktopSearchFocus(true); return; }
    const int next = std::clamp(current + dx + dy * columns, 0, count - 1);
    if (next == current) { navigationBoundary(); return; }
    if (extend) desktopMarkRange(desktop_anchor_, static_cast<size_t>(next));
    else { desktop_marked_.clear(); desktop_anchor_ = static_cast<size_t>(next); }
    desktopFocus(static_cast<size_t>(next));
}

void Launcher::desktopAccept() {
    if (desktop_menu_) { desktopMenuKey(VK_RETURN); return; }
    switch (desktop_zone_) {
    case 0: {
        const auto rows = desktopRows();
        if (desktop_sidebar_focus_ < 0 || desktop_sidebar_focus_ >= static_cast<int>(rows.size())) { desktop_zone_ = 1; break; }
        const auto& row = rows[static_cast<size_t>(desktop_sidebar_focus_)];
        if (row.heading) desktopActivate({Target::sidebar_row, desktop_sidebar_focus_});
        else desktop_zone_ = 1;
        break;
    }
    case 2: {
        if (desktopTargets().size() > 1) { desktopActivate({Target::bulk, desktop_pane_focus_}); break; }
        const int focus = desktop_pane_focus_;
        if (focus == 0) desktopActivate({Target::pane_primary, 0});
        else if (focus == 1) desktopActivate({Target::pane_favorite, 0});
        else if (focus == 2) desktopActivate({Target::pane_more, 0});
        else desktopActivate({Target::pane_quick, focus - 3});
        break;
    }
    case 3: desktopKey(VK_RETURN, false); break;
    default: desktopPickSelected(); break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopBack() {
    if (desktop_menu_) { desktopCloseMenu(); return; }
    if (desktop_downloads_open_) desktop_downloads_open_ = false;
    else if (desktop_drag_.kind != DesktopDrag::Kind::none) { desktop_drag_ = {}; if (GetCapture() == hwnd_) ReleaseCapture(); }
    else if (desktop_search_focus_ || !desktop_query_.empty()) {
        if (!desktop_query_.empty()) { desktop_query_.clear(); desktop_caret_ = 0; desktopApplyQuery(); }
        desktopSearchFocus(false);
    } else if (!desktop_marked_.empty()) desktop_marked_.clear();
    else if (desktop_zone_ != 1) desktop_zone_ = 1;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::desktopCharacter(wchar_t character) {
    if (!desktopActive() || friends_open_ || desktop_menu_ || topDialog() != Modal::none) return false;
    if (character < L' ' || character == 127) return desktop_search_focus_;
    if (!desktop_search_focus_) {
        if (character == L' ') return true; // Space plays the game (WM_KEYDOWN); it starts no search.
        desktopSearchFocus(true);
        desktop_query_.clear(); desktop_caret_ = 0; // Typing over the grid starts a new search.
    }
    if (desktop_query_.size() >= 80) return true;
    desktop_caret_ = std::min(desktop_caret_, desktop_query_.size());
    desktop_query_.insert(desktop_caret_++, 1, character);
    desktop_query_dirty_ = true;
    desktop_query_edited_ = std::chrono::steady_clock::now();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void Launcher::desktopSearchFocus(bool focus) {
    desktop_search_focus_ = focus;
    if (focus) { desktop_zone_ = 3; desktop_caret_ = desktop_query_.size(); desktop_downloads_open_ = false; }
    else if (desktop_zone_ == 3) desktop_zone_ = 1;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopApplyQuery() {
    desktop_query_dirty_ = false;
    desktop_marked_.clear();
    desktop_scroll_ = desktop_scroll_target_ = 0;
    desktop_focus_key_.clear();
    selected_ = 0;
    if (desktopActive()) rebuildCards(true);
}
