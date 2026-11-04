// Pobierania (src/app/downloads.cpp, geometry ui/downloads_view.hpp): the last game's picture
// frosted behind a list of the games being downloaded, each with its cover, store and state, the
// share done and the bytes, and a bar. The focus glides between Back and the rows.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "downloads_view.hpp"

#include <algorithm>
#include <cmath>

namespace {
using palette::gray;
using render_detail::box;
}

void Renderer::drawDownloadGlyph(float x, float cy, float scale, D2D1_COLOR_F ink) {
    const float s = scale, stroke = 2.6f * std::max(0.6f, s);
    // An arrow down into an open tray.
    line(x, cy - 19 * s, x, cy + 5 * s, ink, stroke);
    line(x - 9 * s, cy - 4 * s, x, cy + 5 * s, ink, stroke);
    line(x + 9 * s, cy - 4 * s, x, cy + 5 * s, ink, stroke);
    line(x - 17 * s, cy + 8 * s, x - 17 * s, cy + 17 * s, ink, stroke);
    line(x - 17 * s, cy + 17 * s, x + 17 * s, cy + 17 * s, ink, stroke);
    line(x + 17 * s, cy + 17 * s, x + 17 * s, cy + 8 * s, ink, stroke);
}

void Renderer::drawDownloads() {
    target_->Clear(D2D1::ColorF(0.022f, 0.024f, 0.030f));
    drawBackground();
    drawFrostedBackdrop(1);
    if (!context_->downloads) return;
    const auto& state = *context_->downloads;
    const downloads_ui::Layout layout{viewport_.width, viewport_.height};
    const int count = static_cast<int>(std::min(state.rows.size(), cards_ ? cards_->size() : 0));
    const bool on_back = state.back || !count;
    const auto glide = [&](settings_ui::Rect target) {
        if (!context_->downloads_glide) return box(target);
        const auto& g = *context_->downloads_glide;
        return D2D1::RectF(g.left, g.top, g.right, g.bottom);
    };
    // Back and the heading beside it.
    const auto back = layout.back();
    drawButtonGlass(box(back), 1, false);
    if (on_back) drawButtonFocus(glide(back));
    brush(buttonInk(on_back ? 1.0f : 0.0f));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F((back.left + back.right) * 0.5f, (back.top + back.bottom) * 0.5f), 3.5f, 3.5f), solid_.Get());
    textLine(tr(L"Pobierania"), back.right + 26, (back.top + back.bottom) * 0.5f, layout.right() - back.right - 26, settings_heading_font_.Get(), gray(0.96f));
    if (!count) {
        // Nothing downloading: a sad little cloud and one line, centred in the space under the heading.
        const float cx = viewport_.width * 0.5f, cy = (layout.top + layout.bottom()) * 0.5f - 24;
        const float scale = 4.2f;
        D2D1_MATRIX_3X2_F outer{};
        target_->GetTransform(&outer);
        target_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(cx, cy)) * outer);
        drawCloudGlyph(cx, cy, gray(0.34f), std::nullopt);
        const auto face = gray(0.10f);
        brush(face);
        for (const float dx : {-4.0f, 3.0f}) target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + dx, cy - 3.2f), 1.0f, 1.0f), solid_.Get());
        const D2D1_POINT_2F frown[] = {{cx - 3.5f, cy + 3.4f}, {cx - 2.2f, cy + 2.1f}, {cx - 0.5f, cy + 1.7f}, {cx + 1.2f, cy + 2.1f}, {cx + 2.5f, cy + 3.4f}};
        for (size_t i = 0; i + 1 < std::size(frown); ++i) line(frown[i].x, frown[i].y, frown[i + 1].x, frown[i + 1].y, face, 0.9f);
        target_->SetTransform(outer);
        const float text_width = std::min(layout.right() - layout.left, 720.0f);
        textLine(tr(L"Nic się nie pobiera."), cx - text_width * 0.5f, cy + 78, text_width, category_font_.Get(), gray(0.72f), DWRITE_TEXT_ALIGNMENT_CENTER);
        return;
    }
    target_->PushAxisAlignedClip(D2D1::RectF(layout.left - 4, layout.top - 4, layout.right() + 4, layout.bottom()), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // The focused row: a lighter card gliding between the rows (and in from Back).
    if (!on_back) {
        const auto r = glide(layout.row(static_cast<int>(selected_), state.scroll));
        brush(gray(1, 0.10f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(r, 18, 18), solid_.Get());
    }
    for (int i = 0; i < count; ++i) {
        const auto r = layout.row(i, state.scroll);
        if (r.bottom < layout.top - 4 || r.top > layout.bottom()) continue;
        const auto& row = state.rows[static_cast<size_t>(i)];
        const auto& game = (*cards_)[static_cast<size_t>(i)].game;
        brush(gray(1, 0.04f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(box(r), 18, 18), solid_.Get());
        // The cover.
        const float size = r.bottom - r.top - 24;
        const auto cover = D2D1::RectF(r.left + 12, r.top + 12, r.left + 12 + size, r.top + 12 + size);
        if (const auto shape = tileShape(cover, tile_shape_)) {
            brush(D2D1::ColorF(0.13f, 0.17f, 0.2f));
            target_->FillGeometry(shape.Get(), solid_.Get());
            if (static_cast<size_t>(i) < covers_.size()) fillImage(covers_[i], cover, 12, reveal(covers_[i]), false, shape.Get());
        }
        const float text_left = cover.right + 24, right = r.right - 28;
        // Name and the share done on the first line; store, state and bytes on the second.
        const std::wstring percent = row.fraction ? std::to_wstring(static_cast<int>(*row.fraction * 100)) + L"%" : L"";
        const float percent_width = percent.empty() ? 0 : std::ceil(textAdvance(percent, category_font_.Get())) + 4;
        textLine(game.name, text_left, r.top + 30, right - text_left - percent_width - 24, category_font_.Get(), gray(0.97f));
        if (!percent.empty()) textLine(percent, right - percent_width, r.top + 30, percent_width, category_font_.Get(),
                                       row.moving ? gray(0.97f) : gray(0.62f), DWRITE_TEXT_ALIGNMENT_TRAILING);
        const float amount_width = row.amount.empty() ? 0 : std::ceil(textAdvance(row.amount, settings_body_font_.Get())) + 4;
        textLine(row.store + L"  ·  " + row.status, text_left, r.top + 58, right - text_left - amount_width - 24, settings_body_font_.Get(), palette::muted());
        if (!row.amount.empty()) textLine(row.amount, right - amount_width, r.top + 58, amount_width, settings_body_font_.Get(), palette::muted(),
                                          DWRITE_TEXT_ALIGNMENT_TRAILING);
        if (!row.destination.empty()) textLine(row.destination, text_left, r.top + 83, right - text_left,
                                               friend_status_font_.Get(), palette::muted());
        // The bar: accent while it moves, grey while it waits or is paused; a gliding segment
        // when the store gives no share yet.
        const float bar_top = r.bottom - 22, bar_bottom = bar_top + 6;
        brush(gray(1, 0.12f));
        target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(text_left, bar_top, right, bar_bottom), 3, 3), solid_.Get());
        float begin = text_left, end = text_left;
        if (row.fraction) end = text_left + (right - text_left) * std::clamp(state.bar(static_cast<size_t>(i), row), 0.0f, 1.0f); // Eased (tickDownloadBars).
        else if (row.moving) {
            const float segment = (right - text_left) * 0.2f;
            begin = text_left + (right - text_left - segment) * (0.5f + 0.5f * std::sin(elapsed_ * 2.5f));
            end = begin + segment;
        }
        if (end > begin + 0.5f) {
            brush(row.moving ? uiAccent() : gray(0.6f));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(begin, bar_top, end, bar_bottom), 3, 3), solid_.Get());
        }
    }
    target_->PopAxisAlignedClip();
}
