#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <vector>

// Artwork as it is kept in the cache: scaled down to what the launcher draws and re-encoded
// with the WIC codecs Windows ships (no WebP encoder there): JPEG when fully opaque, PNG when
// it has transparency (logos). SteamGridDB heroes arrive as 3840 px PNGs and logos up to 8000 px.
enum class ImageRole { background, landscape, cover, poster, logo };

// Media i dane → Okładki → Tła gier w 4K (AppSettings::backgrounds_4k): keeps up to 3840×2160
// instead of 2560×1440 and prefers 4K Steam / SteamGridDB heroes over local 1920×620 copies.
void setBackgrounds4k(bool enabled);
bool backgrounds4k();

// The factor (at most 1) a width × height image is scaled by when cached: backgrounds keep what
// covers 2560×1440 (or 3840×2160 with backgrounds4k(); they are drawn cover-fit), other roles fit inside their box.
double compactScale(uint32_t width, uint32_t height, ImageRole role);
// The smaller re-encoding, or empty when the image is already small enough (keep the input).
std::vector<uint8_t> compactImage(std::span<const uint8_t> bytes, ImageRole role);
// Backgrounds the first compaction (before 0.3.0) shrank to 2560 px wide and under 1440 px high:
// automatic ones (no manual crop, not a user's pick) are deleted, so they are downloaded again at
// full size. Returns how many were removed; keep_going is polled between files.
size_t releaseShrunkenBackgrounds(const std::filesystem::path& root, const std::function<bool()>& keep_going);
// Automatic backgrounds narrower than 3000 px (e.g. Steam's local 1920×620 library_hero.jpg or
// 1440p compacted art), removed once when "Tła gier w 4K" is switched on so 4K copies can replace them.
size_t releaseSub4kBackgrounds(const std::filesystem::path& root, const std::function<bool()>& keep_going);
// The role a cache file plays, from its folder and name (heroes\, logos\, stores\...\hero.v2.img,
// artwork\selections\...\logo\..., emulation\backgrounds\...); false for anything else.
bool imageRoleFor(const std::filesystem::path& file, ImageRole& role);
// Writes the image to a cache file, compacted for its role when it has one.
bool writeCachedImage(const std::filesystem::path& file, std::span<const uint8_t> bytes);
// The bulk worker cannot replace a file, even if another worker creates it during a transfer.
class MissingImagesOnly {
public:
    MissingImagesOnly();
    ~MissingImagesOnly();
    MissingImagesOnly(const MissingImagesOnly&) = delete;
    MissingImagesOnly& operator=(const MissingImagesOnly&) = delete;
private:
    bool previous_;
};
// One pass over an existing cache (the folders above): every image larger than its role needs
// is re-encoded in place. keep_going is polled between files. Returns the bytes saved.
uint64_t compactImageCache(const std::filesystem::path& root, const std::function<bool()>& keep_going);
