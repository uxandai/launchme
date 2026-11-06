// Półka's open case (src/ui/game_case_view.hpp): Enter on the focused case lifts it off the shelf,
// opens it and slides its medium out; Graj / Zainstaluj and Szczegóły wait on the right. A modal
// layer (Modal::game_case) over the library; Back closes the case, playing the timeline backwards.
#include "launcher.hpp"
#include "../ui/coverflow_layout.hpp"

#include <algorithm>

game_case::Kind Launcher::gameCaseKind() const {
    if (cards_.empty() || selected_ >= cards_.size()) return {};
    return game_case::kindOf(activePlatformName(cards_[selected_].game));
}

bool Launcher::discArt() const {
    return libraryCoverflow() || (gameCaseShown() && gameCaseKind().media == game_case::Media::disc);
}

void Launcher::openGameCase() {
    if (!libraryBookshelf() || cards_.empty() || selected_ >= cards_.size()) return;
    const auto& game = cards_[selected_].game;
    if (!isGame(game) || game.kind == GameKind::catalog) return;
    auto& state = game_case_;
    const auto key = gameKey(game);
    if (state.time <= 0 || state.key != key) {
        // Where the case stands on the shelf now: the flight starts there.
        const auto view = renderer_.viewport();
        const float flow = library_flow_.valid ? library_flow_.value.value : static_cast<float>(selected_);
        const auto from = shelf_layout::Layout{view.width, view.height}.bounds(static_cast<float>(selected_) - flow);
        // The shelf draws each package at its own width (a Switch case narrower, a big box wider) around the slot's middle.
        // Its own size too, standing on the shelf's floor (a DS case lower than a DVD case).
        const auto kind = gameCaseKind();
        const float middle = (from.left + from.right) * 0.5f, tall = (from.bottom - from.top) * game_case::height(kind);
        const float half = tall * game_case::aspect(kind) * 0.5f;
        state.from_left = middle - half; state.from_top = from.bottom - tall; state.from_right = middle + half; state.from_bottom = from.bottom;
        state.time = settings_.screen_transitions ? 0.0f : game_case::end_time;
    }
    state.open = true; state.key = key; state.focus = 0; state.hover = -1;
    dismissMenu(); cancelEnterHold();
    // Its logo for the title; the disc's own scan for the disc.
    requestArt(game, CoverMessage::Kind::logo);
    if (gameCaseKind().media == game_case::Media::disc) {
        if (const auto* art = art_.find(key); art && art->disc) renderer_.setDisc(selected_, art->disc);
        else requestArt(game, CoverMessage::Kind::disc);
    }
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeGameCase(bool at_once) {
    auto& state = game_case_;
    if (!state.open && state.time <= 0) return;
    state.open = false; state.hover = -1;
    if (at_once || !settings_.screen_transitions) state.time = 0;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::tickGameCase(float dt) {
    auto& state = game_case_;
    if (!state.open && state.time <= 0) return false;
    // The case belongs to the focused game on Półka: another screen, view or game ends it at once.
    const bool same = screen_ == ScreenMode::library && libraryBookshelf() && selected_ < cards_.size() && gameKey(cards_[selected_].game) == state.key;
    if (!same) { state.open = false; state.time = 0; return true; }
    const float before = state.time;
    if (state.open) state.time = std::min(game_case::end_time, state.time + dt);
    else state.time = std::max(0.0f, state.time - dt * game_case::close_speed);
    // Open at rest: the disc keeps turning, the diskette's shutter catches the light.
    return state.time != before || (state.open && state.time >= game_case::end_time && gameCaseKind().media != game_case::Media::cartridge);
}

void Launcher::gameCaseAccept() {
    if (!game_case_.open || cards_.empty() || selected_ >= cards_.size()) return;
    if (game_case_.focus == 1) {
        // Szczegóły: the normal details; Back from them returns to the shelf.
        const auto game = cards_[selected_].game;
        closeGameCase(true);
        openDetails(game);
        return;
    }
    if (!actionEnabled(cards_[selected_].game)) { navigationBoundary(); return; }
    launch(); // Graj / Zainstaluj / Wznów: the usual handling; the case stays open behind it.
}

void Launcher::gameCaseKey(WPARAM key, bool repeated) {
    auto& state = game_case_;
    switch (key) {
    case VK_LEFT: case VK_UP:
        if (state.focus > 0) { state.focus = 0; audio_.navigate(); } else if (!repeated) navigationBoundary();
        break;
    case VK_RIGHT: case VK_DOWN: case VK_TAB:
        if (state.focus < game_case::button_count - 1) { state.focus = game_case::button_count - 1; audio_.navigate(); } else if (!repeated) navigationBoundary();
        break;
    case VK_RETURN: case VK_SPACE: if (!repeated) gameCaseAccept(); break;
    case VK_ESCAPE: case VK_BACK: if (!repeated) { closeGameCase(); audio_.navigate(); } break;
    default: return;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::gameCaseClick(float x, float y) {
    const auto view = renderer_.viewport();
    const int hit = game_case::Layout{view.width, view.height, gameCaseKind()}.hit(x, y);
    if (hit < 0) return;
    game_case_.focus = hit;
    gameCaseAccept();
}

void Launcher::gameCasePointer(float x, float y) {
    const auto view = renderer_.viewport();
    const int hit = game_case::Layout{view.width, view.height, gameCaseKind()}.hit(x, y);
    if (hit == game_case_.hover) return;
    game_case_.hover = hit;
    if (hit >= 0) game_case_.focus = hit;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::gameCasePointerOver(float x, float y) const {
    const auto view = renderer_.viewport();
    return game_case::Layout{view.width, view.height, gameCaseKind()}.hit(x, y) >= 0;
}
