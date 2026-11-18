#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "blades_layout.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

// Blades: the Xbox 360-style home (blades_layout.hpp). Flat colours and thin arcs instead of the
// original's glossy gradients: the channel tabs fan out along the arc on the left like the 2005
// blades, the games are 2008-style panels receding along a rising arc to the right.

using namespace render_detail;

namespace {
D2D1_COLOR_F blend(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return D2D1::ColorF(std::lerp(a.r, b.r, t), std::lerp(a.g, b.g, t), std::lerp(a.b, b.b, t), std::lerp(a.a, b.a, t));
}
D2D1_COLOR_F withAlpha(D2D1_COLOR_F color, float alpha) { color.a = alpha; return color; }
}

void Renderer::drawBlades() {
    using namespace blades_layout;
    const auto& cards = *cards_;
    const float width = viewport_.width, height = viewport_.height;
    const bool light = context_->blades_light;
    auto accent = context_->blades_green ? color(0.36f, 0.73f, 0.13f) : uiAccent();
    accent.a = 1;
    const auto page = light ? color(0.905f, 0.912f, 0.92f) : color(0.052f, 0.058f, 0.066f);
    const auto ink = [&](float alpha) { return light ? color(0.07f, 0.08f, 0.09f, alpha) : color(0.96f, 0.97f, 0.98f, alpha); };

    // The page: one flat colour, the blades' disc a shade apart, and thin arcs of the accent across it.
    brush(page);
    target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    const float radius = arcRadius(height), cx = arcCenterX(height), cy = arcCenterY(height);
    // The floor: its rim rises to the right under the receding panels (the 2008 arc), below the
    // focused game's name.
    const float base = focusSize(height);
    const auto floorCurve = [&](float lift, bool closed) {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(factory_->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) return path;
        sink->BeginFigure(D2D1::Point2F(-10, height * 0.835f + lift), closed ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(D2D1::Point2F(width * 0.47f, height * 0.80f + lift),
                                                             D2D1::Point2F(width + 10, height * 0.435f + lift)));
        if (closed) { sink->AddLine(D2D1::Point2F(width + 10, height + 10)); sink->AddLine(D2D1::Point2F(-10, height + 10)); }
        sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        if (FAILED(sink->Close())) path.Reset();
        return path;
    };
    if (const auto floor = floorCurve(0, true)) {
        brush(withAlpha(accent, light ? 0.075f : 0.06f));
        target_->FillGeometry(floor.Get(), solid_.Get());
    }
    if (const auto rim = floorCurve(0, false)) {
        brush(withAlpha(accent, light ? 0.6f : 0.45f));
        target_->DrawGeometry(rim.Get(), solid_.Get(), 2.0f);
    }
    if (const auto rim = floorCurve(26, false)) {
        brush(withAlpha(accent, 0.15f));
        target_->DrawGeometry(rim.Get(), solid_.Get(), 1.0f);
    }
    // The blades' disc on the left, with its rings.
    brush(light ? color(0.85f, 0.86f, 0.875f) : color(0.082f, 0.09f, 0.1f));
    target_->FillEllipse(D2D1::Ellipse({cx, cy}, radius + 10, radius + 10), solid_.Get());
    for (int ring = 0; ring < 3; ++ring) {
        const float r = radius + 130 + ring * 150.0f;
        brush(withAlpha(accent, (light ? 0.16f : 0.11f) - ring * 0.03f));
        target_->DrawEllipse(D2D1::Ellipse({cx, cy}, r, r), solid_.Get(), 1.25f);
    }
    brush(withAlpha(accent, light ? 0.6f : 0.5f));
    target_->DrawEllipse(D2D1::Ellipse({cx, cy}, radius + 10, radius + 10), solid_.Get(), 2.0f);
    beginScreenForeground();

    // The channel position eases like the panels do, so the tabs turn along their arc.
    const float dt = std::clamp(elapsed_ - blades_time_, 0.0f, 0.1f);
    blades_time_ = elapsed_;
    const float target_channel = static_cast<float>(context_->blades_channel);
    if (blades_channel_ < 0 || std::abs(target_channel - blades_channel_) > 12) blades_channel_ = target_channel;
    else blades_channel_ += (target_channel - blades_channel_) * (1.0f - std::exp(-dt * 11.0f));
    if (std::abs(target_channel - blades_channel_) < 0.001f) blades_channel_ = target_channel;

    D2D1_MATRIX_3X2_F screen{};
    target_->GetTransform(&screen);
    const auto& channels = context_->blades_channels;
    for (int pass = 0; pass < 2; ++pass) // The focused tab last, over its neighbours.
    for (size_t i = 0; i < channels.size(); ++i) {
        const float d = static_cast<float>(i) - blades_channel_;
        const bool front = std::abs(d) < 0.5f;
        if ((pass == 1) != front || std::abs(d) > 5.5f) continue;
        const float angle = d * tab_step;
        const float edge_x = cx + radius * std::cos(angle), edge_y = cy + radius * std::sin(angle);
        const float focus = std::max(0.0f, 1.0f - std::abs(d));
        const float fade = std::clamp(1.0f - (std::abs(d) - 1.0f) * 0.22f, 0.0f, 1.0f);
        const float tall = std::lerp(tab_height, focus_tab_height, focus), out = 44 * focus;
        // Each tab lies along a ray from the disc's centre, like a blade of a fan.
        target_->SetTransform(D2D1::Matrix3x2F::Rotation(angle * 180.0f / std::numbers::pi_v<float>, D2D1::Point2F(edge_x, edge_y)) * screen);
        const auto box = D2D1::RectF(edge_x - tab_width, edge_y - tall * 0.5f, edge_x + out, edge_y + tall * 0.5f);
        const auto idle = light ? color(1, 1, 1, 0.72f * fade) : color(1, 1, 1, 0.06f * fade);
        brush(blend(idle, accent, focus));
        target_->FillRoundedRectangle(D2D1::RoundedRect(box, 6, 6), solid_.Get());
        if (focus < 0.99f) {
            brush(withAlpha(accent, (light ? 0.5f : 0.35f) * fade * (1 - focus)));
            target_->FillRectangle(D2D1::RectF(box.right - 4, box.top + 8, box.right - 1, box.bottom - 8), solid_.Get());
        }
        const auto label = blend(ink(0.66f * fade), color(1, 1, 1, 1), focus);
        textLine(channels[i], box.left + 30, edge_y, box.right - box.left - 52, focus > 0.5f ? category_font_.Get() : menu_font_.Get(), label);
    }
    target_->SetTransform(screen);

    // The panels: the far ones first, then those that passed the front, the focused one last.
    const float clip_left = cx + radius + 24;
    const auto clip = D2D1::RectF(clip_left, 0, width, height);
    const auto panel = [&](size_t i) {
        const auto& card = cards[i];
        if (card.x > width + 40 || card.x + card.width() < clip_left - 40) return;
        const int offset = static_cast<int>(i) - static_cast<int>(selected_);
        auto shown = card;
        shown.opacity *= panelAlpha(offset);
        if (shown.opacity < 0.01f) return;
        drawCard(shown, i);
    };
    target_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t i = cards.size(); i-- > selected_ + 1;) panel(i);
    for (size_t i = 0; i < selected_; ++i) panel(i);
    target_->PopAxisAlignedClip();
    const float launch = smoothFade(context_->launch_progress);
    if (launch > 0.001f) {
        brush(color(0.008f, 0.012f, 0.016f, launch * 0.82f));
        target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    }
    const auto& focused = cards[selected_];
    target_->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    drawCard(focused, selected_);
    target_->PopAxisAlignedClip();

    // The focused game's name and state under its panel, where the floor's arc leaves room.
    const float text_alpha = focused.opacity * (1 - launch);
    const float name_y = panelTop(0, height) + base + 44;
    const float text_left = panelsLeft(height);
    textLine(focused.game.name, text_left, name_y, std::max(120.0f, width - text_left - 120), psfive_name_font_.Get(), ink(text_alpha));
    std::wstring line_text = context_->blades_status;
    if (cards.size() > 1) {
        const auto position = std::to_wstring(selected_ + 1) + L" / " + std::to_wstring(cards.size());
        line_text = line_text.empty() ? position : line_text + L"   ·   " + position;
    }
    if (!line_text.empty())
        textLine(line_text, text_left, name_y + 38, std::max(120.0f, width - text_left - 120), settings_body_font_.Get(), ink(0.6f * text_alpha));
    brush(withAlpha(accent, text_alpha));
    target_->FillRectangle(D2D1::RectF(text_left, name_y - 34, text_left + 36, name_y - 30), solid_.Get());

    if (!context_->blades_clock.empty())
        textLine(context_->blades_clock, width - 300, 64, 220, psfive_clock_font_.Get(), ink(0.9f), DWRITE_TEXT_ALIGNMENT_TRAILING);
    drawQuickShade();
}
