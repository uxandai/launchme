#include "internal.hpp"
#include "artwork/block_compression.hpp"
#include "include/core/SkImageGenerator.h"
#include "include/core/SkPixmap.h"
#include <algorithm>
#include <mutex>

namespace lm_impeller {
namespace {
struct Compressed {
    unsigned format;
    int width, height;
    std::vector<uint8_t> blocks;
};
std::mutex compressed_mutex;
std::unordered_map<uint32_t, std::weak_ptr<const Compressed>> compressed;

// CPU access (WIC, effects) decodes lazily. Normal Impeller draws take the original blocks.
class Generator final : public SkImageGenerator {
public:
    explicit Generator(std::shared_ptr<const Compressed> data)
        : SkImageGenerator(SkImageInfo::Make(data->width, data->height, kBGRA_8888_SkColorType, kPremul_SkAlphaType)), data_(std::move(data)) {}
    ~Generator() override {
        std::lock_guard lock(compressed_mutex);
        compressed.erase(uniqueID());
    }
protected:
    bool onGetPixels(const SkImageInfo& info, void* pixels, size_t stride, const Options&) override {
        if (info.width() != data_->width || info.height() != data_->height) return false;
        const auto& d = *data_;
        const auto bytes = d.format == 1 ? block_compression::decodeBc1(d.blocks.data(), d.blocks.size(), d.width, d.height)
                                         : block_compression::decodeBc3(d.blocks.data(), d.blocks.size(), d.width, d.height);
        return !bytes.empty() && SkPixmap(getInfo(), bytes.data(), static_cast<size_t>(d.width) * 4).readPixels(info, pixels, stride);
    }
private:
    std::shared_ptr<const Compressed> data_;
};
} // namespace

bool supportsCompression(unsigned format) {
    if (!active() || (format != 1 && format != 3)) return false;
    return LaunchmeImpellerSupportsFormat(resources().context.get(), format == 1 ? kLaunchmeImpellerBC1 : kLaunchmeImpellerBC3);
}

sk_sp<SkImage> compressedImage(unsigned format, int width, int height, const uint8_t* data, size_t size) {
    if (!supportsCompression(format) || !data || width <= 0 || height <= 0 || width > 16384 || height > 16384 || width % 4 || height % 4) return nullptr;
    const size_t expected = static_cast<size_t>(width / 4) * (height / 4) * (format == 1 ? 8 : 16);
    if (size != expected) return nullptr;
    auto payload = std::make_shared<Compressed>(Compressed{format, width, height, {data, data + size}});
    auto generator = std::make_unique<Generator>(payload);
    const uint32_t id = generator->uniqueID();
    auto image = SkImages::DeferredFromGenerator(std::move(generator));
    if (image) {
        std::lock_guard lock(compressed_mutex);
        compressed[id] = payload;
    }
    return image;
}

ImpellerTexture textureFor(const SkImage* image) {
    if (!active() || !image) return nullptr;
    auto& r = resources();
    if (auto it = r.images.find(image->uniqueID()); it != r.images.end()) {
        it->second.used = r.frame;
        return it->second.texture.get();
    }
    Texture texture;
    size_t bytes = 0;
    std::shared_ptr<const Compressed> payload;
    {
        std::lock_guard lock(compressed_mutex);
        if (auto it = compressed.find(image->uniqueID()); it != compressed.end()) payload = it->second.lock();
    }
    if (payload) {
        bytes = payload->blocks.size();
        texture.reset(LaunchmeImpellerTextureNew(r.context.get(), payload->format == 1 ? kLaunchmeImpellerBC1 : kLaunchmeImpellerBC3,
            payload->width, payload->height, payload->blocks.data(), bytes));
    } else {
        if (image->width() <= 0 || image->height() <= 0 || image->width() > 16384 || image->height() > 16384) return nullptr;
        bytes = static_cast<size_t>(image->width()) * image->height() * 4;
        if (bytes > 256u * 1024u * 1024u) return nullptr;
        std::vector<uint8_t> pixels(bytes);
        const auto info = SkImageInfo::Make(image->width(), image->height(), kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        if (!image->readPixels(nullptr, info, pixels.data(), static_cast<size_t>(image->width()) * 4, 0, 0)) return nullptr;
        texture.reset(LaunchmeImpellerTextureNew(r.context.get(), kLaunchmeImpellerRGBA, image->width(), image->height(), pixels.data(), bytes));
    }
    if (!texture) return nullptr;
    const auto result = texture.get();
    r.images.emplace(image->uniqueID(), ImageEntry{sk_ref_sp(image), std::move(texture), bytes, r.frame});
    return result;
}

void Resources::trim() {
    // Drop GPU copies whose CPU source no longer has an owner, then the least recently used.
    // Display lists retain their own texture references until the GPU submission is safe.
    size_t bytes = 0;
    for (auto it = images.begin(); it != images.end();) {
        if (it->second.image->unique()) it = images.erase(it);
        else { bytes += it->second.bytes; ++it; }
    }
    constexpr size_t budget = 128u * 1024u * 1024u;
    while (bytes > budget && !images.empty()) {
        const auto oldest = std::min_element(images.begin(), images.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
        bytes -= oldest->second.bytes;
        images.erase(oldest);
    }
    size_t text_bytes = 0;
    for (const auto& entry : texts) text_bytes += static_cast<size_t>(entry.image->width()) * entry.image->height() * 4;
    while ((texts.size() > 512 || text_bytes > 32u * 1024u * 1024u) && !texts.empty()) {
        const auto oldest = std::min_element(texts.begin(), texts.end(), [](const auto& a, const auto& b) { return a.used < b.used; });
        text_bytes -= static_cast<size_t>(oldest->image->width()) * oldest->image->height() * 4;
        texts.erase(oldest);
    }
}
} // namespace lm_impeller
