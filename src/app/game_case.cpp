// The case in the hand (src/ui/game_case_view.hpp), on Półka and in Cover Flow: Enter on the focused
// game lifts its case off the shelf and holds it up, closed, in 3D. The right stick, LB / RB (Page Up
// / Page Down) or a mouse drag turn it; let go, it settles on its front, a spine or its back.
// Graj / Zainstaluj, Instrukcja (the game's manual, src/app/game_manual.cpp) and Szczegóły wait on
// the right; R3, O or a click on the case shows its inside (the lid opens and the medium slides out;
// again shuts it). A modal layer (Modal::game_case) over the library; Back shuts an open case, then
// puts the case back.
#include "launcher.hpp"
#include "../ui/coverflow_layout.hpp"

#include <algorithm>
#include <cmath>

game_case::Kind Launcher::gameCaseKind() const {
    if (cards_.empty() || selected_ >= cards_.size()) return {};
    return game_case::kindOf(activePlatformName(cards_[selected_].game));
}

bool Launcher::discArt() const {
    return libraryCoverflow() || (gameCaseShown() && gameCaseKind().media == game_case::Media::disc);
}

bool Launcher::gameCaseLibrary() const { return libraryBookshelf() || libraryCoverflow(); }

game_case::Layout Launcher::gameCaseLayout() const {
    const auto view = renderer_.viewport();
    return {view.width, view.height, gameCaseKind(), game_case::buttonCount(game_case_.manual)};
}

void Launcher::openGameCase() {
    if (!gameCaseLibrary() || cards_.empty() || selected_ >= cards_.size()) return;
    const auto& game = cards_[selected_].game;
    if (!isGame(game) || game.kind == GameKind::catalog) return;
    auto& state = game_case_;
    const auto key = gameKey(game);
    if (state.held <= 0 || state.key != key) {
        // Where the case stands now: the flight starts there. The shelf draws each package at its own
        // width (a Switch case narrower, a big box wider) around the slot's middle and its own height,
        // standing on the shelf's floor; Cover Flow's poster holds the case's front at its height.
        const auto view = renderer_.viewport();
        const auto kind = gameCaseKind();
        settings_ui::Rect from;
        float tall = 0;
        if (libraryBookshelf()) {
            const float flow = library_flow_.valid ? library_flow_.value.value : static_cast<float>(selected_);
            from = shelf_layout::Layout{view.width, view.height}.bounds(static_cast<float>(selected_) - flow);
            tall = (from.bottom - from.top) * game_case::height(kind);
        } else {
            from = coverflow_layout::poster(cards_[selected_]);
            tall = from.bottom - from.top;
        }
        const float middle = (from.left + from.right) * 0.5f, half = tall * game_case::aspect(kind) * 0.5f;
        state.from_left = middle - half; state.from_top = from.bottom - tall; state.from_right = middle + half; state.from_bottom = from.bottom;
        state.held = settings_.screen_transitions ? 0.0f : game_case::held_end;
        state.time = 0; state.opened = false;
        state.yaw = state.pitch = state.yaw_goal = 0;
    }
    state.open = true; state.key = key; state.focus = 0; state.hover = -1; state.dragging = false;
    state.stick_x = state.stick_y = 0;
    state.manual = gameManualAvailable(game);
    dismissMenu(); cancelEnterHold();
    // Its logo for the title; the disc's own scan for the disc; the back of its box.
    requestArt(game, CoverMessage::Kind::logo);
    if (gameCaseKind().media == game_case::Media::disc) {
        if (const auto* art = art_.find(key); art && art->disc) renderer_.setDisc(selected_, art->disc);
        else requestArt(game, CoverMessage::Kind::disc);
    }
    requestCaseExtras(game);
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeGameCase(bool at_once) {
    auto& state = game_case_;
    if (!state.shown()) return;
    state.open = false; state.opened = false; state.hover = -1; state.dragging = false;
    state.stick_x = state.stick_y = 0;
    manual_.open = false;
    if (at_once) manual_.alpha = 0;
    if (GetCapture() == hwnd_) ReleaseCapture();
    if (at_once || !settings_.screen_transitions) state.time = state.held = 0;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Back: an open case shuts (back to the held one), a held case goes back to the shelf.
void Launcher::gameCaseBack() {
    auto& state = game_case_;
    if (manual_.open) { closeGameManual(); return; }
    if (state.opened) {
        state.opened = false;
        if (!settings_.screen_transitions) state.time = 0;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    closeGameCase();
}

bool Launcher::tickGameCase(float dt) {
    auto& state = game_case_;
    if (!state.shown()) return false;
    // The case belongs to the focused game on Półka or Cover Flow: another screen, view or game ends it at once.
    const bool same = screen_ == ScreenMode::library && gameCaseLibrary() && selected_ < cards_.size() && gameKey(cards_[selected_].game) == state.key;
    if (!same) {
        state.open = state.opened = state.dragging = false; state.time = state.held = 0;
        manual_.open = false; manual_.alpha = 0;
        if (GetCapture() == hwnd_) ReleaseCapture();
        return true;
    }
    const bool manual_motion = tickGameManual(dt);
    const float held = state.held, time = state.time, yaw = state.yaw, pitch = state.pitch;
    if (state.open) state.held = std::min(game_case::held_end, state.held + dt);
    else if (state.time <= 0) state.held = std::max(0.0f, state.held - dt * game_case::close_speed);
    // Turning: free while the case is held closed; otherwise it comes round to its front (the lid
    // opens, or it goes back to the shelf) by the shorter way.
    const bool free = state.open && !state.opened && state.time <= 0;
    const bool stick = free && (state.stick_x != 0 || state.stick_y != 0);
    if (stick) {
        state.yaw += state.stick_x * game_case::turn_speed * dt;
        state.pitch = std::clamp(state.pitch + state.stick_y * game_case::turn_speed * 0.6f * dt, -game_case::tip_limit, game_case::tip_limit);
        state.yaw_goal = game_case::settledYaw(state.yaw);
    } else if (!(free && state.dragging)) {
        const float goal = free ? state.yaw_goal : std::round(state.yaw / (2 * game_case::pi)) * 2 * game_case::pi;
        state.yaw = game_case::settle(state.yaw, goal, dt, free ? 9.0f : 12.0f);
        state.pitch = game_case::settle(state.pitch, 0, dt, free ? 9.0f : 12.0f);
        if (std::abs(state.yaw - goal) < 0.0006f) state.yaw = goal;
        if (std::abs(state.pitch) < 0.0006f) state.pitch = 0;
        if (!free) state.yaw_goal = goal;
    }
    // Whole turns are the same pose: kept small.
    if (std::abs(state.yaw) > 2 * game_case::pi && std::abs(state.yaw - state.yaw_goal) < game_case::pi) {
        const float turns = std::trunc(state.yaw / (2 * game_case::pi)) * 2 * game_case::pi;
        state.yaw -= turns; state.yaw_goal -= turns;
    }
    if (!free && state.yaw == state.yaw_goal && std::abs(state.yaw) > 0.1f) state.yaw = state.yaw_goal = 0;
    // The lid opens once the case faces front.
    if (state.open && state.opened) {
        if (std::abs(state.yaw) < 0.0006f && std::abs(state.pitch) < 0.0006f) {
            state.yaw = state.pitch = state.yaw_goal = 0;
            state.time = std::min(game_case::end_time, state.time + dt);
        }
    } else {
        state.time = std::max(0.0f, state.time - dt * game_case::close_speed);
    }
    // Open at rest: the disc keeps turning, the diskette's shutter catches the light.
    const bool resting_media = state.opened && state.time >= game_case::end_time && gameCaseKind().media != game_case::Media::cartridge;
    return state.held != held || state.time != time || state.yaw != yaw || state.pitch != pitch || stick || resting_media || manual_motion;
}

void Launcher::gameCaseAccept() {
    auto& state = game_case_;
    if (!state.open || cards_.empty() || selected_ >= cards_.size()) return;
    const auto game = cards_[selected_].game;
    switch (game_case::buttonAt(state.focus, state.manual)) {
    case game_case::Button::details:
        // Szczegóły: the normal details; Back from them returns to the library.
        closeGameCase(true);
        openDetails(game);
        return;
    case game_case::Button::manual:
        openGameManual(game);
        return;
    case game_case::Button::action:
        break;
    }
    if (!actionEnabled(game)) { navigationBoundary(); return; }
    launch(); // Graj / Zainstaluj / Wznów: the usual handling; the case stays in the hand behind it.
}

void Launcher::gameCaseShowInside() {
    auto& state = game_case_;
    if (!state.open || manual_.open) return;
    state.opened = !state.opened;
    if (!settings_.screen_transitions) { state.time = state.opened ? game_case::end_time : 0; state.yaw = state.pitch = state.yaw_goal = 0; }
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// A quarter turn at a press (LB / RB, Page Up / Page Down), the held case only.
void Launcher::gameCaseTurn(int quarters) {
    auto& state = game_case_;
    if (manual_.open) { manualTurn(quarters); return; }
    if (!state.open || state.opened || state.time > 0) return;
    state.yaw_goal = game_case::settledYaw(state.yaw_goal) + quarters * game_case::half_pi;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::gameCaseStick(int16_t x, int16_t y) {
    auto& state = game_case_;
    // A dead zone (XInput's right stick's), the rest rescaled to the full range.
    const auto axis = [](int16_t raw) {
        constexpr float dead = 8689.0f / 32767.0f;
        const float value = std::clamp(raw / 32767.0f, -1.0f, 1.0f);
        if (std::abs(value) < dead) return 0.0f;
        return std::copysign((std::abs(value) - dead) / (1 - dead), value);
    };
    const float sx = axis(x), sy = axis(y);
    if (manual_.open) {
        // Reading: the stick moves round a zoomed spread.
        if (sx == manual_.stick_x && sy == manual_.stick_y) return;
        manual_.stick_x = sx; manual_.stick_y = sy;
        state.stick_x = state.stick_y = 0;
        if (sx != 0 || sy != 0) { noteUserInput(); InvalidateRect(hwnd_, nullptr, FALSE); }
        return;
    }
    if (sx == state.stick_x && sy == state.stick_y) return;
    state.stick_x = sx; state.stick_y = sy;
    if (sx != 0 || sy != 0) { noteUserInput(); InvalidateRect(hwnd_, nullptr, FALSE); }
}

void Launcher::gameCaseKey(WPARAM key, bool repeated) {
    auto& state = game_case_;
    if (manual_.open) {
        if (key == VK_ESCAPE || key == VK_BACK) { if (!repeated) gameCaseBack(); return; }
        manualKey(key, repeated);
        return;
    }
    const int last = game_case::buttonCount(state.manual) - 1;
    switch (key) {
    case VK_LEFT: case VK_UP:
        if (state.focus > 0) { --state.focus; audio_.navigate(); } else if (!repeated) navigationBoundary();
        break;
    case VK_RIGHT: case VK_DOWN: case VK_TAB:
        if (state.focus < last) { ++state.focus; audio_.navigate(); } else if (!repeated) navigationBoundary();
        break;
    case VK_PRIOR: gameCaseTurn(-1); break;
    case VK_NEXT: gameCaseTurn(1); break;
    case VK_RETURN: case VK_SPACE: if (!repeated) gameCaseAccept(); break;
    case 'O': if (!repeated) gameCaseShowInside(); break;
    case VK_ESCAPE: case VK_BACK: if (!repeated) { gameCaseBack(); audio_.navigate(); } break;
    default: return;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::gameCaseClick(float x, float y) {
    auto& state = game_case_;
    if (manual_.open) { manualClick(x, y); return; }
    const auto layout = gameCaseLayout();
    const int hit = layout.hit(x, y);
    if (hit >= 0) {
        state.focus = hit;
        gameCaseAccept();
        return;
    }
    // On the held case: a drag turns it, a click opens it. On the open one: a click shuts it.
    if (state.opened && layout.overHeld(x, y, true)) { gameCaseShowInside(); return; }
    if (state.open && !state.opened && state.time <= 0 && layout.overHeld(x, y)) {
        state.dragging = true; state.drag_moved = false;
        state.drag_x = x; state.drag_y = y; state.drag_yaw = state.yaw; state.drag_pitch = state.pitch;
        SetCapture(hwnd_);
    }
}

void Launcher::gameCasePointer(float x, float y) {
    auto& state = game_case_;
    if (state.dragging) {
        // A full drag across the case turns it about half way round; up tips its top away.
        const float h = gameCaseLayout().heldHeight();
        if (std::abs(x - state.drag_x) + std::abs(y - state.drag_y) > 6) state.drag_moved = true;
        state.yaw = state.drag_yaw + (x - state.drag_x) / h * 2.6f;
        state.pitch = std::clamp(state.drag_pitch - (y - state.drag_y) / h * 1.6f, -game_case::tip_limit, game_case::tip_limit);
        state.yaw_goal = game_case::settledYaw(state.yaw);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    const int hit = gameCaseLayout().hit(x, y);
    if (hit == state.hover) return;
    state.hover = hit;
    if (hit >= 0) state.focus = hit;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::gameCaseRelease() {
    auto& state = game_case_;
    if (!state.dragging) return;
    state.dragging = false;
    state.yaw_goal = game_case::settledYaw(state.yaw);
    if (GetCapture() == hwnd_) ReleaseCapture();
    if (!state.drag_moved) gameCaseShowInside();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// The wheel turns the manual's pages, else the held case a quarter turn.
void Launcher::gameCaseWheel(int delta) {
    if (delta == 0) return;
    if (manual_.open) manualTurn(delta > 0 ? -1 : 1);
    else gameCaseTurn(delta > 0 ? -1 : 1);
}

bool Launcher::gameCasePointerOver(float x, float y) const {
    if (manual_.open) return true;
    const auto layout = gameCaseLayout();
    return layout.hit(x, y) >= 0 || (game_case_.open && !game_case_.opened && game_case_.time <= 0 && layout.overHeld(x, y)) ||
           (game_case_.opened && layout.overHeld(x, y, true));
}
