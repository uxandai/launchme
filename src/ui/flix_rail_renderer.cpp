// Renderer: Flix → Pasek boczny (src/ui/flix_rail.hpp). A dark column at the left edge fading into
// the page; each icon a glass disc (the pad's one white with a dark glyph, as Flix's top icons), the
// row in view's own icon marked by an accent bar at the edge; the focused or hovered icon names itself
// in a pill beside the column. The profile shows her face while she is there.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "flix_rail.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

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
    // The pad's white disc springs between the icons (ViewContext::flix_rail_glide, a fractional icon);
    // an icon turns dark as the disc reaches it.
    const float last = static_cast<float>(items.size() - 1);
    const std::optional<float> glide = focus < 0 ? std::nullopt :
        std::optional<float>(std::clamp(context_->flix_rail_glide.value_or(static_cast<float>(focus)), 0.0f, last));
    const auto centreAt = [&](float index) {
        const auto a = layout.item(static_cast<size_t>(std::floor(index))), b = layout.item(static_cast<size_t>(std::ceil(index)));
        const float t = index - std::floor(index);
        return std::lerp((a.top + a.bottom) * 0.5f, (b.top + b.bottom) * 0.5f, t);
    };
    for (size_t i = 0; i < items.size(); ++i) {
        const auto r = layout.item(i);
        const float cy = (r.top + r.bottom) * 0.5f;
        if (active == static_cast<int>(i)) { // The row in view: an accent bar at the column's edge.
            brush(uiAccent(alpha));
            target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(4, cy - 14, 8, cy + 14), 2, 2), solid_.Get());
        }
        const float focus_amount = glide ? std::clamp(1.0f - std::abs(static_cast<float>(i) - *glide), 0.0f, 1.0f) : 0.0f;
        if (focus_amount < 0.999f) drawWidgetPill(D2D1::RectF(r.left, r.top, r.right, r.bottom), alpha * (1.0f - focus_amount), hover == static_cast<int>(i) ? 1.0f : 0.0f);
    }
    if (glide) {
        const auto r = layout.item(0);
        const float cx = (r.left + r.right) * 0.5f, radius = (r.right - r.left) * 0.5f;
        brush(gray(0.96f, alpha));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, centreAt(*glide)), radius + 2, radius + 2), solid_.Get());
    }
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        const auto r = layout.item(i);
        const float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f, radius = (r.right - r.left) * 0.5f;
        const float focus_amount = glide ? std::clamp(1.0f - std::abs(static_cast<float>(i) - *glide), 0.0f, 1.0f) : 0.0f;
        const float light = active == static_cast<int>(i) ? 0.98f : 0.86f;
        const auto ink = gray(std::lerp(light, 0.06f, focus_amount), alpha);
        if (item.kind == flix_rail::Kind::profile && context_->companion)
            drawCompanionFace(context_->companion->look, context_->companion->pose, cx, cy, radius - std::lerp(1.0f, 3.0f, focus_amount), alpha, context_->companion->costume);
        else desktopGlyph(item.glyph, cx, cy, 22, ink);
    }
    // The pad's (else the pointer's) icon names itself beside the column, its pill riding with the disc.
    const int named = focus >= 0 ? focus : hover;
    if (named >= 0 && named < static_cast<int>(items.size())) {
        const auto r = layout.item(static_cast<size_t>(named));
        const auto& label = items[static_cast<size_t>(named)].label;
        const float w = std::min(360.0f, textAdvance(label, settings_body_font_.Get()) + 32);
        const float cy = focus >= 0 && glide ? centreAt(*glide) : (r.top + r.bottom) * 0.5f;
        const auto pill = D2D1::RectF(flix_rail::width + 4, cy - 19, flix_rail::width + 4 + w, cy + 19);
        brush(gray(0.08f, 0.92f * alpha)); target_->FillRoundedRectangle(D2D1::RoundedRect(pill, 19, 19), solid_.Get());
        textLine(label, pill.left + 16, cy, w - 32, settings_body_font_.Get(), gray(0.96f, alpha));
    }
}
