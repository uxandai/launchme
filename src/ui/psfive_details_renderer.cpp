// PSfive → Szczegóły pod rzędem (src/app/psfive_details.cpp): the sheet rising over the game's
// background with its description and facts, the screenshots and similar games; a screenshot
// can be shown full screen. Geometry: psfive_details_layout.hpp.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "psfive_details_layout.hpp"
#include "../i18n/i18n.hpp"

#include <algorithm>

namespace {
using palette::gray;
using render_detail::smoothFade;
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
    const float sheet = psfive_details::sheetTop(height, rise);
    const float top = sheet - details.scroll; // The sections' origin.
    // The sheet: a dark veil that deepens under its first section, over the full-screen background.
    {
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stops;
        Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> veil;
        const D2D1_GRADIENT_STOP veil_stops[] = {{0, D2D1::ColorF(0.01f, 0.012f, 0.016f, 0.0f)}, {0.12f, D2D1::ColorF(0.01f, 0.012f, 0.016f, 0.80f)},
                                                 {1, D2D1::ColorF(0.01f, 0.012f, 0.016f, 0.92f)}};
        if (SUCCEEDED(target_->CreateGradientStopCollection(veil_stops, 3, stops.GetAddressOf())) &&
            SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, sheet - 90), D2D1::Point2F(0, height)),
                                                         stops.Get(), veil.GetAddressOf()))) {
            veil->SetOpacity(rise);
            target_->FillRectangle(D2D1::RectF(0, sheet - 90, width, height), veil.Get());
        }
    }
    target_->PushAxisAlignedClip(D2D1::RectF(0, sheet - 10, width, height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const float left = psfive_details::left, right = width - left;
    const auto focusFrame = [&](D2D1_RECT_F r, bool focused, float radius) {
        brush(focused ? gray(0.98f, rise) : gray(1, 0.10f * rise));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), solid_.Get(), focused ? 3.0f : 1.0f);
    };
    // The title, then the information: the description on the left, the facts on the right.
    textLine(details.title + (details.platform.empty() ? L"" : L"   ·   " + details.platform), left, top + 40, right - left,
             psfive_name_font_.Get(), gray(0.97f, rise));
    const float info_top = top + psfive_details::info_top;
    const float facts_left = std::max(left + 520.0f, width * 0.6f);
    const auto info = D2D1::RectF(left - 18, info_top - 14, right + 18, info_top + psfive_details::info_height);
    if (details.section == 0) {
        brush(gray(1, 0.05f * rise));
        target_->FillRoundedRectangle(D2D1::RoundedRect(info, 18, 18), solid_.Get());
        focusFrame(info, true, 18);
    }
    if (!details.description.empty())
        paragraph(details.description, left, info_top + 8, facts_left - left - 48, details_body_font_.Get(), gray(0.86f, rise), 9);
    else
        textLine(details.loading ? std::wstring(tr(L"Wczytywanie…")) : std::wstring(tr(L"Brak opisu tej gry.")), left, info_top + 24,
                 facts_left - left - 48, details_body_font_.Get(), gray(0.55f, rise));
    float fact_y = info_top + 22;
    for (const auto& [label, value] : details.facts) {
        if (fact_y > info_top + psfive_details::info_height - 30) break;
        textLine(label, facts_left, fact_y, 200, toast_sub_font_.Get(), gray(0.55f, rise));
        textLine(value, facts_left + 210, fact_y, right - facts_left - 210, menu_font_.Get(), gray(0.93f, rise));
        fact_y += 44;
    }
    // Screenshots: a strip of 16:9 pictures, the focused one framed.
    const float strip_width = width;
    if (details.shots || details.loading) {
        textLine(tr(L"Zrzuty ekranu"), left, top + psfive_details::shots_title + 14, 600, category_font_.Get(), gray(0.95f, rise));
        const auto first = psfive_details::firstShown(details.shot, details.shots, strip_width, psfive_details::shot_width + psfive_details::shot_gap);
        if (!details.shots) {
            const auto r = psfive_details::shot(0, 0, top);
            brush(gray(1, 0.05f * rise));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left, r.top, r.right, r.bottom), 14, 14), solid_.Get());
        }
        for (size_t i = first; i < details.shots && i < psfive_shots_.size(); ++i) {
            const auto r = psfive_details::shot(i, first, top);
            if (r.left > width) break;
            const auto rect = D2D1::RectF(r.left, r.top, r.right, r.bottom);
            brush(gray(1, 0.05f * rise));
            target_->FillRoundedRectangle(D2D1::RoundedRect(rect, 14, 14), solid_.Get());
            const auto& image = psfive_shots_[i];
            if (image.bitmap) {
                const auto size = image.bitmap->GetSize();
                const float scale = std::max((rect.right - rect.left) / size.width, (rect.bottom - rect.top) / size.height);
                const float w = size.width * scale, h = size.height * scale;
                const float x = (rect.left + rect.right - w) * 0.5f, y = (rect.top + rect.bottom - h) * 0.5f;
                const auto shape = D2D1::RoundedRect(rect, 14, 14);
                Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> clip;
                if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(shape, clip.GetAddressOf()))) {
                    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
                    target_->DrawBitmap(image.bitmap.Get(), D2D1::RectF(x, y, x + w, y + h), rise * reveal(image), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    target_->PopLayer();
                }
            }
            focusFrame(rect, details.section == 1 && details.shot == i, 14);
        }
    }
    // Similar games: square covers, the focused one's name under it.
    if (!details.similar_names.empty()) {
        textLine(tr(L"Podobne gry"), left, top + psfive_details::similar_title + 14, 600, category_font_.Get(), gray(0.95f, rise));
        const auto first = psfive_details::firstShown(details.similar, details.similar_names.size(), strip_width,
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
                text(details.similar_names[i], rect.left + 10, rect.top + 50, rect.right - rect.left - 20, 70, button_font_.Get(), gray(0.86f, rise));
            const bool focused = details.section == 2 && details.similar == i;
            if (shape) {
                brush(focused ? gray(0.98f, rise) : gray(1, 0.10f * rise));
                target_->DrawGeometry(shape.Get(), solid_.Get(), focused ? 3.0f : 1.0f);
            }
            if (focused) textLine(details.similar_names[i], rect.left, rect.bottom + 24, std::max(320.0f, rect.right - rect.left),
                                  menu_font_.Get(), gray(0.95f, rise));
        }
    }
    target_->PopAxisAlignedClip();
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
