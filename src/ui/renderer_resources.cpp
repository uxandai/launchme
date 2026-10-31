// Direct2D target lifetime and device-dependent resources. Screen drawing stays in *_renderer.cpp.
#include "renderer.hpp"
#include "../artwork/block_compression.hpp"
#include "../platform/diagnostics.hpp"
#include <algorithm>
#include <chrono>
#ifdef _WIN32
#include <d2d1_1helper.h>
#endif

using Microsoft::WRL::ComPtr;
namespace {
D2D1_COLOR_F color(float r, float g, float b, float a = 1.0f) { return D2D1::ColorF(r, g, b, a); }
// Bitmap uploads per frame (uploadDecoded); a 600×900 poster takes about 0.3–1 ms at 4K.
constexpr auto upload_budget = std::chrono::microseconds(3000);
}

// After releaseGraphics: a new factory (and with it a new device) and its shapes.
bool Renderer::ensureFactory() {
    if (factory_) return true;
    if (SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf())) && createShape()) return true;
    factory_.Reset();
    return false;
}

bool Renderer::createTarget() {
    if (target_) return true;
    // After releaseGraphics: a new factory (and with it a new device) and its shapes.
    if (!factory_ && (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf())) || !createShape())) {
        factory_.Reset();
        return false;
    }
    RECT bounds{};
    GetClientRect(hwnd_, &bounds);
    if (layered_) {
        if (!layered_surface_.resize(std::max(1L, bounds.right), std::max(1L, bounds.bottom))) return false;
        ComPtr<ID2D1DCRenderTarget> dc;
        const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        if (FAILED(factory_->CreateDCRenderTarget(&properties, dc.GetAddressOf())) ||
            FAILED(dc->BindDC(layered_surface_.dc(), &bounds)) || FAILED(dc.As(&target_))) return false;
        if (!createTargetResources()) { discardTarget(); return false; }
        return true;
    }
#ifdef _WIN32
    // A flip-model swap chain on the renderer's own device (swap_chain.hpp); the HWND target where
    // it cannot be made (or LAUNCHME_SWAPCHAIN=0).
    if (swap_wanted_ && !swap_failed_) {
        if (swap_.create(factory_.Get(), hwnd_, static_cast<UINT>(std::max(1L, bounds.right)), static_cast<UINT>(std::max(1L, bounds.bottom)),
                         tearing_wanted_)) {
            target_ = swap_.context();
            ++device_generation_;
            limitTextureMemory();
            if (!createTargetResources()) { discardTarget(); return false; }
            return true;
        }
        swap_failed_ = true;
        diagnostics::log("warning", "No flip-model swap chain: the window draws through an HWND render target");
    }
#endif
    // FrameClock paces production; DWM composes the display.
    if (FAILED(factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                         D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN), 96, 96),
            D2D1::HwndRenderTargetProperties(hwnd_,
                D2D1::SizeU(std::max(1L, bounds.right), std::max(1L, bounds.bottom)),
                immediate_present_ ? D2D1_PRESENT_OPTIONS_IMMEDIATELY : D2D1_PRESENT_OPTIONS_NONE),
            window_target_.GetAddressOf()))) return false;
    if (FAILED(window_target_.As(&target_))) return false;
    ++device_generation_;
    limitTextureMemory();
    if (!createTargetResources()) { discardTarget(); return false; }
    return true;
}

// ---- Frames, device caches and video textures --------------------------------------------------

void Renderer::beginFrame() {
#ifdef _WIN32
    // Normally at once: the frame clock already paces frames. Not in the uncapped benchmark, which
    // measures how fast frames are drawn, not shown.
    if (swap_ && !tearing_wanted_) swap_.waitForFrame(50);
#endif
}

HRESULT Renderer::finishFrame(HRESULT drawn) {
#ifdef _WIN32
    if (swap_ && SUCCEEDED(drawn) && FAILED(swap_.present(immediate_present_))) return D2DERR_RECREATE_TARGET;
#endif
    return drawn;
}

#ifdef _WIN32
Microsoft::WRL::ComPtr<ID2D1Device> Renderer::d2dDevice() const {
    ComPtr<ID2D1Device> device;
    if (swap_) { device = swap_.device(); return device; }
    // An HWND target answers as a device context too (Windows 8+); a DC or WIC target has no device.
    ComPtr<ID2D1DeviceContext> context;
    if (target_ && !layered_ && SUCCEEDED(target_.As(&context))) context->GetDevice(device.GetAddressOf());
    return device;
}
#endif

void Renderer::limitTextureMemory() {
#ifdef _WIN32
    // Direct2D's own cache of intermediate textures (effects such as the glass blur, layers, glyphs):
    // at 4K one full-screen intermediate is 33 MB, so a few of them fit; the low memory mode keeps fewer.
    if (const auto device = d2dDevice()) device->SetMaximumTextureMemory(low_memory_ ? uint64_t{64} << 20 : uint64_t{160} << 20);
#endif
}

void Renderer::releaseIdleCaches() {
    const auto now = std::chrono::steady_clock::now();
    if (now - caches_released_ < std::chrono::seconds(10)) return;
    caches_released_ = now;
#ifdef _WIN32
    // What the screens left behind and have not used for 10 s (another screen's effects and layers).
    if (const auto device = d2dDevice()) device->ClearResources(10000);
#endif
}

void Renderer::trimGraphics() {
#ifdef _WIN32
    if (swap_) { swap_.trim(); return; }
    if (const auto device = d2dDevice()) device->ClearResources(0);
#endif
}

void* Renderer::videoDevice() {
#ifdef _WIN32
    if (!swap_ || !gpu_video_) return nullptr;
    swap_.protectForVideo(); // Media Foundation decodes on its own threads with this device.
    return swap_.device3d();
#else
    return nullptr;
#endif
}

void* Renderer::videoTexture(uint32_t width, uint32_t height) {
#ifdef _WIN32
    if (!swap_ || !target_ || !width || !height) return nullptr;
    if (video_texture_) {
        D3D11_TEXTURE2D_DESC current{};
        video_texture_->GetDesc(&current);
        if (current.Width == width && current.Height == height) return video_texture_.Get();
    }
    video_texture_.Reset(); video_texture_bitmap_.Reset();
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width; description.Height = height;
    description.MipLevels = 1; description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc = {1, 0};
    description.Usage = D3D11_USAGE_DEFAULT;
    // Media Foundation's video processor draws into it; Direct2D samples it.
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<IDXGISurface> surface;
    ComPtr<ID2D1Bitmap1> bitmap;
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
    if (FAILED(swap_.device3d()->CreateTexture2D(&description, nullptr, video_texture_.GetAddressOf())) ||
        FAILED(video_texture_.As(&surface)) || FAILED(swap_.context()->CreateBitmapFromDxgiSurface(surface.Get(), &properties, bitmap.GetAddressOf()))) {
        video_texture_.Reset();
        return nullptr;
    }
    video_texture_bitmap_ = bitmap;
    return video_texture_.Get();
#else
    (void)width; (void)height;
    return nullptr;
#endif
}

void Renderer::showVideoTexture() {
#ifdef _WIN32
    if (!video_texture_bitmap_ || video_bitmap_ == video_texture_bitmap_) return;
    const bool first = !video_bitmap_; // The video starts: its fade-in (a new size only swaps the picture).
    video_bitmap_ = video_texture_bitmap_;
    video_blur_.Reset();
    if (first) video_started_ = std::chrono::steady_clock::now();
#endif
}

bool Renderer::createTargetResources() {
    if (FAILED(target_->CreateSolidColorBrush(color(1, 1, 1), solid_.GetAddressOf()))) return false;
    {
        ComPtr<ID2D1DeviceContext> context;
        bc1_supported_ = SUCCEEDED(target_.As(&context)) && context->IsDxgiFormatSupported(DXGI_FORMAT_BC1_UNORM);
        bc3_supported_ = bc1_supported_ && context->IsDxgiFormatSupported(DXGI_FORMAT_BC3_UNORM);
    }
    if (SUCCEEDED(target_.As(&realizer_)) &&
        FAILED(realizer_->CreateFilledGeometryRealization(squircle_.Get(), squircle_tolerance, squircle_fill_.GetAddressOf())))
        realizer_.Reset();
    if (realizer_ && squircle_compact_)
        realizer_->CreateFilledGeometryRealization(squircle_compact_.Get(), squircle_tolerance, squircle_compact_fill_.GetAddressOf());
    const D2D1_GRADIENT_STOP shade_stops[] = {
        {0.0f, color(0, 0, 0, 0)}, {0.57f, color(0, 0, 0, 0)}, {1.0f, color(0, 0, 0, 0.54f)}
    };
    ComPtr<ID2D1GradientStopCollection> stops;
    if (SUCCEEDED(target_->CreateGradientStopCollection(shade_stops, 3, stops.GetAddressOf())))
        target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0, 0), D2D1::Point2F(0, 1)), stops.Get(), shade_.GetAddressOf());
    const D2D1_GRADIENT_STOP install_stops[] = {
        {0.0f, color(0, 0, 0, 0)}, {0.56f, color(0, 0, 0, 0)},
        {0.80f, color(0, 0, 0, 0.26f)}, {1.0f, color(0, 0, 0, 0.78f)}
    };
    stops.Reset();
    if (SUCCEEDED(target_->CreateGradientStopCollection(install_stops, 4, stops.GetAddressOf())))
        target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0, 0), D2D1::Point2F(0, 1)), stops.Get(), install_vignette_.GetAddressOf());
    // Readable status text on bright art, fading smoothly into the image instead
    // of sitting in a separate panel. Reused by squares, posters and capsules.
    const D2D1_GRADIENT_STOP status_stops[] = {
        {0.0f, color(0, 0, 0, 0)}, {0.35f, color(0, 0, 0, 0.20f)},
        {0.62f, color(0, 0, 0, 0.72f)}, {1.0f, color(0, 0, 0, 0.96f)}
    };
    stops.Reset();
    if (SUCCEEDED(target_->CreateGradientStopCollection(status_stops, 4, stops.GetAddressOf())))
        target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0, 0), D2D1::Point2F(0, 1)), stops.Get(), status_shade_.GetAddressOf());
    const D2D1_GRADIENT_STOP aura_stops[] = {
        {0.0f, color(0.27f, 0.56f, 0.56f, 0.17f)},
        {0.4f, color(0.16f, 0.31f, 0.34f, 0.055f)},
        {1.0f, color(0, 0, 0, 0)}
    };
    stops.Reset();
    if (SUCCEEDED(target_->CreateGradientStopCollection(aura_stops, 3, stops.GetAddressOf())))
        target_->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(
            D2D1::Point2F(0, 0), D2D1::Point2F(0, 0), 510, 380), stops.Get(), aura_.GetAddressOf());
    const D2D1_GRADIENT_STOP backdrop_stops[] = {
        {0.0f, color(0.016f, 0.019f, 0.025f, 0.48f)},
        {0.38f, color(0.016f, 0.019f, 0.025f, 0.24f)},
        {0.69f, color(0.016f, 0.019f, 0.025f, 0.43f)},
        {1.0f, color(0.016f, 0.019f, 0.025f, 0.91f)}
    };
    stops.Reset();
    if (SUCCEEDED(target_->CreateGradientStopCollection(backdrop_stops, 4, stops.GetAddressOf())))
        target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0, 0), D2D1::Point2F(0, 900)), stops.Get(), backdrop_fade_.GetAddressOf());
    const D2D1_GRADIENT_STOP friends_stops[] = {
        {0.0f, color(0, 0, 0, 0)}, {0.2f, color(0, 0, 0, 0.104f)},
        {0.5f, color(0, 0, 0, 0.5f)}, {0.8f, color(0, 0, 0, 0.896f)},
        {1.0f, color(0, 0, 0, 1)}
    };
    stops.Reset();
    if (SUCCEEDED(target_->CreateGradientStopCollection(friends_stops, 5, stops.GetAddressOf()))) {
        target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0, 0), D2D1::Point2F(1600, 0)), stops.Get(), friends_backdrop_.GetAddressOf());
        target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0, 0), D2D1::Point2F(32, 0)), stops.Get(), category_rail_mask_.GetAddressOf());
    }
    const D2D1_GRADIENT_STOP categories_stops[] = {
        {0.0f, color(0, 0, 0, 1)}, {0.2f, color(0, 0, 0, 0.896f)},
        {0.5f, color(0, 0, 0, 0.5f)}, {0.8f, color(0, 0, 0, 0.104f)},
        {1.0f, color(0, 0, 0, 0)}
    };
    stops.Reset();
    if (SUCCEEDED(target_->CreateGradientStopCollection(categories_stops, 5, stops.GetAddressOf())))
        target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0, 0), D2D1::Point2F(1600, 0)), stops.Get(), category_backdrop_.GetAddressOf());
    // Quick bar: 0 at the bottom edge, 1 where the fade ends (drawQuickPanel sets the points).
    const D2D1_GRADIENT_STOP quick_stops[] = {
        {0.0f, color(0, 0, 0, 0.95f)}, {0.3f, color(0, 0, 0, 0.85f)},
        {0.55f, color(0, 0, 0, 0.55f)}, {0.8f, color(0, 0, 0, 0.18f)},
        {1.0f, color(0, 0, 0, 0)}
    };
    stops.Reset();
    if (SUCCEEDED(target_->CreateGradientStopCollection(quick_stops, 5, stops.GetAddressOf())))
        target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0, 900), D2D1::Point2F(0, 0)), stops.Get(), quick_fade_.GetAddressOf());
    // Quick bar focus ring (PS5): drawn across the ring from its brightest point (0) to the
    // opposite side (1), so a lit stretch of about +-60 degrees fades into a faint rest of the ring.
    const D2D1_GRADIENT_STOP ring_stops[] = {
        {0.0f, color(0.97f, 0.97f, 0.97f, 1.0f)}, {0.18f, color(0.97f, 0.97f, 0.97f, 0.82f)},
        {0.42f, color(0.97f, 0.97f, 0.97f, 0.36f)}, {1.0f, color(0.97f, 0.97f, 0.97f, 0.2f)}
    };
    stops.Reset();
    if (SUCCEEDED(target_->CreateGradientStopCollection(ring_stops, 4, stops.GetAddressOf())))
        target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0, 0), D2D1::Point2F(0, 1)), stops.Get(), quick_ring_.GetAddressOf());
    // PSfive: the picture stays bright in the middle, darker under the row of tiles at the top
    // and under the buttons and the quick bar at the bottom (drawPsfive sets the end point).
    const D2D1_GRADIENT_STOP psfive_stops[] = {
        {0.0f, color(0, 0, 0, 0.62f)}, {0.17f, color(0, 0, 0, 0.36f)}, {0.36f, color(0, 0, 0, 0.06f)},
        {0.6f, color(0, 0, 0, 0.04f)}, {0.82f, color(0, 0, 0, 0.30f)}, {1.0f, color(0, 0, 0, 0.62f)}
    };
    stops.Reset();
    if (SUCCEEDED(target_->CreateGradientStopCollection(psfive_stops, 6, stops.GetAddressOf())))
        target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
            D2D1::Point2F(0, 0), D2D1::Point2F(0, 900)), stops.Get(), psfive_shade_.GetAddressOf());
    // Metro shortcut tiles, lighter at the top-left corner (drawMetroTile sets the points):
    // Biblioteka green, Platformy PC blue, Emulacja violet, Ustawienia slate, Glance amber,
    // Towarzyszka rose.
    constexpr float metro_colors[6][2][3] = {
        {{0.24f, 0.66f, 0.27f}, {0.05f, 0.33f, 0.10f}}, {{0.25f, 0.53f, 0.88f}, {0.06f, 0.21f, 0.50f}},
        {{0.64f, 0.32f, 0.82f}, {0.28f, 0.09f, 0.44f}}, {{0.42f, 0.47f, 0.55f}, {0.15f, 0.18f, 0.23f}},
        {{0.92f, 0.58f, 0.20f}, {0.52f, 0.22f, 0.05f}}, {{0.88f, 0.40f, 0.60f}, {0.40f, 0.10f, 0.28f}}};
    for (size_t i = 0; i < metro_gradients_.size(); ++i) {
        const auto& [light, dark] = metro_colors[i];
        const D2D1_GRADIENT_STOP metro_stops[] = {{0.0f, color(light[0], light[1], light[2])},
                                                  {1.0f, color(dark[0], dark[1], dark[2])}};
        stops.Reset();
        if (SUCCEEDED(target_->CreateGradientStopCollection(metro_stops, 2, stops.GetAddressOf())))
            target_->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(1, 1)),
                                               stops.Get(), metro_gradients_[i].GetAddressOf());
    }
    for (auto& cover : covers_) createBitmap(cover);
    for (auto& logo : logos_) createBitmap(logo);
    for (auto& hero : heroes_) createBitmap(hero);
    for (auto& poster : posters_) createBitmap(poster);
    for (auto& disc : discs_) createBitmap(disc);
    for (auto& thumbnail : artwork_thumbnails_) createBitmap(thumbnail);
    for (auto& thumbnail : plugin_thumbnails_) createBitmap(thumbnail);
    for (auto& thumbnail : glance_thumbnails_) createBitmap(thumbnail);
    for (auto& image : tarot_images_) createBitmap(image);
    for (auto& image : profile_images_) createBitmap(image);
    for (auto& [id, icon] : widget_icons_) { (void)id; createBitmap(icon); }
    for (auto& tile : tile_thumbnails_) for (auto& thumbnail : tile) createBitmap(thumbnail);
    for (auto& [id, avatar] : avatars_) { (void)id; createBitmap(avatar); }
    for (auto& [name, badge] : platform_badges_) { (void)name; createBitmap(badge); }
    for (auto& image : saver_slots_) createBitmap(image);
    createBitmap(file_browser_preview_);
    createBitmap(notification_cover_);
    createBitmap(custom_poster_);
    createBitmap(companion_photo_);
    createBitmap(companion_photo_large_);
    for (auto& [id, image] : companion_feed_images_) createBitmap(image);
    return true;
}

void Renderer::discardTarget() {
    // Keep exit_capture_attempted_: after device loss use the plain fade fallback.
    exit_frame_.Reset(); exit_line_.Reset(); exit_glow_.Reset();
    screen_frame_.Reset(); screen_bow_target_.Reset(); screen_ui_target_.Reset(); desktop_backdrop_.Reset();
    main_target_.Reset(); main_realizer_.Reset();
    kept_hero_.bitmap.Reset(); kept_hero_.brush.Reset();
    hero_shown_.bitmap.Reset(); hero_leaving_ = {}; hero_mix_ = 1; // The shown picture comes back from its slot.
    video_bitmap_.Reset(); video_blur_.Reset(); // The next video frame creates them again.
    cinematic_hero_.Reset(); cinematic_previous_hero_.Reset();
    desktop_hero_.Reset(); desktop_hero_previous_.Reset(); // Bitmaps of the old device: the pane picks its picture again.
    platform_aura_.Reset();
    platform_aura_name_.clear();
    for (auto& glow : library_glows_) glow.Reset();
    // gray_failed may come from the lost target: the new one tries again.
    for (auto& cover : covers_) { cover.bitmap.Reset(); cover.brush.Reset(); cover.gray_bitmap.Reset(); cover.gray_brush.Reset(); cover.gray_failed = false; cover.blurred.Reset(); cover.blurred_source.Reset(); cover.desaturated.Reset(); cover.desaturated_source.Reset(); }
    for (auto& logo : logos_) { logo.bitmap.Reset(); logo.brush.Reset(); }
    for (auto& hero : heroes_) { hero.bitmap.Reset(); hero.brush.Reset(); }
    for (auto& poster : posters_) { poster.bitmap.Reset(); poster.brush.Reset(); poster.gray_bitmap.Reset(); poster.gray_brush.Reset(); poster.gray_failed = false; poster.blurred.Reset(); poster.blurred_source.Reset(); poster.desaturated.Reset(); poster.desaturated_source.Reset(); }
    for (auto& disc : discs_) { disc.bitmap.Reset(); disc.brush.Reset(); disc.gray_bitmap.Reset(); disc.gray_brush.Reset(); disc.gray_failed = false; }
    for (auto& thumbnail : artwork_thumbnails_) { thumbnail.bitmap.Reset(); thumbnail.brush.Reset(); }
    file_browser_preview_.bitmap.Reset(); file_browser_preview_.brush.Reset();
    notification_cover_.bitmap.Reset(); notification_cover_.brush.Reset();
    custom_poster_.bitmap.Reset(); custom_poster_.brush.Reset();
    companion_photo_.bitmap.Reset(); companion_photo_.brush.Reset();
    companion_photo_large_.bitmap.Reset(); companion_photo_large_.brush.Reset();
    for (auto& [id, image] : companion_feed_images_) { image.bitmap.Reset(); image.brush.Reset(); image.decoding.reset(); }
    for (auto& thumbnail : plugin_thumbnails_) { thumbnail.bitmap.Reset(); thumbnail.brush.Reset(); }
    for (auto& thumbnail : glance_thumbnails_) { thumbnail.bitmap.Reset(); thumbnail.brush.Reset(); }
    for (auto& image : tarot_images_) { image.bitmap.Reset(); image.brush.Reset(); }
    for (auto& image : profile_images_) { image.bitmap.Reset(); image.brush.Reset(); }
    for (auto& [id, sprite] : companion_sprites_) { (void)id; sprite.cover.bitmap.Reset(); sprite.cover.brush.Reset(); sprite.cover.decoding.reset(); }
    for (auto& [id, icon] : widget_icons_) { (void)id; icon.bitmap.Reset(); icon.brush.Reset(); }
    for (auto& tile : tile_thumbnails_) for (auto& thumbnail : tile) { thumbnail.bitmap.Reset(); thumbnail.brush.Reset(); }
    for (auto& [id, avatar] : avatars_) { (void)id; avatar.bitmap.Reset(); avatar.brush.Reset(); }
    for (auto& [name, badge] : platform_badges_) { (void)name; badge.bitmap.Reset(); badge.brush.Reset(); }
    for (auto& image : saver_slots_) { image.bitmap.Reset(); image.brush.Reset(); }
    for (auto* list : {&covers_, &posters_}) for (auto& image : *list) if (image.steelbook) {
        image.steelbook->bitmap.Reset(); image.steelbook->brush.Reset(); image.steelbook->gloss.Reset(); image.steelbook->matte.Reset();
        image.steelbook->failed = false;
        image.steelbook->relief.Reset(); image.steelbook->rim_light.Reset(); image.steelbook->rim_shade.Reset();
        image.steelbook->lighting.Reset(); image.steelbook->highlight.Reset(); image.steelbook->masked.Reset();
        image.steelbook->surface_transform.Reset(); image.steelbook->printed.Reset();
    }
    steelbook_edge_.Reset(); steelbook_light_.Reset(); steelbook_sheen_.Reset(); steelbook_haze_.Reset();
    shelf_floor_.Reset(); shelf_fade_.Reset(); shelf_shade_.Reset(); shelf_gloss_.Reset(); shelf_glow_.Reset(); shelf_spines_.clear();
    steelbook_streak_.Reset(); steelbook_surface_.Reset();
    media_plastic_.Reset(); media_edge_.Reset(); media_grain_.Reset();
    floppy_plastic_.Reset(); floppy_metal_.Reset(); floppy_paper_.Reset();
    floppy_sheen_.Reset(); floppy_bevel_.Reset(); floppy_media_.Reset(); floppy_glint_.Reset();
    saver_context_.Reset(); saver_context_checked_ = false; saver_layer_.Reset();
    saver_mask_.Reset(); saver_sheen_.Reset(); saver_shade_.Reset(); saver_shadow_.Reset(); saver_glows_.clear();
    category_backdrop_.Reset(); friends_backdrop_.Reset(); backdrop_fade_.Reset(); grid_mask_.Reset();
    category_rail_mask_.Reset(); category_rail_layer_.Reset(); quick_fade_.Reset(); quick_ring_.Reset(); quick_layer_.Reset(); psfive_shade_.Reset();
    for (auto& bitmap : frost_bitmaps_) bitmap.Reset();
    frost_blur_.Reset(); frost_ready_ = false;
    frost_foreground_.Reset(); frost_composite_.Reset();
    orbit_blur_.Reset(); orbit_ink_.Reset(); orbit_target_.Reset();
    for (auto& gradient : metro_gradients_) gradient.Reset();
    squircle_strokes_.clear(); squircle_fill_.Reset(); realizer_.Reset();
    brand_realizations_.clear(); poster_shapes_.clear();
    desktop_shade_.Reset(); desktop_fade_top_.Reset(); desktop_fade_bottom_.Reset(); desktop_hero_fade_.Reset();
    squircle_compact_strokes_.clear(); squircle_compact_fill_.Reset();
    aura_.Reset(); status_shade_.Reset(); install_vignette_.Reset(); shade_.Reset(); solid_.Reset(); target_.Reset(); window_target_.Reset();
#ifdef _WIN32
    // Last: everything made on the device went above; a new one comes with the next target.
    video_texture_.Reset(); video_texture_bitmap_.Reset();
    swap_.reset();
#endif
}

DecodeKind Renderer::decodeKind(const Cover& cover) const {
    DecodeKind kind{!cover.hero && !cover.logo, cover.hero, cover.logo, cover.short_edge};
    kind.loose_edge = cover.loose_edge;
    // Backgrounds keep what covers the screen (at least 1080p, so a window made larger later stays
    // sharp): a 3840×1240 hero is 3344×1080 on a 1080p screen, not 1920×620 enlarged 1.75×.
    if (cover.hero && target_) {
        const auto pixels = target_->GetPixelSize();
        kind.cover_width = std::max(1920u, pixels.width);
        kind.cover_height = std::max(1080u, pixels.height);
    }
    // Kompresja grafik: opaque art as BC1, logos as BC3 (their alpha); backgrounds only in the low memory
    // mode, as their smooth full-screen gradients show BC1's steps most.
    kind.block_compress = block_compress_ && bc1_supported_ && (!cover.logo || bc3_supported_) && (!cover.hero || low_memory_);
    kind.block_alpha = kind.block_compress && cover.logo;
    return kind;
}

void Renderer::bindPicture(Cover& cover, ImageBytes pixels) {
    cover.pixels = std::move(pixels);
    cover.droppable = true; cover.dropped = false;
    cover.source.reset(); cover.source_size = 0; cover.source_hash = 0;
}

void Renderer::rebindPicture(Cover& cover, const ImageBytes& pixels) {
    if (!cover.dropped || !pixels) return;
    if (cover.source.expired()) cover.source = pixels; // Read again: the cache's new buffer.
    // To be decoded again (its bitmap went with the device): the bytes come back until uploaded.
    if (!cover.bitmap) { cover.pixels = pixels; cover.dropped = false; }
}

void Renderer::dropBytes(Cover& cover) {
    if (!low_memory_ || !cover.droppable || !cover.pixels || cover.decoding) return;
    cover.source = cover.pixels;
    cover.source_size = cover.pixels->size();
    cover.source_hash = imageHash(*cover.pixels);
    cover.pixels.reset();
    cover.dropped = true;
}

// Hands encoded bytes to a decoder; device loss preserves a decode already in flight.
void Renderer::createBitmap(Cover& cover) {
    if (cover.steelbook && cover.steelbook->source != cover.encoded()) cover.steelbook.reset();
    if (!target_ || !cover.loaded() || cover.bitmap) return;
    if (!cover.decoding) {
        auto bytes = cover.encoded();
        if (!bytes) { cover = {}; art_lost_ = true; return; } // Low memory: let go, and the bitmap went too.
        cover.decoding = decoder_.submit(std::move(bytes), decodeKind(cover));
    }
    if (cover.decoding->done.load(std::memory_order_acquire)) upload(cover, false);
}

void Renderer::upload(Cover& cover, bool reveal) {
    auto ticket = std::move(cover.decoding);
    if (!ticket || !target_) return;
    const auto& image = ticket->image;
    if (!image.valid()) return;
    const auto started = std::chrono::steady_clock::now();
    const auto properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    // A redecode at another size replaces the shown bitmap in place: no second fade-in.
    const bool replacing = cover.bitmap != nullptr;
    ComPtr<ID2D1Bitmap> bitmap;
    ComPtr<ID2D1BitmapBrush> brush;
    if (!image.blocks.empty()) {
        // Kompresja grafik: BC1 an eighth of the BGRA bitmap, BC3 (logos) a quarter. A target that
        // refuses it (or its brush) gets the blocks decoded here, and later pictures as BGRA.
        const bool bc3 = image.block_format == block_compression::Format::bc3;
        bool& supported = bc3 ? bc3_supported_ : bc1_supported_;
        ComPtr<ID2D1DeviceContext> context;
        ComPtr<ID2D1Bitmap1> compressed;
        if (supported && SUCCEEDED(target_.As(&context)) &&
            SUCCEEDED(context->CreateBitmap(D2D1::SizeU(image.width, image.height), image.blocks.data(), (image.width / 4) * (bc3 ? 16 : 8),
                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(bc3 ? DXGI_FORMAT_BC3_UNORM : DXGI_FORMAT_BC1_UNORM,
                                                                                    D2D1_ALPHA_MODE_PREMULTIPLIED)),
                compressed.GetAddressOf())) &&
            SUCCEEDED(target_->CreateBitmapBrush(compressed.Get(), brush.GetAddressOf()))) bitmap = std::move(compressed);
        else {
            supported = false; brush.Reset();
            const auto bgra = bc3 ? block_compression::decodeBc3(image.blocks.data(), image.blocks.size(), image.width, image.height)
                                  : block_compression::decodeBc1(image.blocks.data(), image.blocks.size(), image.width, image.height);
            if (bgra.empty() || FAILED(target_->CreateBitmap(D2D1::SizeU(image.width, image.height), bgra.data(), image.width * 4,
                                                             properties, bitmap.GetAddressOf()))) return;
        }
    } else if (FAILED(target_->CreateBitmap(D2D1::SizeU(image.width, image.height), image.pixels.data(), image.width * 4,
                                            properties, bitmap.GetAddressOf()))) return;
    cover.bitmap = std::move(bitmap);
    if (replacing) {
        cover.brush.Reset(); cover.gray_bitmap.Reset(); cover.gray_brush.Reset(); cover.gray_failed = false;
        cover.blurred.Reset(); cover.blurred_source.Reset(); cover.desaturated.Reset(); cover.desaturated_source.Reset();
        reveal = false;
    }
    if (!cover.hero && !cover.logo) cover.accent = color(image.accent[0], image.accent[1], image.accent[2]);
    if (cover.logo && image.ink.valid) {
        cover.ink_bounds = D2D1::RectF(static_cast<float>(image.ink.left), static_cast<float>(image.ink.top),
                                       static_cast<float>(image.ink.right), static_cast<float>(image.ink.bottom));
        cover.has_ink_bounds = true;
    }
    cover.brush.Reset();
    if (brush) cover.brush = std::move(brush);
    else target_->CreateBitmapBrush(cover.bitmap.Get(), cover.brush.GetAddressOf());
    if (cover.brush) cover.brush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    if (reveal) cover.revealed = last_reveal_ = std::chrono::steady_clock::now();
    dropBytes(cover); // Low memory: the launcher's cache or the disk has the picture.
    if (profiling_) { pending_upload_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); ++pending_uploads_; }
}

void Renderer::uploadDecoded() {
    const auto completed = decoder_.completed();
    if ((completed == decoded_seen_ && !uploads_left_) || !target_) return;
    decoded_seen_ = completed;
    uploads_left_ = false;
    // Finished decodes are uploaded most wanted first (DecodeTicket::priority) within a per-frame
    // budget: forty posters arriving together no longer cost one 15–20 ms frame at 4K. At least
    // one upload per frame; the rest waits for the next (animating() keeps frames coming).
    std::vector<std::pair<int, Cover*>> ready;
    const auto collect = [&](Cover& cover) {
        if (cover.decoding && cover.decoding->done.load(std::memory_order_acquire))
            ready.emplace_back(cover.decoding->priority.load(std::memory_order_relaxed), &cover);
        uploadSteelbook(cover);
    };
    for (auto* list : {&covers_, &logos_, &heroes_, &posters_, &discs_, &artwork_thumbnails_, &plugin_thumbnails_, &glance_thumbnails_, &tarot_images_, &profile_images_})
        for (auto& cover : *list) collect(cover);
    for (auto& tile : tile_thumbnails_) for (auto& thumbnail : tile) collect(thumbnail);
    for (auto& [id, avatar] : avatars_) { (void)id; collect(avatar); }
    for (auto& [id, icon] : widget_icons_) { (void)id; collect(icon); }
    for (auto& [name, badge] : platform_badges_) { (void)name; collect(badge); }
    for (auto& image : saver_slots_) collect(image);
    collect(file_browser_preview_);
    collect(notification_cover_);
    collect(custom_poster_);
    collect(companion_photo_);
    collect(companion_photo_large_);
    for (auto& [id, image] : companion_feed_images_) collect(image);
    std::stable_sort(ready.begin(), ready.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const auto started = std::chrono::steady_clock::now();
    for (size_t i = 0; i < ready.size(); ++i) {
        if (i && std::chrono::steady_clock::now() - started >= upload_budget) { uploads_left_ = true; break; }
        upload(*ready[i].second, true);
    }
}
