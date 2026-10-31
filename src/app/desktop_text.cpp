// Text selection in the desktop window's fields (Edytuj metadane, Ctrl+K, the library's search,
// Dodaj/Edytuj grę): Shift with the arrows, Home and End; Ctrl with the arrows by words; Ctrl+A, C, X,
// V; typing, Backspace and Delete replace a selection; a click places the caret where it falls, a
// drag selects, a double click takes the word, Shift+click extends.
#include "launcher.hpp"

#include <algorithm>
#include <cwctype>

namespace {
constexpr size_t none = std::wstring::npos;
bool wordChar(wchar_t c) { return std::iswalnum(c) || c == L'_'; }
size_t wordLeft(const std::wstring& text, size_t at) {
    while (at > 0 && !wordChar(text[at - 1])) --at;
    while (at > 0 && wordChar(text[at - 1])) --at;
    return at;
}
size_t wordRight(const std::wstring& text, size_t at) {
    while (at < text.size() && !wordChar(text[at])) ++at;
    while (at < text.size() && wordChar(text[at])) ++at;
    return at;
}
} // namespace

// The focused field, wherever it is; another field than last time starts without a selection.
Launcher::DesktopText Launcher::desktopText() {
    DesktopText target;
    if (!desktop_mode_) return target;
    if (desktop_meta_.open) {
        if (desktop_meta_.focus >= 0 && desktop_meta_.focus < desktop_ui::meta_fields) {
            static constexpr size_t limits[desktop_ui::meta_fields] = {120, 4000, 160, 160, 40, 200, 16, 20};
            const auto index = static_cast<size_t>(desktop_meta_.focus);
            target = {&desktop_meta_.fields[index], &desktop_meta_.caret, &desktop_meta_.selected, limits[index], index == 1, 1};
        }
    } else if (desktop_quick_.open) target = {&desktop_quick_.query, &desktop_quick_.caret, nullptr, 80, false, 2};
    else if (topModal() == Modal::custom_editor) {
        if (const int field = custom_game_ui::field(custom_editor_.focus); field >= 0 && field != custom_game_ui::platform_field)
            target = {&custom_editor_.fields[static_cast<size_t>(field)], &custom_editor_.caret, &custom_editor_.selected, 1024, false, 3};
    } else if (topModal() == Modal::none && desktopActive() && desktop_search_focus_)
        target = {&desktop_query_, &desktop_caret_, nullptr, 70, false, 4};
    if (target.text != desktop_text_field_) { desktop_text_field_ = target.text; desktop_text_anchor_ = none; desktop_text_drag_ = false; }
    if (target.text) {
        *target.caret = std::min(*target.caret, target.text->size());
        if (desktop_text_anchor_ != none) desktop_text_anchor_ = std::min(desktop_text_anchor_, target.text->size());
    }
    return target;
}

std::pair<size_t, size_t> Launcher::desktopTextSelection() const {
    if (!desktop_text_field_) return {0, 0};
    const auto& text = *desktop_text_field_;
    // The caret of the field the selection belongs to (desktopText keeps them paired).
    size_t caret = 0;
    bool all = false;
    if (desktop_meta_.open) { caret = desktop_meta_.caret; all = desktop_meta_.selected; }
    else if (desktop_quick_.open) caret = desktop_quick_.caret;
    else if (desktop_text_field_ == &desktop_query_) caret = desktop_caret_;
    else { caret = custom_editor_.caret; all = custom_editor_.selected; }
    if (all) return {0, text.size()};
    if (desktop_text_anchor_ == none) return {0, 0};
    caret = std::min(caret, text.size());
    const size_t anchor = std::min(desktop_text_anchor_, text.size());
    return {std::min(caret, anchor), std::max(caret, anchor)};
}

void Launcher::desktopTextChanged(int owner) {
    switch (owner) {
    case 2: desktop_quick_.selected = 0; desktopQuickUpdate(); break;
    case 3: custom_editor_.error.clear(); custom_editor_.confirm_remove = false; break;
    case 4: desktop_query_dirty_ = true; desktop_query_edited_ = std::chrono::steady_clock::now(); break;
    default: break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::desktopTextKey(WPARAM key) {
    const auto target = desktopText();
    if (!target.text) return false;
    auto& text = *target.text;
    auto& caret = *target.caret;
    auto& anchor = desktop_text_anchor_;
    const bool control = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    if (target.all && *target.all) { anchor = 0; caret = text.size(); *target.all = false; } // "All" becomes a range.
    const size_t begin = anchor == none ? caret : std::min(caret, anchor), end = anchor == none ? caret : std::max(caret, anchor);
    const bool selection = begin < end;
    const auto erase = [&] { text.erase(begin, end - begin); caret = begin; anchor = none; };
    const auto move = [&](size_t to) {
        if (shift) { if (anchor == none) anchor = caret; }
        else anchor = none;
        caret = to;
        if (anchor == caret) anchor = none;
    };
    const auto copy = [&](const std::wstring& value) {
        if (value.empty() || !OpenClipboard(hwnd_)) return;
        EmptyClipboard();
        if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t))) {
            if (auto* data = static_cast<wchar_t*>(GlobalLock(memory))) {
                std::copy(value.c_str(), value.c_str() + value.size() + 1, data);
                GlobalUnlock(memory);
                if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
            } else GlobalFree(memory);
        }
        CloseClipboard();
    };
    switch (key) {
    case VK_LEFT:
        if (!shift && !control && selection) { caret = begin; anchor = none; break; }
        move(control ? wordLeft(text, caret) : caret ? caret - 1 : 0);
        break;
    case VK_RIGHT:
        if (!shift && !control && selection) { caret = end; anchor = none; break; }
        move(control ? wordRight(text, caret) : std::min(caret + 1, text.size()));
        break;
    case VK_HOME: move(0); break;
    case VK_END: move(text.size()); break;
    case 'A':
        if (!control) return false;
        anchor = text.empty() ? none : 0;
        caret = text.size();
        break;
    case 'C': case 'X':
        if (!control) return false;
        if (selection) {
            copy(text.substr(begin, end - begin));
            if (key == 'X') { erase(); desktopTextChanged(target.owner); }
        }
        break;
    case 'V': {
        if (!control) return false;
        std::wstring pasted;
        if (OpenClipboard(hwnd_)) {
            if (const auto handle = GetClipboardData(CF_UNICODETEXT))
                if (const auto* data = static_cast<const wchar_t*>(GlobalLock(handle))) { pasted = data; GlobalUnlock(handle); }
            CloseClipboard();
        }
        std::wstring clean;
        for (const wchar_t c : pasted) {
            if (c == L'\r') continue;
            if (c == L'\n' || c == L'\t') clean += target.multiline && c == L'\n' ? L'\n' : L' ';
            else if (c >= L' ' && c != 127) clean += c;
        }
        if (clean.empty()) break;
        if (selection) erase();
        const size_t room = target.limit > text.size() ? target.limit - text.size() : 0;
        if (clean.size() > room) clean.resize(room);
        text.insert(caret, clean);
        caret += clean.size();
        anchor = none;
        desktopTextChanged(target.owner);
        break;
    }
    case VK_BACK:
        if (selection) { erase(); desktopTextChanged(target.owner); break; }
        if (control && caret) {
            const size_t from = wordLeft(text, caret);
            text.erase(from, caret - from);
            caret = from;
            desktopTextChanged(target.owner);
            break;
        }
        anchor = none;
        return false; // One character: the field's own handling.
    case VK_DELETE:
        if (selection) { erase(); desktopTextChanged(target.owner); break; }
        anchor = none;
        return false;
    default: return false;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

// A typed character replaces the selection; the field's own code then inserts it at the caret.
void Launcher::desktopTextBeforeTyping() {
    const auto target = desktopText();
    if (!target.text) return;
    auto& text = *target.text;
    auto& caret = *target.caret;
    if (target.all && *target.all) return; // The field's own code clears a whole-field selection.
    if (desktop_text_anchor_ == none) return;
    const size_t begin = std::min(caret, desktop_text_anchor_), end = std::max(caret, desktop_text_anchor_);
    desktop_text_anchor_ = none;
    if (begin >= end) return;
    text.erase(begin, end - begin);
    caret = begin;
}

// A press in the focused field at x (DIPs; `left` is where its text starts): the caret where it falls,
// the word on a double click, a range with Shift; holding the button selects while dragging.
void Launcher::desktopTextPress(float left, float x, float scale, bool double_click, bool title_font) {
    const auto target = desktopText();
    if (!target.text) return;
    auto& text = *target.text;
    auto& caret = *target.caret;
    if (target.all) *target.all = false;
    const size_t at = renderer_.desktopCaretAt(text, x - left, title_font);
    if (double_click) {
        size_t begin = at, end = at;
        while (begin > 0 && wordChar(text[begin - 1])) --begin;
        while (end < text.size() && wordChar(text[end])) ++end;
        desktop_text_anchor_ = begin < end ? begin : none;
        caret = end;
    } else if (GetKeyState(VK_SHIFT) < 0) {
        if (desktop_text_anchor_ == none) desktop_text_anchor_ = caret;
        caret = at;
    } else {
        caret = at;
        desktop_text_anchor_ = at;
        desktop_text_drag_ = true;
        desktop_text_drag_left_ = left;
        desktop_text_drag_scale_ = scale;
        desktop_text_drag_title_ = title_font;
        SetCapture(hwnd_);
    }
    if (desktop_text_anchor_ == caret) desktop_text_anchor_ = double_click ? none : desktop_text_anchor_;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

bool Launcher::desktopTextDrag(float mouse_x) {
    if (!desktop_text_drag_) return false;
    const auto target = desktopText();
    if (!target.text || !desktop_text_drag_) { desktop_text_drag_ = false; return false; }
    const size_t at = renderer_.desktopCaretAt(*target.text, mouse_x / desktop_text_drag_scale_ - desktop_text_drag_left_, desktop_text_drag_title_);
    if (at != *target.caret) { *target.caret = at; InvalidateRect(hwnd_, nullptr, FALSE); }
    return true;
}

void Launcher::desktopTextRelease() {
    if (!desktop_text_drag_) return;
    desktop_text_drag_ = false;
    if (GetCapture() == hwnd_) ReleaseCapture();
    const auto target = desktopText();
    if (target.text && desktop_text_anchor_ == *target.caret) desktop_text_anchor_ = none; // A click, not a drag.
}
