// Interfejs → Tapeta (src/app/wallpaper.cpp): the picture or the video frame, cover-fit behind a
// screen. Drawn by the screens that use it (background_renderer.cpp, snitch, Settings, "Zmień
// grafikę"); wallpaper_drawn_ tells the app whether the video's frames are seen at all.
#include "renderer.hpp"
#include "renderer_internal.hpp"

#include <algorithm>

void Renderer::setWallpaperImage(ImageBytes pixels) {
    if (pixels && wallpaper_.pixels && *pixels == *wallpaper_.pixels) return;
    wallpaper_ = {};
    if (!pixels) return;
    wallpaper_.hero = true; // Decoded to cover the screen, like a game's background.
    wallpaper_.pixels = std::move(pixels);
    createBitmap(wallpaper_);
}

void Renderer::setWallpaperFrame(const uint8_t* bgra, uint32_t width, uint32_t height) {
    if (!target_ || !bgra || !width || !height) return;
    if (wallpaper_video_) {
        const auto size = wallpaper_video_->GetPixelSize();
        if (size.width == width && size.height == height) { wallpaper_video_->CopyFromMemory(nullptr, bgra, width * 4); return; }
    }
    wallpaper_video_.Reset();
    target_->CreateBitmap(D2D1::SizeU(width, height), bgra, width * 4,
        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)), wallpaper_video_.GetAddressOf());
}

void Renderer::clearWallpaperVideo() { wallpaper_video_.Reset(); }

void Renderer::setWallpaperPlaces(bool snitch, bool menus, bool artwork, bool tiles) {
    wallpaper_places_ = {snitch, menus, artwork, tiles};
}

bool Renderer::hasWallpaper() const { return wallpaper_video_ || wallpaper_.bitmap || (wallpaper_.pixels && wallpaper_.decoding); }

bool Renderer::drawWallpaperBackdrop(float opacity) {
    ID2D1Bitmap* bitmap = wallpaper_video_ ? wallpaper_video_.Get() : wallpaper_.bitmap.Get();
    if (!bitmap || opacity <= 0.003f || !target_) return false;
    const auto pixels = bitmap->GetPixelSize();
    if (!pixels.width || !pixels.height) return false;
    const float w = static_cast<float>(pixels.width), h = static_cast<float>(pixels.height);
    // Cover-fit and centred; the same picture fills any window shape.
    const float scale = std::max(viewport_.width / w, viewport_.height / h);
    const float x = (viewport_.width - w * scale) * 0.5f, y = (viewport_.height - h * scale) * 0.5f;
    const auto source = D2D1::RectF(0, 0, w, h);
    target_->DrawBitmap(bitmap, D2D1::RectF(x, y, x + w * scale, y + h * scale), std::clamp(opacity, 0.0f, 1.0f),
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
    wallpaper_drawn_ = true;
    return true;
}
