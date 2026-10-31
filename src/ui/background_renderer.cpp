// Renderer: the picture behind the screens: the cover-fit wallpaper, the focused game's
// background with its cross-fades and video (4:3 blurred), the LED/CRT grid over video, and the
// shelves' hero.
#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "carousel.hpp"
#include "category_rail.hpp"
#include "../platform/user_config.hpp"

#include <d2d1effects.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using Microsoft::WRL::ComPtr;
using namespace render_detail;

// One centred cover-fit for every still wallpaper. The slow Details zoom is
// continuous across navigation; layouts never lift or drift the picture.
void Renderer::drawWallpaper(ID2D1Bitmap* image, float opacity, float offset, CropFocus focus) {
    if (!image || opacity <= 0.001f) return;
    const auto pixels = image->GetSize();
    const float scale = std::max(viewport_.width / pixels.width, viewport_.height / pixels.height) *
                        std::clamp(context_->wallpaper_zoom, 1.0f, 1.01f);
    const float width = pixels.width * scale, height = pixels.height * scale;
    // The focus (centred by default) at the screen's centre, never past the picture's edges.
    const float x = std::clamp(viewport_.width * 0.5f - focus.x * width, viewport_.width - width, 0.0f) + offset;
    const float y = std::clamp(viewport_.height * 0.5f - focus.y * height, viewport_.height - height, 0.0f);
    target_->DrawBitmap(image, D2D1::RectF(x, y, x + width, y + height), opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

void Renderer::drawWallpapers(const Wallpaper& under, const Wallpaper& over) {
    const bool lower = under.image && under.opacity > 0.001f, upper = over.image && over.opacity > 0.001f;
    ComPtr<ID2D1DeviceContext> context;
    if (lower && upper) target_.As(&context);
    if (!context) {
        if (lower) drawWallpaper(under.image, under.opacity, 0, under.focus);
        if (upper) drawWallpaper(over.image, over.opacity, 0, over.focus);
        return;
    }
    // In a layer of their own, added together: (1 - f)·A + f·B, then laid over the screen once.
    target_->PushLayer(D2D1::LayerParameters(), nullptr);
    context->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_ADD);
    drawWallpaper(under.image, under.opacity, 0, under.focus);
    drawWallpaper(over.image, over.opacity, 0, over.focus);
    context->SetPrimitiveBlend(D2D1_PRIMITIVE_BLEND_SOURCE_OVER);
    target_->PopLayer();
}

void Renderer::stepHeroFade(const Cover* slot, const ImageBytes& picture) {
    // The frame's time: the clock, or detail_'s growth since the last frame when that is more.
    const auto now = std::chrono::steady_clock::now();
    const float wall = hero_frame_ == std::chrono::steady_clock::time_point{} ? 0.0f :
        std::clamp(std::chrono::duration<float>(now - hero_frame_).count(), 0.0f, 0.05f);
    hero_frame_ = now;
    const float grown = detail_ >= hero_detail_seen_ ? detail_ - hero_detail_seen_ : detail_; // Lower: it started again.
    hero_detail_seen_ = detail_;
    const float advance = std::max(wall, grown / 1.7f) / hero_fade_seconds;
    const bool ready = !picture || (slot && slot->bitmap);
    const auto bind = [&](HeroLayer& layer) {
        layer.pixels = picture; // Equal bytes from another load: compared by pointer from now on.
        if (slot && slot->bitmap) { layer.bitmap = slot->bitmap; layer.tone = slot->tone; layer.focus = slot->focus; }
    };
    if (hero_leaving_.pixels && !hero_leaving_.bitmap) { hero_leaving_ = {}; hero_mix_ = 1; } // Its bitmap is gone (a new target).
    hero_waiting_ = false;
    if (picture && samePicture(picture, hero_shown_.pixels)) bind(hero_shown_);
    else if (!picture && !hero_shown_.pixels) {}
    else if (hero_mix_ < 1 && picture && samePicture(picture, hero_leaving_.pixels)) {
        // Back to the picture fading out: the same cross-fade runs backwards from where it is.
        std::swap(hero_shown_, hero_leaving_);
        hero_mix_ = 1 - hero_mix_;
        bind(hero_shown_);
    } else if (hero_mix_ < 1 || !ready || detail_ < (picture ? hero_settle_detail : 0.999f)) {
        // A cross-fade under way finishes first; a picture still decoding, or a focus that only
        // passes through, waits. Without a picture the last one stays a little longer: a game's
        // background usually arrives a moment after its tile is focused.
        hero_waiting_ = ready;
    } else {
        hero_leaving_ = hero_shown_.pixels && hero_shown_.bitmap ? std::move(hero_shown_) : HeroLayer{};
        hero_shown_ = {};
        if (picture) bind(hero_shown_);
        hero_mix_ = 0;
    }
    hero_mix_ = std::min(1.0f, hero_mix_ + advance);
    if (!hero_leaving_.pixels && !hero_shown_.pixels) hero_mix_ = 1;
    const float level = smoothFade(hero_mix_);
    hero_shown_.level = hero_shown_.pixels ? level : 0.0f;
    hero_leaving_.level = 1.0f - level;
    if (hero_mix_ >= 1) hero_leaving_ = {};
}

float Renderer::stepCinematicFade(ID2D1Bitmap* candidate) {
    const auto now = std::chrono::steady_clock::now();
    const auto progress = [&] { return std::clamp(std::chrono::duration<float>(now - cinematic_changed_).count() / cinematic_fade_seconds, 0.0f, 1.0f); };
    cinematic_waiting_ = false;
    if (screen_changed_ && candidate && candidate == hero_shown_.bitmap.Get() && hero_shown_.level >= 0.999f) {
        // The same picture as the screen before: no fade at all.
        cinematic_hero_ = candidate;
        cinematic_previous_hero_.Reset();
        cinematic_changed_ = now - std::chrono::seconds(1);
    } else if (candidate != cinematic_hero_.Get()) {
        const float t = progress();
        if (t < 1 && candidate && candidate == cinematic_previous_hero_.Get()) {
            // Back to the picture fading out: the cross-fade runs backwards from where it is.
            std::swap(cinematic_hero_, cinematic_previous_hero_);
            cinematic_changed_ = now - std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<float>((1 - t) * cinematic_fade_seconds));
        } else if (t < 1 || detail_ < 0.30f) cinematic_waiting_ = t < 1 && detail_ >= 0.30f; // Held: the fade under way or a passing focus.
        else {
            cinematic_previous_hero_ = cinematic_hero_;
            cinematic_hero_ = candidate;
            cinematic_changed_ = now;
        }
    }
    const float fade = smoothFade(progress());
    if (fade >= 1) cinematic_previous_hero_.Reset();
    return fade;
}

void Renderer::drawBackground() {
    const float width = viewport_.width, height = viewport_.height;
    // A console's colour behind it; a store's or a studio's library stays neutral.
    const bool tinted = !context_->platform.empty() && !studioPlatform(context_->platform);
    if (tinted) {
        const auto tint = platformColor(context_->platform);
        brush(color(0.015f + tint.r * 0.18f, 0.018f + tint.g * 0.18f,
                    0.025f + tint.b * 0.18f));
        target_->FillRectangle(D2D1::RectF(0, 0, width, height), solid_.Get());
        if (platform_aura_name_ != context_->platform || !platform_aura_) {
            platform_aura_.Reset();
            platform_aura_name_ = context_->platform;
            const D2D1_GRADIENT_STOP stops_data[] = {
                {0.0f, color(tint.r, tint.g, tint.b, 0.30f)},
                {0.55f, color(tint.r, tint.g, tint.b, 0.07f)},
                {1.0f, color(0, 0, 0, 0)}
            };
            ComPtr<ID2D1GradientStopCollection> stops;
            if (SUCCEEDED(target_->CreateGradientStopCollection(stops_data, 3, stops.GetAddressOf())))
                target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(
                    D2D1::Point2F(width * 0.60f, height * 0.30f), D2D1::Point2F(0, 0),
                    width * 0.64f, height * 0.82f), stops.Get(), platform_aura_.GetAddressOf());
        }
        if (platform_aura_) target_->FillRectangle(D2D1::RectF(0, 0, width, height), platform_aura_.Get());
    }
    if (library_backdrop_opacity_ > 0.001f) drawLibraryBackdrop(library_backdrop_opacity_);
    // PSfive and Szczegóły use brighter artwork and their own shade. Still images
    // share the same full-screen crop; only the video's presentation follows this mix.
    const bool full_screen = context_->psfive || context_->screen == ScreenMode::details;
    const float target_full = full_screen ? 1.0f : 0.0f;
    if (context_->screen != background_screen_) {
        full_screen_from_ = full_screen_mix_;
        background_screen_ = context_->screen;
    }
    full_screen_mix_ = context_->screen_transition < 0.999f ?
        std::lerp(full_screen_from_, target_full, smoothFade(context_->screen_transition)) : target_full;
    const float video_area = std::lerp(height * 0.88f, height, full_screen_mix_);
    auto drawVideoImage = [&](ID2D1Bitmap* bitmap, float opacity) {
        if (!bitmap || opacity <= 0.001f) return;
        const auto size = bitmap->GetPixelSize();
        const float zoom = 1.025f * std::max(width / size.width, video_area / size.height);
        const float art_width = size.width * zoom, art_height = size.height * zoom;
        const float art_x = (width - art_width) / 2 + 7.0f * std::sin(elapsed_ * 0.24f);
        const float art_y = (video_area - art_height) / 2 + 4.0f * std::sin(elapsed_ * 0.17f);
        target_->DrawBitmap(bitmap,
                            D2D1::RectF(art_x, art_y, art_x + art_width, art_y + art_height),
                            opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    };
    // A playing game video takes the picture's place over 600 ms (src/app/game_video.cpp).
    const float video = video_bitmap_ ? smoothFade(std::chrono::duration<float>(std::chrono::steady_clock::now() - video_started_).count() / 0.6f) : 0.0f;
    const float picture_share = 1.0f - video;
    const auto drawVideo = [&](float opacity) {
        if (!video_bitmap_ || opacity <= 0.001f) return;
        const auto size = video_bitmap_->GetPixelSize();
        if (!size.width || !size.height) return;
        if (static_cast<float>(size.width) / size.height >= 1.6f) { drawVideoImage(video_bitmap_.Get(), opacity); return; }
        // 4:3 and other narrow videos: the whole frame, over a blurred full-screen copy of itself.
        if (realizer_ && !video_blur_) realizer_->CreateEffect(CLSID_D2D1GaussianBlur, video_blur_.GetAddressOf());
        if (realizer_ && video_blur_) {
            video_blur_->SetInput(0, video_bitmap_.Get());
            video_blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 7.0f);
            video_blur_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
            const float zoom = std::max(width / size.width, height / size.height);
            D2D1_MATRIX_3X2_F previous{};
            realizer_->GetTransform(&previous);
            realizer_->SetTransform(D2D1::Matrix3x2F::Scale(zoom, zoom) *
                D2D1::Matrix3x2F::Translation((width - size.width * zoom) / 2, (height - size.height * zoom) / 2) * previous);
            realizer_->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                D2D1::IdentityMatrix(), opacity * 0.5f), nullptr);
            realizer_->DrawImage(video_blur_.Get());
            realizer_->PopLayer();
            realizer_->SetTransform(previous);
        } else drawVideoImage(video_bitmap_.Get(), opacity * 0.3f);
        const float scale = std::min(width / size.width, video_area / size.height);
        const float w = size.width * scale, h = size.height * scale, x = (width - w) / 2, y = (video_area - h) / 2;
        target_->DrawBitmap(video_bitmap_.Get(), D2D1::RectF(x, y, x + w, y + h), opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    };
    float art = 0; // How much video shows: the dot grid lies over video only, pictures stay clean.
    if (cards_ && !cards_->empty() && context_->library_mode == library_view::View::cinematic) {
        // Hold the last settled image during fast navigation; then cross-fade over 350 ms.
        auto* candidate = selected_ < heroes_.size() ? heroes_[selected_].bitmap.Get() : nullptr;
        const float fade = stepCinematicFade(candidate);
        // Kinowy plays the video in its focused tile (drawRectangularCard): the wallpaper stays.
        const CropFocus focus = candidate == cinematic_hero_.Get() && selected_ < heroes_.size() ? heroes_[selected_].focus : CropFocus{};
        drawWallpapers({cinematic_previous_hero_.Get(), 0.85f * (1 - fade)}, {cinematic_hero_.Get(), 0.85f * fade, focus});
    } else if (cards_ && !cards_->empty()) {
        const Cover* slot = selected_ < heroes_.size() ? &heroes_[selected_] : nullptr;
        stepHeroFade(slot, slot && slot->loaded() ? slot->pixels : nullptr);
        // A console logo standing in is faint; a game's art behind a console tile is dimmed, and
        // behind a full library grid of covers it stays quieter than behind a shelf.
        const bool grid = (context_->screen == ScreenMode::library || context_->screen == ScreenMode::search) &&
                          context_->library_mode == library_view::View::grid;
        const auto alpha = [&](const HeroLayer& layer) {
            return layer.tone == HeroTone::logo ? 0.13f : layer.tone == HeroTone::dimmed ? 0.32f :
                   grid ? 0.5f : std::lerp(0.75f, 0.96f, full_screen_mix_);
        };
        drawWallpapers({hero_leaving_.bitmap.Get(), alpha(hero_leaving_) * hero_leaving_.level * picture_share, hero_leaving_.focus},
                       {hero_shown_.bitmap.Get(), alpha(hero_shown_) * hero_shown_.level * picture_share, hero_shown_.focus});
        drawVideo(std::lerp(0.8f, 0.96f, full_screen_mix_) * video);
        art = video_bitmap_ ? video : 0.0f;
    } else if (hero_shown_.bitmap && (context_->screen == ScreenMode::settings || context_->screen == ScreenMode::downloads || context_->screen == ScreenMode::devices || context_->screen == ScreenMode::glance ||
                                     context_->screen == ScreenMode::companion)) {
        const float alpha = hero_shown_.tone == HeroTone::logo ? 0.13f : hero_shown_.tone == HeroTone::dimmed ? 0.32f : 0.75f;
        drawWallpaper(hero_shown_.bitmap.Get(), alpha * hero_shown_.level * picture_share, 0, hero_shown_.focus);
    }
    drawBackgroundGrid(std::clamp(art, 0.0f, 1.0f));
    if (full_screen && full_screen_mix_ >= 0.999f) return; // PSfive and Szczegóły lay their own shade over the picture.
    const float shelf_shade = 1.0f - full_screen_mix_;
    if (backdrop_fade_ && shelf_shade > 0.002f) {
        backdrop_fade_->SetEndPoint(D2D1::Point2F(0, height));
        backdrop_fade_->SetOpacity(shelf_shade);
        target_->FillRectangle(D2D1::RectF(0, 0, width, height), backdrop_fade_.Get());
        backdrop_fade_->SetOpacity(1.0f);
    }
    if (aura_ && !tinted && shelf_shade > 0.002f) {
        aura_->SetCenter(D2D1::Point2F(200, focusY() + 170));
        aura_->SetOpacity((heroes_.empty() || !heroes_[selected_].bitmap ? 1.0f : 0.22f) * shelf_shade);
        target_->FillRectangle(D2D1::RectF(0, 50, width, height), aura_.Get());
    }
}

// Settings → Interfejs → Siatka na tle: a fine grid of black dots over the background video
// (not pictures), like an LED/CRT mask. Its sharp detail at the screen's own resolution hides
// how soft an upscaled 480p trailer is. One cell per device pixel block, so
// the dots stay crisp at any DPI; larger screens get larger cells (about 360 rows of dots).
void Renderer::drawBackgroundGrid(float art) {
    const float strength = background_grid_ * art;
    if (strength <= 0.003f) return;
    const float physical = viewport_.height * viewport_.scale;
    const int cell = std::clamp(static_cast<int>(std::lround(physical / 360.0f)), 3, 12);
    const auto style = background_grid_style_;
    if (!grid_mask_ || grid_mask_cell_ != cell || grid_mask_style_ != style) {
        grid_mask_.Reset();
        grid_mask_cell_ = cell;
        grid_mask_style_ = style;
        // One cell of black (premultiplied BGRA: alpha only). Every pattern darkens about a third
        // of the picture, so the slider means the same strength for each.
        std::vector<uint8_t> pixels(static_cast<size_t>(cell) * cell * 4, 0);
        const int line = std::max(1, static_cast<int>(std::lround(cell / 3.0f)));
        for (int y = 0; y < cell; ++y) for (int x = 0; x < cell; ++x) {
            float alpha = 0;
            if (style == AppSettings::GridStyle::lines) {
                // CRT scanlines: a dark band under every row of the picture.
                alpha = y >= cell - line ? 1.0f : 0.0f;
            } else if (style == AppSettings::GridStyle::mesh) {
                // A mesh around every "pixel": thin dark lines on its top and left edges.
                const int thin = std::max(1, line / 2);
                const float covered = 1.0f - static_cast<float>((cell - thin) * (cell - thin)) / static_cast<float>(cell * cell);
                alpha = x < thin || y < thin ? std::min(1.0f, 0.34f / covered) : 0.0f;
            } else {
                // LED dots: one antialiased dot per cell (4x4 samples per pixel).
                const float centre = cell * 0.5f, radius = cell * 0.34f;
                int inside = 0;
                for (int sy = 0; sy < 4; ++sy) for (int sx = 0; sx < 4; ++sx) {
                    const float dx = x + (sx + 0.5f) / 4 - centre, dy = y + (sy + 0.5f) / 4 - centre;
                    inside += dx * dx + dy * dy <= radius * radius;
                }
                alpha = inside / 16.0f;
            }
            pixels[(static_cast<size_t>(y) * cell + x) * 4 + 3] = static_cast<uint8_t>(std::lround(alpha * 255));
        }
        ComPtr<ID2D1Bitmap> bitmap;
        const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
        if (FAILED(target_->CreateBitmap(D2D1::SizeU(cell, cell), pixels.data(), cell * 4, properties, bitmap.GetAddressOf())) ||
            FAILED(target_->CreateBitmapBrush(bitmap.Get(), D2D1::BitmapBrushProperties(D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP,
                D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR), grid_mask_.GetAddressOf()))) return;
    }
    // The scene is drawn scaled by viewport_.scale: undo it, so one texel is one device pixel.
    grid_mask_->SetTransform(D2D1::Matrix3x2F::Scale(1.0f / viewport_.scale, 1.0f / viewport_.scale));
    grid_mask_->SetOpacity(std::min(strength, 1.0f));
    target_->FillRectangle(D2D1::RectF(0, 0, viewport_.width, viewport_.height), grid_mask_.Get());
}

void Renderer::drawHero() {
    if (cards_->empty()) return;
    if (gameKey((*cards_)[selected_].game).empty()) return;
    // A store's, console's or studio's tile is its own logo: no name over the row as well.
    const auto kind = (*cards_)[selected_].game.kind;
    if (kind == GameKind::platform || kind == GameKind::pc_platform) return;
    const float fade = smoothFade(detail_) * (1.0f - smoothFade(context_->category_menu_progress));
    const float inset = context_->category_rail ? category_rail::Layout{viewport_.width, viewport_.height}.right() : 0;
    const auto& logo = logos_[selected_];
    // With the facts under it (Opcje układu → Informacje pod logo) the logo stands on a raised line.
    const bool facts = !context_->hero_facts.empty();
    constexpr float facts_base = 296;
    float center = inset + 112 + 255; // The logo's horizontal centre: the facts centre under it.
    if (logo.bitmap) {
        const auto size = logo.bitmap->GetPixelSize();
        const auto source = logo.has_ink_bounds ? logo.ink_bounds :
                            D2D1::RectF(0, 0, static_cast<float>(size.width), static_cast<float>(size.height));
        const float ink_width = source.right - source.left, ink_height = source.bottom - source.top;
        const float factor = std::min(510.0f / ink_width, (facts ? 150.0f : 160.0f) / ink_height);
        const float width = ink_width * factor, height = ink_height * factor;
        const float top = facts ? facts_base - height : 183 + (165 - height) / 2;
        target_->DrawBitmap(logo.bitmap.Get(), D2D1::RectF(inset + 112, top, inset + 112 + width, top + height), fade * reveal(logo),
                            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
        center = inset + 112 + width * 0.5f;
    } else if (!context_->logo_pending && !logo.decoding) { // Wait for the lookup/download as well as decoding.
        const auto& name = (*cards_)[selected_].game.name;
        const float box = viewport_.width - inset - 185;
        text(name, inset + 110, (facts ? 165.0f : 205.0f) + (1 - fade) * 12, box, 92, hero_font_.Get(), color(0.93f, 0.96f, 0.95f, fade));
        center = inset + 110 + std::min(box, textAdvance(name, hero_font_.Get())) * 0.5f;
    }
    if (facts) drawHeroFacts(center, facts_base + 30, inset + 56, fade);
}

// The default home's facts under the logo: columns of a small capitalised heading over its value
// (the platform as its mark), centred on the logo, kept clear of the screen's left edge.
void Renderer::drawHeroFacts(float center, float top, float min_left, float alpha) {
    const auto& facts = context_->hero_facts;
    if (facts.empty() || alpha < 0.004f) return;
    constexpr float gap = 46, mark_height = 26, mark_width = 120;
    // The platform's mark: its badge's fitted width, or its name.
    const auto markWidth = [&](const std::wstring& platform) {
        const auto found = platform_badges_.find(platform);
        if (found == platform_badges_.end() || !found->second.bitmap) return std::min(mark_width, textAdvance(platform, menu_font_.Get()));
        const auto& badge = found->second;
        const auto size = badge.bitmap->GetPixelSize();
        const auto source = badge.has_ink_bounds ? badge.ink_bounds : D2D1::RectF(0, 0, static_cast<float>(size.width), static_cast<float>(size.height));
        const float w = source.right - source.left, h = source.bottom - source.top;
        return w < 1 || h < 1 ? 0.0f : w * std::min(mark_width / w, mark_height / h);
    };
    std::vector<float> widths;
    float total = 0;
    for (const auto& fact : facts) {
        const bool mark = &fact == &facts.back() && !context_->hero_platform.empty();
        const float value = mark ? markWidth(context_->hero_platform) : std::min(260.0f, textAdvance(fact.value, menu_font_.Get()));
        widths.push_back(std::max(textAdvance(fact.heading, friend_section_font_.Get()), value));
        total += widths.back();
    }
    total += gap * (facts.size() - 1);
    float x = std::max(min_left, center - total * 0.5f);
    const float heading_y = top, value_y = top + 32;
    // A soft dark copy under each line keeps it legible over a bright background.
    const auto shadowed = [&](const std::wstring& value, float left, float y, float w, IDWriteTextFormat* font, D2D1_COLOR_F ink) {
        textLine(value, left + 1, y + 1.5f, w, font, D2D1::ColorF(0, 0, 0, 0.55f * alpha), DWRITE_TEXT_ALIGNMENT_CENTER);
        textLine(value, left, y, w, font, ink, DWRITE_TEXT_ALIGNMENT_CENTER);
    };
    for (size_t i = 0; i < facts.size(); ++i) {
        const float w = widths[i], mid = x + w * 0.5f;
        if (i) line(x - gap * 0.5f, heading_y - 8, x - gap * 0.5f, value_y + 14, palette::gray(1, 0.18f * alpha), 1);
        shadowed(facts[i].heading, x, heading_y, w, friend_section_font_.Get(), D2D1::ColorF(0.74f, 0.77f, 0.8f, alpha));
        if (i + 1 == facts.size() && !context_->hero_platform.empty()) {
            const float mark = markWidth(context_->hero_platform);
            drawPlatformMark(context_->hero_platform, mid - mark * 0.5f, value_y, mark_height, mark_width, alpha, menu_font_.Get());
        } else shadowed(facts[i].value, x, value_y, w, menu_font_.Get(), palette::text(alpha));
        x += w + gap;
    }
}

float Renderer::videoFade() const {
    if (!video_bitmap_) return 0;
    return smoothFade(std::chrono::duration<float>(std::chrono::steady_clock::now() - video_started_).count() / 0.6f);
}

bool Renderer::drawVideoIn(D2D1_RECT_F rect, ID2D1Geometry* clip, float opacity) {
    const float shown = videoFade() * opacity;
    if (shown <= 0.001f) return false;
    const auto size = video_bitmap_->GetPixelSize();
    const float w = rect.right - rect.left, h = rect.bottom - rect.top;
    if (!size.width || !size.height || w < 1 || h < 1) return false;
    // Cover-fit: it fills the rectangle, centred, cut on the longer side.
    const float zoom = std::max(w / size.width, h / size.height);
    const float vw = size.width * zoom, vh = size.height * zoom;
    const auto destination = D2D1::RectF(rect.left + (w - vw) * 0.5f, rect.top + (h - vh) * 0.5f, rect.left + (w + vw) * 0.5f, rect.top + (h + vh) * 0.5f);
    if (clip) target_->PushLayer(D2D1::LayerParameters(rect, clip), nullptr);
    else target_->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target_->DrawBitmap(video_bitmap_.Get(), destination, shown, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    if (clip) target_->PopLayer(); else target_->PopAxisAlignedClip();
    return true;
}