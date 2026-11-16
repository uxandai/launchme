// Images: premultiplied BGRA pixels in memory, uploaded to an Impeller texture the first time a
// canvas on the context's thread draws them, read back when a CPU consumer (WIC, effects) asks.
// Reduced copies (halved on the GPU, each a 2x2 box filter) stand in for mipmaps when an image is
// drawn much smaller than it is.
#include "kernel.hpp"
#include "state.hpp"

#include <atomic>
#include <cstring>
#include <mutex>
#include <set>

namespace lm_impeller {
namespace {
std::atomic<uint64_t> next_id{1};
std::mutex registry_mutex;
std::set<Image*>& registry() { static std::set<Image*> images; return images; }

bool sizeOk(int width, int height) { return width > 0 && height > 0 && width <= 16384 && height <= 16384; }
} // namespace

struct ImageAccess {
    static void add(Image* image) { std::lock_guard lock(registry_mutex); registry().insert(image); }
    static void remove(Image* image) { std::lock_guard lock(registry_mutex); registry().erase(image); }
};

Image::~Image() {
    ImageAccess::remove(this);
    if (texture_ && epoch_ == state().epoch) ImpellerTextureRelease(texture_);
}

std::shared_ptr<Image> Image::fromPixels(int width, int height, std::vector<uint8_t> bgra) {
    if (!sizeOk(width, height) || bgra.size() != static_cast<size_t>(width) * height * 4) return nullptr;
    std::shared_ptr<Image> image(new Image);
    image->width_ = width;
    image->height_ = height;
    image->id_ = next_id++;
    image->pixels_ = std::make_shared<const std::vector<uint8_t>>(std::move(bgra));
    ImageAccess::add(image.get());
    return image;
}

std::shared_ptr<Image> Image::fromTexture(ImpellerTexture texture, int width, int height) {
    if (!texture || !sizeOk(width, height)) return nullptr;
    std::shared_ptr<Image> image(new Image);
    image->width_ = width;
    image->height_ = height;
    image->id_ = next_id++;
    image->texture_ = texture;
    image->rgba_ = true;
    image->rendered_ = true;
    image->epoch_ = state().epoch;
    ImageAccess::add(image.get());
    return image;
}

bool Image::upsideDown() const { return false; }

bool Image::onGpu() const { return texture_ && epoch_ == state().epoch; }

size_t Image::gpuBytes() const {
    return onGpu() ? static_cast<size_t>(width_) * height_ * 4 : 0;
}

void Image::dropGpu() {
    if (!texture_) return;
    // Only what can be uploaded again.
    if (!pixels_) return;
    if (epoch_ == state().epoch) ImpellerTextureRelease(texture_);
    texture_ = nullptr;
}

std::shared_ptr<const std::vector<uint8_t>> Image::pixels() {
    if (pixels_) return pixels_;
    if (!onGpu() || !active()) return nullptr;
    std::vector<uint8_t> bytes(static_cast<size_t>(width_) * height_ * 4);
    if (!LaunchmeImpellerTextureRead(context(), texture_, bytes.data(), bytes.size())) return nullptr;
    if (rgba_) for (size_t i = 0; i < bytes.size(); i += 4) std::swap(bytes[i], bytes[i + 2]);
    pixels_ = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
    rgba_ = false;
    return pixels_;
}

namespace {
// Pixels in the order the context's textures take them (OpenGL ES: RGBA).
ImpellerTexture upload(const std::vector<uint8_t>& bgra, int width, int height, bool& rgba) {
    rgba = !bgraNative();
    if (!rgba) return LaunchmeImpellerTextureNew(context(), kLaunchmeImpellerBGRA, width, height, bgra.data(), bgra.size());
    std::vector<uint8_t> swapped(bgra);
    for (size_t i = 0; i < swapped.size(); i += 4) std::swap(swapped[i], swapped[i + 2]);
    return LaunchmeImpellerTextureNew(context(), kLaunchmeImpellerRGBA, width, height, swapped.data(), swapped.size());
}
} // namespace

ImpellerTexture Image::texture() {
    if (!active()) return nullptr;
    used_ = state().frame;
    if (texture_ && epoch_ == state().epoch) return texture_;
    texture_ = nullptr;
    if (!pixels_) return nullptr;
    bool rgba = false;
    ImpellerTexture made = upload(*pixels_, width_, height_, rgba);
    if (!made) return nullptr;
    // The memory copy goes once the GPU has the pixels (read back when someone needs them);
    // images that are drawn once and thrown away never pay for both.
    rgba_ = rgba;
    pixels_.reset();
    texture_ = made;
    epoch_ = state().epoch;
    return texture_;
}

bool Image::replace(const std::vector<uint8_t>& bgra) {
    if (!active() || !onGpu() || used_ == state().frame || bgra.size() != static_cast<size_t>(width_) * height_ * 4) return false;
    bool ok = false;
    if (bgraNative()) {
        ok = !rgba_ && LaunchmeImpellerTextureUpdate(context(), texture_, bgra.data(), bgra.size());
    } else if (rgba_) {
        std::vector<uint8_t> swapped(bgra);
        for (size_t i = 0; i < swapped.size(); i += 4) std::swap(swapped[i], swapped[i + 2]);
        ok = LaunchmeImpellerTextureUpdate(context(), texture_, swapped.data(), swapped.size());
    }
    if (!ok) return false;
    pixels_.reset();
    levels_.clear();
    return true;
}

std::shared_ptr<Image> Image::reduced(int level) {
    if (level <= 0) return shared_from_this();
    if (!active()) return nullptr;
    level = std::min(level, 8);
    levels_.resize(std::max<size_t>(levels_.size(), level));
    // Each level halves the one above it on the GPU: linear sampling at the centre of every 2x2
    // block averages it. A level lost with its context (or never made) is made again.
    std::shared_ptr<Image> current = shared_from_this();
    for (int step = 1; step <= level; ++step) {
        auto& slot = levels_[step - 1];
        if (!slot || !slot->onGpu()) {
            const int w = current->width(), h = current->height();
            const int nw = std::max(1, (w + 1) / 2), nh = std::max(1, (h + 1) / 2);
            Canvas canvas(nw, nh);
            if (!canvas.healthy()) return nullptr;
            Paint copy;
            copy.color = {0, 0, 0, 1};
            copy.blend = kImpellerBlendModeSource;
            canvas.drawImageRect(current, Rect::XYWH(0, 0, static_cast<float>(w), static_cast<float>(h)),
                                 Rect::XYWH(0, 0, static_cast<float>(nw), static_cast<float>(nh)), Sampling{false}, &copy, false);
            slot = canvas.snapshot();
            if (!slot) return nullptr;
        }
        current = slot;
    }
    return current;
}

void releaseAllImages() {
    std::lock_guard lock(registry_mutex);
    for (Image* image : registry()) {
        if (!image->texture_) continue;
        if (image->epoch_ == state().epoch) ImpellerTextureRelease(image->texture_);
        // A render result has no copy anywhere else; make it a transparent loss rather than a stale handle.
        image->texture_ = nullptr;
    }
}
} // namespace lm_impeller
