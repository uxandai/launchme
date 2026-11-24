#pragma once

#include <filesystem>
#include <vector>
#include <string_view>
#include <optional>
#include <utility>
#include <span>
#include <cstdint>

struct TextBox {
    int left = 0, top = 0, width = 0, height = 0;
    float confidence = 0;
    bool title_match = false;
};

struct CropFocus {
    float x = 0.5f;
    float y = 0.5f;
};
// 6: art without readable lettering keeps its most detailed square (chooseDetailCrop), not the
// middle; a centred crop of version 5 on such art is upgraded without OCR.
inline constexpr int crop_cache_version = 6;
std::pair<int, int> cropImageSize(const std::filesystem::path& image);
std::optional<CropFocus> manualTextCrop(const std::filesystem::path& image);
bool saveManualTextCrop(const std::filesystem::path& image, CropFocus focus);
bool clearManualTextCrop(const std::filesystem::path& image);
// Cache maintenance must not invalidate a user's image-bound crop. Serialized with crop saves.
bool writeImageWithoutManualCrop(const std::filesystem::path& image, std::span<const uint8_t> bytes);
// Read a saved manual/automatic crop without starting OCR; nullopt needs analysis.
std::optional<CropFocus> storedTextCrop(const std::filesystem::path& image, const std::filesystem::path& metadata,
                                      std::wstring_view title = {});
CropFocus cachedAutomaticCrop(const std::filesystem::path& image, const std::filesystem::path& metadata,
                              std::wstring_view title = {}, bool force = false);
// Records an automatic crop for `image` as cachedAutomaticCrop would, without OCR (benchmark
// fixtures and cache tools); storedTextCrop reads it back. False when the image is unreadable.
bool storeAutomaticTextCrop(const std::filesystem::path& image, const std::filesystem::path& metadata,
                            std::wstring_view title, CropFocus focus);

// Chooses a square window at cover scale (never letterboxes the artwork).
// Matched title words take priority over publisher/footer text. Centers the
// title's bounds with breathing room; without reliable words, centers the image.
CropFocus chooseTextAwareCrop(int width, int height, const std::vector<TextBox>& words);

// Without readable lettering: the square that keeps the most detail (the logo, the figures), not an
// empty sky or floor. `bgra`: width * height packed BGRA pixels (best at ~96 px on the shorter side).
CropFocus chooseDetailCrop(uint32_t width, uint32_t height, std::span<const uint8_t> bgra);
// chooseDetailCrop of an image file (decoded small); centred when unreadable or square.
CropFocus detailCrop(const std::filesystem::path& image);

// Title matching can rescue stylized lettering at lower OCR confidence without
// treating arbitrary low-confidence scenery as text.
std::vector<TextBox> parseCropWords(std::string_view tsv, std::wstring_view title = {});

// Tesseract runs only for non-square art, off the render thread. Results are cached.
CropFocus cachedTextCrop(const std::filesystem::path& image, const std::filesystem::path& metadata,
                         std::wstring_view title = {}, bool force = false);
