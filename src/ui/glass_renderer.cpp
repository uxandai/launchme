// Frosted glass: a panel shows the scene behind it blurred, under a tint (the PSfive buttons,
// dialogs, the power menu, the game menu). The scene drawn so far is copied from the target
// (CopyFromRenderTarget: it flushes the drawing before it) and blurred by Direct2D's Gaussian blur.
#include "renderer.hpp"
#include "desktop_theme.hpp"

#include <algorithm>
#include <cmath>

using Microsoft::WRL::ComPtr;

namespace {
using palette::gray;
}

bool Renderer::frostCapture(ID2D1Geometry* shape, float blur) {
    frost_ready_ = false;
    if (!realizer_ || !shape || layered_) return false;
    D2D1_MATRIX_3X2_F world{};
    target_->GetTransform(&world);
    D2D1_RECT_F bounds{};
    if (FAILED(shape->GetBounds(&world, &bounds))) return false; // Device pixels: the transform holds the viewport scale.
    // The blur reaches about three standard deviations: copy that much around the shape too.
    const float sigma = blur * viewport_.scale, reach = sigma * 3;
    const auto pixels = target_->GetPixelSize();
    const auto edge = [](float value, UINT limit) { return static_cast<UINT>(std::clamp(value, 0.0f, static_cast<float>(limit))); };
    const UINT left = edge(std::floor(bounds.left - reach), pixels.width), top = edge(std::floor(bounds.top - reach), pixels.height);
    const UINT right = edge(std::ceil(bounds.right + reach), pixels.width), bottom = edge(std::ceil(bounds.bottom + reach), pixels.height);
    if (right <= left || bottom <= top) return false;
    const auto size = D2D1::SizeU(right - left, bottom - top);
    ID2D1Bitmap* copy = nullptr;
    for (auto& bitmap : frost_bitmaps_) {
        if (!bitmap) continue;
        const auto current = bitmap->GetPixelSize();
        if (current.width == size.width && current.height == size.height) { copy = bitmap.Get(); break; }
    }
    if (!copy) {
        auto& slot = frost_bitmaps_[frost_next_++ % frost_bitmaps_.size()];
        slot.Reset();
        const auto format = main_target_ ? main_target_->GetPixelFormat() : target_->GetPixelFormat();
        if (FAILED(target_->CreateBitmap(size, D2D1::BitmapProperties(format, 96, 96), slot.GetAddressOf()))) return false;
        copy = slot.Get();
    }
    // No layer or clip may be pushed here (CopyFromRenderTarget refuses them).
    const auto origin = D2D1::Point2U(0, 0);
    const auto source = D2D1::RectU(left, top, right, bottom);
    if (FAILED(copy->CopyFromRenderTarget(&origin, main_target_ ? main_target_.Get() : target_.Get(), &source))) return false;
    if (!frost_blur_ && FAILED(realizer_->CreateEffect(CLSID_D2D1GaussianBlur, frost_blur_.GetAddressOf()))) return false;
    if (main_target_) {
        // Glass sees the stationary wallpaper plus UI drawn so far, even while the
        // latter is rendered into the transparent transition surface.
        if (!frost_foreground_ || frost_foreground_->GetPixelSize().width != size.width || frost_foreground_->GetPixelSize().height != size.height) {
            frost_foreground_.Reset();
            if (FAILED(target_->CreateBitmap(size, D2D1::BitmapProperties(target_->GetPixelFormat(), 96, 96), frost_foreground_.GetAddressOf()))) return false;
        }
        if (FAILED(frost_foreground_->CopyFromRenderTarget(&origin, target_.Get(), &source))) return false;
        if (!frost_composite_ && FAILED(realizer_->CreateEffect(CLSID_D2D1Composite, frost_composite_.GetAddressOf()))) return false;
        frost_composite_->SetInput(0, copy);
        frost_composite_->SetInput(1, frost_foreground_.Get());
        frost_blur_->SetInputEffect(0, frost_composite_.Get());
    } else frost_blur_->SetInput(0, copy);
    frost_blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, sigma);
    frost_blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    frost_origin_ = D2D1::Point2F(static_cast<float>(left), static_cast<float>(top));
    frost_ready_ = true;
    return true;
}

void Renderer::frostDraw(ID2D1Geometry* shape, float opacity) {
    if (!frost_ready_ || !shape || !realizer_ || opacity <= 0.004f) return;
    D2D1_MATRIX_3X2_F previous{};
    realizer_->GetTransform(&previous);
    // The shape clips in the current transform; the copy goes back pixel for pixel where it came from.
    realizer_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), shape, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                D2D1::IdentityMatrix(), std::min(opacity, 1.0f)), nullptr);
    realizer_->SetTransform(D2D1::Matrix3x2F::Translation(frost_origin_.x, frost_origin_.y));
    realizer_->DrawImage(frost_blur_.Get());
    realizer_->SetTransform(previous);
    realizer_->PopLayer();
}

Renderer::Glass Renderer::glassPanel(D2D1_ROUNDED_RECT panel, float blur) {
    Glass glass;
    ComPtr<ID2D1RoundedRectangleGeometry> shape;
    if (FAILED(factory_->CreateRoundedRectangleGeometry(panel, shape.GetAddressOf()))) return glass;
    glass.shape = shape;
    // Efekt szkła 0%: a solid panel, no copy of the scene. The desktop window's dialogs are solid too.
    glass.frosted = !desktop_style_ && glass_level_ > 0.01f && frostCapture(shape.Get(), glassBlur(blur));
    return glass;
}

void Renderer::glassFill(const Glass& glass, D2D1_COLOR_F tint, float opacity) {
    if (!glass.shape) return;
    if (glass.frosted) {
        frostDraw(glass.shape.Get(), opacity);
        tint.a *= glassTint(); // The blurred scene shows through the tint (half of it at 100%).
    }
    tint.a *= opacity;
    if (desktop_style_) {
        // Desktop: an opaque panel with a hairline edge; a plain dark one in the theme's card colour.
        const float peak = std::max({tint.r, tint.g, tint.b}), chroma = peak - std::min({tint.r, tint.g, tint.b});
        if (peak < 0.16f && chroma < 0.04f) { const auto& card = desktop_theme::current().card; tint.r = card.r; tint.g = card.g; tint.b = card.b; }
        tint.a = std::max(tint.a, 0.97f * opacity);
    }
    brush(tint);
    target_->FillGeometry(glass.shape.Get(), solid_.Get());
    if (desktop_style_) { brush(D2D1::ColorF(1, 1, 1, 0.1f * opacity)); target_->DrawGeometry(glass.shape.Get(), solid_.Get(), 1); }
    if (glass.frosted) {
        // A faint light edge along the top: the pane catches the light.
        D2D1_RECT_F bounds{};
        if (SUCCEEDED(glass.shape->GetBounds(nullptr, &bounds))) {
            ComPtr<ID2D1LinearGradientBrush> sheen;
            const D2D1_GRADIENT_STOP stops[] = {{0.0f, D2D1::ColorF(1, 1, 1, 0.07f * glass_level_ * opacity)}, {1.0f, D2D1::ColorF(1, 1, 1, 0)}};
            ComPtr<ID2D1GradientStopCollection> collection;
            if (SUCCEEDED(target_->CreateGradientStopCollection(stops, 2, collection.GetAddressOf())) &&
                SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, bounds.top),
                    D2D1::Point2F(0, bounds.top + std::min(90.0f, (bounds.bottom - bounds.top) * 0.5f))), collection.Get(), sheen.GetAddressOf())))
                target_->FillGeometry(glass.shape.Get(), sheen.Get());
        }
    }
}

bool Renderer::frostPane(D2D1_RECT_F pane, float opacity) {
    if (desktop_style_ || glass_level_ <= 0.01f || pane.right - pane.left < 2 || opacity <= 0.004f) return false;
    ComPtr<ID2D1RectangleGeometry> shape;
    if (FAILED(factory_->CreateRectangleGeometry(pane, shape.GetAddressOf())) || !frostCapture(shape.Get(), glassBlur(24))) return false;
    frostDraw(shape.Get(), opacity);
    return true;
}

void Renderer::drawFrostedBackdrop(float dim) {
    const D2D1_RECT_F screen = D2D1::RectF(0, 0, viewport_.width, viewport_.height);
    if (desktop_style_) {
        // The desktop window barely dims what is behind a dialog, as desktop apps do (a full-screen page
        // almost hides it).
        brush(D2D1::ColorF(0.02f, 0.022f, 0.027f, dim >= 0.999f ? 0.86f : std::clamp(dim * 0.2f, 0.0f, 0.14f)));
        target_->FillRectangle(screen, solid_.Get());
        return;
    }
    bool frosted = false;
    if (glass_level_ > 0.01f) {
        ComPtr<ID2D1RectangleGeometry> shape;
        if (SUCCEEDED(factory_->CreateRectangleGeometry(screen, shape.GetAddressOf())) && frostCapture(shape.Get(), glassBlur(34))) {
            frostDraw(shape.Get());
            frosted = true;
        }
    }
    // Without glass the scene is only a faint tint under an almost opaque dark layer.
    brush(D2D1::ColorF(0.018f, 0.020f, 0.026f, std::min(1.0f, frosted ? std::lerp(0.9f, 0.7f, glass_level_) * dim : std::max(dim, 0.95f))));
    target_->FillRectangle(screen, solid_.Get());
}

// ---- Buttons ----

namespace {
D2D1_COLOR_F rgbColor(uint32_t rgb, float alpha) {
    return D2D1::ColorF(((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, alpha);
}
// WCAG relative luminance of an sRGB colour.
float luminance(D2D1_COLOR_F c) {
    const auto linear = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * linear(c.r) + 0.7152f * linear(c.g) + 0.0722f * linear(c.b);
}
// Dark ink reads better than light ink on this (opaque) colour: the larger WCAG contrast ratio wins.
bool darkInkOn(D2D1_COLOR_F c) {
    constexpr float dark = 0.0040f, light = 0.9387f; // The luminance of the two inks (grey 0.05 and 0.97).
    const float l = luminance(c);
    return (l + 0.05f) / (dark + 0.05f) >= (light + 0.05f) / (l + 0.05f);
}
// The idle tint over the dark scene buttons usually sit on: what the caption is read against.
D2D1_COLOR_F overScene(D2D1_COLOR_F c) {
    constexpr float scene = 0.06f;
    return D2D1::ColorF(c.r * c.a + scene * (1 - c.a), c.g * c.a + scene * (1 - c.a), c.b * c.a + scene * (1 - c.a), 1);
}
}

D2D1_COLOR_F Renderer::buttonFill(float alpha) const { return rgbColor(button_rgb_ ? button_rgb_ : ui_accent_, alpha); }

// PSfive's "Graj": grey over the blur (alone where effects are missing), light on a dark picture,
// darker on a bright one. Tinted: the focused colour, deepened so it stays a background; dark: an
// almost black glass.
D2D1_COLOR_F Renderer::buttonIdleTint(bool hovered, bool enabled, float alpha) const {
    const float lift = hovered && enabled ? 0.07f : 0.0f, a = (enabled ? 0.56f : 0.34f) * alpha;
    switch (button_idle_) {
    case AppSettings::ButtonIdle::tinted: {
        const auto fill = buttonFill();
        const float peak = std::max({fill.r, fill.g, fill.b, 0.001f}), scale = std::min(1.0f, 0.46f / peak);
        // Greys (white, the default fill) keep the neutral glass a touch lighter instead of a hue.
        const float chroma = peak - std::min({fill.r, fill.g, fill.b});
        if (chroma < 0.08f) return D2D1::ColorF(0.3f + lift, 0.305f + lift, 0.32f + lift, a);
        return D2D1::ColorF(fill.r * scale + lift, fill.g * scale + lift, fill.b * scale + lift, std::min(1.0f, a * 1.25f));
    }
    case AppSettings::ButtonIdle::dark: return D2D1::ColorF(0.07f + lift, 0.075f + lift, 0.085f + lift, std::min(1.0f, a * 1.4f));
    default: return D2D1::ColorF(0.25f + lift, 0.255f + lift, 0.27f + lift, a);
    }
}

D2D1_COLOR_F Renderer::buttonInk(float focus, bool enabled, float alpha) const {
    // Light on the idle glass unless the tint is light itself; on the focused fill, whichever ink
    // contrasts more (dark on the default white).
    const bool idle_dark = !desktop_style_ && darkInkOn(overScene(buttonIdleTint(false, true, 1)));
    const bool lit_dark = darkInkOn(buttonFill());
    const float idle = idle_dark ? (enabled ? 0.05f : 0.38f) : (enabled ? 0.97f : 0.5f);
    const float lit = lit_dark ? (enabled ? 0.05f : 0.42f) : (enabled ? 0.97f : 0.62f);
    return palette::gray(std::lerp(idle, lit, std::clamp(focus, 0.0f, 1.0f)), alpha);
}

void Renderer::drawButtonGlass(D2D1_RECT_F r, float alpha, bool frosted, bool hovered, bool enabled) {
    if (alpha <= 0.004f || r.right - r.left < 1 || r.bottom - r.top < 1) return;
    if (desktop_style_) {
        // The desktop window: a quiet outlined button with corners like its own controls.
        const auto shape = D2D1::RoundedRect(r, 8, 8);
        brush(D2D1::ColorF(1, 1, 1, (enabled ? (hovered ? 0.1f : 0.055f) : 0.03f) * alpha));
        target_->FillRoundedRectangle(shape, solid_.Get());
        brush(D2D1::ColorF(1, 1, 1, (hovered && enabled ? 0.18f : 0.1f) * alpha));
        target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + 0.5f, r.top + 0.5f, r.right - 0.5f, r.bottom - 0.5f), 7.5f, 7.5f), solid_.Get(), 1);
        return;
    }
    const auto shape = pill(r);
    if (frosted && glass_level_ > 0.01f) {
        ComPtr<ID2D1RoundedRectangleGeometry> geometry;
        if (SUCCEEDED(factory_->CreateRoundedRectangleGeometry(shape, geometry.GetAddressOf())) && frostCapture(geometry.Get(), 12))
            frostDraw(geometry.Get(), alpha);
    }
    brush(buttonIdleTint(hovered, enabled, alpha));
    target_->FillRoundedRectangle(shape, solid_.Get());
}

void Renderer::drawButtonFocus(D2D1_RECT_F r, float alpha) {
    if (alpha <= 0.004f) return;
    brush(buttonFill(alpha));
    target_->FillRoundedRectangle(desktop_style_ ? D2D1::RoundedRect(r, 8, 8) : pill(r), solid_.Get()); // Desktop: a light primary button.
}

D2D1_COLOR_F Renderer::drawButton(D2D1_RECT_F r, float focus, float alpha, bool frosted, bool hovered, bool enabled) {
    focus = std::clamp(focus, 0.0f, 1.0f);
    if (focus < 0.999f) drawButtonGlass(r, alpha, frosted, hovered, enabled);
    drawButtonFocus(r, focus * alpha * (enabled ? 1.0f : 0.55f));
    return buttonInk(focus, enabled, alpha);
}

void Renderer::drawKeyCap(D2D1_RECT_F r, float alpha, bool enabled) {
    if (alpha <= 0.004f) return;
    // Interfejs → Wygląd podpowiedzi: light glass, or white with dark ink.
    if (context_ && context_->hint_white) brush(gray(0.96f, (enabled ? 0.95f : 0.4f) * alpha));
    else brush(gray(1, (enabled ? 0.2f : 0.07f) * alpha));
    target_->FillRoundedRectangle(pill(r), solid_.Get());
}

D2D1_COLOR_F Renderer::keyCapInk(float alpha) const {
    return context_ && context_->hint_white ? gray(0.07f, alpha) : gray(0.94f, alpha);
}
