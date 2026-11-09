// Desktop mode, what a click or Enter does (src/ui/desktop_view.hpp): the activated target (sidebar,
// cards, toolbar, pane, popovers, the empty library), tools, the poster size and pane
// width, selection marks and refreshing the libraries.
#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../library/organization.hpp"

#include <algorithm>
#include <cmath>

using namespace desktop_ui;
using library_manager_ui::Page;

// ---- Actions ---------------------------------------------------------------------------------

void Launcher::desktopActivate(const desktop_ui::Hit& hit) {
    const auto tools = desktopTools();
    switch (hit.target) {
    case Target::minimize: ShowWindow(hwnd_, SW_MINIMIZE); return;
    case Target::maximize: toggleMaximized(); return;
    case Target::close: PostMessageW(hwnd_, WM_CLOSE, 0, 0); return;
    case Target::sidebar_toggle: desktopToggleSidebar(); return;
    case Target::pane_close: desktop_pane_closed_ = true; desktopReflow(); return;
    case Target::tool:
        if (hit.index >= 0 && hit.index < static_cast<int>(tools.size())) desktopToolPick(tools[static_cast<size_t>(hit.index)]);
        return;
    case Target::search_clear:
        desktop_query_.clear(); desktop_caret_ = 0; desktopApplyQuery(); desktopSearchFocus(true);
        return;
    case Target::sidebar_add: desktopNewCollection({}); return;
    case Target::library_add: openCustomEditor({}); return;
    case Target::library_refresh: desktopRefreshLibraries(); return;
    case Target::sidebar_settings: {
        const auto rows = desktopRows();
        if (hit.index >= 0 && hit.index < static_cast<int>(rows.size())) desktopGroupSettings(rows[static_cast<size_t>(hit.index)].group);
        return;
    }
    case Target::continue_tile: case Target::continue_play:
        if (hit.index < 0 || hit.index >= static_cast<int>(desktop_recent_games_.size())) return;
        desktopRevealGame(library_view::selectionKey(desktop_recent_games_[static_cast<size_t>(hit.index)]));
        if (hit.target == Target::continue_play) desktopLaunchSelected();
        return;
    case Target::sidebar_row: {
        const auto rows = desktopRows();
        if (hit.index < 0 || hit.index >= static_cast<int>(rows.size()) || !rows[static_cast<size_t>(hit.index)].heading) return;
        auto& collapsed = desktop_collapsed_[static_cast<size_t>(rows[static_cast<size_t>(hit.index)].group)];
        collapsed = !collapsed;
        desktop_sidebar_scroll_ = std::clamp(desktop_sidebar_scroll_, 0.0f, desktopLayout().sidebarMaxScroll(desktopRows()));
        desktopSaveState();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case Target::companion_card: case Target::companion_bubble: openCompanion(); return;
    case Target::start_card: desktopOpenStartMenu(); return;
    case Target::companion_more: {
        const auto anchor = desktopLayout().companionMore();
        Menu menu; menu.kind = Menu::Kind::companion;
        menu.x = anchor.right; menu.y = anchor.top; menu.align_right = true;
        menu.items.push_back({Action::companion_change, tr(L"Zmień"), {}, glyph::people, companion::availableLooks().size() > 1});
        menu.items.push_back({Action::companion_mind, tr(L"Umysł i pamięć"), {}, glyph::info, true});
        menu.items.push_back({Action::companion_settings, tr(L"Ustawienia"), {}, glyph::settings, true});
        desktop_menu_ = std::move(menu);
        companion_.link_pointer.reset();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    case Target::card_play:
        desktopFocus(static_cast<size_t>(hit.index), false);
        desktopLaunchSelected();
        return;
    case Target::card_check: desktopToggleMark(static_cast<size_t>(hit.index)); return;
    case Target::sort: desktopOpenSortMenu(); return;
    case Target::view_mode: desktopOpenViewMenu(); return;
    case Target::installed:
        settings_.desktop_installed = !settings_.desktop_installed;
        settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
        desktop_scroll_ = desktop_scroll_target_ = 0;
        rebuildCards(true);
        return;
    case Target::pane_primary: desktopLaunchSelected(); return;
    case Target::pane_favorite: if (!cards_.empty()) toggleFavorite(cards_[selected_].game); return;
    case Target::pane_more: {
        const auto r = desktopLayout().paneMore(desktop_pane_scroll_);
        desktopOpenGameMenu(r.right, r.bottom + 6, false, true);
        if (desktop_menu_) desktop_menu_->align_right = true;
        return;
    }
    case Target::pane_quick: {
        if (cards_.empty()) return;
        const auto game = cards_[selected_].game;
        if (hit.index == 0) openDetails(game);
        else if (hit.index == 1) openArtworkBrowser(game);
        else if (hit.index == 2) {
            if (const auto folder = gameFolder(game); !folder.empty())
                if (execute_(hwnd_, GameAction{folder.wstring(), {}, {}, ActionKind::open_client}) <= 32)
                    showNotification(NotificationKind::error, tr(L"Nie można otworzyć folderu"), folder.wstring());
        } else if (hit.index == 3 && !hideableCopies(game).empty()) toggleHidden(game);
        return;
    }
    case Target::pane_chip: {
        if (cards_.empty()) return;
        std::vector<std::string> ids;
        for (const auto& c : settings_collections_) {
            if (favoriteCollection(c) || (srmCollectionsHidden() && srmCollection(c))) continue;
            if (desktopMember(cards_[selected_].game, c)) ids.push_back("collection:" + c.id);
        }
        if (hit.index >= 0 && hit.index < static_cast<int>(ids.size())) desktopShowSection(ids[static_cast<size_t>(hit.index)]);
        return;
    }
    case Target::pane_chip_add: {
        const auto r = renderer_.desktopChipAdd();
        desktopOpenCollectionsMenu(r.left, r.bottom + 6);
        return;
    }
    case Target::pane_rating: openRatingSource(); return;
    case Target::pane_protondb: if (!cards_.empty()) openProtonDb(cards_[selected_].game); return;
    case Target::pane_hardware: desktop_hardware_open_ = !desktop_hardware_open_; InvalidateRect(hwnd_, nullptr, FALSE); return;
    case Target::pane_studio: { // Developer or Wydawca: that studio's games.
        if (cards_.empty()) return;
        bool loading = false;
        const auto& details = desktopDetails(cards_[selected_].game, loading);
        const auto& value = hit.index == 0 ? details.developer : details.publisher;
        desktopFilterStudio(value.substr(0, value.find(L", ")), hit.index == 0 ? studios::Role::developer : studios::Role::publisher);
        return;
    }
    case Target::pane_achievements: // Szczegóły with its achievements list open (Wstecz: the details).
        if (cards_.empty()) return;
        openDetails(cards_[selected_].game);
        openAchievements();
        return;
    case Target::pane_cloud: // Zarządzaj · Aurelia on its Zapisy w chmurze page.
        if (cards_.empty() || !aureliaCloudFor(cards_[selected_].game)) return;
        { const auto game = cards_[selected_].game; openAurelia(&game); aureliaPage(aurelia_ui::Page::cloud); }
        return;
    case Target::bulk: {
        const auto games = desktopTargets();
        const auto r = desktopLayout().paneBulk(hit.index);
        if (hit.index == 0) desktopOpenCollectionsMenu(r.left, r.bottom + 6);
        else if (hit.index == 1) {
            desktopSetMember(games, desktopFavorites(), true);
            showNotification(NotificationKind::launch, tr(L"Dodano do ulubionych"), gamesLabel(games.size()));
        } else if (hit.index == 2) {
            for (const auto& game : games) retryGameArtwork(game, &game == &games.front());
        } else if (hit.index == 3) {
            for (const auto& game : games) if (!hideableCopies(game).empty() && !hiddenInLauncher(game)) toggleHidden(game);
            desktop_marked_.clear();
        } else { desktop_marked_.clear(); InvalidateRect(hwnd_, nullptr, FALSE); }
        return;
    }
    case Target::menu_item: case Target::submenu_item: {
        if (!desktop_menu_ || hit.index < 0) return;
        const auto& list = hit.target == Target::submenu_item ? desktop_menu_->sub : desktop_menu_->items;
        if (hit.index >= static_cast<int>(list.size())) return;
        const auto item = list[static_cast<size_t>(hit.index)];
        if (!item.enabled || item.separator) return;
        if (item.submenu) { desktopOpenSubmenu(hit.index); return; }
        desktopMenuPick(item, hit.target == Target::submenu_item);
        return;
    }
    case Target::popover_row: {
        if (hit.index < 0 || hit.index >= static_cast<int>(desktop_download_games_.size())) return;
        const auto key = gameKey(desktop_download_games_[static_cast<size_t>(hit.index)]);
        desktop_downloads_open_ = false;
        for (size_t i = 0; i < cards_.size(); ++i)
            if (gameKey(cards_[i].game) == key) { desktopFocus(i); return; }
        openDownloads();
        return;
    }
    case Target::popover_all: desktop_downloads_open_ = false; openDownloads(); return;
    case Target::popover_clear: // What is not moving goes until its state changes (as on the Pobierania page).
        for (size_t i = 0; i < desktop_download_rows_.size() && i < desktop_download_games_.size(); ++i)
            if (!desktop_download_rows_[i].moving) desktop_cleared_downloads_[gameKey(desktop_download_games_[i])] = desktop_download_rows_[i].status;
        desktopRefreshDownloads();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    case Target::popover_action:
        if (hit.index < 0 || hit.index >= static_cast<int>(desktop_download_games_.size()) || hit.index >= static_cast<int>(desktop_download_rows_.size())) return;
        desktop_downloads_open_ = false;
        openDownloadSource(desktop_download_games_[static_cast<size_t>(hit.index)], desktop_download_rows_[static_cast<size_t>(hit.index)]);
        return;
    case Target::empty_primary: case Target::empty_secondary: {
        const bool primary = hit.target == Target::empty_primary;
        const auto* entry = desktopEntry();
        if (!trimmedQuery(desktop_query_).empty()) {
            if (primary) { desktop_query_.clear(); desktop_caret_ = 0; desktopApplyQuery(); }
            else { const auto query = desktop_query_; desktopShowSection("all"); desktop_query_ = query; desktopApplyQuery(); }
        } else if (settings_.desktop_installed && !desktopSource().empty()) {
            settings_.desktop_installed = false; settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
            rebuildCards(true);
        } else if (entry && entry->kind == EntryKind::collection) desktopCollectionDialog(entry->collection, Page::games);
        else if (public_library_.empty()) {
            if (primary) openCustomEditor({});
            else { settings_state_.category = static_cast<int>(settings_ui::Category::library); openSettings(); }
        }
        return;
    }
    default: return;
    }
}

void Launcher::desktopToolPick(desktop_ui::Tool tool) {
    switch (tool) {
    case Tool::glance: openGlance(); return;
    case Tool::tarot: openTarot(); return;
    case Tool::companion: openCompanion(); return;
    case Tool::store:
        openStores();
        return;
    case Tool::friends: toggleFriendsDrawer(); return; // The widgets themselves, as Tryb TV's drawer.
    case Tool::downloads: desktopToggleDownloads(); return;
    case Tool::tv: setDesktopMode(false); return;
    case Tool::settings: openSettings(); return;
    case Tool::more: desktopOpenToolsMenu(); return;
    case Tool::update: updateAction(); return; // Downloads, installs and restarts (or says why not now).
    }
}


// Ctrl+J: Pobierania in the library's place (the sidebar's entry), or back to the library.
void Launcher::desktopToggleDownloads() {
    desktop_downloads_open_ = false;
    if (screen_ == ScreenMode::downloads) { desktopLeavePage(); return; }
    if (profile_.open) closeProfile();
    if (desktopSheet()) desktopLeavePage();
    openDownloads();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopLaunchSelected() {
    if (cards_.empty() || pending_launch_) return;
    desktop_marked_.clear();
    launch();
}

// A pick of the focused card (Enter, A, double click, the search's one result): the default game
// action, its details or launch/install. The pane's Play button and the menu's Graj launch.
void Launcher::desktopPickSelected() {
    if (cards_.empty() || pending_launch_ || selected_ >= cards_.size()) return;
    const auto game = cards_[selected_].game;
    if (settings_.accept_action != AppSettings::AcceptAction::details || !isGame(game) || game.kind == GameKind::catalog) {
        desktopLaunchSelected();
        return;
    }
    desktop_marked_.clear();
    openDetails(game);
}

void Launcher::desktopSetPoster(float width) {
    const int next = std::clamp(static_cast<int>(std::lround(width)), static_cast<int>(poster_min), static_cast<int>(poster_max));
    if (next == settings_.desktop_poster) return;
    settings_.desktop_poster = next;
    settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
    const auto L = desktopLayout();
    // The selected game stays in view while the grid reflows.
    desktop_scroll_target_ = desktop_scroll_ = std::clamp(desktop_scroll_target_, 0.0f, L.maxScroll(cards_.size()));
    if (!cards_.empty()) desktop_scroll_target_ = desktop_scroll_ = L.reveal(selected_, cards_.size(), desktop_scroll_target_);
    desktopRequestArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The pane's width (dragged by its edge; 0 automatic): the grid reflows around the selected game.
void Launcher::desktopSetPane(int width) {
    auto L = desktopLayout();
    L.pane_width = static_cast<float>(width);
    const int next = width <= 0 ? 0 : static_cast<int>(L.paneWidth()); // As far as the window allows.
    if (next == settings_.desktop_pane) return;
    settings_.desktop_pane = next;
    desktopReflow();
}

void Launcher::desktopToggleSidebar() {
    settings_.desktop_narrow = !settings_.desktop_narrow;
    settings_dirty_ = true; settings_edited_ = std::chrono::steady_clock::now();
    desktop_active_y_valid_ = false; // Its rows change: the active bar snaps to its new place.
    desktop_sidebar_scroll_ = std::clamp(desktop_sidebar_scroll_, 0.0f, desktopLayout().sidebarMaxScroll(desktopRows()));
    desktop_sidebar_focus_ = -1;
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopReflow() {
    const auto L = desktopLayout();
    desktop_scroll_target_ = desktop_scroll_ = std::clamp(desktop_scroll_target_, 0.0f, L.maxScroll(cards_.size()));
    if (!cards_.empty()) desktop_scroll_target_ = desktop_scroll_ = L.reveal(selected_, cards_.size(), desktop_scroll_target_);
    desktop_columns_ = L.columns();
    desktopRequestArt();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---- Selection -------------------------------------------------------------------------------

void Launcher::desktopToggleMark(size_t index) {
    if (index >= desktop_keys_.size()) return;
    // The first Ctrl+click keeps the game already selected, as in Explorer.
    if (desktop_marked_.empty() && selected_ < desktop_keys_.size() && selected_ != index) {
        desktop_marked_.insert(desktop_keys_[selected_]);
        desktop_mark_pulse_[desktop_keys_[selected_]] = 1.0f;
    }
    const auto& key = desktop_keys_[index];
    if (!desktop_marked_.erase(key)) desktop_marked_.insert(key);
    desktop_mark_pulse_[key] = 1.0f;
    desktop_anchor_ = index;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::desktopMarkRange(size_t from, size_t to) {
    if (desktop_keys_.empty()) return;
    from = std::min(from, desktop_keys_.size() - 1); to = std::min(to, desktop_keys_.size() - 1);
    desktop_marked_.clear();
    for (size_t i = std::min(from, to); i <= std::max(from, to); ++i) {
        desktop_marked_.insert(desktop_keys_[i]);
        desktop_mark_pulse_[desktop_keys_[i]] = 1.0f;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::vector<Game> Launcher::desktopGamesOf(const std::vector<std::string>& keys) const {
    std::vector<Game> games;
    const std::unordered_set<std::string> wanted(keys.begin(), keys.end());
    for (size_t i = 0; i < cards_.size() && i < desktop_keys_.size(); ++i) if (wanted.contains(desktop_keys_[i])) games.push_back(cards_[i].game);
    return games;
}

// The marked games (in the grid's order) when the selected one is among them, else the selected one.
std::vector<Game> Launcher::desktopTargets() const {
    if (cards_.empty()) return {};
    const bool selected_marked = selected_ < desktop_keys_.size() && desktop_marked_.contains(desktop_keys_[selected_]);
    if (desktop_marked_.size() > 1 || (selected_marked && !desktop_marked_.empty())) {
        auto games = desktopGamesOf(std::vector<std::string>(desktop_marked_.begin(), desktop_marked_.end()));
        if (!games.empty()) return games;
    }
    return {cards_[selected_].game};
}

// Steam's and the PC stores' libraries and the user's games read again (F5, Biblioteka's arrows):
// a game bought or installed meanwhile joins the list when its store reports it.
void Launcher::desktopRefreshLibraries() {
    desktop_refresh_spin_ = 0.75f;
    steam_rescan_->request(); refreshPcLibraries(); refreshCustomGames(); startRomScan();
    // DRM-free: a full scan of its folders (a deleted game's folder takes it off the list); the signed-in
    // GOG and Epic accounts sync their libraries.
    startDrmScan(true);
    if (!gog_state_.account.empty()) gogCommand(gog::Command::sync);
    if (!epic_state_.account.empty()) epicCommand(epic::Command::sync);
    showNotification(NotificationKind::launch, tr(L"Odświeżanie biblioteki"), L"");
}
