// Desktop mode's dialogs over its pages (src/ui/desktop_dialogs_renderer.cpp): Kolekcje / Wersje,
// Dodaj / Edytuj grę and Zidentyfikuj grę. The renderer records each control it drew; a click maps
// onto Tryb TV's state and actions (collections.cpp, my_games.cpp, identify.cpp), keys and typing
// keep theirs. Only the dialog the renderer drew as a desktop dialog is handled here.
#include "launcher.hpp"
#include "launcher_internal.hpp"

using namespace desktop_ui;
using K = desktop_ui::SheetHit::Kind;

namespace {
bool matches(DialogKind drawn, Launcher::Modal modal) {
    return (drawn == DialogKind::collections && modal == Launcher::Modal::library_dialog) ||
           (drawn == DialogKind::custom_editor && modal == Launcher::Modal::custom_editor) ||
           (drawn == DialogKind::identify && modal == Launcher::Modal::identify) ||
           (drawn == DialogKind::power_menu && modal == Launcher::Modal::power_menu) ||
           (drawn == DialogKind::exit_prompt && modal == Launcher::Modal::exit_prompt) ||
           (drawn == DialogKind::gog_install && modal == Launcher::Modal::gog_install) ||
           (drawn == DialogKind::aurelia && modal == Launcher::Modal::aurelia);
}
}

bool Launcher::desktopModalShown() const { return desktop_mode_ && matches(renderer_.desktopModal(), topModal()); }

SheetHit Launcher::desktopModalHit(float mouse_x, float mouse_y) const {
    const float scale = renderer_.desktopViewport().scale, x = mouse_x / scale, y = mouse_y / scale;
    const auto& spots = renderer_.desktopModalHotspots();
    for (auto it = spots.rbegin(); it != spots.rend(); ++it) if (it->rect.contains(x, y)) return it->hit;
    return {};
}

bool Launcher::desktopModalClick(float mouse_x, float mouse_y, bool double_click) {
    if (!desktopModalShown()) return false;
    const auto hit = desktopModalHit(mouse_x, mouse_y);
    // The second click of a double-click never confirms a dialog on its own.
    if (double_click && hit.kind != K::field) return true;
    switch (topModal()) {
    case Modal::power_menu:
        if (hit.kind == K::row && hit.index >= 0 && hit.index + (hud_menu_ ? hud_menu_first_ : 0) < static_cast<int>(powerMenuCount())) {
            power_menu_ = hit.index + (hud_menu_ ? hud_menu_first_ : 0); powerMenuAccept();
        }
        else if (hit.kind != K::dialog) closePowerMenu(); // ✕ or beside it.
        return true;
    case Modal::exit_prompt:
        if (hit.kind == K::row && hit.index >= 0) { exit_prompt_ = hit.index; exitPromptAccept(); }
        else if (hit.kind != K::dialog) { exit_prompt_.reset(); prompt_kind_ = PromptKind::exit; InvalidateRect(hwnd_, nullptr, FALSE); }
        return true;
    case Modal::aurelia: aureliaDesktopClick(hit); return true;
    case Modal::gog_install:
        if (hit.kind == K::row && hit.index >= 0) { gog_install_ui_.row = hit.index; gogInstallAccept(); }
        else if (hit.kind == K::close) gogInstallKey(VK_ESCAPE, false);
        return true;
    case Modal::library_dialog: {
        auto& ui = library_dialog_;
        if (hit.kind == K::close) { for (int i = 0; i < 8 && libraryDialogOpen(); ++i) collectionDialogBack(); return true; }
        if (hit.kind == K::back) { ui.focus = -1; ui.confirm = false; collectionDialogAccept(); return true; }
        if (hit.kind == K::field) { ui.focus = -2; InvalidateRect(hwnd_, nullptr, FALSE); return true; }
        if (hit.kind == K::row) {
            if (hit.index != ui.focus) ui.confirm = false;
            ui.focus = hit.index;
            collectionDialogAccept();
        }
        return true;
    }
    case Modal::custom_editor: {
        using custom_game_ui::Item;
        auto& ui = custom_editor_;
        if (hit.kind == K::close) { closeCustomEditor(); return true; }
        if (hit.kind == K::field && hit.index >= 0 && hit.index < custom_game_ui::field_count) {
            constexpr Item items[] = {Item::name, Item::file, Item::arguments, Item::directory, Item::dll, Item::wine, Item::platform};
            if (items[hit.index] == Item::platform) { // Picked, not typed: a click walks the consoles.
                ui.focus = Item::platform; customEditorCyclePlatform(1); return true;
            }
            if (ui.focus != items[hit.index]) { ui.focus = items[hit.index]; ui.caret = ui.fields[static_cast<size_t>(hit.index)].size(); }
            ui.selected = false;
            ui.confirm_remove = false;
            const float scale = renderer_.desktopViewport().scale; // The caret where the click falls; a drag selects.
            for (const auto& spot : renderer_.desktopModalHotspots())
                if (spot.hit == hit) { desktopTextPress(spot.rect.left + 12, mouse_x / scale, scale, double_click, false); break; }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return true;
        }
        if (hit.kind == K::tile_action) {
            ui.focus = custom_game_ui::browseItem(hit.index);
            ui.confirm_remove = false;
            customEditorAccept();
            return true;
        }
        if (hit.kind == K::action) {
            ui.focus = static_cast<Item>(hit.index);
            if (ui.focus != Item::remove) ui.confirm_remove = false;
            customEditorAccept();
        }
        return true;
    }
    case Modal::identify: {
        auto& state = identify_state_;
        if (hit.kind == K::none || hit.kind == K::close) { closeIdentify(); return true; } // Beside it or ✕: as Back.
        if (state.applying) return true;
        if (!state.manual) { if (hit.kind == K::action) identifyManual(); } // Only "Dopasuj ręcznie" in the lookup view.
        else if (state.menu) { // The provider dropdown: a row picks, anywhere else closes it.
            if (hit.kind == K::dropdown_item) identifyPickProvider(hit.index); else state.menu = false;
        }
        else if (hit.kind == K::toggle) identifyToggleMenu();
        else if (hit.kind == K::action) identifySearch();
        else if (hit.kind == K::field) { state.query_focus = true; state.select_all = true; state.back_focus = false; }
        else if (hit.kind == K::row && !state.busy) {
            state.selected = hit.index; state.query_focus = state.back_focus = false;
            identifyAccept();
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }
    default: return false;
    }
}

bool Launcher::desktopModalMove(float mouse_x, float mouse_y) {
    if (!desktopModalShown()) {
        if (!(desktop_dialog_hover_ == SheetHit{})) { desktop_dialog_hover_ = {}; InvalidateRect(hwnd_, nullptr, FALSE); }
        return false;
    }
    const auto hit = desktopModalHit(mouse_x, mouse_y);
    if (!(hit == desktop_dialog_hover_)) { desktop_dialog_hover_ = hit; InvalidateRect(hwnd_, nullptr, FALSE); }
    return true;
}

LPCWSTR Launcher::desktopModalCursor(float mouse_x, float mouse_y) const {
    if (!desktopModalShown()) return nullptr;
    const auto hit = desktopModalHit(mouse_x, mouse_y);
    switch (hit.kind) {
    case K::none: case K::dialog: return IDC_ARROW;
    case K::field: return IDC_IBEAM;
    default: return IDC_HAND;
    }
}

// A text field of a desktop dialog: its caret blinks.
bool Launcher::tickDesktopDialog() {
    if (!desktopModalShown()) return false;
    const bool typing = (topModal() == Modal::library_dialog && library_dialog_.focus == -2 && library_dialog_.textField()) ||
                        (topModal() == Modal::custom_editor && custom_game_ui::field(custom_editor_.focus) >= 0) ||
                        (topModal() == Modal::identify && identify_state_.query_focus) ||
                        (topModal() == Modal::aurelia && aureliaTyping());
    if (!typing) return false;
    const auto phase = GetTickCount64() / 530;
    if (phase == desktop_dialog_caret_) return false;
    desktop_dialog_caret_ = phase;
    return true;
}
