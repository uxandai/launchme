// Tryb TV's CRT filter around a frame (crt_filter.cpp holds the effect): crtBegin() points the device
// context at a bitmap of the frame's size, crtFinish() draws that bitmap through the effect into the
// real target. With the phosphor the filtered frame is kept, and the next one fades from it.
// Off, in the desktop window, or without a device context: frames are drawn as before.

#include "renderer.hpp"

#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;

void Renderer::crtRelease() {
    crt_frame_ = false; crt_history_valid_ = false;
#ifdef _WIN32
    crt_effect_.Reset(); crt_constants_ = nullptr; crt_sent_ = {};
    crt_scene_.Reset(); crt_history_.Reset(); crt_history_next_.Reset(); crt_output_.Reset();
#endif
}

void Renderer::crtBegin() {
    crt_frame_ = false;
    const auto now = std::chrono::steady_clock::now();
    const bool glow_only = crt_glow_only_;
    crt_glow_only_ = false;
#ifdef _WIN32
    if (!crt_.on || layered_ || !context_ || context_->desktop_window) { crt_history_valid_ = false; return; }
    ComPtr<ID2D1DeviceContext> context;
    if (FAILED(target_.As(&context))) return;
    // The effect: registered with this factory once, made on this device.
    if (!crt_effect_) {
        ComPtr<ID2D1Factory> base;
        ComPtr<ID2D1Factory1> factory;
        context->GetFactory(base.GetAddressOf());
        if (!base || FAILED(base.As(&factory))) return;
        if (crt_registered_ != factory.Get()) {
            if (!crt_filter::registerEffect(factory.Get())) return;
            crt_registered_ = factory.Get();
        }
        if (FAILED(crt_filter::create(context.Get(), crt_effect_.GetAddressOf(), &crt_constants_))) { crt_effect_.Reset(); return; }
    }
    ComPtr<ID2D1Image> output;
    context->GetTarget(output.GetAddressOf());
    ComPtr<ID2D1Bitmap> output_bitmap;
    if (!output || FAILED(output.As(&output_bitmap))) return;
    const auto size = output_bitmap->GetPixelSize();
    float dpi_x = 96, dpi_y = 96;
    context->GetDpi(&dpi_x, &dpi_y);
    const auto fits = [&](const ComPtr<ID2D1Bitmap1>& bitmap) {
        if (!bitmap) return false;
        const auto s = bitmap->GetPixelSize();
        return s.width == size.width && s.height == size.height;
    };
    const auto make = [&](ComPtr<ID2D1Bitmap1>& bitmap) {
        if (fits(bitmap)) return true;
        bitmap.Reset();
        const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi_x, dpi_y);
        return SUCCEEDED(context->CreateBitmap(size, nullptr, 0, &properties, bitmap.GetAddressOf()));
    };
    if (!make(crt_scene_)) return;
    const bool phosphor = crt_.phosphor > 0.001f;
    if (phosphor) {
        if (!fits(crt_history_)) crt_history_valid_ = false;
        if (!make(crt_history_) || !make(crt_history_next_)) { crt_history_.Reset(); crt_history_next_.Reset(); crt_history_valid_ = false; }
    } else {
        crt_history_.Reset(); crt_history_next_.Reset(); crt_history_valid_ = false;
    }
    // A change (not the phosphor's own fading): it glows on for the phosphor's time.
    if (!glow_only) crt_glow_until_ = now + std::chrono::milliseconds(static_cast<int>(crt_view::phosphorSeconds(crt_.phosphor) * 1000) + 50);
    crt_output_ = output;
    context->SetTarget(crt_scene_.Get());
    crt_frame_ = true;
#else
    (void)now; (void)glow_only;
#endif
}

void Renderer::crtFinish() {
    if (!crt_frame_) return;
    crt_frame_ = false;
#ifdef _WIN32
    ComPtr<ID2D1DeviceContext> context;
    if (FAILED(target_.As(&context))) return;
    const auto now = std::chrono::steady_clock::now();
    const float dt = crt_last_frame_ == std::chrono::steady_clock::time_point{} ? 1.0f / 60 :
                     std::clamp(std::chrono::duration<float>(now - crt_last_frame_).count(), 0.0f, 0.5f);
    crt_last_frame_ = now;
    const auto size = crt_scene_->GetPixelSize();
    const float width = static_cast<float>(size.width), height = static_cast<float>(size.height);
    // Lottes' parameters from the sliders.
    auto& c = *crt_constants_;
    c.width = width; c.height = height;
    c.warp_x = crt_.warp_x * crt_view::max_warp; c.warp_y = crt_.warp_y * crt_view::max_warp;
    c.mask = static_cast<float>(crt_.mask);
    const float m = std::clamp(crt_.mask_level, 0.0f, 1.0f);
    c.mask_dark = 1.0f - 0.5f * m; c.mask_light = 1.0f + 0.5f * m;
    c.mask_scale = std::max(1.0f, std::round(height / 1080.0f)); // The triads stay visible on a 4K screen.
    c.hard_scan = -8.0f;
    c.scan_mix = std::clamp(crt_.scanlines, 0.0f, 1.0f);
    c.pitch = crt_view::linePitch(height);
    c.glow = std::clamp(crt_.glow, 0.0f, 1.0f) * 0.5f;
    // What the mask and the lines take away comes back (Lottes' brightBoost), so the screen is not dimmer.
    const float mask_average = crt_.mask == crt_view::Mask::none ? 1.0f :
        crt_.mask == crt_view::Mask::tv ? (2 * c.mask_dark + c.mask_light) / 3 * (c.mask_dark + c.mask_light) / 2 : (2 * c.mask_dark + c.mask_light) / 3;
    c.boost = std::clamp(1.0f / (mask_average * (1.0f - 0.25f * c.scan_mix)), 1.0f, 1.8f);
    const float decay = crt_view::phosphorDecay(crt_.phosphor);
    c.decay = decay > 0 ? std::pow(decay, dt * 60.0f) : 0.0f;
    const bool phosphor = crt_history_ && crt_history_next_;
    c.history = phosphor && crt_history_valid_ ? 1.0f : 0.0f;
    // New constants reach the shader only through a property change (crt_filter.hpp).
    if (std::memcmp(&c, &crt_sent_, sizeof(c)) != 0) { crt_sent_ = c; crt_effect_->SetValue(0, ++crt_generation_); }
    crt_effect_->SetInput(0, crt_scene_.Get());
    crt_effect_->SetInput(1, phosphor && crt_history_valid_ ? crt_history_.Get() : crt_scene_.Get());
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    if (phosphor) {
        // Filtered into the history first; that bitmap is then the frame shown, and the next one's past.
        context->SetTarget(crt_history_next_.Get());
        context->DrawImage(crt_effect_.Get(), D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1_COMPOSITE_MODE_SOURCE_COPY);
        context->SetTarget(crt_output_.Get());
        context->DrawImage(crt_history_next_.Get(), D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1_COMPOSITE_MODE_SOURCE_COPY);
        crt_history_.Swap(crt_history_next_);
        crt_history_valid_ = true;
    } else {
        context->SetTarget(crt_output_.Get());
        context->DrawImage(crt_effect_.Get(), D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1_COMPOSITE_MODE_SOURCE_COPY);
    }
    crt_output_.Reset();
#endif
}
