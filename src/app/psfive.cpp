// PSfive home layout, after the PS5 home screen. It is the default shelf (cards_ = the shown
// category's games; Recent holds psfive_layout::row_games of them and ends with "Wszystkie gry",
// another game category its first row_games games and a "Wszystkie" tile for its library) drawn
// as one row of square covers at the top over the focused game's full-screen background, so
// building, focus, bookmarks and artwork are the shelves' own. What differs: the focused tile is
// pinned to the shared left margin (psfive_layout.hpp), and Enter/A or a click on a game
// moves the focus down to its buttons, "Graj" and "…" (the game menu), instead of starting it.
// Up from the row goes to the top bar (three categories, "Więcej...", Widgets, Menu = the quick
// bar); LB/RB change the tab; Down from the buttons opens the quick bar, or with Szczegóły pod rzędem
// (on by default) scrolls to the game's details and the quick bar opens from above the tabs.
#include "launcher.hpp"
#include "../ui/psfive_layout.hpp"
#include "../ui/grid_layout.hpp"

#include <algorithm>
#include <cstdlib>

bool Launcher::psfiveActive() const {
    return settings_.home_layout == AppSettings::HomeLayout::psfive && screen_ == ScreenMode::shelves && !desktop_mode_;
}

float Launcher::psfiveScale() const { return psfive_layout::sizeScale(static_cast<int>(settings_.psfive_size)); }

bool Launcher::psfiveTopAvailable() const {
    return psfiveActive() && !pending_launch_ && !friends_open_ && !quick_open_ && !menu_open_ && !keyboard_.open &&
        !exit_prompt_ && !power_menu_ && !tour_.open && !launchAnimationActive() && !screensaverActive() && !identifyOpen() &&
        !gogInstallOpen() && !customEditorOpen() && !fileBrowserOpen() && !libraryDialogOpen();
}

bool Launcher::psfiveTopActive() const {
    return psfiveTopAvailable() && (psfiveTopFocused() || psfive_top_hover_ || category_menu_open_);
}

float Launcher::psfiveMenuScroll() const {
    const float scroll = psfive_menu_motion_.valid ? psfive_menu_motion_.scroll.value : category_menu_scroll_;
    return std::clamp(scroll, 0.0f, renderer_.psfiveMoreMenu(categories_.size()).maxScroll());
}

bool Launcher::psfiveAllTile() const {
    if (!psfiveActive() || category_ >= categories_.size()) return false;
    // Not Recent (it ends with "Wszystkie gry"), stores or consoles, as on Flix and the shelves.
    const auto& category = categories_[category_];
    return category.id != "recent" && category.id != "pc-platforms" && category.id != "emulation" &&
           std::any_of(category.games.begin(), category.games.end(), [](const Game& game) { return isGame(game); });
}

bool Launcher::psfiveButtons() const {
    if (selected_ >= cards_.size()) return false;
    const auto& game = cards_[selected_].game;
    return isGame(game) && game.kind != GameKind::catalog;
}

void Launcher::psfiveFocusAction(int action) {
    const int next = action >= 0 && psfiveButtons() ? std::min(action, psfive_layout::action_count - 1) : -1;
    if (next >= 0) psfive_top_ = -1;
    if (next == psfive_action_) return;
    psfive_action_ = next;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

// Its own top bar: three categories and "Więcej..."; with "Pasek kategorii" (psfiveStrip) every
// category is a tab of the strip in the middle, with no overflow.
std::vector<size_t> Launcher::psfiveTabs() const {
    std::vector<size_t> tabs;
    const size_t shown = psfiveStrip() ? categories_.size() : std::min(categories_.size(), psfive_layout::visible_tabs);
    for (size_t i = 0; i < shown; ++i) tabs.push_back(i);
    return tabs;
}

int Launcher::psfiveTabCount() const {
    if (psfiveStrip()) return static_cast<int>(categories_.size());
    return static_cast<int>(std::min(categories_.size(), psfive_layout::visible_tabs + 1));
}

int Launcher::psfiveActiveTab() const {
    const auto tabs = psfiveTabs();
    const auto found = std::find(tabs.begin(), tabs.end(), category_);
    return found == tabs.end() ? (categories_.size() > tabs.size() ? static_cast<int>(tabs.size()) : -1) : static_cast<int>(found - tabs.begin());
}

// A tab shows its category as the row (each keeps its own focused tile); keep_focus leaves the
// top bar's focus on the tab, otherwise the row has it.
void Launcher::psfiveSelectTab(int tab, bool keep_focus) {
    const auto tabs = psfiveTabs();
    if (tab < 0 || tab >= static_cast<int>(tabs.size())) return;
    dismissMenu();
    if (tabs[static_cast<size_t>(tab)] != category_) { showCategory(tabs[static_cast<size_t>(tab)]); audio_.navigate(); }
    psfive_action_ = -1;
    psfive_top_ = keep_focus ? tab : -1;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::psfiveFocusTop(int item) {
    const int count = psfiveTabCount() + psfive_layout::top_count;
    const int next = item >= 0 && psfiveActive() ? std::min(item, count - 1) : -1;
    if (next >= 0) psfive_action_ = -1;
    if (next == psfive_top_) return;
    psfive_top_ = next;
    if (!psfiveStrip() && next == static_cast<int>(psfive_layout::visible_tabs) && next < psfiveTabCount()) openCategoryMenu();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::psfiveTopActivate() {
    if (!psfiveTopFocused()) return;
    const int tabs = static_cast<int>(psfiveTabs().size());
    if (psfive_top_ < tabs) { psfiveFocusTop(-1); audio_.navigate(); return; } // A tab: down to its row.
    if (psfive_top_ < psfiveTabCount()) openCategoryMenu();
    else if (psfive_top_ - psfiveTabCount() == psfive_layout::top_widgets) toggleFriendsDrawer();
    else openQuick(); // Menu: the quick bar; closing it returns here.
}

void Launcher::psfiveTarget(size_t index, float& x, float& y, float& size, float& aspect, float& delay) const {
    const int offset = static_cast<int>(index) - static_cast<int>(selected_);
    const float scale = psfiveScale();
    size = psfive_layout::tileSize(offset == 0, scale);
    x = psfive_layout::tileLeft(offset, scale);
    y = psfive_layout::row_top;
    aspect = 1;
    // The row enters from the focused tile outwards.
    delay = static_cast<float>(std::min(std::abs(offset), 8)) * 0.04f;
}

bool Launcher::psfiveAccept() {
    if (!psfiveActive() || cards_.empty() || selected_ >= cards_.size() || pending_launch_ || friends_open_ ||
        category_menu_open_ || quick_open_ || menu_open_ || launchAnimationActive() || identifyOpen() || gogInstallOpen() ||
        customEditorOpen() || fileBrowserOpen() || libraryDialogOpen()) return false;
    if (psfiveTopFocused()) { psfiveTopActivate(); return true; }
    if (psfiveDetailsShown()) { psfiveDetailsAccept(); return true; }
    if (psfiveActions()) { psfiveActivate(); return true; }
    // The library, "Wszystkie" and store/console tiles open at once (launch()).
    if (!psfiveButtons()) return false;
    // A game: its details (the default game action), else down to "Graj" / "…" (Down reaches them too).
    if (psfiveOpensDetails()) { openDetails(cards_[selected_].game); return true; }
    psfiveFocusAction(0);
    audio_.navigate();
    return true;
}

bool Launcher::psfiveOpensDetails() const {
    return settings_.accept_action == AppSettings::AcceptAction::details && selected_ < cards_.size() &&
           isGame(cards_[selected_].game) && cards_[selected_].game.kind != GameKind::catalog;
}

void Launcher::psfiveActivate() {
    if (!psfiveActions()) return;
    if (psfive_action_ == 1) { // "…": the game menu, unfolding from the button.
        openContextMenu();
        library_menu_from_action_ = menu_open_;
        return;
    }
    if (!actionEnabled(cards_[selected_].game)) { navigationBoundary(); return; }
    launch(); // "Graj" starts (or installs) at once, whatever the default game action is.
}

// Between "Graj" and "…", or along the top bar: moving onto a tab shows its row at once (as the
// Metro tabs), past the last category come "Więcej...", Widgets and Menu.
void Launcher::psfiveHorizontal(int amount) {
    if (psfiveTopFocused()) {
        const int tabs = static_cast<int>(psfiveTabs().size());
        const int next = std::clamp(psfive_top_ + amount, 0, psfiveTabCount() + psfive_layout::top_count - 1);
        if (next == psfive_top_) { navigationBoundary(); return; }
        if (next < tabs) { psfiveSelectTab(next, true); return; }
        psfiveFocusTop(next);
        audio_.navigate();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const int next = std::clamp(psfive_action_ + amount, 0, psfive_layout::action_count - 1);
    if (next == psfive_action_) { navigationBoundary(); return; }
    psfive_action_ = next;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::psfiveVertical(int amount) {
    if (cards_.empty() || !amount) return;
    if (psfiveDetailsShown()) { psfiveDetailsVertical(amount); return; } // The details under the row (psfive_details.cpp).
    // Opcje układu → Szczegóły pod rzędem: Down from the buttons scrolls to the game's details and
    // the menu (quick bar) comes from above the tabs; without it the menu is under the buttons.
    const bool details = settings_.psfive_details;
    if (psfiveTopFocused()) { // The top bar: Down back to the row.
        if (amount > 0) {
            if (!psfiveStrip() && psfive_top_ == static_cast<int>(psfive_layout::visible_tabs) && psfive_top_ < psfiveTabCount()) openCategoryMenu();
            else { psfiveFocusTop(-1); audio_.navigate(); }
        }
        else if (details) openQuick(); // Up past the very top: the menu.
        else navigationBoundary();
        return;
    }
    if (psfiveActions()) {
        // The buttons: Up back to the row, Down to the details (or the quick bar under them).
        if (amount < 0) { psfiveFocusAction(-1); audio_.navigate(); }
        else if (details) psfiveOpenDetails();
        else openQuick();
        return;
    }
    if (amount < 0) { // Up: the shown category, or "Więcej..." for an overflow category.
        const int tab = psfiveActiveTab();
        psfiveFocusTop(tab >= 0 ? tab : 0);
        audio_.navigate();
        return;
    }
    if (psfiveButtons()) { psfiveFocusAction(0); audio_.navigate(); }
    else if (settings_.psfive_details) navigationBoundary(); // The menu is above the tabs.
    else openQuick(); // The library and "Wszystkie" tiles have no buttons.
}

void Launcher::psfiveClickCard(size_t index, bool double_click) {
    if (index >= cards_.size()) return;
    psfiveFocusTop(-1);
    const bool moved = index != selected_;
    if (moved) { focusCard(index, false); audio_.navigate(); }
    const auto game = cards_[index].game;
    if (game.kind == GameKind::all) { openLibrary(); return; }
    if (specialTile(game)) { launch(); return; } // Glance, Tarot, Towarzyszka, Dodaj grę as tiles: one click.
    if (game.kind == GameKind::category) { openCategoryLibrary(game.source_id); return; }
    if (game.kind == GameKind::platform || game.kind == GameKind::pc_platform) {
        categories_[category_].selected = index;
        if (game.kind == GameKind::platform) openPlatform(game.platform); else openPcLibrary(game.source);
        return;
    }
    // A game: the first click focuses it, a click on the focused tile goes down to "Graj" and a
    // double click opens it the default game action's way (its details, or "Graj" pressed).
    if (moved || !psfiveButtons()) return;
    if (double_click && psfiveOpensDetails()) { psfiveFocusAction(-1); openDetails(game); return; }
    psfive_action_ = 0;
    if (double_click) psfiveActivate();
    else { audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE); }
}

void Launcher::psfiveHover(float x, float y) {
    if (!psfiveTopAvailable()) {
        psfive_more_hover_ = psfive_top_hover_ = false;
        return;
    }
    const auto view = renderer_.viewport();
    const int tab = renderer_.psfiveTabAt(x, y);
    const bool strip = psfiveStrip() && library_tabs::home::hit(x, y, view.width, homeTabIndex()).has_value();
    const bool top = tab >= 0 || strip || psfive_layout::hitTop(x, y, view.width, settings_.psfive_clock) >= 0 ||
        (settings_.psfive_clock && psfive_layout::clock(view.width).contains(x, y));
    if (top != psfive_top_hover_) { psfive_top_hover_ = top; InvalidateRect(hwnd_, nullptr, FALSE); }
    const bool more = categories_.size() > psfive_layout::visible_tabs &&
        tab == static_cast<int>(psfive_layout::visible_tabs);
    const bool entered = more && !psfive_more_hover_;
    psfive_more_hover_ = more;
    if (entered && !category_menu_open_) openCategoryMenu();
    if (category_menu_open_) {
        const int row = renderer_.psfiveMoreMenu(categories_.size()).hit(x, y, psfiveMenuScroll(), category_menu_progress_);
        if (row >= 0 && row != category_cursor_) {
            category_cursor_ = row;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
}

bool Launcher::psfiveClick(float x, float y, bool double_click) {
    if (!psfiveActive() || menu_open_) return false;
    if (!double_click && psfiveDetailsClick(x, y)) return true; // The details sheet's strips.
    if (psfiveDetailsShown()) return true; // Nothing under the sheet takes clicks.
    const auto view = renderer_.viewport();
    if (psfiveStrip() && homeTabsClick(x, y)) return true; // "Pasek kategorii" in place of the tabs.
    // Category tabs show their row; the overflow opens beneath its tab.
    if (const int tab = renderer_.psfiveTabAt(x, y); tab >= 0) {
        if (!double_click) {
            if (tab < static_cast<int>(psfiveTabs().size())) psfiveSelectTab(tab, false);
            else openCategoryMenu();
        }
        return true;
    }
    if (const int item = psfive_layout::hitTop(x, y, view.width, settings_.psfive_clock); item >= 0) {
        if (double_click) return true;
        if (item == psfive_layout::top_widgets) toggleFriendsDrawer(); else openQuick();
        return true;
    }
    if (!psfiveButtons()) return false;
    const int action = psfive_layout::hitAction(x, y, view.height, psfiveScale());
    if (action < 0) return false;
    if (double_click) return true; // The first click already acted.
    psfive_action_ = action;
    psfiveActivate();
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}
