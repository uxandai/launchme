#include "renderer.hpp"
#include "launch_animation.hpp"

using Microsoft::WRL::ComPtr;

// The launched game's background over the whole screen, dimmed and still, its logo in the middle (its
// title without one), a ring with a white dot bouncing off its edge beneath. Without a background: its poster or
// cover blurred, else the screen's own background darkened.
void Renderer::drawLaunchAnimation() {
    const float age = context_->launch_animation_seconds, alpha = launch_animation::opacity(age, context_->launch_animation_ending);
    if (alpha <= 0.001f) return;
    const float width = viewport_.width, height = viewport_.height;
    const auto index = context_->launch_artwork_index;
    const auto slot = [&](std::vector<Cover>& slots) -> Cover* { return index < slots.size() && slots[index].bitmap ? &slots[index] : nullptr; };
    brush(D2D1::ColorF(0, 0, 0, alpha));
    target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());

    // Still, and framed exactly as the screen beneath frames it: the same wallpaper placement (the crop focus the
    // owner set in Kadrowanie, the same cover-fit and zoom), so the picture does not jump when it fades in.
    if (auto* hero = slot(heroes_)) drawWallpaper(hero->bitmap.Get(), alpha, 0, hero->focus);
    else if (auto* art = slot(posters_) ? slot(posters_) : slot(covers_)) drawWallpaper(blurredArt(*art), alpha * 0.75f);
    // A touch dimmed, darker towards the bottom where the text sits.
    brush(D2D1::ColorF(0, 0, 0, launch_animation::dim * alpha));
    target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
    ComPtr<ID2D1GradientStopCollection> stops;
    ComPtr<ID2D1LinearGradientBrush> shade;
    const D2D1_GRADIENT_STOP shade_stops[] = {{0, D2D1::ColorF(0, 0, 0, 0)}, {0.55f, D2D1::ColorF(0, 0, 0, 0.10f * alpha)}, {1, D2D1::ColorF(0, 0, 0, 0.55f * alpha)}};
    if (SUCCEEDED(target_->CreateGradientStopCollection(shade_stops, 3, stops.GetAddressOf())) &&
        SUCCEEDED(target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, height)),
                                                    stops.Get(), shade.GetAddressOf())))
        target_->FillRectangle(D2D1::RectF(0, 0, width, height), shade.Get());

    // The logo, centred and fitted into a box; the title in large type without one.
    const float shown = alpha * launch_animation::logo(age), scale = launch_animation::logoScale(age);
    const float cx = width * 0.5f, cy = height * 0.46f;
    float text_top = cy + 40;
    if (auto* logo = slot(logos_)) {
        const auto pixels = logo->bitmap->GetPixelSize();
        const auto source = logo->has_ink_bounds ? logo->ink_bounds : D2D1::RectF(0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height));
        const float w = source.right - source.left, h = source.bottom - source.top;
        if (w > 0 && h > 0) {
            const float box_w = std::min(width * 0.46f, 760.0f), box_h = std::min(height * 0.26f, 300.0f);
            const float fit = std::min(box_w / w, box_h / h) * scale;
            const float lw = w * fit, lh = h * fit;
            target_->DrawBitmap(logo->bitmap.Get(), D2D1::RectF(cx - lw * 0.5f, cy - lh * 0.5f, cx + lw * 0.5f, cy + lh * 0.5f),
                                shown, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
            text_top = cy + lh * 0.5f + 36;
        }
    } else {
        const float box_w = std::min(width * 0.8f, 1200.0f);
        textLine(context_->launch_animation_title, cx - box_w * 0.5f, cy, box_w, hero_font_.Get(),
                 D2D1::ColorF(0.97f, 0.97f, 0.97f, shown), DWRITE_TEXT_ALIGNMENT_CENTER);
        text_top = cy + typeMetrics(hero_font_.Get()).line * 0.5f + 30;
    }
    // A thin ring and a white dot flying chord to chord inside it, a short trail behind: calm, no text.
    const float radius = std::clamp(height * 0.026f, 20.0f, 34.0f), dot_radius = radius * 0.15f;
    const float ring_y = text_top + 18 + radius;
    const auto place = [&](float time) { const auto d = launch_animation::dot(time); return D2D1::Point2F(cx + d.x * (radius - dot_radius - 1), ring_y + d.y * (radius - dot_radius - 1)); };
    brush(D2D1::ColorF(1, 1, 1, 0.30f * shown));
    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, ring_y), radius, radius), solid_.Get(), 1.5f);
    for (int i = 5; i >= 1; --i) {
        const float fade = 1.0f - static_cast<float>(i) / 6.0f;
        brush(D2D1::ColorF(1, 1, 1, 0.22f * fade * fade * shown));
        target_->FillEllipse(D2D1::Ellipse(place(age - 0.045f * static_cast<float>(i)), dot_radius * (0.55f + 0.45f * fade), dot_radius * (0.55f + 0.45f * fade)), solid_.Get());
    }
    brush(D2D1::ColorF(1, 1, 1, 0.95f * shown));
    target_->FillEllipse(D2D1::Ellipse(place(age), dot_radius, dot_radius), solid_.Get());
}
