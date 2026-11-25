#include "launcher.hpp"
#include <algorithm>

void Launcher::indexVersions() {
    versions_ = settings_.merge_versions ? versions::Index(public_library_, true) : versions::Index{};
}
int Launcher::versionsDetailAction() const {
    return screen_ == ScreenMode::details && !cards_.empty() && versions_.find(cards_[0].game) ? collectionsDetailAction() + 1 : -2;
}
void Launcher::openVersions(const Game& game, bool launch) {
    const auto* group = versions_.find(game);
    if (!group || group->size() < 2) return;
    auto& ui = library_dialog_;
    ui = {}; ui.open = true; ui.page = library_manager_ui::Page::versions;
    ui.account = account_; ui.game = game; ui.title = tr(L"Wybierz wersję") + (L" · " + game.name); ui.launch_version = launch;
    // Re-resolve by identity so progress-only updates cannot leave stale launch actions in the picker.
    for (const auto& copy : *group)
        if (const auto it = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& g) { return gameKey(g) == gameKey(copy); }); it != public_library_.end()) ui.versions.push_back(*it);
    if (ui.versions.size() < 2) { ui = {}; return; }
    ui.collection = versions_.by_game.at(gameKey(game));
    if (const auto preferred = library_state_.preferred_versions.find(ui.collection); preferred != library_state_.preferred_versions.end()) {
        ui.remember = true;
        for (size_t i = 0; i < ui.versions.size(); ++i) if (gameKey(ui.versions[i]) == preferred->second) ui.focus = static_cast<int>(i);
    } else if (!launch) {
        // Zmień wersję: the choice becomes the tile's (Zapamiętaj on); the focus starts on the copy shown.
        ui.remember = true;
        for (size_t i = 0; i < ui.versions.size(); ++i) if (gameKey(ui.versions[i]) == gameKey(game)) ui.focus = static_cast<int>(i);
    }
    for (const auto& copy : ui.versions) {
        Game tile; tile.source = copy.source;
        if (versions::emulatedCopy(copy)) { tile.kind = GameKind::platform; tile.platform = copy.platform; } // Its console's logo.
        else tile.kind = GameKind::pc_platform;
        requestArt(tile, CoverMessage::Kind::badge);
    }
    dismissMenu(); closeQuick(); closeFriends(); closeCategoryMenu();
    InvalidateRect(hwnd_, nullptr, FALSE);
}
void Launcher::chooseVersion() {
    auto& ui = library_dialog_;
    if (ui.focus == -1) { collectionDialogBack(); return; }
    if (ui.focus == static_cast<int>(ui.versions.size())) {
        ui.remember = !ui.remember;
        if (!ui.remember) { library_state_.preferred_versions.erase(ui.collection); library_state_dirty_ = true; saveLibraryPreferences(); }
        InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (ui.focus < 0 || ui.focus >= static_cast<int>(ui.versions.size())) return;
    auto game = ui.versions[ui.focus];
    const auto current = std::find_if(public_library_.begin(), public_library_.end(), [&](const Game& item) { return gameKey(item) == gameKey(game); });
    if (current == public_library_.end()) { collectionDialogBack(); return; }
    game = *current;
    const bool launch = ui.launch_version;
    const auto group = ui.collection;
    bool switched = false;
    if (ui.remember && game.visibility == GameVisibility::public_game) {
        const auto old = library_state_.preferred_versions.find(group);
        switched = old == library_state_.preferred_versions.end() || old->second != gameKey(game);
        library_state_.preferred_versions[group] = gameKey(game); library_state_dirty_ = true; saveLibraryPreferences();
    }
    ui = {}; mouse_transition_until_ = GetTickCount64() + GetDoubleClickTime();
    // The tile (shelves, grids, search, the desktop's cards) shows the chosen copy from now on.
    if (switched && screen_ != ScreenMode::details) { buildShelves(); rebuildCards(true); }
    if (launch) launchGame(game);
    else if (screen_ == ScreenMode::details) {
        if (switched) buildShelves(); // Back from Szczegóły finds the tile switched.
        suspendDetails(); game.version_group = group;
        if (const auto pref = library_state_.preferred_versions.find(group); pref != library_state_.preferred_versions.end()) game.version_chosen = pref->second == gameKey(game);
        detail_games_ = {game}; detail_version_selected_ = true;
        details_ = {}; details_requested_ = false; detail_action_ = 0;
        rebuildCards(false); requestDetails();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}
