// Images: premultiplied BGRA pixels or BC1/BC3 blocks in memory, uploaded to an Impeller texture
// the first time a canvas on the context's thread draws them, read back when a CPU consumer
// (WIC, effects) asks. Reduced copies (box filtered) stand in for mipmaps when an image is drawn
// much smaller than it is.
#include "kernel.hpp"
#include "state.hpp"

#include "artwork/block_compression.hpp"

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

std::shared_ptr<Image> Image::fromCompressed(unsigned format, int width, int height, const uint8_t* blocks, size_t bytes) {
    if ((format != 1 && format != 3) || !blocks || !sizeOk(width, height) || width % 4 || height % 4) return nullptr;
    const size_t expected = static_cast<size_t>(width / 4) * (height / 4) * (format == 1 ? 8 : 16);
    if (bytes != expected) return nullptr;
    std::shared_ptr<Image> image(new Image);
    image->width_ = width;
    image->height_ = height;
    image->id_ = next_id++;
    image->format_ = format;
    image->blocks_.assign(blocks, blocks + bytes);
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
    image->epoch_ = state().epoch;
    ImageAccess::add(image.get());
    return image;
}

bool Image::onGpu() const { return texture_ && epoch_ == state().epoch; }

size_t Image::gpuBytes() const {
    if (!onGpu()) return 0;
    return format_ ? blocks_.size() : static_cast<size_t>(width_) * height_ * 4;
}

void Image::dropGpu() {
    if (!texture_) return;
    // Only what can be uploaded again.
    if (!format_ && !pixels_) return;
    if (epoch_ == state().epoch) ImpellerTextureRelease(texture_);
    texture_ = nullptr;
}

std::shared_ptr<const std::vector<uint8_t>> Image::pixels() {
    if (pixels_) return pixels_;
    if (format_) {
        auto bytes = format_ == 1 ? block_compression::decodeBc1(blocks_.data(), blocks_.size(), width_, height_)
                                  : block_compression::decodeBc3(blocks_.data(), blocks_.size(), width_, height_);
        if (bytes.empty()) return nullptr;
        // Not kept: a compressed image exists to avoid holding its pixels.
        return std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
    }
    if (!onGpu() || !active()) return nullptr;
    std::vector<uint8_t> bytes(static_cast<size_t>(width_) * height_ * 4);
    if (!LaunchmeImpellerTextureRead(context(), texture_, bytes.data(), bytes.size())) return nullptr;
    if (rgba_) for (size_t i = 0; i < bytes.size(); i += 4) std::swap(bytes[i], bytes[i + 2]);
    pixels_ = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
    rgba_ = false;
    return pixels_;
}

ImpellerTexture Image::texture() {
    if (!active()) return nullptr;
    used_ = state().frame;
    if (texture_ && epoch_ == state().epoch) return texture_;
    texture_ = nullptr;
    ImpellerTexture made = nullptr;
    if (format_) {
        made = LaunchmeImpellerTextureNew(context(), format_ == 1 ? kLaunchmeImpellerBC1 : kLaunchmeImpellerBC3, width_, height_,
                                          blocks_.data(), blocks_.size());
    } else if (pixels_) {
        made = LaunchmeImpellerTextureNew(context(), kLaunchmeImpellerBGRA, width_, height_, pixels_->data(), pixels_->size());
        // The memory copy goes once the GPU has the pixels (read back when someone needs them);
        // images that are drawn once and thrown away never pay for both.
        if (made) { rgba_ = false; pixels_.reset(); }
    }
    if (!made) return nullptr;
    texture_ = made;
    epoch_ = state().epoch;
    return texture_;
}

std::shared_ptr<Image> Image::reduced(int level) {
    if (level <= 0) return shared_from_this();
    level = std::min(level, 8);
    if (static_cast<int>(levels_.size()) >= level && levels_[level - 1]) return levels_[level - 1];
    const auto source = pixels();
    if (!source) return nullptr;
    std::shared_ptr<const std::vector<uint8_t>> current = source;
    int w = width_, h = height_;
    levels_.resize(std::max<size_t>(levels_.size(), level));
    for (int step = 1; step <= level; ++step) {
        const int nw = std::max(1, (w + 1) / 2), nh = std::max(1, (h + 1) / 2);
        std::vector<uint8_t> next(static_cast<size_t>(nw) * nh * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x) {
                const int x0 = std::min(2 * x, w - 1), x1 = std::min(2 * x + 1, w - 1), y0 = std::min(2 * y, h - 1), y1 = std::min(2 * y + 1, h - 1);
                for (int c = 0; c < 4; ++c) {
                    const int sum = (*current)[(static_cast<size_t>(y0) * w + x0) * 4 + c] + (*current)[(static_cast<size_t>(y0) * w + x1) * 4 + c] +
                                    (*current)[(static_cast<size_t>(y1) * w + x0) * 4 + c] + (*current)[(static_cast<size_t>(y1) * w + x1) * 4 + c];
                    next[(static_cast<size_t>(y) * nw + x) * 4 + c] = static_cast<uint8_t>((sum + 2) / 4);
                }
            }
        w = nw;
        h = nh;
        auto made = Image::fromPixels(w, h, std::move(next));
        if (!made) return nullptr;
        current = made->pixels_;
        levels_[step - 1] = std::move(made);
    }
    return levels_[level - 1];
}

// Drops textures that compressed images can upload again but nothing has drawn for a while.
void trimImages(uint64_t frame) {
    std::lock_guard lock(registry_mutex);
    for (Image* image : registry())
        if (image->compressed() && image->texture_ && image->epoch_ == state().epoch && frame - image->used_ > 600) {
            ImpellerTextureRelease(image->texture_);
            image->texture_ = nullptr;
        }
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
