// Desktop mode, the sidebar's newer parts (src/ui/desktop_view.hpp): Znajomi and Profil under
// Ulubione, Ostatnio grane's three shortcuts with their square covers and Kontynuuj grę's wide tiles
// above Wszystkie gry (one slot list, Renderer::desktop_recent_), the cogs on the headings of
// Biblioteka, Platformy and Emulacja, and Ustawienia and Profil taking the library's place beside a
// sidebar and top bar that keep working: a click there leaves them for the library.
#include "launcher.hpp"
#include "launcher_internal.hpp"
#include "../library/steam_stats.hpp"
#include "../ui/friends_layout.hpp"

#include <algorithm>

using namespace desktop_ui;

std::wstring desktopPlayedWhen(uint64_t seconds); // desktop.cpp

namespace {
constexpr float profile_compact = 0.86f; // As Renderer::desktopModalViewport: Tryb TV's layouts at desktop proportions.
}

// ---- Entries that act ----------------------------------------------------------------------------

void Launcher::desktopPickEntry(const desktop_ui::Entry& entry, bool double_click) {
    switch (entry.kind) {
    case EntryKind::friends: desktopOpenFriends(); return;
    case EntryKind::profile:
        if (profile_.open) return;
        if (desktopSheet()) desktopLeavePage();
        openProfile();
        return;
    case EntryKind::downloads: case EntryKind::settings: case EntryKind::store: { // Pages in the library's place.
        const bool here = (entry.kind == EntryKind::downloads && screen_ == ScreenMode::downloads) ||
                          (entry.kind == EntryKind::settings && screen_ == ScreenMode::settings) ||
                          (entry.kind == EntryKind::store && (screen_ == ScreenMode::store || desktopStorePage()));
        if (here) return;
        if (profile_.open) closeProfile();
        if (desktopSheet()) desktopLeavePage();
        if (entry.kind == EntryKind::downloads) openDownloads();
        else if (entry.kind == EntryKind::settings) openSettings();
        else openStores();
        return;
    }
    case EntryKind::game:
        desktopRevealGame(entry.game);
        if (double_click && !cards_.empty() && selected_ < desktop_keys_.size() && desktop_keys_[selected_] == entry.game) desktopPickSelected();
        return;
    default: desktopShowSection(entry.id); return;
    }
}

// A game's card selected and in view, its pane open: in the section shown when it is there, else in
// Wszystkie gry with the search cleared.
void Launcher::desktopRevealGame(const std::string& key) {
    if (!desktopActive() || key.empty()) return;
    const auto index = [&] { return static_cast<size_t>(std::find(desktop_keys_.begin(), desktop_keys_.end(), key) - desktop_keys_.begin()); };
    if (index() >= desktop_keys_.size()) {
        desktop_query_.clear(); desktop_caret_ = 0; desktop_query_dirty_ = false;
        desktopShowSection("all");
    }
    const size_t found = index();
    if (found >= desktop_keys_.size()) return;
    desktop_marked_.clear();
    desktop_zone_ = 1;
    desktop_anchor_ = found;
    const bool closed = std::exchange(desktop_pane_closed_, false);
    desktopFocus(found, true);
    if (closed) desktopReflow();
}

// Znajomi: the drawer straight on its list of friends.
void Launcher::desktopOpenFriends() {
    if (profile_.open) closeProfile();
    if (desktopSheet()) desktopLeavePage();
    if (!friends_open_) toggleFriendsDrawer();
    if (friends_open_ && !friends_list_) dashboardActivate(static_cast<int>(friends_layout::dashboard::Tile::steam));
}

// A heading's cog: Biblioteka's category (at its platforms for Platformy), Emulacja's for Emulacja.
void Launcher::desktopGroupSettings(desktop_ui::Group group) {
    if (profile_.open) closeProfile();
    const int library = static_cast<int>(group == Group::emulation ? settings_ui::Category::emulation : settings_ui::Category::library);
    if (!settingsOpen()) { settings_state_.category = library; openSettings(); }
    else {
        settings_state_.closeAll();
        settings_state_.selectCategory(library);
        settings_state_.options = settings_state_.editing = false;
        settings_motion_.valid = false;
        buildSettingsRows(); settingsPreviewFocus();
    }
    desktop_sheet_scroll_ = desktop_sheet_scroll_target_ = 0;
    if (group == Group::platforms) revealSetting(settings_ui::Id::pc_platform);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Kontynuuj grę: Wszystkie gry with nothing searched, once something was played. Not while the grid
// is sorted by Ostatnio grane: its first posters (or rows) are those games already.
bool Launcher::desktopContinueShown() const {
    if (!desktop_mode_ || desktop_recent_games_.empty() || !trimmedQuery(desktop_query_).empty()) return false;
    if (library_view::orderFromPersisted(settings_.desktop_sort) == library_view::Order::last_played) return false;
    const auto* entry = desktopEntry();
    return !entry || entry->kind == EntryKind::all;
}

// ---- The recent games' pictures --------------------------------------------------------------------

// Slots 0-2: the square covers (sidebar rows, stand-ins); 3-5: the landscape pictures (the tiles).
void Launcher::desktopBindRecentArt() {
    std::vector<std::string> keys;
    for (const auto& game : desktop_recent_games_) keys.push_back(gameKey(game));
    for (size_t i = 0; i < 3; ++i)
        if (i >= keys.size() || i >= desktop_recent_keys_.size() || desktop_recent_keys_[i] != keys[i]) {
            renderer_.clearDesktopRecent(i);
            renderer_.clearDesktopRecent(3 + i);
        }
    desktop_recent_keys_ = keys;
    if (!runtime_started_) return; // The first frame of a cold start asks for them after the library's own art.
    for (size_t i = 0; i < desktop_recent_games_.size() && i < 3; ++i) {
        const auto& game = desktop_recent_games_[i];
        const auto* art = art_.find(keys[i]);
        if (art && art->cover) renderer_.setDesktopRecent(i, art->cover);
        else requestArt(game, CoverMessage::Kind::cover);
        if (art && art->landscape) renderer_.setDesktopRecent(3 + i, art->landscape);
        else requestArt(game, CoverMessage::Kind::landscape);
    }
}

void Launcher::desktopRecentArtArrived(const std::string& key, CoverMessage::Kind kind, const ImageBytes& image) {
    if (!desktop_mode_ || !image || (kind != CoverMessage::Kind::cover && kind != CoverMessage::Kind::landscape)) return;
    for (size_t i = 0; i < desktop_recent_keys_.size() && i < 3; ++i)
        if (desktop_recent_keys_[i] == key) {
            renderer_.setDesktopRecent(kind == CoverMessage::Kind::cover ? i : 3 + i, image);
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
}

// The list's columns, measured once per rebuild (thousands of rows; a frame draws a few dozen).
void Launcher::desktopListLabels() {
    desktop_list_played_.clear();
    desktop_list_playtime_.clear();
    if (settings_.desktop_view != 3) return;
    desktop_list_played_.reserve(cards_.size());
    desktop_list_playtime_.reserve(cards_.size());
    for (const auto& card : cards_) {
        const auto& game = card.game;
        desktop_list_played_.push_back(game.last_played ? desktopPlayedWhen(game.last_played) : std::wstring(L"—"));
        const auto minutes = gamePlaytime(game);
        desktop_list_playtime_.push_back(minutes && *minutes ? formatPlaytime(*minutes) : std::wstring(L"—"));
    }
}

// ---- Ustawienia and Profil in the library's place ---------------------------------------------------

// Every desktop page (Ustawienia, Pobierania, Sklep, a game's Szczegóły, Zmień grafikę, Glance) sits there.
bool Launcher::desktopPageDocked() const { return desktop_mode_ && desktopSheet() && !launchAnimationActive(); }
float Launcher::desktopSheetDock() const { return desktopPageDocked() ? desktopLayout().sidebarRight() : 0.0f; }
bool Launcher::profileDocked() const { return desktop_mode_ && screen_ == ScreenMode::shelves && !launchAnimationActive(); }

desktop_ui::Rect Launcher::desktopDock() const {
    const auto L = desktopLayout();
    return {L.sidebarRight(), title_h, L.width, L.height};
}

// Out of the page as Back would leave it (sign-ins cancelled, edits closed, saved), page by page.
void Launcher::desktopLeavePage() {
    for (int step = 0; step < 16 && desktopSheet(); ++step) back();
}

// A click on the sidebar or the top bar's tools over Ustawienia or Profil: a heading folds, the
// toggle and a cog act in place; anything else leaves the page for the library and acts there.
bool Launcher::desktopChromeClick(float mouse_x, float mouse_y, bool double_click) {
    const bool settings = desktopPageDocked() && topModal() == Modal::none && !friends_open_ && !menu_open_ && !color_editor_.open;
    const bool profile = profileDocked() && profile_.open && topModal() == Modal::profile;
    if (!settings && !profile) return false;
    const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
    const auto L = desktopLayout();
    const auto hit = desktopHit(x, y);
    if (y < title_h) {
        if (profile && (hit.target == Target::minimize || hit.target == Target::maximize || hit.target == Target::close)) {
            if (!double_click) desktopActivate(hit);
            return true;
        }
        if (hit.target != Target::tool && hit.target != Target::search && hit.target != Target::search_clear && hit.target != Target::sidebar_toggle) return false;
    } else if (x >= L.sidebarRight()) return false;
    if (hit.target == Target::none) return true; // The sidebar's empty space.
    const auto rows = desktopRows();
    const bool row = hit.target == Target::sidebar_row && hit.index >= 0 && hit.index < static_cast<int>(rows.size());
    if ((row && rows[static_cast<size_t>(hit.index)].heading) || hit.target == Target::sidebar_toggle || hit.target == Target::sidebar_settings) {
        if (!double_click) desktopActivate(hit);
        return true;
    }
    if (row && rows[static_cast<size_t>(hit.index)].entry >= 0 && rows[static_cast<size_t>(hit.index)].entry == desktopActiveEntry()) return true; // Already there.
    if (settings) desktopLeavePage();
    else closeProfile();
    if (!desktopActive()) return true;
    desktop_hover_ = {};
    desktopClick(mouse_x, mouse_y, double_click);
    return true;
}

// The pointer over them: their hover, as in the library.
bool Launcher::desktopChromeMove(float mouse_x, float mouse_y) {
    const bool settings = desktopPageDocked() && topModal() == Modal::none && !friends_open_;
    const bool profile = profileDocked() && profile_.open && topModal() == Modal::profile;
    if (!settings && !profile) return false;
    const float scale = desktopScale(), x = mouse_x / scale, y = mouse_y / scale;
    const bool chrome = y < title_h || x < desktopLayout().sidebarRight();
    const auto hit = chrome ? desktopHit(x, y) : desktop_ui::Hit{};
    if (!(hit == desktop_hover_)) {
        desktop_hover_ = hit;
        desktop_hover_at_ = GetTickCount64();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return chrome && hit.target != Target::caption && hit.target != Target::none;
}

// Profil's canvas: docked, the library's place at desktop proportions; else the screen's.
Viewport Launcher::profileViewport() const {
    if (!profileDocked()) return renderer_.viewport();
    const auto view = renderer_.desktopViewport();
    const auto dock = desktopDock();
    Viewport docked;
    docked.scale = view.scale * profile_compact;
    docked.width = (dock.right - dock.left) / profile_compact;
    docked.height = (dock.bottom - dock.top) / profile_compact;
    return docked;
}

std::pair<float, float> Launcher::profilePoint(float mouse_x, float mouse_y) const {
    const auto view = profileViewport();
    if (!profileDocked()) return {mouse_x / view.scale, mouse_y / view.scale};
    const float scale = desktopScale();
    const auto dock = desktopDock();
    return {(mouse_x - dock.left * scale) / view.scale, (mouse_y - dock.top * scale) / view.scale};
}
