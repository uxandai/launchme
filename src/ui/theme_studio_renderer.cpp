// Dostosuj wygląd: the tabs, the options as pictures, the live preview and the saved looks.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "palette.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>

using namespace render_detail;
using palette::gray;
using settings_ui::Id;
using theme_ui::Kind;
using theme_ui::Look;

IDWriteTextFormat* Renderer::fontSample(size_t index) {
    index = std::min(index, font_samples_.size() - 1);
    auto& format = font_samples_[index];
    if (!format) { // The same builder as the interface's fonts, for this family.
        const auto keep = font_family_;
        font_family_ = settings_ui::fonts[index];
        font(format, 30, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        font_family_ = keep;
    }
    return format.Get();
}

namespace {
D2D1_COLOR_F colorOf(uint32_t rgb, float alpha = 1) {
    return D2D1::ColorF(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, alpha);
}
}

// One picture of an option: what the choice looks like, a caption under it.
void Renderer::drawThemeCard(const theme_ui::Option& option, int index, float left, float top, float right, float bottom,
                             bool chosen, bool focused, bool hovered) {
    const auto& card = option.cards[static_cast<size_t>(index)];
    const bool swatch = option.look == Look::swatch;
    const auto frame = D2D1::RoundedRect(D2D1::RectF(left, top, right, bottom), 16, 16);
    if (!swatch) { // The same card as the settings' tiles: a faint panel, the chosen one lighter.
        brush(gray(1, chosen ? 0.12f : hovered ? 0.085f : 0.042f));
        target_->FillRoundedRectangle(frame, solid_.Get());
        brush(gray(1, chosen ? 0.24f : 0.08f));
        target_->DrawRoundedRectangle(frame, solid_.Get(), 1.0f);
    } else if (hovered) {
        brush(gray(1, 0.07f)); target_->FillRoundedRectangle(frame, solid_.Get());
    }
    const float cx = (left + right) * 0.5f;
    const float art_l = left + (swatch ? 8 : 14), art_r = right - (swatch ? 8 : 14), art_t = top + (swatch ? 8 : 12), art_b = bottom - (swatch ? 8 : 34), art_cy = (art_t + art_b) * 0.5f;
    const float art_w = art_r - art_l, art_h = art_b - art_t;
    const auto ink = gray(1, chosen ? 0.95f : 0.6f);
    const auto soft = gray(1, chosen ? 0.35f : 0.2f);
    const int count = static_cast<int>(option.cards.size());
    const float pulse = 0.5f + 0.5f * std::sin(elapsed_ * 3.0f);
    switch (option.look) {
    case Look::layout:
        drawLayoutScene(index, D2D1::RectF(left + 8, top + 8, right - 8, bottom - 32), chosen ? elapsed_ : 0.0f, chosen ? 1.0f : 0.0f);
        break;
    case Look::size: { // Small to large: the same tile growing.
        const float side = (0.42f + 0.58f * (count > 1 ? static_cast<float>(index) / static_cast<float>(count - 1) : 1.0f)) * std::min(art_w, art_h);
        const auto shape = D2D1::RoundedRect(D2D1::RectF(cx - side * 0.5f, art_b - side, cx + side * 0.5f, art_b), side * 0.2f, side * 0.2f);
        brush(soft); target_->FillRoundedRectangle(shape, solid_.Get());
        brush(ink); target_->DrawRoundedRectangle(shape, solid_.Get(), 1.5f);
        break;
    }
    case Look::swatch: {
        const float r = std::min(art_w, art_h) * 0.5f - 1;
        const auto circle = D2D1::Ellipse(D2D1::Point2F(cx, art_cy), r, r);
        if (option.id == Id::accent && card.color == 0) { // Taken from the cover: a pie of colours.
            static constexpr uint32_t pie[4] = {0xE0524D, 0xE8B23A, 0x4DB6AC, 0x6C8EF5};
            for (int slice = 0; slice < 4; ++slice) {
                brush(colorOf(pie[slice]));
                Microsoft::WRL::ComPtr<ID2D1PathGeometry> path; Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
                if (FAILED(factory_->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) continue;
                const float a0 = slice * std::numbers::pi_v<float> * 0.5f, a1 = (slice + 1) * std::numbers::pi_v<float> * 0.5f;
                sink->BeginFigure(D2D1::Point2F(cx, art_cy), D2D1_FIGURE_BEGIN_FILLED);
                sink->AddLine(D2D1::Point2F(cx + r * std::cos(a0), art_cy + r * std::sin(a0)));
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(cx + r * std::cos(a1), art_cy + r * std::sin(a1)), D2D1::SizeF(r, r), 0, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED); sink->Close();
                target_->FillGeometry(path.Get(), solid_.Get());
            }
        } else {
            brush(option.id == Id::button_color && card.color == 0 ? uiAccent() : colorOf(card.color));
            target_->FillEllipse(circle, solid_.Get());
        }
        brush(chosen ? gray(1, 0.95f) : gray(1, 0.3f));
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, art_cy), r + 2.5f, r + 2.5f), solid_.Get(), chosen ? 2.0f : 1.0f);
        break;
    }
    case Look::shape: {
        const float side = std::min(art_w, art_h);
        const auto rect = D2D1::RectF(cx - side * 0.5f, art_cy - side * 0.5f, cx + side * 0.5f, art_cy + side * 0.5f);
        if (const auto shape = tileShape(rect, static_cast<AppSettings::TileShape>(std::clamp(index, 0, 2)))) {
            brush(soft); target_->FillGeometry(shape.Get(), solid_.Get());
            brush(ink); target_->DrawGeometry(shape.Get(), solid_.Get(), 1.6f);
        }
        break;
    }
    case Look::entrance: { // Three tiles arriving: growing in turn, rising in turn, or just there.
        const float side = std::min(art_w / 3.6f, art_h * 0.7f);
        for (int tile = 0; tile < 3; ++tile) {
            const float x = cx + (tile - 1) * (side + 5);
            const float scale = index == 0 ? 0.55f + 0.2f * tile : 1.0f;
            const float lift = index == 1 ? (2 - tile) * 7.0f : 0.0f;
            const float s = side * scale;
            brush(index == 2 ? ink : soft);
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - s * 0.5f, art_cy + side * 0.5f - s - lift + 4, x + s * 0.5f, art_cy + side * 0.5f - lift + 4), 5, 5), solid_.Get());
        }
        break;
    }
    case Look::gap: { // The outline and the picture: touching, the outline pushed out, the picture pulled in.
        const float side = std::min(art_w, art_h) * 0.62f;
        const float outset = index == 1 ? 5.0f : 0.0f, inset = index == 2 ? 5.0f : 0.0f;
        const auto picture = D2D1::RectF(cx - side * 0.5f + inset, art_cy - side * 0.5f + inset, cx + side * 0.5f - inset, art_cy + side * 0.5f - inset);
        brush(soft); target_->FillRoundedRectangle(D2D1::RoundedRect(picture, 5, 5), solid_.Get());
        brush(ink);
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - side * 0.5f - outset, art_cy - side * 0.5f - outset, cx + side * 0.5f + outset, art_cy + side * 0.5f + outset), 7, 7), solid_.Get(), 2.0f);
        break;
    }
    case Look::motion: { // Still, a pulsing halo, an outline with a travelling bright part.
        const float side = std::min(art_w, art_h) * 0.62f;
        const auto rect = D2D1::RectF(cx - side * 0.5f, art_cy - side * 0.5f, cx + side * 0.5f, art_cy + side * 0.5f);
        brush(soft); target_->FillRoundedRectangle(D2D1::RoundedRect(rect, 6, 6), solid_.Get());
        if (index == 1) {
            brush(uiAccent(0.1f + 0.3f * pulse));
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rect.left - 4, rect.top - 4, rect.right + 4, rect.bottom + 4), 9, 9), solid_.Get(), 5.0f);
        }
        brush(index == 0 ? ink : uiAccent(0.85f));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(rect, 6, 6), solid_.Get(), 1.8f);
        if (index == 2) { // A short bright stretch running round.
            const float t = std::fmod(elapsed_ * 0.6f, 1.0f), perimeter = 4 * side;
            const float at = t * perimeter;
            const auto point = [&](float d) {
                d = std::fmod(d + perimeter, perimeter);
                if (d < side) return D2D1::Point2F(rect.left + d, rect.top);
                if (d < 2 * side) return D2D1::Point2F(rect.right, rect.top + d - side);
                if (d < 3 * side) return D2D1::Point2F(rect.right - (d - 2 * side), rect.bottom);
                return D2D1::Point2F(rect.left, rect.bottom - (d - 3 * side));
            };
            for (int step = 0; step < 8; ++step) { brush(gray(1, 0.9f * (1 - step / 8.0f))); const auto p = point(at - step * 2.5f); target_->FillEllipse(D2D1::Ellipse(p, 2.4f, 2.4f), solid_.Get()); }
        }
        break;
    }
    case Look::grid: { // Dots, horizontal lines, a mesh: the pattern laid over the video.
        const auto rect = D2D1::RectF(cx - art_w * 0.5f, art_t, cx + art_w * 0.5f, art_b);
        brush(gray(1, 0.1f)); target_->FillRoundedRectangle(D2D1::RoundedRect(rect, 6, 6), solid_.Get());
        target_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        brush(gray(0, 0.75f));
        for (float y = rect.top + 3; y < rect.bottom; y += 6) {
            const int pattern = index - 1; // Card 0 is Brak: the plain picture.
            if (pattern < 0) break;
            if (pattern == 1) target_->FillRectangle(D2D1::RectF(rect.left, y, rect.right, y + 2), solid_.Get());
            else for (float x = rect.left + 3; x < rect.right; x += 6) {
                if (pattern == 0) target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), 1.6f, 1.6f), solid_.Get());
                else { target_->FillRectangle(D2D1::RectF(x - 3, y, x + 3, y + 1), solid_.Get()); target_->FillRectangle(D2D1::RectF(x, y - 3, x + 1, y + 3), solid_.Get()); }
            }
        }
        target_->PopAxisAlignedClip();
        break;
    }
    case Look::font:
        textLine(L"Aa", left, art_cy + 1, right - left, fontSample(static_cast<size_t>(index)), gray(1, chosen ? 1.0f : 0.75f), DWRITE_TEXT_ALIGNMENT_CENTER);
        break;
    case Look::hint: { // A key cap: glass, or white with dark ink.
        const auto cap = D2D1::RoundedRect(D2D1::RectF(cx - 22, art_cy - 13, cx + 22, art_cy + 13), 8, 8);
        brush(index == 0 ? gray(1, 0.22f) : gray(0.96f, 1)); target_->FillRoundedRectangle(cap, solid_.Get());
        textLine(L"A", cx - 22, art_cy, 44, friend_section_font_.Get(), index == 0 ? gray(1, 0.95f) : gray(0.1f), DWRITE_TEXT_ALIGNMENT_CENTER);
        break;
    }
    case Look::art: { // The shape of the search result's picture.
        const float h = std::min(art_h, 44.0f), w = index == 0 ? h : index == 1 ? h * 2.0f / 3.0f : h * 1.78f;
        const auto shape = D2D1::RoundedRect(D2D1::RectF(cx - w * 0.5f, art_cy - h * 0.5f, cx + w * 0.5f, art_cy + h * 0.5f), 5, 5);
        brush(soft); target_->FillRoundedRectangle(shape, solid_.Get());
        brush(ink); target_->DrawRoundedRectangle(shape, solid_.Get(), 1.5f);
        break;
    }
    case Look::place: { // A row of tiles; the special ones floating, first or last.
        const float side = std::min(art_w / 6.0f, art_h * 0.6f), y = art_cy - side * 0.5f;
        for (int tile = 0; tile < 5; ++tile) {
            const bool special = index == 1 ? tile == 0 : index == 2 ? tile == 4 : false;
            const float x = cx + (tile - 2) * (side + 3) - side * 0.5f;
            brush(special ? uiAccent(0.9f) : soft);
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + side, y + side), 3, 3), solid_.Get());
        }
        if (index == 0) { brush(uiAccent(0.9f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(art_r - 6, art_t + 6), 5, 5), solid_.Get()); }
        break;
    }
    case Look::idle: { // The unfocused button next to a focused one.
        const float w = art_w * 0.78f, h = 12;
        const auto rect = D2D1::RectF(cx - w * 0.5f, art_cy - 10, cx + w * 0.5f, art_cy + 2);
        (void)h;
        brush(index == 0 ? gray(1, 0.16f) : index == 1 ? colorOf(button_rgb_ ? button_rgb_ : ui_accent_, 0.3f) : gray(0, 0.6f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(rect, 6, 6), solid_.Get());
        brush(gray(0.96f, 0.95f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rect.left, rect.bottom + 4, rect.right, rect.bottom + 16), 6, 6), solid_.Get());
        break;
    }
    case Look::text: break;
    }
    if (!swatch) {
        textLine(card.label, left + 14, bottom - 17, right - left - 24, settings_body_font_.Get(), gray(1, chosen ? 0.97f : 0.6f));
        if (chosen) { // The tick the settings' choices carry, as a badge in the corner.
            const float x = right - 20, y = top + 20;
            brush(gray(0, 0.6f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), 11, 11), solid_.Get());
            line(x - 5, y, x - 1.5f, y + 3.5f, gray(0.96f), 1.8f); line(x - 1.5f, y + 3.5f, x + 5, y - 4.5f, gray(0.96f), 1.8f);
        }
    }
    if (focused) drawCompanionCardFocus(D2D1::RectF(left, top, right, bottom), 4);
}

// The look as it is now, in miniature: the layout in motion, tiles in their shape with the focus outline and
// halo, buttons, a switch and a slider in the accent, the font, the glass. All of it drawn from the applied settings.
void Renderer::drawThemePreview(float left, float top, float right, float bottom) {
    const auto frame = D2D1::RoundedRect(D2D1::RectF(left, top, right, bottom), 18, 18);
    target_->PushAxisAlignedClip(D2D1::RectF(left, top, right, bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    brush(D2D1::ColorF(0.03f, 0.036f, 0.045f)); target_->FillRoundedRectangle(frame, solid_.Get());
    if (drawWallpaperBackdrop(0.95f)) { brush(gray(0, 0.35f)); target_->FillRectangle(D2D1::RectF(left, top, right, bottom), solid_.Get()); }
    const float w = right - left, pad = 22;
    // 1. The layout, in its own scene.
    const float scene_h = std::min((bottom - top) * 0.34f, (w - 2 * pad) / 2.5f);
    drawLayoutScene(context_->theme_studio ? context_->theme_studio->layout : 0, D2D1::RectF(left + pad, top + pad, right - pad, top + pad + scene_h), elapsed_ * 0.6f, 1.0f);
    // 2. Tiles in the chosen shape; the third is focused.
    const float tiles_top = top + pad + scene_h + 20;
    const float side = std::min(88.0f, (w - 2 * pad - 4 * 14) / 5.0f);
    const float gap = (w - 2 * pad - 5 * side) / 4.0f;
    static constexpr uint32_t hues[5] = {0x3E5C76, 0x7D4E57, 0x4F772D, 0x6B4E9B, 0x9C6644};
    for (int i = 0; i < 5; ++i) {
        const float x = left + pad + i * (side + gap);
        const bool focused = i == 2;
        const float inset = focused ? focusInset(4) + 4 : 0;
        const auto rect = D2D1::RectF(x + inset, tiles_top + inset, x + side - inset, tiles_top + side - inset);
        const auto shape = tileShape(rect, tile_shape_);
        if (!shape) continue;
        brush(colorOf(hues[i], 1)); target_->FillGeometry(shape.Get(), solid_.Get());
        brush(gray(1, 0.14f)); target_->FillRectangle(D2D1::RectF(rect.left, rect.bottom - (rect.bottom - rect.top) * 0.28f, rect.right, rect.bottom), solid_.Get());
        if (!focused) continue;
        const auto accent = cover_accent_ ? colorOf(cover_accent_) : D2D1::ColorF(0.9f, 0.9f, 0.9f);
        const float lift = focusOutset(4);
        const auto outline = tileShape(D2D1::RectF(x - lift, tiles_top - lift, x + side + lift, tiles_top + side + lift), tile_shape_);
        if (!outline) continue;
        if (glow_level_ > 0.01f) {
            auto halo = accent; halo.a = std::min(0.6f, (0.14f + (glow_pulse_ ? 0.18f * (0.5f + 0.5f * std::sin(elapsed_ * 2.5f)) : 0.1f)) * glow_level_);
            brush(halo); target_->DrawGeometry(outline.Get(), solid_.Get(), 9.0f);
        }
        brush(accent); target_->DrawGeometry(outline.Get(), solid_.Get(), 2.5f);
        if (glow_trace_) {
            const float t = std::fmod(elapsed_ * 0.5f, 1.0f), perimeter = 4 * side, at = t * perimeter;
            const auto point = [&](float d) {
                d = std::fmod(d + perimeter, perimeter);
                if (d < side) return D2D1::Point2F(x + d, tiles_top);
                if (d < 2 * side) return D2D1::Point2F(x + side, tiles_top + d - side);
                if (d < 3 * side) return D2D1::Point2F(x + side - (d - 2 * side), tiles_top + side);
                return D2D1::Point2F(x, tiles_top + side - (d - 3 * side));
            };
            for (int step = 0; step < 10; ++step) { brush(gray(1, 0.9f * (1 - step / 10.0f))); target_->FillEllipse(D2D1::Ellipse(point(at - step * 3.0f), 3.2f, 3.2f), solid_.Get()); }
        }
    }
    // 3. A glass panel: the font, buttons, a switch and a slider.
    const float panel_top = tiles_top + side + 22;
    if (panel_top + 96 < bottom - 10) {
        const auto panel = D2D1::RoundedRect(D2D1::RectF(left + pad, panel_top, right - pad, bottom - pad), 16, 16);
        const auto glass = glassPanel(panel, 12);
        glassFill(glass, D2D1::ColorF(0.05f, 0.055f, 0.07f, 0.55f + 0.4f * (1 - glass_level_)));
        if (panel_borders_) { brush(gray(1, 0.18f)); target_->DrawRoundedRectangle(panel, solid_.Get(), 1); }
        const float px = left + pad + 20, pw = right - left - 2 * pad - 40, py = panel_top + 24;
        textLine(L"Aa  Launchme", px, py, pw, settings_heading_font_.Get(), gray(0.96f));
        const float row_y = py + 48;
        const auto focused_button = D2D1::RectF(px, row_y, px + 118, row_y + 38);
        const auto idle_button = D2D1::RectF(px + 130, row_y, px + 248, row_y + 38);
        const auto ink = drawButton(focused_button, 1.0f, 1.0f, false, false);
        textLine(tr(L"Graj"), focused_button.left, row_y + 19, 118, button_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
        const auto idle_ink = drawButton(idle_button, 0.0f, 1.0f, false, false);
        textLine(tr(L"Opcje"), idle_button.left, row_y + 19, 118, button_font_.Get(), idle_ink, DWRITE_TEXT_ALIGNMENT_CENTER);
        // A switch and a slider in the interface accent.
        const float sx = px + 276;
        if (sx + 120 < px + pw) {
            brush(uiAccent(0.9f)); target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(sx, row_y + 6, sx + 52, row_y + 32), 13, 13), solid_.Get());
            brush(gray(0.08f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sx + 39, row_y + 19), 9, 9), solid_.Get());
            const float tx = sx + 70, tw = std::max(40.0f, px + pw - tx);
            brush(gray(1, 0.18f)); target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tx, row_y + 15, tx + tw, row_y + 23), 4, 4), solid_.Get());
            brush(uiAccent(0.95f)); target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tx, row_y + 15, tx + tw * 0.62f, row_y + 23), 4, 4), solid_.Get());
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx + tw * 0.62f, row_y + 19), 8, 8), solid_.Get());
        }
    }
    target_->PopAxisAlignedClip();
    brush(gray(1, 0.14f)); target_->DrawRoundedRectangle(frame, solid_.Get(), 1);
}

// The home screen is drawn into a target of its own (the chain in Renderer::paint), then shown in the preview.
bool Renderer::beginThemeScene() {
    if (main_target_ || theme_main_target_) return false;
    const auto pixels = target_->GetPixelSize();
    if (!theme_scene_target_ || theme_scene_target_->GetPixelSize().width != pixels.width ||
        theme_scene_target_->GetPixelSize().height != pixels.height) {
        theme_scene_target_.Reset();
        const auto format = D2D1::PixelFormat(target_->GetPixelFormat().format, D2D1_ALPHA_MODE_PREMULTIPLIED);
        if (FAILED(target_->CreateCompatibleRenderTarget(target_->GetSize(), pixels, format,
            D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, theme_scene_target_.GetAddressOf()))) return false;
    }
    theme_main_target_ = target_;
    theme_main_realizer_ = realizer_;
    target_ = theme_scene_target_;
    realizer_.Reset();
    target_.As(&realizer_);
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale));
    target_->Clear(D2D1::ColorF(0.025f, 0.030f, 0.036f));
    return true;
}

HRESULT Renderer::endThemeScene() {
    if (!theme_main_target_) return S_OK;
    const auto result = target_->EndDraw();
    target_ = std::move(theme_main_target_);
    realizer_ = std::move(theme_main_realizer_);
    if (FAILED(result)) return result;
    return theme_scene_target_->GetBitmap(theme_scene_bitmap_.ReleaseAndGetAddressOf());
}

void Renderer::drawThemeStudio() {
    const auto* view = context_->theme_studio;
    if (!view) return;
    const auto& state = *view;
    const theme_ui::Layout layout{viewport_.width, viewport_.height};
    const float base = viewport_.scale;
    const float appear = smoothFade(state.appear), fade = smoothFade(state.tab_fade);
    const bool scene = context_->theme_scene && theme_scene_bitmap_;
    // A screen of its own, like Ustawienia: the last game's background frosted behind it.
    if (scene) { target_->Clear(D2D1::ColorF(0.022f, 0.024f, 0.030f)); drawBackground(); drawFrostedBackdrop(1); }
    else { brush(D2D1::ColorF(0.022f, 0.024f, 0.030f)); target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get()); }
    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), appear), nullptr);
    const float lift = (1.0f - appear) * 18.0f;
    const auto place = [&](float dy) { target_->SetTransform(D2D1::Matrix3x2F::Translation(0, dy) * D2D1::Matrix3x2F::Scale(base, base)); };
    place(lift);
    const auto fill = [&](const theme_ui::Rect& r, D2D1_COLOR_F ink, float radius) {
        brush(ink); target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), radius, radius), solid_.Get());
    };
    const auto outline = [&](const theme_ui::Rect& r, D2D1_COLOR_F ink, float thickness, float radius) {
        brush(ink); target_->DrawRoundedRectangle(D2D1::RoundedRect(box(r), radius, radius), solid_.Get(), thickness);
    };
    const auto middle = [](const theme_ui::Rect& r) { return (r.top + r.bottom) * 0.5f; };

    // Heading: the back dot, the title, the tabs between LB and RB.
    {
        const auto back = layout.backGlass();
        drawButtonGlass(box(back), 1, false);
        if (state.hover.kind == theme_ui::Hit::close) fill(back, gray(1, 0.14f), 18);
        brush(gray(0.96f, 0.92f));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((back.left + back.right) * 0.5f, middle(back)), 4, 4), solid_.Get());
        const auto title = layout.title();
        textLine(tr(L"Dostosuj wygląd"), title.left, 106, title.right - title.left, settings_heading_font_.Get(), gray(0.96f));
    }
    {
        const float tab_w = layout.tabWidth(), x = layout.tabsLeft() + state.tab_pos * tab_w;
        const auto first = layout.tab(0);
        fill({x + 2, first.top, x + tab_w - 2, first.bottom}, gray(0.96f), 20);
        for (int i = 0; i < theme_ui::tab_count; ++i) {
            const auto r = layout.tab(i);
            const bool hovered = state.hover.kind == theme_ui::Hit::tab && state.hover.index == i && i != state.tab;
            if (hovered) fill({r.left + 2, r.top, r.right - 2, r.bottom}, gray(1, 0.1f), 20);
            const float away = std::clamp(std::abs(static_cast<float>(i) - state.tab_pos), 0.0f, 1.0f);
            textLine(tr(theme_ui::tab_names[i]), r.left, middle(r), r.right - r.left, settings_body_font_.Get(),
                     gray(std::lerp(0.08f, 0.72f, away)), DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        const float cy = middle(first);
        drawShoulderHint(false, layout.tab(0).left - 8, cy, state.tab > 0 ? 1.0f : 0.4f);
        drawShoulderHint(true, layout.tab(theme_ui::tab_count - 1).right + 8, cy, state.tab + 1 < theme_ui::tab_count ? 1.0f : 0.4f);
    }

    // Options: a new tab's arrive rising and fading in; the column scrolls smoothly.
    const auto list = layout.list();
    const float scroll = state.shown_scroll;
    target_->PushAxisAlignedClip(box(layout.listClip()), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), fade), nullptr);
    place(lift + (1.0f - fade) * 16.0f);
    for (int i = 0; i < static_cast<int>(state.options.size()); ++i) {
        const auto& o = state.options[static_cast<size_t>(i)];
        const auto r = layout.option(state.options, i, scroll);
        if (r.bottom < list.top - 20 || r.top > list.bottom + 20) continue;
        const bool focused = state.row == i;
        const auto name_ink = gray(1, focused ? 0.97f : 0.85f);
        const auto row_panel = [&](bool hovered) {
            fill(r, gray(1, focused ? 0.14f : hovered ? 0.075f : 0.042f), 16);
            outline(r, gray(1, 0.08f), 1.0f, 16);
        };
        switch (o.kind) {
        case Kind::cards:
            textLine(o.title, r.left + 4, r.top + 12, r.right - r.left - 8, settings_body_font_.Get(), gray(1, 0.55f));
            for (int c = 0; c < static_cast<int>(o.cards.size()); ++c) {
                const auto cr = layout.card(state.options, i, c, scroll);
                if (cr.right < layout.listClip().left || cr.left > layout.listClip().right) continue;
                const bool hovered = state.hover.kind == theme_ui::Hit::card && state.hover.index == i && state.hover.slot == c;
                drawThemeCard(o, c, cr.left, cr.top, cr.right, cr.bottom, c == o.current, focused && c == o.current, hovered);
            }
            break;
        case Kind::toggle: {
            row_panel(state.hover.index == i && state.hover.kind == theme_ui::Hit::toggle);
            textLine(o.title, r.left + 22, middle(r), r.right - r.left - 270, settings_body_font_.Get(), name_ink);
            const float y = middle(r), right = r.right - 22;
            textLine(o.on ? tr(L"Włączone") : tr(L"Wyłączone"), right - 52 - 16 - 148, y, 148, settings_body_font_.Get(), palette::stateLabel(false), DWRITE_TEXT_ALIGNMENT_TRAILING);
            const float on = std::clamp(o.on_shown, 0.0f, 1.0f);
            const auto off_bg = gray(0.23f), accent = uiAccent();
            fill({right - 52, y - 14, right, y + 14}, D2D1::ColorF(std::lerp(off_bg.r, accent.r, on), std::lerp(off_bg.g, accent.g, on), std::lerp(off_bg.b, accent.b, on), 1.0f), 14);
            brush(gray(std::lerp(0.67f, 0.08f, on)));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(std::lerp(right - 52 + 14, right - 14, on), y), 10, 10), solid_.Get());
            break;
        }
        case Kind::slider: {
            row_panel(false);
            textLine(o.title, r.left + 22, middle(r), r.right - r.left - 400, settings_body_font_.Get(), name_ink);
            const auto t = layout.track(r), m = layout.minus(r), p = layout.plus(r);
            for (const auto* button : {&m, &p}) {
                const bool hovered = state.hover.index == i && state.hover.kind == (button == &m ? theme_ui::Hit::minus : theme_ui::Hit::plus);
                const float bx = (button->left + button->right) * 0.5f, by = middle(*button);
                brush(gray(1, hovered ? 0.2f : 0.1f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bx, by), 17, 17), solid_.Get());
                line(bx - 6, by, bx + 6, by, gray(1, 0.9f), 2);
                if (button == &p) line(bx, by - 6, bx, by + 6, gray(1, 0.9f), 2);
            }
            const float cy = middle(t), x = t.left + (t.right - t.left) * std::clamp(o.level_shown, 0.0f, 1.0f);
            brush(gray(1, 0.18f)); target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(t.left, cy - 4, t.right, cy + 4), 4, 4), solid_.Get());
            brush(uiAccent(0.95f)); target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(t.left, cy - 4, x, cy + 4), 4, 4), solid_.Get());
            brush(gray(0.96f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), 9, 9), solid_.Get());
            textLine(o.value, m.left - 78, middle(r), 70, settings_body_font_.Get(), gray(1, 0.6f), DWRITE_TEXT_ALIGNMENT_TRAILING);
            break;
        }
        case Kind::action: {
            row_panel(false);
            textLine(o.title, r.left + 22, middle(r), r.right - r.left - 260, settings_body_font_.Get(), name_ink);
            const auto b = box(layout.actionButton(r));
            const bool hovered = state.hover.index == i && state.hover.kind == theme_ui::Hit::action;
            const bool bin_focus = focused && o.trash && state.card == 1;
            const auto ink = drawButton(b, focused && !bin_focus ? 1.0f : 0.0f, 1, false, hovered);
            textLine(o.value, b.left, (b.top + b.bottom) * 0.5f, b.right - b.left, settings_body_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
            if (o.trash) { // A bin beside it: red under the pointer.
                const auto t = layout.trashButton(r);
                const bool over = state.hover.index == i && state.hover.kind == theme_ui::Hit::trash;
                const float tx = (t.left + t.right) * 0.5f, ty = middle(t);
                fill(t, over || bin_focus ? D2D1::ColorF(0.86f, 0.24f, 0.26f, 0.9f) : gray(1, 0.1f), 21);
                const auto glyph = gray(1, over || bin_focus ? 0.98f : 0.8f);
                line(tx - 8, ty - 6, tx + 8, ty - 6, glyph, 1.8f); line(tx - 3, ty - 6, tx - 3, ty - 9.5f, glyph, 1.8f);
                line(tx + 3, ty - 6, tx + 3, ty - 9.5f, glyph, 1.8f); line(tx - 3, ty - 9.5f, tx + 3, ty - 9.5f, glyph, 1.8f);
                brush(glyph); target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tx - 6, ty - 3, tx + 6, ty + 9), 2.5f, 2.5f), solid_.Get(), 1.8f);
                line(tx - 2, ty - 0.5f, tx - 2, ty + 6, glyph, 1.4f); line(tx + 2, ty - 0.5f, tx + 2, ty + 6, glyph, 1.4f);
                if (bin_focus) drawCompanionCardFocus(box(t), 4);
            }
            break;
        }
        }
    }
    // Wyglądy: Cofnij zmiany and Zapisz wygląd, the saved looks under them as rows; or the name field while naming.
    if (state.current() == theme_ui::Tab::looks) {
        const auto sc = state.shown_scroll;
        if (state.naming) {
            const auto field = layout.nameField(sc);
            fill(field, gray(1, 0.07f), 22);
            outline(field, uiAccent(0.9f), 1.5f, 22);
            textLine(tr(L"Nazwa:"), field.left + 18, middle(field), 70, settings_body_font_.Get(), gray(1, 0.55f));
            const float tx = field.left + 84;
            textLine(state.name, tx, middle(field), field.right - tx - 14, settings_body_font_.Get(), gray(0.97f));
            const float cursor = tx + textAdvance(state.name, settings_body_font_.Get()) + 2;
            if ((GetTickCount64() / 530) % 2 == 0) { brush(gray(0.97f)); target_->FillRectangle(D2D1::RectF(cursor, field.top + 12, cursor + 1.5f, field.bottom - 12), solid_.Get()); }
            for (const auto& [rect, caption, hit, primary] : {std::tuple{layout.nameOk(sc), tr(L"Zapisz"), theme_ui::Hit::name_ok, true},
                                                             std::tuple{layout.nameCancel(sc), tr(L"Anuluj"), theme_ui::Hit::name_cancel, false}}) {
                const auto b = box(rect);
                const auto ink = drawButton(b, primary ? 1.0f : 0.0f, 1, false, state.hover.kind == hit);
                textLine(caption, b.left, (b.top + b.bottom) * 0.5f, b.right - b.left, settings_body_font_.Get(), ink, DWRITE_TEXT_ALIGNMENT_CENTER);
            }
        } else {
            const auto u = box(layout.undo(sc)), sv = box(layout.saveButton(sc));
            const auto undo_ink = drawButton(u, state.item == theme_ui::State::undo_item ? 1.0f : 0.0f, 1, false, state.hover.kind == theme_ui::Hit::revert);
            textLine(tr(L"Cofnij zmiany"), u.left, (u.top + u.bottom) * 0.5f, u.right - u.left, settings_body_font_.Get(), undo_ink, DWRITE_TEXT_ALIGNMENT_CENTER);
            const auto save_ink = drawButton(sv, state.item == theme_ui::State::save_item ? 1.0f : 0.0f, 1, false, state.hover.kind == theme_ui::Hit::save);
            textLine(tr(L"Zapisz wygląd…"), sv.left, (sv.top + sv.bottom) * 0.5f, sv.right - sv.left, settings_body_font_.Get(), save_ink, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        for (int i = 0; i < static_cast<int>(state.saved.size()); ++i) {
            const auto r = layout.chip(i, sc);
            if (r.bottom < list.top - 20 || r.top > list.bottom + 20) continue;
            const bool focused = state.item == theme_ui::State::first_saved + i;
            const bool hovered = (state.hover.kind == theme_ui::Hit::chip || state.hover.kind == theme_ui::Hit::chip_delete) && state.hover.index == i;
            const bool active = state.active == i;
            fill(r, gray(1, active ? 0.14f : hovered || focused ? 0.1f : 0.045f), 16);
            outline(r, gray(1, active ? 0.24f : 0.08f), 1.0f, 16);
            const bool delete_visible = focused || hovered;
            const float text_left = r.left + (active ? 44 : 22);
            if (active) { brush(uiAccent(0.95f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(r.left + 26, middle(r)), 5, 5), solid_.Get()); }
            textLine(state.saved[static_cast<size_t>(i)], text_left, middle(r), r.right - text_left - (delete_visible ? 60 : 20), settings_body_font_.Get(), gray(1, active || focused ? 0.97f : 0.75f));
            if (delete_visible) {
                const auto d = layout.chipDelete(r);
                const float dx = (d.left + d.right) * 0.5f, dy = middle(d);
                const bool over = state.hover.kind == theme_ui::Hit::chip_delete && state.hover.index == i;
                brush(over || (state.delete_armed && focused) ? D2D1::ColorF(0.9f, 0.3f, 0.3f, 0.9f) : gray(1, 0.14f)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dx, dy), 13, 13), solid_.Get());
                line(dx - 4, dy - 4, dx + 4, dy + 4, gray(1, 0.95f), 1.6f); line(dx + 4, dy - 4, dx - 4, dy + 4, gray(1, 0.95f), 1.6f);
            }
            if (focused) drawCompanionCardFocus(box(r), 4);
        }
    }
    target_->PopLayer();
    place(lift);
    target_->PopAxisAlignedClip();
    if (const float total = state.current() == theme_ui::Tab::looks ? layout.looksTotal(state) : layout.total(state.options), room = list.bottom - list.top, overflow = total - room; overflow > 0) { // A scroll thumb.
        const float thumb = std::max(30.0f, room * room / total);
        const float y = list.top + (room - thumb) * (std::clamp(scroll / overflow, 0.0f, 1.0f));
        brush(gray(1, 0.12f)); target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(layout.listClip().right + 8, y, layout.listClip().right + 11, y + thumb), 1.5f, 1.5f), solid_.Get());
    }

    // The preview: the real home screen, live, in the screen's own proportions.
    const auto pv = layout.preview();
    if (scene) {
        const auto rect = D2D1::RoundedRect(box(pv), 18, 18);
        Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> mask;
        if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(rect, mask.GetAddressOf()))) {
            target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), mask.Get()), nullptr);
            target_->DrawBitmap(theme_scene_bitmap_.Get(), rect.rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            if (state.scene_in < 1.0f) { brush(D2D1::ColorF(0.01f, 0.012f, 0.016f, 1.0f - smoothFade(std::clamp((state.scene_in - 0.2f) / 0.8f, 0.0f, 1.0f)))); target_->FillRectangle(rect.rect, solid_.Get()); }
            target_->PopLayer();
        }
        brush(gray(1, 0.14f)); target_->DrawRoundedRectangle(rect, solid_.Get(), 1.0f);
    } else drawThemePreview(pv.left, pv.top, pv.right, pv.bottom);

    if (!state.status.empty()) textLine(state.status, layout.statusLine().left + 4, (layout.statusLine().top + layout.statusLine().bottom) * 0.5f, layout.statusLine().right - layout.statusLine().left, settings_body_font_.Get(), gray(1, 0.6f));
    target_->PopLayer();
    target_->SetTransform(D2D1::Matrix3x2F::Scale(base, base));
}
