#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "xmb_layout.hpp"
#include "occlusion_mask.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
using palette::gray;
using render_detail::smoothFade;
}

void Renderer::drawXmb() {
    const auto& cards = *cards_;
    const size_t top = std::min(context_->xmb_top, cards.size());
    const auto bounds = D2D1::RectF(0, 0, viewport_.width, viewport_.height);
    std::vector<Microsoft::WRL::ComPtr<ID2D1Geometry>> tiles;
    if (top < cards.size()) for (size_t i = 0; i < top; ++i) {
        const auto& card = cards[i];
        if (card.opacity < 0.01f || card.size <= 0 || card.x > viewport_.width + 45 || card.x + card.width() < -45) continue;
        const float zoom = i == selected_ ? 1 + 0.13f * smoothFade(context_->launch_progress) : 1;
        const float dx = card.width() * (zoom - 1) * 0.5f, dy = card.size * (zoom - 1) * 0.5f;
        tiles.push_back(tileShape(D2D1::RectF(card.x - dx, card.y - dy, card.x + card.width() + dx, card.y + card.size + dy), tile_shape_));
    }
    const auto mask = occlusionMask(factory_.Get(), bounds, tiles);
    // The column fades out towards the bottom edge (and the hints there) instead of being cut by it.
    Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> bottom_fade;
    {
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stops;
        const D2D1_GRADIENT_STOP fade_stops[] = {{0, D2D1::ColorF(0, 0, 0, 1)}, {1, D2D1::ColorF(0, 0, 0, 0)}};
        if (SUCCEEDED(target_->CreateGradientStopCollection(fade_stops, 2, stops.GetAddressOf())))
            target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, viewport_.height - 140),
                D2D1::Point2F(0, viewport_.height - 36)), stops.Get(), bottom_fade.GetAddressOf());
    }
    if (bottom_fade) target_->PushLayer(D2D1::LayerParameters(bounds, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::IdentityMatrix(), 1.0f, bottom_fade.Get()), nullptr);
    // Column cards, labels and the moving focus outline travel behind the whole top row.
    // Its translucent tile fills must not let the scrolling artwork bleed through.
    if (mask) target_->PushLayer(D2D1::LayerParameters(bounds, mask.Get()), nullptr);
    for (size_t i = top; i < cards.size(); ++i) if (i != selected_) drawCard(cards[i], i);
    drawXmbLabels();
    if (mask) target_->PopLayer();
    if (bottom_fade) target_->PopLayer();
    for (size_t i = 0; i < top; ++i) if (i != selected_) drawCard(cards[i], i);
    drawXmbCaption();
    drawXmbLogo();
    const float focus = smoothFade(context_->launch_progress);
    if (focus > 0.001f) {
        brush(D2D1::ColorF(0.008f, 0.012f, 0.016f, focus * 0.82f));
        target_->FillRectangle(bounds, solid_.Get());
    }
    if (selected_ < cards.size()) {
        if (mask && selected_ >= top) target_->PushLayer(D2D1::LayerParameters(bounds, mask.Get()), nullptr);
        drawCard(cards[selected_], selected_);
        if (mask && selected_ >= top) target_->PopLayer();
    }
    // Her line beside her tile of the row (right of it: the focused tile and its caption sit left).
    if (context_->companion && focus < 0.001f)
        for (size_t i = 0; i < top; ++i) {
            const auto& card = cards[i];
            if (card.game.kind != GameKind::companion || card.opacity < 0.01f || card.x + card.size < 0 || card.x > viewport_.width) continue;
            const auto rect = D2D1::RectF(card.x, card.y, card.x + card.size, card.y + card.size);
            drawCompanionTileBubble(rect, card.y + card.size * 1.06f - card.size * 1.04f * 0.44f, card.opacity, false);
        }
}

float Renderer::xmbTransparency(size_t index, bool selected) const {
    if (selected) return 1.0f;
    const size_t top = context_->xmb_top;
    if (index < top) {
        const int offset = std::abs(static_cast<int>(index) - static_cast<int>(context_->xmb_column));
        return offset == 0 ? 0.95f : std::max(0.3f, 0.86f - 0.07f * offset);
    }
    const int row = static_cast<int>(index - top), focus = context_->xmb_row;
    if (focus < 0) return std::max(0.25f, 0.72f - 0.08f * row); // Preview under a focused tile.
    const int offset = row - focus;
    return offset < 0 ? 0.42f : std::max(0.3f, 0.9f - 0.09f * offset);
}

void Renderer::drawXmbTileIcon(const GameCard& card, size_t index, float x, float y, float size, float transparency) {
    const auto base = D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale);
    if (card.game.kind == GameKind::category) {
        // 2x2 preview of the collection's first covers, in unit tile space like the cover fill.
        target_->SetTransform(D2D1::Matrix3x2F::Scale(size, size) * D2D1::Matrix3x2F::Translation(x, y) * base);
        constexpr float pad = 0.1f, gap = 0.05f, cell = (1.0f - 2 * pad - gap) / 2;
        for (int slot = 0; slot < 4; ++slot) {
            const float left = pad + (slot % 2) * (cell + gap), top = pad + (slot / 2) * (cell + gap);
            const auto shape = tileShape(D2D1::RectF(left, top, left + cell, top + cell), tile_shape_);
            if (!shape) continue;
            const Cover* thumbnail = index < tile_thumbnails_.size() ? &tile_thumbnails_[index][slot] : nullptr;
            if (thumbnail && thumbnail->bitmap && thumbnail->brush) {
                const auto pixels = thumbnail->bitmap->GetPixelSize();
                const float zoom = cell / static_cast<float>(std::max(1u, std::min(pixels.width, pixels.height)));
                const float w = pixels.width * zoom, h = pixels.height * zoom;
                const float offset_x = left + std::clamp(cell * 0.5f - thumbnail->focus.x * w, cell - w, 0.0f);
                const float offset_y = top + std::clamp(cell * 0.5f - thumbnail->focus.y * h, cell - h, 0.0f);
                thumbnail->brush->SetTransform(D2D1::Matrix3x2F::Scale(zoom, zoom) * D2D1::Matrix3x2F::Translation(offset_x, offset_y));
                thumbnail->brush->SetOpacity(transparency);
                target_->FillGeometry(shape.Get(), thumbnail->brush.Get());
            } else {
                brush(gray(1, 0.07f * transparency));
                target_->FillGeometry(shape.Get(), solid_.Get());
            }
        }
        return;
    }
    target_->SetTransform(base);
    const float cx = x + size * 0.5f, cy = y + size * 0.5f;
    const float stroke = std::max(1.6f, size * 0.018f);
    const auto ink = gray(0.94f, 0.9f * transparency);
    if (card.game.kind == GameKind::companion) {
        // Towarzyszka: her close-up in the tile, over a rose tint.
        if (!context_->companion) return;
        if (const auto shape = tileShape(D2D1::RectF(x, y, x + size, y + size), tile_shape_)) {
            brush(D2D1::ColorF(0.88f, 0.40f, 0.60f, 0.24f * transparency)); target_->FillGeometry(shape.Get(), solid_.Get());
            drawCompanionInTile(shape.Get(), cx, y + size * 1.06f, size * 1.04f, transparency);
        }
        return;
    }
    if (card.game.kind == GameKind::glance) {
        // Glance: its glyph over an amber tint (the colour of its Metro tile).
        if (const auto shape = tileShape(D2D1::RectF(x, y, x + size, y + size), tile_shape_)) {
            brush(D2D1::ColorF(0.92f, 0.58f, 0.20f, 0.22f * transparency)); target_->FillGeometry(shape.Get(), solid_.Get());
        }
        drawGlanceGlyph(cx, cy, size * 0.36f, ink);
        return;
    }
    brush(ink);
    if (card.game.kind == GameKind::settings) {
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), size * 0.13f, size * 0.13f), solid_.Get(), stroke);
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), size * 0.047f, size * 0.047f), solid_.Get(), stroke * 0.85f);
        for (int spoke = 0; spoke < 8; ++spoke) {
            const float angle = spoke * std::numbers::pi_v<float> / 4;
            line(cx + std::cos(angle) * size * 0.155f, cy + std::sin(angle) * size * 0.155f,
                 cx + std::cos(angle) * size * 0.215f, cy + std::sin(angle) * size * 0.215f, ink, stroke * 1.25f);
        }
        return;
    }
    // "Wszystkie gry": the same 2x2 grid glyph as the quick menu shortcut.
    const float cell = size * 0.13f, gap = size * 0.035f;
    for (int tile = 0; tile < 4; ++tile) {
        const float left = tile % 2 ? cx + gap * 0.5f : cx - gap * 0.5f - cell;
        const float top = tile / 2 ? cy + gap * 0.5f : cy - gap * 0.5f - cell;
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, top, left + cell, top + cell), cell * 0.25f, cell * 0.25f),
                                      solid_.Get(), stroke);
    }
}

void Renderer::drawXmbCaption() {
    const auto& cards = *cards_;
    const size_t top = context_->xmb_top;
    if (context_->xmb_column >= top || context_->xmb_column >= cards.size()) return;
    const auto& column_tile = cards[context_->xmb_column];
    const float center = column_tile.x + column_tile.width() * 0.5f;
    const float caption_alpha = context_->xmb_row < 0 ? 1.0f : 0.72f;
    const float caption_y = column_tile.y + column_tile.size + 10;
    // 17 px over 16 px (15 px was too small from the sofa); the band to the column holds both.
    textLine(context_->xmb_caption, center - 180, caption_y + 11, 360, toast_font_.Get(), gray(0.96f, caption_alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    if (!context_->xmb_subcaption.empty())
        textLine(context_->xmb_subcaption, center - 180, caption_y + 33, 360, settings_body_font_.Get(), gray(0.64f, caption_alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
}

void Renderer::drawXmbLabels() {
    using namespace xmb_layout;
    const auto& cards = *cards_;
    const size_t top = context_->xmb_top;
    const float label_x = labelLeft(context_->xmb_scale);
    const float full_width = std::max(120.0f, viewport_.width - label_x - 80);
    const auto logo = xmbLogoRect(); // Labels beside the corner logo stop short of it.
    for (size_t i = top; i < cards.size(); ++i) {
        const auto& card = cards[i];
        if (card.opacity < 0.01f || card.y > viewport_.height || card.y + card.size < 0) continue;
        const bool focus = i == selected_;
        const float alpha = card.opacity * xmbTransparency(i, focus);
        const float middle = card.y + card.size * 0.5f;
        const bool beside_logo = logo && middle + 32 > logo->top && middle - 32 < logo->bottom;
        const float width = beside_logo ? std::max(120.0f, std::min(full_width, logo->left - 28 - label_x)) : full_width;
        if (!focus) {
            text(card.game.name, label_x, middle - 13, width, 28, menu_font_.Get(), gray(0.86f, alpha));
            continue;
        }
        const auto& game = card.game;
        const auto session = sessionOf(game);
        const std::wstring status = session == SessionState::frozen ? std::wstring(tr(L"Wstrzymana")) : session == SessionState::running ? std::wstring(tr(L"W grze")) :
                                    game.installation.active() ? std::wstring(primaryActionLabel(game)) :
                                    !game.installed ? std::wstring(localFileGame(game.source) ? missingFileLabel(game) : tr(L"Do instalacji")) : activePlatformName(game);
        text(game.name, label_x, middle - 31, width, 32, category_font_.Get(), gray(0.97f, alpha));
        text(status, label_x, middle + 5, width, 24, settings_body_font_.Get(), gray(0.62f, alpha));
    }
}

// The focused game's logo in the bottom-right corner (XMB: right-aligned with the platform badge
// and just above it; PSfive: on the buttons' bottom line). The ink bounds trim transparent or
// matte margins, so every logo sits on the same edges.
std::optional<D2D1_RECT_F> Renderer::cornerLogoRect(float max_width, float max_height, float right, float bottom) const {
    if (!cards_ || selected_ >= cards_->size() || selected_ >= logos_.size() || !isGame((*cards_)[selected_].game)) return std::nullopt;
    const auto& logo = logos_[selected_];
    if (!logo.bitmap) return std::nullopt;
    const auto pixels = logo.bitmap->GetPixelSize();
    const auto source = logo.has_ink_bounds ? logo.ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
    const float ink_w = source.right - source.left, ink_h = source.bottom - source.top;
    if (ink_w < 1 || ink_h < 1) return std::nullopt;
    const float factor = std::min(max_width / ink_w, max_height / ink_h);
    return D2D1::RectF(right - ink_w * factor, bottom - ink_h * factor, right, bottom);
}

std::optional<D2D1_RECT_F> Renderer::xmbLogoRect() const {
    const float scale = context_->xmb_scale;
    // Button hints along the bottom edge lift the logo with the platform badge under it.
    return cornerLogoRect(std::min(400.0f * scale, viewport_.width * 0.3f), 118.0f * scale, viewport_.width - 57,
                          viewport_.height - 82 - (context_->hints.empty() ? 0.0f : 30.0f));
}

void Renderer::drawCornerLogo(std::optional<D2D1_RECT_F> box) {
    if (!box) return;
    const auto& logo = logos_[selected_];
    // Fades and rises in after a focus change; the quick bar dims it with the rest of the screen.
    const float fade = smoothFade(detail_);
    const float alpha = fade * reveal(logo) * (1.0f - smoothFade(context_->quick_progress));
    if (alpha < 0.004f) return;
    const auto pixels = logo.bitmap->GetPixelSize();
    const auto source = logo.has_ink_bounds ? logo.ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
    const float rise = (1.0f - fade) * 10.0f;
    target_->DrawBitmap(logo.bitmap.Get(), D2D1::RectF(box->left, box->top + rise, box->right, box->bottom + rise),
                        0.96f * alpha, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
}
