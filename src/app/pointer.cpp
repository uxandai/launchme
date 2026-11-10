// Mouse clicks and hover hit testing across every screen and overlay.
#include "launcher.hpp"
#include "../ui/category_menu.hpp"
#include "../ui/category_rail.hpp"
#include "../ui/context_menu.hpp"
#include "../ui/exit_prompt.hpp"
#include "../ui/friends_layout.hpp"
#include "../ui/game_details_layout.hpp"
#include "../ui/grid_layout.hpp"
#include "../ui/psfive_layout.hpp"
#include "../ui/snitch_layout.hpp"
#include "../ui/quick_menu.hpp"
#include "../ui/tour_view.hpp"

#include <cmath>

// The grid's Back circle; the stores' catalog keeps it where a plugin page has it.
bool Launcher::gridBackHit(float x, float y) const {
    if (screen_ == ScreenMode::store) return plugin_ui::Layout::backBox(renderer_.viewport().width).contains(x, y);
    return grid_layout::hitBack(x, y);
}

void Launcher::click(float mouse_x, float mouse_y, bool double_click) {
    if (launchAnimationActive()) return;
    // A TV screen inside the desktop window: its window buttons stay above every layer.
    if (!double_click && windowStripClick(mouse_x, mouse_y)) return;
    if (desktopEmbedded()) {
        // The sheet around Urządzenia, kadrowanie or a plugin page: its ✕ is Back; the screen beneath
        // works in its own canvas.
        const float scale = desktopScale();
        const auto view = renderer_.desktopViewport();
        if (topModal() == Modal::none && !friends_open_ && desktop_ui::embedSheet(view.width, view.height).close().contains(mouse_x / scale, mouse_y / scale)) {
            if (!double_click) back();
            return;
        }
        desktopEmbedMap(mouse_x, mouse_y);
    }
    const auto view = renderer_.viewport();
    const float x = mouse_x / view.scale, y = mouse_y / view.scale; // Logical coordinates.
    const auto modal = topModal();
    // On-screen keyboard keys; a click elsewhere goes to the layer beneath (and may close its field).
    if (keyboard_.open && modal != Modal::exit_prompt && modal != Modal::power_menu && keyboardClick(x, y)) return;
    // Ustawienia or Profil in the library's place: the sidebar and top bar around them still work.
    if (desktopChromeClick(mouse_x, mouse_y, double_click)) return;
    // Desktop mode draws some of them as desktop dialogs: their own controls (desktop_dialogs.cpp).
    if (modal != Modal::none && desktopModalClick(mouse_x, mouse_y, double_click)) return;
    // The topmost layer takes the click. The second click of a double-click never confirms a dialog
    // on its own (e.g. "Usuń" in the game editor).
    switch (modal) {
    case Modal::none: break;
    case Modal::companion_picker: if (!double_click) companionPickerClick(x, y); return;
    case Modal::theme_studio: if (!double_click) { themeStudioClick(x, y); if (theme_.dragging) SetCapture(hwnd_); } return;
    case Modal::tarot: if (!double_click) tarotClick(x, y); return;
    case Modal::profile: if (!double_click) { const auto [px, py] = profilePoint(mouse_x, mouse_y); profileClick(px, py); } return;
    case Modal::game_case: if (!double_click) gameCaseClick(x, y); return;
    case Modal::aurelia: if (!double_click) aureliaClick(x, y); return;
    case Modal::whats_new: if (!double_click) whatsNewClick(x, y); return;
    case Modal::exit_prompt: if (!double_click) exitPromptClick(x, y); return;
    case Modal::power_menu: if (!double_click) powerMenuClick(x, y); return;
    case Modal::tour: if (!double_click) tourClick(x, y); return;
    case Modal::file_browser: fileBrowserClick(x, y, double_click); return;
    case Modal::library_dialog: if (!double_click) collectionDialogClick(x, y); return;
    case Modal::custom_editor: if (!double_click) customEditorClick(x, y); return;
    case Modal::metadata: if (!double_click) metadataClick(x, y); return;
    case Modal::bulk_covers: {
        const int hit = bulk_covers_ui::Layout{view.width, view.height, bulk_ui_.buttons()}.hit(x, y);
        if (hit >= 0 && !double_click) { bulk_ui_.focus = hit; bulkCoversKey(VK_RETURN, false); }
        return;
    }
    case Modal::identify: if (!double_click) identifyClick(x, y); return;
    case Modal::gog_install: {
        const auto row = gog_install_ui::Layout{view.width, view.height, gog_install_ui::helperInstall(gog_install_ui_.game.source) || gog_install_ui_.steam}.hit(x, y);
        if (!double_click && row >= 0) { gog_install_ui_.row = row; gogInstallAccept(); }
        return;
    }
    }
    if (pending_launch_) return;
    if (desktopActive() && !friends_open_ && !quick_open_ && !menu_open_) { desktopClick(mouse_x, mouse_y, double_click); return; }
    if (desktopSheet() && !friends_open_ && !menu_open_) { desktopSettingsClick(mouse_x, mouse_y, double_click); return; } // Desktop dialogs.
    if (glanceOpen() && !friends_open_) {
        if (!double_click) glanceClick(x, y);
        return;
    }
    // Its free-standing tile or Flix pill (Metro and XMB: a tile of the layout, below).
    if (glanceAvailable() && home_widgets::glanceArea(homeWidgetSpots(), glance_hover_motion_.value).contains(x, y)) {
        if (!double_click) openGlance();
        return;
    }
    if (companionOpen() && !friends_open_) {
        if (!double_click) companionClick(x, y);
        return;
    }
    // Her tile, her head above it (or her Flix pill) and her speech bubble open the chat.
    if (companionAvailable() && (home_widgets::companionArea(homeWidgetSpots()).contains(x, y) || renderer_.companionBubble().contains(x, y))) {
        if (!double_click && !companionLinkClick(x, y)) openCompanion();
        return;
    }
    if (screen_ == ScreenMode::plugin_page && !friends_open_) {
        if (!double_click) pluginPageClick(x, y);
        return;
    }
    if (downloadsOpen() && !friends_open_ && !gogInstallOpen()) {
        if (!double_click) downloadsClick(x, y);
        return;
    }
    if (double_click && GetTickCount64() < mouse_transition_until_) return;
    // A double-click that closes an editor/menu must not also launch the tile
    // exposed beneath it. Keep this gesture boundary shared by all game views.
    struct GestureBoundary {
        ScreenMode& screen; bool& menu; library_ui::Panel& panel; ULONGLONG& until;
        ScreenMode previous_screen; bool previous_menu; library_ui::Panel previous_panel;
        ~GestureBoundary() {
            if (screen != previous_screen || (previous_menu && !menu) ||
                (previous_panel != library_ui::Panel::none && panel != previous_panel)) until = GetTickCount64() + GetDoubleClickTime();
        }
    } boundary{screen_, menu_open_, library_controls_.panel, mouse_transition_until_, screen_, menu_open_, library_controls_.panel};
    if (cropOpen()) {
        if (!double_click) cropClick(x, y);
        return;
    }
    if (devicesOpen()) {
        if (!double_click) deviceClick(x, y);
        return;
    }
    if (artworkOpen()) {
        artworkClick(x, y, double_click);
        return;
    }
    if (screen_ == ScreenMode::settings) {
        if (friends_open_) { closeFriends(); return; }
        settingsClick(x, y);
        return;
    }
    if (snitchClick(x, y, double_click)) return;
    // Flix → Pasek boczny: an icon acts at once (no menu, drawer or prompt over the page).
    if (!quick_open_ && !friends_open_ && !menu_open_ && !category_menu_open_ && !exit_prompt_)
        if (const int rail = flixRailAt(x, y); rail >= 0) { flixRailAccept(rail); return; }
    if (emptyHomeClick(x, y, double_click)) return;
    if (quick_open_) {
        if (snitchActive()) { closeQuick(); return; }
        if (y >= view.height - 112 && y <= view.height - 12) {
            for (int i = 0; i < quick_menu::count(quick_menu::store_slots); ++i) {
                if (std::abs(x - quick_menu::center(view.width, i, quick_menu::store_slots)) < 43) {
                    quick_selected_ = i;
                    quickAccept();
                    return;
                }
            }
        }
        closeQuick();
        return;
    }
    if (category_menu_open_) {
        if (psfiveActive()) {
            if (double_click) return;
            // Hover has already opened More; clicking its label must keep the list open.
            if (renderer_.psfiveTabAt(x, y) == static_cast<int>(psfiveVisibleTabs())) return;
            const auto layout = renderer_.psfiveMoreMenu(categories_.size(), psfiveVisibleTabs());
            const int row = layout.hit(x, y, psfiveMenuScroll(), category_menu_progress_);
            if (row >= 0) { category_cursor_ = row; categoryMenuAccept(); }
            else closeCategoryMenu();
            return;
        }
        if (!overCategoryMenu(mouse_x)) { closeCategoryMenu(); return; }
        const int row = category_menu::hit(y, view.height, categories_.size(), category_menu_scroll_);
        if (row >= 0) {
            category_cursor_ = row;
            categoryMenuAccept();
        }
        return;
    }
    if (friends_open_) {
        if (!overFriends(mouse_x)) { closeFriends(); return; }
        // The first click already acted; its double-click must not open what appeared beneath.
        if (double_click) return;
        const auto frame = friendsFrame();
        const float drawer_left = frame.list_left;
        if (plugin_drawer_active_) { pluginPageClick(x - drawer_left, y); return; }
        if (!friends_list_) {
            dashboardPointerDown(x, y);
            return;
        }
        // The Znajomi page: its own corner; the list stays beside the conversation.
        const auto [px, py] = friendsPoint(mouse_x, mouse_y);
        const bool in_chat = frame.docked ? px >= frame.chat.left : true;
        if (friend_chat_.open && in_chat) { friendChatClick(px - frame.chat.left, py); return; } // The conversation's Back, field and Send.
        if (frame.docked && in_chat) return;
        if (friends_layout::listBack(drawer_left, frame.docked).contains(px, py)) { friendsBack(); return; }
        if (py < 104) return; // Rows scrolled under the header are hidden.
        const auto index = friends_layout::hit(friends_.friends, py + friends_scroll_);
        if (index) { friends_cursor_ = *index; friends_back_ = false; friendsChat(); }
        return;
    }
    if (gridScreen(screen_) && gridBackHit(x, y)) { back(); return; }
    if (!menu_open_ && libraryClick(x, y, double_click)) return;
    if (psfiveClick(x, y, double_click)) return; // PSfive "Graj" / "…".
    if (achievementsOpen()) { if (!double_click) achievementsClick(x, y); return; }
    if (!menu_open_ && !library_controls_.open() && screen_ != ScreenMode::details && library_view::metadata(libraryPresentation()) &&
        !ratingUrl().empty() && renderer_.ratingLink().contains(x, y)) { if (!double_click) openRatingSource(); return; }
    if (screen_ == ScreenMode::details && !menu_open_) {
        if (double_click || cards_.empty()) return; // A double-click on a Store tile cannot also press the detail action.
        // Every target from the shared geometry; the click focuses it and acts as Enter would.
        const int action = details_layout::Layout{view.width, view.height}.hit(detailsActions(), x, y);
        if (action < -1) return;
        detail_action_ = action;
        launch();
        return;
    }
    if (menu_open_ && !cards_.empty() && menuDialog()) {
        // Duże menu gry: a row runs its action, a click outside the dialog closes it.
        const context_menu::Dialog dialog{view.width, view.height, context_menu::rows(menuRows())};
        if (const int row = dialog.hit(x, y, dialog.first(menu_selected_)); row >= 0) { menu_selected_ = row; menuAccept(); return; }
        if (!dialog.panel.contains(x, y)) dismissMenu();
        return;
    }
    if (menu_open_ && !cards_.empty()) {
        const auto& active = cards_[selected_];
        const auto rows = menuRows();
        const bool beside = menuBeside();
        const auto menu = context_menu::bounds(active, rows, beside, view.width, view.height, libraryMenuAnchor());
        const float menu_y = menu.top;
        if (menu.contains(x, y)) {
            if (y >= menu_y + 8) {
                const int row = static_cast<int>((y - menu_y - 8) / 53);
                if (row < context_menu::rows(rows)) {
                    menu_selected_ = row;
                    menuAccept();
                }
            }
            return;
        }
        dismissMenu();
        if (screen_ == ScreenMode::details) return;
    }
    if (categoryRailActive()) {
        const auto row = category_rail::Layout{view.width, view.height, categories_.size(), category_}.hit(x, y);
        if (row >= 0) {
            if (static_cast<size_t>(row) != category_) { showCategory(static_cast<size_t>(row)); audio_.navigate(); }
            focusCategoryRail(true);
            return;
        }
    }
    if (storeTabClick(x, y)) return; // The Stores' tabs on the catalog screen.
    if (homeTabsClick(x, y)) return; // Metro / Flix "Pasek kategorii".
    if (metroActive()) { // A Metro tab: its page, focus on the page's tile.
        if (const int tab = renderer_.metroTabAt(x, y); tab >= 0 && static_cast<size_t>(tab) < metro_pages_.size()) {
            metroFocusTabs(false);
            if (static_cast<size_t>(tab) != metro_page_) homePage(tab - static_cast<int>(metro_page_));
            return;
        }
    }
    for (size_t i = 0; i < cards_.size(); ++i) {
        const auto& card = cards_[i];
        if (cardHit(i, x, y)) {
            if (categoryRailActive()) focusCategoryRail(false);
            if (xmbActive()) { xmbClick(i, double_click); return; }
            if (flixActive()) { flixClick(i, double_click); return; }
            if (metroActive()) { metroClick(i, double_click); return; }
            if (psfiveActive()) { psfiveClickCard(i, double_click); return; }
            if (screen_ == ScreenMode::store) { selected_ = i; openDetails(card.game); return; }
            if (card.game.kind == GameKind::all) { openLibrary(); return; }
            if (specialTile(card.game)) { focusCard(i, false); launch(); return; } // Glance, Tarot, Towarzyszka, Dodaj grę: one click.
            if (card.game.kind == GameKind::category && shelfAllTile()) { // The default layout's "Wszystkie".
                categories_[category_].selected = i;
                openCategoryLibrary(card.game.source_id);
                return;
            }
            if (card.game.kind == GameKind::platform) {
                if (screen_ == ScreenMode::shelves) categories_[category_].selected = i;
                openPlatform(card.game.platform);
                return;
            }
            if (card.game.kind == GameKind::pc_platform) {
                if (screen_ == ScreenMode::shelves) categories_[category_].selected = i;
                openPcLibrary(card.game.source);
                return;
            }
            if (card.game.kind == GameKind::back) { back(); return; }
            if (i == selected_ && double_click) accept();
            else if (i != selected_) {
                focusCard(i, false);
                audio_.navigate();
            }
            return;
        }
    }
}

bool Launcher::hoverButton(float mouse_x, float mouse_y) const {
    if (windowStripHit(mouse_x, mouse_y) >= 0) return true;
    desktopEmbedMap(mouse_x, mouse_y); // Urządzenia, kadrowanie, plugin pages in a desktop sheet.
    const auto view = renderer_.viewport();
    const float x = mouse_x / view.scale, y = mouse_y / view.scale;
    const auto modal = topModal();
    if (keyboard_.open && modal != Modal::exit_prompt && modal != Modal::power_menu) {
        const osk::Layout layout{view.width, view.height};
        if (layout.panel().contains(x, y)) return layout.hit(keyboard_, x, y)[0] >= 0;
    }
    // The topmost layer: a hand where its click acts (as click() routes it), nothing beneath it.
    switch (modal) {
    case Modal::none: break;
    case Modal::exit_prompt: return exit_prompt::Layout{view.width, view.height}.hit(x, y) >= 0;
    case Modal::power_menu: return powerMenuGeometryFor(view.width, view.height).hit(x, y) >= 0;
    case Modal::tour: {
        const tour::Layout layout{view.width, view.height, tourCardAtBottom(), tour::Layout::firstFor(static_cast<int>(settings_.home_layout))};
        return layout.hitButton(x, y, tour_.step > 0) >= 0 ||
               (tour_.step == static_cast<int>(tour::Step::layout) && layout.hitTile(x, y) >= 0);
    }
    case Modal::file_browser: {
        const auto& ui = file_browser_;
        const file_browser::Layout layout{view.width, view.height, ui.request.images};
        return layout.back().contains(x, y) || layout.hitButton(x, y, static_cast<int>(ui.buttons().size())) >= 0 ||
               layout.hitPlace(x, y, static_cast<int>(ui.places.size())) >= 0 || layout.hitRow(x, y, static_cast<int>(ui.entries.size()), ui.scroll) >= 0;
    }
    case Modal::library_dialog:
        return library_manager_ui::Layout::of(view.width, view.height, library_dialog_).hit(library_dialog_, x, y) != -3;
    case Modal::custom_editor: {
        const auto hit = custom_game_ui::Layout::of(custom_editor_, view.width, view.height).hit(x, y, custom_editor_.editing());
        return hit && custom_game_ui::field(*hit) < 0;
    }
    case Modal::metadata:
        if (desktop_meta_.find.open) return meta_find::Layout{view.width, view.height}.hit(x, y, desktop_meta_.find) >= 0;
        return desktop_ui::MetaLayout{view.width, view.height}.hit(x, y, desktop_meta_.identify) >= desktop_ui::meta_fields;
    case Modal::bulk_covers: return bulk_covers_ui::Layout{view.width, view.height, bulk_ui_.buttons()}.hit(x, y) >= 0;
    case Modal::identify: {
        const identify_ui::Layout layout{view.width, view.height};
        if (!identify_state_.manual) return layout.back().contains(x, y) || (!identify_state_.applying && layout.manualButton().contains(x, y));
        if (identify_state_.menu) return layout.menuHit(x, y, static_cast<int>(identify_ui::providers(identify_state_.emulated).size())) >= 0;
        return layout.back().contains(x, y) || layout.source().contains(x, y) || layout.search().contains(x, y) || layout.hit(x, y, identify_state_) >= 0;
    }
    case Modal::gog_install: return gog_install_ui::Layout{view.width, view.height, gog_install_ui::helperInstall(gog_install_ui_.game.source) || gog_install_ui_.steam}.hit(x, y) >= 0;
    case Modal::companion_picker: return companion_ui::PickerLayout{view.width, view.height}.hit(companion_.picker, x, y) != companion_ui::no_hit;
    case Modal::theme_studio: return themeStudioHot(x, y);
    case Modal::tarot: return tarotPointerOver(x, y);
    case Modal::profile: { const auto [px, py] = profilePoint(mouse_x, mouse_y); return profilePointerOver(px, py); }
    case Modal::game_case: return gameCasePointerOver(x, y);
    case Modal::aurelia: return aurelia_ui::Layout{view.width, view.height}.hit(aurelia_ui_, x, y) != -9;
    case Modal::whats_new: return update_ui::WhatsNewLayout{view.width, view.height}.ok().contains(x, y);
    }
    if (launchAnimationActive()) return false;
    if (glanceAvailable() && home_widgets::glanceArea(homeWidgetSpots(), glance_hover_motion_.value).contains(x, y)) return true;
    if (glanceOpen() && !friends_open_) return glance_ui::Layout{view.width, view.height}.hit(x, y, glance_) != -100;
    if (companionAvailable() && (home_widgets::companionArea(homeWidgetSpots()).contains(x, y) || renderer_.companionBubble().contains(x, y))) return true;
    if (companionOpen() && !friends_open_ && !keyboard_.open) {
        if (companion_.chat.feed) return companion_ui::FeedLayout{view.width, view.height}.hit(x, y, companion_.feed.view) != companion_ui::FeedLayout::no_hit;
        if (companion_.chat.mind)
            return companion_.chat.mind_hover != companion_ui::no_hit ||
                   companion_ui::Layout{view.width, view.height}.mindClose().contains(x, y);
        return companion_.chat.viewer || companion_ui::Layout{view.width, view.height}.hit(x, y, companion_.chat) != companion_ui::no_hit ||
               (!companion_.chat.menu && !companion_.chat.confirm && (renderer_.companionPhoto().contains(x, y) || !renderer_.companionLinkAt(x, y).empty() || !renderer_.companionVideoAt(x, y).empty()));
    }
    if (metroActive() && !friends_open_ && !menu_open_ && !quick_open_ && renderer_.metroTabAt(x, y) >= 0) return true;
    if (homeTabsShown() && !friends_open_ && !menu_open_ && !quick_open_ && !category_menu_open_) {
        if (const auto offset = library_tabs::home::hit(x, y, view.width, homeTabIndex()); offset && *offset &&
            homeTabIndex() + *offset >= 0 && homeTabIndex() + *offset < static_cast<int>(homeTabs().size())) return true;
    }
    if (screen_ == ScreenMode::plugin_page && !friends_open_) {
        const plugin_ui::Layout layout{view.width, view.height, false, plugin_page_.dialog};
        return layout.back(x, y) || layout.hit(plugin_page_, x, y) >= 0;
    }
    if (downloadsOpen() && !friends_open_) return downloadsHover(x, y);
    if (cropOpen()) {
        const auto layout = cropLayout();
        if (layout.back().contains(x, y)) return true;
        return !crop_state_.loading && crop_state_.error.empty() &&
            ((crop_state_.adjustable() && layout.preview().contains(x, y)) || layout.button(0).contains(x, y) || layout.button(1).contains(x, y));
    }
    if (devicesOpen()) {
        const device_ui::Layout layout{view.width, view.height, device_state_.panel};
        if (device_state_.gallery()) return layout.back().contains(x, y) || layout.hitGallery(device_state_, x, y) >= 0;
        return layout.back().contains(x, y) || (device_state_.panel == devices::Panel::audio ? device_ui::AudioLayout{view.width, view.height}.hit(device_state_, x, y) : layout.hit(x, y, device_state_.rows.size(), device_state_.scroll)) >= 0;
    }
    if (artworkOpen()) {
        const auto layout = artwork_state_.layout(view.width, view.height);
        if (artwork_state_.local_open) return layout.hitLocal(x, y) >= 0;
        if (artwork_state_.provider_open) return layout.hitProvider(x, y) >= 0;
        if (layout.backButton().contains(x, y)) return true;
        if (artwork_state_.applying) return false;
        if (layout.localButton().contains(x, y) || (!artwork_state_.query.empty() && layout.clearButton().contains(x, y))) return true;
        if (artwork_state_.croppable() && layout.cropButton().contains(x, y)) return true;
        if (artwork_state_.providerChoice() && layout.providerButton().contains(x, y)) return true;
        for (int i = 0; i < artwork_state_.railCount(); ++i) if (layout.base().rail(i).contains(x, y)) return true;
        return (artwork_state_.page > 0 && layout.pageButton(false).contains(x, y)) ||
               (artwork_state_.more && layout.pageButton(true).contains(x, y)) ||
               layout.hit(x, y, artwork_state_.assets.size(), artwork_state_.scroll) >= 0;
    }
    if (settingsOpen() && !friends_open_) {
        const auto layout = settings_ui::Layout::of(view.width, view.height, settings_state_, settings_rows_);
        if (editingSecret()) return layout.secretSave().contains(x, y) || layout.secretCancel().contains(x, y) ||
                                    (layout.import_help && layout.secretBrowse().contains(x, y));
        if (settings_state_.editing && !settings_rows_.empty() && settings_rows_[settings_state_.row()].picker())
            return layout.hitChoice(x, y, static_cast<int>(settings_rows_[settings_state_.row()].pickList(settings_state_.tile).size()), settings_state_.choice) >= 0;
        return layout.hitRail(x, y) >= 0 || layout.backButton().contains(x, y) ||
               (settings_save_failed_ && layout.saveStatus().contains(x, y)) ||
               layout.hitRow(x, y, static_cast<int>(settings_rows_.size()), settings_state_.offset()) >= 0;
    }
    if (snitchActive() && !friends_open_ && !menu_open_ && !pending_launch_ &&
        (snitch_layout::hitDock(view.width, view.height, x, y, quick_menu::store_slots) >= 0 ||
         snitch_layout::profile().contains(x, y))) return true;
    if (flixRailAt(x, y) >= 0) return true;
    if (emptyHomeActive() && emptyHomeLayout().hit(x, y) >= 0) return true;
    if (quick_open_) {
        if (snitchActive()) return false;
        for (int i = 0; i < quick_menu::count(quick_menu::store_slots); ++i)
            if (std::abs(x - quick_menu::center(view.width, i, quick_menu::store_slots)) < 43 &&
                y >= view.height - 112 && y <= view.height - 12) return true;
        return false;
    }
    if (friends_open_ && overFriends(mouse_x)) {
        const float drawer_left = view.width - friends_layout::drawer_width;
        if (plugin_drawer_active_) {
            const plugin_ui::Layout layout{friends_layout::drawer_width, view.height, true};
            return layout.back(x - drawer_left, y) || layout.hit(plugin_page_, x - drawer_left, y) >= 0;
        }
        if (!friends_list_) { // Temperature tiles only show values.
            const auto tile = dashboardLayout().hit(drawer_left, x, y, dashboard_scroll_, view.height);
            using friends_layout::dashboard::Tile;
            constexpr int plugins = friends_layout::dashboard::tile_count;
            return tile == friends_layout::dashboard::reorganize || (dashboard_editing_ && tile >= 0) ||
                tile == static_cast<int>(Tile::steam) || tile == static_cast<int>(Tile::discord) || tile == static_cast<int>(Tile::profile) ||
                (tile >= plugins && tile < plugins + static_cast<int>(plugin_widgets_.size()));
        }
        const auto frame = friendsFrame();
        const auto [px, py] = friendsPoint(mouse_x, mouse_y);
        if (friend_chat_.open && (!frame.docked || px >= frame.chat.left)) return frame.chat.hit(px, py) != 0;
        return friends_layout::listBack(frame.list_left, frame.docked).contains(px, py) ||
               (py >= 104 && px < frame.list_left + friends_layout::drawer_width && friends_layout::hit(friends_.friends, py + friends_scroll_).has_value());
    }
    if (category_menu_open_ && psfiveActive()) {
        return renderer_.psfiveMoreMenu(categories_.size(), psfiveVisibleTabs()).hit(x, y, psfiveMenuScroll(), category_menu_progress_) >= 0;
    }
    if (category_menu_open_ && overCategoryMenu(mouse_x)) {
        return category_menu::hit(y, view.height, categories_.size(), category_menu_scroll_) >= 0;
    }
    if (gridScreen(screen_) && gridBackHit(x, y) && !(libraryControls() && library_controls_.open())) return true;
    if (libraryControls()) {
        const library_ui::Layout layout{view.width};
        if (library_controls_.panel == library_ui::Panel::filters) return layout.hitChoice(x, y) >= 0;
        if (library_controls_.panel == library_ui::Panel::views) {
            const float panel_x = x - library_ui::views::offset(library_views_progress_);
            return library_ui::views::hitOption(panel_x, y, library_controls_, view.height, library_dropdown_progress_) >= 0 ||
                (library_controls_.view_dropdown && library_ui::views::hit(panel_x, y, view.height, library_dropdown_progress_) >= 0);
        }
        if (library_controls_.search && layout.field().contains(x, y)) return true;
        if (grid_scroll_ < 80 || screen_ == ScreenMode::platform_games || library_controls_.search || library_view::pinnedHeader(libraryView()))
            for (const int i : {library_ui::filters_button, library_ui::search_button, library_ui::views_button})
                if ((i == library_ui::filters_button || screen_ == ScreenMode::library) && layout.button(i).contains(x, y)) return true;
        if (!menu_open_ && libraryActions() && !cards_.empty()) {
            const int action = library_presentation::Layout{view.width, view.height}.hitAction(x, y);
            if (action >= 0) return action != 0 || actionEnabled(cards_[selected_].game);
        }
        if (!menu_open_ && libraryOverview() && !cards_.empty()) {
            const int button = overview_layout::Panel{view.width, view.height}.hitButton(x, y);
            if (button >= 0) return button == 0 || actionEnabled(cards_[selected_].game);
        }
    }
    if (achievementsOpen()) {
        const achievements_ui::Layout layout{view.width, view.height};
        if (achievements_.card) return layout.cardBack().contains(x, y);
        return layout.back().contains(x, y) || layout.hit(x, y, achievements_.items.size(), achievements_.scroll) >= 0; // A row opens its card.
    }
    if (screen_ == ScreenMode::details && !menu_open_) {
        const int action = details_layout::Layout{view.width, view.height}.hit(detailsActions(), x, y);
        return action >= -1 && (action != 0 || (!cards_.empty() && actionEnabled(cards_[0].game)));
    }
    if (menu_open_ && !cards_.empty() && menuDialog()) {
        const context_menu::Dialog dialog{view.width, view.height, context_menu::rows(menuRows())};
        return dialog.hit(x, y, dialog.first(menu_selected_)) >= 0;
    }
    if (menu_open_ && !cards_.empty()) {
        const auto& active = cards_[selected_];
        const auto rows = menuRows();
        const bool beside = menuBeside();
        if (context_menu::bounds(active, rows, beside, view.width, view.height, libraryMenuAnchor()).contains(x, y))
            return true;
    }
    if (categoryRailActive() && category_rail::Layout{view.width, view.height, categories_.size(), category_}.hit(x, y) >= 0) return true;
    if (psfiveActive() && !menu_open_ && ((psfiveButtons() && psfive_layout::hitAction(x, y, view.height, psfiveScale()) >= 0) ||
        psfive_layout::hitTop(x, y, view.width, settings_.psfive_clock) >= 0 || renderer_.psfiveTabAt(x, y) >= 0)) return true;
    for (size_t i = 0; i < cards_.size(); ++i) if (cardHit(i, x, y)) return true;
    if (library_view::metadata(libraryPresentation()) && !ratingUrl().empty() && renderer_.ratingLink().contains(x, y)) return true;
    return false;
}

// Hover across screens, leaving the window, and the end of a mouse drag.
void Launcher::mouseMove(float mouse_x, float mouse_y) {
    if (topModal() == Modal::aurelia) {
        if (desktop_mode_ && desktopModalMove(mouse_x, mouse_y)) return; // Drawn as a desktop dialog.
        const auto view = renderer_.viewport();
        const int hit = aurelia_ui::Layout{view.width, view.height}.hit(aurelia_ui_, mouse_x / view.scale, mouse_y / view.scale);
        if (aurelia_ui_.hover != hit) { aurelia_ui_.hover = hit; InvalidateRect(hwnd_, nullptr, FALSE); }
        return;
    }
    if (topModal() == Modal::theme_studio) {
        const auto view = renderer_.viewport();
        if (theme_.dragging) themeStudioDrag(mouse_x / view.scale, mouse_y / view.scale);
        else themeStudioHover(mouse_x / view.scale, mouse_y / view.scale);
        return;
    }
    companionPointer(mouse_x, mouse_y);
    if (topModal() == Modal::companion_picker) {
        const auto view = renderer_.viewport();
        const int hit = companion_ui::PickerLayout{view.width, view.height}.hit(companion_.picker, mouse_x / view.scale, mouse_y / view.scale);
        if (companion_.picker.hover != hit) { companion_.picker.hover = hit; InvalidateRect(hwnd_, nullptr, FALSE); }
        return;
    }
    if (topModal() == Modal::tarot) {
        const auto view = renderer_.viewport();
        tarotPointer(mouse_x / view.scale, mouse_y / view.scale);
        return;
    }
    if (topModal() == Modal::game_case) {
        const auto view = renderer_.viewport();
        gameCasePointer(mouse_x / view.scale, mouse_y / view.scale);
        return;
    }
    if (topModal() == Modal::profile) {
        if (desktopChromeMove(mouse_x, mouse_y)) { profilePointer(-1e6f, -1e6f); return; } // The sidebar's hover; the page's goes.
        const auto [px, py] = profilePoint(mouse_x, mouse_y);
        profilePointer(px, py);
        return;
    }
    if (topModal() == Modal::metadata) {
        const auto view = renderer_.viewport();
        metadataPointer(mouse_x / view.scale, mouse_y / view.scale);
        return;
    }
    if (desktopTextDrag(mouse_x)) return; // Selecting in a desktop field.
    using namespace settings_ui;
    if (desktop_mode_) {
        if (desktopModalMove(mouse_x, mouse_y)) return; // A desktop dialog over everything else.
        const int strip = windowStripHit(mouse_x, mouse_y);
        if (strip != desktop_strip_hover_) { desktop_strip_hover_ = strip; InvalidateRect(hwnd_, nullptr, FALSE); }
        // The library: its hover, drags and menus (open dialogs and the drawer keep theirs).
        if (desktopActive() && ((topModal() == Modal::none && !friends_open_ && !quick_open_) || desktop_drag_.kind != DesktopDrag::Kind::none)) {
            desktopMouseMove(mouse_x, mouse_y);
            return;
        }
        if (friendsDocked() && desktopChromeMove(mouse_x, mouse_y)) return; // Znajomi: the sidebar's hover beside the page.
        if (desktopActive() && !(desktop_hover_ == desktop_ui::Hit{})) { desktop_hover_ = {}; InvalidateRect(hwnd_, nullptr, FALSE); }
        if (desktopSheet() && topModal() == Modal::none && !friends_open_ && desktop_menu_) { desktopMouseMove(mouse_x, mouse_y); return; } // Szczegóły' "…" menu.
        if (desktopSheet() && topModal() == Modal::none && !friends_open_) { desktopChromeMove(mouse_x, mouse_y); desktopSettingsMove(mouse_x, mouse_y); return; }
        if (desktopEmbedded()) {
            // The sheet's ✕, then the screen beneath in its own canvas.
            const float scale = desktopScale();
            const auto view = renderer_.desktopViewport();
            const bool close = desktop_ui::embedSheet(view.width, view.height).close().contains(mouse_x / scale, mouse_y / scale);
            const desktop_ui::SheetHit hit = close ? desktop_ui::SheetHit{desktop_ui::SheetHit::Kind::close} : desktop_ui::SheetHit{};
            if (!(hit == desktop_sheet_hover_)) { desktop_sheet_hover_ = hit; InvalidateRect(hwnd_, nullptr, FALSE); }
            desktopEmbedMap(mouse_x, mouse_y);
        }
    }
    const auto glance_view = renderer_.viewport();
    const auto widget_spots = homeWidgetSpots();
    const bool glance_hover = glanceAvailable() && home_widgets::glanceArea(widget_spots, glance_hover_motion_.value).contains(mouse_x / glance_view.scale, mouse_y / glance_view.scale);
    if (glance_hover_ != glance_hover) { glance_hover_ = glance_hover; InvalidateRect(hwnd_, nullptr, FALSE); }
    {
        // Pasek boczny: the icon under the pointer lights up and names itself.
        const int rail = flixRailAt(mouse_x / glance_view.scale, mouse_y / glance_view.scale);
        if (flix_rail_hover_ != rail) { flix_rail_hover_ = rail; InvalidateRect(hwnd_, nullptr, FALSE); }
        // The empty home's buttons: the one under the pointer lights up.
        const int empty = emptyHomeActive() ? emptyHomeLayout().hit(mouse_x / glance_view.scale, mouse_y / glance_view.scale) : -1;
        if (empty_hover_ != empty) { empty_hover_ = empty; InvalidateRect(hwnd_, nullptr, FALSE); }
    }
    if (glanceOpen() && !friends_open_ && !power_menu_ && !exit_prompt_) {
        const int hover = glance_ui::Layout{glance_view.width,glance_view.height}.hit(mouse_x/glance_view.scale,mouse_y/glance_view.scale,glance_);
        if (glance_.hover != hover) { glance_.hover = hover; InvalidateRect(hwnd_,nullptr,FALSE); }
    }
    {
        const auto tile = home_widgets::companionArea(widget_spots);
        const float x = mouse_x / glance_view.scale, y = mouse_y / glance_view.scale;
        const bool hover = companionAvailable() && (tile.contains(x, y) || renderer_.companionBubble().contains(x, y));
        if (companion_.hover != hover) { companion_.hover = hover; InvalidateRect(hwnd_, nullptr, FALSE); }
        if (companionOpen() && !friends_open_ && !power_menu_ && !exit_prompt_) {
            const auto& chat = companion_.chat;
            int hit = (chat.viewer || chat.mind || chat.feed) ? companion_ui::no_hit : companion_ui::Layout{glance_view.width, glance_view.height}.hit(x, y, chat);
            // Her photo in the log: a click enlarges it.
            if (hit == companion_ui::no_hit && !chat.viewer && !chat.mind && !chat.feed && !chat.menu && !chat.confirm && renderer_.companionPhoto().contains(x, y)) hit = companion_ui::photo_hit;
            if (companion_.chat.hover != hit) { companion_.chat.hover = hit; InvalidateRect(hwnd_, nullptr, FALSE); }
        }
    }
    if (library_view_drag_) {
        if (library_controls_.panel != library_ui::Panel::views || library_controls_.view_dropdown || !libraryOverview()) { pointerUp(); return; }
        const int columns = library_ui::views::columnsAt(mouse_x / renderer_.viewport().scale - library_ui::views::offset(library_views_progress_));
        if (columns != library_controls_.options.overview_columns) { library_controls_.options.overview_columns = columns; applyLibraryOptions(); }
        return;
    }
    psfiveHover(mouse_x / renderer_.viewport().scale, mouse_y / renderer_.viewport().scale);
    if (tour_.open && !exit_prompt_) { const auto view = renderer_.viewport(); tourHover(mouse_x / view.scale, mouse_y / view.scale); return; }
    if (fileBrowserOpen()) { const auto view = renderer_.viewport(); fileBrowserHover(mouse_x / view.scale, mouse_y / view.scale); return; }
    if (color_editor_.open) { if (color_editor_.dragging) colorEditorDrag(mouse_x / renderer_.viewport().scale); return; }
    if (friendChatOpen()) { // The conversation's Back, field and Send under the pointer.
        const auto frame = friendsFrame();
        const auto [px, py] = friendsPoint(mouse_x, mouse_y);
        const int hover = frame.chat.hit(px, py);
        if (friend_chat_.hover != hover) { friend_chat_.hover = hover; InvalidateRect(hwnd_, nullptr, FALSE); }
    }
    if (dashboard_press_source_ == 3) { const auto view = drawerViewport(); dashboardPointerMove(mouse_x / view.scale, mouse_y / view.scale); return; }
    if (devicesOpen()) { const auto view = renderer_.viewport(); deviceDrag(mouse_x / view.scale, mouse_y / view.scale); return; }
    if (cropOpen()) { const auto view = renderer_.viewport(); cropDrag(mouse_x / view.scale, mouse_y / view.scale); return; }
    if (!settingsOpen()) return;
    const auto view = renderer_.viewport();
    if (!settings_dragging_) {
        const auto layout = Layout::of(view.width, view.height, settings_state_, settings_rows_);
        const float x = mouse_x / view.scale, y = mouse_y / view.scale;
        const int row = settings_state_.editing || customEditorOpen() || friends_open_ ? -1 : layout.hitRow(x, y, static_cast<int>(settings_rows_.size()), settings_state_.offset());
        int option = -1;
        if (row >= 0 && settings_rows_[row].control == Control::layout && layout.layoutBand(row, settings_state_.offset()).contains(x, y)) {
            const float first = layout.layoutFirstOf(row, settings_rows_[row].choice);
            for (int i = 0; i < home_layout_count; ++i) if (layout.layoutOption(row, settings_state_.offset(), i, first).contains(x, y)) option = i;
        }
        const int badge = row >= 0 && !settings_rows_[row].reset.empty() && !settings_rows_[row].badge.empty() &&
                          layout.badge(row, settings_state_.offset(), settings_rows_[row].control).contains(x, y) ? row : -1;
        if (settings_hover_row_ != row || settings_preview_.option != option || settings_hover_badge_ != badge) {
            settings_hover_row_ = row; settings_hover_badge_ = badge; settings_preview_.focus(option, GetTickCount64());
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    const auto track = Layout::of(view.width, view.height, settings_state_, settings_rows_).track(settings_state_.row(), settings_state_.offset());
    settingsSetLevel((mouse_x / view.scale - track.left) / (track.right - track.left));
}

void Launcher::mouseLeave() {
    if (companion_.link_pointer || companion_.picker.hover != companion_ui::no_hit) {
        companion_.link_pointer.reset(); companion_.picker.hover = companion_ui::no_hit;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (desktop_mode_ && (desktop_strip_hover_ >= 0 || !(desktop_hover_ == desktop_ui::Hit{}))) {
        desktop_strip_hover_ = -1;
        if (desktop_drag_.kind == DesktopDrag::Kind::none) desktop_hover_ = {}; // A drag keeps its capture.
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (glance_.hover != -100) { glance_.hover = -100; InvalidateRect(hwnd_,nullptr,FALSE); }
    if (glance_hover_) { glance_hover_ = false; InvalidateRect(hwnd_, nullptr, FALSE); }
    if (companion_.hover || companion_.chat.hover != companion_ui::no_hit) { companion_.hover = false; companion_.chat.hover = companion_ui::no_hit; InvalidateRect(hwnd_, nullptr, FALSE); }
    psfive_more_hover_ = false;
    if (psfive_top_hover_) { psfive_top_hover_ = false; InvalidateRect(hwnd_, nullptr, FALSE); }
    if (tour_.open) tourHover(-1e6f, -1e6f);
    if (settings_hover_row_ < 0 && settings_preview_.option < 0) return;
    settings_hover_row_ = settings_hover_badge_ = -1; settings_preview_.focus(-1, GetTickCount64());
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::pointerUp(bool cancelled) {
    desktopTextRelease();
    companion_.chat.mind_dragging = false;
    if (desktop_mode_ && cancelled && (desktop_drag_.kind != DesktopDrag::Kind::none || desktop_pressed_.target != desktop_ui::Target::none)) {
        // Capture lost (Alt+Tab, a dialog): the drag or press ends without acting.
        desktop_drag_ = {}; desktop_pressed_ = {};
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (library_view_drag_) { library_view_drag_ = false; if (GetCapture() == hwnd_) ReleaseCapture(); saveLibraryPreferences(); }
    if (cancelled && dashboard_press_source_ == 3) dashboardCancelPress();
    else dashboardPress(3, false, GetTickCount64());
    if (audio_drag_column_ >= 0) {
        audio_drag_column_ = -1; device_state_.audio_edit_column = -1;
        if (GetCapture() == hwnd_) ReleaseCapture();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    if (crop_state_.dragging) { crop_state_.dragging = false; if (GetCapture() == hwnd_) ReleaseCapture(); }
    if (color_editor_.dragging) { color_editor_.dragging = false; if (GetCapture() == hwnd_) ReleaseCapture(); }
    if (theme_.dragging) { themeStudioRelease(); if (GetCapture() == hwnd_) ReleaseCapture(); }
    gameCaseRelease();
    if (!settings_dragging_) return;
    settings_dragging_ = false;
    if (GetCapture() == hwnd_) ReleaseCapture();
    saveSettings();
}
