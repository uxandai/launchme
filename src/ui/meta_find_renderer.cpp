// Renderer: "Znajdź" in Edytuj metadane, the pane over the dialog's fields (src/ui/meta_find_view.hpp). One
// layout, two looks: the desktop window's (flat fields and buttons) and Tryb TV's (pills, glass buttons).
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "desktop_view.hpp"

#include <algorithm>

using namespace render_detail;

namespace {
using palette::gray;
// One line of a longer text: line breaks become spaces.
std::wstring oneLine(std::wstring text) {
    std::replace(text.begin(), text.end(), L'\n', L' ');
    return text;
}
} // namespace

void Renderer::drawMetaFind(const desktop_ui::MetaView& meta, bool tv) {
    namespace mf = meta_find;
    const auto& find = meta.find;
    if (!find.open) return;
    const mf::Layout layout{viewport_.width, viewport_.height};
    const auto p = layout.panel();
    IDWriteTextFormat* const body = tv ? settings_body_font_.Get() : desktop_font_.Get();
    IDWriteTextFormat* const fine = tv ? toast_sub_font_.Get() : desktop_small_font_.Get();
    const D2D1_COLOR_F ink = tv ? gray(0.95f) : palette::text();
    const D2D1_COLOR_F muted = tv ? gray(0.55f) : palette::muted();
    const auto fill = [&](settings_ui::Rect r, float radius, D2D1_COLOR_F color) {
        if (!tv) { desktopFill(r, radius, color); return; }
        brush(color);
        target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), radius, radius), solid_.Get());
    };
    const auto focusRing = [&](settings_ui::Rect r, float radius) {
        if (!tv) { desktopFocusRing(r, radius); return; }
        brush(uiAccent());
        target_->DrawRoundedRectangle(D2D1::RoundedRect(box(r), radius, radius), solid_.Get(), 1.5f);
    };
    const auto button = [&](settings_ui::Rect r, int id, int style, const std::wstring& label, bool enabled) {
        const bool hover = find.hover == id && enabled;
        if (!tv) {
            desktopButton(r, style, hover ? 1.0f : 0.0f, false, enabled, 0, label);
            if (find.focus == id) desktopFocusRing(r, 8);
            return;
        }
        const auto b = box(r);
        const auto color = drawButton(b, find.focus == id ? 1.0f : 0.0f, enabled ? 1.0f : 0.45f, false, hover, enabled);
        textLine(label, b.left + 12, (b.top + b.bottom) * 0.5f, b.right - b.left - 24, toast_font_.Get(), color, DWRITE_TEXT_ALIGNMENT_CENTER);
    };

    // The title to look for, and its button.
    const auto field = layout.field();
    textLine(tr(L"Tytuł do wyszukania"), field.left + 2, field.top - 11, field.right - field.left, fine, muted);
    const bool in_query = find.focus == mf::item::query;
    if (!tv) {
        desktopTextField(field, find.query, tr(L"Tytuł gry"), in_query ? std::min(find.caret, find.query.size()) : find.query.size(), in_query,
                         in_query && find.selected, find.hover == mf::item::query ? 1.0f : 0.0f);
    } else {
        const auto shape = pill(box(field));
        brush(gray(1, in_query && find.selected ? 0.13f : in_query ? 0.08f : 0.05f + (find.hover == mf::item::query ? 0.025f : 0.0f)));
        target_->FillRoundedRectangle(shape, solid_.Get());
        if (in_query) { brush(uiAccent()); target_->DrawRoundedRectangle(shape, solid_.Get(), 1.5f); }
        const float inset = (field.bottom - field.top) * 0.5f;
        target_->PushAxisAlignedClip(D2D1::RectF(field.left + 4, field.top + 1, field.right - 4, field.bottom - 1), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (find.query.empty() && !in_query) textLine(tr(L"Tytuł gry"), field.left + inset, (field.top + field.bottom) * 0.5f, field.right - field.left - 2 * inset, body, gray(0.42f));
        else {
            const auto metrics = typeMetrics(body);
            fieldText(find.query, in_query ? std::min(find.caret, find.query.size()) : find.query.size(), in_query && !find.selected, field.left + inset,
                      (field.top + field.bottom) * 0.5f - metrics.line * 0.5f, field.right - field.left - 2 * inset, metrics.line, body, ink);
        }
        target_->PopAxisAlignedClip();
    }
    button(layout.searchButton(), mf::item::search, 2, tr(L"Szukaj"), !find.busy);

    // The results, best first.
    if (find.results.empty()) {
        const auto first = layout.result(0);
        textLine(find.status.empty() ? tr(L"Wpisz tytuł i naciśnij Szukaj.") : find.status, first.left + 12, first.top + 18, first.right - first.left - 24, body, muted);
    }
    for (int slot = 0; slot < mf::visible && find.has(slot); ++slot) {
        const auto& result = find.results[static_cast<size_t>(slot)];
        const auto r = layout.result(slot);
        const int id = mf::item::result_first + slot;
        const bool chosen = find.chosen == slot;
        if (chosen) fill(r, 8, uiAccent(0.2f));
        else if (find.hover == id) fill(r, 8, gray(1, 0.06f));
        const float cy = (r.top + r.bottom) * 0.5f;
        const float source_w = 112, note_w = std::min(150.0f, textAdvance(result.note, fine) + 4);
        textLine(result.source, r.left + 12, cy, source_w, fine, muted);
        textLine(result.title, r.left + 12 + source_w, cy, r.right - r.left - 24 - source_w - note_w - 8, chosen ? (tv ? body : desktop_strong_font_.Get()) : body, ink);
        textLine(result.note, r.right - 12 - note_w, cy, note_w, fine, muted, DWRITE_TEXT_ALIGNMENT_TRAILING);
        if (find.focus == id) focusRing(r, 8);
    }

    // What to take from the chosen one.
    if (const auto* result = find.current()) {
        textLine(tr(L"Pola do wstawienia"), p.left + 26, layout.takesTop() - 12, 300, fine, muted);
        static const wchar_t* const names[mf::take_count] = {L"Opis", L"Producent", L"Wydawca", L"Data premiery", L"Gatunki"};
        for (int slot = 0; slot < mf::take_count; ++slot) {
            const auto r = layout.take(slot);
            const int id = mf::item::take_first + slot;
            const bool can = find.canTake(slot), on = can && find.take[static_cast<size_t>(slot)];
            if (find.hover == id && can) fill(r, 8, gray(1, 0.05f));
            const float cy = (r.top + r.bottom) * 0.5f;
            const settings_ui::Rect check{r.left + 12, cy - 9, r.left + 30, cy + 9};
            fill(check, 5, gray(1, can ? 0.12f : 0.05f));
            if (on) fill({check.left + 4, check.top + 4, check.right - 4, check.bottom - 4}, 3, uiAccent());
            const auto text_ink = can ? ink : gray(tv ? 0.4f : 0.45f);
            textLine(tr(names[slot]), r.left + 42, cy, 130, body, text_ink);
            const auto& value = result->values[static_cast<size_t>(slot)];
            textLine(value.empty() ? std::wstring(L"—") : oneLine(value), r.left + 180, cy, r.right - r.left - 192, fine, can ? muted : gray(0.35f));
            if (find.focus == id) focusRing(r, 8);
        }
    }
    button(layout.insertButton(), mf::item::insert, 2, tr(L"Wstaw"), find.anyTaken());
    button(layout.backButton(), mf::item::back, 1, tr(L"Wróć do pól"), true);
}
