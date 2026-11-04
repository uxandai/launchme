// The quick bar, the private shelf (Ctrl+H), the category rail and menu, and the game's context
// menu (Enter held, Y, right click): opening, moving and accepting.
#include "launcher.hpp"
#include "../ui/category_menu.hpp"
#include "../ui/category_rail.hpp"
#include "../ui/context_menu.hpp"
#include "../ui/quick_menu.hpp"
#include "../platform/text_encoding.hpp"

#include <algorithm>

void Launcher::openQuick() {
    if ((category_ != 0 && !metroActive() && !psfiveActive() && !flixActive()) || pending_launch_ || friends_open_ || category_menu_open_) return;
    dismissMenu();
    snitch_profile_focus_ = false;
    metro_tabs_ = false;
    quick_selected_ = 0;
    quick_open_ = true;
    audio_.quick();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeQuick() {
    quick_open_ = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::quickMove(int amount) {
    if (!quick_open_) return;
    const int next = std::clamp(quick_selected_ + amount, 0, quick_menu::count(quick_menu::store_slots) - 1);
    if (next == quick_selected_) { navigationBoundary(); return; }
    quick_selected_ = next;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::quickAccept() {
    if (!quick_open_) return;
    const auto action = quick_menu::action(quick_selected_, quick_menu::store_slots);
    closeQuick();
    if (action == quick_menu::Action::search) openSearch();
    else if (action == quick_menu::Action::library) openLibrary();
    else if (action == quick_menu::Action::store) openStores();
    else if (action == quick_menu::Action::settings) openSettings();
    else if (action == quick_menu::Action::controllers) openDevices(devices::Panel::controllers);
    else if (action == quick_menu::Action::audio) openDevices(devices::Panel::audio);
    else if (action == quick_menu::Action::downloads) openDownloads();
    else if (action == quick_menu::Action::widgets) toggleFriendsDrawer();
    else openPowerMenu();
}

void Launcher::togglePrivateCategory() {
    if (cropOpen() || devicesOpen() || artworkOpen() || pending_launch_) return;
    if (!private_shelf_ && private_category_.games.empty()) return;
    rememberPlace();
    closeQuick();
    closeCategoryMenu();
    const auto current = categories_.empty() ? std::string{} : categories_[category_].id;
    private_shelf_ = !private_shelf_;
    buildShelves();
    // Shown on the home screen: focus it at the bottom. Hidden: the same category, or the first.
    const std::string wanted = private_shelf_ && screen_ == ScreenMode::shelves ? "private" : current;
    size_t next = 0;
    for (size_t i = 0; i < categories_.size(); ++i) if (categories_[i].id == wanted) next = i;
    const auto shown = [](const Game& game) { return game.visibility == GameVisibility::public_game; };
    // Hiding again leaves the screens that only listed (or show) hidden games.
    const bool leave = !private_shelf_ &&
        ((screen_ == ScreenMode::library && library_category_ == "private") ||
         (screen_ == ScreenMode::details && !detail_games_.empty() && !shown(detail_games_[0])));
    if (screen_ == ScreenMode::details && leave) suspendDetails();
    if (screen_ == ScreenMode::library || screen_ == ScreenMode::platform_games) {
        if (leave) { showCategory(next); return; }
        // Grids stay where they are: the games appear or disappear in place.
        dismissMenu();
        const auto selected = cards_.empty() ? std::string{} : library_view::selectionKey(cards_[selected_].game);
        if (screen_ == ScreenMode::platform_games) {
            const auto found = platform_games_.find(nested_platform_);
            if (found == platform_games_.end()) { showCategory(next); return; }
            nested_games_.resize(1); // The back tile.
            nested_games_.insert(nested_games_.end(), found->second.begin(), found->second.end());
        }
        category_ = next;
        rebuildCards(false);
        if (const auto found = findCard(selected, true)) focusCard(*found);
        return;
    }
    if (screen_ == ScreenMode::shelves || leave) {
        dismissMenu();
        showCategory(next);
        if (xmbActive())
            for (size_t i = 0; i < xmb_top_.size(); ++i)
                if (xmb_top_[i].kind == GameKind::category && xmb_top_[i].app_id == next) { xmbFocus(i, -1); break; }
        audio_.drawer();
    } else category_ = next;
}

bool Launcher::categoryRailActive() const {
    return settings_.show_categories && settings_.home_layout == AppSettings::HomeLayout::shelves &&
           screen_ == ScreenMode::shelves && !desktop_mode_;
}

void Launcher::focusCategoryRail(bool focus) {
    if (!categoryRailActive() || pending_launch_) return;
    dismissMenu();
    closeCategoryMenu(); category_menu_progress_ = 0;
    if (category_rail_focus_ != focus) audio_.navigate();
    category_rail_focus_ = focus;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::openCategoryMenu() {
    if (categoryRailActive()) { focusCategoryRail(true); return; }
    if (metroActive() || snitchActive()) return; // Metro has tabs; snitch has no categories.
    if (psfiveActive() && (categories_.size() <= psfiveVisibleTabs() || psfiveStrip())) return; // The strip has every category.
    if (categories_.size() < 2 || pending_launch_) return;
    dismissMenu();
    if (friends_open_) closeFriends();
    category_cursor_ = static_cast<int>(category_);
    if (psfiveActive()) {
        category_cursor_ = std::max(static_cast<int>(psfiveVisibleTabs()), category_cursor_);
        psfive_action_ = -1;
        psfive_top_ = static_cast<int>(psfiveVisibleTabs());
    }
    category_menu_scroll_ = 0;
    ensureCategoryCursorVisible();
    if (psfiveActive()) {
        psfive_menu_motion_.row.snap(static_cast<float>(category_cursor_));
        psfive_menu_motion_.scroll.snap(category_menu_scroll_);
        psfive_menu_motion_.valid = true;
    }
    category_highlight_y_ = category_menu::top(renderer_.viewport().height, category_titles_.size()) +
                            category_cursor_ * category_menu::row_height - category_menu_scroll_;
    category_menu_open_ = true;
    audio_.drawer();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeCategoryMenu() {
    category_menu_open_ = false;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::categoryMenuHorizontal(int amount, bool repeated) {
    if (!category_menu_open_ || !amount) return;
    // PSfive's dropdown belongs to the top bar: leave it for the adjacent tab/icon,
    // without accepting the highlighted category. The side drawer keeps its keyboard shortcuts.
    if (psfiveActive()) navigate(amount);
    else if (amount > 0) categoryMenuAccept();
    else if (!repeated) closeCategoryMenu();
}

void Launcher::categoryMenuMove(int amount) {
    if (!category_menu_open_ || categories_.empty()) return;
    const int first = psfiveActive() ? static_cast<int>(psfiveVisibleTabs()) : 0;
    if (first >= static_cast<int>(categories_.size())) { closeCategoryMenu(); return; }
    const int next = std::clamp(category_cursor_ + amount, first, static_cast<int>(categories_.size()) - 1);
    if (next == category_cursor_) { navigationBoundary(); return; }
    category_cursor_ = next;
    ensureCategoryCursorVisible();
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::ensureCategoryCursorVisible() {
    if (psfiveActive()) {
        const auto layout = renderer_.psfiveMoreMenu(categories_.size(), psfiveVisibleTabs());
        const float row = layout.rowTop(category_cursor_, category_menu_scroll_);
        if (row < layout.top()) category_menu_scroll_ += row - layout.top();
        else if (row + layout.row_height > layout.bottom()) category_menu_scroll_ += row + layout.row_height - layout.bottom();
        category_menu_scroll_ = std::clamp(category_menu_scroll_, 0.0f, layout.maxScroll());
        return;
    }
    const float height = renderer_.viewport().height;
    const float row = category_menu::top(height, category_titles_.size()) +
                      category_cursor_ * category_menu::row_height - category_menu_scroll_;
    if (row < 55) category_menu_scroll_ += row - 58;
    else if (row + category_menu::row_height > height - 22)
        category_menu_scroll_ += row + category_menu::row_height - (height - 25);
    category_menu_scroll_ = std::clamp(category_menu_scroll_, 0.0f,
                             category_menu::maxScroll(height, category_titles_.size()));
}

void Launcher::scrollCategoryMenu(int amount) {
    if (!category_menu_open_) return;
    if (psfiveActive()) {
        const auto layout = renderer_.psfiveMoreMenu(categories_.size(), psfiveVisibleTabs());
        category_menu_scroll_ = std::clamp(category_menu_scroll_ + amount * layout.row_height, 0.0f, layout.maxScroll());
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const float height = renderer_.viewport().height;
    category_menu_scroll_ = std::clamp(category_menu_scroll_ + amount * category_menu::row_height,
                                      0.0f, category_menu::maxScroll(height, category_titles_.size()));
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::categoryMenuAccept() {
    if (!category_menu_open_) return;
    const auto next = static_cast<size_t>(category_cursor_);
    closeCategoryMenu();
    if (next >= categories_.size()) return;
    if (psfiveActive()) psfiveFocusTop(-1);
    if (next == category_) return;
    audio_.navigate();
    if (flixActive()) { // Every category is a row already: scroll to it.
        const auto row = std::find_if(flix_rows_.begin(), flix_rows_.end(), [&](const FlixRow& item) { return item.category == next; });
        if (row != flix_rows_.end()) flixFocus(static_cast<size_t>(row - flix_rows_.begin()), categories_[next].selected);
        return;
    }
    if (metroActive()) { // Every category is a page already: slide to it.
        const auto page = std::find_if(metro_pages_.begin(), metro_pages_.end(), [&](const MetroPage& item) { return item.category == next; });
        if (page != metro_pages_.end()) metroFocus(page->begin + std::min(categories_[next].selected, page->count - 1));
        return;
    }
    showCategory(next);
}

bool Launcher::overCategoryMenu(float mouse_x) const {
    return mouse_x / renderer_.viewport().scale <= 350.0f;
}

void Launcher::openContextMenu() {
    if (desktopActive()) {
        // The pad's menu button or a held Enter: the game's menu beside its poster.
        if (pending_launch_ || friends_open_ || quick_open_ || topModal() != Modal::none || cards_.empty()) return;
        if (desktop_menu_) { desktopCloseMenu(); return; }
        const auto poster = desktopLayout().poster(selected_, desktop_scroll_);
        desktopOpenGameMenu(poster.right - 14, poster.top + 18, true);
        return;
    }
    if (categoryRailActive() && category_rail_focus_) return;
    if (metroTabsFocused() || psfiveTopFocused()) return; // The tab strip / top bar has no game.
    // A dialog above the screen (Zidentyfikuj grę, a collection, the file picker) owns the input: a
    // menu opened beneath it would take D-pad and A while hidden (Ukryj, Wyklucz, Zamknij grę).
    // (The walkthrough opens it on purpose to show it.)
    if (topDialog() != Modal::none || exit_prompt_ || power_menu_) return;
    if (pending_launch_ || friends_open_ || category_menu_open_ || quick_open_ || cards_.empty() ||
        (screen_ != ScreenMode::shelves && screen_ != ScreenMode::platform_games && screen_ != ScreenMode::details && !gridScreen(screen_)) ||
        (libraryControls() && (library_controls_.focus >= 0 || library_controls_.open())) || searchBackFocused() ||
        (!isGame(cards_[selected_].game) && !studioMenuTile(cards_[selected_].game))) return;
    if (menu_open_) { dismissMenu(); return; }
    cancelEnterHold(); menu_open_ = true; menu_selected_ = 0;
    library_menu_from_action_ = psfiveActions(); // PSfive buttons: the menu unfolds from "…".
    audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::contextClick(float mouse_x, float mouse_y) {
    desktopEmbedMap(mouse_x, mouse_y); // Urządzenia, kadrowanie, plugin pages in a desktop sheet.
    if (desktopActive() && topModal() == Modal::none && !friends_open_ && !quick_open_ && !pending_launch_) {
        desktopContextClick(mouse_x, mouse_y);
        return;
    }
    if (libraryDialogOpen()) return;
    if (gogInstallOpen()) return;
    if (pending_launch_ || friends_open_ || category_menu_open_ || quick_open_ || exit_prompt_ || power_menu_ || tour_.open ||
        (screen_ != ScreenMode::shelves && screen_ != ScreenMode::platform_games && !gridScreen(screen_))) return;
    const auto view = renderer_.viewport(); const float x = mouse_x / view.scale, y = mouse_y / view.scale;
    for (size_t i = 0; i < cards_.size(); ++i) {
        if ((!isGame(cards_[i].game) && !studioMenuTile(cards_[i].game)) || !cardHit(i, x, y)) continue;
        library_controls_.focus = -1; closeLibraryPanel();
        dismissMenu(); focusCard(i); openContextMenu(); return;
    }
    // Beside the games: the launcher's own menu (Szukaj, Ustawienia, Zasilanie...), as Guide/Home
    // opens it; with only a mouse there is no other way to it in most home layouts.
    if (menu_open_) { dismissMenu(); return; }
    openGuideMenu();
}

void Launcher::menuMove(int amount) {
    if (!menu_open_) return;
    const int next = std::clamp(menu_selected_ + amount, 0, context_menu::rows(menuRows()) - 1);
    if (next == menu_selected_) { navigationBoundary(); return; }
    menu_selected_ = next;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::menuAccept() {
    if (!menu_open_) return;
    using context_menu::Item;
    const auto item = context_menu::item(menu_selected_, menuRows());
    if (item == Item::primary) {
        dismissMenu();
        if (!cards_.empty() && cards_[selected_].game.kind == GameKind::catalog) catalogAction(cards_[selected_].game);
        else launch();
        return;
    }
    if (cards_.empty()) return;
    const auto game = cards_[selected_].game;
    switch (item) {
    case Item::suspend: dismissMenu(); freezeGame(gameKey(game), true); break;
    case Item::close: dismissMenu(); closeGame(game); break;
    case Item::plugin: {
        const auto actions = pluginContext(game);
        const int index = menu_selected_ - context_menu::row(Item::plugin, menuRows());
        if (index >= 0 && index < static_cast<int>(actions.size())) pluginAction(actions[index], game);
        break;
    }
    case Item::details: openDetails(game); break;
    case Item::retry: retryGameArtwork(game); dismissMenu(); break;
    case Item::folder:
        dismissMenu();
        if (const auto folder = gameFolder(game); !folder.empty())
            if (execute_(hwnd_, GameAction{folder.wstring(), {}, {}, ActionKind::open_client}) <= 32)
                showNotification(NotificationKind::error, tr(L"Nie można otworzyć folderu"), folder.wstring());
        break;
    case Item::hide: dismissMenu(); toggleHidden(game); break;
    case Item::exclude: askExcludeGame(game); break;
    case Item::uninstall: uninstallGame(game); break;
    case Item::remove: askRemoveFromLibrary(game); break;
    case Item::launch: dismissMenu(); openLaunchEditor(game); break;
    case Item::compat: dismissMenu(); openGameCompatSettings(game); break;
    case Item::shortcuts: dismissMenu(); openGameShortcuts(game); break;
    case Item::metadata: dismissMenu(); if (desktop_mode_) desktopMetaOpen(game); else openMetadata(game); break;
    case Item::aurelia: dismissMenu(); openAurelia(&game); break;
    case Item::identify: dismissMenu(); openIdentifyFor(game); break;
    case Item::steam_account: cycleSteamAccount(game); break; // The menu stays: the row shows the next choice.
    // A studio's logo: moved, the menu stays with it (its tile keeps the focus); hidden, the menu closes.
    case Item::studio_earlier: case Item::studio_later: moveStudio(text_encoding::utf8(game.platform), item == Item::studio_earlier ? -1 : 1); break;
    case Item::studio_hide: dismissMenu(); hideStudio(text_encoding::utf8(game.platform)); break;
    case Item::emulator: cycleGameEmulator(game); break; // The menu stays: the row shows the next choice.
    default: openArtworkBrowser(game); break;
    }
}

void Launcher::dismissMenu() {
    enter_held_ = false;
    menu_open_ = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}
