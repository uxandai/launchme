// Desktop mode's popups (src/ui/desktop_view.hpp): the menus with their submenus, Pobierania under
// its shortcut, the posters being dragged, tooltips, and the window buttons drawn over a TV screen
// shown inside the desktop window. Solid panels with a hairline border and a soft shadow.
#include "renderer.hpp"
#include "renderer_internal.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace desktop_ui;

namespace {
using palette::gray;
D2D1_COLOR_F rgb(float r, float g, float b, float a = 1) { return D2D1::ColorF(r, g, b, a); }
const D2D1_COLOR_F panel_bg = rgb(0.082f, 0.088f, 0.102f);
float luminance(D2D1_COLOR_F c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
}

// ---- Menus -----------------------------------------------------------------------------------

void Renderer::drawDesktopMenuPanel(Rect panel, const std::vector<MenuItem>& items, int hover, int open) {
    desktopShadow(panel, 10, 1);
    desktopFill(panel, 10, panel_bg);
    desktopStroke(panel, 10, gray(1, 0.1f));
    target_->PushAxisAlignedClip(D2D1::RectF(panel.left, panel.top, panel.right, panel.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const auto& item = items[static_cast<size_t>(i)];
        const auto r = itemRect(panel, items, i);
        if (r.bottom > panel.bottom) break;
        const float cy = (r.top + r.bottom) * 0.5f;
        if (item.separator) {
            brush(gray(1, 0.07f));
            target_->FillRectangle(D2D1::RectF(r.left + 6, cy, r.right - 6, cy + 1), solid_.Get());
            continue;
        }
        const bool lit = item.enabled && (i == hover || i == open);
        if (lit) desktopFill(r, 6, item.danger ? rgb(0.86f, 0.26f, 0.22f, 0.18f) : gray(1, 0.075f));
        const auto ink = !item.enabled ? gray(0.42f) : item.danger ? palette::danger() : gray(lit ? 0.98f : 0.9f);
        if (item.checked) desktopGlyph(glyph::check, r.left + 16, cy, 12, item.enabled ? uiAccent() : gray(0.42f));
        else if (item.icon) desktopGlyph(item.icon, r.left + 16, cy, 14, !item.enabled ? gray(0.38f) : item.danger ? palette::danger() : gray(lit ? 0.92f : 0.7f));
        float right = r.right - 10;
        if (item.submenu) { desktopGlyph(glyph::chevron_right, r.right - 14, cy, 9, gray(item.enabled ? 0.7f : 0.38f)); right -= 18; }
        if (!item.shortcut.empty()) {
            const float w = textAdvance(item.shortcut, desktop_small_font_.Get());
            textLine(item.shortcut, right - w, cy, w + 2, desktop_small_font_.Get(), gray(item.enabled ? 0.52f : 0.36f));
            right -= w + 12;
        }
        textLine(item.label, r.left + 36, cy, right - r.left - 36, desktop_font_.Get(), ink);
    }
    target_->PopAxisAlignedClip();
}

void Renderer::drawDesktopMenu(const Layout& layout, const Menu& menu) {
    const auto panel = menuRect(menu, layout.width, layout.height);
    const bool sub = menu.sub_parent >= 0 && !menu.sub.empty();
    drawDesktopMenuPanel(panel, menu.items, sub && menu.in_sub ? -1 : menu.hover, sub ? menu.sub_parent : -1);
    if (sub) drawDesktopMenuPanel(subRect(menu, layout.width, layout.height), menu.sub, menu.sub_hover, -1);
}

// ---- Pobierania ------------------------------------------------------------------------------

void Renderer::drawDesktopDownloads(const Layout& layout, const View& view) {
    const auto at = std::find(view.tools.begin(), view.tools.end(), Tool::downloads);
    if (at == view.tools.end()) return;
    const auto anchor = layout.tool(static_cast<int>(at - view.tools.begin()));
    const int rows = static_cast<int>(view.download_rows.size());
    const auto p = layout.popover(rows, anchor);
    desktopShadow(p, 12, 1);
    desktopFill(p, 12, panel_bg);
    desktopStroke(p, 12, gray(1, 0.1f));
    // A notch pointing at the shortcut.
    {
        const float x = (anchor.left + anchor.right) * 0.5f;
        ComPtr<ID2D1PathGeometry> notch;
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(factory_->CreatePathGeometry(notch.GetAddressOf())) && SUCCEEDED(notch->Open(sink.GetAddressOf()))) {
            sink->BeginFigure(D2D1::Point2F(x - 7, p.top + 0.5f), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(D2D1::Point2F(x, p.top - 6));
            sink->AddLine(D2D1::Point2F(x + 7, p.top + 0.5f));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if (SUCCEEDED(sink->Close())) {
                brush(panel_bg);
                target_->FillGeometry(notch.Get(), solid_.Get());
                brush(gray(1, 0.1f));
                target_->DrawGeometry(notch.Get(), solid_.Get(), 1);
                brush(panel_bg);
                target_->FillRectangle(D2D1::RectF(x - 6.5f, p.top, x + 6.5f, p.top + 1.5f), solid_.Get());
            }
        }
    }
    const float left = p.left + 18, right = p.right - 18;
    const float head = p.top + popover_head * 0.5f + 2;
    textLine(tr(L"Pobierania"), left, head, 200, desktop_strong_font_.Get(), palette::text());
    std::wstring summary;
    if (!rows) summary = {};
    else if (view.downloads_moving) summary = std::wstring(tr(L"Aktywne: ")) + std::to_wstring(view.downloads_moving);
    else summary = std::wstring(tr(L"W kolejce: ")) + std::to_wstring(rows);
    float summary_right = right;
    if (view.downloads_clearable) { // Wyczyść listę: what is not moving.
        const auto clear = layout.popoverClear(rows, anchor);
        desktopButton(clear, 0, view.hover.target == Target::popover_clear ? 1.0f : 0.0f, view.pressed.target == Target::popover_clear, true, 0, tr(L"Wyczyść listę"));
        summary_right = clear.left - 10;
    }
    if (!summary.empty()) {
        const float w = textAdvance(summary, desktop_small_font_.Get());
        textLine(summary, summary_right - w, head, w + 2, desktop_small_font_.Get(), palette::muted());
    }
    brush(gray(1, 0.06f));
    target_->FillRectangle(D2D1::RectF(p.left + 1, p.top + popover_head - 1, p.right - 1, p.top + popover_head), solid_.Get());
    if (!rows) {
        const auto r = layout.popoverRow(0, rows, anchor);
        const float cy = (r.top + r.bottom) * 0.5f;
        desktopGlyph(glyph::download, left + 14, cy, 16, gray(0.5f));
        textLine(tr(L"Nic się teraz nie pobiera"), left + 40, cy - 8, right - left - 40, desktop_font_.Get(), gray(0.86f));
        textLine(tr(L"Instalacje ze sklepów pojawią się tutaj."), left + 40, cy + 10, right - left - 40, desktop_small_font_.Get(), palette::muted());
    }
    for (int i = 0; i < std::min(rows, popover_rows); ++i) {
        const auto& row = view.download_rows[static_cast<size_t>(i)];
        const auto r = layout.popoverRow(i, rows, anchor);
        const bool hover = view.hover == Hit{Target::popover_row, i} || view.hover == Hit{Target::popover_action, i};
        if (hover) desktopFill(r, 8, gray(1, 0.06f));
        const float x = r.left + 12, w = r.right - 12 - x;
        const std::wstring name = i < static_cast<int>(view.download_names.size()) ? view.download_names[static_cast<size_t>(i)] : row.store;
        // The ring: its share, or a turning segment while none is known; paused: still.
        const float cx = x + 16, cy = (r.top + r.bottom) * 0.5f;
        desktopArc(cx, cy, 14, 0, 2 * std::numbers::pi_v<float>, gray(1, 0.1f), 2.5f);
        if (row.fraction) desktopArc(cx, cy, 14, -std::numbers::pi_v<float> * 0.5f, std::max(0.04f, *row.fraction) * 2 * std::numbers::pi_v<float>,
                                     row.moving ? uiAccent() : gray(0.6f), 2.5f);
        else if (row.moving) desktopArc(cx, cy, 14, elapsed_ * 4.0f, 1.4f, uiAccent(), 2.5f);
        desktopGlyph(row.moving ? glyph::download : glyph::pause, cx, cy, 11, row.moving ? gray(0.9f) : gray(0.6f));
        const float text_left = x + 42;
        float meta_right = r.right - 12;
        if (hover && static_cast<size_t>(i) < view.download_actions.size()) { // Its own button in place of the share.
            const auto action = Layout::popoverAction(r);
            const Hit hit{Target::popover_action, i};
            desktopButton(action, 1, view.hover == hit ? 1.0f : 0.0f, view.pressed == hit, true, 0, view.download_actions[static_cast<size_t>(i)]);
            meta_right = action.left - 8;
        } else if (row.fraction) {
            const auto percent = std::to_wstring(static_cast<int>(std::lround(*row.fraction * 100))) + L"%";
            const float pw = textAdvance(percent, desktop_strong_font_.Get());
            textLine(percent, meta_right - pw, cy - 9, pw + 2, desktop_strong_font_.Get(), row.moving ? palette::text() : gray(0.7f));
        }
        textLine(name, text_left, cy - 9, meta_right - text_left - 52, desktop_strong_font_.Get(), palette::text());
        std::wstring meta = row.store;
        if (!row.status.empty()) meta += (meta.empty() ? L"" : L" · ") + row.status;
        if (!row.amount.empty()) meta += (meta.empty() ? L"" : L" · ") + row.amount;
        textLine(meta, text_left, cy + 10, x + w - text_left, desktop_small_font_.Get(), palette::muted());
    }
    const auto all = layout.popoverAll(rows, anchor);
    const bool all_hover = view.hover.target == Target::popover_all;
    desktopButton(all, 1, all_hover ? 1.0f : 0.0f, view.pressed.target == Target::popover_all, true, 0, tr(L"Otwórz Pobierania"));
}

// ---- Dragging, tooltips ----------------------------------------------------------------------

void Renderer::drawDesktopDrag(const Layout&, const View& view) {
    if (!cards_ || view.drag_card < 0 || static_cast<size_t>(view.drag_card) >= cards_->size()) return;
    const auto index = static_cast<size_t>(view.drag_card);
    const float w = 66, h = 99;
    const float x = view.drag_x + 14, y = view.drag_y + 6;
    D2D1_MATRIX_3X2_F base{};
    target_->GetTransform(&base);
    // More than one: a small stack behind.
    const int behind = static_cast<int>(std::min<size_t>(view.drag_count, 3)) - 1;
    for (int i = behind; i >= 1; --i) {
        target_->SetTransform(D2D1::Matrix3x2F::Rotation(static_cast<float>(i) * 5.0f, D2D1::Point2F(x + w * 0.5f, y + h)) * base);
        desktopFill({x, y, x + w, y + h}, 8, rgb(0.16f, 0.17f, 0.2f, 0.95f));
        desktopStroke({x, y, x + w, y + h}, 8, gray(1, 0.12f));
    }
    target_->SetTransform(D2D1::Matrix3x2F::Rotation(-3.0f, D2D1::Point2F(x + w * 0.5f, y + h)) * base);
    desktopShadow({x, y, x + w, y + h}, 8, 1);
    const auto rect = D2D1::RectF(x, y, x + w, y + h);
    desktopFill({x, y, x + w, y + h}, 8, rgb(0.14f, 0.16f, 0.19f));
    ComPtr<ID2D1RoundedRectangleGeometry> clip;
    factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect, 8, 8), clip.GetAddressOf());
    if (index < posters_.size() && posters_[index].bitmap) fillImage(posters_[index], rect, 8, 0.96f, false, clip.Get());
    else if (index < covers_.size() && covers_[index].bitmap) fillImage(covers_[index], rect, 8, 0.96f, false, clip.Get(), true);
    desktopStroke({x, y, x + w, y + h}, 8, view.drag_valid ? uiAccent() : gray(1, 0.2f), view.drag_valid ? 2.0f : 1.0f);
    if (view.drag_count > 1) {
        const auto count = std::to_wstring(view.drag_count);
        const float bw = std::max(22.0f, textAdvance(count, desktop_label_font_.Get()) + 12);
        const Rect badge{x + w - bw * 0.5f - 2, y - 9, x + w + bw * 0.5f - 2, y + 13};
        desktopFill(badge, 11, uiAccent());
        textLine(count, badge.left, (badge.top + badge.bottom) * 0.5f, badge.right - badge.left, desktop_label_font_.Get(),
                 luminance(uiAccent()) > 0.6f ? gray(0.06f) : gray(0.98f), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    target_->SetTransform(base);
    // What letting go does.
    if (!view.drag_label.empty()) {
        const float lw = std::min(320.0f, textAdvance(view.drag_label, desktop_small_font_.Get()) + 22);
        const Rect pill{x - 4, y + h + 12, x - 4 + lw, y + h + 38};
        desktopShadow(pill, 13, 0.6f);
        desktopFill(pill, 13, view.drag_valid ? rgb(0.10f, 0.11f, 0.13f) : rgb(0.10f, 0.11f, 0.13f, 0.94f));
        desktopStroke(pill, 13, view.drag_valid ? uiAccent(0.85f) : gray(1, 0.12f));
        textLine(view.drag_label, pill.left + 11, (pill.top + pill.bottom) * 0.5f, lw - 20, desktop_small_font_.Get(),
                 view.drag_valid ? palette::text() : palette::muted());
    }
}

void Renderer::drawDesktopTooltip(const Layout& layout, const View& view) {
    if (view.tooltip.empty() || view.tooltip_alpha < 0.01f) return;
    const float alpha = view.tooltip_alpha;
    const float w = std::min(340.0f, textAdvance(view.tooltip, desktop_small_font_.Get()) + 20), h = 28;
    const auto& a = view.tooltip_anchor;
    float x = std::clamp((a.left + a.right - w) * 0.5f, 8.0f, layout.width - w - 8);
    float y = a.bottom + 8 + (1 - alpha) * 3;
    if (y + h > layout.height - 8) y = a.top - 8 - h - (1 - alpha) * 3;
    const Rect r{x, y, x + w, y + h};
    desktopShadow(r, 7, 0.5f * alpha);
    desktopFill(r, 7, rgb(0.15f, 0.16f, 0.185f, 0.98f * alpha));
    desktopStroke(r, 7, gray(1, 0.1f * alpha));
    textLine(view.tooltip, x + 10, y + h * 0.5f, w - 20, desktop_small_font_.Get(), gray(0.95f, alpha));
}

// ---- The window strip over the TV screens ----------------------------------------------------

void Renderer::drawWindowStrip() {
    if (!context_ || !context_->window_strip) return;
    const float dpi = std::max(0.5f, context_->window_dpi);
    const auto pixels = target_->GetPixelSize();
    const float width = static_cast<float>(pixels.width);
    D2D1_MATRIX_3X2_F base{};
    target_->GetTransform(&base);
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    // A soft shade behind the buttons keeps them legible over bright pictures.
    {
        const auto first = stripButton(0, width, dpi);
        ComPtr<ID2D1GradientStopCollection> stops;
        ComPtr<ID2D1RadialGradientBrush> shade;
        const D2D1_GRADIENT_STOP shade_stops[] = {{0, gray(0, 0.38f)}, {1, gray(0, 0)}};
        const float rx = (width - first.left) * 1.25f, ry = strip_height * dpi * 2.2f;
        if (SUCCEEDED(target_->CreateGradientStopCollection(shade_stops, 2, stops.GetAddressOf())) &&
            SUCCEEDED(target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(width, 0), D2D1::Point2F(0, 0), rx, ry),
                                                        stops.Get(), shade.GetAddressOf())))
            target_->FillRectangle(D2D1::RectF(width - rx, 0, width, ry), shade.Get());
    }
    for (int i = 0; i < 3; ++i) {
        const auto r = stripButton(i, width, dpi);
        const bool hover = context_->window_strip_hover == i || (i == 1 && context_->window_max_hover);
        if (hover) {
            brush(i == 2 ? rgb(0.77f, 0.17f, 0.11f) : gray(1, 0.14f));
            target_->FillRectangle(D2D1::RectF(r.left, r.top, r.right, r.bottom), solid_.Get());
        }
        const wchar_t g = i == 0 ? glyph::minimize : i == 1 ? (context_->window_maximized ? glyph::restore : glyph::maximize) : glyph::window_close;
        desktopGlyph(g, (r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f, 10 * dpi, gray(hover ? 1.0f : 0.88f));
    }
    target_->SetTransform(base);
}


// ---- Ctrl+K ----------------------------------------------------------------------------------

void Renderer::drawDesktopQuick(const View& view) {
    const auto& quick = view.quick;
    if (!quick.open) return;
    brush(rgb(0.01f, 0.012f, 0.016f, 0.12f)); // Barely dimmed, as the window's dialogs.
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    const QuickLayout layout{viewport_.width, viewport_.height, static_cast<int>(quick.items.size())};
    const auto p = layout.panel(), field = layout.field();
    desktopShadow(p, 14, 1.6f);
    desktopFill(p, 14, panel_bg);
    desktopStroke(p, 14, gray(1, 0.12f));
    // The field: what is typed, its caret.
    const float cy = (field.top + field.bottom) * 0.5f;
    desktopGlyph(glyph::search, field.left + 26, cy, 16, palette::muted());
    const float text_left = field.left + 50, text_right = field.right - 70;
    const auto m = typeMetrics(desktop_title_font_.Get());
    const bool blink = (GetTickCount64() / 530) % 2 == 0;
    if (quick.query.empty()) {
        if (blink) desktopFill({text_left, cy - 11, text_left + 1.5f, cy + 11}, 0, palette::text());
        textLine(tr(L"Szukaj gier, kolekcji, ustawień i poleceń…"), text_left + 4, cy, text_right - text_left - 4, desktop_title_font_.Get(), palette::muted(0.8f));
    } else {
        desktopSelection(quick.query, text_left, cy, text_right - text_left, desktop_title_font_.Get());
        fieldText(quick.query, std::min(quick.caret, quick.query.size()), blink, text_left, cy - m.line * 0.5f, text_right - text_left, m.line,
                     desktop_title_font_.Get(), palette::text());
    }
    const auto esc = std::wstring(L"Esc");
    const float esc_w = textAdvance(esc, desktop_label_font_.Get()) + 14;
    const Rect cap{field.right - 18 - esc_w, cy - 11, field.right - 18, cy + 11};
    desktopStroke(cap, 5, gray(1, 0.18f));
    textLine(esc, cap.left, cy, cap.right - cap.left, desktop_label_font_.Get(), palette::muted(), DWRITE_TEXT_ALIGNMENT_CENTER);
    brush(gray(1, 0.07f));
    target_->FillRectangle(D2D1::RectF(p.left + 1, field.bottom - 1, p.right - 1, field.bottom), solid_.Get());
    // The results.
    if (quick.items.empty())
        textLine(tr(L"Nic nie pasuje. Spróbuj innej nazwy."), p.left + 24, (layout.row(0).top + layout.row(0).bottom) * 0.5f, p.right - p.left - 48,
                 desktop_font_.Get(), palette::muted());
    for (int i = 0; i < static_cast<int>(quick.items.size()) && i < quick_rows; ++i) {
        const auto& item = quick.items[static_cast<size_t>(i)];
        const auto r = layout.row(i);
        const bool selected = i == quick.selected, hover = i == quick.hover;
        if (selected) desktopFill(r, 8, uiAccent(0.2f));
        else if (hover) desktopFill(r, 8, gray(1, 0.05f));
        const float rcy = (r.top + r.bottom) * 0.5f;
        const auto ink = selected ? palette::text() : gray(0.78f);
        if (!desktopBrand(item.brand, r.left + 22, rcy, 16, ink)) desktopGlyph(item.icon ? item.icon : glyph::game, r.left + 22, rcy, 15, ink);
        const float detail_w = std::min(180.0f, textAdvance(item.detail, desktop_small_font_.Get()) + 4);
        textLine(item.title, r.left + 46, rcy, r.right - r.left - 46 - detail_w - 20, selected ? desktop_strong_font_.Get() : desktop_font_.Get(), palette::text());
        textLine(item.detail, r.right - 12 - detail_w, rcy, detail_w, desktop_small_font_.Get(), palette::muted(), DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    // What the keys do.
    const float foot = p.bottom - quick_foot * 0.5f;
    brush(gray(1, 0.06f));
    target_->FillRectangle(D2D1::RectF(p.left + 1, p.bottom - quick_foot, p.right - 1, p.bottom - quick_foot + 1), solid_.Get());
    textLine(tr(L"↑↓ wybór   Enter otwórz   Ctrl+Enter graj   Esc zamknij"), p.left + 20, foot, p.right - p.left - 40, desktop_small_font_.Get(), palette::muted());
}

// ---- Edytuj metadane ---------------------------------------------------------------------------

void Renderer::drawDesktopMeta(const MetaView& meta) {
    if (!meta.open) return;
    brush(rgb(0.01f, 0.012f, 0.016f, 0.36f)); // Dimmed as every dialog of the window (desktopDialogFrame).
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    const MetaLayout layout{viewport_.width, viewport_.height};
    const auto p = layout.panel();
    desktopShadow(p, 14, 1.6f);
    desktopFill(p, 14, panel_bg);
    desktopStroke(p, 14, gray(1, 0.12f));
    const bool finding = meta.find.open;
    textLine(std::wstring(finding ? tr(L"Znajdź metadane") : tr(L"Edytuj metadane")) + L" \u00b7 " + meta.game, p.left + 24, p.top + 30,
             p.right - p.left - (finding ? 90 : 220), desktop_title_font_.Get(), palette::text());
    textLine(finding ? (meta.pad ? tr(L"Wybierz grę i pola. Wstaw kopiuje je do dialogu, zapisuje dopiero Zapisz. B wraca.")
                                 : tr(L"Wybierz grę i pola. Wstaw kopiuje je do dialogu, zapisuje dopiero Zapisz. Esc wraca."))
                     : meta.pad ? tr(L"Puste pole: wartość automatyczna. A wpisuje w pole, B zamyka.") : tr(L"Puste pole: wartość automatyczna. Ctrl+Enter zapisuje, Esc anuluje."),
             p.left + 24, p.top + 56, p.right - p.left - 48,
             desktop_small_font_.Get(), palette::muted());
    const auto close = layout.close();
    if (meta.hover == static_cast<int>(MetaItem::close)) desktopFill(close, 8, gray(1, 0.08f));
    desktopGlyph(glyph::close, (close.left + close.right) * 0.5f, (close.top + close.bottom) * 0.5f, 12, palette::muted());
    if (finding) { drawMetaFind(meta, false); return; }
    {
        const auto find = layout.findButton(); // The stores' and databases' details, to pick from.
        const int id = static_cast<int>(MetaItem::find);
        desktopButton(find, 1, meta.hover == id ? 1.0f : 0.0f, false, true, 0, tr(L"Znajdź…"));
        if (meta.focus == id) desktopFocusRing(find, 8);
    }
    static const wchar_t* const labels[meta_fields] = {L"Tytuł", L"Opis", L"Producent", L"Wydawca", L"Data premiery", L"Gatunki", L"Czas przejścia (h)",
                                                        L"Czas gry"};
    static const wchar_t* const hints[meta_fields] = {L"Nazwa w bibliotece", L"Kilka zdań o grze", L"Studio", L"Firma wydająca",
                                                       L"np. 2024 albo 12.05.2024", L"np. Akcja, Przygodowa", L"np. 22", L"np. 12:30"};
    {
        // Beside Czas gry: the automatic value and that later play is added to the typed one.
        const auto f = layout.field(static_cast<int>(MetaItem::playtime));
        textLine(meta.playtime_note, f.right + 16, f.top + 11, p.right - 24 - f.right - 16, desktop_small_font_.Get(), palette::muted());
        textLine(tr(L"Dalsza gra doliczy się do wpisanej wartości."), f.right + 16, f.top + 29, p.right - 24 - f.right - 16,
                 desktop_small_font_.Get(), palette::muted(0.75f));
    }
    for (int i = 0; i < meta_fields; ++i) {
        const auto f = layout.field(i);
        const auto index = static_cast<size_t>(i);
        textLine(tr(labels[i]), f.left + 2, f.top - 11, f.right - f.left, desktop_small_font_.Get(), palette::muted());
        const bool focused = meta.focus == i;
        const float hover = meta.hover == i ? 1.0f : 0.0f;
        const auto& value = meta.fields[index];
        if (i != 1) {
            desktopTextField(f, value, tr(hints[i]), focused ? std::min(meta.caret, value.size()) : value.size(), focused, focused && meta.selected, hover);
            continue;
        }
        // Opis: several lines; the caret is a bar in the text where it stands.
        desktopFill(f, 9, gray(1, focused ? 0.07f : 0.045f + 0.02f * hover));
        if (focused) desktopStroke({f.left - 1, f.top - 1, f.right + 1, f.bottom + 1}, 10, uiAccent(0.9f), 1.5f);
        else desktopStroke(f, 9, gray(1, 0.1f));
        auto text = value;
        size_t caret = std::min(meta.caret, text.size());
        if (focused && text.size() > 520 && caret > 420) { // Long: the part before the caret, so the caret shows.
            const size_t from = caret - 420;
            text = L"\u2026 " + text.substr(from);
            caret = caret - from + 2;
        }
        if (focused && !meta.selected) text.insert(caret, L"|");
        target_->PushAxisAlignedClip(D2D1::RectF(f.left + 1, f.top + 1, f.right - 1, f.bottom - 1), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (focused && meta.selected) desktopFill({f.left + 6, f.top + 6, f.right - 6, f.bottom - 6}, 4, uiAccent(0.18f));
        if (value.empty() && !focused) textLine(tr(hints[i]), f.left + 12, f.top + 19, f.right - f.left - 24, desktop_font_.Get(), palette::muted(0.8f));
        else paragraph(text, f.left + 12, f.top + 9, f.right - f.left - 24, desktop_font_.Get(), palette::text(), 6);
        target_->PopAxisAlignedClip();
    }
    const auto button = [&](MetaItem item, int style, const std::wstring& label, bool enabled) {
        const auto r = layout.button(item);
        const int id = static_cast<int>(item);
        desktopButton(r, style, meta.hover == id && enabled ? 1.0f : 0.0f, false, enabled, 0, label);
        if (meta.focus == id) desktopFocusRing(r, 8);
    };
    button(MetaItem::restore, 0, tr(L"Przywróć automatyczne"), meta.overridden);
    if (meta.identify) button(MetaItem::identify, 1, tr(L"Zidentyfikuj"), true); // The game's match in RAWG and its artwork.
    button(MetaItem::save, 2, tr(L"Zapisz"), true);
    button(MetaItem::cancel, 1, tr(L"Anuluj"), true);
}