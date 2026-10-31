// PSfive → Szczegóły pod rzędem (src/app/psfive_details.cpp): the sheet rising over the game's
// background with its description and facts, the screenshots and similar games; a screenshot
// can be shown full screen. Geometry: psfive_details_layout.hpp.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "psfive_details_layout.hpp"
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

void Renderer::setPsfiveSimilar(size_t index, ImageBytes pixels) {
    if (index >= psfive_similar_.size() || !pixels) return;
    auto& image = psfive_similar_[index];
    if (image.pixels == pixels) return;
    image = {};
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

void Renderer::drawPsfiveDetails() {
    if (!context_->psfive_details) return;
    const auto& details = *context_->psfive_details;
    const float width = viewport_.width, height = viewport_.height;
    const float rise = smoothFade(details.progress);
    if (rise <= 0.001f) return;
    const float top = psfive_details::sheetTop(height, rise);
    // The sheet: a dark veil that deepens under its cards, over the full-screen background.
    {
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stops;
        Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> veil;
        const D2D1_GRADIENT_STOP veil_stops[] = {{0, D2D1::ColorF(0.01f, 0.012f, 0.016f, 0.0f)}, {0.14f, D2D1::ColorF(0.01f, 0.012f, 0.016f, 0.82f)},
                                                 {1, D2D1::ColorF(0.01f, 0.012f, 0.016f, 0.94f)}};
        if (SUCCEEDED(target_->CreateGradientStopCollection(veil_stops, 3, stops.GetAddressOf())) &&
            SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, top - 84), D2D1::Point2F(0, height)),
                                                         stops.Get(), veil.GetAddressOf()))) {
            veil->SetOpacity(rise);
            target_->FillRectangle(D2D1::RectF(0, top - 84, width, height), veil.Get());
        }
    }
    // Cards as on Szczegóły: frosted panels with a small capitalised heading.
    const auto card = [&](settings_ui::Rect r, const std::wstring& heading) {
        glassFill(glassPanel(D2D1::RoundedRect(box(r), 20, 20), 18), D2D1::ColorF(0.05f, 0.055f, 0.07f, 0.66f), rise);
        textLine(upper(heading), r.left + 24, r.top + 30, r.right - r.left - 48, friend_section_font_.Get(), gray(0.6f, rise));
    };
    const bool achievements = details.achievements_total > 0;
    const psfive_details::Cards cards{width, top, achievements};
    {
        const auto r = cards.description;
        card(r, tr(L"Opis"));
        const auto metrics = typeMetrics(details_body_font_.Get());
        const int lines = std::max(1, static_cast<int>((r.bottom - r.top - 56 - 20) / std::max(1.0f, metrics.line)));
        if (!details.description.empty())
            paragraph(details.description, r.left + 24, r.top + 52, r.right - r.left - 48, details_body_font_.Get(), D2D1::ColorF(0.80f, 0.82f, 0.85f, rise), lines);
        else if (details.loading)
            drawSkeleton(D2D1::RectF(r.left + 24, r.top + 52, r.right - 24, r.bottom - 20), std::min(lines, 5), metrics.line, rise);
        else
            paragraph(tr(L"Opis gry nie jest dostępny."), r.left + 24, r.top + 52, r.right - r.left - 48, details_body_font_.Get(), gray(0.6f, rise), 2);
    }
    {
        const auto r = cards.facts;
        card(r, tr(L"Informacje"));
        const auto& rows = details.facts;
        const float first = r.top + 66, step = rows.empty() ? 0 : std::min(34.0f, (r.bottom - 22 - first) / rows.size());
        const float label_width = std::min(130.0f, (r.right - r.left - 48) * 0.36f);
        for (size_t i = 0; i < rows.size(); ++i) {
            const float y = first + step * (static_cast<float>(i) + 0.5f) - 4;
            textLine(rows[i].first, r.left + 24, y, label_width, settings_body_font_.Get(), palette::muted(rise));
            textLine(rows[i].second, r.left + 24 + label_width + 12, y, r.right - r.left - 60 - label_width, settings_body_font_.Get(), gray(0.95f, rise));
        }
    }
    if (achievements) {
        // Unlocked of all, the share and its bar, as on Szczegóły.
        const auto r = cards.achievements;
        card(r, tr(L"Osiągnięcia"));
        const float share = std::clamp(static_cast<float>(details.achievements_unlocked) / details.achievements_total, 0.0f, 1.0f);
        const float value_y = r.top + 92;
        textLine(std::to_wstring(details.achievements_unlocked) + L" / " + std::to_wstring(details.achievements_total), r.left + 24, value_y,
                 r.right - r.left - 140, settings_heading_font_.Get(), gray(0.97f, rise));
        textLine(std::to_wstring(static_cast<int>(share * 100 + 0.5f)) + L"%", r.right - 124, value_y, 100, category_font_.Get(), gray(0.72f, rise),
                 DWRITE_TEXT_ALIGNMENT_TRAILING);
        const float bar = value_y + 40;
        brush(gray(1, 0.14f * rise));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + 24, bar, r.right - 24, bar + 6), 3, 3), solid_.Get());
        if (share > 0) {
            brush(uiAccent(rise));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + 24, bar, r.left + 24 + (r.right - r.left - 48) * share, bar + 6), 3, 3), solid_.Get());
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
    // Screenshots: a strip of 16:9 pictures.
    if (details.shots || details.loading) {
        textLine(upper(tr(L"Zrzuty ekranu")), psfive_details::left, top + psfive_details::shots_label, 600, friend_section_font_.Get(), gray(0.6f, rise));
        const auto first = psfive_details::firstShown(details.shot, details.shots, width, psfive_details::shot_width + psfive_details::shot_gap);
        if (!details.shots) {
            const auto r = psfive_details::shot(0, 0, top);
            drawSkeleton(D2D1::RectF(r.left, r.top, r.right, r.bottom), 1, r.bottom - r.top, rise);
        }
        for (size_t i = first; i < details.shots && i < psfive_shots_.size(); ++i) {
            const auto r = psfive_details::shot(i, first, top);
            if (r.left > width) break;
            const auto rect = D2D1::RectF(r.left, r.top, r.right, r.bottom);
            const auto shape = D2D1::RoundedRect(rect, 14, 14);
            brush(gray(1, 0.05f * rise));
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
                    target_->DrawBitmap(image.bitmap.Get(), D2D1::RectF(x, y, x + w, y + h), rise * reveal(image), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    target_->PopLayer();
                }
            }
            brush(gray(1, 0.10f * rise));
            target_->DrawRoundedRectangle(shape, solid_.Get(), 1.0f);
            if (details.section == 1 && details.shot == i) focus = std::pair{glided(rect), accentOf(nullptr)};
        }
    }
    // Similar games: square covers, the focused one's name under it.
    if (!details.similar_names.empty()) {
        textLine(upper(tr(L"Podobne gry")), psfive_details::left, top + psfive_details::similar_label, 600, friend_section_font_.Get(), gray(0.6f, rise));
        const auto first = psfive_details::firstShown(details.similar, details.similar_names.size(), width,
                                                       psfive_details::similar_size + psfive_details::similar_gap);
        for (size_t i = first; i < details.similar_names.size(); ++i) {
            const auto r = psfive_details::similar(i, first, top);
            if (r.left > width) break;
            const auto rect = D2D1::RectF(r.left, r.top, r.right, r.bottom);
            const auto shape = tileShape(rect, tile_shape_);
            brush(gray(1, 0.06f * rise));
            if (shape) target_->FillGeometry(shape.Get(), solid_.Get());
            const auto* image = i < psfive_similar_.size() ? &psfive_similar_[i] : nullptr;
            if (image && image->bitmap && shape) {
                const auto size = image->bitmap->GetSize();
                const float side = std::min(size.width, size.height);
                const float scale = (rect.right - rect.left) / side;
                const float w = size.width * scale, h = size.height * scale;
                const float x = rect.left - (w - (rect.right - rect.left)) * 0.5f, y = rect.top - (h - (rect.bottom - rect.top)) * 0.5f;
                target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), shape.Get()), nullptr);
                target_->DrawBitmap(image->bitmap.Get(), D2D1::RectF(x, y, x + w, y + h), rise * reveal(*image), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                target_->PopLayer();
            } else if (!image || !image->bitmap)
                text(details.similar_names[i], rect.left + 10, rect.top + 44, rect.right - rect.left - 20, 70, button_font_.Get(), gray(0.86f, rise));
            if (shape) { brush(gray(1, 0.10f * rise)); target_->DrawGeometry(shape.Get(), solid_.Get(), 1.0f); }
            if (details.section == 2 && details.similar == i) {
                focus = std::pair{glided(rect), accentOf(image)};
                textLine(details.similar_names[i], rect.left, rect.bottom + 24, std::max(360.0f, rect.right - rect.left), menu_font_.Get(), gray(0.95f, rise));
            }
        }
    }
    if (focus) {
        const auto& [rect, tint] = *focus;
        Microsoft::WRL::ComPtr<ID2D1Geometry> outline;
        if (details.section == 2) outline = tileShape(rect, tile_shape_);
        else {
            Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> rounded;
            if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect, 14, 14), rounded.GetAddressOf()))) outline = rounded;
        }
        if (outline) drawCoverOutline(outline.Get(), tint, rise, true);
    }
    // A screenshot full screen: its full picture (the strip's while it loads), contained.
    if (details.viewer) {
        brush(D2D1::ColorF(0, 0, 0, 0.94f));
        target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
        const Cover* image = psfive_shot_full_.bitmap ? &psfive_shot_full_ : details.shot < psfive_shots_.size() ? &psfive_shots_[details.shot] : nullptr;
        if (image && image->bitmap) {
            const auto size = image->bitmap->GetSize();
            const float scale = std::min((width - 120) / size.width, (height - 140) / size.height);
            const float w = size.width * scale, h = size.height * scale;
            const float x = (width - w) * 0.5f, y = (height - h) * 0.5f - 10;
            target_->DrawBitmap(image->bitmap.Get(), D2D1::RectF(x, y, x + w, y + h), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        textLine(std::to_wstring(details.shot + 1) + L" / " + std::to_wstring(details.shots), 0, height - 46, width, menu_font_.Get(), gray(0.7f),
                 DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}