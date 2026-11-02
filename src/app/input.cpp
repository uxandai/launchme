// Gamepad and keyboard input that is not tied to one screen: the pad dispatch,
// short/hold gestures of Guide (Home) and Start (TAB), and the power menu.
#include "launcher.hpp"
#include "../ui/exit_prompt.hpp"

#include <xinput.h>

#include <algorithm>
#include <cmath>

namespace {
uint64_t nowMs() { return GetTickCount64(); }
// The D-pad or the left stick as an arrow key, vertical first.
int padArrow(const PadReading& state, WORD buttons) {
    return (buttons & XINPUT_GAMEPAD_DPAD_UP) || state.y > 16000 ? VK_UP :
        (buttons & XINPUT_GAMEPAD_DPAD_DOWN) || state.y < -16000 ? VK_DOWN :
        (buttons & XINPUT_GAMEPAD_DPAD_LEFT) || state.x < -16000 ? VK_LEFT :
        (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) || state.x > 16000 ? VK_RIGHT : 0;
}
// Milliseconds between repeats of a held arrow key: Windows' keyboard repeat rate (0..31 = ~2.5..30
// a second, 31 by default); ~30 a second elsewhere.
ULONGLONG keyRepeatInterval() {
#ifdef _WIN32
    DWORD speed = 31;
    if (!SystemParametersInfoW(SPI_GETKEYBOARDSPEED, 0, &speed, 0)) speed = 31;
    const double rate = 2.5 + std::min<DWORD>(speed, 31) * (27.5 / 31.0);
    return static_cast<ULONGLONG>(std::lround(1000.0 / rate));
#else
    return 33;
#endif
}
}

// --- Modal layers -----------------------------------------------------------

Launcher::Modal Launcher::topModal() const {
    if (exit_prompt_) return Modal::exit_prompt;
    if (power_menu_) return Modal::power_menu;
    if (tour_.open) return Modal::tour;
    return topDialog();
}

Launcher::Modal Launcher::topDialog() const {
    if (whats_new_.open) return Modal::whats_new; // Shown alone, right after an update's restart.
    if (aurelia_ui_.open) return fileBrowserOpen() ? Modal::file_browser : Modal::aurelia; // Its library picker opens over it.
    if (companion_.picker.open) return Modal::companion_picker;
    if (fileBrowserOpen()) return Modal::file_browser; // Opened from the panels below it.
    if (libraryDialogOpen()) return Modal::library_dialog;
    if (identifyOpen()) return Modal::identify; // Also over Dodaj / Edytuj grę ("Zidentyfikuj grę" under its poster).
    if (customEditorOpen()) return Modal::custom_editor;
    if (metadataOpen()) return Modal::metadata; // From the game's menu.
    if (bulkCoversOpen()) return Modal::bulk_covers;
    if (gogInstallOpen()) return Modal::gog_install;
    if (tarot_.open) return Modal::tarot; // Opened only from the bare home.
    if (profile_.open) return Modal::profile; // Over any screen, from the drawer: the lowest dialog.
    return Modal::none;
}

bool Launcher::homeKeyIsGuide() const {
    // The desktop window keeps Home for its lists and fields; the pad's Guide still works there.
    if (desktop_mode_) return false;
    // The installer and the power menu leave Home to the Guide gesture (short: menu, hold: power).
    const auto modal = topModal();
    return (modal == Modal::none || modal == Modal::power_menu || modal == Modal::gog_install) &&
           !searching() && !editingSecret() && !editingArtworkSearch() && !customEditorOpen() && !metadataOpen() && !fileBrowserOpen();
}

bool Launcher::modalKey(WPARAM key, bool repeated) {
    if (key == VK_HOME && homeKeyIsGuide()) return false;
    switch (topModal()) {
    case Modal::none: return false;
    case Modal::exit_prompt: exitPromptKey(key, repeated); break;
    case Modal::power_menu: powerMenuKey(key, repeated); break;
    case Modal::tour: tourKey(key, repeated); break;
    case Modal::file_browser: fileBrowserKey(key, repeated); break;
    case Modal::library_dialog: libraryDialogKey(key, repeated); break;
    case Modal::custom_editor: customEditorKey(key, repeated); break;
    case Modal::metadata: metadataKey(key, repeated); break;
    case Modal::bulk_covers: bulkCoversKey(key, repeated); break;
    case Modal::identify: identifyKey(key, repeated); break;
    case Modal::gog_install: gogInstallKey(key, repeated); break;
    case Modal::companion_picker: companionPickerKey(key, repeated); break;
    case Modal::tarot: tarotKey(key, repeated); break;
    case Modal::profile: profileKey(key, repeated); break;
    case Modal::aurelia: aureliaKey(key, repeated); break;
    case Modal::whats_new: if (!repeated || key == VK_UP || key == VK_DOWN) whatsNewKey(key); break;
    }
    return true;
}

// --- Gestures ---------------------------------------------------------------

void Launcher::toggleFriendsDrawer() {
    if (launchAnimationActive()) return;
    if (identifyOpen()) return;
    if (exit_prompt_ || power_menu_ || gogInstallOpen() || fileBrowserOpen() || pending_launch_) return;
    // These screens use TAB/Start for their own focus switching.
    if (cropOpen() || devicesOpen() || artworkOpen() || settingsOpen()) return;
    if (friends_open_) { closeFriends(); return; }
    closeQuick();
    closeCategoryMenu();
    dismissMenu();
    friends_open_ = true;
    friends_list_ = false; // Opens on the dashboard tiles, Steam friends tile focused.
    plugin_drawer_active_ = false; renderer_.resetPluginThumbnails(0);
    dashboard_tile_ = 0;
    dashboard_scroll_ = 0;
    if (const auto layout = dashboardLayout(); !layout.items.empty()) dashboard_tile_ = layout.items.front().source;
    clock_check_at_ = 0;
    audio_.drawer();
    loadFriends();
    startDashboardPoll();
}

void Launcher::openAudioShortcut() {
    if (launchAnimationActive()) return;
    if (identifyOpen()) return;
    if (exit_prompt_ || power_menu_ || gogInstallOpen() || customEditorOpen() || fileBrowserOpen() || pending_launch_) return;
    if (devicesOpen() && device_state_.panel == devices::Panel::audio) { back(); return; }
    openDevices(devices::Panel::audio);
}

void Launcher::openGuideMenu() {
    if (launchAnimationActive()) return;
    if (identifyOpen()) return;
    // Modal panels keep their input (and unsaved text) until closed.
    if (exit_prompt_ || pending_launch_ || gogInstallOpen() || customEditorOpen() || fileBrowserOpen()) return;
    if (power_menu_) { closePowerMenu(); return; }
    if (flixIconsFocused()) { flixIconsLeave(); return; }
    if (flixRailFocused()) { flixRailFocus(-1); return; }
    if (quick_open_) { closeQuick(); return; }
    // The desktop window: the pad's Guide button takes the launcher to Tryb TV (like Big Picture).
    if (desktop_mode_) { setDesktopMode(false); return; }
    // Like a console Home button: peel every open screen/overlay through the same
    // path as Back (so Settings save, panels close their services), then show the bar.
    for (int layer = 0; layer < 8; ++layer) {
        const bool home = screen_ == ScreenMode::shelves && !friends_open_ && !category_menu_open_ &&
                          !menu_open_ && !(xmbActive() && xmb_row_ >= 0) && !(flixActive() && flix_row_ > 0) &&
                          !(metroActive() && metro_page_ > 0) &&
                          !(libraryControls() && (library_controls_.open() || library_controls_.focus >= 0 || library_controls_.search));
        if (home) break;
        back();
    }
    if (screen_ != ScreenMode::shelves) return; // A screen refused to close (e.g. applying artwork).
    if (!xmbActive() && category_ != 0) showCategory(0);
    openQuick();
}

void Launcher::tabDown(bool repeated) {
    if (cropOpen()) { if (!repeated) cropTab(GetKeyState(VK_SHIFT) & 0x8000 ? -1 : 1); return; }
    if (devicesOpen()) { if (!repeated) deviceColumn(GetKeyState(VK_SHIFT) & 0x8000 ? -1 : device_state_.audio_column ? -1 : 1); return; }
    if (artworkOpen()) {
        if (!repeated && !artwork_state_.applying) {
            if (!artwork_state_.search_focus && !artwork_state_.gallery) { artwork_state_.search_focus = true; artwork_state_.query_selected = true; }
            else if (artwork_state_.search_focus) {
                if (artwork_state_.results_mode && !artwork_state_.games.empty()) { artwork_state_.search_focus = false; artwork_state_.gallery = true; }
                else if (!artwork_state_.results_mode) { artwork_state_.search_focus = false; artwork_state_.gallery = !artwork_state_.assets.empty(); }
                else leaveArtworkSearch(); // An unsearched edit: back to the gallery.
            }
            else artwork_state_.gallery = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    if (settingsOpen()) {
        if (!repeated && !settings_state_.editing && !color_editor_.open) {
            if (settings_state_.options) settings_state_.options = false;
            else settings_state_.enter(static_cast<int>(settings_rows_.size()));
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return;
    }
    if (!repeated) tab_key_.update(true, nowMs());
}

void Launcher::tabUp() {
    if (tab_key_.update(false, nowMs()) == HoldGesture::Event::press) toggleFriendsDrawer();
}

void Launcher::cancelTabHold() { tab_key_.swallow(false); home_key_.swallow(false); }

void Launcher::homeDown(bool repeated) {
    if (!repeated) home_key_.update(true, nowMs());
}

void Launcher::homeUp() {
    if (home_key_.update(false, nowMs()) == HoldGesture::Event::press) openGuideMenu();
}

// Holds fire while the key is still down, so the frame loop checks them.
void Launcher::pollHoldGestures() {
    const auto now = nowMs();
    dashboardPollHold(now);
    if (tab_key_.down() && tab_key_.update(true, now) == HoldGesture::Event::hold) { noteUserInput(); openAudioShortcut(); }
    if (home_key_.down() && home_key_.update(true, now) == HoldGesture::Event::hold) { noteUserInput(); openPowerMenu(); }
}

// --- Power menu -------------------------------------------------------------

void Launcher::openPowerMenu() {
    if (identifyOpen()) return;
    if (exit_prompt_ || pending_launch_) return;
    dismissMenu(); closeQuick(); closeCategoryMenu(); closeFriends(); cancelEnterHold();
    power_items_ = power_menu::items(power_actions_());
    if (gameOverlayOpen()) std::erase(power_items_, power_menu::Item::tv_mode); // Over a game: the window stays as it is.
    power_menu_ = 0;
    audio_.context();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closePowerMenu() {
    power_menu_.reset();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::powerMenuMove(int amount) {
    if (!power_menu_ || power_items_.empty()) return;
    const int next = std::clamp(*power_menu_ + amount, 0, static_cast<int>(power_items_.size()) - 1);
    if (next == *power_menu_) { navigationBoundary(); return; }
    power_menu_ = next;
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::powerMenuAccept() {
    if (!power_menu_ || *power_menu_ >= static_cast<int>(power_items_.size())) return;
    const auto item = power_items_[*power_menu_];
    closePowerMenu();
    if (item == power_menu::Item::cancel) return;
    // Same path as the window's close button: suspended games still ask first.
    if (item == power_menu::Item::exit) { PostMessageW(hwnd_, WM_CLOSE, 0, 0); return; }
    if (item == power_menu::Item::tv_mode) { setDesktopMode(!desktop_mode_); return; }
    // Windows may end the session without destroying the window: persist first.
    if (settings_dirty_) saveSettings();
    saveLibraryPreferences();
    if (!power_(static_cast<PowerAction>(item)))
        showNotification(NotificationKind::error, tr(L"Windows odrzucił polecenie zasilania"), power_menu::label(item));
}

void Launcher::powerMenuKey(WPARAM key, bool repeated) {
    if (key == VK_UP || key == VK_LEFT || key == 'W' || key == 'A') powerMenuMove(-1);
    else if (key == VK_DOWN || key == VK_RIGHT || key == 'S' || key == 'D') powerMenuMove(1);
    else if ((key == VK_RETURN || key == VK_SPACE) && !repeated) powerMenuAccept();
    else if (key == VK_ESCAPE || key == VK_BACK) closePowerMenu();
}

void Launcher::powerMenuClick(float x, float y) {
    const auto layout = powerMenuGeometry();
    const int choice = layout.hit(x, y);
    if (choice >= 0) { power_menu_ = choice; powerMenuAccept(); }
    else if (!layout.panel().contains(x, y)) closePowerMenu();
}

power_menu::Geometry Launcher::powerMenuGeometry() const {
    const auto view = renderer_.viewport();
    return {view.width, view.height, power_menu::gaps(power_items_)};
}

// --- Gamepad ----------------------------------------------------------------

void Launcher::controllerNavigate(int direction) {
    if (!direction) { pad_edge_direction_ = 0; return; }
    navigation_edge_ = false;
    const auto modal = topModal();
    if (modal == Modal::exit_prompt) exitPromptMove(direction / 2);
    else if (modal == Modal::power_menu) powerMenuMove(direction / 2);
    else if (std::abs(direction) == 1) navigate(direction);
    else {
        const int amount = direction / 2;
        if (friends_open_) friendsMove(amount);
        else if (category_menu_open_) categoryMenuMove(amount);
        else if (menu_open_) menuMove(amount);
        else changeCategory(amount);
    }
    if (navigation_edge_) {
        if (pad_edge_direction_ != direction) boundary_feedback_();
        pad_edge_direction_ = direction;
    } else pad_edge_direction_ = 0;
}

void Launcher::controller() {
    controller(controller_input_.read());
}

void Launcher::controller(const PadReading& state) {
    if (closing_) return;
    const bool held_direction = state.connected && ((state.buttons & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT |
        XINPUT_GAMEPAD_DPAD_RIGHT)) || std::abs(static_cast<int>(state.x)) > 16000 || std::abs(static_cast<int>(state.y)) > 16000);
    const int layer = static_cast<int>(topModal()) * 4096 + static_cast<int>(screen_) * 32 + (keyboard_.open ? 1 : 0) +
                      (menu_open_ ? 2 : 0) + (quick_open_ ? 4 : 0) + (friends_open_ ? 8 : 0) + (category_menu_open_ ? 16 : 0);
    // Only a direction already held before the layer opened is latched; one pressed now acts.
    if (layer != pad_layer_) {
        if (held_direction && pad_direction_held_ && pad_layer_ != -1) pad_direction_latched_ = true;
        pad_layer_ = layer;
    }
    pad_direction_held_ = held_direction;
    if (pad_direction_latched_ && !held_direction) pad_direction_latched_ = false;
    if (!pad_direction_latched_) { controllerInput(state); return; }
    PadReading still = state;
    still.buttons &= static_cast<WORD>(~(XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT));
    still.x = still.y = 0;
    controllerInput(still);
}

void Launcher::controllerInput(const PadReading& state) {
    if (closing_) return;
    const auto now = GetTickCount64();
    if (!state.connected) {
        controllerNavigate(0);
        controller_input_.stopRumble();
        if (pad_connected_ && now - pad_last_seen_ >= 450) {
            pad_connected_ = false;
            controller_direction_ = 0;
            controller_buttons_ = 0;
            pad_guide_.swallow(false); pad_start_.swallow(false);
            if (dashboard_press_source_ == 2) dashboardCancelPress();
            if (pad_enter_hold_) cancelEnterHold();
        }
        return;
    }
    pad_last_seen_ = now;
    const WORD buttons = state.buttons;
    // L2/LT is an edge of its own (no free bit in `buttons`); it opens and closes the companion.
    const bool left_trigger = state.left_trigger && !controller_left_trigger_;
    controller_left_trigger_ = state.left_trigger;
    const auto swallow = [&] {
        if (dashboard_press_source_ == 2) dashboardCancelPress();
        if (pad_enter_hold_) cancelEnterHold();
        controllerNavigate(0);
        controller_input_.stopRumble();
        // An already-held button on reconnection/refocus must never act on its release.
        controller_buttons_ = buttons;
        controller_direction_ = 0;
        pad_guide_.swallow(buttons & pad_guide);
        pad_start_.swallow(buttons & XINPUT_GAMEPAD_START);
    };
    if (!pad_connected_ || controller_id_ != state.id) {
        controller_id_ = state.id;
        controller_name_ = state.name;
        // Connection toasts come from the WGI/HID census, including inactive/second pads.
        pad_connected_ = true;
        swallow();
        return;
    }
    if (browser_ && browser_->owns(overlay_backend_.foreground())) {
        // The owned browser is a separate foreground HWND. Its input must never navigate
        // or launch a game on the disabled Glance window beneath it.
        const auto pressed=[&](WORD button) { return (buttons&button) && !(controller_buttons_&button); };
        pad_guide_.swallow(buttons&pad_guide); pad_start_.swallow(buttons&XINPUT_GAMEPAD_START);
        if (pressed(XINPUT_GAMEPAD_B)) browser_->command(BrowserWindow::Command::close);
        else if (pressed(XINPUT_GAMEPAD_A)) browser_->command(BrowserWindow::Command::accept);
        else if (pressed(XINPUT_GAMEPAD_LEFT_SHOULDER)) browser_->command(BrowserWindow::Command::back);
        else if (pressed(XINPUT_GAMEPAD_RIGHT_SHOULDER)) browser_->command(BrowserWindow::Command::forward);
        else if (pressed(XINPUT_GAMEPAD_Y)) browser_->command(BrowserWindow::Command::reload);
        if (pressed(XINPUT_GAMEPAD_DPAD_LEFT)) browser_->command(BrowserWindow::Command::previous_button);
        if (pressed(XINPUT_GAMEPAD_DPAD_RIGHT)) browser_->command(BrowserWindow::Command::next_button);
        const int direction=(buttons&XINPUT_GAMEPAD_DPAD_UP) || state.y>16000 ? -1 :
            (buttons&XINPUT_GAMEPAD_DPAD_DOWN) || state.y<-16000 ? 1 : 0;
        if (direction && (direction!=controller_direction_ || now>=controller_repeat_)) {
            browser_->scroll(direction*100); controller_repeat_=now+65; noteUserInput();
        }
        controller_direction_=direction; controller_buttons_=buttons; return;
    }
    if (gameOverlayOpen()) {
        if (overlay_closing_) { swallow(); return; }
        const auto foreground = overlay_backend_.foreground();
        if (!foreground) { swallow(); return; } // Transient focus handoff / secure desktop.
        if (!overlayForeground(foreground)) { closeGameOverlay(false); swallow(); return; }
        const auto pressed = [&](WORD button) { return (buttons & button) && !(controller_buttons_ & button); };
        const auto guide = pad_guide_.update(buttons & pad_guide, now);
        if (guide == HoldGesture::Event::press) { overlayKey(VK_HOME); swallow(); return; }
        if (guide == HoldGesture::Event::hold && !overlay_close_confirm_) { openPowerMenu(); invalidateGameOverlay(); }
        pad_start_.swallow(buttons & XINPUT_GAMEPAD_START); // Options belongs to the game, even over Guide.
        const int direction = padArrow(state, buttons);
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            overlayKey(direction); controller_repeat_ = now + (direction != controller_direction_ ? 280 : 170);
        }
        dashboardPollHold(now);
        const bool widget_release = !(buttons & XINPUT_GAMEPAD_A) && (controller_buttons_ & XINPUT_GAMEPAD_A);
        if (widget_release) dashboardPress(2, false, now);
        if (pressed(XINPUT_GAMEPAD_A) && !dashboardPress(2, true, now)) overlayKey(VK_RETURN);
        else if (pressed(XINPUT_GAMEPAD_B)) overlayKey(VK_ESCAPE);
        controller_direction_ = direction; controller_buttons_ = buttons;
        return;
    }
    if (overlay_backend_.foreground() != hwnd_) {
        if (dashboard_press_source_ == 2) dashboardCancelPress();
        if (!settings_.guide_overlay || !settings_.guide_menu) {
            // No game in play: Guide is the way home from wherever a pad user ended up (a store's
            // sign-in page in the browser, a client window, Explorer from "Otwórz folder").
            // While a game runs it stays the game's (or Steam's) without the experimental overlay.
            if (settings_.guide_menu && !anyActiveSession() && !pending_launch_) {
                const auto guide = pad_guide_.update(buttons & pad_guide, now);
                pad_start_.swallow(buttons & XINPUT_GAMEPAD_START);
                controllerNavigate(0); controller_input_.stopRumble();
                controller_buttons_ = buttons; controller_direction_ = 0;
                if (guide == HoldGesture::Event::press) bringToFront();
                return;
            }
            swallow(); return;
        }
        controllerNavigate(0); controller_input_.stopRumble();
        // Guide is the only global pad shortcut. Options/Start belongs to the game.
        // Face buttons and sticks stay with the game until the overlay is open.
        const auto guide = pad_guide_.update(buttons & pad_guide, now);
        pad_start_.swallow(buttons & XINPUT_GAMEPAD_START);
        controller_buttons_ = buttons; controller_direction_ = 0;
        if (guide == HoldGesture::Event::press || guide == HoldGesture::Event::hold) {
            toggleGameOverlay(true); // The pad opens on the widgets; Shift+Tab opens the bar.
            if (guide == HoldGesture::Event::hold && gameOverlayOpen()) { openPowerMenu(); invalidateGameOverlay(); }
        }
        return;
    }
    if (launchAnimationActive()) {
        // B hides the starting game's picture (once it is past the press that started it).
        if ((buttons & XINPUT_GAMEPAD_B) && !(controller_buttons_ & XINPUT_GAMEPAD_B)) dismissLaunchAnimation();
        swallow(); return;
    }
    // Screensaver: any button or stick ends it; the waking input is swallowed until released.
    const bool stick = std::abs(state.x) > screensaver::stick_threshold || std::abs(state.y) > screensaver::stick_threshold;
    if (screensaverVerdict(saver_gate_.pad(buttons, stick || state.left_trigger))) { swallow(); return; }
    // Any pad activity restarts the screensaver's idle period, also buttons without an action here.
    if (buttons != controller_buttons_ || stick || left_trigger) last_input_ = std::chrono::steady_clock::now();
    if ((buttons & ~controller_buttons_) || stick || left_trigger) pad_input_ = true; // A field reached now brings up the keyboard.
    const auto pressed = [&](WORD button) { return (buttons & button) && !(controller_buttons_ & button); };
    const auto vertical = [&] {
        return (buttons & XINPUT_GAMEPAD_DPAD_UP) || state.y > 16000 ? -2 : (buttons & XINPUT_GAMEPAD_DPAD_DOWN) || state.y < -16000 ? 2 : 0;
    };
    // Guide/PS and Start/Options: short press vs hold, above every screen but the exit prompt.
    if (bulkCoversOpen() || identifyOpen() || tour_.open || libraryDialogOpen() || keyboard_.open || companion_.picker.open || tarot_.open || whats_new_.open ||
        profile_.open || aurelia_ui_.open) { // The keyboard: Start = "Gotowe".
        pad_guide_.swallow(buttons & pad_guide); pad_start_.swallow(buttons & XINPUT_GAMEPAD_START);
    } else if (!exit_prompt_) {
        const auto guide = pad_guide_.update(settings_.guide_menu && (buttons & pad_guide), now);
        if (guide == HoldGesture::Event::press) { noteUserInput(); openGuideMenu(); }
        else if (guide == HoldGesture::Event::hold) { noteUserInput(); openPowerMenu(); }
        const auto start = pad_start_.update(buttons & XINPUT_GAMEPAD_START, now);
        if (start == HoldGesture::Event::press) { noteUserInput(); toggleFriendsDrawer(); }
        else if (start == HoldGesture::Event::hold) { noteUserInput(); openAudioShortcut(); }
    }
    if (modalPad(state, buttons, now)) return;
    if (pressed(XINPUT_GAMEPAD_BACK) && tarotAvailable()) { // View / Select / Share: Tarot.
        noteUserInput(); openTarot(); controller_buttons_ = buttons; controller_direction_ = 0; return;
    }
    if (pressed(pad_right_trigger) && glanceAvailable()) {
        noteUserInput(); openGlance(); controller_buttons_ = buttons; controller_direction_ = 0; return;
    }
    if (left_trigger && companionAvailable()) {
        noteUserInput(); openCompanion(); controller_buttons_ = buttons; controller_direction_ = 0; return;
    }
    if (companionOpen() && !friends_open_) {
        // Her chat: D-pad/stick between Back, the suggestions and the field; A picks, B or L2 closes,
        // X types, Y asks what to play, LB/RB scroll the conversation.
        const int direction = padArrow(state, buttons);
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            noteUserInput();
            companionMove(direction == VK_LEFT ? -1 : direction == VK_RIGHT ? 1 : 0, direction == VK_UP ? -1 : direction == VK_DOWN ? 1 : 0);
            controller_repeat_ = now + (direction != controller_direction_ ? 280 : 170);
        }
        if (left_trigger || pressed(XINPUT_GAMEPAD_B)) { noteUserInput(); back(); }
        else if (pressed(XINPUT_GAMEPAD_A)) { noteUserInput(); companionAccept(); }
        else if (pressed(XINPUT_GAMEPAD_X)) {
            noteUserInput();
            if (companion_.chat.mind && !companion_.chat.mind_confirm) companionMindDeleteConfirm(true);
            else if (companion_.chat.feed && companion_.feed.view.typing()) openKeyboard();
            else if (!companion_.chat.confirm && !companion_.chat.viewer && !companion_.chat.mind && !companion_.chat.feed) openKeyboard();
        }
        else if (pressed(XINPUT_GAMEPAD_Y)) { noteUserInput(); companionQuick(); }
        else if (pressed(XINPUT_GAMEPAD_LEFT_SHOULDER)) { noteUserInput(); companionScroll(1); }
        else if (pressed(XINPUT_GAMEPAD_RIGHT_SHOULDER)) { noteUserInput(); companionScroll(-1); }
        controller_direction_ = direction; controller_buttons_ = buttons; return;
    }
    if (glanceOpen() && !friends_open_) {
        const int direction = vertical() ? vertical() : (buttons & XINPUT_GAMEPAD_DPAD_LEFT) || state.x < -16000 ? -1 :
            (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) || state.x > 16000 ? 1 : 0;
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            noteUserInput(); controllerNavigate(direction); controller_repeat_ = now + (direction != controller_direction_ ? 280 : 170);
        }
        if (pressed(XINPUT_GAMEPAD_LEFT_SHOULDER)) glanceStepSection(-1);
        else if (pressed(XINPUT_GAMEPAD_RIGHT_SHOULDER)) glanceStepSection(1);
        else if (pressed(XINPUT_GAMEPAD_A)) glanceAccept();
        else if (pressed(XINPUT_GAMEPAD_B)) back();
        controller_direction_ = direction; controller_buttons_ = buttons; return;
    }
    if (friendChatOpen()) {
        // A friend's conversation in the drawer: up/down scroll it (up from its top reaches Back),
        // A types (or takes Back), B returns to the list, X the keyboard, Y reconnects, LB/RB a page.
        const int direction = vertical();
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            noteUserInput(); friendChatKey(direction < 0 ? VK_UP : VK_DOWN, false);
            controller_repeat_ = now + (direction != controller_direction_ ? 280 : 120);
        }
        if (pressed(XINPUT_GAMEPAD_A)) { noteUserInput(); friendsAccept(); }
        else if (pressed(XINPUT_GAMEPAD_B)) { noteUserInput(); closeFriendChat(); audio_.navigate(); }
        else if (pressed(XINPUT_GAMEPAD_X)) { noteUserInput(); friend_chat_.back_focus = false; openKeyboard(); }
        else if (pressed(XINPUT_GAMEPAD_Y)) { noteUserInput(); friendChatReconnect(); }
        else if (pressed(XINPUT_GAMEPAD_LEFT_SHOULDER)) friendChatScroll(-320);
        else if (pressed(XINPUT_GAMEPAD_RIGHT_SHOULDER)) friendChatScroll(320);
        controller_direction_ = direction; controller_buttons_ = buttons; return;
    }
    // Y: the focused game's menu on every screen (as a held A); the library bar is above the first row.
    if (pressed(XINPUT_GAMEPAD_Y)) { noteUserInput(); openContextMenu(); }
    // The drawers, the quick bar and the category menu keep LB/RB as Left/Right on the other layouts.
    const bool home_shoulders = ((metroActive() || psfiveActive()) && !menu_open_) ||
        (homeShoulderCategories() && !menu_open_ && !friends_open_ && !quick_open_ && !category_menu_open_);
    const bool shoulders_page = settingsOpen() || artworkOpen() || cropOpen() || devicesOpen() || home_shoulders ||
                                (libraryTabsShown() && !menu_open_);
    const int shoulder = pressed(XINPUT_GAMEPAD_LEFT_SHOULDER) ? -1 : pressed(XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 1 : 0;
    if (cropOpen()) {
        if (shoulder) cropTab(shoulder);
        if (pressed(XINPUT_GAMEPAD_X)) cropReset();
    } else if (pressed(XINPUT_GAMEPAD_X)) { noteUserInput(); if (!settingsResetFocused()) openKeyboard(); } // Settings: a row's own value back to the default.
    if (shoulder && settingsOpen() && !settings_state_.editing) { noteUserInput(); settingsShoulder(shoulder); }
    if (shoulder && artworkOpen()) artworkPage(shoulder);
    if (shoulder && devicesOpen()) deviceColumn(shoulder);
    if (shoulder && libraryTabsShown() && !menu_open_) { noteUserInput(); switchLibraryTab(shoulder); } // Kinowy / Przegląd categories.
    if (shoulder && home_shoulders) { noteUserInput(); homePage(shoulder); } // Metro pages, PSfive tabs, the other layouts' categories.
    int direction = 0;
    if (buttons & XINPUT_GAMEPAD_DPAD_LEFT || (!shoulders_page && (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER)) || state.x < -16000) direction = -1;
    if (buttons & XINPUT_GAMEPAD_DPAD_RIGHT || (!shoulders_page && (buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER)) || state.x > 16000) direction = 1;
    if (const int up_down = vertical()) direction = up_down;
    if (direction != controller_direction_) { controllerNavigate(0); pad_direction_since_ = now; }
    if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
        noteUserInput();
        if (pad_enter_hold_) cancelEnterHold(); // Moving away while A is held: neither the menu nor a launch.
        controllerNavigate(direction);
        // A held direction repeats as a held arrow key does (Windows' keyboard repeat rate, ~30 a second
        // by default): a row of games is crossed as fast with the pad as with the keyboard.
        // A volume slider glides instead: 1% about every 30 ms after a short pause (~0..100% in 3 s).
        const bool volume = std::abs(direction) == 1 && volumeSliderFocused();
        controller_repeat_ = now + (volume ? (direction != controller_direction_ ? 260 : 30) :
                                    direction != controller_direction_ ? 280 : keyRepeatInterval());
    }
    dashboardPollHold(now);
    if (!(buttons & XINPUT_GAMEPAD_A) && (controller_buttons_ & XINPUT_GAMEPAD_A)) {
        dashboardPress(2, false, now);
        // A released before the hold opened the game's menu: a plain press.
        if (pad_enter_hold_) {
            pad_enter_hold_ = false;
            if (enter_held_) { enter_held_ = false; if (!menu_open_) accept(); }
        }
    }
    if (pressed(XINPUT_GAMEPAD_A) && !dashboardPress(2, true, now)) {
        noteUserInput();
        if (settingsOpen()) settingsActivate();
        else if (flixIconsFocused()) flixIconAccept();
        else if (flixRailFocused()) flixRailAccept(flix_rail_focus_);
        else if (quick_open_) quickAccept();
        else if (friends_open_) friendsAccept();
        else if (category_menu_open_) categoryMenuAccept();
        else if (menu_open_) menuAccept();
        else if (acceptHoldsForMenu()) {
            // As a held Enter: tick() opens the game's menu after enter_hold_seconds.
            pad_enter_hold_ = enter_held_ = true;
            enter_started_ = std::chrono::steady_clock::now();
        } else accept();
    }
    if (pressed(XINPUT_GAMEPAD_B)) { noteUserInput(); back(); }
    controller_direction_ = direction;
    controller_buttons_ = buttons;
}

bool Launcher::modalPad(const PadReading& state, WORD buttons, ULONGLONG now) {
    const auto pressed = [&](WORD button) { return (buttons & button) && !(controller_buttons_ & button); };
    const auto modal = topModal();
    if (modal == Modal::exit_prompt || modal == Modal::power_menu) {
        const int direction = (buttons & XINPUT_GAMEPAD_DPAD_UP) || state.y > 16000 ? -2 : (buttons & XINPUT_GAMEPAD_DPAD_DOWN) || state.y < -16000 ? 2 : 0;
        if (direction != controller_direction_) controllerNavigate(0);
        if (direction && direction != controller_direction_) {
            noteUserInput();
            controllerNavigate(direction);
        }
        controller_direction_ = direction;
        const bool power = modal == Modal::power_menu;
        if (pressed(XINPUT_GAMEPAD_A)) { noteUserInput(); if (power) powerMenuAccept(); else exitPromptAccept(); }
        else if (pressed(XINPUT_GAMEPAD_B)) { noteUserInput(); if (power) closePowerMenu(); else exitPromptKey(VK_ESCAPE, false); }
        controller_buttons_ = buttons;
        return true;
    }
    if (keyboard_.open && !textFieldActive()) closeKeyboard(); // Its field closed underneath (Esc, a click).
    int direction = padArrow(state, buttons);
    if (keyboard_.open) {
        // On-screen keyboard above its field's dialog: D-pad/stick pick a key (rows wrap), A types it,
        // B erases, Y a space, LB/RB move the caret, Start confirms ("Gotowe"), X hides the keyboard.
        controllerNavigate(0);
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            noteUserInput();
            keyboardMove(direction == VK_LEFT ? -1 : direction == VK_RIGHT ? 1 : 0, direction == VK_UP ? -1 : direction == VK_DOWN ? 1 : 0);
            controller_repeat_ = now + (direction != controller_direction_ ? 280 : 110);
        }
        // B repeats while held, as Backspace does.
        const bool erase = (buttons & XINPUT_GAMEPAD_B) && (pressed(XINPUT_GAMEPAD_B) || now >= keyboard_erase_repeat_);
        if (pressed(XINPUT_GAMEPAD_A)) { noteUserInput(); keyboardPress(osk::focused(keyboard_)); }
        else if (erase) {
            noteUserInput(); keyboardPress({osk::Kind::backspace});
            keyboard_erase_repeat_ = now + (pressed(XINPUT_GAMEPAD_B) ? 420 : 70);
        } else if (pressed(XINPUT_GAMEPAD_Y)) { noteUserInput(); keyboardPress({osk::Kind::space, L' '}); }
        else if (pressed(XINPUT_GAMEPAD_START)) { noteUserInput(); keyboardDone(); }
        else if (pressed(XINPUT_GAMEPAD_X)) { noteUserInput(); closeKeyboard(); }
        else if (pressed(XINPUT_GAMEPAD_LEFT_SHOULDER)) { noteUserInput(); keyboardCaret(-1); }
        else if (pressed(XINPUT_GAMEPAD_RIGHT_SHOULDER)) { noteUserInput(); keyboardCaret(1); }
        controller_direction_ = direction; controller_buttons_ = buttons;
        return true;
    }
    switch (modal) {
    case Modal::aurelia:
        controllerNavigate(0);
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            noteUserInput(); aureliaKey(direction, false); controller_repeat_ = now + (direction != controller_direction_ ? 280 : 150);
        }
        if (pressed(XINPUT_GAMEPAD_A)) { noteUserInput(); aureliaKey(VK_RETURN, false); }
        else if (pressed(XINPUT_GAMEPAD_B)) { noteUserInput(); aureliaKey(VK_ESCAPE, false); }
        else if (pressed(XINPUT_GAMEPAD_X) && !aurelia_ui_.input_hint.empty()) { aurelia_ui_.input_focus = true; openKeyboard(); }
        else if (pressed(XINPUT_GAMEPAD_Y)) aureliaKey(VK_F5, false);
        else if (pressed(XINPUT_GAMEPAD_LEFT_SHOULDER)) aureliaScroll(-4);
        else if (pressed(XINPUT_GAMEPAD_RIGHT_SHOULDER)) aureliaScroll(4);
        break;
    case Modal::whats_new: // Co nowego: D-pad/stick scroll, A or B closes it.
        controllerNavigate(0);
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            noteUserInput(); whatsNewKey(direction);
            controller_repeat_ = now + (direction != controller_direction_ ? 280 : 90);
        }
        if (pressed(XINPUT_GAMEPAD_A) || pressed(XINPUT_GAMEPAD_B)) { noteUserInput(); closeWhatsNew(); }
        break;
    case Modal::tarot:
        // The reading: D-pad/stick move between the cards (and the buttons under the games), A picks,
        // B takes the last card back (on the first, and over the games, it closes), View closes.
        controllerNavigate(0);
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            noteUserInput(); tarotKey(direction, false);
            controller_repeat_ = now + (direction != controller_direction_ ? 280 : 200);
        }
        if (pressed(XINPUT_GAMEPAD_A)) { noteUserInput(); tarotKey(VK_RETURN, false); }
        else if (pressed(XINPUT_GAMEPAD_B)) { noteUserInput(); tarotKey(VK_BACK, false); }
        else if (pressed(XINPUT_GAMEPAD_BACK)) { noteUserInput(); tarotKey(VK_ESCAPE, false); }
        break;
    case Modal::profile:
        // Profil: D-pad/stick move between its games and platforms, A opens one, B closes it, LB/RB
        // scroll a screen up or down.
        controllerNavigate(0);
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            noteUserInput(); profileKey(direction, false);
            controller_repeat_ = now + (direction != controller_direction_ ? 280 : 150);
        }
        if (pressed(XINPUT_GAMEPAD_A)) { noteUserInput(); profileKey(VK_RETURN, false); }
        else if (pressed(XINPUT_GAMEPAD_B) || pressed(XINPUT_GAMEPAD_BACK)) { noteUserInput(); profileKey(VK_ESCAPE, false); }
        else if (pressed(XINPUT_GAMEPAD_LEFT_SHOULDER)) { noteUserInput(); profileKey(VK_PRIOR, false); }
        else if (pressed(XINPUT_GAMEPAD_RIGHT_SHOULDER)) { noteUserInput(); profileKey(VK_NEXT, false); }
        break;
    case Modal::companion_picker:
        controllerNavigate(0);
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            noteUserInput(); companionPickerKey(direction, false);
            controller_repeat_ = now + (direction != controller_direction_ ? 280 : 170);
        }
        if (pressed(XINPUT_GAMEPAD_A)) { noteUserInput(); companionPickerKey(VK_RETURN, false); }
        else if (pressed(XINPUT_GAMEPAD_B)) { noteUserInput(); companionPickerKey(VK_ESCAPE, false); }
        else if (pressed(XINPUT_GAMEPAD_LEFT_SHOULDER)) companionPickerKey(VK_PRIOR, false);
        else if (pressed(XINPUT_GAMEPAD_RIGHT_SHOULDER)) companionPickerKey(VK_NEXT, false);
        break;
    case Modal::bulk_covers:
        controllerNavigate(0);
        direction = (buttons & XINPUT_GAMEPAD_DPAD_LEFT) || state.x < -16000 ? VK_LEFT : // Horizontal first: its buttons are a row.
            (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) || state.x > 16000 ? VK_RIGHT :
            (buttons & XINPUT_GAMEPAD_DPAD_UP) || state.y > 16000 ? VK_UP :
            (buttons & XINPUT_GAMEPAD_DPAD_DOWN) || state.y < -16000 ? VK_DOWN : 0;
        if (direction && direction != controller_direction_) bulkCoversKey(direction, false);
        if (pressed(XINPUT_GAMEPAD_A)) bulkCoversKey(VK_RETURN, false);
        else if (pressed(XINPUT_GAMEPAD_B)) bulkCoversKey(VK_ESCAPE, false);
        break;
    case Modal::file_browser:
        // File picker: D-pad/stick move (lists repeat faster), A opens or picks, B goes one folder
        // up (at "Ten komputer" it cancels), LB/RB page, Y shows hidden files, X picks the open folder.
        controllerNavigate(0);
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            noteUserInput(); fileBrowserKey(direction, false);
            controller_repeat_ = now + (direction != controller_direction_ ? 280 : 110);
        }
        if (pressed(XINPUT_GAMEPAD_A)) { noteUserInput(); fileBrowserKey(VK_RETURN, false); }
        else if (pressed(XINPUT_GAMEPAD_B)) { noteUserInput(); fileBrowserKey(VK_BACK, false); }
        else if (pressed(XINPUT_GAMEPAD_LEFT_SHOULDER)) { noteUserInput(); fileBrowserKey(VK_PRIOR, false); }
        else if (pressed(XINPUT_GAMEPAD_RIGHT_SHOULDER)) { noteUserInput(); fileBrowserKey(VK_NEXT, false); }
        else if (pressed(XINPUT_GAMEPAD_Y)) { noteUserInput(); fileBrowserButton(file_browser::Button::hidden); }
        else if (pressed(XINPUT_GAMEPAD_X) && file_browser_.folderMode()) { noteUserInput(); fileBrowserButton(file_browser::Button::choose_folder); }
        break;
    case Modal::library_dialog:
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            noteUserInput(); libraryDialogKey(direction, false); controller_repeat_ = now + (direction != controller_direction_ ? 280 : 170);
        }
        if (pressed(XINPUT_GAMEPAD_A)) { noteUserInput(); libraryDialogKey(VK_RETURN, false); }
        else if (pressed(XINPUT_GAMEPAD_B)) { noteUserInput(); libraryDialogKey(VK_ESCAPE, false); }
        else if (pressed(XINPUT_GAMEPAD_X)) { noteUserInput(); openKeyboard(); } // Name or games filter.
        break;
    case Modal::tour:
        // Walkthrough: D-pad/stick move between its buttons or layout tiles, A confirms, B goes back.
        controllerNavigate(0);
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            noteUserInput(); tourKey(direction, false);
            controller_repeat_ = now + (direction != controller_direction_ ? 280 : 170);
        }
        if (pressed(XINPUT_GAMEPAD_A)) { noteUserInput(); tourKey(VK_RETURN, false); }
        else if (pressed(XINPUT_GAMEPAD_B)) { noteUserInput(); tourKey(VK_ESCAPE, false); }
        break;
    case Modal::gog_install:
        controllerNavigate(0);
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            gogInstallKey(direction, false); controller_repeat_ = now + 230;
        }
        if (pressed(XINPUT_GAMEPAD_A)) gogInstallKey(VK_RETURN, false);
        if (pressed(XINPUT_GAMEPAD_B)) gogInstallKey(VK_ESCAPE, false);
        break;
    case Modal::custom_editor:
        controllerNavigate(0);
        // D-pad moves between fields and buttons, A activates, B cancels.
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            noteUserInput(); customEditorKey(direction, false);
            controller_repeat_ = now + (direction != controller_direction_ ? 280 : 170);
        }
        if (pressed(XINPUT_GAMEPAD_A)) { noteUserInput(); customEditorKey(VK_RETURN, false); }
        else if (pressed(XINPUT_GAMEPAD_B)) { noteUserInput(); customEditorKey(VK_ESCAPE, false); }
        else if (pressed(XINPUT_GAMEPAD_X)) { noteUserInput(); openKeyboard(); } // The focused field, else the name.
        break;
    case Modal::metadata:
        // Edytuj metadane: D-pad moves between fields and buttons, A types into a field (the on-screen
        // keyboard) or presses a button, X brings the keyboard up, B closes it.
        controllerNavigate(0);
        if (direction && (direction != controller_direction_ || now >= controller_repeat_)) {
            noteUserInput(); metadataKey(direction, false);
            controller_repeat_ = now + (direction != controller_direction_ ? 280 : 170);
        }
        if (pressed(XINPUT_GAMEPAD_A)) { noteUserInput(); metadataKey(VK_RETURN, false); }
        else if (pressed(XINPUT_GAMEPAD_B)) { noteUserInput(); metadataKey(VK_ESCAPE, false); }
        else if (pressed(XINPUT_GAMEPAD_X)) { noteUserInput(); openKeyboard(); }
        break;
    default: return false; // No layer; the identify dialog takes the screen's paths (D-pad, A, B, X).
    }
    controller_direction_ = direction; controller_buttons_ = buttons;
    return true;
}
