// Desktop mode's dialogs (src/ui/desktop_sheet.hpp): Ustawienia as a compact sheet in the desktop
// window. The rows, their state and every action are Tryb TV's (settings.cpp, settings_rows.cpp);
// this file maps the mouse onto them: buttons act on release over the same target, switches flip
// from anywhere on their row, choices open a list under their field, sliders drag, and the key and
// colour dialogs sit inside the sheet. Keys and the pad keep settings.cpp's handling.
#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../net/companion_characters.hpp"
#include "../library/steam_stats.hpp"

#include <algorithm>
#include <cmath>

using namespace desktop_ui;
using settings_ui::Control;
using settings_ui::Id;
using K = desktop_ui::SheetHit::Kind;

namespace {
settings_ui::Rect inflate(settings_ui::Rect r, float x, float y) { return {r.left - x, r.top - y, r.right + x, r.bottom + y}; }
}

std::wstring desktopPlayedWhen(uint64_t seconds); // desktop.cpp

// A store plugin page in the store's sheet (the same frame as the catalog); other plugin pages stay a TV screen in a sheet.
bool Launcher::desktopStorePage() const { return desktop_mode_ && screen_ == ScreenMode::plugin_page && storeTabsShown(); }

bool Launcher::desktopSheet() const {
    return desktopStorePage() || desktop_mode_ && (screen_ == ScreenMode::settings || screen_ == ScreenMode::details || screen_ == ScreenMode::artwork ||
                             screen_ == ScreenMode::downloads || screen_ == ScreenMode::glance || screen_ == ScreenMode::store);
}

// ---- Tryb TV's screens inside a desktop sheet: Urządzenia, kadrowanie, plugin pages ----------------

// As launchGame() decides: a GOG or Epic game with its account signed in installs through the launcher.
bool Launcher::desktopOwnInstall(const Game& game) const {
    return (game.source == GameSource::gog && !gog_state_.account.empty()) || (game.source == GameSource::epic && !epic_state_.account.empty()) ||
           (game.source == GameSource::amazon && amazonConnected()) || (xboxOwnInstall(game) && xboxConnected());
}

// Sklep from the keyboard: the field keeps the typing (WM_CHAR, Backspace); arrows walk the results by
// the page's own columns, Enter opens the chosen one (or searches at once after typing), Esc clears
// the field, then leaves.
bool Launcher::desktopStoreKey(WPARAM key) {
    const auto viewport = renderer_.desktopViewport();
    const auto grid = storeGrid(artworkSheet(viewport.width, viewport.height, desktopSheetDock()));
    const auto columns = static_cast<size_t>(grid.columns);
    const size_t count = cards_.size();
    switch (key) {
    case VK_ESCAPE:
        if (!search_query_.empty()) { search_query_.clear(); search_after_ = 0; searchStore(); }
        else back();
        return true;
    case VK_RETURN:
        if (search_dirty_ || !count) { if (!search_query_.empty()) searchStore(); }
        else if (selected_ < count) openDetails(cards_[selected_].game);
        return true;
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: {
        if (!count) return true;
        size_t next = std::min(selected_, count - 1);
        if (desktop_sheet_keyboard_) { // The first press only shows where the focus is.
            if (key == VK_LEFT && next > 0) --next;
            else if (key == VK_RIGHT && next + 1 < count) ++next;
            else if (key == VK_UP && next >= columns) next -= columns;
            else if (key == VK_DOWN && next + columns < count) next += columns;
            else if (key == VK_DOWN && next / columns + 1 < (count + columns - 1) / columns) next = count - 1; // A shorter last row.
        }
        selected_ = next;
        desktop_sheet_keyboard_ = true;
        const auto r = grid.poster(selected_, 0); // Into view.
        const float top = r.top - grid.top, bottom = top + grid.row_h, height = desktopSheetViewHeight();
        if (top < desktop_sheet_scroll_target_) desktop_sheet_scroll_target_ = top;
        else if (bottom > desktop_sheet_scroll_target_ + height) desktop_sheet_scroll_target_ = bottom - height;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    default: return false;
    }
}

bool Launcher::desktopEmbedded() const {
    return desktop_mode_ && (screen_ == ScreenMode::devices || screen_ == ScreenMode::crop || (screen_ == ScreenMode::plugin_page && !desktopStorePage()));
}

void Launcher::desktopEmbedMap(float& mouse_x, float& mouse_y) const {
    if (!desktopEmbedded()) return;
    const auto origin = renderer_.desktopEmbedOrigin();
    mouse_x -= origin.x; mouse_y -= origin.y;
}

// ---- Pages measured while drawn: Szczegóły, Zmień grafikę ----------------------------------------

SheetHit Launcher::desktopHotspotHit(float x, float y) const {
    const auto& spots = renderer_.desktopHotspots();
    for (auto it = spots.rbegin(); it != spots.rend(); ++it) if (it->rect.contains(x, y)) return it->hit;
    return {};
}

// The visible height of the page's scrolling part.
float Launcher::desktopSheetViewHeight() const {
    const auto view = renderer_.desktopViewport();
    if (screen_ == ScreenMode::artwork) {
        const auto p = artworkSheet(view.width, view.height, desktopSheetDock()).panel();
        return std::max(0.0f, p.bottom - 60 - (p.top + sheet_header + 76));
    }
    if (screen_ == ScreenMode::store || screen_ == ScreenMode::plugin_page) { // Below its field and state line.
        const auto sheet = artworkSheet(view.width, view.height, desktopSheetDock());
        return std::max(0.0f, sheet.panel().bottom - 12 - storeGrid(sheet).top);
    }
    if (screen_ == ScreenMode::glance) { // Below its tabs (desktop_pages_renderer.cpp).
        const auto p = artworkSheet(view.width, view.height, desktopSheetDock()).panel();
        return std::max(0.0f, p.bottom - 12 - (p.top + sheet_header + 60));
    }
    const auto p = detailsSheet(view.width, view.height, desktopSheetDock()).panel();
    return std::max(0.0f, p.bottom - 1 - (p.top + sheet_header));
}

void Launcher::desktopPageActivate(const SheetHit& hit, bool double_click) {
    if (screen_ == ScreenMode::plugin_page) { // A store plugin page: the catalog's frame, its rows below.
        const bool search = !plugin_page_.rows.empty() && plugin_page_.rows.front().input;
        switch (hit.kind) {
        case K::close: back(); return;
        case K::nav: switchStoreTab(hit.index - storeTabIndex()); return;
        case K::field: if (search) plugin_page_.focus = 0; return;
        case K::action:
            if (!search) return;
            if (hit.index == 1) plugin_page_.query.clear(); // The clear button.
            else { plugin_page_.focus = 0; pluginPageAccept(); } // Szukaj.
            return;
        case K::tile:
            if (hit.index >= 0 && static_cast<size_t>(hit.index) < plugin_page_.rows.size()) {
                plugin_page_.focus = hit.index; selected_ = static_cast<size_t>(hit.index);
                pluginPageAccept();
            }
            return;
        default: return;
        }
    }
    if (screen_ == ScreenMode::store) {
        switch (hit.kind) {
        case K::close: back(); return;
        case K::nav: switchStoreTab(hit.index - storeTabIndex()); return; // The Stores' tabs in the header.
        case K::action:
            if (hit.index == 1) { search_query_.clear(); search_after_ = 0; } // ✕ in the field.
            if (hit.index == 0 && search_query_.empty()) return;
            searchStore(); // Szukaj: at once, without waiting for the typing pause.
            return;
        case K::tile:
            if (hit.index >= 0 && static_cast<size_t>(hit.index) < cards_.size()) {
                selected_ = static_cast<size_t>(hit.index);
                openDetails(cards_[selected_].game); // Its Szczegóły; Back returns here.
            }
            return;
        default: return;
        }
    }
    if (screen_ == ScreenMode::downloads) {
        if (hit.kind == K::close) { back(); return; }
        if (hit.kind == K::action && hit.index == 1) {
            // Wyczyść listę: what is not moving now (finished, paused, queued) until its state changes.
            for (size_t i = 0; i < downloads_state_.rows.size() && i < downloads_games_.size(); ++i)
                if (!downloads_state_.rows[i].moving) desktop_cleared_downloads_[gameKey(downloads_games_[i])] = downloads_state_.rows[i].status;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (hit.kind == K::tile_action && hit.index >= 0 && static_cast<size_t>(hit.index) < downloads_games_.size()) {
            const auto game = downloads_games_[static_cast<size_t>(hit.index)];
            if (desktopOwnInstall(game)) { // Its install dialog: "Przerwij" stops it.
                if (game.source == GameSource::epic) openEpicInstall(game);
                else if (game.source == GameSource::amazon) openAmazonInstall(game);
                else if (game.source == GameSource::xbox) openXboxInstall(game);
                else openGogInstall(game);
                return;
            }
            downloads_state_.back = false;
            selected_ = static_cast<size_t>(hit.index);
            downloadsAccept(); // The store's client (or the plugin), where it can be stopped.
            return;
        }
        if (hit.kind == K::row && hit.index >= 0 && static_cast<size_t>(hit.index) < downloads_games_.size()) {
            downloads_state_.back = false;
            selected_ = static_cast<size_t>(hit.index);
            downloadsAccept(); // Its store's client, installer or plugin, as in Tryb TV.
        }
        return;
    }
    if (screen_ == ScreenMode::glance) {
        if (hit.kind == K::close) { back(); return; }
        if (hit.kind == K::nav) { glanceSection(hit.index); desktop_sheet_scroll_ = desktop_sheet_scroll_target_ = 0; return; }
        if (hit.kind == K::action) { glance_.focus = -3; glanceAccept(); return; } // Refresh.
        if (hit.kind == K::tile && hit.sub >= 0 && static_cast<size_t>(hit.sub) < glance_.feed.items.size()) {
            glance_.focus = hit.sub;
            glance_image_focus_->store(hit.sub);
            glanceAccept(); // The article in the user's browser.
        }
        return;
    }
    if (screen_ == ScreenMode::details) {
        if (cards_.empty()) return;
        const auto game = cards_[0].game;
        switch (hit.kind) {
        case K::close: for (int i = 0; i < 4 && screen_ == ScreenMode::details; ++i) back(); return;
        case K::back: back(); return;
        case K::row: openAchievementCard(hit.index); return; // Osiągnięcia: a row's card.
        case K::dialog_cancel: closeAchievementCard(); return;
        case K::dialog: return;
        case K::action: detail_action_ = hit.index; launch(); return;
        case K::tile:
            if (hit.sub == static_cast<int>(DetailsExtra::more)) {
                // The game's menu under "…", for this game alone (the grid's marks are not its targets here).
                const auto spot = std::find_if(renderer_.desktopHotspots().begin(), renderer_.desktopHotspots().end(),
                                               [&](const auto& item) { return item.hit == hit; });
                const auto marked = std::exchange(desktop_marked_, {});
                const size_t selected = std::exchange(selected_, size_t{0});
                if (spot != renderer_.desktopHotspots().end()) {
                    desktopOpenGameMenu(spot->rect.right, spot->rect.bottom + 6, false, true);
                    if (desktop_menu_) desktop_menu_->align_right = true;
                }
                desktop_marked_ = marked; selected_ = selected;
            } else if (hit.sub == static_cast<int>(DetailsExtra::artwork)) openArtworkBrowser(game);
            else if (hit.sub == static_cast<int>(DetailsExtra::folder)) {
                if (const auto folder = gameFolder(game); !folder.empty() && execute_(hwnd_, GameAction{folder.wstring(), {}, {}, ActionKind::open_client}) <= 32)
                    showNotification(NotificationKind::error, tr(L"Nie można otworzyć folderu"), folder.wstring());
            } else if (!hideableCopies(game).empty()) toggleHidden(game);
            return;
        default: return;
        }
    }
    if (screen_ != ScreenMode::artwork) return;
    auto& state = artwork_state_;
    switch (hit.kind) {
    case K::close: case K::back: closeArtworkBrowser(); return;
    case K::nav:
        state.gallery = state.search_focus = false;
        if (hit.index != state.category || state.results_mode) {
            state.category = hit.index; state.page = 0; state.source = state.override_source;
            loadArtworkPage();
        }
        desktop_sheet_scroll_ = desktop_sheet_scroll_target_ = 0;
        return;
    case K::tile_action: openLocalArtwork(); return;
    case K::cog:
        state.local_focus = state.crop_focus = state.back_focus = false; state.gallery = state.search_focus = false; // Its cog cycles; no TV focus.
        cycleArtworkProvider();
        return;
    case K::toggle: openArtworkCrop(); return;
    case K::field: state.search_focus = true; state.gallery = false; state.query_selected = true; return;
    case K::action: if (hit.index == 1) clearArtworkQuery(); else { state.search_focus = false; searchArtwork(); } return;
    case K::row:
        if (hit.index < 0 || static_cast<size_t>(hit.index) >= state.games.size()) return;
        state.selected = static_cast<size_t>(hit.index); state.search_focus = false; state.gallery = true;
        artworkAccept();
        desktop_sheet_scroll_ = desktop_sheet_scroll_target_ = 0;
        return;
    case K::tile:
        if (hit.sub < 0 || static_cast<size_t>(hit.sub) >= state.assets.size()) return;
        state.selected = static_cast<size_t>(hit.sub); state.gallery = true; state.search_focus = false;
        if (double_click) artworkAccept(); // Double-click applies at once, as Zastosuj.
        return;
    case K::up: case K::down: artworkPage(hit.kind == K::up ? -1 : 1); desktop_sheet_scroll_ = desktop_sheet_scroll_target_ = 0; return;
    case K::dropdown_item: state.local_kind = state.local_row = hit.index; audio_.context(); return;
    case K::dialog_save:
        if (state.local_open) { state.local_row = artwork_ui::State::local_pick_row; pickLocalArtwork(); }
        else if (state.gallery && state.selected < state.assets.size()) { state.search_focus = false; artworkAccept(); }
        return;
    case K::dialog_cancel: closeLocalArtwork(); return;
    default: return;
    }
}

settings_sheet::Layout Launcher::desktopSettingsLayout() const {
    const auto view = renderer_.desktopViewport();
    return settings_sheet::Layout::of(view.width, view.height, settings_state_, settings_rows_, desktopSheetDock());
}

// The control a picker's list opens from: the row's field, or the source tile being changed.
settings_ui::Rect Launcher::desktopPickerAnchor(const settings_sheet::Layout& L) const {
    const int row = settings_state_.row();
    if (row < 0 || row >= static_cast<int>(settings_rows_.size())) return {};
    const auto& item = settings_rows_[static_cast<size_t>(row)];
    if (item.control == Control::sources && !item.tiles.empty()) {
        const int count = static_cast<int>(item.tiles.size());
        return L.tile(row, desktop_sheet_scroll_, item.control, std::clamp(settings_state_.tile, 0, count - 1), count, settings_state_.tile);
    }
    return L.field(row, desktop_sheet_scroll_);
}

SheetHit Launcher::desktopSettingsHit(float x, float y) const {
    const auto L = desktopSettingsLayout();
    const float s = desktop_sheet_scroll_;
    if (color_editor_.open) {
        if (L.colorSave().contains(x, y)) return {K::dialog_save};
        if (L.colorCancel().contains(x, y)) return {K::dialog_cancel};
        for (int slider = 0; slider < settings_ui::ColorEditor::sliders; ++slider)
            if (inflate(L.colorTrack(slider), 12, 8).contains(x, y)) return {K::color_track, slider};
        return L.colorDialog().contains(x, y) ? SheetHit{K::dialog} : SheetHit{};
    }
    if (editingSecret()) {
        if (L.import_help && L.secretBrowse().contains(x, y)) return {K::dialog_browse};
        if (L.secretSave().contains(x, y)) return {K::dialog_save};
        if (L.secretCancel().contains(x, y)) return {K::dialog_cancel};
        if (L.secretField().contains(x, y)) return {K::dialog_field};
        return L.secretDialog().contains(x, y) ? SheetHit{K::dialog} : SheetHit{};
    }
    if (settingsPicking()) {
        const auto& item = settings_rows_[static_cast<size_t>(settings_state_.row())];
        const int count = static_cast<int>(item.pickList(settings_state_.tile).size());
        const auto list = L.dropdown(desktopPickerAnchor(L), count);
        if (!list.contains(x, y)) return {};
        const int first = settings_sheet::Layout::firstChoice(count, settings_state_.choice);
        for (int slot = 0; slot < std::min(count - first, settings_sheet::dropdown_visible); ++slot)
            if (L.dropdownItem(list, slot).contains(x, y)) return {K::dropdown_item, first + slot};
        return {K::dropdown};
    }
    if (L.close().contains(x, y)) return {K::close};
    if (settings_state_.subpage() && L.back().contains(x, y)) return {K::back};
    if (settings_save_failed_ && L.saveStatus().contains(x, y)) return {K::save_status};
    if (const int nav = L.hitNav(x, y); nav >= 0) return {K::nav, nav};
    const int index = L.hitRow(x, y, s);
    if (index < 0 || index >= static_cast<int>(settings_rows_.size())) return {};
    const auto& item = settings_rows_[static_cast<size_t>(index)];
    if (!item.reset.empty() && !item.badge.empty() && L.badge(item, index, s).contains(x, y)) return {K::badge, index};
    switch (item.control) {
    case Control::layout: {
        const float first = settings_ui::layoutFirst(item.choice, L.layout_visible);
        if (L.layoutBand(index, s).contains(x, y))
            for (int option = 0; option < settings_ui::home_layout_count; ++option)
                if (L.layoutTile(index, s, option, first).contains(x, y)) return {K::layout, index, option};
        return {K::row, index};
    }
    case Control::avatar: {
        const int count = companion::lookCount(), first = settings_sheet::Layout::avatarFirst(item.choice, count);
        for (int option = first; option < std::min(count, first + settings_sheet::Layout::avatar_visible); ++option)
            if (L.avatarCard(index, s, option, count, first).contains(x, y)) return {K::avatar, index, option};
        return {K::row, index};
    }
    case Control::sources: case Control::pads: {
        const int count = static_cast<int>(item.tiles.size());
        const int tile = L.hitTile(index, s, item.control, count, settings_state_.tile, x, y);
        if (tile < 0) return {K::row, index};
        const auto rect = L.tile(index, s, item.control, tile, count, settings_state_.tile);
        if (item.control == Control::pads && !item.tiles[static_cast<size_t>(tile)].action.empty() &&
            settings_sheet::Layout::tileAction(rect).contains(x, y)) return {K::tile_action, index, tile};
        return {K::tile, index, tile};
    }
    case Control::slider:
        if (inflate(L.track(index, s), 10, 8).contains(x, y)) return {K::track, index};
        return {K::row, index};
    case Control::choice: case Control::secret: case Control::text:
        return {L.field(index, s).contains(x, y) ? K::field : K::row, index};
    case Control::action: case Control::count: case Control::page:
        return {L.action(index, s).contains(x, y) ? K::action : K::row, index};
    case Control::fold: return {K::row, index}; // The whole heading opens or closes its section.
    case Control::platform:
        if (settings_ui::hasOptionsButton(item.id)) { if (L.cog(index, s).contains(x, y)) return {K::cog, index}; }
        else if (L.action(index, s).contains(x, y)) return {K::action, index};
        break;
    default: break;
    }
    const float right = L.toggleRight(item), y_line = L.controlY(index, s);
    if (settings_ui::Rect{right - 48, y_line - 16, right + 8, y_line + 16}.contains(x, y)) return {K::toggle, index};
    return {K::row, index};
}

void Launcher::desktopSettingsClick(float mouse_x, float mouse_y, bool double_click) {
    const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
    if (desktop_menu_) { // The game's menu from Szczegóły' "…": its items, or a click beside it closes it.
        const auto hit = desktopHit(x, y);
        if ((hit.target == desktop_ui::Target::menu_item || hit.target == desktop_ui::Target::submenu_item) && hit.index >= 0) { desktop_pressed_ = hit; SetCapture(hwnd_); }
        else desktopCloseMenu();
        return;
    }
    desktop_sheet_keyboard_ = false;
    if (screen_ == ScreenMode::settings && homeRowMoving()) { stopHomeRowMove(); return; } // A click puts the lifted row down.
    if (screen_ != ScreenMode::settings) {
        const auto hit = desktopHotspotHit(x, y);
        // A click beside the artwork's "Z dysku" panel closes it.
        if (screen_ == ScreenMode::artwork && artwork_state_.local_open && hit.kind != K::dialog && hit.kind != K::dropdown_item &&
            hit.kind != K::dialog_save && hit.kind != K::dialog_cancel) { closeLocalArtwork(); return; }
        if (screen_ == ScreenMode::artwork && hit.kind != K::field && artwork_state_.search_focus) { artwork_state_.search_focus = false; InvalidateRect(hwnd_, nullptr, FALSE); }
        if (hit.kind == K::none || hit.kind == K::dialog) return;
        if (double_click && hit.kind == K::tile && screen_ == ScreenMode::artwork) { desktopPageActivate(hit, true); return; }
        desktop_sheet_pressed_ = hit;
        SetCapture(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto hit = desktopSettingsHit(x, y);
    const auto L = desktopSettingsLayout();
    switch (hit.kind) {
    case K::color_track: {
        color_editor_.focus = hit.index;
        color_editor_.dragging = true;
        SetCapture(hwnd_);
        const auto track = L.colorTrack(hit.index);
        color_editor_.setLevel(hit.index, (x - track.left) / std::max(1.0f, track.right - track.left));
        colorEditorPreview();
        return;
    }
    case K::track: {
        settings_state_.options = true;
        settings_state_.selected[settings_state_.category] = hit.index;
        settings_state_.editing = false;
        settings_dragging_ = true;
        SetCapture(hwnd_);
        const auto track = L.track(hit.index, desktop_sheet_scroll_);
        settingsSetLevel((x - track.left) / std::max(1.0f, track.right - track.left));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case K::dialog: case K::dialog_field: case K::dropdown: return;
    case K::none:
        // A click beside an open list or dialog closes it, as Esc does.
        if (color_editor_.open) closeColorEditor(false);
        else if (editingSecret()) back();
        else if (settingsPicking()) { settings_state_.editing = false; InvalidateRect(hwnd_, nullptr, FALSE); }
        return;
    default:
        desktop_sheet_pressed_ = hit;
        desktop_row_drag_ = {};
        if ((hit.kind == K::row || hit.kind == K::action) && settings_state_.at(settings_ui::Page::home_rows) && hit.index >= 0 &&
            hit.index < static_cast<int>(settings_rows_.size()) && settings_rows_[static_cast<size_t>(hit.index)].id == Id::home_row &&
            homeRowMovable(settings_rows_[static_cast<size_t>(hit.index)].key))
            desktop_row_drag_ = {true, y, settings_rows_[static_cast<size_t>(hit.index)].key};
        SetCapture(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
}

void Launcher::desktopSettingsRelease(float mouse_x, float mouse_y) {
    const auto drag = std::exchange(desktop_row_drag_, {});
    if (drag.armed && home_row_moving_ == drag.key) { desktop_sheet_pressed_ = {}; stopHomeRowMove(); return; } // Dragged: put down.
    if (const auto menu_press = std::exchange(desktop_pressed_, {}); desktop_menu_ && menu_press.target != desktop_ui::Target::none) {
        const float scale = desktopScale();
        if (desktopHit(mouse_x / scale, mouse_y / scale) == menu_press) desktopActivate(menu_press);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto pressed = std::exchange(desktop_sheet_pressed_, {});
    if (pressed.kind == K::none || !desktopSheet()) return;
    const float scale = desktopScale();
    if (screen_ != ScreenMode::settings) {
        if (desktopHotspotHit(mouse_x / scale, mouse_y / scale) == pressed) desktopPageActivate(pressed, false);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto hit = desktopSettingsHit(mouse_x / scale, mouse_y / scale);
    if (!(hit == pressed)) { InvalidateRect(hwnd_, nullptr, FALSE); return; }
    desktopSettingsActivate(hit);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopSettingsActivate(const SheetHit& hit) {
    const auto focus = [&](int row) {
        settings_state_.options = true;
        settings_state_.editing = false;
        settings_state_.subcontrol = 0;
        settings_state_.selected[settings_state_.category] = row;
        settings_state_.back_focused = false;
    };
    switch (hit.kind) {
    case K::close:
        // Leaves Settings the usual way (sub-pages first, sign-ins cancelled, then saved).
        for (int i = 0; i < 8 && settingsOpen(); ++i) {
            settings_state_.options = settings_state_.editing = false;
            back();
        }
        return;
    case K::back: settings_state_.editing = false; back(); desktop_sheet_scroll_ = desktop_sheet_scroll_target_ = 0; return;
    case K::save_status: saveSettings(); return;
    case K::nav:
        settings_state_.selectCategory(hit.index);
        buildSettingsRows();
        settingsPreviewFocus();
        desktop_sheet_scroll_ = desktop_sheet_scroll_target_ = 0;
        return;
    case K::dropdown_item: settings_state_.choice = hit.index; settingsActivate(); return;
    case K::dialog_save:
        if (color_editor_.open) closeColorEditor(true);
        else { settings_secret_cancel_ = settings_secret_browse_ = false; settingsActivate(); }
        return;
    case K::dialog_cancel:
        if (color_editor_.open) closeColorEditor(false); else back();
        return;
    case K::dialog_browse: browseCompanionImport(); return;
    default: break;
    }
    if (hit.index < 0 || hit.index >= static_cast<int>(settings_rows_.size())) return;
    const auto item = settings_rows_[static_cast<size_t>(hit.index)];
    // A dense row shows its description at the first click; the next one (or its control) changes it.
    const bool revealing = item.dense && hit.kind == K::row && !(settings_state_.options && settings_state_.row() == hit.index);
    focus(hit.index);
    if (revealing) { audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    switch (hit.kind) {
    case K::badge: settingsResetRow(item); return;
    case K::layout: setHomeLayout(hit.sub); audio_.context(); return;
    case K::avatar: {
        const auto look = static_cast<companion::Look>(hit.sub);
        if (const auto* character = companion::character(look); !character || character->soon) { navigationBoundary(); return; } // "Wkrótce".
        settings_.companion_look = std::string(companion::lookKey(look));
        settingsChanged(); audio_.context();
        return;
    }
    case K::tile: case K::tile_action: settings_state_.tile = hit.sub; settingsActivate(); return;
    case K::cog: case K::action:
        if (item.control == Control::platform) settings_state_.editing = true;
        settingsActivate();
        return;
    case K::toggle: settingsActivate(); return;
    case K::field: settingsActivate(); return;
    case K::row:
        // A switch row flips from anywhere on it; a choice opens its list; tall rows and sliders keep
        // their own controls.
        if (item.control == Control::slider || settings_sheet::tall(item.control)) { InvalidateRect(hwnd_, nullptr, FALSE); return; }
        if (item.locked && item.control != Control::action && item.control != Control::count && item.control != Control::page &&
            item.control != Control::fold) { navigationBoundary(); return; }
        settingsActivate();
        return;
    default: return;
    }
}

void Launcher::desktopSettingsMove(float mouse_x, float mouse_y) {
    const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
    if (screen_ != ScreenMode::settings) {
        const auto hit = desktopHotspotHit(x, y);
        if (!(hit == desktop_sheet_hover_)) { desktop_sheet_hover_ = hit; InvalidateRect(hwnd_, nullptr, FALSE); }
        return;
    }
    const auto L = desktopSettingsLayout();
    if (color_editor_.dragging && color_editor_.focus < settings_ui::ColorEditor::sliders) {
        const auto track = L.colorTrack(color_editor_.focus);
        color_editor_.setLevel(color_editor_.focus, (x - track.left) / std::max(1.0f, track.right - track.left));
        colorEditorPreview();
        return;
    }
    if (settings_dragging_ && !settings_rows_.empty()) {
        const auto track = L.track(settings_state_.row(), desktop_sheet_scroll_);
        settingsSetLevel((x - track.left) / std::max(1.0f, track.right - track.left));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (desktop_row_drag_.armed && settings_state_.at(settings_ui::Page::home_rows)) {
        // A home row dragged: lifted once the pointer has moved, then it moves to the row under the pointer.
        const auto& key = desktop_row_drag_.key;
        if (home_row_moving_ != key && std::abs(y - desktop_row_drag_.y) < 6) return;
        if (home_row_moving_ != key) liftHomeRow(key);
        const auto under = desktopSettingsHit(x, y);
        const auto at = [&] {
            for (size_t i = 0; i < settings_rows_.size(); ++i) if (settings_rows_[i].id == Id::home_row && settings_rows_[i].key == key) return static_cast<int>(i);
            return -1;
        };
        if ((under.kind == K::row || under.kind == K::action) && under.index >= 0 && under.index < static_cast<int>(settings_rows_.size()) &&
            settings_rows_[static_cast<size_t>(under.index)].id == Id::home_row && settings_rows_[static_cast<size_t>(under.index)].key != "recent") {
            for (int steps = under.index - at(); steps != 0 && moveHomeRow(key, steps > 0 ? 1 : -1); steps += steps > 0 ? -1 : 1) {}
        }
        if (const int row = at(); row >= 0) { settings_state_.options = true; settings_state_.selected[settings_state_.category] = row; }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const auto hit = desktopSettingsHit(x, y);
    if (settingsPicking() && hit.kind == K::dropdown_item && hit.index != settings_state_.choice) {
        settings_state_.choice = hit.index;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    const int preview = hit.kind == K::layout ? hit.sub : -1;
    if (preview != settings_preview_.option) { settings_preview_.focus(preview, GetTickCount64()); InvalidateRect(hwnd_, nullptr, FALSE); }
    if (!(hit == desktop_sheet_hover_)) { desktop_sheet_hover_ = hit; InvalidateRect(hwnd_, nullptr, FALSE); }
}

bool Launcher::desktopSheetWheel(float, float, int delta) {
    if (!desktopSheet() || topModal() != Modal::none || friends_open_ || keyboard_.open) return false;
    const float notches = static_cast<float>(delta) / WHEEL_DELTA;
    if (screen_ != ScreenMode::settings) {
        if (screen_ == ScreenMode::artwork && artwork_state_.local_open) return true;
        if (achievementsOpen() && achievements_.card) return true; // The card stays; the list under it too.
        const float limit = std::max(0.0f, renderer_.desktopSheetHeight() - desktopSheetViewHeight());
        desktop_sheet_scroll_target_ = std::clamp(desktop_sheet_scroll_target_ - notches * 80, 0.0f, limit);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (color_editor_.open || editingSecret()) return true;
    if (settingsPicking()) {
        const auto& item = settings_rows_[static_cast<size_t>(settings_state_.row())];
        const int count = static_cast<int>(item.pickList(settings_state_.tile).size());
        settings_state_.choice = std::clamp(settings_state_.choice - static_cast<int>(std::round(notches)), 0, std::max(0, count - 1));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    const auto L = desktopSettingsLayout();
    desktop_sheet_scroll_target_ = std::clamp(desktop_sheet_scroll_target_ - notches * 72, 0.0f, L.maxScroll());
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

LPCWSTR Launcher::desktopSheetCursor(float mouse_x, float mouse_y) const {
    if (!desktopSheet() || topModal() != Modal::none || friends_open_) return nullptr;
    const float scale = desktopScale();
    if (screen_ != ScreenMode::settings) {
        const auto hit = desktopHotspotHit(mouse_x / scale, mouse_y / scale);
        return hit.kind == K::none || hit.kind == K::dialog ? IDC_ARROW : hit.kind == K::field ? IDC_IBEAM : IDC_HAND;
    }
    const auto hit = desktopSettingsHit(mouse_x / scale, mouse_y / scale);
    switch (hit.kind) {
    case K::none: case K::dialog: case K::dropdown: return IDC_ARROW;
    case K::dialog_field: return IDC_IBEAM;
    case K::row: {
        if (hit.index < 0 || hit.index >= static_cast<int>(settings_rows_.size())) return IDC_ARROW;
        const auto& item = settings_rows_[static_cast<size_t>(hit.index)];
        return item.control == Control::slider || settings_sheet::tall(item.control) || item.locked ? IDC_ARROW : IDC_HAND;
    }
    default: return IDC_HAND;
    }
}

std::string Launcher::desktopSettingsPage() const { return settingsPage(); }

// Ustawienia → Asystent: the page opens at its heading with her switch focused.
void Launcher::openCompanionSettings() {
    openSettings();
    if (!settingsOpen()) return;
    settings_state_.selectCategory(static_cast<int>(settings_ui::Category::companion));
    buildSettingsRows();
    const auto found = std::find_if(settings_rows_.begin(), settings_rows_.end(), [](const settings_ui::Row& row) { return row.id == Id::companion; });
    if (found != settings_rows_.end()) {
        const int row = static_cast<int>(found - settings_rows_.begin());
        settings_state_.options = true;
        settings_state_.selected[settings_state_.category] = row;
        ensureSettingVisible(); // Tryb TV's list.
        if (desktop_mode_) {
            const auto L = desktopSettingsLayout();
            const float label = L.labels[static_cast<size_t>(row)].empty() ? 0.0f : settings_sheet::section_gap - 12;
            desktop_sheet_scroll_ = desktop_sheet_scroll_target_ = std::clamp(L.tops[static_cast<size_t>(row)] - label, 0.0f, L.maxScroll());
            // This page is already shown: tickDesktopSheet keeps the scroll instead of starting at the top.
            desktop_sheet_page_ = desktopSettingsPage();
            desktop_sheet_row_ = row;
        }
    }
    settingsPreviewFocus();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Scroll glides to its target; a key moving the focus brings its row into view; a new page starts
// at its top.
bool Launcher::tickDesktopSheet(float dt) {
    if (!desktopSheet()) { desktop_sheet_nav_screen_ = -1; return false; }
    bool paint = false;
    const float nav_target = screen_ == ScreenMode::settings ? static_cast<float>(std::clamp(settings_state_.category, 0, static_cast<int>(settings_ui::categories.size()) - 1)) :
                             screen_ == ScreenMode::artwork ? static_cast<float>(artwork_state_.category) :
                             screen_ == ScreenMode::glance ? static_cast<float>(glance_ui::tabIndex(glance_.section)) : -1.0f;
    if (nav_target >= 0.0f) {
        if (desktop_sheet_nav_screen_ != static_cast<int>(screen_)) {
            desktop_sheet_nav_.snap(nav_target);
            desktop_sheet_nav_screen_ = static_cast<int>(screen_);
        } else {
            desktop_sheet_nav_.step(nav_target, dt, spring::glide.omega, spring::glide.zeta);
            if (!desktop_sheet_nav_.settled(nav_target, 0.004f)) paint = true;
        }
    } else desktop_sheet_nav_screen_ = -1;
    if (screen_ != ScreenMode::settings) {
        // Szczegóły (its achievements list is a page of its own) and Zmień grafikę (per type and page).
        const auto page = screen_ == ScreenMode::details ? std::string("details/") + (achievementsOpen() ? "a/" : "") + (cards_.empty() ? std::string{} : gameKey(cards_[0].game)) :
                          screen_ == ScreenMode::downloads ? std::string("downloads") :
                          screen_ == ScreenMode::glance ? "glance/" + std::to_string(glance_.section) :
                          screen_ == ScreenMode::store ? "store/" + std::to_string(store_jobs_->generation.load()) : // Each search starts at the top.
                          screen_ == ScreenMode::plugin_page ? "plugin/" + plugin_page_.plugin + '/' + text_encoding::utf8(plugin_page_.title) :
                          "artwork/" + std::to_string(artwork_state_.category) + '/' + std::to_string(artwork_state_.page) + (artwork_state_.results_mode ? "/r" : "");
        if (page != desktop_sheet_page_) { desktop_sheet_page_ = page; desktop_sheet_scroll_ = desktop_sheet_scroll_target_ = 0; paint = true; }
        const float limit = std::max(0.0f, renderer_.desktopSheetHeight() - desktopSheetViewHeight());
        desktop_sheet_scroll_target_ = std::clamp(desktop_sheet_scroll_target_, 0.0f, limit);
        if (std::abs(desktop_sheet_scroll_ - desktop_sheet_scroll_target_) > 0.3f) {
            desktop_sheet_scroll_ += (desktop_sheet_scroll_target_ - desktop_sheet_scroll_) * ease(dt, 16.0f);
            paint = true;
        } else if (desktop_sheet_scroll_ != desktop_sheet_scroll_target_) { desktop_sheet_scroll_ = desktop_sheet_scroll_target_; paint = true; }
        if (screen_ == ScreenMode::artwork && artwork_state_.search_focus) {
            const auto phase = GetTickCount64() / 530;
            if (phase != desktop_sheet_caret_) { desktop_sheet_caret_ = phase; paint = true; }
        }
        if (screen_ == ScreenMode::artwork && (artwork_state_.loading || artwork_state_.applying)) paint = true; // Placeholders pulse.
        if (screen_ == ScreenMode::glance && glance_.loading) paint = true;
        if (screen_ == ScreenMode::plugin_page) { // A store's page: keys bring the focused row into view; the art follows the scroll.
            const auto phase = GetTickCount64() / 530;
            if (phase != desktop_sheet_caret_) { desktop_sheet_caret_ = phase; paint = true; }
            if (desktop_sheet_keyboard_ && plugin_page_.focus >= 0 && plugin_page_.focus != desktop_sheet_row_ && plugin_page_.focus < static_cast<int>(plugin_page_.rows.size())) {
                const auto& row = plugin_page_.rows[static_cast<size_t>(plugin_page_.focus)];
                const float view_height = desktopSheetViewHeight();
                if (row.top < desktop_sheet_scroll_target_) desktop_sheet_scroll_target_ = row.top;
                else if (row.top + row.height() > desktop_sheet_scroll_target_ + view_height) desktop_sheet_scroll_target_ = row.top + row.height() - view_height;
            }
            desktop_sheet_row_ = plugin_page_.focus;
            if (std::abs(plugin_page_.scroll - desktop_sheet_scroll_) > 1.0f) { plugin_page_.scroll = desktop_sheet_scroll_; requestPluginPageArt(); }
        }
        if (screen_ == ScreenMode::store) { // The field's caret blinks; placeholders pulse while it searches.
            const auto phase = GetTickCount64() / 530;
            if (phase != desktop_sheet_caret_) { desktop_sheet_caret_ = phase; paint = true; }
            if (store_loading_) paint = true;
        }
        if (screen_ == ScreenMode::downloads && std::any_of(downloads_state_.rows.begin(), downloads_state_.rows.end(),
                                                            [](const auto& row) { return row.moving && !row.fraction; })) paint = true;
        return paint;
    }
    const auto L = desktopSettingsLayout();
    const auto page = desktopSettingsPage();
    if (page != desktop_sheet_page_) {
        desktop_sheet_page_ = page;
        desktop_sheet_scroll_ = desktop_sheet_scroll_target_ = 0;
        desktop_sheet_row_ = -1;
        // Back from a page: the tile it was opened from stays in view.
        if (settings_state_.options && settings_state_.row() > 0) {
            desktop_sheet_scroll_ = desktop_sheet_scroll_target_ = L.ensureVisible(settings_state_.row(), 0);
            desktop_sheet_row_ = settings_state_.row();
        }
        paint = true;
    }
    if (desktop_sheet_keyboard_ && settings_state_.options && settings_state_.row() != desktop_sheet_row_) {
        desktop_sheet_row_ = settings_state_.row();
        desktop_sheet_scroll_target_ = L.ensureVisible(desktop_sheet_row_, desktop_sheet_scroll_target_);
    }
    desktop_sheet_scroll_target_ = std::clamp(desktop_sheet_scroll_target_, 0.0f, L.maxScroll());
    if (std::abs(desktop_sheet_scroll_ - desktop_sheet_scroll_target_) > 0.3f) {
        desktop_sheet_scroll_ += (desktop_sheet_scroll_target_ - desktop_sheet_scroll_) * ease(dt, 16.0f);
        paint = true;
    } else if (desktop_sheet_scroll_ != desktop_sheet_scroll_target_) { desktop_sheet_scroll_ = desktop_sheet_scroll_target_; paint = true; }
    // The key dialog's caret blinks.
    if (editingSecret()) { const auto phase = GetTickCount64() / 530; if (phase != desktop_sheet_caret_) { desktop_sheet_caret_ = phase; paint = true; } }
    return paint;
}

void Launcher::desktopSheetView(SheetView& view) const {
    view.scroll = desktop_sheet_scroll_;
    if (desktop_sheet_nav_screen_ == static_cast<int>(screen_)) view.nav_glide = desktop_sheet_nav_.value;
    view.hover = desktop_sheet_hover_;
    view.pressed = desktop_sheet_pressed_;
    view.keyboard = desktop_sheet_keyboard_;
    view.maximized = IsZoomed(hwnd_) != FALSE;
    view.active_window = desktop_window_active_;
    view.window_hover = desktop_strip_hover_;
    view.max_hover = desktop_max_hover_;
    view.dock = desktopSheetDock();
    if (screen_ == ScreenMode::store) view.store_loading = store_loading_;
    if (screen_ == ScreenMode::downloads) {
        for (size_t i = 0; i < downloads_state_.rows.size(); ++i) {
            const bool own = i < downloads_games_.size() && desktopOwnInstall(downloads_games_[i]);
            view.download_actions.push_back(own ? std::wstring(tr(L"Anuluj")) : std::wstring(tr(L"Otwórz")));
            bool hidden = false;
            if (i < downloads_games_.size() && !downloads_state_.rows[i].moving)
                if (const auto found = desktop_cleared_downloads_.find(gameKey(downloads_games_[i])); found != desktop_cleared_downloads_.end())
                    hidden = found->second == downloads_state_.rows[i].status;
            view.download_hidden.push_back(hidden ? 1 : 0);
        }
    }
    if (screen_ == ScreenMode::details && !cards_.empty()) {
        const auto& game = cards_[0].game;
        const auto minutes = gamePlaytime(game, details_.playtime_minutes);
        view.playtime = minutes && *minutes ? formatPlaytime(*minutes) : std::wstring(tr(L"Brak"));
        view.last_played = desktopPlayedWhen(game.last_played);
        view.folder = !gameFolder(game).empty();
        view.hideable = !hideableCopies(game).empty();
        view.hidden = view.hideable && hiddenInLauncher(game);
    }
}
