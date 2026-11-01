// Renderer: Edytuj metadane in Tryb TV (from the game's menu). The desktop dialog's geometry
// (desktop_ui::MetaLayout, so the pointer and the pad behave the same) in the TV's own look: the
// dimmed screen and dark panel of Zidentyfikuj grę, pill fields, glass buttons.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "desktop_view.hpp"

#include <algorithm>

using namespace render_detail;

namespace {
using palette::gray;
}

void Renderer::drawTvMeta(const desktop_ui::MetaView& meta) {
    using desktop_ui::MetaItem;
    using desktop_ui::meta_fields;
    if (!meta.open) return;
    brush(gray(0, 0.82f));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
    const desktop_ui::MetaLayout layout{viewport_.width, viewport_.height};
    const auto p = layout.panel();
    brush(gray(0.055f)); target_->FillRoundedRectangle(D2D1::RoundedRect(box(p), 18, 18), solid_.Get());
    if (panel_borders_) { brush(gray(1, 0.18f)); target_->DrawRoundedRectangle(D2D1::RoundedRect(box(p), 18, 18), solid_.Get()); }
    textLine(std::wstring(tr(L"Edytuj metadane")) + L" \u00b7 " + meta.game, p.left + 24, p.top + 30, p.right - p.left - 90, category_font_.Get(), gray(0.96f));
    textLine(tr(L"Puste pole: wartość automatyczna. A wpisuje w pole, B zamyka."), p.left + 24, p.top + 58, p.right - p.left - 48,
             toast_sub_font_.Get(), gray(0.55f));
    // Close: a glass circle with a dot, as Back in the TV's dialogs.
    const auto close = layout.close();
    brush(drawButton(box(close), meta.focus == static_cast<int>(MetaItem::close) ? 1.0f : 0.0f, 1, false,
                     meta.hover == static_cast<int>(MetaItem::close)));
    target_->FillEllipse(D2D1::Ellipse({(close.left + close.right) * 0.5f, (close.top + close.bottom) * 0.5f}, 3, 3), solid_.Get());
    static const wchar_t* const labels[meta_fields] = {L"Tytuł", L"Opis", L"Producent", L"Wydawca", L"Data premiery", L"Gatunki", L"Czas przejścia (h)",
                                                        L"Czas gry"};
    static const wchar_t* const hints[meta_fields] = {L"Nazwa w bibliotece", L"Kilka zdań o grze", L"Studio", L"Firma wydająca",
                                                       L"np. 2024 albo 12.05.2024", L"np. Akcja, Przygodowa", L"np. 22", L"np. 12:30"};
    {
        // Beside Czas gry: the automatic value and that later play is added to the typed one.
        const auto f = layout.field(static_cast<int>(MetaItem::playtime));
        textLine(meta.playtime_note, f.right + 16, f.top + 11, p.right - 24 - f.right - 16, toast_sub_font_.Get(), gray(0.6f));
        textLine(tr(L"Dalsza gra doliczy się do wpisanej wartości."), f.right + 16, f.top + 29, p.right - 24 - f.right - 16, toast_sub_font_.Get(), gray(0.45f));
    }
    for (int i = 0; i < meta_fields; ++i) {
        const auto f = layout.field(i);
        const auto index = static_cast<size_t>(i);
        textLine(tr(labels[i]), f.left + 4, f.top - 11, f.right - f.left, toast_sub_font_.Get(), gray(0.55f));
        const bool focused = meta.focus == i;
        const float hover = meta.hover == i ? 1.0f : 0.0f;
        const auto& value = meta.fields[index];
        const auto shape = i == 1 ? D2D1::RoundedRect(box(f), 14, 14) : pill(box(f));
        brush(gray(1, focused && meta.selected ? 0.13f : focused ? 0.08f : 0.05f + 0.025f * hover));
        target_->FillRoundedRectangle(shape, solid_.Get());
        if (focused) { brush(uiAccent()); target_->DrawRoundedRectangle(shape, solid_.Get(), 1.5f); }
        const float inset = i == 1 ? 14.0f : (f.bottom - f.top) * 0.5f;
        target_->PushAxisAlignedClip(D2D1::RectF(f.left + 4, f.top + 1, f.right - 4, f.bottom - 1), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (value.empty() && !focused) {
            textLine(tr(hints[i]), f.left + inset, i == 1 ? f.top + 20 : (f.top + f.bottom) * 0.5f, f.right - f.left - 2 * inset,
                     settings_body_font_.Get(), gray(0.42f));
        } else if (i != 1) {
            const auto metrics = typeMetrics(settings_body_font_.Get());
            fieldText(value, focused ? std::min(meta.caret, value.size()) : value.size(), focused && !meta.selected, f.left + inset,
                      (f.top + f.bottom) * 0.5f - metrics.line * 0.5f, f.right - f.left - 2 * inset, metrics.line, settings_body_font_.Get(), gray(0.95f));
        } else {
            // Opis: several lines; the caret is a bar in the text where it stands.
            auto text = value;
            size_t caret = std::min(meta.caret, text.size());
            if (focused && text.size() > 520 && caret > 420) { // Long: the part before the caret, so the caret shows.
                const size_t from = caret - 420;
                text = L"\u2026 " + text.substr(from);
                caret = caret - from + 2;
            }
            if (focused && !meta.selected) text.insert(caret, L"|");
            paragraph(text, f.left + inset, f.top + 10, f.right - f.left - 2 * inset, settings_body_font_.Get(), gray(0.95f), 6);
        }
        target_->PopAxisAlignedClip();
    }
    const auto button = [&](MetaItem item, const std::wstring& label, bool enabled) {
        const auto r = box(layout.button(item));
        const int id = static_cast<int>(item);
        const auto ink = drawButton(r, meta.focus == id ? 1.0f : 0.0f, enabled ? 1.0f : 0.45f, false, meta.hover == id && enabled, enabled);
        textLine(label, r.left + 12, (r.top + r.bottom) * 0.5f, r.right - r.left - 24, toast_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    };
    button(MetaItem::restore, tr(L"Przywróć automatyczne"), meta.overridden);
    if (meta.identify) button(MetaItem::identify, tr(L"Zidentyfikuj"), true);
    button(MetaItem::save, tr(L"Zapisz"), true);
    button(MetaItem::cancel, tr(L"Anuluj"), true);
}
