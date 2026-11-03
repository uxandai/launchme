// Text on Direct2D render targets: the brush becomes the paint and the DirectWrite port
// (dwrite_*.cpp, Impeller's typography) lays the glyphs out and draws them (lm_gfx::drawText/drawTextLayout).
#include "d2d_internal.hpp"
#include "../win32/dwrite.h"

namespace lm_d2d {
void Canvas::drawText(const WCHAR* text, UINT32 length, IDWriteTextFormat* format, const D2D1_RECT_F* rect, ID2D1Brush* brush,
                      D2D1_DRAW_TEXT_OPTIONS options) {
    gx::Paint paint;
    gx::Canvas* canvas = text && format && rect && brushPaint(brush, paint) ? prepare() : nullptr;
    if (!canvas) return;
    lm_gfx::drawText(canvas, text, length, format, rect->left, rect->top, rect->right, rect->bottom, (options & D2D1_DRAW_TEXT_OPTIONS_CLIP) != 0,
                     paint, text_antialias_ == D2D1_TEXT_ANTIALIAS_MODE_ALIASED);
}

// D2D1_DRAW_TEXT_OPTIONS_CLIP: nothing outside the layout's box (its maximum width and height).
void Canvas::drawTextLayout(D2D1_POINT_2F origin, IDWriteTextLayout* layout, ID2D1Brush* brush, D2D1_DRAW_TEXT_OPTIONS options) {
    gx::Paint paint;
    gx::Canvas* canvas = layout && brushPaint(brush, paint) ? prepare() : nullptr;
    if (!canvas) return;
    const int restore_to = canvas->saveCount();
    canvas->save();
    DWRITE_TEXT_METRICS metrics{};
    if ((options & D2D1_DRAW_TEXT_OPTIONS_CLIP) && SUCCEEDED(layout->GetMetrics(&metrics)))
        canvas->clipRect(gx::Rect::XYWH(origin.x, origin.y, metrics.layoutWidth, metrics.layoutHeight));
    lm_gfx::drawTextLayout(canvas, layout, origin.x, origin.y, paint, text_antialias_ == D2D1_TEXT_ANTIALIAS_MODE_ALIASED);
    canvas->restoreToCount(restore_to);
}
} // namespace lm_d2d
