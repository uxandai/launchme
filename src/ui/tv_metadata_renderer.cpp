// Renderer: Edytuj metadane in Tryb TV (from the game's menu). The desktop dialog's geometry
// (desktop_ui::MetaLayout with its TV measures, so the pointer and the pad behave the same) in the look of
// Dodaj / Edytuj grę: the same panel and heading, dark wells for the fields, glass buttons, one white ring
// that springs to the focused item (ViewContext::meta_glide), the game's poster on the right; Znajdź
// metadane / Identyfikuj grę first in the bottom row, white.
// Also the dialogs' shared parts: tvDialogFrame, tvFocusRing, desktopDialogSheet, dialogImage and their
// entrance and exit (dialogScrim, DialogMotion).
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "desktop_view.hpp"
#include "desktop_theme.hpp"

#include <algorithm>

using namespace render_detail;

namespace {
using palette::gray;
}

// ---- Shared by the dialogs ----------------------------------------------------------------------

void Renderer::dialogScrim(D2D1_COLOR_F color) {
    color.a *= std::clamp(dialog_motion_, 0.0f, 1.0f);
    if (color.a <= 0.001f) return;
    brush(color);
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
}

Renderer::DialogMotion::DialogMotion(Renderer& renderer, settings_ui::Rect panel) : renderer_(renderer) {
    const float shown = std::clamp(renderer.dialog_motion_, 0.0f, 1.0f);
    if (shown >= 0.999f || !renderer.target_) return;
    active_ = true;
    renderer.target_->GetTransform(&transform_);
    const float scale = 0.96f + 0.04f * shown;
    const D2D1_POINT_2F center{(panel.left + panel.right) * 0.5f, (panel.top + panel.bottom) * 0.5f};
    renderer.target_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, center) * D2D1::Matrix3x2F::Translation(0, 18 * (1 - shown)) * transform_);
    renderer.target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), shown),
                                nullptr);
}

Renderer::DialogMotion::~DialogMotion() {
    if (!active_) return;
    renderer_.target_->PopLayer();
    renderer_.target_->SetTransform(transform_);
}

void Renderer::tvDialogFrame(settings_ui::Rect p, const std::wstring& heading, const std::wstring& subtitle) {
    const auto shape = D2D1::RoundedRect(box(p), 22, 22);
    brush(gray(0.07f));
    target_->FillRoundedRectangle(shape, solid_.Get());
    if (panel_borders_) { brush(gray(1, 0.16f)); target_->DrawRoundedRectangle(shape, solid_.Get()); }
    textGroup(heading, subtitle, p.left + 40, p.top + 40, p.right - p.left - 80, settings_heading_font_.Get(), gray(0.96f), settings_body_font_.Get(),
              gray(0.58f), 1, true, heading_group_gap);
}

void Renderer::tvFocusRing(settings_ui::Rect r, float radius, const std::optional<ViewContext::GlideBox>& glide) {
    if (glide) r = {glide->left, glide->top, glide->right, glide->bottom}; // Springing towards r.
    brush(gray(0.96f));
    target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left - 4, r.top - 4, r.right + 4, r.bottom + 4), radius + 4, radius + 4), solid_.Get(), 2.5f);
}

void Renderer::desktopDialogSheet(settings_ui::Rect p, const std::wstring& title, bool close_hover) {
    desktop_modal_hotspots_.clear(); // Only the topmost dialog answers the pointer (its dim is the caller's: dialogScrim).
    desktopShadow(p, 14, 1.4f);
    desktopFill(p, 14, desktop_theme::current().card);
    desktopStroke(p, 14, gray(1, 0.1f));
    textLine(title, p.left + 22, p.top + 28, p.right - p.left - 82, desktop_title_font_.Get(), palette::text());
    const settings_ui::Rect c{p.right - 48, p.top + 10, p.right - 12, p.top + 46};
    if (close_hover) desktopFill(c, 8, gray(1, 0.08f));
    desktopGlyph(desktop_ui::glyph::close, (c.left + c.right) * 0.5f, (c.top + c.bottom) * 0.5f, 12, gray(close_hover ? 0.98f : 0.72f));
}

Renderer::Cover* Renderer::dialogImage(Cover& slot, const ImageBytes& bytes) {
    if (!bytes || bytes->empty()) { if (slot.loaded()) slot = {}; return nullptr; }
    if (slot.pixels != bytes) { slot = {}; slot.pixels = bytes; }
    if (slot.loaded() && !slot.bitmap) createBitmap(slot); // Uploaded once decoded.
    return slot.bitmap ? &slot : nullptr;
}

// ---- Edytuj metadane ----------------------------------------------------------------------------

void Renderer::drawTvMeta(const desktop_ui::MetaView& meta) {
    using desktop_ui::MetaItem;
    using desktop_ui::meta_fields;
    if (!meta.open) return;
    dialogScrim(gray(0, 0.80f));
    const desktop_ui::MetaLayout layout{viewport_.width, viewport_.height, true};
    const auto p = layout.panel();
    const DialogMotion motion(*this, p);
    const bool finding = meta.find.open;
    const std::wstring action = finding ? (meta.identify ? tr(L"Identyfikuj grę") : tr(L"Znajdź metadane")) : tr(L"Edytuj metadane");
    tvDialogFrame(p, meta.game, action);
    if (finding) { drawMetaFind(meta, true); return; }
    auto* const body = settings_body_font_.Get();
    auto* const fine = toast_sub_font_.Get();
    const auto middle = [](settings_ui::Rect r) { return (r.top + r.bottom) * 0.5f; };
    std::optional<settings_ui::Rect> ring;
    float ring_radius = 0;

    static const wchar_t* const labels[meta_fields] = {L"Tytuł", L"Opis", L"Producent", L"Wydawca", L"Data premiery", L"Gatunki", L"Czas przejścia (h)",
                                                        L"Czas gry"};
    static const wchar_t* const hints[meta_fields] = {L"Nazwa w bibliotece", L"Kilka zdań o grze", L"Studio", L"Firma wydająca",
                                                       L"np. 2024 albo 12.05.2024", L"np. Akcja, Przygodowa", L"np. 22", L"np. 12:30"};
    {
        // Beside Czas gry: the automatic value and that later play is added to the typed one.
        const auto f = layout.field(static_cast<int>(MetaItem::playtime));
        const float width = layout.fieldsRight() - f.right - 20;
        textLine(meta.playtime_note, f.right + 20, f.top + 14, width, fine, gray(0.62f));
        textLine(tr(L"Dalsza gra doliczy się do wpisanej wartości."), f.right + 20, f.top + 36, width, fine, gray(0.45f));
    }
    for (int i = 0; i < meta_fields; ++i) {
        const auto f = layout.field(i);
        const auto index = static_cast<size_t>(i);
        textLine(tr(labels[i]), f.left + 4, f.top - 13, f.right - f.left, fine, gray(0.58f));
        const bool focused = meta.focus == i;
        const float hover = meta.hover == i ? 1.0f : 0.0f;
        const auto& value = meta.fields[index];
        const float radius = i == 1 ? 16.0f : (f.bottom - f.top) * 0.5f;
        const auto shape = D2D1::RoundedRect(box(f), radius, radius);
        brush(focused && meta.selected ? uiAccent(0.22f) : gray(0.025f + 0.02f * hover));
        target_->FillRoundedRectangle(shape, solid_.Get());
        if (focused) { ring = f; ring_radius = radius; }
        const float inset = i == 1 ? 18.0f : (f.bottom - f.top) * 0.5f;
        target_->PushAxisAlignedClip(D2D1::RectF(f.left + 4, f.top + 1, f.right - 4, f.bottom - 1), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (value.empty() && !focused) {
            textLine(tr(hints[i]), f.left + inset, i == 1 ? f.top + 24 : middle(f), f.right - f.left - 2 * inset, body, gray(0.40f));
        } else if (i != 1) {
            const auto metrics = typeMetrics(body);
            fieldText(value, focused ? std::min(meta.caret, value.size()) : value.size(), focused && !meta.selected, f.left + inset,
                      middle(f) - metrics.line * 0.5f, f.right - f.left - 2 * inset, metrics.line, body, gray(0.95f));
        } else {
            // Opis: several lines; the caret is a bar in the text where it stands.
            auto text = value;
            size_t caret = std::min(meta.caret, text.size());
            if (focused && text.size() > 520 && caret > 420) { // Long: the part before the caret, so the caret shows.
                const size_t from = caret - 420;
                text = L"… " + text.substr(from);
                caret = caret - from + 2;
            }
            if (focused && !meta.selected) text.insert(caret, L"|");
            paragraph(text, f.left + inset, f.top + 14, f.right - f.left - 2 * inset, body, gray(0.95f), 6);
        }
        target_->PopAxisAlignedClip();
    }

    // The poster and, under it, what names the game.
    {
        const auto q = layout.poster();
        const float radius = 14;
        brush(gray(0.03f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(box(q), radius, radius), solid_.Get());
        if (auto* poster = dialogImage(meta_poster_, context_->meta_poster)) fillPoster(*poster, box(q), radius, 1.0f, false, nullptr);
        else {
            const auto alignment = body->GetTextAlignment();
            body->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            const float width = q.right - q.left - 32, height = paragraph(tr(L"Brak okładki"), q.left + 16, 0, width, body, gray(0), 2, false);
            paragraph(tr(L"Brak okładki"), q.left + 16, (q.top + q.bottom - height) * 0.5f, width, body, gray(0.46f), 2);
            body->SetTextAlignment(alignment);
        }
        brush(gray(1, 0.10f));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(box(q), radius, radius), solid_.Get());
        if (!meta.identity.empty()) paragraph(meta.identity, q.left + 4, q.bottom + 12, q.right - q.left - 8, fine, gray(0.55f), 2);
    }
    {
        // Znajdź metadane / Identyfikuj grę: the dialog's way to the databases, white, first in the row.
        const auto b = layout.button(MetaItem::find);
        const int id = static_cast<int>(MetaItem::find);
        const auto ink = drawButton(box(b), 1, 1, false, meta.hover == id, true);
        const std::wstring label = meta.identify ? tr(L"Identyfikuj grę") : tr(L"Znajdź metadane");
        const float label_w = std::min(textAdvance(label, body), b.right - b.left - 76), start = (b.left + b.right - label_w - 26) * 0.5f;
        drawSearchGlyph(start + 7, middle(b), 7, ink, 2.0f);
        textLine(label, start + 26, middle(b), label_w + 2, body, ink);
        if (meta.focus == id) { ring = b; ring_radius = (b.bottom - b.top) * 0.5f; }
    }

    const auto button = [&](MetaItem item, const std::wstring& label, bool enabled) {
        const auto r = layout.button(item);
        const int id = static_cast<int>(item);
        const auto ink = drawButton(box(r), 0, 1, false, meta.hover == id && enabled, enabled);
        textLine(label, r.left + 12, middle(r), r.right - r.left - 24, body, ink, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (meta.focus == id) { ring = r; ring_radius = (r.bottom - r.top) * 0.5f; }
    };
    button(MetaItem::restore, tr(L"Przywróć automatyczne"), meta.overridden);
    button(MetaItem::cancel, tr(L"Anuluj"), true);
    button(MetaItem::save, tr(L"Zapisz"), true);
    if (ring) tvFocusRing(*ring, ring_radius, context_->meta_glide);
}
