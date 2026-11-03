// Settings text and secret fields (API keys, addresses, sign-in codes and pasted store pages)
// and the artwork search field: select all, erase and paste. Characters arrive through onCharacter.
#include "launcher.hpp"

#include <algorithm>

using namespace settings_ui;

bool Launcher::editingSecret() const {
    return settingsOpen() && settings_state_.editing && !settings_rows_.empty() &&
           (settings_rows_[settings_state_.row()].control == Control::secret || settings_rows_[settings_state_.row()].control == Control::text);
}

void Launcher::selectSecret() {
    if (editingArtworkSearch()) { artwork_state_.query_selected = true; InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (!editingSecret()) return;
    settings_secret_selected_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::clearSecretInput() {
    SecureZeroMemory(settings_secret_input_.data(), settings_secret_input_.size() * sizeof(wchar_t));
    settings_secret_input_.clear();
    settings_secret_cancel_ = false; // A dialog opens (or is typed into) on Save.
    settings_secret_browse_ = false;
}

void Launcher::settingsTextRow(std::wstring current, const std::function<bool(const std::wstring&)>& commit, bool unquote) {
    if (settings_state_.editing) {
        auto value = settings_secret_input_;
        if (unquote) { // A pasted path or address may come in quotes.
            const auto first = value.find_first_not_of(L" \t\""), last = value.find_last_not_of(L" \t\"");
            value = first == std::wstring::npos ? std::wstring{} : value.substr(first, last - first + 1);
        }
        if (!commit(value)) return;
        clearSecretInput();
        settings_state_.editing = false;
        settingsChanged();
    } else {
        clearSecretInput();
        settings_secret_input_ = std::move(current);
        settings_secret_selected_ = true;
        settings_state_.editing = true;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::eraseSecret() {
    if (editingArtworkSearch()) {
        if (artwork_state_.query_selected) artwork_state_.query.clear(); else if (!artwork_state_.query.empty()) artwork_state_.query.pop_back();
        artwork_state_.query_selected = false; editArtworkQuery(); return;
    }
    if (!editingSecret()) return;
    if (settings_secret_selected_) clearSecretInput();
    else if (!settings_secret_input_.empty()) { settings_secret_input_.back() = L'\0'; settings_secret_input_.pop_back(); }
    settings_secret_selected_ = false;
    settingsSearchEdited();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::pasteSecret() {
    if ((!editingSecret() && !editingArtworkSearch()) || !OpenClipboard(hwnd_)) return;
    const auto handle = GetClipboardData(CF_UNICODETEXT);
    if (handle) {
        const auto* text = static_cast<const wchar_t*>(GlobalLock(handle));
        if (text) {
            // Pasted store pages (Battle.net game list) are far longer than keys or sign-in codes.
            const size_t limit = editingSecret() && settings_rows_[settings_state_.row()].id == Id::battlenet_callback ? 262144 :
                                 editingSecret() && settings_rows_[settings_state_.row()].id == Id::ea_callback ? 16384 : 8192;
            const size_t capacity = std::min<size_t>(GlobalSize(handle) / sizeof(wchar_t), limit);
            for (size_t i = 0; i < capacity && text[i]; ++i) onCharacter(text[i]);
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
}
