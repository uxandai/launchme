// Desktop mode, the pointer side (src/ui/desktop_view.hpp): hit tests on the shared geometry, the
// mouse (hover, buttons acting on release, double-click to play, right-click menus, the wheel,
// dragging games onto collections and collections into order), the cursor and the tooltips.
// What the targets do: desktop_actions.cpp; keys: desktop_keys.cpp; menus and collections:
// desktop_menus.cpp.
#include "launcher.hpp"
#include "launcher_internal.hpp"

#include <algorithm>
#include <cmath>

using namespace desktop_ui;

// ---- Hit tests -------------------------------------------------------------------------------

std::pair<bool, bool> Launcher::desktopEmptyButtons() const {
    if (!cards_.empty() || scanning_ || libraryLoading()) return {false, false}; // As drawn: the skeleton has no buttons.
    const auto* entry = desktopEntry();
    if (!trimmedQuery(desktop_query_).empty()) return {true, desktop_section_ != "all"};
    if (settings_.desktop_installed && !desktopSource().empty()) return {true, false};
    if (entry && entry->kind == EntryKind::collection) return {true, false};
    if (entry && entry->kind == EntryKind::favorites) return {false, false};
    if (public_library_.empty()) return {true, true};
    return {false, false};
}

desktop_ui::Hit Launcher::desktopHit(float x, float y) const {
    const auto L = desktopLayout();
    if (desktop_menu_) {
        const auto& menu = *desktop_menu_;
        if (menu.sub_parent >= 0 && !menu.sub.empty()) {
            const auto sub = subRect(menu, L.width, L.height);
            if (sub.contains(x, y)) return {Target::submenu_item, hitItem(sub, menu.sub, x, y)};
        }
        const auto panel = menuRect(menu, L.width, L.height);
        if (panel.contains(x, y)) return {Target::menu_item, hitItem(panel, menu.items, x, y)};
        return {};
    }
    const auto tools = desktopTools();
    if (desktop_downloads_open_) {
        const auto at = std::find(tools.begin(), tools.end(), Tool::downloads);
        const auto anchor = L.tool(static_cast<int>(at - tools.begin()));
        const int rows = static_cast<int>(desktop_download_rows_.size());
        if (L.popover(rows, anchor).contains(x, y)) {
            if (L.popoverAll(rows, anchor).contains(x, y)) return {Target::popover_all, 0};
            if (desktopDownloadsClearable() && L.popoverClear(rows, anchor).contains(x, y)) return {Target::popover_clear, 0};
            for (int i = 0; i < std::min(rows, popover_rows); ++i)
                if (const auto r = L.popoverRow(i, rows, anchor); r.contains(x, y)) return {Layout::popoverAction(r).contains(x, y) ? Target::popover_action : Target::popover_row, i};
            return {Target::popover_row, -1};
        }
    }
    if (y < title_h) {
        for (int i = 0; i < 3; ++i)
            if (L.windowButton(i).contains(x, y)) return {i == 0 ? Target::minimize : i == 1 ? Target::maximize : Target::close, i};
        for (int i = 0; i < static_cast<int>(tools.size()); ++i) if (L.tool(i).contains(x, y)) return {Target::tool, i};
        if (L.sidebarToggle().contains(x, y)) return {Target::sidebar_toggle, 0};
        if (!desktop_query_.empty() && L.searchClear().contains(x, y)) return {Target::search_clear, 0};
        if (L.search().contains(x, y)) return {Target::search, 0};
        return {Target::caption, 0};
    }
    if (settings_.companion && renderer_.companionBubble().contains(x, y)) return {Target::companion_bubble, 0};
    // The pane's left edge: dragged, it resizes the pane (and the grid's columns).
    if (L.paneClose().contains(x, y) && !desktop_downloads_open_) return {Target::pane_close, 0};
    if (L.paneEdge().contains(x, y) && !desktop_downloads_open_) return {Target::pane_resize, 0};
    if (x < L.sidebarRight()) {
        if (L.companion_card && L.companionMore().contains(x, y)) return {Target::companion_more, 0};
        if (L.companion_card && L.companionCard().contains(x, y)) return {Target::companion_card, 0};
        if (L.start_card && L.startCard().contains(x, y)) return {Target::start_card, 0};
        const auto rows = desktopRows();
        const int row = L.hitRow(rows, desktop_sidebar_scroll_, x, y);
        if (row >= 0 && rows[static_cast<size_t>(row)].heading && rows[static_cast<size_t>(row)].group == Group::collections &&
            L.headingAdd(rows[static_cast<size_t>(row)], desktop_sidebar_scroll_).contains(x, y)) return {Target::sidebar_add, row};
        // Biblioteka's heading: "+" adds a game from disk, the arrows read the libraries again (F5).
        if (row >= 0 && rows[static_cast<size_t>(row)].heading && rows[static_cast<size_t>(row)].group == Group::library) {
            if (L.headingAdd(rows[static_cast<size_t>(row)], desktop_sidebar_scroll_).contains(x, y)) return {Target::library_add, row};
            if (L.headingRefresh(rows[static_cast<size_t>(row)], desktop_sidebar_scroll_).contains(x, y)) return {Target::library_refresh, row};
        }
        // Biblioteka, Platformy, Emulacja: the cog into their place in Ustawienia.
        if (row >= 0 && rows[static_cast<size_t>(row)].heading && settingsGroup(rows[static_cast<size_t>(row)].group) &&
            L.headingCog(rows[static_cast<size_t>(row)], desktop_sidebar_scroll_).contains(x, y)) return {Target::sidebar_settings, row};
        if (row >= 0) return {Target::sidebar_row, row};
        return {};
    }
    if (x >= L.paneLeft()) {
        const size_t marked = desktopTargets().size();
        if (marked > 1) {
            for (int i = 0; i < Layout::bulk_count; ++i) if (L.paneBulk(i).contains(x, y)) return {Target::bulk, i};
            return {};
        }
        if (cards_.empty()) return {};
        const float s = desktop_pane_scroll_;
        if (y < L.paneTop(s) + L.bannerHeight()) return {};
        if (L.panePrimary(s).contains(x, y)) return {Target::pane_primary, 0};
        if (L.favorite && L.paneFavorite(s).contains(x, y)) return {Target::pane_favorite, 0};
        if (L.paneMore(s).contains(x, y)) return {Target::pane_more, 0};
        for (int i = 0; i < Layout::quick_count; ++i) if (L.paneQuick(i, s).contains(x, y)) return {Target::pane_quick, i};
        const auto& chips = renderer_.desktopChips();
        for (int i = 0; i < static_cast<int>(chips.size()); ++i) if (chips[static_cast<size_t>(i)].contains(x, y)) return {Target::pane_chip, i};
        if (renderer_.desktopChipAdd().contains(x, y)) return {Target::pane_chip_add, 0};
        if (renderer_.desktopRating().contains(x, y)) return {Target::pane_rating, 0};
        if (renderer_.desktopProtonDb().contains(x, y)) return {Target::pane_protondb, 0};
        if (renderer_.desktopHardware().contains(x, y)) return {Target::pane_hardware, 0};
        for (int i = 0; i < 2; ++i) if (renderer_.desktopStudios()[static_cast<size_t>(i)].contains(x, y)) return {Target::pane_studio, i};
        if (renderer_.desktopAchievements().contains(x, y)) return {Target::pane_achievements, 0};
        if (renderer_.desktopCloud().contains(x, y)) return {Target::pane_cloud, 0};
        return {};
    }
    if (L.sortButton().contains(x, y)) return {Target::sort, 0};
    if (L.viewButton().contains(x, y)) return {Target::view_mode, 0};
    if (L.installedChip().contains(x, y)) return {Target::installed, 0};
    if (L.sizeSlider().contains(x, y)) return {Target::size_slider, 0};
    if (cards_.empty()) {
        const auto [primary, secondary] = desktopEmptyButtons();
        if (primary && L.emptyButton(0, secondary).contains(x, y)) return {Target::empty_primary, 0};
        if (secondary && L.emptyButton(1, true).contains(x, y)) return {Target::empty_secondary, 0};
        return {Target::grid, -1};
    }
    auto track = L.scrollTrack();
    track.left -= 4;
    if (L.maxScroll(cards_.size()) > 0 && track.contains(x, y)) return {Target::scrollbar, 0};
    if (const int tile = L.hitContinue(desktop_scroll_, x, y); tile >= 0)
        return {Layout::playButton(L.continueTile(tile, desktop_scroll_)).contains(x, y) ? Target::continue_play : Target::continue_tile, tile};
    const int card = L.hitCard(cards_.size(), desktop_scroll_, x, y);
    if (card >= 0) {
        const auto poster = L.poster(static_cast<size_t>(card), desktop_scroll_);
        const float hover = static_cast<size_t>(card) < desktop_card_hover_.size() ? desktop_card_hover_[static_cast<size_t>(card)] : 0.0f;
        const bool shown = hover > 0.3f || desktop_hover_ == Hit{Target::card, card} || desktop_hover_ == Hit{Target::card_play, card} ||
                           desktop_hover_ == Hit{Target::card_check, card};
        if ((shown || !desktop_marked_.empty()) && Layout::checkBox(poster).contains(x, y)) return {Target::card_check, card};
        return {Target::card, card};
    }
    return {Target::grid, -1};
}

// ---- Mouse -----------------------------------------------------------------------------------

void Launcher::desktopClick(float mouse_x, float mouse_y, bool double_click) {
    if (desktop_meta_.open && desktopActive()) { desktopMetaPointer(mouse_x, mouse_y, true, double_click); return; }
    if (desktop_quick_.open && desktopActive()) { desktopQuickPointer(mouse_x, mouse_y, true, double_click); return; }
    const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
    desktop_keyboard_ = false;
    const auto hit = desktopHit(x, y);
    if (desktop_menu_) {
        if ((hit.target == Target::menu_item || hit.target == Target::submenu_item) && hit.index >= 0) { desktop_pressed_ = hit; SetCapture(hwnd_); return; }
        if (hit.target != Target::menu_item && hit.target != Target::submenu_item) desktopCloseMenu(); // A click beside a menu only closes it.
        return;
    }
    const auto tools = desktopTools();
    if (desktop_downloads_open_ && hit.target != Target::popover_row && hit.target != Target::popover_all && hit.target != Target::popover_clear &&
        hit.target != Target::popover_action) {
        const bool own = hit.target == Target::tool && hit.index >= 0 && hit.index < static_cast<int>(tools.size()) && tools[static_cast<size_t>(hit.index)] == Tool::downloads;
        desktop_downloads_open_ = false;
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (own || hit.target != Target::card) return; // Its own shortcut toggles; a card may still be picked.
    }
    if (hit.target == Target::companion_bubble && !renderer_.companionLinkAt(x, y).empty()) {
        desktop_pressed_ = {};
        if (!double_click) companionLinkClick(x, y);
        return;
    }
    desktop_pressed_ = hit;
    desktop_drag_ = {};
    desktop_drag_.x0 = desktop_drag_.x = x; desktop_drag_.y0 = desktop_drag_.y = y;
    if (hit.target != Target::search && hit.target != Target::search_clear && desktop_search_focus_) desktopSearchFocus(false);
    switch (hit.target) {
    case Target::card: {
        const auto index = static_cast<size_t>(hit.index);
        desktop_zone_ = 1;
        if (double_click && !keyDown(VK_CONTROL) && !keyDown(VK_SHIFT)) {
            desktop_pressed_ = {};
            desktopFocus(index, false);
            desktopPickSelected();
            return;
        }
        if (desktop_pane_closed_) { desktop_pane_closed_ = false; desktopReflow(); } // A game clicked: its pane again.
        if (keyDown(VK_CONTROL)) desktopToggleMark(index);
        else if (keyDown(VK_SHIFT)) desktopMarkRange(desktop_anchor_, index);
        else {
            if (index >= desktop_keys_.size() || !desktop_marked_.contains(desktop_keys_[index])) desktop_marked_.clear();
            desktop_anchor_ = index;
        }
        desktopFocus(index, false);
        desktop_drag_.kind = DesktopDrag::Kind::card;
        desktop_drag_.card = hit.index;
        SetCapture(hwnd_);
        return;
    }
    case Target::sidebar_row: {
        const auto rows = desktopRows();
        if (hit.index < 0 || hit.index >= static_cast<int>(rows.size())) return;
        const auto& row = rows[static_cast<size_t>(hit.index)];
        desktop_zone_ = 0; desktop_sidebar_focus_ = hit.index;
        if (!row.heading) {
            const auto entry = desktop_entries_[static_cast<size_t>(row.entry)];
            if (actionEntry(entry.kind)) { desktop_pressed_ = {}; desktopPickEntry(entry, double_click); InvalidateRect(hwnd_, nullptr, FALSE); return; }
            desktopShowSection(entry.id);
            if (entry.reorderable) { desktop_drag_.kind = DesktopDrag::Kind::entry; desktop_drag_.row = hit.index; }
        }
        SetCapture(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case Target::continue_tile:
        if (!double_click) break;
        desktop_pressed_ = {};
        if (hit.index >= 0 && hit.index < static_cast<int>(desktop_recent_games_.size())) {
            desktopRevealGame(library_view::selectionKey(desktop_recent_games_[static_cast<size_t>(hit.index)]));
            desktopPickSelected();
        }
        return;
    case Target::search:
        desktopSearchFocus(true);
        desktopTextPress(desktopLayout().search().left + 36, x, desktopScale(), double_click, false); // Caret where it falls; drag selects.
        return;
    case Target::scrollbar: {
        const auto L = desktopLayout();
        const auto thumb = L.scrollThumb(cards_.size(), desktop_scroll_);
        if (y >= thumb.top && y < thumb.bottom) desktop_drag_.grab = y - thumb.top;
        else {
            desktop_drag_.grab = (thumb.bottom - thumb.top) * 0.5f;
            desktop_scroll_ = desktop_scroll_target_ = L.scrollFromThumb(cards_.size(), y - desktop_drag_.grab);
            desktopRequestArt();
        }
        desktop_drag_.kind = DesktopDrag::Kind::scrollbar; desktop_drag_.active = true;
        desktop_scrolled_at_ = GetTickCount64();
        SetCapture(hwnd_); InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case Target::pane_resize:
        if (double_click) { desktop_pressed_ = {}; desktopSetPane(0); settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now(); return; } // Back to automatic.
        desktop_drag_.kind = DesktopDrag::Kind::pane; desktop_drag_.active = true;
        desktop_drag_.grab = x - desktopLayout().paneLeft();
        SetCapture(hwnd_); InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case Target::size_slider: {
        const auto slider = desktopLayout().sizeSlider();
        desktopSetPoster(posterFromLevel((x - slider.left - 7) / std::max(1.0f, slider.right - slider.left - 14)));
        desktop_drag_.kind = DesktopDrag::Kind::slider; desktop_drag_.active = true;
        SetCapture(hwnd_);
        return;
    }
    case Target::grid:
        if (!keyDown(VK_CONTROL) && !desktop_marked_.empty()) { desktop_marked_.clear(); InvalidateRect(hwnd_, nullptr, FALSE); }
        else if (!keyDown(VK_CONTROL) && !keyDown(VK_SHIFT) && !double_click) { desktop_pane_closed_ = true; desktopReflow(); } // A click beside the games: none selected.
        desktop_pressed_ = {};
        return;
    case Target::none: case Target::caption:
        desktop_pressed_ = {};
        return;
    default: break;
    }
    SetCapture(hwnd_); // Buttons act on release, over the same target.
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopPointerUp(float mouse_x, float mouse_y) {
    if (!desktop_mode_) return;
    if (desktopSheet()) {
        desktopSettingsRelease(mouse_x, mouse_y); // Its buttons act on release; drags end in pointerUp().
        if (GetCapture() == hwnd_ && !settings_dragging_ && !color_editor_.dragging) ReleaseCapture();
        return;
    }
    const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
    const auto drag = std::exchange(desktop_drag_, {});
    const auto pressed = std::exchange(desktop_pressed_, {});
    if (GetCapture() == hwnd_) ReleaseCapture();
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (!desktopActive()) return;
    if (drag.active) {
        const auto rows = desktopRows();
        if (drag.kind == DesktopDrag::Kind::card && drag.drop_row >= 0 && drag.drop_row < static_cast<int>(rows.size()) && rows[static_cast<size_t>(drag.drop_row)].entry >= 0)
            desktopDrop(desktop_entries_[static_cast<size_t>(rows[static_cast<size_t>(drag.drop_row)].entry)], drag.games);
        else if (drag.kind == DesktopDrag::Kind::entry && drag.row >= 0 && drag.row < static_cast<int>(rows.size()) && rows[static_cast<size_t>(drag.row)].entry >= 0)
            desktopReorder(desktop_entries_[static_cast<size_t>(rows[static_cast<size_t>(drag.row)].entry)].collection, drag.reorder_before);
        else if (drag.kind == DesktopDrag::Kind::slider || drag.kind == DesktopDrag::Kind::pane) { settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now(); }
        return;
    }
    const auto hit = desktopHit(x, y);
    if (pressed.target == Target::none || !(hit == pressed)) return;
    desktopActivate(hit);
}

void Launcher::desktopMouseMove(float mouse_x, float mouse_y) {
    if (desktop_meta_.open && desktopActive()) { desktopMetaPointer(mouse_x, mouse_y, false); return; }
    if (desktop_quick_.open && desktopActive()) { desktopQuickPointer(mouse_x, mouse_y, false); return; }
    const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
    auto& drag = desktop_drag_;
    if (drag.kind != DesktopDrag::Kind::none) {
        drag.x = x; drag.y = y;
        const auto L = desktopLayout();
        switch (drag.kind) {
        case DesktopDrag::Kind::scrollbar:
            desktop_scroll_ = desktop_scroll_target_ = L.scrollFromThumb(cards_.size(), y - drag.grab);
            desktop_scrolled_at_ = GetTickCount64();
            desktopRequestArt();
            break;
        case DesktopDrag::Kind::slider: {
            const auto slider = L.sizeSlider();
            desktopSetPoster(posterFromLevel((x - slider.left - 7) / std::max(1.0f, slider.right - slider.left - 14)));
            break;
        }
        case DesktopDrag::Kind::pane: desktopSetPane(static_cast<int>(std::round(L.width - (x - drag.grab)))); break;
        case DesktopDrag::Kind::card: {
            if (!drag.active && std::hypot(x - drag.x0, y - drag.y0) > 6) {
                drag.active = true;
                desktop_tooltip_ = 0;
                drag.games.clear();
                for (const auto& game : desktopTargets()) drag.games.push_back(library_view::selectionKey(game));
            }
            if (!drag.active) break;
            drag.drop_row = -1;
            const auto rows = desktopRows();
            const int row = L.hitRow(rows, desktop_sidebar_scroll_, x, y);
            if (row >= 0 && !rows[static_cast<size_t>(row)].heading && desktop_entries_[static_cast<size_t>(rows[static_cast<size_t>(row)].entry)].droppable)
                drag.drop_row = row;
            break;
        }
        case DesktopDrag::Kind::entry: {
            if (!drag.active && std::abs(y - drag.y0) > 6) drag.active = true;
            if (!drag.active) break;
            const auto rows = desktopRows();
            drag.reorder_before = -1; drag.reorder_y = -1;
            int k = 0;
            float last_bottom = -1;
            for (const auto& row : rows) {
                if (row.heading || row.group != Group::collections) continue;
                const auto r = L.sidebarRow(row, desktop_sidebar_scroll_);
                if (y < (r.top + r.bottom) * 0.5f) { drag.reorder_before = k; drag.reorder_y = r.top - 1; break; }
                last_bottom = r.bottom + 1;
                ++k;
            }
            if (drag.reorder_before < 0) drag.reorder_y = last_bottom;
            break;
        }
        default: break;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto hit = desktopHit(x, y);
    if (desktop_menu_) {
        auto& menu = *desktop_menu_;
        if (hit.target == Target::menu_item && hit.index != menu.hover) {
            menu.hover = hit.index; menu.keyboard = false; menu.in_sub = false;
            if (hit.index >= 0 && menu.items[static_cast<size_t>(hit.index)].submenu && menu.items[static_cast<size_t>(hit.index)].enabled) desktopOpenSubmenu(hit.index);
            else if (hit.index >= 0) { menu.sub_parent = -1; menu.sub.clear(); }
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else if (hit.target == Target::submenu_item && hit.index != menu.sub_hover) {
            menu.sub_hover = hit.index; menu.keyboard = false; menu.in_sub = true;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
    if (!(hit == desktop_hover_)) {
        desktop_hover_ = hit;
        desktop_hover_at_ = GetTickCount64();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void Launcher::desktopContextClick(float mouse_x, float mouse_y) {
    if (desktop_meta_.open) return; // Its edits stay until Zapisz or Anuluj.
    if (desktop_quick_.open) { desktopQuickClose(); return; }
    const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
    desktop_keyboard_ = false;
    if (desktop_menu_) desktopCloseMenu();
    desktop_downloads_open_ = false;
    const auto hit = desktopHit(x, y);
    switch (hit.target) {
    case Target::card: case Target::card_play: case Target::card_check: {
        const auto index = static_cast<size_t>(hit.index);
        if (index >= desktop_keys_.size() || !desktop_marked_.contains(desktop_keys_[index])) desktop_marked_.clear();
        desktop_zone_ = 1;
        desktopFocus(index, false);
        desktopOpenGameMenu(x, y, false);
        return;
    }
    case Target::continue_tile: case Target::continue_play:
        if (hit.index < 0 || hit.index >= static_cast<int>(desktop_recent_games_.size())) return;
        desktopRevealGame(library_view::selectionKey(desktop_recent_games_[static_cast<size_t>(hit.index)]));
        desktopOpenGameMenu(x, y, false);
        return;
    case Target::sidebar_row: case Target::sidebar_add: {
        const auto rows = desktopRows();
        if (hit.index < 0 || hit.index >= static_cast<int>(rows.size())) return;
        const auto& row = rows[static_cast<size_t>(hit.index)];
        if (!row.heading && desktop_entries_[static_cast<size_t>(row.entry)].kind == EntryKind::game) { // A recent game: its own menu.
            desktopRevealGame(desktop_entries_[static_cast<size_t>(row.entry)].game);
            desktopOpenGameMenu(x, y, false);
        } else if (!row.heading && actionEntry(desktop_entries_[static_cast<size_t>(row.entry)].kind)) return;
        else if (!row.heading) desktopOpenSectionMenu(row.entry, x, y);
        else if (row.group == Group::collections) {
            Menu menu; menu.kind = Menu::Kind::section; menu.x = x; menu.y = y;
            menu.items.push_back({Action::collection_new, tr(L"Nowa kolekcja…"), {}, glyph::add});
            menu.items.push_back({Action::collection_manage, tr(L"Zarządzaj kolekcjami…"), {}, glyph::edit});
            desktop_menu_ = std::move(menu);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    case Target::pane_primary: case Target::pane_favorite: case Target::pane_more: case Target::pane_quick: case Target::pane_chip:
    case Target::pane_chip_add: case Target::pane_rating: case Target::pane_studio: case Target::pane_achievements: case Target::pane_cloud:
    case Target::pane_protondb: case Target::pane_hardware:
        desktopOpenGameMenu(x, y, false);
        return;
    default: return;
    }
}

bool Launcher::desktopWheel(float mouse_x, float mouse_y, int delta, bool control) {
    if ((desktop_quick_.open || desktop_meta_.open) && desktopActive()) return true; // The list under them stays put.
    if (desktopSheet()) return desktopSheetWheel(mouse_x, mouse_y, delta);
    if (!desktopActive() || topModal() != Modal::none || friends_open_ || keyboard_.open) return false;
    const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
    if (desktop_menu_) { desktopCloseMenu(); return true; }
    if (desktop_downloads_open_) { desktop_downloads_open_ = false; InvalidateRect(hwnd_, nullptr, FALSE); }
    const float notches = static_cast<float>(delta) / WHEEL_DELTA;
    const auto L = desktopLayout();
    if (control) { desktopSetPoster(static_cast<float>(settings_.desktop_poster) + notches * 12); return true; }
    if (y < title_h) return true;
    if (x < L.sidebarRight()) {
        desktop_sidebar_scroll_ = std::clamp(desktop_sidebar_scroll_ - notches * 64, 0.0f, L.sidebarMaxScroll(desktopRows()));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (x >= L.paneLeft()) {
        const float limit = std::max(0.0f, renderer_.desktopPaneHeight() - (L.height - title_h));
        desktop_pane_scroll_ = std::clamp(desktop_pane_scroll_ - notches * 72, 0.0f, limit);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    desktopScrollBy(-notches * 120);
    return true;
}

void Launcher::desktopScrollBy(float amount) {
    const auto L = desktopLayout();
    desktop_scroll_target_ = std::clamp(desktop_scroll_target_ + amount, 0.0f, L.maxScroll(cards_.size()));
    desktop_scrolled_at_ = GetTickCount64();
    desktopRequestArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

LPCWSTR Launcher::desktopCursor(float mouse_x, float mouse_y) const {
    if (const auto shape = desktopModalCursor(mouse_x, mouse_y)) return shape;
    // Ustawienia or Profil in the library's place: the sidebar and top bar around them.
    if ((desktopPageDocked() && topModal() == Modal::none && !friends_open_) || (profileDocked() && profile_.open && topModal() == Modal::profile)) {
        const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
        if (y < title_h || x < desktopLayout().sidebarRight()) {
            const auto hit = desktopHit(x, y);
            if (hit.target == Target::search) return IDC_IBEAM;
            return hit.target == Target::none || hit.target == Target::caption ? IDC_ARROW : IDC_HAND;
        }
    }
    if (desktop_meta_.open && desktopActive()) { // Edytuj metadane: its fields take text, its buttons a click.
        const auto view = renderer_.desktopViewport();
        if (desktop_meta_.find.open) {
            const int found = meta_find::Layout{view.width, view.height}.hit(mouse_x / desktopScale(), mouse_y / desktopScale(), desktop_meta_.find);
            return found < 0 ? IDC_ARROW : found == meta_find::item::query ? IDC_IBEAM : IDC_HAND;
        }
        const int hit = desktop_ui::MetaLayout{view.width, view.height}.hit(mouse_x / desktopScale(), mouse_y / desktopScale(), desktop_meta_.identify);
        return hit < 0 ? IDC_ARROW : hit < desktop_ui::meta_fields ? IDC_IBEAM : IDC_HAND;
    }
    if (desktop_quick_.open && desktopActive()) {
        const auto view = renderer_.desktopViewport();
        const desktop_ui::QuickLayout layout{view.width, view.height, static_cast<int>(desktop_quick_.items.size())};
        return layout.hit(mouse_x / desktopScale(), mouse_y / desktopScale()) >= 0 ? IDC_HAND : IDC_ARROW;
    }
    if (desktopEmbedded()) { // The sheet's ✕; the screen beneath answers through hoverButton().
        const float scale = desktopScale();
        const auto view = renderer_.desktopViewport();
        return embedSheet(view.width, view.height).close().contains(mouse_x / scale, mouse_y / scale) ? IDC_HAND : nullptr;
    }
    if (desktopSheet()) return windowStripHit(mouse_x, mouse_y) >= 0 ? nullptr : desktopSheetCursor(mouse_x, mouse_y);
    if (!desktopActive() || topModal() != Modal::none || friends_open_ || pending_launch_) return nullptr;
    if (desktop_drag_.active && desktop_drag_.kind == DesktopDrag::Kind::card) return desktop_drag_.drop_row >= 0 ? IDC_HAND : IDC_ARROW;
    if (desktop_drag_.active && desktop_drag_.kind == DesktopDrag::Kind::entry) return IDC_SIZENS;
    if (desktop_drag_.active && desktop_drag_.kind == DesktopDrag::Kind::pane) return IDC_SIZEWE;
    const float scale = desktopScale();
    const auto hit = desktopHit(mouse_x / scale, mouse_y / scale);
    switch (hit.target) {
    case Target::search: return IDC_IBEAM;
    case Target::pane_resize: return IDC_SIZEWE;
    case Target::none: case Target::caption: case Target::grid: return IDC_ARROW;
    case Target::menu_item: case Target::submenu_item: return IDC_ARROW;
    case Target::pane_quick:
        if (hit.index == 2 && !cards_.empty() && gameFolder(cards_[selected_].game).empty()) return IDC_ARROW;
        return IDC_HAND;
    default: return IDC_HAND;
    }
}

// ---- Tooltips --------------------------------------------------------------------------------

std::wstring Launcher::desktopTooltip(const desktop_ui::Hit& hit) const {
    const auto tools = desktopTools();
    switch (hit.target) {
    case Target::minimize: return tr(L"Minimalizuj");
    case Target::maximize: return IsZoomed(hwnd_) ? tr(L"Przywróć") : tr(L"Maksymalizuj");
    case Target::close: return tr(L"Zamknij");
    case Target::sidebar_toggle: return settings_.desktop_narrow ? tr(L"Rozwiń panel boczny") : tr(L"Zwiń panel boczny");
    case Target::pane_resize: return tr(L"Przeciągnij, aby zmienić szerokość; dwuklik: domyślna");
    case Target::pane_close: return tr(L"Zamknij panel gry");
    case Target::sidebar_row: { // Narrow, the icons alone: their names on hover.
        if (!settings_.desktop_narrow) return {};
        const auto rows = desktopRows();
        if (hit.index < 0 || hit.index >= static_cast<int>(rows.size()) || rows[static_cast<size_t>(hit.index)].entry < 0) return {};
        return desktop_entries_[static_cast<size_t>(rows[static_cast<size_t>(hit.index)].entry)].title;
    }
    case Target::tool:
        if (hit.index < 0 || hit.index >= static_cast<int>(tools.size())) return {};
        switch (tools[static_cast<size_t>(hit.index)]) {
        case Tool::glance: return tr(L"Glance — nowości i premiery");
        case Tool::tarot: return tr(L"Tarot — wróżba z kart");
        case Tool::companion: return companion::name(companionLook());
        case Tool::store: return tr(L"Sklep");
        case Tool::friends: return tr(L"Widżety · profil i znajomi");
        case Tool::downloads: {
            const auto moving = std::count_if(desktop_download_rows_.begin(), desktop_download_rows_.end(), [](const auto& row) { return row.moving; });
            return moving ? std::wstring(tr(L"Pobierania")) + L" · " + std::to_wstring(moving) : std::wstring(tr(L"Pobierania"));
        }
        case Tool::tv: return tr(L"Tryb TV");
        case Tool::settings: return tr(L"Ustawienia");
        case Tool::more: return tr(L"Więcej · Widżety, Glance, Tarot, Sklep");
        case Tool::update: return std::get<0>(updateRow()) + L" · " + std::get<2>(updateRow());
        }
        return {};
    case Target::search_clear: return tr(L"Wyczyść");
    case Target::sidebar_add: return tr(L"Nowa kolekcja");
    case Target::library_add: return tr(L"Dodaj grę z dysku");
    case Target::library_refresh: return tr(L"Odśwież biblioteki (F5)");
    case Target::sidebar_settings: return tr(L"Otwórz w Ustawieniach");
    case Target::continue_play:
        if (hit.index >= 0 && hit.index < static_cast<int>(desktop_recent_games_.size())) return actionLabel(desktop_recent_games_[static_cast<size_t>(hit.index)]);
        return {};
    case Target::companion_card: return std::wstring(tr(L"Porozmawiaj: ")) + companion::name(companionLook());
    case Target::companion_more: return tr(L"Więcej opcji");
    case Target::start_card: return tr(L"Pierwsze kroki: platformy, klucze, ROM-y, pierwsza gra");
    case Target::card:
        if (hit.index >= 0 && hit.index < static_cast<int>(cards_.size())) return cards_[static_cast<size_t>(hit.index)].game.name;
        return {};
    case Target::card_play:
        if (hit.index >= 0 && hit.index < static_cast<int>(cards_.size())) return actionLabel(cards_[static_cast<size_t>(hit.index)].game);
        return {};
    case Target::card_check: return tr(L"Zaznacz (Ctrl+klik)");
    case Target::sort: return tr(L"Sortowanie");
    case Target::view_mode: return tr(L"Widok: plakaty, kwadraty, poziome, lista");
    case Target::installed: return tr(L"Tylko zainstalowane gry");
    case Target::size_slider: return tr(L"Rozmiar plakatów (Ctrl+kółko)");
    case Target::pane_favorite:
        return !cards_.empty() && isFavorite(cards_[selected_].game) ? tr(L"Usuń z ulubionych") : tr(L"Dodaj do ulubionych");
    case Target::pane_more: return tr(L"Więcej opcji");
    case Target::pane_quick:
        if (hit.index == 2 && !cards_.empty() && gameFolder(cards_[selected_].game).empty()) return tr(L"Gra nie jest zainstalowana");
        return {};
    case Target::pane_chip_add: return tr(L"Dodaj do kolekcji");
    case Target::pane_achievements: return tr(L"Pokaż osiągnięcia");
    case Target::pane_cloud: return tr(L"Zapisy w chmurze");
    default: return {};
    }
}

desktop_ui::Rect Launcher::desktopTooltipAnchor(const desktop_ui::Hit& hit) const {
    const auto L = desktopLayout();
    switch (hit.target) {
    case Target::minimize: return L.windowButton(0);
    case Target::maximize: return L.windowButton(1);
    case Target::close: return L.windowButton(2);
    case Target::sidebar_toggle: return L.sidebarToggle();
    case Target::pane_resize: { const auto e = L.paneEdge(); const float y = desktop_drag_.y > 0 ? desktop_drag_.y : (e.top + e.bottom) * 0.5f; return {e.left, y - 12, e.right, y + 12}; }
    case Target::sidebar_row: {
        const auto rows = desktopRows();
        return hit.index >= 0 && hit.index < static_cast<int>(rows.size()) ? L.sidebarRow(rows[static_cast<size_t>(hit.index)], desktop_sidebar_scroll_) : Rect{};
    }
    case Target::tool: return L.tool(hit.index);
    case Target::search_clear: return L.searchClear();
    case Target::sidebar_add: {
        const auto rows = desktopRows();
        return hit.index >= 0 && hit.index < static_cast<int>(rows.size()) ? L.headingAdd(rows[static_cast<size_t>(hit.index)], desktop_sidebar_scroll_) : Rect{};
    }
    case Target::library_add: case Target::library_refresh: {
        const auto rows = desktopRows();
        if (hit.index < 0 || hit.index >= static_cast<int>(rows.size())) return {};
        const auto& row = rows[static_cast<size_t>(hit.index)];
        return hit.target == Target::library_add ? L.headingAdd(row, desktop_sidebar_scroll_) : L.headingRefresh(row, desktop_sidebar_scroll_);
    }
    case Target::sidebar_settings: {
        const auto rows = desktopRows();
        return hit.index >= 0 && hit.index < static_cast<int>(rows.size()) ? L.headingCog(rows[static_cast<size_t>(hit.index)], desktop_sidebar_scroll_) : Rect{};
    }
    case Target::continue_play: return Layout::playButton(L.continueTile(hit.index, desktop_scroll_));
    case Target::companion_card: return L.companionCard();
    case Target::start_card: return L.startCard();
    case Target::companion_more: return L.companionMore();
    case Target::card: case Target::card_play: case Target::card_check: {
        const auto poster = L.poster(static_cast<size_t>(std::max(hit.index, 0)), desktop_scroll_);
        if (hit.target == Target::card_play) return Layout::playButton(poster);
        if (hit.target == Target::card_check) return Layout::checkBox(poster);
        return {poster.left, poster.top, poster.right, poster.bottom + L.captionHeight()};
    }
    case Target::sort: return L.sortButton();
    case Target::view_mode: return L.viewButton();
    case Target::installed: return L.installedChip();
    case Target::size_slider: return L.sizeSlider();
    case Target::pane_favorite: return L.paneFavorite(desktop_pane_scroll_);
    case Target::pane_more: return L.paneMore(desktop_pane_scroll_);
    case Target::pane_quick: return L.paneQuick(hit.index, desktop_pane_scroll_);
    case Target::pane_chip_add: return renderer_.desktopChipAdd();
    case Target::pane_achievements: return renderer_.desktopAchievements();
    case Target::pane_cloud: return renderer_.desktopCloud();
    default: return {};
    }
}
