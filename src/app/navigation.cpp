// Keyboard/pad navigation between cards, shelves and screens: directions, opening libraries and
// platforms, back, search typing and categories; the home without games. Menus:
// navigation_menus.cpp; Enter hold and launching: game_launch.cpp.
#include "launcher.hpp"
#include "../library/search.hpp"
#include "../platform/text_encoding.hpp"
#include "../ui/category_rail.hpp"
#include "../ui/empty_library.hpp"

#include <algorithm>

void Launcher::navigate(int amount, bool edge_menu) {
    if (libraryDialogOpen()) { libraryDialogKey(amount < 0 ? VK_LEFT : VK_RIGHT, false); return; }
    if (edge_menu && amount) { nav_direction_ = amount < 0 ? -1 : 1; nav_direction_at_ = GetTickCount64(); } // Grid rows: changeCategory.
    if (identifyOpen()) return;
    if (gogInstallOpen()) { gogInstallKey(amount < 0 ? VK_LEFT : VK_RIGHT, false); return; }
    if (customEditorOpen()) { customEditorKey(amount < 0 ? VK_LEFT : VK_RIGHT, false); return; }
    if (friends_open_) { dashboardMove(amount, 0); return; } // The drawer overlays every screen.
    if (glanceOpen()) { glanceMove(amount, 0); return; }
    if (companionOpen()) { companionMove(amount, 0); return; }
    if (desktopActive() && topDialog() == Modal::none && !quick_open_) { desktopMove(amount, 0); return; } // The pad in the desktop window.
    if (category_menu_open_) {
        if (psfiveActive() && edge_menu && amount) {
            closeCategoryMenu();
            psfiveHorizontal(amount);
            return;
        }
        if (edge_menu && amount > 0) { closeCategoryMenu(); audio_.navigate(); }
        return; // Dismiss without selecting the highlighted category or moving a game card.
    }
    if (screen_ == ScreenMode::plugin_page && plugin_page_.tiled) { pluginPageSide(amount); return; }
    if (screen_ == ScreenMode::plugin_page) { if (amount < 0) { plugin_page_.focus = -1; InvalidateRect(hwnd_, nullptr, FALSE); } else if (plugin_page_.focus < 0) pluginPageMove(1); return; }
    if (downloadsOpen()) { // Left: Back; Right from Back: the rows.
        if (amount < 0 && !downloads_state_.back) { downloads_state_.back = true; audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); }
        else if (amount > 0 && downloads_state_.back) downloadsMove(1);
        else if (edge_menu) navigationBoundary();
        return;
    }
    if (libraryHorizontal(amount, edge_menu)) return;
    if (cropOpen()) { cropMove(amount, 0); return; }
    if (devicesOpen()) { deviceAdjust(amount); return; }
    if (artworkOpen()) { artworkMove(amount, 0); return; }
    if (settingsOpen() && !friends_open_) { settingsAdjust(amount); return; }
    if (achievementsOpen()) { if (achievements_.card) achievementsMove(amount); return; } // A card: the previous/next one; the list scrolls vertically only.
    if (screen_ == ScreenMode::details) {
        if (menu_open_) { dismissMenu(); return; }
        if (edge_menu && std::abs(amount) == 1) { detailsMove(amount, 0); return; } // Keys and pad: by position.
        // Programmatic jumps: the buttons, then the achievements row when the game has achievements.
        const int next = std::clamp(detail_action_ + amount, -1, std::max({lastDetailAction(), achievementsDetailAction(), ratingDetailAction(), pluginDetailAction()}));
        if (next == detail_action_) { navigationBoundary(); return; }
        if (next != detail_action_) audio_.navigate();
        detail_action_ = next;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (flixRailFocused()) { if (amount > 0) flixRailFocus(-1); else navigationBoundary(); return; } // Right: back to the tile.
    if (snitchProfileFocused()) { if (amount > 0) snitchFocusProfile(false); else if (edge_menu) navigationBoundary(); return; } // Right: back to the row.
    if (quick_open_) { quickMove(amount); return; }
    if (searchBackFocused() && !friends_open_) { if (edge_menu) navigationBoundary(); return; } // Back stands alone in its row.
    if (categoryRailActive() && category_rail_focus_ && edge_menu && !pending_launch_) {
        if (amount > 0) focusCategoryRail(false); else navigationBoundary();
        return;
    }
    // No games: Left/Right walk the empty home's buttons (Left past the first one reaches the rail).
    if (emptyHomeActive() && edge_menu && std::abs(amount) == 1 && !(categoryRailActive() && amount < 0 && empty_focus_ == 0)) {
        emptyHomeMove(amount); return;
    }
    if (categoryRailActive() && edge_menu && amount < 0 && (cards_.empty() || selected_ == 0) && !pending_launch_) {
        focusCategoryRail(true); return;
    }
    if (cards_.empty() || pending_launch_ || friends_open_ || category_menu_open_) return;
    if (xmbActive()) {
        if (menu_open_) dismissMenu();
        // Keys move between tiles; programmatic returns (details, artwork, crop) pass a card index delta.
        if (edge_menu) xmbHorizontal(amount);
        else xmbFocusCard(static_cast<size_t>(std::clamp(static_cast<int>(selected_) + amount, 0, static_cast<int>(cards_.size()) - 1)));
        return;
    }
    if (flixActive()) {
        if (menu_open_) dismissMenu();
        if (edge_menu) flixHorizontal(amount);
        else flixFocusCard(static_cast<size_t>(std::clamp(static_cast<int>(selected_) + amount, 0, static_cast<int>(cards_.size()) - 1)));
        return;
    }
    if (metroActive()) {
        if (menu_open_) dismissMenu();
        if (edge_menu) metroMove(amount, 0);
        else metroFocus(static_cast<size_t>(std::clamp(static_cast<int>(selected_) + amount, 0, static_cast<int>(cards_.size()) - 1)));
        return;
    }
    if (psfiveDetailsShown() && edge_menu) { if (menu_open_) dismissMenu(); psfiveDetailsHorizontal(amount); return; } // Screenshots, similar games.
    if ((psfiveActions() || psfiveTopFocused()) && edge_menu) { if (menu_open_) dismissMenu(); psfiveHorizontal(amount); return; } // "Graj" / "…", top bar.
    if (screen_ == ScreenMode::shelves && edge_menu && amount < 0 && selected_ == 0) {
        if (psfiveActive() || snitchActive()) { navigationBoundary(); return; }
        openCategoryMenu();
        return;
    }
    // Grid keys stay in their row: Left in the first column opens the library's view drawer,
    // Right in the last one does nothing. Programmatic moves (edge_menu false) pass index deltas.
    if (gridScreen(screen_) && edge_menu && std::abs(amount) == 1) {
        const auto columns = static_cast<size_t>(libraryColumns());
        const bool horizontal = libraryCarousel() || libraryHorizontalCinematic();
        const size_t column = horizontal ? selected_ : selected_ % columns;
        if (amount < 0 && column == 0) {
            if (screen_ == ScreenMode::library) openLibraryPanel(library_ui::Panel::views);
            else navigationBoundary();
            return;
        }
        if (amount > 0 && ((!horizontal && column + 1 == columns) || selected_ + 1 >= cards_.size())) { if (!libraryList()) navigationBoundary(); return; }
    }
    if (menu_open_) dismissMenu();
    const auto next = static_cast<size_t>(std::clamp(static_cast<int>(selected_) + amount,
                                                    0, static_cast<int>(cards_.size()) - 1));
    if (next == selected_) { navigationBoundary(); return; }
    focusCard(next);
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::openLibrary() {
    rememberPlace(); library_controls_ = {};
    versions_ = versions::Index(public_library_);
    grouped_library_ = versions_.collapse(public_library_, library_state_.preferred_versions, library_state_.first_seen);
    pc_source_.reset(); library_console_.clear();
    library_category_.clear(); category_library_.clear();
    if (public_library_.empty()) {
        // Metro's Biblioteka tile and the bar's Wszystkie gry: say why nothing opens.
        if (!libraryLoading()) showNotification(NotificationKind::launch, tr(L"Biblioteka jest pusta"), tr(L"Dodaj grę albo włącz Platformy w Ustawieniach → Biblioteka."));
        return;
    }
    screen_ = ScreenMode::library;
    grid_scroll_ = 0;
    closeQuick();
    audio_.quick();
    rebuildCards(true);
}

// View / Select on the home screen: the whole library of where the focus is. A store's or console's
// tile opens its games, a category tile ("Wszystkie") its category; a row of games its library, a row
// of logos its board; Ostatnio grane its own tab of Wszystkie gry (Wszystkie gry once that tab is left
// out in Biblioteka → Ekran główny i kolekcje). Anything else, or an empty row: Wszystkie gry.
void Launcher::viewButton() {
    if (screen_ != ScreenMode::shelves || !homeOverlayAvailable() || cards_.empty()) return;
    const Game* focused = selected_ < cards_.size() ? &cards_[selected_].game : nullptr;
    const auto id = category_ < categories_.size() ? categories_[category_].id : std::string{};
    if (focused && focused->kind == GameKind::pc_platform) { openPcLibrary(focused->source); return; }
    if (focused && focused->kind == GameKind::category) { openCategoryLibrary(focused->source_id); return; }
    if (focused && focused->kind == GameKind::platform && !studioCategory(id)) { openPlatform(focused->platform); return; }
    if (!id.empty() && id != "pc-platforms" && id != "emulation" && !(id == "recent" && !homeRowTab(id))) {
        openCategoryLibrary(id);
        if (screen_ == ScreenMode::library) return;
    }
    openLibrary();
}

const Launcher::Category* Launcher::findCategory(const std::string& id) const {
    for (const auto* list : {&categories_, &library_categories_})
        for (const auto& category : *list) if (category.id == id) return &category;
    return nullptr;
}

void Launcher::openCategoryLibrary(const std::string& id) {
    const auto* category = findCategory(id);
    // Wydawcy and Developerzy: a board of their logos; a logo opens its games (openPlatform).
    const bool board = studioCategory(id);
    if (!category || (!board && std::none_of(category->games.begin(), category->games.end(), [](const Game& game) { return isGame(game); })) ||
        category->games.empty()) return;
    rememberPlace(); library_controls_ = {};
    pc_source_.reset(); library_console_.clear(); studio_board_return_.clear();
    library_category_ = id;
    category_library_title_ = category->title;
    category_library_.clear();
    for (const auto& game : category->games) if (board || isGame(game)) category_library_.push_back(game);
    screen_ = ScreenMode::library;
    grid_scroll_ = 0;
    closeQuick();
    dismissMenu();
    rebuildCards(true);
    audio_.drawer();
}

const std::vector<Game>& Launcher::librarySource() const {
    static const std::vector<Game> none;
    if (!library_console_.empty()) { const auto* games = consoleGames(library_console_); return games ? *games : none; }
    return pc_source_ ? pc_library_ : !library_category_.empty() ? category_library_ : grouped_library_;
}

// A console's games: a library like a PC store's (its own view, order, filter and search,
// saved as "console:<name>"), in every home layout.
void Launcher::openPlatform(const std::wstring& name) {
    const auto* games = consoleGames(name); // A console's, or a publisher's or developer's.
    if (!games || games->empty()) return;
    // From a board of logos (Wydawcy, Developerzy): Back returns to it.
    // Another studio's tab keeps it.
    const bool studio_tab = studios::parse(text_encoding::utf8(library_console_)) && studios::parse(text_encoding::utf8(name));
    studio_board_return_ = studioBoard() ? library_category_ : studio_tab ? studio_board_return_ : std::string{};
    rememberPlace(); library_controls_ = {};
    pc_source_.reset();
    library_category_.clear(); category_library_.clear();
    library_console_ = nested_platform_ = name;
    screen_ = ScreenMode::library;
    grid_scroll_ = 0;
    closeQuick();
    dismissMenu();
    rebuildCards(true);
    audio_.drawer();
}

void Launcher::openSearch() {
    rememberPlace();
    screen_ = ScreenMode::search;
    grid_back_focus_ = false;
    search_query_.clear(); search_after_ = 0;
    loadRecentSearches(); recent_search_focus_ = -1;
    search_results_.clear(); search_shown_query_.clear();
    search_dirty_ = false;
    grid_scroll_ = 0;
    closeQuick();
    audio_.quick();
    rebuildCards(true);
}

void Launcher::back(bool escape) {
    const auto dialog = topDialog();
    if (desktopActive() && dialog == Modal::none && !friends_open_ && !quick_open_ && !menu_open_) { desktopBack(); return; }
    if (dialog == Modal::bulk_covers) { bulkCoversClose(); return; }
    if (dialog == Modal::library_dialog) { collectionDialogBack(); return; }
    if (plugin_command_pending_) { ++plugin_command_generation_; plugin_command_pending_ = false; }
    switch (dialog) {
    case Modal::aurelia: aureliaKey(VK_ESCAPE, false); return;
    case Modal::companion_picker: companionPicker(false); return;
    case Modal::theme_studio: themeStudioKey(VK_ESCAPE, false); return;
    case Modal::file_browser: closeFileBrowser(); return; // Over every panel that opens it.
    case Modal::custom_editor: closeCustomEditor(); return;
    case Modal::metadata: desktopMetaClose(); return;
    case Modal::identify: closeIdentify(); return;
    case Modal::gog_install: gogInstallKey(VK_ESCAPE, false); return;
    case Modal::tarot: closeTarot(); return;
    case Modal::profile: closeProfile(); return;
    case Modal::game_case: gameCaseBack(); return;
    case Modal::whats_new: closeWhatsNew(); return;
    default: break;
    }
    if (color_editor_.open) { closeColorEditor(false); return; }
    if (friends_open_) { friendsBack(); return; } // The drawer overlays every screen.
    if (achievementsOpen()) { if (achievements_.card) closeAchievementCard(); else closeAchievements(); return; }
    if (!quick_open_ && !menu_open_ && pluginPageBack()) return; // The page's own level above.
    if (libraryButtons() && library_controls_.action >= 0 && !menu_open_ && !library_controls_.open()) {
        library_controls_.action = -1; InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (libraryControls() && (library_controls_.open() || library_controls_.focus >= 0 || library_controls_.search)) {
        // Panel, then the search field (typing or not: its filter goes before leaving), then the bar.
        if (library_controls_.panel == library_ui::Panel::views && library_controls_.view_dropdown) library_controls_.view_dropdown = false;
        else if (library_controls_.open()) closeLibraryPanel();
        else if (library_controls_.typing() || (library_controls_.search && library_controls_.focus < 0)) closeLibrarySearch();
        else { library_controls_.focus = -1; focusCard(selected_); }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (cropOpen()) { closeCropEditor(); return; }
    if (devicesOpen()) {
        if (device_state_.audio_edit_column >= 0) { endAudioEditing(); return; }
        pointerUp();
        if (device_service_) device_service_->close();
        if (device_state_.panel == devices::Panel::bluetooth && bluetooth_from_panel_) { openDevices(bluetooth_return_); return; }
        screen_ = device_return_;
        if (glanceOpen() && glance_pending_) requestGlance(glance_pending_refresh_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (artworkOpen()) {
        if (artwork_state_.local_open) { closeLocalArtwork(); return; } // "Z dysku…" panel first.
        if (artwork_state_.provider_open) { artwork_state_.provider_open = false; InvalidateRect(hwnd_, nullptr, FALSE); return; } // Then "Źródło"'s list.
        if (artwork_state_.provider_focus) { artwork_state_.provider_focus = false; InvalidateRect(hwnd_, nullptr, FALSE); return; } // Back to the rail.
        if (artwork_state_.search_focus || artwork_state_.results_mode) {
            artwork_state_.search_focus = artwork_state_.results_mode = false;
            artwork_state_.source = artwork_state_.override_source; loadArtworkPage(); return;
        }
        if (artwork_state_.gallery && !artwork_state_.applying) {
            artwork_state_.gallery = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
        } else closeArtworkBrowser();
        return;
    }
    if (flixRailFocused()) { flixRailFocus(-1); return; }
    if (snitchProfileFocused()) { snitchFocusProfile(false); return; }
    if (quick_open_) { closeQuick(); return; }
    if (category_menu_open_) { closeCategoryMenu(); return; }
    if (menu_open_) { dismissMenu(); return; }
    if (screen_ == ScreenMode::settings) {
        pointerUp();
        mouseLeave();
        if (homeRowMoving()) { stopHomeRowMove(); return; } // Back puts the lifted collection down first.
        if (const auto platform = settings_state_.platform()) {
            // A sign-in in progress ends with its page; while editing, Back ends the edit first.
            const bool editing = settings_state_.editing;
            cancelPlatformSignIn(*platform);
            clearSecretInput(); settings_state_.editing = false;
            // DRM-free game list → its page (focus on "Znalezione gry"), then the overview.
            if (!editing) settings_state_.close();
            settings_motion_.valid = false; buildSettingsRows(); return;
        }
        // A page → the row it was opened from (a value being edited closes first, below); a page
        // opened from outside Settings (a game's Proton / Wine…) → where it was opened.
        if (settings_state_.subpage() && !settings_state_.editing) {
            if (settings_page_exits_ && settings_state_.pages.size() == 1) {
                settings_page_exits_ = false;
                settings_state_.closeAll();
                settings_state_.options = false;
                saveSettings();
                showCategory(category_);
                return;
            }
            closeSettingsPage();
            return;
        }
        if (gog_state_.state == gog::State::preparing || gog_state_.state == gog::State::signing_in || gog_state_.state == gog::State::verifying) gogCommand(gog::Command::cancel);
        if (epic_state_.state == epic::State::preparing || epic_state_.state == epic::State::signing_in || epic_state_.state == epic::State::verifying) epicCommand(epic::Command::cancel);
        if (settings_state_.back()) { clearSecretInput(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
        saveSettings();
        showCategory(category_);
        return;
    }
    if (screen_ == ScreenMode::details) {
        store_jobs_->generation.fetch_add(1);
        details_loading_ = false;
        screen_ = detail_return_;
        grid_scroll_ = detail_return_scroll_;
        rebuildCards(true);
        if (!cards_.empty()) navigate(static_cast<int>(std::min(detail_return_selected_, cards_.size() - 1)) - static_cast<int>(selected_), false);
        return;
    }
    if (glanceOpen()) { closeGlance(); return; }
    if (companionOpen()) {
        if (companion_.chat.feed) companionFeedBack();
        else if (companion_.chat.viewer) companionViewer(false); // Her enlarged photo closes first.
        else if (companion_.chat.mind && companion_.chat.mind_confirm) companionMindDeleteConfirm(false); // Memory delete dialog: Back is Anuluj.
        else if (companion_.chat.mind) companionMind(false); // Her 3D mind screen closes first.
        else if (companion_.chat.confirm) companionResetConfirm(false); // The reset dialog: Back is Anuluj.
        else if (companion_.chat.menu) companionMenu(false); // Her "…" menu closes first.
        else if (companion_.chat.archive) companionCloseArchive(); // A history page: back to the live conversation.
        else closeCompanion();
        return;
    }
    if (screen_ == ScreenMode::store) { store_jobs_->generation.fetch_add(1); store_loading_ = false; }
    if (screen_ == ScreenMode::platform_games) { showCategory(category_); return; }
    if (screen_ == ScreenMode::library && !library_console_.empty() && !studio_board_return_.empty()) {
        // A studio's games opened from the board of logos: back to the board, on its logo.
        const auto studio = library_console_;
        openCategoryLibrary(std::exchange(studio_board_return_, {}));
        for (size_t i = 0; i < cards_.size(); ++i) if (cards_[i].game.platform == studio) { focusCard(i); break; }
        return;
    }
    if (screen_ != ScreenMode::shelves) { showCategory(category_); return; }
    if (xmbActive() && xmb_row_ >= 0) { xmbFocus(xmb_column_, -1); audio_.navigate(); return; }
    if (flixActive() && flix_row_ > 0) { flixFocus(0, categories_.empty() ? 0 : categories_[0].selected); audio_.navigate(); return; }
    if (metroTabsFocused()) { metroFocusTabs(false); audio_.navigate(); return; } // The tabs give the focus back to the page.
    if (metroActive() && metro_page_ > 0) { homePage(-static_cast<int>(metro_page_)); return; } // Back to the start page.
    if (psfiveDetailsShown()) { psfiveDetailsBack(); return; } // A screenshot shown full screen, then the details: back up to "Graj" / "…".
    if (psfiveActions()) { psfiveFocusAction(-1); audio_.navigate(); return; } // The buttons give the focus back to the row.
    if (psfiveTopFocused()) { psfiveFocusTop(-1); audio_.navigate(); return; } // So does the top bar.
    if (psfiveActive() && category_ != 0) { showCategory(0); audio_.navigate(); return; } // Another shelf: back to Recent.
    if (categoryRailActive() && !category_rail_focus_) { focusCategoryRail(true); return; }
    if (escape) openPowerMenu(); // Home with nothing left to close: Esc shows Zasilanie.
}

void Launcher::onCharacter(wchar_t character) {
    if (desktop_mode_ && character >= L' ' && character != 127) desktopTextBeforeTyping(); // Typing replaces a selection.
    if (desktop_meta_.open && desktopActive() && topModal() == Modal::none) { desktopMetaChar(character); return; } // Edytuj metadane.
    if (desktop_quick_.open && desktopActive() && topModal() == Modal::none) { desktopQuickChar(character); return; } // Ctrl+K's field.
    switch (topDialog()) {
    case Modal::file_browser: fileBrowserCharacter(character); return;
    case Modal::aurelia: aureliaCharacter(character); return;
    case Modal::library_dialog: collectionDialogEdit(character); return;
    case Modal::custom_editor: customEditorInsert(std::wstring_view(&character, 1)); return;
    case Modal::metadata: desktopMetaChar(character); return;
    case Modal::identify: identifyEdit(character); return;
    case Modal::theme_studio: themeStudioChar(character); return;
    case Modal::gog_install: case Modal::companion_picker: case Modal::tarot: case Modal::whats_new: case Modal::profile: case Modal::game_case: return;
    default: break; // No dialog, or one without a text field (Pobierz brakujące okładki).
    }
    if (friendChatOpen() && topDialog() == Modal::none) { friendChatCharacter(character); return; } // The drawer's conversation.
    if (desktopActive() && desktopCharacter(character)) return; // Type-to-search in the desktop window.
    // A store page: any letter typed anywhere on it goes into its search field.
    if (screen_ == ScreenMode::plugin_page && !plugin_drawer_active_ && !friends_open_ && !plugin_page_.dialog && !plugin_page_.typing() &&
        !plugin_page_.rows.empty() && plugin_page_.rows.front().input && character > L' ' && character != 127) plugin_page_.focus = 0;
    if (pluginPageTyping()) { pluginPageType(character); return; }
    if (companionTyping()) { companionType(character); return; }
    if (artworkOpen()) {
        if (character < L' ' || character == 127 || artwork_state_.applying) return;
        if (artwork_state_.query_selected) { artwork_state_.query.clear(); artwork_state_.query_selected = false; }
        if (!artwork_state_.search_focus) artwork_state_.query.clear();
        if (artwork_state_.query.size() < 160) artwork_state_.query += character;
        editArtworkQuery(); return;
    }
    if (editingSecret()) {
        // Plugin settings and plain text rows (ScreenScraper softname, URL) also take spaces and non-ASCII.
        const bool plugin_text = settings_rows_[settings_state_.row()].id == settings_ui::Id::plugin_setting ||
                                 settings_rows_[settings_state_.row()].control == settings_ui::Control::text;
        if (plugin_text ? character < 32 : (character <= 32 || character >= 127)) return;
        if (settings_secret_selected_) { clearSecretInput(); settings_secret_selected_ = false; }
        const auto id = settings_rows_[settings_state_.row()].id;
        const size_t limit = id == settings_ui::Id::battlenet_callback ? 262144u : id == settings_ui::Id::ea_callback ? 16384u :
                             id == settings_ui::Id::gog_callback || id == settings_ui::Id::amazon_callback ? 8192u : 512u;
        if (settings_secret_input_.size() < limit) settings_secret_input_ += character;
        settingsSearchEdited();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    // On the library screen typing starts its search at once (a leading space does not).
    if (!libraryTyping() && libraryTypeAhead() && character > L' ' && character != 127) openLibrarySearch();
    if (libraryTyping()) {
        if (character < L' ' || character == 127 || library_controls_.query.size() >= 70) return;
        library_controls_.query.insert(library_controls_.caret(), 1, character);
        library_query_dirty_ = true;
        library_query_edited_ = std::chrono::steady_clock::now();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!searching() || character < L' ' || character == 127 || search_query_.size() >= 70) return;
    dismissMenu();
    grid_back_focus_ = false; // Typing returns to the field and its results.
    recent_search_focus_ = -1;
    if (screen_ == ScreenMode::store) {
        store_jobs_->generation.fetch_add(1);
        store_loading_ = false;
        store_results_.clear();
        rebuildCards(false);
        store_status_.clear();
    }
    search_query_.insert(searchCaret(), 1, character);
    search_dirty_ = true;
    search_edited_ = std::chrono::steady_clock::now();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::eraseSearchCharacter(bool repeated) {
    if (pluginPageTyping()) { pluginPageErase(); return; }
    if (companionTyping()) { companionErase(); return; }
    if (libraryTyping()) {
        // Backspace on an empty field folds it away; a held one only empties it.
        if (library_controls_.query.empty()) { if (!repeated) closeLibrarySearch(); return; }
        const auto caret = library_controls_.caret();
        if (!caret) return; // The insertion point at the start: nothing before it.
        library_controls_.query.erase(caret - 1, 1);
        library_query_dirty_ = true;
        library_query_edited_ = std::chrono::steady_clock::now();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (!searching() || search_query_.empty()) { if (!repeated) back(); return; }
    const auto caret = searchCaret();
    if (!caret) return; // The insertion point at the start: nothing before it.
    dismissMenu();
    search_query_.erase(caret - 1, 1);
    if (screen_ == ScreenMode::store) {
        store_jobs_->generation.fetch_add(1);
        store_loading_ = false;
        store_results_.clear();
        rebuildCards(false);
    }
    search_dirty_ = true;
    search_edited_ = std::chrono::steady_clock::now();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::updateSearch() {
    search_dirty_ = false;
    versions_ = versions::Index(public_library_);
    grouped_library_ = versions_.collapse(public_library_, library_state_.preferred_versions, library_state_.first_seen);
    search_results_ = searchPublicGames(grouped_library_, search_query_, private_shelf_);
    // A typed change starts from the top; a rebuild in the background (scans, resolved names, art)
    // keeps the cards, the scroll and the focus on its game (rebuildCards keys them by the query).
    if (search_query_ != search_shown_query_) grid_scroll_ = 0;
    search_shown_query_ = search_query_;
    rebuildCards(false);
}

// Details as laid out on screen (ui/game_details_layout.hpp): Back at the top left, the rating,
// the row of buttons, the cards at the bottom. Keys move to the nearest target that way; from a
// card Up returns to the button last left. Action indices only identify the targets.
void Launcher::detailsMove(int dx, int dy) {
    if (cards_.empty()) return;
    const auto view = renderer_.viewport();
    const details_layout::Layout layout{view.width, view.height};
    const auto actions = detailsActions();
    const auto all = layout.items(actions);
    const int current = detail_action_;
    const auto* from = layout.find(all, current);
    int next = !from ? 0 : details_layout::move(all, current, dx, dy); // A target that is gone: the primary button.
    if (from && from->card() && dy < 0) {
        const auto row = layout.buttons(actions);
        if (std::any_of(row.begin(), row.end(), [&](const details_layout::Item& item) { return item.action == detail_column_; })) next = detail_column_;
    }
    if (const auto* to = layout.find(all, next); to && !to->card() && to->kind != details_layout::Kind::back && to->kind != details_layout::Kind::rating)
        detail_column_ = next;
    if (next == current) { navigationBoundary(); return; }
    audio_.navigate();
    detail_action_ = next;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::changeCategory(int amount) {
    if (glanceOpen() && !friends_open_) { glanceMove(0, amount); return; }
    if (companionOpen() && !friends_open_) { companionMove(0, amount); return; }
    if (libraryDialogOpen()) { libraryDialogKey(amount < 0 ? VK_UP : VK_DOWN, false); return; }
    if (menu_open_ && screen_ == ScreenMode::details) { menuMove(amount); return; }
    if (screen_ == ScreenMode::plugin_page && !friends_open_) { pluginPageMove(amount); return; }
    if (downloadsOpen() && !friends_open_ && !gogInstallOpen()) { downloadsMove(amount); return; }
    if (amount) { nav_direction_ = amount < 0 ? -2 : 2; nav_direction_at_ = GetTickCount64(); }
    if (identifyOpen()) { identifyMove(amount); return; }
    if (gogInstallOpen()) { gogInstallKey(amount < 0 ? VK_UP : VK_DOWN, false); return; }
    if (customEditorOpen()) { customEditorKey(amount < 0 ? VK_UP : VK_DOWN, false); return; }
    if (desktopActive() && topDialog() == Modal::none && !friends_open_ && !quick_open_) { desktopMove(0, amount); return; }
    if (libraryVertical(amount)) return;
    if (cropOpen()) { cropMove(0, amount); return; }
    if (devicesOpen()) { deviceMove(amount); return; }
    if (artworkOpen()) { artworkMove(0, amount); return; }
    if (achievementsOpen()) { achievementsMove(amount); return; }
    if (screen_ == ScreenMode::details) { if (std::abs(amount) == 1) detailsMove(0, amount); else navigate(amount, false); return; }
    if (screen_ == ScreenMode::settings) { settingsMove(amount); return; }
    if (gridScreen(screen_)) {
        if (screen_ == ScreenMode::search || screen_ == ScreenMode::store) {
            if (recentSearchMove(amount)) return; // The empty field's recent searches.
            // Up from the first row: Back at the top left; Down returns to the games.
            if (grid_back_focus_) {
                if (amount > 0 && !cards_.empty()) { grid_back_focus_ = false; audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); }
                else navigationBoundary();
                return;
            }
            if (amount < 0 && (cards_.empty() || selected_ < static_cast<size_t>(libraryColumns()))) {
                grid_back_focus_ = true; audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); return;
            }
        }
        const auto before = selected_;
        navigate(amount * libraryColumns(), false);
        if (screen_ == ScreenMode::library && selected_ != before) noteFastScroll();
        return;
    }
    if (screen_ != ScreenMode::shelves) return;
    if (categories_.empty() || pending_launch_ || category_menu_open_) return;
    // The quick bar closes towards the tiles: Down on the other layouts, Up on Metro (opened by Down).
    // PSfive opens it from the buttons (Down) and closes it either way; with its details under the
    // row (Opcje układu → Szczegóły pod rzędem) it opens from above the tabs, and Down closes it.
    if (flixRailFocused()) { flixRailMove(amount); return; }
    if (snitchProfileFocused()) { if (amount > 0) snitchFocusProfile(false); return; } // Down: back to the row.
    if (quick_open_) {
        if (psfiveActive() && settings_.psfive_details) { if (amount > 0) closeQuick(); else navigationBoundary(); return; }
        if (psfiveActive() || ((metroActive() || snitchActive()) ? amount < 0 : amount > 0)) closeQuick();
        return;
    }
    if (snitchActive()) {
        if (amount > 0) openQuick(); else snitchFocusProfile(true); // Up from the row: the profile picture.
        return;
    }
    if (xmbActive()) { dismissMenu(); xmbVertical(amount); return; }
    if (flixActive()) { dismissMenu(); flixVertical(amount); return; }
    if (metroActive()) { dismissMenu(); metroMove(0, amount); return; }
    if (psfiveActive()) { dismissMenu(); psfiveVertical(amount); return; }
    if (category_ == 0 && amount < 0) { openQuick(); return; }
    dismissMenu();
    const int next = std::clamp(static_cast<int>(category_) + amount, 0,
                                static_cast<int>(categories_.size()) - 1);
    if (next == static_cast<int>(category_)) { navigationBoundary(); return; }
    audio_.navigate();
    const bool rail_focus = categoryRailActive() && category_rail_focus_;
    showCategory(static_cast<size_t>(next));
    category_rail_focus_ = rail_focus;
}

// ---- The home without games (src/ui/empty_library.hpp) ---------------------------------------

empty_library::Reason Launcher::emptyLibraryReason() const {
    using empty_library::Reason;
    if (libraryLoading()) return Reason::loading;
    // No Steam account: either no Steam on this PC at all, or Steam with nobody signed in.
    if (!account_) return steam_executable_ && !steam_executable_().empty() ? Reason::no_account : Reason::no_steam;
    return Reason::empty;
}

bool Launcher::emptyHomeActive() const {
    return screen_ == ScreenMode::shelves && cards_.empty() && !libraryLoading() && !desktopActive() && !pending_launch_ &&
        !quick_open_ && !friends_open_ && !menu_open_ && !category_menu_open_ && !category_rail_focus_ && topModal() == Modal::none &&
        !keyboard_.open && !closing_;
}

empty_library::Layout Launcher::emptyHomeLayout() const {
    const auto view = renderer_.viewport();
    return {view.width, view.height, categoryRailActive() ? category_rail::Layout{view.width, view.height}.right() : 0.0f};
}

void Launcher::emptyHomeMove(int amount) {
    const int next = std::clamp(empty_focus_ + amount, 0, empty_library::action_count - 1);
    if (next == empty_focus_) { navigationBoundary(); return; }
    empty_focus_ = next;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Dodaj grę: the editor of a program, shortcut or script; Platformy: Ustawienia → Biblioteka with its
// stores; Odśwież: every library scanned again (Steam signed in meanwhile, a store installed).
void Launcher::emptyHomeActivate(int action) {
    empty_focus_ = std::clamp(action, 0, empty_library::action_count - 1);
    audio_.context();
    switch (empty_focus_) {
    case empty_library::add_game: openCustomEditor({}); break;
    case empty_library::platforms:
        openSettings();
        settings_state_.selectCategory(static_cast<int>(settings_ui::Category::library));
        buildSettingsRows();
        settingsPreviewFocus();
        break;
    default: desktopRefreshLibraries(); break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::emptyHomeClick(float x, float y, bool double_click) {
    if (!emptyHomeActive()) return false;
    const int action = emptyHomeLayout().hit(x, y);
    if (action < 0) return false;
    if (!double_click) emptyHomeActivate(action);
    return true;
}
