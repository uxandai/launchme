// One-shot exit: one GPU-local snapshot, a slight curved retreat, then a vertical
// light trail in the All games backdrop's cyan. No shader, blur or CPU readback.
#include "renderer.hpp"

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
float ease(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
D2D1_COLOR_F exitLight(uint32_t rgb, float alpha) {
    return D2D1::ColorF(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, alpha);
}
D2D1_COLOR_F exitCore(uint32_t rgb, float alpha) {
    const float r = ((rgb >> 16) & 255) / 255.0f, g = ((rgb >> 8) & 255) / 255.0f, b = (rgb & 255) / 255.0f;
    return D2D1::ColorF(std::lerp(r, 1.0f, 0.84f), std::lerp(g, 1.0f, 0.84f), std::lerp(b, 1.0f, 0.84f), alpha);
}
}

void Renderer::setExitColor(uint32_t rgb) {
    const uint32_t clean = rgb ? rgb : 0x57D1F0;
    if (clean == exit_color_) return;
    exit_color_ = clean;
    exit_line_.Reset();
    exit_glow_.Reset();
}

void Renderer::beginScreenForeground() {
    if (main_target_ || context_->screen_transition >= 0.999f || context_->launch_animation_seconds >= 0) return;
    const auto pixels = target_->GetPixelSize();
    if (!screen_ui_target_ || screen_ui_target_->GetPixelSize().width != pixels.width ||
        screen_ui_target_->GetPixelSize().height != pixels.height) {
        screen_ui_target_.Reset();
        const auto format = D2D1::PixelFormat(target_->GetPixelFormat().format, D2D1_ALPHA_MODE_PREMULTIPLIED);
        if (FAILED(target_->CreateCompatibleRenderTarget(target_->GetSize(), pixels, format,
            D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, screen_ui_target_.GetAddressOf()))) return;
    }
    main_target_ = target_;
    main_realizer_ = realizer_;
    target_ = screen_ui_target_;
    realizer_.Reset();
    target_.As(&realizer_);
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale));
    target_->Clear(D2D1::ColorF(0, 0, 0, 0));
}

HRESULT Renderer::endScreenForeground() {
    if (!main_target_) return S_OK;
    const auto result = target_->EndDraw();
    target_ = std::move(main_target_);
    realizer_ = std::move(main_realizer_);
    ComPtr<ID2D1Bitmap> foreground;
    if (FAILED(result)) return result;
    const auto bitmap_result = screen_ui_target_->GetBitmap(foreground.GetAddressOf());
    if (FAILED(bitmap_result)) return bitmap_result;
    drawScreenTransition(context_->screen_transition, foreground.Get());
    return S_OK;
}

void Renderer::drawScreenTransition(float progress, ID2D1Bitmap* ui_frame) {
    const float t = std::clamp(progress, 0.0f, 1.0f);
    if (t >= 0.999f) return;
    const float inv = 1.0f - t;
    const float opacity = 1.0f - std::pow(inv, 2.4f);
    const float u_zoom = inv * inv * inv;
    const float u_bend = inv * inv;
    const auto pixels = target_->GetPixelSize();
    const auto size = target_->GetSize();
    if (ui_frame && context_ && context_->desktop_window) {
        // The desktop window: a page or dialog only fades in, rising from 98%, over the still window
        // behind it; the TV mode's zoom and bow would distort the whole interface.
        const float ease = 1.0f - inv * inv * inv;
        const float zoom = 0.98f + 0.02f * ease;
        target_->SetTransform(D2D1::Matrix3x2F::Scale(zoom, zoom, D2D1::Point2F(size.width * 0.5f, size.height * 0.5f)));
        target_->DrawBitmap(ui_frame, D2D1::RectF(0, 0, size.width, size.height), ease, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        target_->SetTransform(D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale));
        return;
    }
    if (!ui_frame && pixels.width && pixels.height && (!screen_frame_ ||
        screen_frame_->GetPixelSize().width != pixels.width ||
        screen_frame_->GetPixelSize().height != pixels.height)) {
        screen_frame_.Reset();
        const auto properties = D2D1::BitmapProperties(target_->GetPixelFormat(), 96, 96);
        target_->CreateBitmap(pixels, properties, screen_frame_.GetAddressOf());
    }
    const bool captured = ui_frame || (screen_frame_ && SUCCEEDED(screen_frame_->CopyFromRenderTarget(nullptr, target_.Get(), nullptr)));
    if (!captured) {
        target_->SetTransform(D2D1::Matrix3x2F::Identity());
        brush(D2D1::ColorF(0.014f, 0.017f, 0.022f, 1.0f - opacity));
        target_->FillRectangle(D2D1::RectF(0, 0, size.width, size.height), solid_.Get());
        target_->SetTransform(D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale));
        return;
    }

    if (!ui_frame) {
        // Opaque panels use the original whole-screen entrance. Wallpaper screens
        // have already drawn their full backdrop and pass a transparent foreground.
        target_->SetTransform(D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale));
        target_->Clear(D2D1::ColorF(0.014f, 0.017f, 0.022f));
    }

    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    if (opacity > 0.001f && size.width >= 4 && size.height >= 4) {
        ID2D1Bitmap* frame = ui_frame ? ui_frame : screen_frame_.Get();
        const auto source = frame->GetSize();
        const float inset_base_x = size.width * 0.036f * u_zoom;
        const float inset_base_y = size.height * 0.036f * u_zoom;
        const float bow_x = size.width * 0.018f * u_bend;
        const float bow_y = size.height * 0.020f * u_bend;
        const float radial = 0.065f * u_bend;
        constexpr int strips = 128;

        // Pass 1: vertical strips bend the top and bottom edges and apply horizontal
        // radial lens foreshortening into a transparent intermediate surface.
        ComPtr<ID2D1Bitmap> bowed;
        if (!screen_bow_target_ || screen_bow_target_->GetPixelSize().width != pixels.width ||
            screen_bow_target_->GetPixelSize().height != pixels.height) {
            screen_bow_target_.Reset();
            const auto format = D2D1::PixelFormat(target_->GetPixelFormat().format, D2D1_ALPHA_MODE_PREMULTIPLIED);
            target_->CreateCompatibleRenderTarget(size, pixels, format,
                D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, screen_bow_target_.GetAddressOf());
        }
        if (screen_bow_target_) {
            screen_bow_target_->BeginDraw();
            screen_bow_target_->SetTransform(D2D1::Matrix3x2F::Identity());
            screen_bow_target_->Clear(D2D1::ColorF(0, 0, 0, 0));
            for (int i = 0; i < strips; ++i) {
                const float a = static_cast<float>(i) / strips, b = static_cast<float>(i + 1) / strips;
                const float x = a + b - 1.0f;
                const float xa = 2.0f * a - 1.0f, xb = 2.0f * b - 1.0f;
                const float inset_y = bow_y * x * x;
                const float left = std::round(a * size.width), right = std::round(b * size.width);
                if (right <= left || size.height - inset_y <= inset_y) continue;
                const float sa = 0.5f + 0.5f * xa * ((1.0f - radial) + radial * xa * xa);
                const float sb = 0.5f + 0.5f * xb * ((1.0f - radial) + radial * xb * xb);
                screen_bow_target_->DrawBitmap(frame,
                    D2D1::RectF(left, inset_y, right, size.height - inset_y),
                    1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                    D2D1::RectF(std::clamp(sa, 0.0f, 1.0f) * source.width, 0,
                                std::clamp(sb, 0.0f, 1.0f) * source.width, source.height));
            }
            if (FAILED(screen_bow_target_->EndDraw()) || FAILED(screen_bow_target_->GetBitmap(bowed.GetAddressOf())))
                bowed.Reset();
        }

        // Pass 2: horizontal strips apply the zoom, bend the left and right edges,
        // apply vertical radial foreshortening, and fade the screen in at `opacity`.
        ID2D1Bitmap* pass2_source = bowed ? bowed.Get() : frame;
        const auto src_size = pass2_source->GetSize();
        const float top_edge = inset_base_y, bottom_edge = size.height - inset_base_y;
        const float drawn_height = std::max(1.0f, bottom_edge - top_edge);
        for (int i = 0; i < strips; ++i) {
            const float a = static_cast<float>(i) / strips, b = static_cast<float>(i + 1) / strips;
            const float y = a + b - 1.0f;
            const float inset_x = inset_base_x + bow_x * y * y;
            const float top = std::round(std::lerp(top_edge, bottom_edge, a));
            const float bottom = std::round(std::lerp(top_edge, bottom_edge, b));
            if (bottom <= top || size.width - inset_x <= inset_x) continue;
            const float va = 2.0f * std::clamp((top - top_edge) / drawn_height, 0.0f, 1.0f) - 1.0f;
            const float vb = 2.0f * std::clamp((bottom - top_edge) / drawn_height, 0.0f, 1.0f) - 1.0f;
            const float sa = 0.5f + 0.5f * va * ((1.0f - radial) + radial * va * va);
            const float sb = 0.5f + 0.5f * vb * ((1.0f - radial) + radial * vb * vb);
            target_->DrawBitmap(pass2_source,
                D2D1::RectF(inset_x, top, size.width - inset_x, bottom),
                opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                D2D1::RectF(0, std::clamp(sa, 0.0f, 1.0f) * src_size.height,
                            src_size.width, std::clamp(sb, 0.0f, 1.0f) * src_size.height));
        }

        const float rim_alpha = 0.16f * opacity * u_bend;
        if (!ui_frame && rim_alpha > 0.004f && factory_) {
            ComPtr<ID2D1PathGeometry> rim;
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(factory_->CreatePathGeometry(rim.GetAddressOf())) && SUCCEEDED(rim->Open(sink.GetAddressOf()))) {
                constexpr int edge_steps = 32;
                const float corner_y = bow_y * (drawn_height / size.height);
                const auto topPoint = [&](int step) {
                    const float f = static_cast<float>(step) / edge_steps;
                    const float xn = 2.0f * f - 1.0f;
                    const float y = top_edge + corner_y * xn * xn;
                    const float yn = 2.0f * (y - top_edge) / drawn_height - 1.0f;
                    const float ix = inset_base_x + bow_x * yn * yn;
                    return D2D1::Point2F(std::lerp(ix, size.width - ix, f), y);
                };
                const auto rightPoint = [&](int step) {
                    const float f = static_cast<float>(step) / edge_steps;
                    const float y = std::lerp(top_edge + corner_y, bottom_edge - corner_y, f);
                    const float yn = 2.0f * (y - top_edge) / drawn_height - 1.0f;
                    return D2D1::Point2F(size.width - (inset_base_x + bow_x * yn * yn), y);
                };
                const auto bottomPoint = [&](int step) {
                    const float f = static_cast<float>(step) / edge_steps;
                    const float xn = 1.0f - 2.0f * f;
                    const float y = bottom_edge - corner_y * xn * xn;
                    const float yn = 2.0f * (y - top_edge) / drawn_height - 1.0f;
                    const float ix = inset_base_x + bow_x * yn * yn;
                    return D2D1::Point2F(std::lerp(size.width - ix, ix, f), y);
                };
                const auto leftPoint = [&](int step) {
                    const float f = static_cast<float>(step) / edge_steps;
                    const float y = std::lerp(bottom_edge - corner_y, top_edge + corner_y, f);
                    const float yn = 2.0f * (y - top_edge) / drawn_height - 1.0f;
                    return D2D1::Point2F(inset_base_x + bow_x * yn * yn, y);
                };
                sink->BeginFigure(topPoint(0), D2D1_FIGURE_BEGIN_HOLLOW);
                for (int s = 1; s <= edge_steps; ++s) sink->AddLine(topPoint(s));
                for (int s = 1; s <= edge_steps; ++s) sink->AddLine(rightPoint(s));
                for (int s = 1; s <= edge_steps; ++s) sink->AddLine(bottomPoint(s));
                for (int s = 1; s < edge_steps; ++s) sink->AddLine(leftPoint(s));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                if (SUCCEEDED(sink->Close())) {
                    brush(D2D1::ColorF(0.90f, 0.95f, 0.99f, rim_alpha));
                    target_->DrawGeometry(rim.Get(), solid_.Get(), 1.2f * viewport_.scale);
                }
            }
        }
    }
    target_->SetTransform(D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale));
}

void Renderer::drawExitTransition(float progress) {
    const float t = std::clamp(progress, 0.0f, 1.0f);
    if (!exit_capture_attempted_) {
        exit_capture_attempted_ = true;
        // Copy before EndDraw: an HWND target need not retain its presented frame.
        // All scene clips/layers have been popped by this point.
        const auto properties = D2D1::BitmapProperties(target_->GetPixelFormat(), 96, 96);
        if (FAILED(target_->CreateBitmap(target_->GetPixelSize(), properties, exit_frame_.GetAddressOf())) ||
            FAILED(exit_frame_->CopyFromRenderTarget(nullptr, target_.Get(), nullptr))) exit_frame_.Reset();
    }
    const float opacity = 1 - ease(t / 0.90f);
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    const auto size = target_->GetSize();
    if (exit_frame_) {
        target_->Clear(D2D1::ColorF(0, 0, 0));
        if (opacity > 0) {
            const auto source = exit_frame_->GetSize();
            const float bend = ease(t);
            const float inset_y = size.height * 0.012f * bend;
            // Horizontal strips approximate a shallow CRT bow. Pixel-aligned
            // shared edges prevent hairline seams; only 128 bitmap draws/frame.
            constexpr int strips = 128;
            for (int i = 0; i < strips; ++i) {
                const float a = static_cast<float>(i) / strips, b = static_cast<float>(i + 1) / strips;
                const float y = a + b - 1;
                const float inset_x = size.width * bend * (0.007f + 0.016f * y * y);
                const auto destination = D2D1::RectF(inset_x, std::round(std::lerp(inset_y, size.height - inset_y, a)),
                    size.width - inset_x, std::round(std::lerp(inset_y, size.height - inset_y, b)));
                if (destination.bottom <= destination.top) continue;
                const float drawn_height = size.height - 2 * inset_y;
                const float source_top = std::clamp((destination.top - inset_y) / drawn_height, 0.0f, 1.0f) * source.height;
                const float source_bottom = std::clamp((destination.bottom - inset_y) / drawn_height, 0.0f, 1.0f) * source.height;
                target_->DrawBitmap(exit_frame_.Get(), destination, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                    D2D1::RectF(0, source_top, source.width, source_bottom));
            }
        }
    } else {
        // Allocation/copy unavailable: the same timed fade over the live scene.
        brush(D2D1::ColorF(0, 0, 0, 1 - opacity));
        target_->FillRectangle(D2D1::RectF(0, 0, size.width, size.height), solid_.Get());
    }

    const float alpha = ease(t / 0.14f) * (1 - ease((t - 0.72f) / 0.24f));
    if (alpha <= 0) return;
    target_->SetTransform(D2D1::Matrix3x2F::Scale(viewport_.scale, viewport_.scale));
    const float height = viewport_.height, x = viewport_.width * 0.5f;
    const float scale = height / 900;
    const float top = height * 0.13f, bottom = height * 0.87f;
    const float y = std::lerp(top, bottom, ease((t - 0.06f) / 0.86f));
    if (!exit_line_) {
        const auto c = [&](float a) { return exitLight(exit_color_, a); };
        const D2D1_GRADIENT_STOP data[] = {{0, c(0)}, {0.16f, c(1)}, {0.84f, c(1)}, {1, c(0)}};
        ComPtr<ID2D1GradientStopCollection> stops;
        if (SUCCEEDED(target_->CreateGradientStopCollection(data, 4, stops.GetAddressOf())))
            target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({x, top}, {x, bottom}),
                stops.Get(), exit_line_.GetAddressOf());
    }
    if (!exit_glow_) {
        const auto c = [&](float a) { return exitLight(exit_color_, a); };
        const D2D1_GRADIENT_STOP data[] = {{0, c(1)}, {0.18f, c(0.65f)}, {0.48f, c(0.19f)}, {1, c(0)}};
        ComPtr<ID2D1GradientStopCollection> stops;
        if (SUCCEEDED(target_->CreateGradientStopCollection(data, 4, stops.GetAddressOf())))
            target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({x, y}, {0, 0}, 1, 1),
                stops.Get(), exit_glow_.GetAddressOf());
    }
    if (exit_line_) {
        exit_line_->SetStartPoint({x, top}); exit_line_->SetEndPoint({x, bottom});
        const auto stroke = [&](float width, float strength) {
            exit_line_->SetOpacity(alpha * strength);
            target_->DrawLine({x, top}, {x, bottom}, exit_line_.Get(), width * scale);
        };
        stroke(22, 0.012f); stroke(11, 0.030f); stroke(5.5f, 0.065f); stroke(1.5f, 0.28f);
    }
    if (exit_glow_) {
        exit_glow_->SetCenter({x, y});
        // A short lit wake, followed by the soft halo around the single dot.
        exit_glow_->SetRadiusX(60 * scale); exit_glow_->SetRadiusY(110 * scale);
        exit_glow_->SetOpacity(alpha * 0.75f);
        target_->DrawLine({x, std::max(top, y - 110 * scale)}, {x, y}, exit_glow_.Get(), 2.8f * scale);
        for (const float radius : {56.0f, 18.0f}) {
            const float r = radius * scale;
            exit_glow_->SetRadiusX(r); exit_glow_->SetRadiusY(r);
            exit_glow_->SetOpacity(alpha * (radius > 20 ? 0.36f : 0.72f));
            target_->FillRectangle(D2D1::RectF(x - r, y - r, x + r, y + r), exit_glow_.Get());
        }
    }
    brush(exitCore(exit_color_, alpha * 0.96f));
    target_->FillEllipse(D2D1::Ellipse({x, y}, 3.4f * scale, 3.4f * scale), solid_.Get());
}

void Renderer::drawExitPreview(D2D1_RECT_F box) {
    const float cycle = std::fmod(elapsed_, 2.4f);
    // 0..1.05 s: the exit sweep; 1.05..1.45 s: dark pause; 1.45..2.1 s: scene comes back; 2.1..2.4 s: live home.
    if (cycle >= 2.1f) return;
    const float t = std::clamp(cycle / 1.05f, 0.0f, 1.0f);
    const float back = cycle <= 1.45f ? 0.0f : ease((cycle - 1.45f) / 0.65f);
    const float dim = ease(t / 0.90f) * (1.0f - back);
    if (dim > 0.001f) {
        brush(D2D1::ColorF(0, 0, 0, dim));
        target_->FillRectangle(box, solid_.Get());
    }
    const float alpha = ease(t / 0.14f) * (1.0f - ease((t - 0.72f) / 0.24f)) * (1.0f - back);
    if (alpha <= 0.001f) return;
    const float height = box.bottom - box.top, x = (box.left + box.right) * 0.5f;
    const float scale = height / 900.0f;
    const float top = box.top + height * 0.13f, bottom = box.top + height * 0.87f;
    const float y = std::lerp(top, bottom, ease((t - 0.06f) / 0.86f));
    if (!exit_line_) {
        const auto c = [&](float a) { return exitLight(exit_color_, a); };
        const D2D1_GRADIENT_STOP data[] = {{0, c(0)}, {0.16f, c(1)}, {0.84f, c(1)}, {1, c(0)}};
        ComPtr<ID2D1GradientStopCollection> stops;
        if (SUCCEEDED(target_->CreateGradientStopCollection(data, 4, stops.GetAddressOf())))
            target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties({x, top}, {x, bottom}),
                stops.Get(), exit_line_.GetAddressOf());
    }
    if (!exit_glow_) {
        const auto c = [&](float a) { return exitLight(exit_color_, a); };
        const D2D1_GRADIENT_STOP data[] = {{0, c(1)}, {0.18f, c(0.65f)}, {0.48f, c(0.19f)}, {1, c(0)}};
        ComPtr<ID2D1GradientStopCollection> stops;
        if (SUCCEEDED(target_->CreateGradientStopCollection(data, 4, stops.GetAddressOf())))
            target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties({x, y}, {0, 0}, 1, 1),
                stops.Get(), exit_glow_.GetAddressOf());
    }
    if (exit_line_) {
        exit_line_->SetStartPoint({x, top}); exit_line_->SetEndPoint({x, bottom});
        for (const auto [width, strength] : {std::pair{22.0f, 0.012f}, {11.0f, 0.030f}, {5.5f, 0.065f}, {1.5f, 0.28f}}) {
            exit_line_->SetOpacity(alpha * strength);
            target_->DrawLine({x, top}, {x, bottom}, exit_line_.Get(), width * scale);
        }
    }
    if (exit_glow_) {
        exit_glow_->SetCenter({x, y});
        exit_glow_->SetRadiusX(60 * scale); exit_glow_->SetRadiusY(110 * scale);
        exit_glow_->SetOpacity(alpha * 0.75f);
        target_->DrawLine({x, std::max(top, y - 110 * scale)}, {x, y}, exit_glow_.Get(), 2.8f * scale);
        for (const float radius : {56.0f, 18.0f}) {
            const float r = radius * scale;
            exit_glow_->SetRadiusX(r); exit_glow_->SetRadiusY(r);
            exit_glow_->SetOpacity(alpha * (radius > 20 ? 0.36f : 0.72f));
            target_->FillRectangle(D2D1::RectF(x - r, y - r, x + r, y + r), exit_glow_.Get());
        }
    }
    brush(exitCore(exit_color_, alpha * 0.96f));
    target_->FillEllipse(D2D1::Ellipse({x, y}, 3.4f * scale, 3.4f * scale), solid_.Get());
}
