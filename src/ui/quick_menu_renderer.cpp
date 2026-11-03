// Renderer: the quick bar (Home/Guide) and the shade it casts over the home.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "quick_menu.hpp"
#include "snitch_layout.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace render_detail;

void Renderer::drawQuickShade() {
    if (context_->snitch) return;
    const float alpha = smoothFade(context_->quick_progress);
    if (alpha < 0.004f) return;
    brush(color(0, 0, 0, 0.73f * alpha));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), solid_.Get());
}

void Renderer::drawQuickPanel() {
    const bool snitch = context_->snitch;
    const float focus_alpha = smoothFade(context_->quick_progress);
    const float alpha = snitch ? 1.0f : focus_alpha;
    if (alpha < 0.004f) return;
    const float cy = quick_menu::centerY(viewport_.height, context_->quick_progress, context_->guide_overlay);
    const float width = viewport_.width;
    const int store = static_cast<int>(context_->plugin_menu.size());
    const bool home = context_->guide_overlay;
    const auto screen = D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale);
    const float compact = snitch ? snitch_layout::dockScale(width, store) : 1.0f;
    const auto base = snitch ? D2D1::Matrix3x2F::Scale(compact, compact) *
        D2D1::Matrix3x2F::Translation(width * (1 - compact) * 0.5f, snitch_layout::dockY(viewport_.height) - cy * compact) * screen : screen;
    target_->SetTransform(base);
    if (snitch) for (int i = 0; i < quick_menu::count(store); ++i) {
        brush(color(0.31f, 0.31f, 0.31f));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(quick_menu::center(width, i, store), cy), 44, 44), solid_.Get());
    }
    if (quick_fade_ && !snitch) {
        // Black rising from the bottom edge over the tiles still under the icons (Flix: the next
        // row), above the cards and below the bar. It grows a little while the bar comes in.
        constexpr float reach = 240;
        const float top = viewport_.height - reach * (0.6f + 0.4f * alpha);
        quick_fade_->SetStartPoint(D2D1::Point2F(0, viewport_.height));
        quick_fade_->SetEndPoint(D2D1::Point2F(0, top));
        quick_fade_->SetOpacity(alpha);
        target_->FillRectangle(D2D1::RectF(0, top, width, viewport_.height), quick_fade_.Get());
    }
    // The focus circle glides between icons (src/app/focus_motion.cpp): translucent while it
    // travels, white once it rests on the selection, where the icon turns dark.
    const float target_x = quick_menu::center(width, context_->quick_selected, store, home);
    const float circle_x = context_->quick_circle_x.value_or(target_x);
    const float solid = 1.0f - 0.72f * smoothFade(std::abs(circle_x - target_x) / 58.0f);
    // As on the PS5: a thin ring around the focused circle, a small dark gap away from it. The
    // ring stays; its opacity travels - a brighter stretch slowly circles it clockwise.
    const auto ring = [&](float radius, float stroke, float opacity) {
        if (opacity < 0.004f) return;
        const auto ellipse = D2D1::Ellipse(D2D1::Point2F(circle_x, cy), radius, radius);
        if (!quick_ring_) {
            brush(color(0.93f, 0.93f, 0.93f, 0.85f * opacity));
            target_->DrawEllipse(ellipse, solid_.Get(), stroke);
            return;
        }
        constexpr float period = 4.0f; // Seconds per lap.
        const float pi = std::numbers::pi_v<float>;
        const float angle = std::fmod(std::max(0.0f, context_->quick_ring_time) / period, 1.0f) * 2 * pi - pi * 0.5f;
        const float dx = std::cos(angle) * radius, dy = std::sin(angle) * radius;
        quick_ring_->SetStartPoint(D2D1::Point2F(circle_x + dx, cy + dy));
        quick_ring_->SetEndPoint(D2D1::Point2F(circle_x - dx, cy - dy));
        quick_ring_->SetOpacity(opacity);
        target_->DrawEllipse(ellipse, quick_ring_.Get(), stroke);
    };
    if (snitch) ring(48, 2.5f, solid * focus_alpha); // Around the dock's 44 discs.
    else {
        brush(color(0.93f, 0.93f, 0.93f, solid * focus_alpha));
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(circle_x, cy), 36, 36), solid_.Get());
        // Only once the circle rests on an icon: a 3 px gap, then a 2 px ring.
        ring(36 + 3 + 1, 2.0f, smoothFade((solid - 0.28f) / 0.72f) * focus_alpha);
    }
    const auto glyph = [&](quick_menu::Action action, float x, D2D1_COLOR_F ink, int index) { drawQuickGlyph(action, x, cy, ink, index, home); };
    const int count = quick_menu::count(store, home);
    for (int i = 0; i < count; ++i) {
        const float x = quick_menu::center(width, i, store, home);
        const float lit = std::clamp(1.0f - std::abs(x - circle_x) / 116.0f, 0.0f, 1.0f);
        glyph(quick_menu::action(i, store, home), x, color(0.94f, 0.94f, 0.94f, (snitch ? 1.0f : std::lerp(0.67f, 0.99f, lit)) * alpha), i);
    }
    // The same icons in dark, clipped to the circle: they flip exactly under its edge.
    ComPtr<ID2D1EllipseGeometry> circle;
    if (!quick_layer_) target_->CreateLayer(quick_layer_.GetAddressOf());
    if (!snitch && quick_layer_ && SUCCEEDED(factory_->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(circle_x, cy), 36, 36), circle.GetAddressOf()))) {
        target_->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), circle.Get()), quick_layer_.Get());
        for (int i = 0; i < count; ++i) {
            const float x = quick_menu::center(width, i, store, home);
            if (std::abs(x - circle_x) < 36 + 26) glyph(quick_menu::action(i, store, home), x, color(0.07f, 0.075f, 0.085f, solid * alpha), i);
        }
        target_->PopLayer();
    }
    // Pobierania: how many games are transferring now, in an accent dot at the icon's corner.
    if (context_->downloads_moving > 0) for (int i = 0; i < count; ++i) {
        if (quick_menu::action(i, store, home) != quick_menu::Action::downloads) continue;
        const float bx = quick_menu::center(width, i, store, home) + 19, by = cy - 19;
        brush(uiAccent(alpha)); target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bx, by), 10, 10), solid_.Get());
        const auto accent = uiAccent();
        const bool light = accent.r * 0.3f + accent.g * 0.59f + accent.b * 0.11f > 0.6f;
        textLine(std::to_wstring(std::min(context_->downloads_moving, 9)), bx - 10, by, 20, friend_section_font_.Get(),
                 light ? color(0.05f, 0.05f, 0.06f, alpha) : color(0.98f, 0.98f, 0.98f, alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    const float lit = std::clamp(1.0f - std::abs(target_x - circle_x) / 116.0f, 0.0f, 1.0f);
    const auto action = quick_menu::action(context_->quick_selected, store, home);
    // Readable from the sofa: 17 px under the bar; the dock is drawn scaled down, so a larger face
    // there lands at about the same size on screen.
    const float caption_width = (action == quick_menu::Action::dashboard ? 180.0f : 150.0f) * (snitch ? 1.3f : 1.0f);
    textLine(action == quick_menu::Action::store ? context_->plugin_menu[quick_menu::pluginIndex(context_->quick_selected, home)] : std::wstring(quick_menu::caption(action)),
             target_x - caption_width * 0.5f, cy + (snitch ? 70 : 51), caption_width, snitch ? category_font_.Get() : toast_font_.Get(),
             color(0.9f, 0.9f, 0.9f, focus_alpha * smoothFade(lit)), DWRITE_TEXT_ALIGNMENT_CENTER);
    target_->SetTransform(screen);
}

// One quick-bar icon centred on (x, cy), about 44 px across: the bar and the Guide overlay's bar.
void Renderer::drawQuickGlyph(quick_menu::Action action, float x, float cy, D2D1_COLOR_F ink, int index, bool home) {
    brush(ink);
    if (action == quick_menu::Action::dashboard) {
        line(x - 20, cy - 2, x, cy - 20, ink, 2.6f);
        line(x, cy - 20, x + 20, cy - 2, ink, 2.6f);
        line(x - 14, cy - 4, x - 14, cy + 18, ink, 2.6f);
        line(x + 14, cy - 4, x + 14, cy + 18, ink, 2.6f);
        line(x - 14, cy + 18, x + 14, cy + 18, ink, 2.6f);
        target_->DrawRectangle(D2D1::RectF(x - 5, cy + 5, x + 5, cy + 18), solid_.Get(), 2.3f);
    } else if (action == quick_menu::Action::close_game) {
        line(x - 13, cy - 13, x + 13, cy + 13, ink, 2.8f);
        line(x + 13, cy - 13, x - 13, cy + 13, ink, 2.8f);
    } else if (action == quick_menu::Action::search) {
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x - 4, cy - 4), 12, 12), solid_.Get(), 2.7f);
        line(x + 5, cy + 5, x + 18, cy + 18, ink, 3);
    } else if (action == quick_menu::Action::library) {
        // 2x2 tile grid: the same destination as the "Wszystkie gry" shelf tile.
        for (int tile = 0; tile < 4; ++tile) {
            const float tx = x + (tile % 2 ? 3.0f : -17.0f), ty = cy + (tile / 2 ? 3.0f : -17.0f);
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(tx, ty, tx + 14, ty + 14), 3.5f, 3.5f), solid_.Get(), 2.3f);
        }
    } else if (action == quick_menu::Action::store) {
        const auto plugin = static_cast<size_t>(quick_menu::pluginIndex(index, home));
        if (plugin < context_->plugin_menu_glyphs.size() && context_->plugin_menu_glyphs[plugin] == "server-download") {
            // Two server bays and a downward transfer arrow, distinct from the Store bag.
            for (int bay = 0; bay < 2; ++bay) {
                const float top = cy - 17 + bay * 18.0f;
                target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 23, top, x + 1, top + 13), 3, 3), solid_.Get(), 2.4f);
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x - 16, top + 6.5f), 1.7f, 1.7f), solid_.Get());
            }
            line(x + 15, cy - 17, x + 15, cy + 15, ink, 2.5f);
            line(x + 7, cy + 7, x + 15, cy + 15, ink, 2.5f);
            line(x + 23, cy + 7, x + 15, cy + 15, ink, 2.5f);
        } else {
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 16, cy - 10, x + 16, cy + 17), 4, 4), solid_.Get(), 2.4f);
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 8, cy - 20, x + 8, cy - 2), 7, 7), solid_.Get(), 2.4f);
        }
    } else if (action == quick_menu::Action::downloads) {
        drawDownloadGlyph(x, cy, 1, ink);
    } else if (action == quick_menu::Action::widgets) {
        // The same asymmetric widget layout as the PSfive drawer icon.
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 19, cy - 17, x - 3, cy + 17), 3.5f, 3.5f), solid_.Get(), 2.4f);
        for (int row = 0; row < 2; ++row) {
            const float top = cy - 17 + row * 20.0f;
            target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 4, top, x + 19, top + 14), 3, 3), solid_.Get(), 2.4f);
        }
    } else if (action == quick_menu::Action::controllers) {
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x - 22, cy - 13, x + 22, cy + 15), 10, 10), solid_.Get(), 2.4f);
        line(x - 16, cy, x - 4, cy, ink, 2.4f); line(x - 10, cy - 6, x - 10, cy + 6, ink, 2.4f);
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 9, cy - 4), 2.5f, 2.5f), solid_.Get());
        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 15, cy + 3), 2.5f, 2.5f), solid_.Get());
    } else if (action == quick_menu::Action::audio) {
        // Speaker with the system volume: x when muted or silent, then ), )) and ))).
        const float dx = -4;
        target_->DrawRectangle(D2D1::RectF(x + dx - 18, cy - 6, x + dx - 9, cy + 6), solid_.Get(), 2.3f);
        line(x + dx - 9, cy - 6, x + dx + 1, cy - 16, ink, 2.3f); line(x + dx + 1, cy - 16, x + dx + 1, cy + 16, ink, 2.3f);
        line(x + dx + 1, cy + 16, x + dx - 9, cy + 6, ink, 2.3f);
        const float level = context_->volume_level;
        if (context_->volume_muted || (level >= 0 && level < 0.005f)) {
            line(x + dx + 8, cy - 6, x + dx + 19, cy + 6, ink, 2.3f);
            line(x + dx + 8, cy + 6, x + dx + 19, cy - 6, ink, 2.3f);
        } else {
            // Sound waves as round arcs around the speaker's mouth.
            if (!tarot_stroke_)
                factory_->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                                        D2D1_LINE_JOIN_ROUND), nullptr, 0, tarot_stroke_.GetAddressOf());
            const int waves = level < 0 ? 1 : level < 0.34f ? 1 : level < 0.67f ? 2 : 3;
            const float centre = x + dx + 1;
            for (int wave = 0; wave < waves; ++wave) {
                const float radius = 9.0f + wave * 6.0f, half = 0.82f;
                ComPtr<ID2D1PathGeometry> path;
                ComPtr<ID2D1GeometrySink> sink;
                if (FAILED(factory_->CreatePathGeometry(path.GetAddressOf())) || FAILED(path->Open(sink.GetAddressOf()))) break;
                sink->BeginFigure(D2D1::Point2F(centre + std::cos(-half) * radius, cy + std::sin(-half) * radius), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(centre + std::cos(half) * radius, cy + std::sin(half) * radius), D2D1::SizeF(radius, radius), 0,
                                              D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                if (FAILED(sink->Close())) break;
                target_->DrawGeometry(path.Get(), solid_.Get(), 2.3f, tarot_stroke_.Get());
            }
        }
    } else if (action == quick_menu::Action::settings) {
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), 12, 12), solid_.Get(), 2.3f);
        target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), 4.3f, 4.3f), solid_.Get(), 2.0f);
        for (int spoke = 0; spoke < 8; ++spoke) {
            const float angle = spoke * std::numbers::pi_v<float> / 4;
            line(x + std::cos(angle) * 14, cy + std::sin(angle) * 14,
                 x + std::cos(angle) * 20, cy + std::sin(angle) * 20, ink, 2.8f);
        }
    } else {
        // Power: an open ring (no cut-out painted over it, so it works on the white circle too).
        if (!power_arc_) {
            constexpr float r = 16, gap = 0.72f; // Half the opening at the top, radians.
            const float half_pi = std::numbers::pi_v<float> * 0.5f;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(factory_->CreatePathGeometry(power_arc_.GetAddressOf())) && SUCCEEDED(power_arc_->Open(sink.GetAddressOf()))) {
                sink->BeginFigure(D2D1::Point2F(r * std::cos(-half_pi + gap), r * std::sin(-half_pi + gap)), D2D1_FIGURE_BEGIN_HOLLOW);
                sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(r * std::cos(-half_pi - gap), r * std::sin(-half_pi - gap)), D2D1::SizeF(r, r), 0,
                                              D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                if (FAILED(sink->Close())) power_arc_.Reset();
            } else power_arc_.Reset();
        }
        if (power_arc_) {
            D2D1_MATRIX_3X2_F base{};
            target_->GetTransform(&base);
            target_->SetTransform(D2D1::Matrix3x2F::Translation(x, cy + 2) * base);
            target_->DrawGeometry(power_arc_.Get(), solid_.Get(), 2.5f);
            target_->SetTransform(base);
        }
        line(x, cy - 21, x, cy - 1, ink, 2.9f);
    }
}
