// Szczegóły → Zrzuty ekranu i podobne gry (src/app/details_media.cpp): the game's screenshots and
// similar games under its cards. Tryb TV: two strips sliding as PSfive's do, the focused item in
// the tiles' outline (geometry: game_details_layout.hpp). Desktop: two more sections of the sheet,
// pictures wrapped into rows. The pictures are PSfive's slots (psfive_shots_, psfive_similar_).
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "desktop_theme.hpp"

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
using palette::gray;
using render_detail::color;
using render_detail::upper;
}

void Renderer::drawDetailsMedia(const details_layout::Layout& layout, const details_layout::Actions& actions, int focus) {
    const auto* media = context_->details_media ? &*context_->details_media : nullptr;
    if (!media) return;
    const float width = viewport_.width, height = viewport_.height;
    const float left = layout.left();
    // A picture covering its rounded box; a cover placed by its crop focus inside the tile's shape.
    const auto picture = [&](const Cover& image, D2D1_RECT_F rect, ID2D1Geometry* clip, float alpha, bool square) {
        if (!image.bitmap || !clip) return;
        const auto size = image.bitmap->GetSize();
        const float cell_w = rect.right - rect.left, cell_h = rect.bottom - rect.top;
        const float scale = square ? cell_w / std::min(size.width, size.height) : std::max(cell_w / size.width, cell_h / size.height);
        const float w = size.width * scale, h = size.height * scale;
        const float x = square ? rect.left + std::clamp(cell_w * 0.5f - image.focus.x * w, std::min(cell_w - w, 0.0f), 0.0f) : (rect.left + rect.right - w) * 0.5f;
        const float y = square ? rect.top + std::clamp(cell_h * 0.5f - image.focus.y * h, std::min(cell_h - h, 0.0f), 0.0f) : (rect.top + rect.bottom - h) * 0.5f;
        target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip), nullptr);
        target_->DrawBitmap(image.bitmap.Get(), D2D1::RectF(x, y, x + w, y + h), alpha * reveal(image), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        target_->PopLayer();
    };
    // Past the left margin an item fades over one step, as PSfive's strips do; scrolled up past the
    // top, it fades too.
    const auto fade = [&](float x, float step) { return std::clamp((x - (left - step)) / step, 0.0f, 1.0f); };
    const auto top_fade = [&](float y) { return std::clamp((y - 10) / 80, 0.0f, 1.0f); };
    ComPtr<ID2D1Geometry> focus_shape;
    D2D1_COLOR_F focus_tint = color(0.94f, 0.96f, 0.97f);
    if (layout.hasShots(actions)) {
        textLine(upper(tr(L"Zrzuty ekranu")), left, layout.shotsLabel(), 600, friend_section_font_.Get(), gray(0.6f, top_fade(layout.shotsLabel())));
        if (!actions.shots) {
            const auto r = layout.shot(actions, 0);
            if (r.top < height) drawSkeleton(D2D1::RectF(r.left, r.top, r.right, r.bottom), 1, r.bottom - r.top);
        }
        const float step = details_layout::Layout::shot_width + details_layout::Layout::strip_gap;
        for (size_t i = 0; i < actions.shots && i < psfive_shots_.size(); ++i) {
            const auto r = layout.shot(actions, i);
            if (r.left > width || r.top > height) break;
            const float shown = fade(r.left, step) * top_fade(r.top);
            if (shown <= 0.003f) continue;
            const auto rect = D2D1::RectF(r.left, r.top, r.right, r.bottom);
            ComPtr<ID2D1RoundedRectangleGeometry> shape;
            if (FAILED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(rect, 14, 14), shape.GetAddressOf()))) continue;
            brush(gray(1, 0.05f * shown));
            target_->FillGeometry(shape.Get(), solid_.Get());
            picture(psfive_shots_[i], rect, shape.Get(), shown, false);
            brush(gray(1, 0.10f * shown));
            target_->DrawGeometry(shape.Get(), solid_.Get(), 1.0f);
            if (focus == details_layout::shot_action + static_cast<int>(i)) focus_shape = shape;
        }
    }
    if (actions.similar) {
        textLine(upper(tr(L"Podobne gry")), left, layout.similarLabel(actions), 600, friend_section_font_.Get(), gray(0.6f, top_fade(layout.similarLabel(actions))));
        const float step = details_layout::Layout::similar_size + details_layout::Layout::strip_gap;
        for (size_t i = 0; i < actions.similar; ++i) {
            const auto r = layout.similar(actions, i);
            if (r.left > width || r.top > height) break;
            const float shown = fade(r.left, step) * top_fade(r.top);
            if (shown <= 0.003f) continue;
            const auto rect = D2D1::RectF(r.left, r.top, r.right, r.bottom);
            const auto shape = tileShape(rect, tile_shape_);
            if (!shape) continue;
            brush(gray(1, 0.06f * shown));
            target_->FillGeometry(shape.Get(), solid_.Get());
            const auto* image = i < psfive_similar_.size() ? &psfive_similar_[i] : nullptr;
            if (image && image->bitmap) picture(*image, rect, shape.Get(), shown, true);
            else if (i < media->similar_names.size())
                text(media->similar_names[i], rect.left + 10, rect.top + 44, rect.right - rect.left - 20, 70, button_font_.Get(), gray(0.86f, shown));
            brush(gray(1, 0.10f * shown));
            target_->DrawGeometry(shape.Get(), solid_.Get(), 1.0f);
            if (focus == details_layout::similar_action + static_cast<int>(i)) {
                focus_shape = shape;
                if (cover_accent_) focus_tint = color(((cover_accent_ >> 16) & 255) / 255.0f, ((cover_accent_ >> 8) & 255) / 255.0f, (cover_accent_ & 255) / 255.0f);
                else if (image && image->bitmap) focus_tint = image->accent;
            }
        }
    }
    if (focus_shape) drawCoverOutline(focus_shape.Get(), focus_tint, 1, true);
}

float Renderer::drawDesktopDetailsMedia(float left, float right, float y, const desktop_ui::SheetView& view) {
    using desktop_ui::SheetHit;
    using desktop_ui::DetailsExtra;
    const auto* media = context_->details_media ? &*context_->details_media : nullptr;
    if (!media) return y;
    const float gap = 12;
    const auto hovered = [&](SheetHit hit) { return view.hover == hit; };
    if (media->shots || media->loading) {
        y += 18;
        textLine(upper(tr(L"Zrzuty ekranu")), left, y, right - left, desktop_label_font_.Get(), gray(0.52f));
        y += 16;
        // As many 16:9 pictures in a row as fit at about 300 pixels wide.
        const int columns = std::max(2, static_cast<int>((right - left + gap) / (300 + gap)));
        const float w = (right - left - gap * (columns - 1)) / columns, h = w * 9 / 16;
        if (!media->shots) { drawSkeleton(D2D1::RectF(left, y, left + w, y + h), 1, h); y += h + gap; }
        for (size_t i = 0; i < media->shots && i < psfive_shots_.size(); ++i) {
            const int column = static_cast<int>(i) % columns;
            const float x = left + column * (w + gap), top = y + static_cast<float>(i / columns) * (h + gap);
            const SheetHit hit{SheetHit::Kind::tile, static_cast<int>(i), static_cast<int>(DetailsExtra::shot)};
            const settings_ui::Rect r{x, top, x + w, top + h};
            desktopFill(r, 10, gray(1, 0.04f));
            ComPtr<ID2D1RoundedRectangleGeometry> clip;
            const auto& image = psfive_shots_[i];
            if (image.bitmap && SUCCEEDED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(r.left, r.top, r.right, r.bottom), 10, 10), clip.GetAddressOf()))) {
                const auto size = image.bitmap->GetSize();
                const float scale = std::max(w / size.width, h / size.height);
                const float iw = size.width * scale, ih = size.height * scale;
                target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
                target_->DrawBitmap(image.bitmap.Get(), D2D1::RectF(x + (w - iw) * 0.5f, top + (h - ih) * 0.5f, x + (w + iw) * 0.5f, top + (h + ih) * 0.5f),
                                    reveal(image), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                target_->PopLayer();
            }
            desktopStroke(r, 10, hovered(hit) ? uiAccent(0.9f) : gray(1, 0.06f), hovered(hit) ? 2.0f : 1.0f);
            desktopHot(hit, r);
        }
        if (media->shots) y += static_cast<float>((media->shots + columns - 1) / columns) * (h + gap);
        y += 12;
    }
    if (!media->similar_names.empty()) {
        y += 18;
        textLine(upper(tr(L"Podobne gry")), left, y, right - left, desktop_label_font_.Get(), gray(0.52f));
        y += 16;
        const int columns = std::max(3, static_cast<int>((right - left + gap) / (140 + gap)));
        const float size = (right - left - gap * (columns - 1)) / columns;
        for (size_t i = 0; i < media->similar_names.size(); ++i) {
            const int column = static_cast<int>(i) % columns;
            const float x = left + column * (size + gap), top = y + static_cast<float>(i / columns) * (size + 30 + gap);
            const SheetHit hit{SheetHit::Kind::tile, static_cast<int>(i), static_cast<int>(DetailsExtra::similar)};
            const settings_ui::Rect r{x, top, x + size, top + size};
            desktopFill(r, 10, gray(1, 0.04f));
            const auto* image = i < psfive_similar_.size() ? &psfive_similar_[i] : nullptr;
            ComPtr<ID2D1RoundedRectangleGeometry> clip;
            if (image && image->bitmap && SUCCEEDED(factory_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(r.left, r.top, r.right, r.bottom), 10, 10), clip.GetAddressOf()))) {
                const auto pixels = image->bitmap->GetSize();
                const float scale = size / std::min(pixels.width, pixels.height);
                const float w = pixels.width * scale, h = pixels.height * scale;
                const float ix = x + std::clamp(size * 0.5f - image->focus.x * w, std::min(size - w, 0.0f), 0.0f);
                const float iy = top + std::clamp(size * 0.5f - image->focus.y * h, std::min(size - h, 0.0f), 0.0f);
                target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clip.Get()), nullptr);
                target_->DrawBitmap(image->bitmap.Get(), D2D1::RectF(ix, iy, ix + w, iy + h), reveal(*image), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                target_->PopLayer();
            }
            desktopStroke(r, 10, hovered(hit) ? uiAccent(0.9f) : gray(1, 0.06f), hovered(hit) ? 2.0f : 1.0f);
            textLine(media->similar_names[i], x, top + size + 16, size, desktop_small_font_.Get(), hovered(hit) ? palette::text() : palette::muted());
            desktopHot(hit, {x, top, x + size, top + size + 26});
        }
        y += static_cast<float>((media->similar_names.size() + columns - 1) / columns) * (size + 30 + gap);
    }
    return y;
}
