// Renderer: Flix → Pasek boczny (src/ui/flix_rail.hpp). A dark column at the left edge fading into
// the page; each icon a glass disc (the pad's one white with a dark glyph, as Flix's top icons), the
// row in view's own icon marked by an accent bar at the edge; the focused or hovered icon names itself
// in a pill beside the column. The profile shows her face while she is there.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "flix_rail.hpp"

#include <algorithm>

using namespace render_detail;

namespace {
using palette::gray;
}

void Renderer::drawFlixRail() {
    const auto& items = context_->flix_rail;
    const float alpha = context_->flix_rail_alpha;
    if (items.empty() || alpha < 0.004f) return;
    const flix_rail::Layout layout{viewport_.height, &items};
    // The column: dark at the edge, clear where the rows begin.
    {
        Microsoft::WRL::ComPtr<ID2D1GradientStopCollection> stops;
        const D2D1_GRADIENT_STOP gradient[]{{0.0f, D2D1::ColorF(0, 0, 0, 0.78f * alpha)}, {0.62f, D2D1::ColorF(0, 0, 0, 0.55f * alpha)},
                                            {1.0f, D2D1::ColorF(0, 0, 0, 0.0f)}};
        Microsoft::WRL::ComPtr<ID2D1LinearGradientBrush> shade;
        if (SUCCEEDED(target_->CreateGradientStopCollection(gradient, 3, &stops)) &&
            SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(flix_rail::width + 28, 0)),
                                                         stops.Get(), &shade)))
            target_->FillRectangle(D2D1::RectF(0, 0, flix_rail::width + 28, viewport_.height), shade.Get());
    }
    const int focus = context_->flix_rail_focus, hover = context_->flix_rail_hover, active = context_->flix_rail_active;
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        const auto r = layout.item(i);
        const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f, radius = (r.right - r.left) * 0.5f;
        const bool focused = focus == static_cast<int>(i), hovered = hover == static_cast<int>(i);
        if (active == static_cast<int>(i)) { // The row in view: an accent bar at the column's edge.
            brush(uiAccent(alpha));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(4, cy - 14, 8, cy + 14), 2, 2), solid_.Get());
        }
        if (focused) {
            brush(gray(0.96f, alpha));
            target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius + 2, radius + 2), solid_.Get());
        } else drawWidgetPill(D2D1::RectF(r.left, r.top, r.right, r.bottom), alpha, hovered ? 1.0f : 0.0f);
        const auto ink = focused ? gray(0.06f, alpha) : gray(active == static_cast<int>(i) ? 0.98f : 0.86f, alpha);
        if (item.kind == flix_rail::Kind::profile && context_->companion)
            drawCompanionFace(context_->companion->look, context_->companion->pose, cx, cy, radius - (focused ? 3.0f : 1.0f), alpha, context_->companion->costume);
        else desktopGlyph(item.glyph, cx, cy, 22, ink);
    }
    // The pad's (else the pointer's) icon names itself beside the column.
    const int named = focus >= 0 ? focus : hover;
    if (named >= 0 && named < static_cast<int>(items.size())) {
        const auto r = layout.item(static_cast<size_t>(named));
        const auto& label = items[static_cast<size_t>(named)].label;
        const float w = std::min(360.0f, textAdvance(label, settings_body_font_.Get()) + 32), cy = (r.top + r.bottom) * 0.5f;
        const auto pill = D2D1::RectF(flix_rail::width + 4, cy - 19, flix_rail::width + 4 + w, cy + 19);
        brush(gray(0.08f, 0.92f * alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(pill, 19, 19), solid_.Get());
        textLine(label, pill.left + 16, cy, w - 32, settings_body_font_.Get(), gray(0.96f, alpha));
    }
}
