// Aurelia's pages (src/app/aurelia_ui.cpp, src/ui/aurelia_view.hpp) in Launchme's own look: Tryb TV
// as its other dialogs (Opcje uruchamiania: the panel, Back and the heading, cards for the rows, one white
// ring that springs between the items, the accent only on switches), the desktop window as its desktop
// dialogs (desktop_dialogs_renderer.cpp).
#include "renderer.hpp"
#include "renderer_internal.hpp"

#include <algorithm>
#include <cmath>

using Kind = desktop_ui::SheetHit::Kind;

namespace {
using palette::gray;
using settings_ui::Rect;
using render_detail::box;
float middle(Rect r) { return (r.top + r.bottom) * 0.5f; }
}

void Renderer::drawAurelia() {
    if (!context_ || !context_->aurelia || !context_->aurelia->open) return;
    if (desktopDialogs()) { drawDesktopAurelia(); return; }
    auto& s = *context_->aurelia;
    const aurelia_ui::Layout layout{viewport_.width, viewport_.height};
    const auto p = layout.panel();
    const auto accent = uiAccent();
    // As Tryb TV's other dialogs (Opcje uruchamiania, Edytuj metadane): the dim, the panel and their entrance
    // and exit (ViewContext::aurelia_motion), cards for the rows, glass buttons and one white ring that springs
    // to the focused item (ViewContext::aurelia_glide).
    dialogScrim(gray(0, 0.80f));
    const DialogMotion motion(*this, p);
    const auto shape = D2D1::RoundedRect(box(p), 22, 22);
    brush(gray(0.07f)); target_->FillRoundedRectangle(shape, solid_.Get());
    if (panel_borders_) { brush(gray(1, 0.16f)); target_->DrawRoundedRectangle(shape, solid_.Get()); }
    std::optional<Rect> ring;
    float ring_radius = 0;
    const auto focusOn = [&](Rect r, float radius) { ring = r; ring_radius = radius; };
    const auto drawRing = [&] { if (ring) tvFocusRing(*ring, ring_radius, context_->aurelia_glide); };
    // Back (a glass circle with a dot) and the heading, the game beneath.
    const auto back = layout.back();
    brush(drawButton(box(back), 0, 1, false, s.hover == -1));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((back.left + back.right) * 0.5f, middle(back)), 3, 3), solid_.Get());
    if (s.back_focus && !s.confirmation) focusOn(back, (back.bottom - back.top) * 0.5f);
    const float title_left = back.right + 20;
    textGroup(s.confirmation ? s.confirmation->title : s.title, s.confirmation ? std::wstring{} : s.subtitle, title_left, middle(back),
              p.right - layout.pad - title_left, settings_heading_font_.Get(), gray(0.96f), settings_body_font_.Get(), gray(0.58f), 1, false,
              heading_group_gap);
    if (s.busy || s.progress >= 0) {
        // Working: a thin bar under the header, its share when known, else a gliding segment.
        const float left = p.left + layout.pad, right = p.right - layout.pad, y = p.top + layout.header - 18;
        brush(gray(1, 0.08f)); target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, y, right, y + 3), 1.5f, 1.5f), solid_.Get());
        float from = left, to = left + (right - left) * std::clamp(s.progress, 0.0f, 1.0f);
        if (s.progress < 0) { const float segment = (right - left) * 0.18f; from = left + (right - left - segment) * (0.5f + 0.5f * std::sin(elapsed_ * 2.4f)); to = from + segment; }
        brush(accent); target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(from, y, to, y + 3), 1.5f, 1.5f), solid_.Get());
    }
    const auto button = [&](Rect r, const std::wstring& label, bool hover, bool enabled) {
        textLine(label, r.left + 12, middle(r), r.right - r.left - 24, settings_body_font_.Get(), drawButton(box(r), 0, 1, false, hover && enabled, enabled),
                 DWRITE_TEXT_ALIGNMENT_CENTER);
    };
    if (s.confirmation) {
        // The question's consequence, then Anuluj / Potwierdź (the safe answer first).
        if (!s.confirmation->description.empty())
            paragraph(s.confirmation->description, p.left + layout.pad, p.top + layout.header, p.right - p.left - 2 * layout.pad, settings_body_font_.Get(), gray(0.78f), 4);
        const auto cancel = layout.cancel(), yes = layout.confirm();
        button(cancel, tr(L"Anuluj"), s.hover == -4, true);
        button(yes, tr(L"Potwierdź"), s.hover == -5, true);
        const auto chosen = s.confirm_focus ? yes : cancel;
        focusOn(chosen, (chosen.bottom - chosen.top) * 0.5f);
        s.visible = layout.visible(s);
        drawRing();
        return;
    }
    // Setup: a plain note under the header, measured, so the rows start below its last line.
    if (s.page == aurelia_ui::Page::setup && s.login == aurelia_ui::State::Login::none) {
        const float width = p.right - p.left - 2 * layout.pad;
        s.notice_height = paragraph(aurelia_ui::notice(), p.left + layout.pad, p.top + layout.header, width, settings_body_font_.Get(), gray(0.58f), 4);
    } else s.notice_height = 0;
    if (layout.showsQr(s)) {
        // The sign-in's QR code, then what to do with it.
        const auto code = layout.qr(s);
        drawQrCode(code, s.qr, s.qr_size);
        textLine(s.qr_scanned ? tr(L"Zatwierdź logowanie w aplikacji Steam Mobile") : tr(L"Zeskanuj kod aplikacją Steam Mobile"), p.left + layout.pad, code.bottom + 16,
                 p.right - p.left - 2 * layout.pad, settings_body_font_.Get(), gray(0.86f), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    s.visible = layout.visible(s);
    if (s.focus < s.first) s.first = s.focus;
    if (s.focus >= s.first + s.visible) s.first = s.focus - s.visible + 1;
    s.first = std::clamp(s.first, 0, std::max(0, static_cast<int>(s.rows.size()) - s.visible));
    const auto toggle = [&](float right, float y, bool on, bool locked) {
        const auto off = gray(locked ? 0.15f : 0.23f);
        brush(on ? (locked ? D2D1::ColorF(accent.r, accent.g, accent.b, 0.45f) : accent) : off);
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(right - 52, y - 14, right, y + 14), 14, 14), solid_.Get());
        brush(gray(on ? 0.08f : locked ? 0.36f : 0.67f));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(on ? right - 14 : right - 38, y), 10, 10), solid_.Get());
        textLine(on ? tr(L"Włączone") : tr(L"Wyłączone"), right - 66 - 148, y, 148, settings_body_font_.Get(), palette::stateLabel(locked), DWRITE_TEXT_ALIGNMENT_TRAILING);
    };
    const int last = std::min(static_cast<int>(s.rows.size()), s.first + s.visible);
    for (int i = s.first; i < last; ++i) {
        const auto& row = s.rows[static_cast<size_t>(i)];
        const auto r = layout.row(s, i);
        const bool focused = !s.input_focus && !s.back_focus && s.focus == i, hover = s.hover == i;
        // Rows as the launch options' cards: a faint card, a little lighter under the pointer; the focus is the ring.
        brush(gray(1, hover ? 0.065f : 0.04f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), 16, 16), solid_.Get());
        if (focused) focusOn(r, 16);
        const float y = middle(r), left = r.left + 24;
        float right = r.right - 24;
        if (row.toggle) { toggle(right, y, row.checked, row.locked); right -= 66 + 148 + 12; }
        else if (row.page) {
            const auto ink = focused ? gray(0.95f) : gray(0.6f);
            line(right - 10, y - 6, right - 4, y, ink, 1.8f); line(right - 4, y, right - 10, y + 6, ink, 1.8f);
            right -= 28;
        }
        if (!row.value.empty() && !row.toggle) {
            const float w = std::min(320.0f, std::ceil(textAdvance(row.value, settings_body_font_.Get())) + 4);
            textLine(row.value, right - w, y, w, settings_body_font_.Get(), gray(0.62f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            right -= w + 20;
        }
        const auto ink = row.locked ? gray(0.45f) : row.danger ? palette::danger() : gray(0.95f);
        textGroup(row.title, row.description, left, y, right - left, menu_font_.Get(), ink, settings_body_font_.Get(), gray(0.58f), 1);
    }
    if (s.rows.empty() && !s.busy)
        textLine(tr(L"Brak pozycji"), p.left + layout.pad + 4, layout.rowsTop(s) + 24, p.right - p.left - 2 * layout.pad, settings_body_font_.Get(), gray(0.58f));
    if (static_cast<int>(s.rows.size()) > s.visible) {
        const float top = layout.rowsTop(s), track = s.visible * layout.row_step - (layout.row_step - layout.row_height);
        const float thumb = std::max(28.0f, track * s.visible / static_cast<float>(s.rows.size()));
        const float y = top + (track - thumb) * s.first / std::max(1, static_cast<int>(s.rows.size()) - s.visible);
        brush(gray(1, 0.25f)); target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(p.right - 18, y, p.right - 15, y + thumb), 1.5f, 1.5f), solid_.Get());
    }
    if (!s.status.empty()) textLine(s.status, p.left + layout.pad + 4, layout.statusY(s), p.right - p.left - 2 * layout.pad, settings_body_font_.Get(), gray(0.58f));
    if (layout.field(s)) {
        // The field as the launch options' wells (the ring while it types) and its button.
        const auto f = layout.input();
        const bool typing = s.input_focus;
        brush(gray(typing ? 0.045f : 0.025f + (s.hover == -2 ? 0.02f : 0.0f))); target_->FillRoundedRectangle(pill(box(f)), solid_.Get());
        if (typing) focusOn(f, (f.bottom - f.top) * 0.5f);
        const float text_left = f.left + 26, room = f.right - f.left - 52;
        // The password shows as dots.
        const auto value = s.input_secret ? std::wstring(s.input.size(), L'•') : s.input;
        const auto shown = value.size() > 90 ? L"…" + value.substr(value.size() - 89) : value;
        if (shown.empty()) textLine(s.input_hint, text_left, middle(f), room, menu_font_.Get(), gray(0.42f));
        else textLine(shown, text_left, middle(f), room, menu_font_.Get(), gray(0.95f));
        if (typing && std::sin(elapsed_ * 4.0f) >= 0)
            textLine(L"│", text_left + std::min(room - 6, shown.empty() ? 0.0f : textAdvance(shown, menu_font_.Get())) - 2, middle(f), 20, menu_font_.Get(), gray(0.95f));
        button(layout.send(), s.submit, s.hover == -3, !s.input.empty() && !s.busy);
    }
    drawRing();
}

// The desktop window: a desktop dialog (its frame, ✕ and Back on inner pages), rows as the
// Kolekcje dialog's, switches, the field and buttons as the game editor's.
void Renderer::drawDesktopAurelia() {
    auto& s = *context_->aurelia;
    const auto& hover = context_->desktop_dialog_hover;
    const auto view = desktopViewport();
    D2D1_MATRIX_3X2_F saved{};
    target_->GetTransform(&saved);
    target_->SetTransform(D2D1::Matrix3x2F::Scale(view.scale, view.scale));
    using desktop_ui::SheetHit;
    using desktop_ui::sheet_header;
    const float width = 600, line_h = 50, field_h = s.input_hint.empty() || s.confirmation ? 0.0f : 56.0f;
    const bool inner = s.page != aurelia_ui::Page::setup && s.page != aurelia_ui::Page::game;
    // The note (setup) and the game's name, measured; then as many rows as the window allows.
    const float text_width = width - 44;
    const float subtitle = s.subtitle.empty() || s.confirmation ? 0.0f : 24.0f;
    const float note = s.page == aurelia_ui::Page::setup && !s.confirmation && s.login == aurelia_ui::State::Login::none ?
        paragraph(aurelia_ui::notice(), 0, 0, text_width, desktop_small_font_.Get(), palette::muted(), 9, false) + 14 : 0.0f;
    const float status = s.status.empty() ? 0.0f : 28.0f;
    // The sign-in's QR code under the note.
    const bool qr = s.page == aurelia_ui::Page::setup && !s.confirmation && s.login == aurelia_ui::State::Login::form && !s.qr.empty() && s.qr_size > 0;
    constexpr float qr_side = 196;
    const float qr_h = qr ? qr_side + 40 : 0.0f;
    const float chrome = sheet_header + 12 + subtitle + note + qr_h + field_h + status + 14;
    const float most = std::min(640.0f, view.height - 80);
    int capacity = std::max(2, static_cast<int>((most - chrome) / line_h));
    if (s.confirmation) capacity = 0;
    const int count = static_cast<int>(s.rows.size());
    const int shown = s.confirmation ? 0 : std::clamp(count, 1, capacity);
    const float confirm_h = s.confirmation ? (s.confirmation->description.empty() ? 0.0f :
        paragraph(s.confirmation->description, 0, 0, text_width, desktop_font_.Get(), palette::muted(), 4, false) + 20) + 72 : 0.0f;
    const float height = s.confirmation ? sheet_header + 20 + confirm_h : chrome + static_cast<float>(shown) * line_h;
    const auto p = desktopDialogFrame(view, width, height, s.confirmation ? s.confirmation->title : s.title, inner && !s.confirmation);
    float y = p.top + sheet_header + 12;
    // Back on inner pages, else ✕, with the keyboard's or the pad's focus (Up from the first row).
    if (s.back_focus && !s.confirmation)
        desktopFocusRing(inner ? Rect{p.left + 12, p.top + 10, p.left + 48, p.top + 46} : Rect{p.right - 48, p.top + 10, p.right - 12, p.top + 46}, 8);
    if (s.busy || s.progress >= 0) {
        const float left = p.left + 1, right = p.right - 1, top = p.top + sheet_header - 1;
        float from = left, to = left + (right - left) * std::clamp(s.progress, 0.0f, 1.0f);
        if (s.progress < 0) { const float segment = (right - left) * 0.2f; from = left + (right - left - segment) * (0.5f + 0.5f * std::sin(elapsed_ * 2.4f)); to = from + segment; }
        desktopFill({from, top, to, top + 2}, 1, uiAccent());
    }
    if (s.confirmation) {
        if (!s.confirmation->description.empty())
            y += paragraph(s.confirmation->description, p.left + 22, y + 4, text_width, desktop_font_.Get(), palette::muted(), 4) + 20;
        const float by = p.bottom - 20 - 36;
        const Rect yes{p.right - 22 - 130, by, p.right - 22, by + 36}, no{yes.left - 10 - 110, by, yes.left - 10, by + 36};
        desktopButton(no, 1, hover == SheetHit{Kind::action, 1} ? 1.0f : 0.0f, false, true, 0, tr(L"Anuluj"));
        desktopButton(yes, 2, hover == SheetHit{Kind::action, 2} ? 1.0f : 0.0f, false, true, 0, tr(L"Potwierdź"));
        desktopFocusRing(s.confirm_focus ? yes : no, 8);
        desktopModalHot({Kind::action, 1}, no);
        desktopModalHot({Kind::action, 2}, yes);
        s.visible = 1;
        desktop_modal_ = desktop_ui::DialogKind::aurelia;
        target_->SetTransform(saved);
        return;
    }
    if (subtitle > 0) { textLine(s.subtitle, p.left + 22, y + 8, p.right - p.left - 44, desktop_small_font_.Get(), palette::muted()); y += subtitle; }
    if (note > 0) { paragraph(aurelia_ui::notice(), p.left + 22, y + 2, text_width, desktop_small_font_.Get(), palette::muted(), 9); y += note; }
    if (qr) {
        const float cx = (p.left + p.right) * 0.5f;
        drawQrCode({cx - qr_side * 0.5f, y + 2, cx + qr_side * 0.5f, y + 2 + qr_side}, s.qr, s.qr_size);
        textLine(s.qr_scanned ? tr(L"Zatwierdź logowanie w aplikacji Steam Mobile") : tr(L"Zeskanuj kod aplikacją Steam Mobile"), p.left + 22, y + qr_side + 20,
                 p.right - p.left - 44, desktop_font_.Get(), palette::text(), DWRITE_TEXT_ALIGNMENT_CENTER);
        y += qr_h;
    }
    s.visible = capacity;
    if (s.focus < s.first) s.first = s.focus;
    if (s.focus >= s.first + capacity) s.first = s.focus - capacity + 1;
    s.first = std::clamp(s.first, 0, std::max(0, count - capacity));
    for (int i = s.first; i < std::min(count, s.first + capacity); ++i) {
        const auto& row = s.rows[static_cast<size_t>(i)];
        const float top = y + static_cast<float>(i - s.first) * line_h;
        const Rect r{p.left + 12, top, p.right - 12, top + line_h - 4};
        const bool focused = !s.input_focus && !s.back_focus && s.focus == i, over = hover == SheetHit{Kind::row, i};
        if (focused || over) desktopFill(r, 8, gray(1, focused ? 0.075f : 0.045f));
        float right = r.right - 12;
        if (row.toggle) {
            desktopSwitch({right - 40, middle(r) - 11, right, middle(r) + 11}, row.checked ? 1.0f : 0.0f, row.locked, over ? 1.0f : 0.0f);
            right -= 52;
        } else if (row.page) { desktopGlyph(desktop_ui::glyph::chevron_right, right - 8, middle(r), 10, gray(0.6f)); right -= 26; }
        if (!row.value.empty() && !row.toggle) {
            const float w = std::min(240.0f, std::ceil(textAdvance(row.value, desktop_small_font_.Get())) + 4);
            textLine(row.value, right - w, middle(r), w, desktop_small_font_.Get(), palette::muted(), DWRITE_TEXT_ALIGNMENT_TRAILING);
            right -= w + 14;
        }
        const auto ink = row.locked ? gray(0.45f) : row.danger ? palette::danger() : palette::text();
        if (row.description.empty()) textLine(row.title, r.left + 12, middle(r), right - r.left - 12, desktop_font_.Get(), ink);
        else {
            textLine(row.title, r.left + 12, r.top + 16, right - r.left - 12, desktop_font_.Get(), ink);
            textLine(row.description, r.left + 12, r.top + 34, right - r.left - 12, desktop_small_font_.Get(), palette::muted());
        }
        if (!row.locked && row.action != aurelia_ui::Action::none) desktopModalHot({Kind::row, i}, r);
    }
    if (count > capacity) {
        const float track = static_cast<float>(capacity) * line_h - 8, length = std::max(24.0f, track * static_cast<float>(capacity) / static_cast<float>(count));
        const float top = y + (track - length) * static_cast<float>(s.first) / static_cast<float>(count - capacity);
        desktopFill({p.right - 7, top, p.right - 4, top + length}, 1.5f, gray(1, 0.22f));
    }
    if (!count && !s.busy) textLine(tr(L"Brak pozycji"), p.left + 24, y + 22, p.right - p.left - 48, desktop_font_.Get(), palette::muted());
    y += static_cast<float>(shown) * line_h;
    if (status > 0) textLine(s.status, p.left + 22, y + 12, p.right - p.left - 44, desktop_small_font_.Get(), palette::muted());
    if (field_h > 0) {
        const float fy = p.bottom - 20 - 40;
        const Rect f{p.left + 20, fy, p.right - 20 - 130, fy + 40}, b{p.right - 20 - 120, fy, p.right - 20, fy + 40};
        const auto value = s.input_secret ? std::wstring(s.input.size(), L'\u2022') : s.input; // The password shows as dots.
        desktopTextField(f, value, s.input_hint, value.size(), s.input_focus, false, hover.kind == Kind::field ? 1.0f : 0.0f);
        desktopModalHot({Kind::field}, f);
        const bool ready = !s.input.empty() && !s.busy;
        desktopButton(b, 2, hover == SheetHit{Kind::action, 0} ? 1.0f : 0.0f, false, ready, 0, s.submit);
        if (ready) desktopModalHot({Kind::action, 0}, b);
    }
    desktop_modal_ = desktop_ui::DialogKind::aurelia;
    target_->SetTransform(saved);
}

// A QR code: dark modules on a white square whose margin is its quiet zone; whole pixels per module.
void Renderer::drawQrCode(settings_ui::Rect r, const std::vector<uint8_t>& modules, int size) {
    if (size <= 0 || modules.size() != static_cast<size_t>(size) * static_cast<size_t>(size)) return;
    brush(D2D1::ColorF(1, 1, 1));
    target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), 8, 8), solid_.Get());
    const float cell = std::max(1.0f, std::floor((r.right - r.left) / static_cast<float>(size + 6)));
    const float left = std::round(r.left + ((r.right - r.left) - cell * static_cast<float>(size)) * 0.5f);
    const float top = std::round(r.top + ((r.bottom - r.top) - cell * static_cast<float>(size)) * 0.5f);
    brush(D2D1::ColorF(0, 0, 0));
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size;) {
            if (!modules[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)]) { ++x; continue; }
            int end = x;
            while (end < size && modules[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(end)]) ++end; // One rectangle per run.
            target_->FillRectangle(D2D1::RectF(left + cell * static_cast<float>(x), top + cell * static_cast<float>(y),
                                                left + cell * static_cast<float>(end), top + cell * static_cast<float>(y + 1)), solid_.Get());
            x = end;
        }
}

void Renderer::drawCloudGlyph(float cx, float cy, D2D1_COLOR_F ink, std::optional<aurelia::CloudState> state) {
    using S = aurelia::CloudState;
    // The cloud: three round tops on a flat base, filled.
    brush(ink);
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 1.5f, cy - 2.5f), 7.0f, 7.0f), solid_.Get());
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - 8.0f, cy + 1.5f), 4.8f, 4.8f), solid_.Get());
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 7.0f, cy + 1.0f), 5.4f, 5.4f), solid_.Get());
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - 8.0f, cy + 0.5f, cx + 7.5f, cy + 6.4f), 2.5f, 2.5f), solid_.Get());
    if (!state) return;
    const bool synced = *state == S::synced, problem = *state == S::conflicts || *state == S::incomplete || *state == S::failed;
    const bool moving = *state == S::syncing || *state == S::checking;
    if (!synced && !problem && !moving) return;
    // The badge on its lower right: a tick, an exclamation mark, or two dots while it works.
    const float bx = cx + 9.5f, by = cy + 6.5f;
    brush(gray(0.07f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bx, by), 7.2f, 7.2f), solid_.Get());
    brush(problem ? palette::dangerStrong() : ink); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bx, by), 5.6f, 5.6f), solid_.Get());
    const auto mark = gray(0.07f);
    if (synced) { line(bx - 3.0f, by, bx - 0.8f, by + 2.4f, mark, 1.6f); line(bx - 0.8f, by + 2.4f, bx + 3.2f, by - 2.2f, mark, 1.6f); }
    else if (problem) { line(bx, by - 3.2f, bx, by + 0.6f, mark, 1.7f); brush(mark); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bx, by + 2.8f), 0.9f, 0.9f), solid_.Get()); }
    else { brush(mark); for (const float dx : {-2.2f, 2.2f}) target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bx + dx, by), 1.1f, 1.1f), solid_.Get()); }
}