// PSfive → Szczegóły pod rzędem (src/app/psfive_details.cpp): the sheet rising over the game's
// background with its description and facts, the screenshots and similar games; a screenshot
// can be shown full screen. Geometry: psfive_details_layout.hpp.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "psfive_details_layout.hpp"
#include "psfive_layout.hpp"
#include "../i18n/i18n.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace {
using palette::gray;
using render_detail::box;
using render_detail::color;
using render_detail::smoothFade;
using render_detail::upper;
}

void Renderer::resetPsfiveDetails(size_t shots, size_t similar) {
    psfive_shots_.clear(); psfive_shots_.resize(shots);
    psfive_similar_.clear(); psfive_similar_.resize(similar);
}

void Renderer::setPsfiveShot(size_t index, ImageBytes pixels) {
    if (index >= psfive_shots_.size()) return;
    auto& image = psfive_shots_[index];
    image = {};
    image.hero = true; // Kept whole (16:9), no square crop or accent.
    image.pixels = std::move(pixels);
    createBitmap(image);
}

void Renderer::setPsfiveSimilar(size_t index, ImageBytes pixels, CropFocus focus) {
    if (index >= psfive_similar_.size() || !pixels) return;
    auto& image = psfive_similar_[index];
    image.focus = focus; // The game's own square window (Kadruj, or the text-aware crop), as on its tile.
    if (image.pixels == pixels) return;
    image = {};
    image.focus = focus;
    image.pixels = std::move(pixels);
    image.short_edge = coverEdge();
    createBitmap(image);
}

bool Renderer::hasPsfiveSimilar(size_t index) const { return index < psfive_similar_.size() && psfive_similar_[index].loaded(); }

void Renderer::setPsfiveShotFull(ImageBytes pixels) {
    psfive_shot_full_ = {};
    if (!pixels) return;
    psfive_shot_full_.hero = true;
    psfive_shot_full_.pixels = std::move(pixels);
    createBitmap(psfive_shot_full_);
}

// Under the sheet, drawn before the logo and buttons (they stand over it): a dark veil that deepens
// toward the bottom. While the sheet only peeks it starts high and darkens slowly; risen, it is
// short and dense over the sheet's own top.
void Renderer::drawPsfiveDetailsVeil() {
    if (!context_->psfive_details) return;
    const float rise = smoothFade(context_->psfive_details->progress), hub = psfive_layout::hubEase(context_->psfive_hub);
    const float opacity = std::max(rise, hub);
    if (opacity <= 0.001f) return;
    const float width = viewport_.width, height = viewport_.height;
    const float top = psfive_details::sheetTop(height, hub, rise);
    const float reach = std::lerp(420.0f, 84.0f, rise), start = top - reach;
    const float span = std::max(1.0f, height - start), knee = std::clamp((reach + 30) / span, 0.0f, 1.0f);
    const auto ink = [](float a) { return D2D1::ColorF(0.01f, 0.012f, 0.016f, a); };
    const D2D1_GRADIENT_STOP stops[] = {{0, ink(0)}, {knee * 0.45f, ink(0.22f)}, {knee * 0.8f, ink(0.58f)}, {knee, ink(0.8f)}, {1, ink(0.94f)}};
    Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> collection;
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> veil;
    if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 5, collection.GetAddressOf())) &&
        SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, start), D2D1::Point2F(0, height)),
                                                     collection.Get(), veil.GetAddressOf()))) {
        veil->SetOpacity(opacity);
        target_->FillRectangle(D2D1::RectF(0, start, width, height), veil.Get());
    }
}

void Renderer::drawPsfiveDetails() {
    if (!context_->psfive_details) return;
    const auto& details = *context_->psfive_details;
    const float width = viewport_.width, height = viewport_.height;
    const float rise = smoothFade(details.progress), hub = psfive_layout::hubEase(context_->psfive_hub);
    const float sheet = std::max(rise, hub); // The cards: peeking under the buttons (the hub) or risen.
    if (sheet <= 0.001f) return;
    const float top = psfive_details::sheetTop(height, hub, rise) - details.scroll;
    const float left = psfive_layout::tabs_left;
    // Activity cards, as on the PS5's game hub: frosted panels of one size with a small heading.
    const auto card = [&](settings_ui::Rect r, const std::wstring& heading, float alpha) {
        glassFill(glassPanel(D2D1::RoundedRect(box(r), 20, 20), 18), D2D1::ColorF(0.05f, 0.055f, 0.07f, 0.66f), alpha);
        textLine(upper(heading), r.left + 24, r.top + 30, r.right - r.left - 48, friend_section_font_.Get(), gray(0.6f, alpha));
    };
    for (size_t i = 0; i < details.cards.size(); ++i) {
        const auto r = psfive_details::card(i, width, top, left);
        const float alpha = sheet * psfive_details::topFade(r.top);
        if (alpha <= 0.003f || r.top > height) continue;
        switch (static_cast<psfive_details::Card>(details.cards[i])) {
        case psfive_details::Card::achievements: {
            // A ring of the share with its percent inside, the count beside it.
            card(r, tr(L"Osiągnięcia"), alpha);
            const float share = details.achievements_total ? std::clamp(static_cast<float>(details.achievements_unlocked) / details.achievements_total, 0.0f, 1.0f) : 0.0f;
            const float radius = 50, cx = r.left + 24 + radius, cy = (r.top + 52 + r.bottom - 18) * 0.5f;
            brush(gray(1, 0.14f * alpha));
            target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), solid_.Get(), 7);
            if (share > 0.001f) {
                Microsoft::WRL::ComPtr<ID2D1PathGeometry> arc;
                Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                if (share >= 0.999f) {
                    brush(uiAccent(alpha));
                    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), solid_.Get(), 7);
                } else if (SUCCEEDED(factory_->CreatePathGeometry(arc.GetAddressOf())) && SUCCEEDED(arc->Open(sink.GetAddressOf()))) {
                    const float angle = share * 6.2831853f;
                    sink->BeginFigure(D2D1::Point2F(cx, cy - radius), D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + radius * std::sin(angle), cy - radius * std::cos(angle)), D2D1::SizeF(radius, radius), 0,
                                                  D2D1_SWEEP_DIRECTION_CLOCKWISE, share > 0.5f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    if (SUCCEEDED(sink->Close())) {
                        Microsoft::WRL::ComPtr<ID2D1StrokeStyle> round;
                        factory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND), nullptr, 0, round.GetAddressOf());
                        brush(uiAccent(alpha));
                        target_->DrawGeometry(arc.Get(), solid_.Get(), 7, round.Get());
                    }
                }
            }
            textLine(std::to_wstring(static_cast<int>(share * 100 + 0.5f)) + L"%", cx - radius, cy, radius * 2, category_font_.Get(), gray(0.97f, alpha),
                     DWRITE_TEXT_ALIGNMENT_CENTER);
            const float text_left = cx + radius + 24;
            textLine(std::to_wstring(details.achievements_unlocked) + L" / " + std::to_wstring(details.achievements_total), text_left, cy - 14,
                     r.right - 20 - text_left, settings_heading_font_.Get(), gray(0.97f, alpha));
            textLine(tr(L"odblokowane"), text_left, cy + 20, r.right - 20 - text_left, settings_body_font_.Get(), palette::muted(alpha));
            break;
        }
        case psfive_details::Card::playtime: {
            card(r, tr(L"Czas gry"), alpha);
            textLine(details.playtime.empty() ? std::wstring(L"—") : details.playtime, r.left + 24, r.top + 104, r.right - r.left - 48,
                     settings_heading_font_.Get(), gray(0.97f, alpha));
            if (!details.last_played.empty()) {
                textLine(tr(L"Ostatnio"), r.left + 24, r.bottom - 58, r.right - r.left - 48, settings_body_font_.Get(), palette::muted(alpha));
                textLine(details.last_played, r.left + 24, r.bottom - 32, r.right - r.left - 48, settings_body_font_.Get(), gray(0.92f, alpha));
            }
            break;
        }
        case psfive_details::Card::description: {
            card(r, tr(L"Opis"), alpha);
            const auto metrics = typeMetrics(details_body_font_.Get());
            const int lines = std::max(1, static_cast<int>((r.bottom - r.top - 56 - 20) / std::max(1.0f, metrics.line)));
            if (!details.description.empty())
                paragraph(details.description, r.left + 24, r.top + 52, r.right - r.left - 48, details_body_font_.Get(), D2D1::ColorF(0.80f, 0.82f, 0.85f, alpha), lines);
            else if (details.loading)
                drawSkeleton(D2D1::RectF(r.left + 24, r.top + 52, r.right - 24, r.bottom - 20), std::min(lines, 5), metrics.line, alpha);
            else
                paragraph(tr(L"Opis gry nie jest dostępny."), r.left + 24, r.top + 52, r.right - r.left - 48, details_body_font_.Get(), gray(0.6f, alpha), 2);
            break;
        }
        case psfive_details::Card::facts: {
            card(r, tr(L"Informacje"), alpha);
            const auto& rows = details.facts;
            const float first = r.top + 58, step = 34;
            const size_t fit = static_cast<size_t>(std::max(0.0f, (r.bottom - 14 - first) / step));
            const float label_width = std::min(120.0f, (r.right - r.left - 48) * 0.38f);
            for (size_t row = 0; row < rows.size() && row < fit; ++row) {
                const float y = first + step * (static_cast<float>(row) + 0.5f);
                textLine(rows[row].first, r.left + 24, y, label_width, settings_body_font_.Get(), palette::muted(alpha));
                textLine(rows[row].second, r.left + 24 + label_width + 10, y, r.right - r.left - 58 - label_width, settings_body_font_.Get(), gray(0.95f, alpha));
            }
            break;
        }
        }
    }
    // The focused item's outline: the tiles' own (glow, pulse or the travelling light), gliding between items.
    const auto accentOf = [&](const Cover* image) -> D2D1_COLOR_F {
        if (cover_accent_) return color(((cover_accent_ >> 16) & 255) / 255.0f, ((cover_accent_ >> 8) & 255) / 255.0f, (cover_accent_ & 255) / 255.0f);
        return image && image->bitmap && !image->hero ? image->accent : color(0.94f, 0.96f, 0.97f);
    };
    std::optional<std::pair<D2D1_RECT_F, D2D1_COLOR_F>> focus;
    const auto glided = [&](D2D1_RECT_F fallback) {
        return details.focus_glide ? D2D1::RectF(details.focus_left, details.focus_top, details.focus_right, details.focus_bottom) : fallback;
    };
    // Screenshots: a strip of 16:9 pictures, sliding as tiles do; one leaving past the margin fades.
    const float shot_step = psfive_details::shot_width + psfive_details::shot_gap;
    const float similar_step = psfive_details::similar_size + psfive_details::similar_gap;
    if (details.shots || details.loading) {
        textLine(upper(tr(L"Zrzuty ekranu")), left, top + psfive_details::shots_label, 600, friend_section_font_.Get(),
                 gray(0.6f, rise * psfive_details::topFade(top + psfive_details::shots_label - 10)));
        if (!details.shots) {
            const auto r = psfive_details::shot(0, 0, top, left);
            drawSkeleton(D2D1::RectF(r.left, r.top, r.right, r.bottom), 1, r.bottom - r.top, rise);
        }
        for (size_t i = 0; i < details.shots && i < psfive_shots_.size(); ++i) {
            const auto r = psfive_details::shot(i, details.shots_scroll, top, left);
            if (r.left > width) break;
            const float shown = rise * psfive_details::edgeFade(r.left, shot_step, left) * psfive_details::topFade(r.top);
            if (shown <= 0.003f) continue;
            const auto rect = D2D1::RectF(r.left, r.top, r.right, r.bottom);
            const auto shape = D2D1::RoundedRect(rect, 14, 14);
            brush(gray(1, 0.05f * shown));
            target_->FillRoundedRectangle(shape, solid_.Get());
            const auto& image = psfive_shots_[i];
            if (image.bitmap) {
                const auto size = image.bitmap->GetSize();
                const float scale = std::max((rect.right - rect.left) / size.width, (rect.bottom - rect.top) / size.height);
                const float w = size.width * scale, h = size.height * scale;
                const float x = (rect.left + rect.right - w) * 0.5f, y = (rect.top + rect.bottom - h) * 0.5f;
                Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> clip;
                if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(shape, clip.GetAddressOf()))) {
                    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
                    target_->DrawBitmap(image.bitmap.Get(), D2D1::RectF(x, y, x + w, y + h), shown * reveal(image), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    target_->PopLayer();
                }
            }
            brush(gray(1, 0.10f * shown));
            target_->DrawRoundedRectangle(shape, solid_.Get(), 1.0f);
            if (details.section == 1 && details.shot == i) focus = std::pair{glided(rect), accentOf(nullptr)};
        }
    }
    // Similar games: square covers sliding the same way (a name only inside a cover that has no picture yet).
    if (!details.similar_names.empty()) {
        textLine(upper(tr(L"Podobne gry")), left, top + psfive_details::similar_label - psfive_details::similarShift(details.similar_up), 600, friend_section_font_.Get(),
                 gray(0.6f, rise * psfive_details::topFade(top + psfive_details::similar_label - psfive_details::similarShift(details.similar_up) - 10)));
        for (size_t i = 0; i < details.similar_names.size(); ++i) {
            const auto r = psfive_details::similar(i, details.similar_scroll, top, left, details.similar_up);
            if (r.left > width) break;
            const float shown = rise * psfive_details::edgeFade(r.left, similar_step, left) * psfive_details::topFade(r.top);
            if (shown <= 0.003f) continue;
            const auto rect = D2D1::RectF(r.left, r.top, r.right, r.bottom);
            const auto shape = tileShape(rect, tile_shape_);
            brush(gray(1, 0.06f * shown));
            if (shape) target_->FillGeometry(shape.Get(), solid_.Get());
            const auto* image = i < psfive_similar_.size() ? &psfive_similar_[i] : nullptr;
            if (image && image->bitmap && shape) {
                const auto size = image->bitmap->GetSize();
                const float side = std::min(size.width, size.height);
                const float scale = (rect.right - rect.left) / side;
                const float w = size.width * scale, h = size.height * scale;
                const float cell_w = rect.right - rect.left, cell_h = rect.bottom - rect.top;
                // The cover's crop focus (Kadruj or the automatic one), as the tiles place it.
                const float x = rect.left + std::clamp(cell_w * 0.5f - image->focus.x * w, std::min(cell_w - w, 0.0f), 0.0f);
                const float y = rect.top + std::clamp(cell_h * 0.5f - image->focus.y * h, std::min(cell_h - h, 0.0f), 0.0f);
                target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), shape.Get()), nullptr);
                target_->DrawBitmap(image->bitmap.Get(), D2D1::RectF(x, y, x + w, y + h), shown * reveal(*image), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                target_->PopLayer();
            } else if (!image || !image->bitmap)
                text(details.similar_names[i], rect.left + 10, rect.top + 44, rect.right - rect.left - 20, 70, button_font_.Get(), gray(0.86f, shown));
            if (shape) { brush(gray(1, 0.10f * shown)); target_->DrawGeometry(shape.Get(), solid_.Get(), 1.0f); }
            if (details.section == 2 && details.similar == i) {
                focus = std::pair{glided(rect), accentOf(image)}; // No caption under it: the covers speak.
            }
        }
    }
    if (details.section == 0 && rise > 0.5f && details.card < details.cards.size()) {
        const auto r = psfive_details::card(details.card, width, top, left);
        focus = std::pair{glided(box(r)), accentOf(nullptr)};
    }
    if (focus) {
        const auto& [rect, tint] = *focus;
        Microsoft::WRL::ComPtr<ID2D1Geometry> outline;
        if (details.section == 2) outline = tileShape(rect, tile_shape_);
        else {
            const float corner = details.section == 0 ? 20.0f : 14.0f;
            Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> rounded;
            if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect, corner, corner), rounded.GetAddressOf()))) outline = rounded;
        }
        if (outline) drawCoverOutline(outline.Get(), tint, rise, true);
    }
}

// Opis opened: the whole description over the darkened screen, the game's name over it.
void Renderer::drawPsfiveTextView(const std::wstring& title, const std::wstring& text) {
    const float width = viewport_.width, height = viewport_.height;
    brush(D2D1::ColorF(0.01f, 0.012f, 0.016f, 0.94f));
    target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    const float column = std::min(1000.0f, width - 2 * 160), x = (width - column) * 0.5f;
    textLine(title, x, 150, column, settings_heading_font_.Get(), gray(0.97f));
    textLine(upper(tr(L"Opis")), x, 112, column, friend_section_font_.Get(), gray(0.6f));
    const auto metrics = typeMetrics(details_body_font_.Get());
    const int lines = std::max(1, static_cast<int>((height - 200 - 120) / std::max(1.0f, metrics.line * 1.0f)));
    paragraph(text.empty() ? std::wstring(tr(L"Opis gry nie jest dostępny.")) : text, x, 200, column, details_body_font_.Get(),
              D2D1::ColorF(0.86f, 0.88f, 0.9f), lines);
}

// A screenshot full screen: its full picture (the strip's while it loads), contained.
void Renderer::drawShotViewer(size_t shot, size_t shots) {
    const float width = viewport_.width, height = viewport_.height;
    brush(D2D1::ColorF(0, 0, 0, 0.94f));
    target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    const Cover* image = psfive_shot_full_.bitmap ? &psfive_shot_full_ : shot < psfive_shots_.size() ? &psfive_shots_[shot] : nullptr;
    if (image && image->bitmap) {
        const auto size = image->bitmap->GetSize();
        const float scale = std::min((width - 120) / size.width, (height - 140) / size.height);
        const float w = size.width * scale, h = size.height * scale;
        const float x = (width - w) * 0.5f, y = (height - h) * 0.5f - 10;
        target_->DrawBitmap(image->bitmap.Get(), D2D1::RectF(x, y, x + w, y + h), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    textLine(std::to_wstring(shot + 1) + L" / " + std::to_wstring(shots), 0, height - 46, width, menu_font_.Get(), gray(0.7f),
             DWRITE_TEXT_ALIGNMENT_CENTER);
}