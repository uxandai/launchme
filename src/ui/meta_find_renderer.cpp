// Renderer: "Identyfikuj grę" in Edytuj metadane, the pane in place of the dialog's fields
// (src/ui/meta_find_view.hpp). The list on the left (each game's picture, title, where it comes from), the
// chosen game on the right (its picture, what it gives). One layout, two looks: the desktop window's (flat
// fields and buttons) and Tryb TV's (dark wells, glass buttons, one white focus ring).
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
// A source's mark where its picture is missing: "RAWG", "eShop"…
std::wstring sourceMark(const std::wstring& source) {
    if (source == L"Nintendo eShop") return L"eShop";
    if (source == L"Microsoft Store") return L"Xbox";
    if (source == L"ScreenScraper") return L"SS";
    return source;
}
} // namespace

void Renderer::drawMetaFind(const desktop_ui::MetaView& meta, bool tv) {
    namespace mf = meta_find;
    const auto& find = meta.find;
    if (!find.open) return;
    const mf::Layout layout{viewport_.width, viewport_.height, tv};
    IDWriteTextFormat* const body = tv ? settings_body_font_.Get() : desktop_font_.Get();
    IDWriteTextFormat* const fine = tv ? toast_sub_font_.Get() : desktop_small_font_.Get();
    IDWriteTextFormat* const strong = tv ? menu_font_.Get() : desktop_strong_font_.Get();
    const D2D1_COLOR_F ink = tv ? gray(0.95f) : palette::text();
    const D2D1_COLOR_F muted = tv ? gray(0.58f) : palette::muted();
    const auto middle = [](settings_ui::Rect r) { return (r.top + r.bottom) * 0.5f; };
    const auto fill = [&](settings_ui::Rect r, float radius, D2D1_COLOR_F color) {
        if (!tv) { desktopFill(r, radius, color); return; }
        brush(color);
        target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), radius, radius), solid_.Get());
    };
    // Tryb TV: one ring, drawn last; the window: its focus ring at once.
    std::optional<settings_ui::Rect> ring;
    float ring_radius = 0;
    const auto focusRing = [&](settings_ui::Rect r, float radius) {
        if (!tv) { desktopFocusRing(r, radius); return; }
        ring = r; ring_radius = radius;
    };
    const auto button = [&](settings_ui::Rect r, int id, int style, const std::wstring& label, bool enabled) {
        const bool hover = find.hover == id && enabled;
        if (!tv) {
            desktopButton(r, style, hover ? 1.0f : 0.0f, false, enabled, 0, label);
            if (find.focus == id) desktopFocusRing(r, 8);
            return;
        }
        const auto b = box(r);
        D2D1_COLOR_F color;
        if (style == 2 && enabled) { // The primary one: the accent.
            const float radius = (b.bottom - b.top) * 0.5f;
            brush(uiAccent(hover ? 1.0f : 0.92f));
            target_->FillRoundedRectangle(D2D1::RoundedRect(b, radius, radius), solid_.Get());
            color = gray(0.06f);
        } else color = drawButton(b, 0, enabled ? 1.0f : 0.45f, false, hover, enabled);
        textLine(label, b.left + 12, (b.top + b.bottom) * 0.5f, b.right - b.left - 24, tv ? body : toast_font_.Get(), color, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (find.focus == id) focusRing(r, (r.bottom - r.top) * 0.5f);
    };
    // A picture in its frame (cover-filled), else the source's mark.
    const auto picture = [&](settings_ui::Rect r, Cover* image, const mf::Result& result, float radius) {
        fill(r, radius, tv ? gray(0.03f) : gray(1, 0.05f));
        if (image) fillImage(*image, box(r), radius, reveal(*image));
        else textLine(sourceMark(result.source), r.left + 6, middle(r), r.right - r.left - 12, fine, tv ? gray(0.42f) : palette::muted(0.8f),
                      DWRITE_TEXT_ALIGNMENT_CENTER);
    };

    // The title to look for, and its button (a spinner while the sources answer).
    const auto field = layout.field();
    textLine(tr(L"Tytuł do wyszukania"), field.left + 4, field.top - (tv ? 13.0f : 11.0f), field.right - field.left, fine, muted);
    const bool in_query = find.focus == mf::item::query;
    if (!tv) {
        desktopTextField(field, find.query, tr(L"Tytuł gry"), in_query ? std::min(find.caret, find.query.size()) : find.query.size(), in_query,
                         in_query && find.selected, find.hover == mf::item::query ? 1.0f : 0.0f);
    } else {
        const float radius = (field.bottom - field.top) * 0.5f;
        fill(field, radius, in_query && find.selected ? uiAccent(0.22f) : gray(0.025f + (find.hover == mf::item::query ? 0.02f : 0.0f)));
        if (in_query) focusRing(field, radius);
        target_->PushAxisAlignedClip(D2D1::RectF(field.left + 4, field.top + 1, field.right - 4, field.bottom - 1), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (find.query.empty() && !in_query) textLine(tr(L"Tytuł gry"), field.left + radius, middle(field), field.right - field.left - 2 * radius, body, gray(0.40f));
        else {
            const auto metrics = typeMetrics(body);
            fieldText(find.query, in_query ? std::min(find.caret, find.query.size()) : find.query.size(), in_query && !find.selected, field.left + radius,
                      middle(field) - metrics.line * 0.5f, field.right - field.left - 2 * radius, metrics.line, body, ink);
        }
        target_->PopAxisAlignedClip();
    }
    const auto search = layout.searchButton();
    button(search, mf::item::search, 1, find.busy ? std::wstring{} : std::wstring(tr(L"Szukaj")), !find.busy && !find.applying);
    if (find.busy) drawSpinner((search.left + search.right) * 0.5f, middle(search), tv ? 11.0f : 8.0f, tv ? gray(0.85f) : palette::text());

    // The results, best first: the picture, the title, where it comes from (a database's in the accent: it identifies).
    if (find.results.empty()) {
        const auto first = layout.result(0);
        textLine(find.status.empty() ? tr(L"Wpisz tytuł i naciśnij Szukaj.") : find.status, first.left + 12, first.top + 22, first.right - first.left - 24, body, muted);
    }
    for (int slot = 0; slot < mf::visible && find.has(slot); ++slot) {
        const auto& result = find.results[static_cast<size_t>(slot)];
        const auto r = layout.result(slot);
        const int id = mf::item::result_first + slot;
        const bool chosen = find.chosen == slot;
        const float radius = tv ? 14.0f : 8.0f;
        if (chosen) fill(r, radius, tv ? gray(1, 0.09f) : uiAccent(0.2f));
        else if (find.hover == id) fill(r, radius, gray(1, tv ? 0.05f : 0.06f));
        const auto thumb = layout.thumb(slot);
        picture(thumb, dialogImage(find_thumbs_[static_cast<size_t>(slot)], result.thumb), result, tv ? 9.0f : 6.0f);
        const float left = thumb.right + (tv ? 16.0f : 12.0f), width = r.right - left - 12, cy = middle(r);
        const float gap = tv ? 13.0f : 10.0f;
        textLine(result.title, left, cy - gap, width, chosen ? strong : body, ink);
        const float source_w = std::min(width, textAdvance(result.source + L" ", fine));
        textLine(result.source, left, cy + gap, source_w, fine, result.database() ? uiAccent() : muted);
        if (!result.note.empty())
            textLine(L"· " + oneLine(result.note), left + source_w, cy + gap, width - source_w, fine, muted);
        if (find.focus == id) focusRing(r, radius);
    }

    // The chosen game: its picture, title, and what it gives.
    if (const auto* result = find.current()) {
        const auto d = layout.detail();
        const auto shot = layout.picture();
        const float radius = tv ? 14.0f : 8.0f;
        picture(shot, dialogImage(find_thumbs_[static_cast<size_t>(find.chosen)], result->thumb), *result, radius);
        const float info = layout.infoTop();
        textLine(result->title, d.left + 2, info + (tv ? 14.0f : 11.0f), d.right - d.left - 4, strong, ink);
        std::wstring origin = result->source;
        if (!result->note.empty()) origin += L" · " + oneLine(result->note);
        textLine(origin, d.left + 2, info + (tv ? 42.0f : 33.0f), d.right - d.left - 4, fine, muted);
        if (find.applying) {
            const float cy = layout.takesTop() + 40;
            drawSpinner((d.left + d.right) * 0.5f, cy, tv ? 20.0f : 14.0f, uiAccent());
            if (!find.status.empty()) paragraph(find.status, d.left + 4, cy + 36, d.right - d.left - 8, fine, muted, 3);
        } else if (result->database()) {
            // A database's game identifies this one: its details, rating and artwork (the player's own entries stay).
            const std::wstring what = result->kind == mf::Result::Kind::rawg
                ? tr(L"Zidentyfikuj grę w RAWG: opis, twórcy, data premiery, gatunki, ocena i brakujące grafiki. Twoje własne wpisy zostaną.")
                : tr(L"Zidentyfikuj grę w ScreenScraper: opis, twórcy, data premiery, gatunki i grafiki. Twoje własne wpisy zostaną.");
            paragraph(what, d.left + 2, layout.takesTop() - (tv ? 20.0f : 16.0f), d.right - d.left - 4, body, tv ? gray(0.8f) : palette::text(), 6);
            if (!find.status.empty()) paragraph(find.status, d.left + 2, d.bottom - (tv ? 48.0f : 40.0f), d.right - d.left - 4, fine, palette::danger(), 2);
        } else {
            textLine(tr(L"Pola do wstawienia"), d.left + 4, layout.takesTop() - (tv ? 14.0f : 12.0f), d.right - d.left, fine, muted);
            static const wchar_t* const names[mf::take_count] = {L"Opis", L"Producent", L"Wydawca", L"Data premiery", L"Gatunki"};
            for (int slot = 0; slot < mf::take_count; ++slot) {
                const auto r = layout.take(slot);
                const int id = mf::item::take_first + slot;
                const bool can = find.canTake(slot), on = can && find.take[static_cast<size_t>(slot)];
                if (find.hover == id && can) fill(r, 8, gray(1, 0.05f));
                const float cy = middle(r), box_half = tv ? 11.0f : 9.0f;
                const settings_ui::Rect check{r.left + 8, cy - box_half, r.left + 8 + 2 * box_half, cy + box_half};
                fill(check, 5, on ? uiAccent() : gray(1, can ? 0.12f : 0.05f));
                if (on) { // A tick.
                    line(check.left + box_half * 0.45f, cy, check.left + box_half * 0.85f, cy + box_half * 0.45f, gray(0.06f), 2.2f);
                    line(check.left + box_half * 0.85f, cy + box_half * 0.45f, check.right - box_half * 0.4f, cy - box_half * 0.5f, gray(0.06f), 2.2f);
                }
                const auto text_ink = can ? ink : gray(tv ? 0.4f : 0.45f);
                const float name_w = tv ? 140.0f : 112.0f, left = check.right + 12;
                textLine(tr(names[slot]), left, cy, name_w, body, text_ink);
                const auto& value = result->values[static_cast<size_t>(slot)];
                textLine(value.empty() ? std::wstring(L"—") : oneLine(value), left + name_w, cy, r.right - left - name_w - 8, fine, can ? muted : gray(0.35f));
                if (find.focus == id) focusRing(r, 8);
            }
        }
    }

    const auto* chosen = find.current();
    button(layout.insertButton(), mf::item::insert, 2, chosen && chosen->database() ? tr(L"Zidentyfikuj") : tr(L"Wstaw"), find.canApply());
    button(layout.backButton(), mf::item::back, 1, find.direct ? tr(L"Anuluj") : tr(L"Wróć"), !find.applying);
    if (ring) tvFocusRing(*ring, ring_radius, context_->meta_glide);
}
