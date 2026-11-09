// The game's manual over the case in the hand (game_manual_view.hpp): the booklet's spread on a dark
// table, its page numbers, the hints; pages still being drawn wait as pale sheets.
#include "renderer.hpp"
#include "game_manual_view.hpp"
#include "renderer_internal.hpp"

#include <algorithm>
#include <cmath>

using palette::gray;

void Renderer::drawGameManual() {
    const auto* state = context_->manual;
    if (!state || state->alpha < 0.003f || !cards_ || selected_ >= cards_->size()) return;
    const float a = state->alpha, W = viewport_.width, H = viewport_.height;
    const manual_ui::Layout layout{W, H};
    brush(gray(0.02f, 0.985f * a)); target_->FillRectangle(D2D1::RectF(0, 0, W, H), solid_.Get());
    const auto& game = (*cards_)[selected_].game;
    // The title and where we are.
    textLine(game.name + L"  ·  " + tr(L"Instrukcja"), layout.left(), 62, W * 0.6f, settings_heading_font_.Get(), gray(0.95f, a));
    const auto [first, second] = manual_ui::pagesOf(state->spread, state->pages);
    std::wstring where;
    if (state->pages > 0) {
        const int shown_first = first + 1, shown_last = (second >= 0 ? second : first) + 1;
        where = (shown_first == shown_last ? std::to_wstring(shown_first) : std::to_wstring(shown_first) + L"–" + std::to_wstring(shown_last)) +
                L" / " + std::to_wstring(state->pages);
    }
    if (!where.empty()) textLine(where, W * 0.6f, 62, W * 0.4f - layout.left(), settings_body_font_.Get(), gray(0.70f, a), DWRITE_TEXT_ALIGNMENT_TRAILING);
    if (state->failed) {
        textLine(tr(L"Nie można otworzyć instrukcji."), 0, H * 0.5f, W, settings_body_font_.Get(), gray(0.80f, a), DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
        // The spread: the pages as large as fit, side by side (the cover alone), zoomed round the middle.
        const float room_w = layout.right() - layout.left(), room_h = layout.bottom() - layout.top();
        const int count = second >= 0 ? 2 : 1;
        // Pages not drawn yet take the shape of one that is (else A5).
        float page_aspect = 0.70f;
        for (const int page : {first, second})
            if (auto* image = page >= 0 ? manualPage(page) : nullptr) { const auto size = image->bitmap->GetSize(); if (size.height > 0) { page_aspect = size.width / size.height; break; } }
        float ph = room_h, pw = ph * page_aspect;
        if (pw * count > room_w) { pw = room_w / count; ph = pw / page_aspect; }
        const float zoom = state->zoomed ? manual_ui::zoom : 1.0f;
        const float total_w = pw * count * zoom, total_h = ph * zoom;
        const float cx = (layout.left() + layout.right()) * 0.5f, cy = (layout.top() + layout.bottom()) * 0.5f;
        // Zoomed: the pan moves across what overflows the room.
        const float spare_x = std::max(0.0f, total_w - room_w) * 0.5f, spare_y = std::max(0.0f, total_h - room_h) * 0.5f;
        const float left = cx - total_w * 0.5f - state->pan_x * spare_x, top = cy - total_h * 0.5f - state->pan_y * spare_y;
        target_->PushAxisAlignedClip(D2D1::RectF(0, layout.top() - 12, W, layout.bottom() + 12), D2D1_ANTIALIAS_MODE_ALIASED);
        int slot = 0;
        for (const int page : {first, second}) {
            if (page < 0) continue;
            const auto r = D2D1::RectF(left + pw * zoom * slot, top, left + pw * zoom * (slot + 1), top + total_h);
            ++slot;
            // A soft shadow under the sheet.
            for (int layer = 3; layer >= 1; --layer) {
                const float spread = layer * 6.0f;
                brush(gray(0, 0.10f * a)); target_->FillRectangle(D2D1::RectF(r.left - spread * 0.5f, r.top + spread * 0.3f, r.right + spread * 0.5f, r.bottom + spread), solid_.Get());
            }
            if (auto* image = manualPage(page)) {
                target_->DrawBitmap(image->bitmap.Get(), r, a * reveal(*image), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            } else {
                brush(gray(0.90f, 0.10f * a)); target_->FillRectangle(r, solid_.Get());
                // A page the engine could not draw says so; one on its way waits as a pale sheet.
                if (state->isBroken(page))
                    paragraph(tr(L"Nie można wyświetlić tej strony."), r.left + 24, (r.top + r.bottom) * 0.5f - 24, r.right - r.left - 48, settings_body_font_.Get(), gray(0.80f, a), 3);
                else textLine(L"…", r.left, (r.top + r.bottom) * 0.5f, r.right - r.left, settings_heading_font_.Get(), gray(0.80f, a), DWRITE_TEXT_ALIGNMENT_CENTER);
            }
        }
        // The fold between two pages.
        if (count == 2) {
            const float fold = left + pw * zoom;
            brush(gray(0, 0.22f * a)); target_->FillRectangle(D2D1::RectF(fold - 1.5f, top, fold + 1.5f, top + total_h), solid_.Get());
        }
        target_->PopAxisAlignedClip();
    }
    // The hints: the system's own, in the bottom right corner (input_hints.cpp).
}
