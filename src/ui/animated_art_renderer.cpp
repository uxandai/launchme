// Animated SteamGridDB artwork (animated_image.hpp). Zmień grafikę's tiles play their short loops
// from the start; a card's cover, poster or background moves only while the card has the focus
// (or the pointer, in the desktop window): src/app/animated_art.cpp hands its frames in, and they
// take the place of the still in that slot, so every layout drawing the slot shows them.
#include "renderer.hpp"

#include <algorithm>

namespace {
const auto frame_properties = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);

uint64_t sinceMs(std::chrono::steady_clock::time_point start) {
    return static_cast<uint64_t>(std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count()));
}
} // namespace

void Renderer::setArtworkThumbnailAnimation(size_t index, std::shared_ptr<const animated_image::Frames> frames) {
    if (index >= artwork_thumbnails_.size()) return;
    if (artwork_animations_.size() < artwork_thumbnails_.size()) artwork_animations_.resize(artwork_thumbnails_.size());
    auto& animation = artwork_animations_[index];
    animation = {};
    if (!frames || frames->pixels.empty() || !frames->width || !frames->height) return;
    animation.frames = std::move(frames);
    animation.started = std::chrono::steady_clock::now();
    artwork_thumbnails_[index].revealed = animation.started;
}

bool Renderer::artworkAnimationDue() const {
    for (const auto& animation : artwork_animations_)
        if (animation.frames && animation.frames->pixels.size() > 1 &&
            animation.frames->at(sinceMs(animation.started)) != animation.shown) return true;
    return false;
}

// Each moving tile's frame of this moment copied into its bitmap, which stands in its thumbnail slot.
void Renderer::stepArtworkAnimations() {
    if (!target_) return;
    for (size_t i = 0; i < artwork_animations_.size() && i < artwork_thumbnails_.size(); ++i) {
        auto& animation = artwork_animations_[i];
        if (!animation.frames) continue;
        const auto& frames = *animation.frames;
        const size_t index = frames.at(sinceMs(animation.started));
        if (!animation.bitmap) {
            animation.brush.Reset();
            animation.shown = SIZE_MAX;
            if (FAILED(target_->CreateBitmap(D2D1::SizeU(frames.width, frames.height), frames.pixels[index].data(), frames.width * 4,
                                             frame_properties, animation.bitmap.GetAddressOf()))) { animation = {}; continue; }
            target_->CreateBitmapBrush(animation.bitmap.Get(), animation.brush.GetAddressOf());
            if (animation.brush) animation.brush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else if (index != animation.shown) animation.bitmap->CopyFromMemory(nullptr, frames.pixels[index].data(), frames.width * 4);
        animation.shown = index;
        auto& slot = artwork_thumbnails_[i];
        if (slot.bitmap != animation.bitmap) { slot.bitmap = animation.bitmap; slot.brush = animation.brush; }
    }
}

D2D1_SIZE_U Renderer::animatedArtSize(size_t index, const ImageBytes& still) const {
    for (const auto* slots : {&covers_, &posters_, &heroes_})
        if (const auto* slot = artHolding(*slots, index, still)) return slot->bitmap->GetPixelSize();
    return {0, 0};
}

bool Renderer::setAnimatedArtFrame(size_t index, const ImageBytes& still, const uint8_t* pixels, uint32_t width, uint32_t height) {
    if (!target_ || !still || !pixels || !width || !height) return false;
    bool shown = false;
    for (auto* slots : {&covers_, &posters_, &heroes_}) {
        // This slot's stand-in already: the next frame into the same bitmap.
        auto found = std::find_if(art_frames_.begin(), art_frames_.end(), [&](const ArtFrame& frame) { return frame.slots == slots && frame.index == index; });
        if (found != art_frames_.end()) {
            const bool current = index < slots->size() && (*slots)[index].bitmap == found->frame && found->still_pixels == still;
            if (!current) { art_frames_.erase(found); found = art_frames_.end(); } // Bound anew since: the still there is another.
        }
        if (found == art_frames_.end()) {
            if (!artHolding(*slots, index, still)) continue;
            auto* slot = &(*slots)[index];
            ArtFrame frame{slots, index, still, slot->bitmap, slot->brush};
            if (FAILED(target_->CreateBitmap(D2D1::SizeU(width, height), pixels, width * 4, frame_properties, frame.frame.GetAddressOf())) ||
                FAILED(target_->CreateBitmapBrush(frame.frame.Get(), frame.brush.GetAddressOf()))) continue;
            frame.brush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            slot->bitmap = frame.frame;
            slot->brush = frame.brush;
            art_frames_.push_back(std::move(frame));
            shown = true;
            continue;
        }
        const auto size = found->frame->GetPixelSize();
        if (size.width != width || size.height != height) {
            // Another size (the player opened again for a larger slot): a new bitmap in its place.
            Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
            Microsoft::WRL::ComPtr<ID2D1BitmapBrush> brush;
            if (FAILED(target_->CreateBitmap(D2D1::SizeU(width, height), pixels, width * 4, frame_properties, bitmap.GetAddressOf())) ||
                FAILED(target_->CreateBitmapBrush(bitmap.Get(), brush.GetAddressOf()))) continue;
            brush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            found->frame = bitmap; found->brush = brush;
            (*slots)[index].bitmap = bitmap; (*slots)[index].brush = brush;
        } else found->frame->CopyFromMemory(nullptr, pixels, width * 4);
        shown = true;
    }
    return shown;
}

void Renderer::clearAnimatedArt() {
    // The still comes back where the slot still holds the frame bitmap (not bound anew since).
    for (auto& frame : art_frames_) {
        if (frame.index >= frame.slots->size()) continue;
        auto& slot = (*frame.slots)[frame.index];
        if (slot.bitmap != frame.frame) continue;
        slot.bitmap = frame.still_bitmap;
        slot.brush = frame.still_brush;
    }
    art_frames_.clear();
}

bool Renderer::animatedArtShown() const { return !art_frames_.empty(); }
