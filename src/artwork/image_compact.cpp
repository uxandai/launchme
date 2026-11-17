#include "image_compact.hpp"
#include "../platform/file_io.hpp"
#include "text_crop.hpp"
#include "image_codec.hpp"
#include "../platform/atomic_file.hpp"

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <fstream>

namespace {
std::atomic_bool keep_4k_backgrounds{false};
struct Box { uint32_t width, height; };
// What the launcher draws at most (a 2560 px wide window by default, or 3840×2160 with 4K backgrounds on).
Box limit(ImageRole role) {
    switch (role) {
    case ImageRole::background:
        return keep_4k_backgrounds.load(std::memory_order_relaxed) ? Box{3840, 2160} : Box{2560, 1440};
    case ImageRole::landscape: return {1280, 720};
    case ImageRole::cover: return {1024, 1024};
    case ImageRole::poster: return {800, 1200};
    default: return {1280, 720}; // Logos.
    }
}

std::wstring lower(std::wstring text) {
    for (auto& ch : text) ch = static_cast<wchar_t>(std::towlower(ch));
    return text;
}

std::vector<uint8_t> readFile(const std::filesystem::path& file) { return file_io::readBytes(file, 64 * 1024 * 1024); }
}

void setBackgrounds4k(bool enabled) { keep_4k_backgrounds.store(enabled, std::memory_order_relaxed); }
bool backgrounds4k() { return keep_4k_backgrounds.load(std::memory_order_relaxed); }

double compactScale(uint32_t width, uint32_t height, ImageRole role) {
    if (!width || !height) return 1.0;
    const auto box = limit(role);
    const double across = static_cast<double>(box.width) / width, down = static_cast<double>(box.height) / height;
    // A background is drawn cover-fit (it fills the screen and its sides are cut off), so it keeps
    // what still covers the box: a 3840×1240 hero stays whole, 2560 px wide it would cover a 16:9
    // screen only 827 px high and look soft. Everything else fits inside its box.
    return std::min(1.0, role == ImageRole::background ? std::max(across, down) : std::min(across, down));
}

std::vector<uint8_t> compactImage(std::span<const uint8_t> bytes, ImageRole role) {
    // PNG and JPEG only: other files are left as they are.
    const bool png = bytes.size() > 8 && bytes[0] == 0x89 && bytes[1] == 'P';
    const bool jpeg = bytes.size() > 3 && bytes[0] == 0xff && bytes[1] == 0xd8;
    if ((!png && !jpeg) || bytes.size() > 64 * 1024 * 1024 || bytes.size() < 96 * 1024) return {};
    // A JPEG already within the limits gains nothing from a second encoding.
    const auto image = image_codec::decode(bytes, [&](const image_codec::Info& info) -> std::optional<image_codec::Size> {
        if (info.width > 16384 || info.height > 16384) return {};
        const double scale = compactScale(info.width, info.height, role);
        if (jpeg && scale >= 1.0) return {};
        return image_codec::Size{std::max<uint32_t>(1, static_cast<uint32_t>(info.width * scale + 0.5)),
                                 std::max<uint32_t>(1, static_cast<uint32_t>(info.height * scale + 0.5))};
    }, image_codec::Filter::cubic);
    if (!image) return {};
    bool transparent = false;
    for (size_t i = 3; i < image->pixels.size(); i += 4) if (image->pixels[i] < 250) { transparent = true; break; }
    const auto result = transparent ? image_codec::encodePng(*image) : image_codec::encodeJpeg(*image, 0.88f);
    if (result.empty() || result.size() > bytes.size()) return {};
    // Worth it only when clearly smaller.
    return result.size() < bytes.size() * 9 / 10 ? result : std::vector<uint8_t>{};
}

bool imageRoleFor(const std::filesystem::path& file, ImageRole& role) {
    if (lower(file.extension().wstring()) != L".img") return false;
    auto path = lower(file.lexically_normal().wstring());
    std::replace(path.begin(), path.end(), L'/', L'\\'); // Folder rules below use Windows separators.
    const auto name = lower(file.filename().wstring());
    const auto has = [&](std::wstring_view part) { return path.find(part) != std::wstring::npos; };
    // Achievement icons, browser thumbnails and platform logos stay as they are.
    if (has(L"\\thumbnails\\") || has(L"\\achievements\\") || has(L"\\platforms\\") || has(L"\\avatars\\")) return false;
    if (has(L"\\logos\\") || has(L"\\logo\\") || name.starts_with(L"logo")) { role = ImageRole::logo; return true; }
    if (has(L"\\heroes\\") || has(L"\\hero\\") || has(L"\\backgrounds\\") || name.starts_with(L"hero")) { role = ImageRole::background; return true; }
    if (has(L"\\landscapes\\") || has(L"\\landscape\\") || name.starts_with(L"landscape")) { role = ImageRole::landscape; return true; }
    if (has(L"\\posters\\") || has(L"\\poster\\") || name.starts_with(L"poster")) { role = ImageRole::poster; return true; }
    if (has(L"\\covers\\") || has(L"\\cover\\") || name.starts_with(L"cover")) { role = ImageRole::cover; return true; }
    if (has(L"\\discs\\") || has(L"\\disc\\") || name.starts_with(L"disc")) { role = ImageRole::cover; return true; } // Square; alpha is preserved by the encoder.
    return false;
}

namespace { thread_local bool missing_images_only = false; }
MissingImagesOnly::MissingImagesOnly() : previous_(missing_images_only) { missing_images_only = true; }
MissingImagesOnly::~MissingImagesOnly() { missing_images_only = previous_; }
bool writeCachedImage(const std::filesystem::path& file, std::span<const uint8_t> bytes) {
    ImageRole role{};
    if (imageRoleFor(file, role))
        if (const auto compact = compactImage(bytes, role); !compact.empty()) return atomic_file::write(file, compact, !missing_images_only);
    return atomic_file::write(file, bytes, !missing_images_only);
}

size_t releaseShrunkenBackgrounds(const std::filesystem::path& root, const std::function<bool()>& keep_going) {
    size_t removed = 0;
    // Steam games' heroes\ and other stores' stores\<source>\<key>\hero.v2.img. Picks
    // (artwork\selections) and ES-DE copies (emulation\) are not downloaded again by themselves.
    for (const auto* folder : {L"heroes", L"stores"}) {
        std::error_code ec;
        for (auto item = std::filesystem::recursive_directory_iterator(root / folder, ec); !ec && item != std::filesystem::recursive_directory_iterator(); item.increment(ec)) {
            if (!keep_going()) return removed;
            std::error_code file;
            ImageRole role{};
            if (!item->is_regular_file(file) || !imageRoleFor(item->path(), role) || role != ImageRole::background) continue;
            const auto [width, height] = cropImageSize(item->path());
            if (width != 2560 || height <= 0 || height >= 1440 || manualTextCrop(item->path())) continue;
            auto focus = item->path(); focus.replace_extension(L".focus");
            if (std::filesystem::remove(item->path(), file)) { ++removed; std::filesystem::remove(focus, file); }
        }
    }
    return removed;
}

size_t releaseSub4kBackgrounds(const std::filesystem::path& root, const std::function<bool()>& keep_going) {
    size_t removed = 0;
    for (const auto* folder : {L"heroes", L"stores"}) {
        std::error_code ec;
        for (auto item = std::filesystem::recursive_directory_iterator(root / folder, ec); !ec && item != std::filesystem::recursive_directory_iterator(); item.increment(ec)) {
            if (!keep_going()) return removed;
            std::error_code file;
            ImageRole role{};
            if (!item->is_regular_file(file) || !imageRoleFor(item->path(), role) || role != ImageRole::background) continue;
            const auto [width, height] = cropImageSize(item->path());
            if (width <= 0 || height <= 0 || width >= 3000 || manualTextCrop(item->path())) continue;
            auto focus = item->path(); focus.replace_extension(L".focus");
            if (std::filesystem::remove(item->path(), file)) { ++removed; std::filesystem::remove(focus, file); }
        }
    }
    return removed;
}

uint64_t compactImageCache(const std::filesystem::path& root, const std::function<bool()>& keep_going) {
    uint64_t saved = 0;
    for (const auto* folder : {L"heroes", L"logos", L"covers", L"posters", L"landscapes", L"stores", L"emulation", L"artwork"}) {
        std::error_code ec;
        for (auto item = std::filesystem::recursive_directory_iterator(root / folder, ec); !ec && item != std::filesystem::recursive_directory_iterator(); item.increment(ec)) {
            if (!keep_going()) return saved;
            std::error_code file;
            ImageRole role{};
            if (!item->is_regular_file(file) || !imageRoleFor(item->path(), role)) continue;
            const auto bytes = readFile(item->path());
            if (bytes.empty()) continue;
            const auto compact = compactImage(bytes, role);
            if (!compact.empty() && writeImageWithoutManualCrop(item->path(), compact)) saved += bytes.size() - compact.size();
        }
    }
    return saved;
}
