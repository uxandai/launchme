#pragma once
// Aurelia's management pages (src/app/aurelia_ui.cpp): the setup page and a game's pages (cloud,
// DLC, versions, Workshop, libraries). Tryb TV draws them as a standard dialog with this geometry
// (src/ui/aurelia_renderer.cpp); the desktop window as a desktop dialog that records its own
// hotspots. Friends and chat are not here: they live in the drawer (friend_chat_view.hpp).
#include "settings_view.hpp"
#include "../store/aurelia.hpp"

#include <algorithm>
#include <optional>

namespace aurelia_ui {
inline const wchar_t* notice() {
    return tr(L"Aurelia to osobny, opcjonalny program, który loguje się na Twoje konto Steam i pozwala Launchme zarządzać grami bez klienta Steam: instalować je i aktualizować, wybierać wersje i gałęzie, synchronizować zapisy w chmurze, obsługiwać DLC i Warsztat oraz czatować ze znajomymi.\n\nNajlepiej działa z grami single player / offline. Niektóre gry, zwłaszcza multiplayer i korzystające z DRM lub Steamworks, mogą nadal wymagać uruchomionego klienta Steam.");
}
enum class Page { setup, game, cloud, dlc, versions, branches, workshop, libraries };
enum class Action {
    none, toggle, cloud_toggle, default_launch, helper, repository, login, logout, reconnect, refresh, close,
    login_start, login_refresh, login_cancel, // The in-app sign-in (setup page): open it, a new QR code, leave it.
    cloud, dlc, versions, branches, workshop,
    sync, resolve_cloud, resolve_local, install, update, verify, cancel, pause, resume, forget, forget_files, relink, import_files, duplicates, uninstall, steam, auto_cloud,
    enable_dlc, disable_dlc, pin, unpin, downgrade, set_branch, move, choose_library, browse_library,
    workshop_install, workshop_subscribe, workshop_unsubscribe
};
struct Row {
    std::wstring title, description, value;
    Action action = Action::none;
    uint64_t id = 0;
    std::wstring payload;
    bool checked = false, toggle = false, danger = false, locked = false;
    bool page = false; // Opens a page of its own (a chevron).
};
inline bool opensPage(Action action) {
    return action == Action::cloud || action == Action::dlc || action == Action::versions || action == Action::branches || action == Action::workshop;
}
struct State {
    bool open = false, intro = false, busy = false, input_focus = false;
    bool enabled = false, active = false, installed = false, authenticated = false;
    Page page = Page::setup;
    Game game;
    std::wstring title, subtitle, status, input, input_hint, submit;
    std::vector<Row> rows;
    int focus = 0, first = 0, hover = -9;
    std::optional<Row> confirmation;
    std::optional<bool> pinned; // From the game page's `available`; unknown until it answers.
    float progress = -1;
    float notice_height = 0; // The setup page's note, measured while drawing (Tryb TV).
    int visible = 0;         // Rows the last drawn dialog shows; 0 before the first frame.
    bool confirm_focus = false; // Confirmation: Potwierdź (true) or Anuluj (false) has the focus.
    bool confirm_only = false;  // Opened for its question alone (Odinstaluj from the game's menu): the answer closes it.
    // The sign-in page (src/app/aurelia_ui.cpp): Steam's QR code for the Steam Mobile app and, in the
    // same page, the field for the account's name and then its password (dots); a Steam Guard code
    // when Steam asks for one; waiting while the password's sign-in runs. The QR challenge comes as
    // rows of modules (1: dark), scanned or not.
    enum class Login { none, form, guard, waiting };
    Login login = Login::none;
    std::wstring login_account; // Typed: the field then takes the password.
    bool guard_email = false, qr_scanned = false, qr_failed = false, input_secret = false;
    std::vector<uint8_t> qr;
    int qr_size = 0;
    bool back_focus = false; // Back (Tryb TV and the desktop's inner pages) or ✕ has the focus.
};

// Tryb TV: a panel in the middle of the 1600 × 900 canvas, Back and the title at its top, the rows,
// and at the bottom the status line and an optional field. Hit codes: -1 Back, -2 the field,
// -3 its button, -4 Anuluj, -5 Potwierdź, a row's index, -9 nothing.
struct Layout {
    float width = 1600, height = 900;
    static constexpr float pad = 32, row_height = 66, row_step = 76, header = 112, status_space = 40;
    settings_ui::Rect panel() const {
        const float w = std::min(960.0f, width - 64), h = std::min(780.0f, height - 48);
        return {(width - w) / 2, (height - h) / 2, (width + w) / 2, (height + h) / 2};
    }
    settings_ui::Rect back() const { const auto p = panel(); return {p.left + pad, p.top + pad, p.left + pad + 48, p.top + pad + 48}; }
    // The sign-in's QR code (on white, its quiet zone inside) under the note, centred; the rows below it.
    static constexpr float qr_side = 236, qr_block = qr_side + 44;
    float noteBottom(const State& s) const {
        const auto p = panel();
        const bool note = s.page == Page::setup && !s.confirmation && s.login == State::Login::none && s.notice_height > 0;
        return p.top + header + (note ? s.notice_height + 20 : 0.0f);
    }
    bool showsQr(const State& s) const { return s.page == Page::setup && !s.confirmation && s.login == State::Login::form && !s.qr.empty() && s.qr_size > 0; }
    settings_ui::Rect qr(const State& s) const {
        const auto p = panel(); const float top = noteBottom(s), cx = (p.left + p.right) * 0.5f;
        return {cx - qr_side * 0.5f, top, cx + qr_side * 0.5f, top + qr_side};
    }
    float rowsTop(const State& s) const { return noteBottom(s) + (showsQr(s) ? qr_block : 0.0f); }
    bool field(const State& s) const { return !s.input_hint.empty() && !s.confirmation; }
    settings_ui::Rect input() const { const auto p = panel(); return {p.left + pad, p.bottom - pad - 56, p.right - pad - 192, p.bottom - pad}; }
    settings_ui::Rect send() const { const auto p = panel(); return {p.right - pad - 180, p.bottom - pad - 56, p.right - pad, p.bottom - pad}; }
    settings_ui::Rect confirm() const { const auto p = panel(); return {p.right - pad - 200, p.bottom - pad - 56, p.right - pad, p.bottom - pad}; }
    settings_ui::Rect cancel() const { const auto c = confirm(); return {c.left - 14 - 200, c.top, c.left - 14, c.bottom}; }
    // The status line sits just above the field (or the panel's bottom padding).
    float statusY(const State& s) const { return (field(s) ? input().top - 14 : panel().bottom - pad) - status_space * 0.5f; }
    float rowsBottom(const State& s) const { return (field(s) ? input().top - 14 : panel().bottom - pad) - status_space; }
    int visible(const State& s) const {
        return std::max(1, static_cast<int>((rowsBottom(s) - rowsTop(s) + row_step - row_height) / row_step));
    }
    settings_ui::Rect row(const State& s, int index) const {
        const auto p = panel(); const float y = rowsTop(s) + (index - s.first) * row_step;
        return {p.left + pad, y, p.right - pad, y + row_height};
    }
    int hit(const State& s, float x, float y) const {
        if (back().contains(x, y)) return -1;
        if (s.confirmation) {
            if (cancel().contains(x, y)) return -4;
            if (confirm().contains(x, y)) return -5;
            return -9;
        }
        if (field(s)) {
            if (input().contains(x, y)) return -2;
            if (send().contains(x, y)) return -3;
        }
        for (int i = s.first; i < std::min(static_cast<int>(s.rows.size()), s.first + visible(s)); ++i)
            if (row(s, i).contains(x, y)) return i;
        return -9;
    }
};
}
