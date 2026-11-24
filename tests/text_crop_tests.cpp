#include "../src/artwork/text_crop.hpp"
#include "../src/artwork/image_compact.hpp"
#include "../src/ui/carousel.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include "image_fixture.hpp"
#include <fstream>

int main() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const auto temporary = std::filesystem::temp_directory_path() / (L"launchme-manual-crop-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); CoUninitialize(); } } cleanup{temporary};
    std::filesystem::create_directories(temporary);
    const auto image = temporary / L"poster.png", metadata = temporary / L"poster.focus";
    const auto put = [&](uint8_t tint) {
        const auto bytes = pngFixture(tint, 32, 48);
        std::ofstream output(image, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    };
    put(40);
    if (!saveManualTextCrop(image, {0.5f, 0.6f}) || !manualTextCrop(image)) return 10;
    if (std::abs(cachedTextCrop(image, metadata, L"Fixture", true).y - 0.6f) > 0.0001f) return 11;
    put(40); // Rewriting identical original bytes must preserve the manual choice.
    if (!manualTextCrop(image)) return 12;
    put(80); // A different image at the same path must not inherit another crop.
    if (manualTextCrop(image)) return 13;
    if (!clearManualTextCrop(image) || manualTextCrop(image)) return 14;
    // Background compaction used to rewrite the image and silently invalidate its manual crop.
    {
        const auto covers = temporary / L"covers";
        std::filesystem::create_directories(covers);
        auto original = pngFixture(40, 1600, 2400);
        original.resize(2 * 1024 * 1024, 0); // Valid PNG with padding, large enough to exercise compaction.
        const auto cover = covers / L"fixture.v2.img", automatic = covers / L"automatic.v2.img";
        for (const auto& file : {cover, automatic}) {
            std::ofstream out(file, std::ios::binary);
            out.write(reinterpret_cast<const char*>(original.data()), static_cast<std::streamsize>(original.size()));
        }
        const auto compact = compactImage(original, ImageRole::cover);
        if (compact.empty() || !saveManualTextCrop(cover, {0.5f, 0.65f})) return 15;
        if (!compactImageCache(temporary, [] { return true; })) return 16;
        const auto crop = manualTextCrop(cover);
        if (!crop || std::abs(crop->y - 0.65f) > 0.0001f || std::filesystem::file_size(cover) != original.size() ||
            std::filesystem::file_size(automatic) >= original.size()) return 17;
        auto focus = cover; focus.replace_extension(L".focus");
        const auto stored = storedTextCrop(cover, focus, L"Fixture");
        if (!stored || std::abs(stored->y - 0.65f) > 0.0001f) return 18;
    }
    // Real TSV geometry/confidence from the reported poster. The decorative
    // logo was recognized below the old 60% cutoff and silently center-cropped.
    constexpr auto stylized =
        "5\t1\t1\t1\t1\t1\t127\t111\t364\t236\t41.232819\tDweller\n"
        "5\t1\t2\t1\t1\t1\t269\t455\t56\t55\t35.55\t@\n"
        "5\t1\t3\t1\t1\t1\t65\t512\t266\t115\t35.43\tee\n"
        "5\t1\t5\t1\t1\t1\t154\t669\t192\t76\t40.98\tpee\n";
    auto recognized = parseCropWords(stylized, L"Well Dweller");
    if (recognized.size() != 1 || !recognized[0].title_match || !parseCropWords(stylized, L"Other game").empty()) return 2;
    const auto well = chooseTextAwareCrop(600, 900, recognized);
    if (well.y * 900 - 300 > 1) { std::cerr << "Stylized title and its upper ornament were clipped\n"; return 3; }
    // A crisp publisher/footer must not overrule a weaker match of the game's title.
    recognized.push_back({60, 815, 440, 50, 99});
    if (std::abs(chooseTextAwareCrop(600, 900, recognized).y - well.y) > 0.001f) return 4;
    const auto central_title = chooseTextAwareCrop(600, 900,
        {{80, 400, 440, 100, 35, true}, {40, 20, 300, 60, 97}, {50, 820, 400, 40, 99}});
    if (std::abs(central_title.y - 0.5f) > 0.001f) return 5;
    const auto invalid = chooseTextAwareCrop(600, 900, {{0, 0, 500, 900, std::numeric_limits<float>::quiet_NaN(), true},
        {-1, 20, 100, 20, 90, true}, {1, 1, std::numeric_limits<int>::max(), 40, 99, true}});
    if (invalid.y != 0.5f) return 6;
    { // No lettering read: the most detailed square, not the middle (a logo at the top of a flat poster).
        constexpr uint32_t w = 64, h = 96;
        std::vector<uint8_t> poster(static_cast<size_t>(w) * h * 4, 40);
        for (uint32_t y = 4; y < 22; ++y)
            for (uint32_t x = 8; x < 56; ++x) {
                const uint8_t ink = ((x / 3 + y / 3) % 2) ? 230 : 30;
                for (int c = 0; c < 3; ++c) poster[(static_cast<size_t>(y) * w + x) * 4 + c] = ink;
            }
        const auto top = chooseDetailCrop(w, h, poster);
        if (top.y * h - w * 0.5f > 4 || top.x != 0.5f) return 19; // The window holds the logo (rows 4-21).
        // A flat picture keeps the middle; a capsule slides sideways.
        std::vector<uint8_t> flat(static_cast<size_t>(w) * h * 4, 90);
        if (std::abs(chooseDetailCrop(w, h, flat).y - 0.5f) > 0.001f) return 20;
        std::vector<uint8_t> capsule(static_cast<size_t>(h) * w * 4, 40);
        for (uint32_t y = 10; y < 50; ++y)
            for (uint32_t x = 70; x < 92; ++x)
                for (int c = 0; c < 3; ++c) capsule[(static_cast<size_t>(y) * h + x) * 4 + c] = ((x + y) % 2) ? 220 : 20;
        if (chooseDetailCrop(h, w, capsule).x <= 0.6f) return 21;
    }
    for (const auto& words : std::vector<std::vector<TextBox>>{
             {}, {{80, 400, 440, 100, 95}}, {{10, 10, 120, 40, 30}}}) {
        const auto center = chooseTextAwareCrop(600, 900, words);
        if (std::abs(center.x - 0.5f) > 0.001f || std::abs(center.y - 0.5f) > 0.001f) {
            std::cerr << "Poster with central logo or no reliable OCR must use its center\n";
            return 1;
        }
    }
    const auto at_top = chooseTextAwareCrop(600, 900, {{30, 15, 440, 80, 96}});
    if (at_top.y * 900 - 300 > 15) return 1;
    const auto fits = chooseTextAwareCrop(600, 900,
        {{50, 18, 190, 80, 95}, {90, 551, 260, 32, 90}});
    const float top = fits.y * 900 - 300;
    if (top > 18 || top + 600 < 583) {
        std::cerr << "OCR crop cut off text that fits in a square\n";
        return 1;
    }
    const auto at_bottom = chooseTextAwareCrop(600, 900, {{20, 780, 230, 40, 96}});
    if (at_bottom.y * 900 - 300 > 780 || at_bottom.y * 900 + 300 < 820) {
        std::cerr << "OCR crop cut off bottom-aligned title\n";
        return 1;
    }
    const auto at_right = chooseTextAwareCrop(900, 600, {{760, 80, 120, 50, 97}});
    if (at_right.x * 900 + 300 < 880) {
        std::cerr << "OCR crop cut off a title on the right of a wide header\n";
        return 1;
    }
    const auto square = chooseTextAwareCrop(512, 512, {});
    if (std::abs(square.x - 0.5f) > 0.001f || std::abs(square.y - 0.5f) > 0.001f) return 1;
    if (carousel::distance(0, 7, 9) != -7) return 1;
    // A scrolling shelf binds art for every card it shows: the large focus and nine more at
    // 1600 wide (the last one cut by the edge), more on a wider screen, never past the end.
    if (carousel::visibleEnd(0, 40, 1600) != 10 || carousel::visibleEnd(12, 40, 1600) != 22 ||
        carousel::visibleEnd(0, 40, 2560) != 16 || carousel::visibleEnd(5, 8, 1600) != 8) {
        std::cerr << "Shelf art range does not cover the visible cards\n";
        return 1;
    }
    for (size_t selected = 0; selected < 9; ++selected) {
        const float compact_size = carousel::smallSize(9, 1600, true);
        float end = 0;
        for (size_t index = 0; index < 9; ++index) {
            const float x = carousel::left(index, selected, 9, 1600, true);
            const float size = index == selected ? 2 * compact_size : compact_size;
            if (x < 55 || x < end || x + size > 1545) {
                std::cerr << "First shelf scrolled or clipped at index " << selected << '\n';
                return 1;
            }
            end = x + size;
        }
    }
    return 0;
}
