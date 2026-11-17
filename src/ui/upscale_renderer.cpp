// Tryb TV's background upscaler wrapper: draws a background or wallpaper bitmap through FSR 1.0
// (EASU + RCAS) on Windows or 5-tap Catmull-Rom + CAS on Linux (Impeller) whenever the image is
// noticeably smaller than its target area on screen; falls back to bilinear DrawBitmap otherwise.
#include "renderer.hpp"
#ifndef _WIN32
#include <launchme_d2d.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace {
// Native 4K (or near-native) images go straight through hardware bilinear without running a shader.
constexpr float min_upscale_ratio = 1.12f;
}

void Renderer::upscaleRelease() {
#ifdef _WIN32
    for (auto& slot : upscale_slots_) {
        slot.effect.Reset();
        slot.constants = nullptr;
        slot.sent = {};
        slot.source = nullptr;
    }
#endif
}

void Renderer::drawUpscaledBitmap(ID2D1Bitmap* image, const D2D1_RECT_F& dest, float opacity, int slot_index) {
    if (!image || opacity <= 0.001f || !target_) return;
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    const auto drawLinear = [&] {
        target_->DrawBitmap(image, dest, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    };
    if (!wallpaper_upscale_ || layered_ || (context_ && context_->desktop_window)) {
        drawLinear();
        return;
    }
    const auto src_px = image->GetPixelSize();
    if (src_px.width < 64 || src_px.height < 64 || dest.right <= dest.left || dest.bottom <= dest.top) {
        drawLinear();
        return;
    }
    D2D1_MATRIX_3X2_F world{};
    target_->GetTransform(&world);
    if (std::abs(world._12) > 1e-4f || std::abs(world._21) > 1e-4f || world._11 <= 0.0f || world._22 <= 0.0f) {
        drawLinear();
        return;
    }
    const float full_left = dest.left * world._11 + world._31;
    const float full_top = dest.top * world._22 + world._32;
    const float full_w = (dest.right - dest.left) * world._11;
    const float full_h = (dest.bottom - dest.top) * world._22;
    const float ratio = std::max(full_w / static_cast<float>(src_px.width), full_h / static_cast<float>(src_px.height));
    if (ratio < min_upscale_ratio) {
        drawLinear();
        return;
    }

#ifdef _WIN32
    ComPtr<ID2D1DeviceContext> context;
    if (FAILED(target_.As(&context))) {
        drawLinear();
        return;
    }
    const auto target_px = target_->GetPixelSize();
    if (!target_px.width || !target_px.height) {
        drawLinear();
        return;
    }
    const int vis_l = std::max(0, static_cast<int>(std::floor(full_left)));
    const int vis_t = std::max(0, static_cast<int>(std::floor(full_top)));
    const int vis_r = std::min(static_cast<int>(target_px.width), static_cast<int>(std::ceil(full_left + full_w)));
    const int vis_b = std::min(static_cast<int>(target_px.height), static_cast<int>(std::ceil(full_top + full_h)));
    if (vis_r <= vis_l || vis_b <= vis_t) return;
    const int out_w = vis_r - vis_l, out_h = vis_b - vis_t;

    auto& slot = upscale_slots_[static_cast<size_t>(std::clamp(slot_index, 0, 1))];
    if (!slot.effect) {
        ComPtr<ID2D1Factory> base;
        ComPtr<ID2D1Factory1> factory;
        context->GetFactory(base.GetAddressOf());
        if (!base || FAILED(base.As(&factory))) { drawLinear(); return; }
        if (upscale_registered_ != factory.Get()) {
            if (!upscale_filter::registerEffect(factory.Get())) { drawLinear(); return; }
            upscale_registered_ = factory.Get();
        }
        if (FAILED(upscale_filter::create(context.Get(), slot.effect.GetAddressOf(), &slot.constants))) {
            slot.effect.Reset();
            drawLinear();
            return;
        }
        // Cache the primary background's sharpened 4K output in Direct2D's resource pool so idle
        // focus pulses over a settled game background do not re-evaluate EASU + RCAS each frame.
        if (slot_index == 0 && !low_memory_) slot.effect->SetValue(D2D1_PROPERTY_CACHED, TRUE);
    }

    auto& c = *slot.constants;
    c.src_width = static_cast<float>(src_px.width);
    c.src_height = static_cast<float>(src_px.height);
    c.out_width = static_cast<float>(out_w);
    c.out_height = static_cast<float>(out_h);
    c.crop_x = ((static_cast<float>(vis_l) - full_left) / full_w) * c.src_width;
    c.crop_y = ((static_cast<float>(vis_t) - full_top) / full_h) * c.src_height;
    c.crop_width = (static_cast<float>(out_w) / full_w) * c.src_width;
    c.crop_height = (static_cast<float>(out_h) / full_h) * c.src_height;
    c.rcas_con = 0.8408964f; // exp2(-0.25f)
    c.opacity = opacity;

    if (slot.source != image) {
        slot.effect->SetInput(0, image);
        slot.source = image;
    }
    if (std::memcmp(&c, &slot.sent, sizeof(c)) != 0) {
        slot.sent = c;
        slot.effect->SetValue(0, ++slot.generation);
    }

    const auto composite = context->GetPrimitiveBlend() == D2D1_PRIMITIVE_BLEND_ADD
                               ? D2D1_COMPOSITE_MODE_PLUS
                               : D2D1_COMPOSITE_MODE_SOURCE_OVER;
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    context->DrawImage(slot.effect.Get(), D2D1::Point2F(static_cast<float>(vis_l), static_cast<float>(vis_t)),
                       D2D1::RectF(0, 0, static_cast<float>(out_w), static_cast<float>(out_h)),
                       D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, composite);
    context->SetTransform(world);
#else
    (void)slot_index;
    if (!LaunchmeUpscaleDraw(target_.Get(), image, dest, opacity)) drawLinear();
#endif
}
