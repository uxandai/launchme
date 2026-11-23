// Dodaj / Edytuj grę and Opcje uruchamiania (my_games.cpp): the panel's focus, its keys and pad, a
// tap or click on it. The Mody tab's rows (game_mods_panel.cpp) and the options under the launch tab's
// fields (launch_choices.cpp) move here like the rest.
#include "launcher.hpp"

#include <algorithm>

using custom_game_ui::Item;

namespace {
// Caret steps keep surrogate pairs together.
size_t previousStop(const std::wstring& text, size_t caret) {
    if (!caret) return 0;
    --caret;
    if (caret && IS_LOW_SURROGATE(text[caret]) && IS_HIGH_SURROGATE(text[caret - 1])) --caret;
    return caret;
}
size_t nextStop(const std::wstring& text, size_t caret) {
    if (caret >= text.size()) return text.size();
    ++caret;
    if (caret < text.size() && IS_LOW_SURROGATE(text[caret]) && IS_HIGH_SURROGATE(text[caret - 1])) ++caret;
    return caret;
}
} // namespace

void Launcher::customEditorFocus(Item item) {
    auto& ui = custom_editor_;
    if (!ui.open || ui.focus == item || (item == Item::remove && !ui.editing()) || (item == Item::identify && !ui.posterVisible())) return;
    if (custom_game_ui::tabItem(item)) { // A tab shows as soon as it is reached.
        if (ui.tabs()) customEditorTab(item == Item::tab_mods ? custom_game_ui::Tab::mods : item == Item::tab_scripts ? custom_game_ui::Tab::scripts : custom_game_ui::Tab::launch);
        return;
    }
    if (item == Item::mods_folder || custom_game_ui::modItem(item)) { // The Mody tab's folder and its rows.
        if (!ui.mods() || (item == Item::mods_folder && mods_.emulator)) return;
        const auto* line = ui.mod_row < ui.modCount() ? &ui.mod_rows[static_cast<size_t>(ui.mod_row)] : nullptr;
        if (custom_game_ui::modItem(item) && !line) return;
        if (item == Item::mod_remove && !line->removable) return;
        if (item == Item::mod_method && !line->methods) return;
    }
    if (item == Item::option && ui.option >= ui.optionCount()) return;
    if (item == Item::option_more && (ui.option >= ui.optionCount() || !ui.options[static_cast<size_t>(ui.option)].more)) return;
    if ((item == Item::pick_file && !ui.pills()) || (item == Item::pick_folder && !ui.pills() && !ui.folderButton())) return;
    if (item == Item::file_browse && !ui.browses(custom_game_ui::file_field)) return;
    if (item == Item::identify) ui.identify_from = ui.focus;
    if (const int row = custom_game_ui::row(item); row < custom_game_ui::field_count && !ui.shows(row)) return;
    ui.focus = item;
    ui.selected = ui.confirm_remove = false;
    if (const int index = custom_game_ui::field(item); index >= 0) ui.caret = ui.fields[index].size();
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::customEditorMove(int horizontal, int vertical) {
    const auto focus = custom_editor_.focus;
    auto next = focus;
    const bool poster = custom_editor_.posterVisible();
    const bool pills = custom_editor_.pills();
    const bool tabs = custom_editor_.tabs();
    const bool file_browse = custom_editor_.browses(custom_game_ui::file_field);
    const int options = custom_editor_.optionCount();
    const auto lastField = [&] { // The last row of fields shown, -1 when none.
        int row = custom_game_ui::field_count - 1;
        while (row >= 0 && !custom_editor_.shows(row)) --row;
        return row;
    };
    const auto fieldItem = [](int row) { return custom_game_ui::fieldItem(row); };
    const auto firstRow = [&] { // Down from above the fields: the first field, else the options or the buttons.
        int row = 0;
        while (row < custom_game_ui::field_count && !custom_editor_.shows(row)) ++row;
        if (row < custom_game_ui::field_count) return fieldItem(row);
        if (options) custom_editor_.option = 0;
        return options ? Item::option : Item::save;
    };
    if (custom_game_ui::tabItem(focus)) {
        // The tabs: Left/Right between them (each shows at once), Down into the tab, Up nowhere.
        if (horizontal) customEditorTabStep(horizontal > 0 ? 1 : -1);
        else if (vertical > 0 && custom_editor_.mods()) {
            if (mods_.emulator) { custom_editor_.mod_row = 0; customEditorFocus(Item::mod_action); }
            else customEditorFocus(Item::mods_folder);
        }
        else if (vertical > 0) customEditorFocus(pills ? Item::pick_file : firstRow());
        else navigationBoundary();
        return;
    }
    if (custom_editor_.mods() && (focus == Item::mods_folder || custom_game_ui::modItem(focus) || custom_game_ui::row(focus) == custom_game_ui::buttons_row)) {
        // The Mody tab: the folder, then a row per mod (its method, install, "Usuń"), then the buttons.
        auto& ui = custom_editor_;
        const int count = ui.modCount();
        const auto rowItem = [&](int row, Item wanted) { // The same column where the row has it, else its install.
            const auto& line = ui.mod_rows[static_cast<size_t>(row)];
            if (wanted == Item::mod_remove && !line.removable) return Item::mod_action;
            if (wanted == Item::mod_method && !line.methods) return Item::mod_action;
            return custom_game_ui::modItem(wanted) ? wanted : Item::mod_action;
        };
        const auto toRow = [&](int row, Item wanted) {
            if (row != ui.mod_row) mods_.remove_row = mods_.anticheat_row = -1;
            ui.mod_row = row;
            const auto item = rowItem(row, wanted);
            if (ui.focus == item) audio_.navigate();
            else customEditorFocus(item);
            modsPanelRefresh(false);
        };
        if (focus == Item::mods_folder) {
            if (vertical > 0 && count) toRow(0, Item::mod_action);
            else if (vertical < 0) customEditorFocus(Item::tab_mods);
            else navigationBoundary();
            return;
        }
        if (custom_game_ui::row(focus) == custom_game_ui::buttons_row) {
            if (vertical < 0 && count) { toRow(count - 1, Item::mod_action); return; }
        } else {
            if (focus == Item::mod_method && horizontal) { modsPanelMethod(horizontal); return; } // The method steps back or on.
            // The row's controls left to right: the method, "Usuń" (Launchme's own), install.
            const auto& line = ui.mod_rows[static_cast<size_t>(ui.mod_row)];
            if (horizontal > 0) { if (focus == Item::mod_remove) customEditorFocus(Item::mod_action); else navigationBoundary(); return; }
            if (horizontal < 0) {
                if (focus == Item::mod_action && line.removable) customEditorFocus(Item::mod_remove);
                else if (line.methods) customEditorFocus(Item::mod_method);
                else navigationBoundary();
                return;
            }
            if (vertical < 0) {
                if (ui.mod_row > 0) toRow(ui.mod_row - 1, focus);
                else if (mods_.emulator) customEditorFocus(Item::tab_mods);
                else customEditorFocus(Item::mods_folder);
                return;
            }
            if (vertical > 0) {
                if (ui.mod_row + 1 < count) toRow(ui.mod_row + 1, focus);
                else customEditorFocus(Item::save);
            }
            return;
        }
    }
    if (focus == Item::option || focus == Item::option_more) {
        // The options, a row each: Left/Right step a choice (Right from a switch: its "…"), Up/Down to the
        // next row; above them the last field (or the tabs), under them the buttons.
        const int current = std::clamp(custom_editor_.option, 0, options - 1);
        const auto& option = custom_editor_.options[static_cast<size_t>(current)];
        if (horizontal) {
            if (focus == Item::option_more) { if (horizontal < 0) customEditorFocus(Item::option); else navigationBoundary(); }
            else if (option.choice()) customEditorOptionStep(horizontal > 0 ? 1 : -1);
            else if (horizontal > 0 && option.more) customEditorFocus(Item::option_more);
            else navigationBoundary();
            return;
        }
        const int other = current + (vertical > 0 ? 1 : -1);
        if (other >= 0 && other < options) { customEditorOption(other); return; }
        if (vertical > 0) next = Item::save;
        else if (const int row = lastField(); row >= 0) next = fieldItem(row);
        else if (tabs) next = custom_editor_.scripts() ? Item::tab_scripts : Item::tab_launch;
        else { navigationBoundary(); return; }
        customEditorFocus(next);
        return;
    }
    // Up from the buttons, Down from the last field: the options between them.
    const bool last_row = custom_game_ui::row(focus) == lastField() && custom_game_ui::row(focus) < custom_game_ui::field_count;
    if (options && vertical < 0 && custom_game_ui::row(focus) == custom_game_ui::buttons_row) { customEditorOption(options - 1); return; }
    if (options && vertical > 0 && last_row) { customEditorOption(0); return; }
    if (focus == Item::pick_file || focus == Item::pick_folder) {
        // The pills' row above the fields: Left/Right between them (then the poster's column), Down to the first field.
        if (vertical > 0) {
            int row = 0;
            while (row < custom_game_ui::field_count && !custom_editor_.shows(row)) ++row;
            next = row == 0 ? Item::name : row == 1 ? Item::file : Item::save;
        } else if (vertical < 0) {
            if (tabs) next = Item::tab_launch; // Above them: the tabs.
            else { navigationBoundary(); return; }
        }
        else if (horizontal > 0) next = focus == Item::pick_file ? Item::pick_folder : poster ? Item::identify : focus;
        else if (horizontal < 0) next = focus == Item::pick_folder ? Item::pick_file : focus;
    } else if (focus == Item::identify) {
        // The poster's column: Left back to the row it was reached from, Down to the bottom buttons.
        if (horizontal < 0) next = custom_editor_.identify_from == Item::identify ? Item::name : custom_editor_.identify_from;
        else if (vertical > 0) next = Item::save;
        else { navigationBoundary(); return; }
    } else if (vertical) {
        // Rows the panel leaves out (launch options) are passed over.
        int row = custom_game_ui::row(focus);
        do row += vertical; while (row >= 0 && row < custom_game_ui::field_count && !custom_editor_.shows(row));
        if (row < 0) {
            if (pills) { customEditorFocus(Item::pick_file); return; } // Up from the first field: the pills, then the tabs.
            if (tabs) customEditorFocus(custom_editor_.scripts() ? Item::tab_scripts : Item::tab_launch);
            return;
        }
        row = std::min(row, custom_game_ui::buttons_row);
        const bool right = focus == Item::file_browse || focus == Item::directory_browse || focus == Item::wine_page || focus == Item::save ||
                           custom_game_ui::testItem(focus);
        switch (row) {
        case 0: next = Item::name; break;
        case 1: next = right && file_browse ? Item::file_browse : Item::file; break;
        case 2: next = Item::arguments; break;
        case 3: next = right ? Item::directory_browse : Item::directory; break;
        case 4: next = Item::dll; break;
        case 5: next = right ? Item::wine_page : Item::wine; break;
        case 6: next = Item::platform; break;
        case 7: case 8: case 9: next = right ? custom_game_ui::browseItem(row) : fieldItem(row); break;
        default: if (custom_game_ui::row(focus) != custom_game_ui::buttons_row) next = Item::save; break;
        }
    } else if (horizontal) {
        const bool forward = horizontal > 0;
        // The rightmost item of a field row continues to "Zidentyfikuj grę" under the poster.
        switch (focus) {
        case Item::name: case Item::arguments: case Item::dll: case Item::platform: if (forward && poster) next = Item::identify; break;
        case Item::file: if (forward) next = file_browse ? Item::file_browse : poster ? Item::identify : focus; break;
        case Item::file_browse: next = forward ? (poster ? Item::identify : focus) : Item::file; break;
        case Item::directory: if (forward) next = Item::directory_browse; break;
        case Item::directory_browse: next = forward ? (poster ? Item::identify : focus) : Item::directory; break;
        case Item::wine: if (forward) next = Item::wine_page; break;
        case Item::wine_page: next = forward ? (poster ? Item::identify : focus) : Item::wine; break;
        case Item::before: case Item::started: case Item::exited: if (forward) next = custom_game_ui::browseItem(custom_game_ui::row(focus)); break;
        case Item::test_before: case Item::test_started: case Item::test_exited: if (!forward) next = fieldItem(custom_game_ui::row(focus)); break;
        case Item::remove: if (forward) next = Item::cancel; break;
        case Item::cancel: next = forward ? Item::save : custom_editor_.editing() ? Item::remove : Item::cancel; break;
        case Item::save: if (!forward) next = Item::cancel; break;
        default: break;
        }
    }
    customEditorFocus(next);
}

void Launcher::customEditorKey(WPARAM key, bool repeated) {
    if (!customEditorOpen()) return;
    auto& ui = custom_editor_;
    if (key == VK_F8 && !repeated) { togglePerformance(); return; }
    if (key == VK_F11 && !repeated) { toggleFullscreen(); return; }
    if (ui.dropdown.open) { // An open list keeps the keys: Up/Down through it, A picks, B closes it.
        switch (key) {
        case VK_UP: customEditorDropdownMove(-1); break;
        case VK_DOWN: customEditorDropdownMove(1); break;
        case VK_RETURN: case VK_SPACE: if (!repeated) customEditorDropdownChoose(ui.dropdown.focus); break;
        case VK_ESCAPE: case VK_BACK: case VK_LEFT: case VK_RIGHT: case VK_TAB: customEditorDropdownClose(); break;
        default: break;
        }
        return;
    }
    if (key == VK_ESCAPE) { closeCustomEditor(); return; }
    // AltGr reports Ctrl+Alt: AltGr+A types "ą" and must not select the field.
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000);
    const int index = custom_game_ui::field(ui.focus);
    // The platform is picked, never typed: Left/Right walk the consoles.
    if (index == custom_game_ui::platform_field) {
        if (key == VK_LEFT || key == VK_RIGHT) { customEditorCyclePlatform(key == VK_LEFT ? -1 : 1); return; }
        if (key == VK_BACK || key == VK_DELETE || key == 'V' || key == 'A') return;
    } else if (index >= 0) {
        auto& text = ui.fields[index];
        ui.caret = std::min(ui.caret, text.size());
        const auto edited = [&] { ui.error.clear(); InvalidateRect(hwnd_, nullptr, FALSE); };
        switch (key) {
        case 'V': if (control) { customEditorPaste(); return; } break;
        case 'A':
            if (control) { ui.selected = !text.empty(); ui.caret = text.size(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
            break;
        case VK_BACK: case VK_DELETE:
            if (ui.selected) { text.clear(); ui.caret = 0; }
            else if (key == VK_BACK && ui.caret) {
                const auto start = previousStop(text, ui.caret);
                text.erase(start, ui.caret - start); ui.caret = start;
            } else if (key == VK_DELETE && ui.caret < text.size()) text.erase(ui.caret, nextStop(text, ui.caret) - ui.caret);
            ui.selected = false; edited(); return;
        case VK_HOME: case VK_END:
            ui.caret = key == VK_HOME ? 0 : text.size(); ui.selected = false;
            InvalidateRect(hwnd_, nullptr, FALSE); return;
        case VK_LEFT:
            ui.caret = ui.selected ? 0 : previousStop(text, ui.caret); ui.selected = false;
            InvalidateRect(hwnd_, nullptr, FALSE); return;
        case VK_RIGHT:
            // At the end of the text Right continues to the row's "Wybierz…" button.
            if (ui.selected || ui.caret < text.size()) {
                ui.caret = ui.selected ? text.size() : nextStop(text, ui.caret); ui.selected = false;
                InvalidateRect(hwnd_, nullptr, FALSE); return;
            }
            break;
        default: break;
        }
    } else if (key == VK_BACK) { closeCustomEditor(); return; }
    switch (key) {
    case VK_UP: customEditorMove(0, -1); break;
    case VK_DOWN: customEditorMove(0, 1); break;
    case VK_LEFT: customEditorMove(-1, 0); break;
    case VK_RIGHT: customEditorMove(1, 0); break;
    case VK_TAB: {
        std::vector<Item> order;
        if (ui.tabs()) order.push_back(ui.tabItemOf(ui.tab));
        if (ui.mods()) {
            if (!mods_.emulator) order.push_back(Item::mods_folder);
            if (ui.mod_row < ui.modCount()) {
                const auto& line = ui.mod_rows[static_cast<size_t>(ui.mod_row)];
                if (line.methods) order.push_back(Item::mod_method);
                if (line.removable) order.push_back(Item::mod_remove);
                order.push_back(Item::mod_action);
            }
        }
        if (ui.pills()) { order.push_back(Item::pick_file); order.push_back(Item::pick_folder); }
        for (const auto item : {Item::name, Item::file, Item::file_browse, Item::arguments, Item::directory, Item::directory_browse,
                                Item::dll, Item::wine, Item::wine_page, Item::platform, Item::before, Item::test_before,
                                Item::started, Item::test_started, Item::exited, Item::test_exited})
            if (ui.shows(custom_game_ui::row(item)) && (item != Item::file_browse || ui.browses(custom_game_ui::file_field))) order.push_back(item);
        if (ui.optionCount()) order.push_back(Item::option);
        if (ui.posterVisible()) order.push_back(Item::identify);
        if (ui.folderButton()) order.push_back(Item::pick_folder);
        if (ui.editing()) order.push_back(Item::remove);
        order.push_back(Item::cancel); order.push_back(Item::save);
        const bool back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ui.focus == Item::option && (back ? ui.option > 0 : ui.option + 1 < ui.optionCount())) { // Each option in turn.
            customEditorOption(ui.option + (back ? -1 : 1));
            break;
        }
        const auto position = static_cast<int>(std::find(order.begin(), order.end(), ui.focus) - order.begin());
        const int count = static_cast<int>(order.size());
        const auto target = order[(position + (back ? count - 1 : 1)) % count];
        if (target == Item::option) customEditorOption(back ? ui.optionCount() - 1 : 0);
        else customEditorFocus(target);
        break;
    }
    case VK_RETURN: if (!repeated) customEditorAccept(); break;
    case VK_SPACE: if (!repeated && index < 0) customEditorAccept(); break; // In a field WM_CHAR types the space.
    default: break;
    }
}

void Launcher::customEditorAccept() {
    auto& ui = custom_editor_;
    if (!ui.open) return;
    switch (ui.focus) {
    case Item::pick_file: customEditorBrowse(false); break;
    case Item::pick_folder: customEditorPickFolder(); break; // One game, a ROM folder or a folder of games (add_game.cpp).
    case Item::file_browse: customEditorBrowse(false); break;
    case Item::directory_browse: customEditorBrowse(true); break;
    case Item::wine_page: {
        // Saved first (the page shows the same options), then the game's Proton / Wine page.
        const auto key = ui.game_key;
        customEditorSave();
        if (custom_editor_.open || key.empty()) break; // A field to correct.
        for (const auto* games : {&public_library_, &private_category_.games})
            for (const auto& game : *games)
                if (gameKey(game) == key) { openGameCompatSettings(game); return; }
        break;
    }
    case Item::platform: customEditorCyclePlatform(1); break;
    case Item::tab_launch: customEditorTab(custom_game_ui::Tab::launch); break;
    case Item::tab_scripts: customEditorTab(custom_game_ui::Tab::scripts); break;
    case Item::tab_mods: customEditorTab(custom_game_ui::Tab::mods); break;
    case Item::mods_folder: modsPanelFolder(); break;
    case Item::mod_method: customEditorDropdown(Item::mod_method, ui.mod_row); break; // Its list; a pick goes on to the install.
    case Item::mod_action: modsPanelAction(false); break;
    case Item::mod_remove: modsPanelAction(true); break;
    case Item::test_before: case Item::test_started: case Item::test_exited: customEditorTestScript(custom_game_ui::row(ui.focus)); break;
    case Item::option: customEditorOptionAccept(); break;
    case Item::option_more: customEditorOptionMore(); break;
    case Item::save: customEditorSave(); break;
    case Item::cancel: closeCustomEditor(); break;
    case Item::identify: customEditorIdentify(); break;
    case Item::remove:
        if (ui.confirm_remove) { customEditorRemove(); break; }
        ui.confirm_remove = true;
        audio_.context();
        InvalidateRect(hwnd_, nullptr, FALSE);
        break;
    default: customEditorMove(0, 1); break; // A text field: on to the next row.
    }
}

void Launcher::customEditorClick(float x, float y) {
    const auto view = renderer_.viewport();
    if (custom_editor_.dropdown.open) { // An open list: a row picks, anywhere else closes it.
        const int item = custom_game_ui::Layout::of(custom_editor_, view.width, view.height).dropdownAt(x, y);
        if (item >= 0) customEditorDropdownChoose(item); else customEditorDropdownClose();
        return;
    }
    const auto hit = custom_game_ui::Layout::of(custom_editor_, view.width, view.height).hit(x, y, custom_editor_.editing());
    if (!hit) return;
    if (*hit == custom_game_ui::Item::platform && custom_editor_.focus == custom_game_ui::Item::platform) { customEditorCyclePlatform(1); return; }
    if (custom_game_ui::modItem(*hit)) { // A mod row's control: that row first (its method opens its list).
        const auto layout = custom_game_ui::Layout::of(custom_editor_, view.width, view.height);
        const int row = layout.modAt(x, y);
        if (row < 0) return;
        if (row != custom_editor_.mod_row) mods_.remove_row = mods_.anticheat_row = -1;
        custom_editor_.mod_row = row;
        customEditorFocus(*hit);
        customEditorAccept();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (*hit == custom_game_ui::Item::option || *hit == custom_game_ui::Item::option_more) { // An option: a switch flips, a choice opens its list.
        const int index = custom_game_ui::Layout::of(custom_editor_, view.width, view.height).optionAt(x, y);
        if (index < 0) return;
        customEditorOption(index);
        if (*hit == custom_game_ui::Item::option_more) { customEditorFocus(*hit); customEditorOptionMore(); }
        else customEditorOptionAccept();
        return;
    }
    customEditorFocus(*hit);
    if (custom_game_ui::field(*hit) < 0) customEditorAccept();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

