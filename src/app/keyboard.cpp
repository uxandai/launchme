// On-screen keyboard for the pad (X): typed keys go through the same paths as a physical
// keyboard (onCharacter, the per-field Backspace/Enter handlers), so every text field gets it.
// Layout and focus movement: src/ui/onscreen_keyboard.hpp.
#include "launcher.hpp"

using settings_ui::Control;

bool Launcher::textFieldActive() const {
    if (aurelia_ui_.open) return aureliaTyping();
    if (friendChatTyping() && topDialog() == Modal::none) return true; // The drawer's conversation.
    return (libraryDialogOpen() && library_dialog_.textField() && library_dialog_.focus == -2) ||
           (identifyOpen() && identify_state_.query_focus) ||
           (customEditorOpen() && custom_game_ui::field(custom_editor_.focus) >= 0) ||
           (metadataOpen() && metaTextFocused()) ||
           editingArtworkSearch() || editingSecret() || searching();
}

// X: focus the screen's text field when it has one (Home and the library open their search),
// then show the keyboard; X again hides it.
void Launcher::openKeyboard() {
    if (keyboard_.open) { closeKeyboard(); return; }
    if (desktop_mode_) return; // The desktop window types with the keyboard; no on-screen one.
    if (pending_launch_ || menu_open_ || quick_open_ || (friends_open_ && !friendChatTyping()) || category_menu_open_) return;
    if (!textFieldActive()) {
        if (libraryDialogOpen()) {
            if (!library_dialog_.textField()) return;
            library_dialog_.focus = -2;
        } else if (identifyOpen()) { identifyManual(); identify_state_.query_focus = true; identify_state_.back_focus = false; }
        else if (customEditorOpen()) customEditorFocus(custom_game_ui::Item::name);
        else if (metadataOpen() && desktop_meta_.find.open) { desktop_meta_.find.focus = meta_find::item::query; desktop_meta_.find.caret = desktop_meta_.find.query.size(); } // Znajdź: the title.
        else if (metadataOpen()) { desktop_meta_.focus = 0; desktop_meta_.caret = desktop_meta_.fields[0].size(); } // Tytuł.
        else if (artworkOpen()) {
            if (artwork_state_.applying) return;
            artwork_state_.gallery = false; artwork_state_.search_focus = true; artwork_state_.query_selected = true;
        } else if (settingsOpen()) {
            if (settings_rows_.empty() || settings_state_.editing) return;
            const auto control = settings_rows_[settings_state_.row()].control;
            if (control != Control::secret && control != Control::text) return;
            settingsActivate(); // Starts editing the key or text.
        } else if (companionOpen()) companion_.chat.focus = companion_ui::focus_input; // Her chat's message field.
        else if (screen_ == ScreenMode::library) openLibrarySearch();
        else if (screen_ == ScreenMode::shelves || screen_ == ScreenMode::platform_games) openSearch();
        else if (screen_ == ScreenMode::plugin_page && !plugin_drawer_active_ && !plugin_page_.rows.empty() && plugin_page_.rows.front().input) {
            plugin_page_.focus = 0; plugin_page_.scroll = 0; selected_ = 0; // X: the page's search field.
        }
        if (!textFieldActive()) return;
    }
    osk::open(keyboard_);
    audio_.drawer();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::closeKeyboard() {
    if (!keyboard_.open) return;
    keyboard_.open = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::keyboardMove(int dx, int dy) {
    if (!osk::move(keyboard_, dx, dy)) { navigationBoundary(); return; }
    audio_.navigate();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::keyboardPress(const osk::Key& key) {
    switch (key.kind) {
    case osk::Kind::character:
        onCharacter(osk::typed(keyboard_, key.character));
        keyboard_.shift = false;
        break;
    case osk::Kind::space: onCharacter(L' '); break;
    case osk::Kind::shift: keyboard_.shift = !keyboard_.shift; break;
    case osk::Kind::layer:
        keyboard_.symbols = !keyboard_.symbols;
        osk::focus(keyboard_, keyboard_.row, keyboard_.key); // Same position on the other layer.
        break;
    case osk::Kind::backspace: keyboardBackspace(); break;
    case osk::Kind::done: keyboardDone(); return;
    }
    if (keyboard_.open && !textFieldActive()) closeKeyboard(); // Backspace on an empty search leaves it.
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// Backspace as on a physical keyboard, per field.
void Launcher::keyboardBackspace() {
    if (aureliaTyping()) { aureliaKey(VK_BACK, false); return; }
    if (friendChatTyping()) { if (!friend_chat_.input.empty()) friendChatKey(VK_BACK, false); return; }
    if (libraryDialogOpen()) libraryDialogKey(VK_BACK, false);
    else if (identifyOpen()) identifyKey(VK_BACK, false);
    else if (customEditorOpen()) customEditorKey(VK_BACK, false);
    else if (metadataOpen()) desktopMetaKey(VK_BACK);
    else if (editingSecret() || editingArtworkSearch()) eraseSecret();
    else if (searching()) eraseSearchCharacter();
}

// "Gotowe" / Start: Enter for fields that confirm with it (settings keys, artwork and RAWG
// searches, collection names, the library filter). Home and store searches are live: their
// Enter would start the first result, so they only hide the keyboard.
void Launcher::keyboardDone() {
    closeKeyboard();
    if (aureliaTyping()) { aureliaSubmit(); return; }
    if (friendChatTyping()) { friendChatSend(); return; }
    if (metadataOpen()) { if (desktop_meta_.find.open) metaFindSearch(); return; } // Znajdź searches; a field keeps its text (Zapisz saves them all).
    if ((screen_ == ScreenMode::search || screen_ == ScreenMode::store) && !libraryDialogOpen() && !identifyOpen() && !customEditorOpen()) return;
    enterDown(false);
    enterUp();
}

// LB/RB: the insertion point of fields that have one (searches, the game editor).
void Launcher::keyboardCaret(int amount) {
    const auto step = [&](size_t& after, size_t size) {
        after = std::min(after, size);
        if (amount < 0 && after < size) ++after;
        else if (amount > 0 && after > 0) --after;
        else { navigationBoundary(); return; }
        audio_.navigate();
    };
    if (customEditorOpen() && custom_game_ui::field(custom_editor_.focus) >= 0) customEditorKey(amount < 0 ? VK_LEFT : VK_RIGHT, false);
    else if (metadataOpen() && desktop_meta_.find.open) { // Znajdź's title: the caret, not the pad's move between items.
        auto& find = desktop_meta_.find;
        if (find.focus == meta_find::item::query) { if (amount < 0 && find.caret) --find.caret; else if (amount > 0 && find.caret < find.query.size()) ++find.caret; }
    }
    else if (metadataOpen() && metaTextFocused()) desktopMetaKey(amount < 0 ? VK_LEFT : VK_RIGHT);
    else if (pluginPageTyping() || companionTyping()) navigationBoundary(); // Append-only.
    else if (libraryTyping()) step(library_controls_.after, library_controls_.query.size());
    else if (searching()) step(search_after_, search_query_.size());
    InvalidateRect(hwnd_, nullptr, FALSE);
}

size_t Launcher::keyboardPreviewCaret() const {
    if (aureliaTyping()) return aurelia_ui_.input.size();
    if (friendChatTyping()) return friend_chat_.input.size();
    if (customEditorOpen() && custom_game_ui::field(custom_editor_.focus) >= 0) return custom_editor_.caret;
    if (metadataOpen() && metaTextFocused()) return desktop_meta_.find.open ? desktop_meta_.find.caret : desktop_meta_.caret;
    if (!libraryDialogOpen() && !identifyOpen() && !customEditorOpen() && !metadataOpen() && !artworkOpen() && !editingSecret() && !pluginPageTyping() && !companionTyping()) {
        if (libraryTyping()) return library_controls_.caret();
        return searchCaret();
    }
    return keyboardPreview().size(); // Append-only fields.
}

// A search or a key/text setting reached with the pad brings the keyboard up by itself, once
// per entry: hidden with X it stays hidden until the field is left and entered again. Forms
// with several fields and prefilled searches (game editor, artwork, RAWG) wait for X.
void Launcher::autoKeyboard() {
    if (desktop_mode_) { text_field_shown_ = textFieldActive(); return; } // No on-screen keyboard in the desktop window.
    const bool field = textFieldActive();
    const bool entered = field && !text_field_shown_;
    text_field_shown_ = field;
    if (!entered || keyboard_.open || !pad_input_) return; // pad_input_ is only set with the launcher in front.
    if (customEditorOpen() || metadataOpen() || artworkOpen() || identifyOpen() || companionOpen()) return; // Her chat: A or X on the field.
    if (libraryDialogOpen() && library_dialog_.page != library_manager_ui::Page::name) return;
    osk::open(keyboard_);
    audio_.drawer();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::wstring Launcher::keyboardPreview() const {
    if (aureliaTyping()) return aurelia_ui_.input;
    if (friendChatTyping()) return friend_chat_.input;
    if (libraryDialogOpen()) return library_dialog_.input;
    if (identifyOpen()) return identify_state_.query;
    if (customEditorOpen()) {
        const int field = custom_game_ui::field(custom_editor_.focus);
        return field >= 0 ? custom_editor_.fields[static_cast<size_t>(field)] : std::wstring{};
    }
    if (metadataOpen() && desktop_meta_.find.open) return desktop_meta_.find.query;
    if (metadataOpen()) return desktop_meta_.focus < desktop_ui::meta_fields ? desktop_meta_.fields[static_cast<size_t>(desktop_meta_.focus)] : std::wstring{};
    if (artworkOpen()) return artwork_state_.query;
    if (editingSecret()) {
        // Keys and callbacks stay hidden, as in their field.
        if (settings_rows_[settings_state_.row()].control == Control::text) return settings_secret_input_;
        return std::wstring(std::min<size_t>(settings_secret_input_.size(), 48), L'•');
    }
    if (libraryTyping()) return library_controls_.query;
    if (pluginPageTyping()) return plugin_page_.query;
    if (companionTyping()) return companion_.chat.feed ? companion_.feed.view.input : companion_.chat.input;
    return search_query_;
}

bool Launcher::keyboardClick(float x, float y) {
    if (!keyboard_.open) return false;
    const auto view = renderer_.viewport();
    const osk::Layout layout{view.width, view.height};
    const auto [row, index] = layout.hit(keyboard_, x, y);
    if (row >= 0) {
        osk::focus(keyboard_, row, index);
        keyboardPress(osk::focused(keyboard_));
        return true;
    }
    return layout.panel().contains(x, y); // The panel itself takes the click.
}
