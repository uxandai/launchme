#include "launcher.hpp"
#include "../platform/unix_time.hpp"
#include "../platform/atomic_file.hpp"
#include "../platform/text_encoding.hpp"
#include "../library/search.hpp"
#include "../ui/grid_layout.hpp"
#include "../ui/category_rail.hpp"
#include "../ui/psfive_layout.hpp"
#include "../ui/snitch_layout.hpp"

namespace {
// Vertical moves closer than this belong to one run (a held key or stick repeats faster).
constexpr auto fast_scroll_gap = std::chrono::milliseconds(330);
// The kind of library a saved scope is (libraryScope: "<account>:console:PS2", "<account>:category:<id>",
// "<account>:console:publisher:capcom"), for Interfejs → Widoki biblioteki; empty: none of them.
std::string_view viewKind(std::string_view scope) {
    const auto colon = scope.find(':');
    if (colon == std::string_view::npos) return {};
    scope.remove_prefix(colon + 1);
    if (scope.starts_with("console:publisher:") || scope.starts_with("console:developer:")) return "studio";
    if (scope.starts_with("console:")) return "console";
    if (scope.starts_with("category:")) {
        // Shelves the launcher builds itself (src/app/shelves.cpp) are not collections; publishers and
        // developers are boards of logos.
        constexpr std::string_view built_in[] = {"recent", "pc-platforms", "emulation", "updated", "discovery", "private", "publishers", "developers"};
        scope.remove_prefix(9);
        return std::find(std::begin(built_in), std::end(built_in), scope) != std::end(built_in) ? std::string_view{} : std::string_view("collection");
    }
    return {};
}
}
bool Launcher::libraryControls() const { return screen_ == ScreenMode::library || screen_ == ScreenMode::platform_games; }
library_view::View Launcher::libraryView() const { return screen_ == ScreenMode::library ? library_controls_.options.view : library_view::View::grid; }
// The library's own options; the search results' grid shows the artwork chosen in Interfejs →
// Grafiki w wyszukiwarce; other grid screens the plain square grid.
library_view::Options Launcher::libraryPresentation() const {
    if (screen_ == ScreenMode::library) return library_controls_.options;
    library_view::Options options;
    if (screen_ == ScreenMode::search && !desktopActive()) library_view::setGridArt(options, static_cast<int>(settings_.search_art));
    return options;
}
bool Launcher::libraryGridPosters() const { return libraryView() == library_view::View::grid && libraryPresentation().grid_posters; }
bool Launcher::libraryGridLandscape() const { return libraryView() == library_view::View::grid && libraryPresentation().grid_landscape; }
bool Launcher::libraryGridBackdrop() const {
    return !desktopActive() && (screen_ == ScreenMode::library || screen_ == ScreenMode::search) && libraryView() == library_view::View::grid;
}
bool Launcher::libraryHorizontalCinematic() const { return libraryCinematic() && libraryPresentation().cinematic_horizontal; }
bool Launcher::libraryList() const { return library_view::rows(libraryView()); }
bool Launcher::libraryPreview() const { return library_view::preview(libraryView()); }
bool Launcher::libraryCinematic() const { return libraryView() == library_view::View::cinematic; }
bool Launcher::libraryBookshelf() const { return libraryView() == library_view::View::bookshelf; }
bool Launcher::libraryOverview() const { return libraryView() == library_view::View::overview; }
bool Launcher::libraryLandscape() const { return library_view::landscape(libraryView()); }
bool Launcher::libraryListPlus() const { return libraryView() == library_view::View::list_plus; }
// The tabs in the top bar, in every view: "Wszystkie gry" and the shelves' own libraries; in a
// store's library the PC stores, in a console's the consoles (Platformy PC / Emulacja). Hidden
// while the search field is open (its line takes the title back).
bool Launcher::libraryTabsShown() const {
    return screen_ == ScreenMode::library && !library_controls_.search && !libraryTabs().empty();
}
std::vector<Launcher::LibraryTab> Launcher::libraryTabs() const {
    std::vector<LibraryTab> tabs;
    const auto shelf = [&](const char* id) -> const Category* {
        for (const auto& category : categories_) if (category.id == id) return &category;
        return nullptr;
    };
    if (pc_source_) {
        // The stores of the Platformy PC shelf, in its order.
        if (const auto* stores = shelf("pc-platforms")) for (const auto& tile : stores->games) {
            if (tile.kind != GameKind::pc_platform) continue;
            const auto games = static_cast<size_t>(std::count_if(public_library_.begin(), public_library_.end(),
                [&](const Game& game) { return platformSource(game.source) == tile.source && game.kind != GameKind::nonsteam && isGame(game); }));
            if (games || tile.source == *pc_source_) tabs.push_back({sourceSlug(tile.source), tile.name, games});
        }
        return tabs;
    }
    if (const auto studio = studios::parse(text_encoding::utf8(library_console_))) {
        // A publisher's or developer's games: the others of its row, as their logos.
        const auto role = studio->first;
        for (const auto& entry : studio_groups_[static_cast<size_t>(role)]) {
            auto tile = studioTile(role, entry);
            const auto* games = consoleGames(tile.platform);
            if (games && !games->empty()) tabs.push_back({text_encoding::utf8(tile.platform), tile.name, games->size(), tile.platform});
        }
        return tabs;
    }
    if (studioBoard()) {
        // The boards of logos: Wydawcy, Developerzy.
        for (const auto& category : categories_)
            if (studioCategory(category.id) && !category.games.empty()) tabs.push_back({category.id, category.title, category.games.size()});
        return tabs;
    }
    if (!library_console_.empty()) {
        // The consoles of the Emulacja shelf.
        if (const auto* consoles = shelf("emulation")) for (const auto& tile : consoles->games) {
            if (tile.kind != GameKind::platform) continue;
            const auto found = platform_games_.find(tile.platform);
            if (found != platform_games_.end() && !found->second.empty())
                tabs.push_back({text_encoding::utf8(tile.platform), tile.name, found->second.size()});
        }
        return tabs;
    }
    const auto all = grouped_library_.empty() ? static_cast<size_t>(std::count_if(public_library_.begin(), public_library_.end(),
        [](const Game& game) { return isGame(game); })) : grouped_library_.size();
    tabs.push_back({{}, tr(L"Wszystkie gry"), all});
    for (const auto& category : categories_) {
        if (category.id == "pc-platforms" || category.id == "emulation") continue;
        const auto games = static_cast<size_t>(std::count_if(category.games.begin(), category.games.end(), [](const Game& game) { return isGame(game); }));
        if (games) tabs.push_back({category.id, category.title, games});
    }
    return tabs;
}
int Launcher::libraryTabIndex(const std::vector<LibraryTab>& tabs) const {
    const std::string current = pc_source_ ? std::string(sourceSlug(*pc_source_)) : !library_console_.empty() ? text_encoding::utf8(library_console_) : library_category_;
    for (size_t i = 0; i < tabs.size(); ++i) if (tabs[i].id == current) return static_cast<int>(i);
    return 0;
}
void Launcher::switchLibraryTab(int amount) {
    if (!libraryTabsShown() || !amount || menu_open_ || pending_launch_ || library_controls_.open()) return;
    const auto tabs = libraryTabs();
    const int current = libraryTabIndex(tabs), next = std::clamp(current + amount, 0, static_cast<int>(tabs.size()) - 1);
    if (next == current) { navigationBoundary(); return; }
    // The next category in the same view (its own order and filter stay as saved).
    const auto view = library_controls_.options.view;
    const auto& tab = tabs[next];
    if (pc_source_ || !library_console_.empty()) {
        // Another store or console: its own library, in the same view.
        if (pc_source_) {
            for (const auto source : {GameSource::steam, GameSource::gog, GameSource::epic, GameSource::ea, GameSource::ubisoft, GameSource::battlenet,
                                      GameSource::amazon, GameSource::xbox, GameSource::multimc, GameSource::drmfree, GameSource::custom})
                if (sourceSlug(source) == tab.id) { openPcLibrary(source); break; }
        } else openPlatform(text_encoding::wide(tab.id));
        auto options = libraryOptions();
        if (options.view != view) { options.view = view; library_state_.options[libraryScope()] = options; library_state_dirty_ = true; }
        library_controls_.options = libraryOptions();
        grid_scroll_ = 0;
        rebuildCards(false);
        focusCard(0);
        rememberPlace();
        audio_.navigate();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (tab.id.empty()) {
        versions_ = versions::Index(public_library_);
        grouped_library_ = versions_.collapse(public_library_, library_state_.preferred_versions, library_state_.first_seen);
        library_category_.clear(); category_library_.clear();
    } else {
        const auto category = std::find_if(categories_.begin(), categories_.end(), [&](const Category& item) { return item.id == tab.id; });
        if (category == categories_.end()) return;
        library_category_ = tab.id;
        category_library_title_ = category->title;
        category_library_.clear();
        for (const auto& game : category->games) if (isGame(game) || studioCategory(tab.id)) category_library_.push_back(game);
    }
    auto options = libraryOptions();
    if (options.view != view) { options.view = view; library_state_.options[libraryScope()] = options; library_state_dirty_ = true; }
    library_controls_.focus = library_controls_.action = -1;
    grid_scroll_ = 0;
    rebuildCards(false);
    focusCard(0);
    rememberPlace();
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
bool Launcher::libraryCoverflow() const { return libraryView() == library_view::View::coverflow; }
bool Launcher::libraryCarousel() const { return library_view::carousel(libraryView()); }
bool Launcher::libraryActions() const { return library_view::actions(libraryView()); }
bool Launcher::libraryButtons() const { return libraryActions() || libraryOverview(); }
settings_ui::Rect Launcher::libraryButton(int index) const {
    const auto view = renderer_.viewport();
    if (libraryOverview()) return overview_layout::Panel{view.width, view.height}.button(index);
    return library_presentation::Layout{view.width, view.height}.action(index);
}
int Launcher::libraryColumns() const { return library_layout::columns(libraryView(), renderer_.viewport().width, libraryPresentation()); }
bool Launcher::menuBeside() const { return xmbActive() || flixActive() || metroActive() || psfiveActive() || snitchActive() || libraryList() || libraryCarousel(); }
std::optional<settings_ui::Rect> Launcher::libraryMenuAnchor() const {
    if (screen_ == ScreenMode::details) {
        const auto view = renderer_.viewport();
        return details_layout::Layout{view.width, view.height}.rect(detailsActions(), 1);
    }
    if (!library_menu_from_action_) return std::nullopt;
    const auto view = renderer_.viewport();
    if (psfiveActive()) return psfive_layout::action(1, view.height, psfiveScale()); // "…" under the row.
    if (!libraryActions()) return std::nullopt;
    return library_presentation::Layout{view.width, view.height}.action(1);
}
bool Launcher::libraryTypeAhead() const {
    return screen_ == ScreenMode::library && !library_controls_.open() && !menu_open_ && !friends_open_ && !pending_launch_ &&
           !quick_open_ && !category_menu_open_ && !customEditorOpen() && !gogInstallOpen() && !power_menu_ && !exit_prompt_;
}
void Launcher::initializeLibraryPreferences() {
    const auto file = userConfigDirectory() / L"library-state.ini";
    atomic_file::removeStaleTemporaries(file.parent_path()); // Left by a crash or a killed process.
    auto saved = loadLibraryState(file);
    if (!saved) {
        // A damaged file (or one written by a newer version) is not a dead end: the rows this
        // build understands are kept, the original is set aside, and history works again.
        if (auto salvaged = salvageLibraryState(file)) {
            const auto aside = file.parent_path() / L"library-state.damaged.ini";
            if (CopyFileW(file.wstring().c_str(), aside.wstring().c_str(), FALSE)) {
                saved = std::move(salvaged->state);
                library_state_dirty_ = true;
                showNotification(NotificationKind::error, tr(L"Historia biblioteki była uszkodzona"),
                    tr(L"Odzyskano, co się dało. Oryginał: library-state.damaged.ini"));
            }
        }
    }
    library_state_readable_ = saved.has_value();
    library_state_ = saved ? std::move(*saved) : LibraryState{};
    restore_pending_ = false; // The home starts on its first tile (Ostatnio grane), not where the last session ended.
    if (library_state_readable_) library_writer_ = std::make_unique<LibraryStateWriter>(file);
    else showNotification(NotificationKind::error, tr(L"Nie można odczytać historii biblioteki"), tr(L"Plik jest zablokowany przez inny program. Zmiany nie będą zapisane do ponownego uruchomienia."));
}
void Launcher::pollLibrarySave() {
    if (!library_writer_) return;
    const auto result = library_writer_->takeResult();
    if (!result) return;
    if (!*result) {
        library_state_dirty_ = true;
        if (!library_state_save_failed_) showNotification(NotificationKind::error, tr(L"Nie udało się zapisać widoku biblioteki"), L"");
    }
    library_state_save_failed_ = !*result;
}
// F6 / Y: the filter menu opens at once (and closes again) without losing the focused game.
void Launcher::focusLibraryControls() {
    if (!libraryControls()) return;
    dismissMenu(); menu_progress_ = 0; closeFriends(); closeCategoryMenu();
    library_controls_.action = -1;
    if (library_controls_.panel == library_ui::Panel::filters) {
        closeLibraryPanel(); library_controls_.focus = -1; focusCard(selected_);
    } else {
        library_controls_.focus = library_ui::filters_button;
        if (!library_view::pinnedHeader(libraryView())) grid_scroll_ = 0;
        openLibraryPanel(library_ui::Panel::filters);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::openLibraryPanel(library_ui::Panel panel) {
    if (!libraryControls() || panel == library_ui::Panel::none) return;
    if (panel == library_ui::Panel::views && screen_ != ScreenMode::library) return;
    dismissMenu();
    library_controls_.action = -1;
    library_controls_.panel = library_panel_shown_ = panel;
    library_controls_.view_dropdown = false; library_controls_.view_row = 0;
    library_controls_.choice = panel == library_ui::Panel::filters ? static_cast<int>(library_controls_.options.order) :
                               library_view::drawerRow(library_controls_.options.view);
    library_choice_glide_.value.snap(static_cast<float>(library_controls_.choice));
    library_choice_glide_.valid = true;
    if (panel == library_ui::Panel::views) audio_.drawer(); else audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::closeLibraryPanel() {
    if (library_view_drag_) { library_view_drag_ = false; if (GetCapture() == hwnd_) ReleaseCapture(); }
    library_controls_.panel = library_ui::Panel::none;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::focusCard(size_t index, bool reveal) {
    if (cards_.empty()) return;
    snitch_profile_focus_ = false; // Snitch: a card takes the focus back from the profile picture.
    if (xmbActive()) { xmbFocusCard(std::min(index, cards_.size() - 1)); return; }
    if (flixActive()) { flixFocusCard(std::min(index, cards_.size() - 1)); return; }
    if (metroActive()) { metroFocus(std::min(index, cards_.size() - 1)); return; }
    category_rail_focus_ = false;
    library_controls_.action = -1;
    // A library refresh focusing the shelf's same card again keeps its settled background and title.
    const bool same_focus = screen_ == ScreenMode::shelves && std::min(index, cards_.size() - 1) == selected_;
    if (!same_focus) psfive_action_ = psfive_top_ = psfive_details_ = -1; // PSfive: another tile takes the focus back from the buttons and the top bar.
    selected_ = std::min(index, cards_.size() - 1);
    if (screen_ == ScreenMode::shelves) {
        categories_[category_].selected = selected_;
    } else if (gridScreen(screen_) && reveal) {
        const auto view = renderer_.viewport();
        grid_scroll_ = library_layout::reveal(libraryView(), selected_, cards_.size(), view.width, view.height, grid_scroll_, libraryPresentation());
    }
    if (!same_focus) detail_ = 0;
    library_focus_age_ = 0;
    if (library_view::metadata(libraryPresentation())) preview_.settle();
    if (screen_ == ScreenMode::shelves && cards_[selected_].game.kind == GameKind::category) flixFocusArt(); // "Wszystkie": its category's background.
    if (const auto* found = art_.find(gameKey(cards_[selected_].game))) {
        if (found->logo && !libraryListPlus()) renderer_.setLogo(selected_, found->logo);
        if ((screen_ == ScreenMode::shelves || screen_ == ScreenMode::platform_games || screen_ == ScreenMode::library) && found->hero)
            renderer_.setHero(selected_, found->hero, found->hero_tone, true, found->hero_focus); // Replaces a stand-in the slot kept.
    }
    requestVisibleArt(); InvalidateRect(hwnd_, nullptr, FALSE);
}
std::optional<size_t> Launcher::findCard(const std::string& key, bool selection_key) const {
    const auto matches = [&](const GameCard& card) { return (selection_key ? library_view::selectionKey(card.game) : gameKey(card.game)) == key; };
    if (flixActive() && flix_row_ < flix_rows_.size()) {
        const auto& row = flix_rows_[flix_row_];
        for (size_t i = row.begin; i < row.begin + row.count && i < cards_.size(); ++i) if (matches(cards_[i])) return i;
    }
    if (metroActive() && metro_page_ < metro_pages_.size()) { // A game can be on the start page and on its category's.
        const auto& page = metro_pages_[metro_page_];
        for (size_t i = page.begin; i < page.begin + page.count && i < cards_.size(); ++i) if (matches(cards_[i])) return i;
    }
    for (size_t i = 0; i < cards_.size(); ++i) if (matches(cards_[i])) return i;
    return std::nullopt;
}
bool Launcher::cardHit(size_t index, float x, float y) const {
    if (index >= cards_.size()) return false;
    const auto view = renderer_.viewport();
    if (categoryRailActive() && x < category_rail::Layout{view.width, view.height}.contentClip()) return false;
    if (psfiveActive() && x < psfive_layout::rowClipLeft()) return false;
    if (snitchActive() && x < snitch_layout::clip_left) return false;
    const auto& card = cards_[index];
    if (libraryBookshelf()) {
        // The covers as drawn in perspective around the gliding centre; nearer the middle on top.
        if (y < 180 || card.opacity < 0.05f) return false;
        const float flow = library_flow_.valid ? library_flow_.value.value : static_cast<float>(selected_);
        return shelf_layout::Layout{view.width, view.height}.hit(cards_.size(), flow, x, y) == index;
    }
    if (libraryCarousel()) {
        const auto hit = [&](const GameCard& item) {
            const auto r = coverflow_layout::poster(item);
            return r.contains(x, y);
        };
        if (y < 180 || card.opacity < 0.05f || !hit(card)) return false;
        if (index != selected_ && hit(cards_[selected_])) return false;
        // Match the draw order while posters overlap during a change of selection.
        for (size_t i = selected_ + 1; i < index; ++i)
            if (hit(cards_[i])) return false;
        return true;
    }
    // List rows scrolled under the header are hidden, as are grid cards under a pinned search header.
    if (libraryList()) {
        if (libraryListPlus() && index == selected_) {
            const auto poster = list_plus_layout::Layout{view.width, view.height}.poster();
            if (poster.contains(x, y)) return true;
        }
        const float left = library_layout::rowLeft(libraryView(), view.width, view.height);
        const float right = library_layout::rowRight(libraryView(), view.width, view.height);
        return y >= list_layout::list_top - 10 && y < view.height - list_layout::bottom_margin && x >= left && x < right &&
            y >= list_layout::rowTop(card.y) && y < list_layout::rowTop(card.y) + list_layout::row_height;
    }
    if (libraryCinematic() && y < (libraryPresentation().cinematic_column ? cinematic_column::list_top :
                                   libraryHorizontalCinematic() ? cinematic_row::top(view.width, view.height) : cinematic_layout::list_top) - 12) return false;
    if (libraryOverview() && (y < overview_layout::list_top - 6 || x < overview_layout::gridLeft(view.width) - 6)) return false;
    if (screen_ == ScreenMode::library && library_controls_.search && y < list_layout::list_top) return false;
    return card.contains(x, y);
}
std::string Launcher::libraryScope() const {
    // A console's library keeps the scope its old games screen used: saved choices still apply.
    const auto scope = screen_ == ScreenMode::platform_games ? "console:" + text_encoding::utf8(nested_platform_) :
        !library_console_.empty() ? "console:" + text_encoding::utf8(library_console_) : pc_source_ ? sourceSlug(*pc_source_) :
        !library_category_.empty() ? "category:" + library_category_ : std::string("all");
    return std::to_string(account_) + ':' + scope + ((!gog_state_.account.empty() && (pc_source_ == GameSource::gog || scope == "all")) ? ":gog:" + gog_state_.account : "") +
        ((!epic_state_.account.empty() && (pc_source_ == GameSource::epic || scope == "all")) ? ":epic:" + epic_state_.account : "");
}
library_view::Options Launcher::libraryOptions() const {
    if (studioBoard()) return {}; // The board of logos: square tiles, its own order.
    const auto found = library_state_.options.find(libraryScope());
    if (found != library_state_.options.end()) return found->second;
    library_view::Options options; options.view = defaultLibraryView(); // Not chosen for it yet: its kind's view.
    return options;
}
library_view::View Launcher::defaultLibraryView() const {
    const auto kind = viewKind(libraryScope());
    const int saved = kind == "console" ? settings_.default_view_console : kind == "collection" ? settings_.default_view_collection :
                      kind == "studio" ? settings_.default_view_studio : 0;
    return library_view::viewFromPersisted(std::clamp(saved, 0, library_view::persisted_view_count - 1));
}
int Launcher::libraryViewOverrides(std::string_view kind) const {
    return static_cast<int>(std::count_if(library_state_.options.begin(), library_state_.options.end(),
                                          [&](const auto& entry) { return viewKind(entry.first) == kind; }));
}
// Every library of the kind loses its own view, order and filter: it follows the default view again.
void Launcher::resetLibraryViews(std::string_view kind) {
    if (!library_state_readable_) { showNotification(NotificationKind::error, tr(L"Nie można odczytać historii biblioteki"), L""); return; }
    const auto removed = std::erase_if(library_state_.options, [&](const auto& entry) { return viewKind(entry.first) == kind; });
    if (removed) library_state_dirty_ = true;
    showNotification(NotificationKind::launch, removed ? tr(L"Przywrócono domyślny widok") : tr(L"Wszystkie mają domyślny widok"),
                     removed ? tr(L"Bibliotek: ") + std::to_wstring(removed) : std::wstring{});
    audio_.context();
    if (settingsOpen()) buildSettingsRows();
}
void Launcher::recordLibraryGames() {
    const auto now = unix_time::now();
    // The very first library is no news to her; later first sightings are new games.
    const bool first_library = library_state_.first_seen.empty();
    std::unordered_set<std::string> detected;
    // try_emplace: no map node is built for the (usual) key that is already there.
    // Games shown by Ctrl+H leave no trace in the saved library state.
    // Record the scanned inventory, including uninstalled games currently filtered out of the UI.
    // Showing them later is not a new addition. Disabled sources and hidden games stay unrecorded.
    for (const auto* list : {&installed_library_, &store_library_, &custom_library_, &drm_library_, &shortcut_library_, &uninstalled_library_, &public_library_})
        for (const auto& game : *list) {
            if (!isGame(game) || game.visibility != GameVisibility::public_game || locallyHidden(game) || excludedGame(game)) continue;
            if (game.source != GameSource::steam && game.source != GameSource::custom && !settings_.enabled_pc_sources.contains(sourceSlug(game.source))) continue;
            auto key = gameKey(game);
            if (key.empty() || !library_state_.first_seen.try_emplace(key, now).second) continue;
            library_state_dirty_ = true;
            if (!first_library) detected.insert(std::move(key));
        }
    std::vector<std::string> added;
    for (const auto& game : public_library_) if (auto key = gameKey(game); detected.erase(key)) added.push_back(std::move(key));
    companionLibraryChanged(std::move(added));
}
uint64_t Launcher::recentTime(const Game& game) const {
    if (game.installed || game.installation.active()) return game.last_played;
    // Not installed (Niezainstalowane w Ostatnio granych): by the date it was added, never by play.
    return settings_.recent_uninstalled ? library_view::addedTime(game, library_state_.first_seen) : 0;
}
void Launcher::refreshRecentGames() {
    const auto selected = cards_.empty() ? std::string{} : library_view::selectionKey(cards_[selected_].game);
    const auto category = categories_.empty() ? std::string{} : categories_[category_].id;
    buildShelves();
    const auto found_category = std::find_if(categories_.begin(), categories_.end(), [&](const Category& item) { return item.id == category; });
    if (found_category != categories_.end()) category_ = static_cast<size_t>(found_category - categories_.begin());
    if (screen_ == ScreenMode::search) updateSearch();
    else if (screen_ == ScreenMode::shelves || screen_ == ScreenMode::library || screen_ == ScreenMode::platform_games) rebuildCards();
    // Before the first key, click or pad press the home stays on its first tile as Recent re-sorts (a start-up scan,
    // a game's session): the focus follows the tile, not the game that happened to stand there.
    if (homeUntouched()) focusCard(0, false);
    else if (const auto found = findCard(selected, true)) focusCard(*found, false);
    saveLibraryPreferences();
}
void Launcher::rememberPlace() {
    // The desktop window keeps its own place (desktopSaveState); Tryb TV's bookmark stays as it was.
    if (desktop_mode_) return;
    // The Ctrl+H shelf is never a bookmark: it is off again after a restart.
    if (scanning_ || restore_pending_ || library_transition_ || theme_.open || cards_.empty() || categories_.empty() || // The studio's demo walks the focus: not a place to return to.
        (screen_ == ScreenMode::shelves && categories_[std::min(category_, categories_.size() - 1)].id == "private") ||
        (screen_ != ScreenMode::shelves && !libraryControls())) return;
    if (libraryControls() && (library_controls_.focus >= 0 || library_controls_.open() || library_controls_.search)) return;
    const auto& game = cards_[selected_].game;
    if (game.visibility != GameVisibility::public_game) return;
    LibraryBookmark next;
    next.account = account_;
    next.gog_account = gog_state_.account;
    next.epic_account = epic_state_.account;
    const bool console = screen_ == ScreenMode::platform_games || (screen_ == ScreenMode::library && !library_console_.empty());
    next.place = console ? LibraryPlace::platform : screen_ == ScreenMode::library ? LibraryPlace::library : LibraryPlace::shelves;
    next.category = categories_[category_].id;
    next.selection = library_view::selectionKey(game);
    if (screen_ == ScreenMode::library && pc_source_) next.source = sourceSlug(*pc_source_);
    else if (screen_ == ScreenMode::library && !library_category_.empty()) { next.source = "category"; next.category = library_category_; }
    if (console) next.platform = nested_platform_;
    next.scroll = screen_ == ScreenMode::library ? grid_scroll_ : 0;
    if (library_state_.bookmark != next) { library_state_.bookmark = std::move(next); library_state_dirty_ = true; }
}
void Launcher::saveLibraryPreferences() {
    if (!library_state_dirty_ || !library_state_readable_) return;
    if (library_writer_) {
        library_writer_->save(library_state_); library_state_dirty_ = false;
        return;
    }
    library_state_dirty_ = !saveLibraryState(library_state_, userConfigDirectory() / L"library-state.ini");
    if (library_state_dirty_ && !library_state_save_failed_) showNotification(NotificationKind::error, tr(L"Nie udało się zapisać widoku biblioteki"), L"");
    library_state_save_failed_ = library_state_dirty_;
}
bool Launcher::homeUntouched() const {
    return !input_seen_ && !desktop_mode_ && screen_ == ScreenMode::shelves && category_ == 0 && !cards_.empty();
}
bool Launcher::startupHold() {
    if (!startup_hold_) return false;
#ifdef LAUNCHME_UI_TEST
    startup_hold_ = false; return false; // The UI tests check the tiles at once.
#endif
    if (!runtime_started_ || desktopActive() || screen_ != ScreenMode::shelves || input_seen_) { startup_hold_ = !runtime_started_ && !desktop_mode_; return false; }
    if (cards_.empty()) { // Nothing to hold yet: the loading state shows on its own.
        if (!libraryLoading() && !rom_scanning_) startup_hold_ = false;
        return false;
    }
    const auto now = GetTickCount64();
    if (!startup_hold_until_) startup_hold_until_ = now + startup_hold_ms;
    const bool settled = !libraryLoading() && !pc_scanning_ && !rom_scanning_ && gog_initial_ready_ && epic_initial_ready_ && !restore_pending_;
    if (settled || now >= startup_hold_until_) startup_hold_ = false;
    return startup_hold_;
}
void Launcher::noteUserInput() {
    if (drm_idle_scan_ && drm_stop_) drm_stop_->store(true); // Idle scans give way to the user.
    cancelGameFocus();
    restore_pending_ = false;
    desktop_restore_key_.clear(); // The user moves on: a late scan must not pull the focus back.
    last_input_ = std::chrono::steady_clock::now();
    input_seen_ = true;
}
void Launcher::restorePlace() {
    // Settings, a device page or Guide's drawer opened before the scans finished: the saved place does not take it away.
    if (((screen_ == ScreenMode::settings || screen_ == ScreenMode::devices) && (overlay_return_ || anyActiveSession())) || gameOverlayOpen()) { restore_pending_ = false; return; }
    if (desktop_mode_ || !restore_pending_ || !library_state_.bookmark) { restore_pending_ = false; return; }
    const auto saved = *library_state_.bookmark;
    if (saved.account != account_) { restore_pending_ = false; return; }
    if (runtime_started_ && !gog_initial_ready_) return;
    if (runtime_started_ && !epic_initial_ready_) return;
    if (saved.gog_account != gog_state_.account) { restore_pending_ = false; return; }
    if (saved.epic_account != epic_state_.account) { restore_pending_ = false; return; }
    std::optional<GameSource> source;
    const bool category_library = saved.source == "category"; // A shelf category's own library.
    if (!saved.source.empty() && !category_library) {
        for (const auto candidate : {GameSource::steam, GameSource::gog, GameSource::battlenet, GameSource::ea, GameSource::ubisoft, GameSource::epic, GameSource::amazon,
                                     GameSource::xbox, GameSource::multimc, GameSource::drmfree, GameSource::custom})
            if (saved.source == sourceSlug(candidate)) source = candidate;
        // User-added and DRM-free games are loaded before the first shelves; stores wait for their scan.
        // Gry dodane ręcznie (an older bookmark) is DRM-free's library now.
        if (source) source = platformSource(*source);
        const bool user_games = source == GameSource::drmfree;
        if (!source) { restore_pending_ = false; return; }
        const bool empty = source == GameSource::drmfree ? (drm_library_.empty() || !settings_.enabled_pc_sources.contains(sourceSlug(GameSource::drmfree))) && custom_library_.empty() :
                           *source != GameSource::steam && !settings_.enabled_pc_sources.contains(saved.source);
        if (empty) { restore_pending_ = false; return; }
        if (*source != GameSource::steam && !user_games && !pc_initial_ready_) return;
    }
    restore_pending_ = false;
    const auto category = std::find_if(categories_.begin(), categories_.end(), [&](const Category& item) { return item.id == saved.category; });
    if (category == categories_.end()) return;
    showCategory(static_cast<size_t>(category - categories_.begin()));
    if (saved.place == LibraryPlace::library) {
        if (source) openPcLibrary(*source);
        else if (category_library) { openCategoryLibrary(saved.category); if (screen_ != ScreenMode::library) { showCategory(0); return; } }
        else openLibrary();
    } else if (saved.place == LibraryPlace::platform) {
        if (!settings_.show_emulation || !platform_games_.contains(saved.platform)) { showCategory(0); return; }
        openPlatform(saved.platform);
    }
    const auto found = findCard(saved.selection, true);
    if (!found || cards_[*found].game.visibility != GameVisibility::public_game) { showCategory(0); return; }
    const auto view = renderer_.viewport();
    if (screen_ == ScreenMode::library)
        grid_scroll_ = std::clamp(saved.scroll, 0.0f, library_layout::maxScroll(libraryView(), cards_.size(), view.width, view.height, libraryPresentation()));
    focusCard(*found);
}
void Launcher::applyLibraryOptions() {
    fast_scroll_alpha_ = 0;
    fast_scroll_at_ = fast_scroll_started_ = {};
    const auto key = cards_.empty() ? std::string{} : library_view::selectionKey(cards_[selected_].game);
    library_state_.options[libraryScope()] = library_controls_.options; library_state_dirty_ = true;
    grid_scroll_ = 0;
    rebuildCards();
    const auto found = findCard(key, true);
    focusCard(found ? *found : selected_, library_controls_.focus < 0);
    rememberPlace();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
bool Launcher::adjustLibraryView(int amount) {
    auto& options = library_controls_.options;
    const auto before = options;
    const auto toggle = [&](bool& value) { value = amount ? amount > 0 : !value; };
    // A list of values: Left/Right step without wrapping, Enter/A/a click cycles.
    const auto step = [&](int current, int count) { return amount ? std::clamp(current + amount, 0, count - 1) : (current + 1) % count; };
    using library_view::View;
    switch (options.view) {
    case View::grid: library_view::setGridArt(options, step(library_view::gridArt(options), 3)); break;
    case View::list:
        if (library_controls_.view_row == 1) toggle(options.list_year);
        else if (library_controls_.view_row == 2) toggle(options.list_playtime);
        else if (library_controls_.view_row == 3) toggle(options.list_rating);
        break;
    case View::cinematic:
        if (library_controls_.view_row <= 1) library_view::setCinematicScroll(options, step(library_view::cinematicScroll(options), 3));
        else if (library_controls_.view_row == 2) toggle(options.cinematic_platform);
        else if (library_controls_.view_row == 3) toggle(options.cinematic_played);
        else if (library_controls_.view_row == 4) toggle(options.cinematic_playtime);
        break;
    case View::overview: options.overview_columns = std::clamp(options.overview_columns + (amount ? amount : options.overview_columns == 5 ? -3 : 1), 2, 5); break;
    case View::coverflow:
    case View::bookshelf:
        // Wariant: Półka / Steelbook / Piracka płyta; Półka's own switches below it.
        if (library_controls_.view_row <= 1)
            library_view::setCoverflowVariant(options, step(library_view::coverflowVariant(options), library_view::coverflow_variants));
        else toggle(library_controls_.view_row == 2 ? options.bookshelf_gray : options.bookshelf_platform);
        break;
    case View::list_plus: toggle(options.list_plus_steelbook); break;
    default: return true;
    }
    if (options != before) { applyLibraryOptions(); audio_.navigate(); }
    return true;
}
// Left/Right adjusts options; an opening Left hold never dismisses the panel again.
// In the games, the row edges are handled by navigate() (no wrapping, Left opens the drawer).
bool Launcher::libraryHorizontal(int amount, bool edge_menu) {
    if (!libraryControls() || menu_open_ || pending_launch_ || quick_open_) return false;
    using library_ui::Panel;
    if (library_controls_.panel == Panel::views) {
        if (library_controls_.view_dropdown) return true;
        if (library_controls_.view_row > 0 && library_controls_.view_row < library_ui::views::closeRow(libraryView())) adjustLibraryView(amount);
        // Left opened this panel. Repeats remain here instead of closing and reopening it.
        return true;
    }
    if (library_controls_.panel == Panel::filters) return true;
    if (edge_menu && libraryOverview() && library_controls_.focus < 0 && !cards_.empty() && std::abs(amount) == 1) {
        // Przegląd: Left from the first column goes to the panel's buttons (the primary action
        // beside the tiles, then "Szczegóły"), Left past them opens the view drawer; Right returns.
        auto& action = library_controls_.action;
        if (action < 0) {
            if (amount > 0 || selected_ % static_cast<size_t>(libraryColumns()) != 0) return false; // The tiles move.
            action = 1;
        } else if (amount > 0 && action == 1) action = -1;
        else if (amount < 0 && action == 0) { openLibraryPanel(Panel::views); return true; }
        else action += amount;
        audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); return true;
    }
    if (edge_menu && libraryActions() && library_controls_.focus < 0 && !cards_.empty()) {
        auto& action = library_controls_.action;
        if (action >= 0) {
            if (amount < 0 && action == 0) action = -1;
            else { const int next = std::clamp(action + amount, 0, library_presentation::action_count - 1); if (next == action) navigationBoundary(); action = next; }
            audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); return true;
        }
        if (libraryView() == library_view::View::detailed && amount > 0) {
            action = 0; audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); return true;
        }
    }
    if (library_controls_.focus < 0) return false;
    if (amount < 0 && library_controls_.focus == library_ui::back_button && screen_ == ScreenMode::library) {
        openLibraryPanel(Panel::views); return true;
    }
    // Left to right: Back, filters, search, views. Only the library has the last two icons.
    const auto positionOf = [](int focus) { return focus == library_ui::back_button ? 0 : focus == library_ui::search_button ? 2 : focus == library_ui::views_button ? 3 : 1; };
    const int position = positionOf(library_controls_.focus);
    const int next_position = std::clamp(position + amount, 0, screen_ == ScreenMode::library ? 3 : 1);
    const int next = next_position == 0 ? library_ui::back_button : next_position == 2 ? library_ui::search_button :
        next_position == 3 ? library_ui::views_button : library_ui::filters_button;
    if (next == library_controls_.focus) navigationBoundary();
    if (next != library_controls_.focus) audio_.navigate();
    library_controls_.focus = next;
    InvalidateRect(hwnd_, nullptr, FALSE); return true;
}
bool Launcher::libraryVertical(int amount) {
    if (!libraryControls() || menu_open_ || pending_launch_ || quick_open_) return false;
    using library_ui::Panel;
    if (libraryOverview() && !library_controls_.open() && library_controls_.action >= 0) {
        // Przegląd's buttons sit at the bottom: Up goes to the top bar, nothing is below them.
        if (amount > 0) { navigationBoundary(); return true; }
        library_controls_.action = -1;
        library_controls_.focus = library_ui::filters_button;
        audio_.navigate();
    } else if (libraryActions() && !library_controls_.open() && library_controls_.action >= 0) {
        library_controls_.action = -1;
        if (amount < 0) library_controls_.focus = library_ui::filters_button;
        else focusCard(selected_);
    } else if (library_controls_.panel == Panel::views && !library_controls_.view_dropdown && amount < 0 && library_controls_.view_row == 0) {
        // Up past the first view: over to the top bar (magnifier), which the drawer shows.
        closeLibraryPanel();
        library_controls_.focus = library_ui::search_button;
        if (!library_view::pinnedHeader(libraryView())) grid_scroll_ = 0;
        audio_.navigate();
    } else if (library_controls_.panel == Panel::views && !library_controls_.view_dropdown) {
        library_controls_.view_row = std::clamp(library_controls_.view_row + amount, 0, library_ui::views::closeRow(libraryView()));
        audio_.navigate();
    } else if (library_controls_.panel == Panel::views && library_controls_.view_dropdown &&
               (library_controls_.view_row == library_ui::views::closeRow(libraryView()) ||
                (amount > 0 && library_controls_.choice + amount >= library_view::view_count))) {
        if (library_controls_.view_row == library_ui::views::closeRow(libraryView())) {
            if (amount < 0) { library_controls_.view_row = 0; library_controls_.choice = library_view::view_count - 1; audio_.navigate(); }
        } else { library_controls_.view_row = library_ui::views::closeRow(libraryView()); audio_.navigate(); }
    } else if (library_controls_.open()) {
        const int rows = library_controls_.panel == Panel::filters ? library_ui::filter_rows : library_view::view_count;
        const int next = std::clamp(library_controls_.choice + amount, 0, rows - 1);
        if (next == library_controls_.choice) navigationBoundary();
        if (next != library_controls_.choice) audio_.navigate();
        library_controls_.choice = next;
    } else if (library_controls_.focus >= 0) { if (amount > 0 && !cards_.empty()) { library_controls_.focus = -1; focusCard(selected_); } }
    else if (amount < 0 && studioBoard() && selected_ < static_cast<size_t>(libraryColumns())) { navigationBoundary(); return true; } // The board has no order or filter.
    else if (amount < 0 && (screen_ == ScreenMode::platform_games || libraryCarousel() || libraryHorizontalCinematic() || selected_ < static_cast<size_t>(libraryColumns()))) {
        // With the search field open, Up returns to it.
        library_controls_.focus = library_controls_.search ? library_ui::search_button : library_ui::filters_button;
        // A pinned toolbar is already visible: moving focus must not move the games beneath it.
        if (!library_view::pinnedHeader(libraryView())) grid_scroll_ = 0;
    } else if (cards_.empty()) library_controls_.focus = library_controls_.search ? library_ui::search_button : library_ui::filters_button;
    else if (!libraryCarousel() && !libraryHorizontalCinematic()) return false;
    InvalidateRect(hwnd_, nullptr, FALSE); return true;
}
bool Launcher::libraryAccept() {
    if (!libraryControls() || menu_open_ || pending_launch_ || friends_open_ || quick_open_) return false;
    using library_ui::Panel;
    auto& options = library_controls_.options;
    if (library_controls_.panel == Panel::views) {
        if (library_controls_.view_row == library_ui::views::closeRow(options.view)) {
            closeLibraryPanel(); library_controls_.focus = -1; audio_.navigate(); return true;
        }
        if (!library_controls_.view_dropdown) {
            if (library_controls_.view_row > 0) return adjustLibraryView(0);
            library_controls_.view_dropdown = true;
            library_controls_.choice = library_view::drawerRow(options.view);
            library_choice_glide_.value.snap(static_cast<float>(library_controls_.choice));
            InvalidateRect(hwnd_, nullptr, FALSE); return true;
        }
        const auto view = library_view::drawerView(library_controls_.choice);
        library_controls_.view_dropdown = false; library_controls_.view_row = 0;
        audio_.context();
        // Coverflow's row keeps the variant already shown (Półka or the flat carousel).
        if (library_view::drawerRow(view) != library_view::drawerRow(options.view)) { options.view = view; applyLibraryOptions(); }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    if (library_controls_.panel == Panel::filters) {
        // Orders are exclusive, the last row toggles; every choice dismisses the menu.
        if (library_controls_.choice < library_view::order_count) options.order = static_cast<library_view::Order>(library_controls_.choice);
        else options.installed_only = !options.installed_only;
        closeLibraryPanel(); audio_.context(); applyLibraryOptions();
    } else if (libraryOverview() && library_controls_.action >= 0 && !cards_.empty()) {
        if (library_controls_.action == 1) return !actionEnabled(cards_[selected_].game); // launch() handles the primary action.
        openDetails(cards_[selected_].game); // "Szczegóły" keeps the focus for the way back.
        return true;
    } else if (libraryActions() && library_controls_.action >= 0 && !cards_.empty()) {
        if (library_controls_.action == 0) return !actionEnabled(cards_[selected_].game); // launch() handles the primary action.
        library_controls_.action = -1;
        openContextMenu();
        library_menu_from_action_ = menu_open_;
        return true;
    } else if (library_controls_.focus < 0) return false;
    else if (library_controls_.focus == library_ui::back_button) {
        // Back: leaves the library as B does from the games (an open search field closes with it).
        library_controls_.focus = -1;
        if (library_controls_.search) closeLibrarySearch();
        back();
        return true;
    } else if (library_controls_.focus == library_ui::search_button) {
        // Enter in an open field goes down to the (filtered) games.
        if (!library_controls_.search) openLibrarySearch();
        else if (!cards_.empty()) {
            if (library_query_dirty_) applyLibraryQuery();
            library_controls_.focus = -1;
            focusCard(selected_);
        }
    } else openLibraryPanel(library_controls_.focus == library_ui::views_button ? Panel::views : Panel::filters);
    InvalidateRect(hwnd_, nullptr, FALSE); return true;
}
bool Launcher::libraryClick(float x, float y, bool double_click) {
    if (!libraryControls()) return false;
    using library_ui::Panel;
    const auto view = renderer_.viewport();
    const library_ui::Layout layout{view.width};
    if (library_controls_.panel == Panel::filters) {
        if (const int row = layout.hitChoice(x, y); row >= 0) { library_controls_.choice = row; return libraryAccept(); }
        if (!layout.menu().contains(x, y)) closeLibraryPanel();
        return true;
    }
    if (library_controls_.panel == Panel::views) {
        if (double_click) return true;
        x -= library_ui::views::offset(library_views_progress_);
        if (library_ui::views::close(view.height).contains(x, y)) {
            library_controls_.view_row = library_ui::views::closeRow(libraryView()); return libraryAccept();
        }
        if (library_controls_.view_dropdown) {
            if (const int row = library_ui::views::hit(x, y, view.height, library_dropdown_progress_); row >= 0) {
                library_controls_.view_row = 0; library_controls_.choice = row; return libraryAccept();
            }
            // Rows which are still being revealed, and their gaps, are not click targets.
            if (x >= library_ui::views::padding && x < library_ui::views::width - library_ui::views::padding &&
                y >= library_ui::views::top(view.height) && y < library_ui::views::revealedBottom(view.height, 1)) return true;
            library_controls_.view_dropdown = false; InvalidateRect(hwnd_, nullptr, FALSE); return true;
        }
        if (const int row = library_ui::views::hitOption(x, y, library_controls_, view.height, library_dropdown_progress_); row >= 0) {
            library_controls_.view_row = row;
            if (row == 1 && libraryOverview()) {
                const int columns = library_ui::views::columnsAt(x);
                if (columns != library_controls_.options.overview_columns) { library_controls_.options.overview_columns = columns; applyLibraryOptions(); }
                library_view_drag_ = true; SetCapture(hwnd_); return true;
            }
            return libraryAccept();
        }
        if (!library_ui::views::panel(view.height).contains(x, y)) closeLibraryPanel();
        return true;
    }
    // With the field open, both it and the magnifier just put the caret back.
    if (library_controls_.search && (layout.field().contains(x, y) || layout.search().contains(x, y))) {
        library_controls_.action = -1; library_controls_.focus = library_ui::search_button;
        InvalidateRect(hwnd_, nullptr, FALSE); return true;
    }
    const bool bar = grid_scroll_ < 80 || screen_ == ScreenMode::platform_games || library_controls_.search || library_view::pinnedHeader(libraryView());
    if (bar) for (const int i : {library_ui::filters_button, library_ui::search_button, library_ui::views_button}) {
        if (i != library_ui::filters_button && screen_ != ScreenMode::library) continue;
        if (layout.button(i).contains(x, y)) { library_controls_.action = -1; library_controls_.focus = i; return libraryAccept(); }
    }
    if (bar && libraryTabsShown()) {
        // A neighbouring tab switches the category; key hints live in the bottom bar.
        if (const auto offset = library_tabs::hit(x, y, view.width, libraryTabIndex(libraryTabs()))) {
            if (*offset) switchLibraryTab(*offset);
            return true;
        }
    }
    if (libraryOverview() && !cards_.empty()) {
        // The panel's buttons: "Szczegóły" and the primary action.
        const int button = overview_layout::Panel{view.width, view.height}.hitButton(x, y);
        if (button >= 0) {
            library_controls_.focus = library_controls_.action = -1;
            if (!double_click) { if (button == 0) openDetails(cards_[selected_].game); else launch(); }
            return true;
        }
    }
    library_controls_.focus = -1;
    if (libraryActions() && !cards_.empty()) {
        const int action = library_presentation::Layout{view.width, view.height}.hitAction(x, y);
        if (action >= 0) {
            if (!double_click) { library_controls_.action = action; launch(); InvalidateRect(hwnd_, nullptr, FALSE); }
            return true;
        }
    }
    library_controls_.action = -1;
    return false;
}
// The magnifier unfolds the search field under the title; typing filters the games below
// (grid or list) in their current order. The home screen's search is a separate screen.
void Launcher::openLibrarySearch() {
    if (screen_ != ScreenMode::library) return;
    closeLibraryPanel();
    library_controls_.action = -1;
    library_controls_.search = true;
    library_controls_.focus = library_ui::search_button;
    grid_scroll_ = 0;
    audio_.quick();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::closeLibrarySearch() {
    const bool filtered = !library_controls_.query.empty();
    library_controls_.search = false;
    library_controls_.query.clear(); library_controls_.after = 0;
    library_query_dirty_ = false;
    if (filtered) applyLibraryQuery(); // The full list again, around the same game.
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::applyLibraryQuery() {
    library_query_dirty_ = false;
    const auto key = cards_.empty() ? std::string{} : library_view::selectionKey(cards_[selected_].game);
    grid_scroll_ = 0;
    rebuildCards();
    const auto found = findCard(key, true);
    if (!cards_.empty()) focusCard(found ? *found : 0, library_controls_.focus < 0);
    InvalidateRect(hwnd_, nullptr, FALSE);
}
// The default home's facts under the logo (Opcje układu → Informacje pod logo).
bool Launcher::heroFactsShown() const {
    return settings_.shelves_hero_facts && screen_ == ScreenMode::shelves && settings_.home_layout == AppSettings::HomeLayout::shelves &&
           !desktopActive() && !cards_.empty() && selected_ < cards_.size() && isGame(cards_[selected_].game);
}
void Launcher::requestPreview() {
    const bool hero = heroFactsShown();
    if (!runtime_started_ || (!library_view::metadata(libraryPresentation()) && !hero) || cards_.empty()) return;
    const auto& selected = cards_[selected_].game;
    preview_.request(selected, account_);
    if (hero) return; // Only the focused game: its playtime and release date.
    if ((libraryView() != library_view::View::list && !libraryListPlus()) || (steamArtwork(selected) && !preview_.find(selected))) return;
    if (libraryView() == library_view::View::list && !libraryPresentation().list_year && !libraryPresentation().list_playtime) return;
    // Once the focused preview is ready, warm the visible list one row at a time. The same
    // debounce/latest-request worker gives a new selection priority over this background work.
    const auto view = renderer_.viewport();
    const auto [first, end] = list_layout::visibleRange(cards_.size(), view.height, grid_scroll_);
    for (size_t i = first; i < end; ++i) {
        const auto& card = cards_[i];
        const float top = list_layout::top(i, grid_scroll_);
        if (top + list_layout::row_height <= list_layout::list_top - 10 || top >= view.height - list_layout::bottom_margin ||
            !steamArtwork(card.game) || preview_.find(card.game)) continue;
        preview_.request(card.game, account_);
        break;
    }
}
void Launcher::onPreview(std::unique_ptr<PreviewMessage> message) {
    if (message->app_id) gameIndexTouch({"steam:" + std::to_string(message->app_id)}); // Its store page is cached now.
    if (preview_.accept(std::move(*message)) && (library_view::metadata(libraryPresentation()) || heroFactsShown() || psfiveDetailsShown())) InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::noteFastScroll() {
    if (screen_ != ScreenMode::library || library_controls_.options.order != library_view::Order::alphabetical) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - fast_scroll_at_ >= fast_scroll_gap) fast_scroll_started_ = now; // A new run of moves.
    fast_scroll_at_ = now;
}
std::wstring Launcher::scrollLetter() const {
    if (cards_.empty() || screen_ != ScreenMode::library || library_controls_.options.order != library_view::Order::alphabetical) return {};
    for (const wchar_t ch : cards_[selected_].game.name) {
        if (!IsCharAlphaNumericW(ch)) continue;
        if (ch >= L'0' && ch <= L'9') return L"#";
        wchar_t upper[2]{ch, 0};
        CharUpperBuffW(upper, 1);
        return std::wstring(1, upper[0]);
    }
    return L"#";
}
