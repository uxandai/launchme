// Exercises real Impeller GL/Vulkan uploads, drawing, readback, resize and lifecycle.
// No account, application profile, network, or store actions are involved.
#include "linux/impeller/internal.hpp"
#include "artwork/block_compression.hpp"
#include "artwork/image_codec.hpp"
#include "platform/user_config.hpp"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkPath.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSurface.h"
#include "include/core/SkShader.h"
#include "include/core/SkTypeface.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#include <SDL3/SDL.h>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    std::cout << (condition ? "ok: " : "FAIL: ") << message << '\n';
    if (!condition) ++failures;
}
std::vector<uint8_t> pixels(const SkImage* image) {
    if (!image) return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(image->width()) * image->height() * 4);
    if (!image->readPixels(nullptr, SkImageInfo::Make(image->width(), image->height(), kBGRA_8888_SkColorType, kPremul_SkAlphaType),
        bytes.data(), static_cast<size_t>(image->width()) * 4, 0, 0)) return {};
    return bytes;
}
void capture(const SkImage* image, const std::string& name) {
    const char* dir = std::getenv("LAUNCHME_UI_CAPTURE_DIR");
    if (!dir || !image) return;
    const auto encoded = image_codec::encodePng({image_codec::Format::png, static_cast<uint32_t>(image->width()),
        static_cast<uint32_t>(image->height()), pixels(image)});
    std::filesystem::create_directories(dir);
    std::ofstream out(std::filesystem::path(dir) / (name + ".png"), std::ios::binary);
    out.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    check(out.good() && !encoded.empty(), "PNG capture");
}
void scene(SkCanvas* canvas, const SkImage* image, const SkTextBlob* text) {
    canvas->clear(SkColorSetRGB(20, 25, 30));
    SkPaint paint; paint.setAntiAlias(true); paint.setColor(SK_ColorRED);
    canvas->drawRect(SkRect::MakeXYWH(8, 8, 40, 32), paint);
    canvas->save();
    canvas->translate(90, 10);
    canvas->clipRRect(SkRRect::MakeRectXY(SkRect::MakeWH(80, 70), 12, 12), true);
    canvas->saveLayerAlphaf(nullptr, 0.5f);
    paint.setColor(SK_ColorGREEN);
    canvas->drawRect(SkRect::MakeWH(100, 100), paint);
    canvas->restore();
    canvas->restore();
    paint.setColor(SK_ColorWHITE); paint.setStyle(SkPaint::kStroke_Style); paint.setStrokeWidth(3);
    const float dash[]{6, 3}; paint.setPathEffect(SkDashPathEffect::Make(dash, 0));
    canvas->drawLine(10, 95, 200, 95, paint);
    paint.setPathEffect(nullptr); paint.setStyle(SkPaint::kFill_Style);
    if (image) canvas->drawImageRect(image, SkRect::MakeXYWH(10, 120, 96, 96), SkSamplingOptions(SkFilterMode::kNearest), nullptr);
    if (text) canvas->drawTextBlob(text, 115, 160, paint);
    const SkColor4f colors[]{{1, 0, 0, 1}, {0, 1, 0, 0.25f}, {0, 0, 1, 0}};
    const float stops[]{0, 0.5f, 1};
    const SkPoint ends[]{{115, 195}, {300, 195}};
    const SkGradient gradient(SkGradient::Colors(colors, stops, SkTileMode::kClamp),
        SkGradient::Interpolation{SkGradient::Interpolation::InPremul::kYes});
    paint.setShader(SkShaders::LinearGradient(ends, gradient));
    canvas->drawRect(SkRect::MakeXYWH(115, 185, 185, 30), paint);
}
} // namespace

int main(int argc, char** argv) {
    const bool vulkan = argc > 1 && std::string_view(argv[1]) == "vulkan";
    const std::string backend = vulkan ? "vulkan" : "gl";
    const auto settings_dir = std::filesystem::temp_directory_path() /
        ("launchme-impeller-settings-" + backend + "-" + std::to_string(SDL_GetPerformanceCounter()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{settings_dir};
    check(!AppSettings{}.impeller && !parseAppSettings("impeller=yes\n").impeller &&
        parseAppSettings("impeller=1\n").impeller, "renderer opt-in and strict parsing");
    for (const bool enabled : {true, false}) {
        AppSettings setting; setting.impeller = enabled;
        const auto file = settings_dir / "settings.ini";
        check(saveAppSettings(setting, file) && loadAppSettings(file).impeller == enabled, "renderer setting survives restart/load");
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::cerr << SDL_GetError() << '\n'; return 77; }
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* window = SDL_CreateWindow("Launchme Impeller test", 320, 240,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | (vulkan ? SDL_WINDOW_VULKAN : SDL_WINDOW_OPENGL));
    SDL_GLContext gl = !vulkan && window ? SDL_GL_CreateContext(window) : nullptr;
    if (!window || (!vulkan && !gl)) { std::cerr << SDL_GetError() << '\n'; SDL_Quit(); return 77; }
    if (gl) SDL_GL_MakeCurrent(window, gl);
    if (!(vulkan ? lm_impeller::tryVulkan(true) : lm_impeller::tryOpenGl())) {
        std::cerr << "Impeller context unavailable\n";
        if (gl) SDL_GL_DestroyContext(gl);
        SDL_DestroyWindow(window); SDL_Quit(); return 77;
    }
    const auto context = lm_impeller::resources().context.get();
    check(LaunchmeImpellerGetAbiVersion() == LAUNCHME_IMPELLER_ABI, "extension ABI");
    const std::array<uint8_t, 8> red{0, 0xf8, 0, 0, 0, 0, 0, 0};
    check(!LaunchmeImpellerTextureNew(context, kLaunchmeImpellerBC1, 4, 4, red.data(), 7), "truncated block refused");
    check(!LaunchmeImpellerTextureNew(context, kLaunchmeImpellerBC1, 5, 4, red.data(), 8), "unaligned dimensions refused");
    check(!LaunchmeImpellerTextureNew(context, kLaunchmeImpellerBC1, 0, 4, red.data(), 8), "empty dimensions refused");
    check(!LaunchmeImpellerTextureNew(context, static_cast<LaunchmeImpellerFormat>(99), 4, 4, red.data(), 8), "unknown format refused");
    check(!LaunchmeImpellerTextureNew(nullptr, kLaunchmeImpellerBC1, 4, 4, red.data(), 8), "null context refused");
    const bool bc = lm_impeller::supportsCompression(1);
    std::cout << "BC1/BC3 capability: " << bc << '\n';
    auto image = bc ? lm_impeller::compressedImage(1, 4, 4, red.data(), red.size()) : nullptr;
    if (bc) check(image != nullptr, "BC1 lazy image");
    auto fonts = SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
    SkFont font(fonts->matchFamilyStyle("DejaVu Sans", SkFontStyle()), 18);
    auto text = SkTextBlob::MakeFromString("Impeller", font);
    check(text && !text->bounds().isEmpty(), "font fixture");
    auto reference = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(320, 240));
    scene(reference->getCanvas(), image.get(), text.get());
    auto canvas = lm_impeller::beginFrame(window, 320, 240);
    check(canvas != nullptr, "frame canvas");
    sk_sp<SkImage> rendered;
    if (canvas) {
        scene(canvas.get(), image.get(), text.get());
        rendered = lm_impeller::snapshot(canvas.get());
        check(rendered != nullptr, "GPU snapshot/readback");
        check(lm_impeller::present(window), "window present after snapshot");
    }
    if (rendered) {
        const auto actual = pixels(rendered.get()), expected = pixels(reference->makeImageSnapshot().get());
        check(actual.size() == expected.size(), "readback extent");
        size_t mismatches = 0; uint64_t total_error = 0;
        for (size_t i = 0; i < actual.size() && i < expected.size(); ++i) {
            const int error = std::abs(static_cast<int>(actual[i]) - expected[i]);
            total_error += error; mismatches += error > 24;
        }
        const double mean = static_cast<double>(total_error) / expected.size();
        const double changed = static_cast<double>(mismatches) / expected.size();
        std::cout << "Mean channel error: " << mean << "; channels differing >24: " << changed << '\n';
        check(mean < 4 && changed < 0.03, "CPU reference parity (AA edge tolerance)");
        int gradient_error = 0;
        if (actual.size() == expected.size()) for (size_t x = 116; x < 299; ++x)
            for (size_t channel = 0; channel < 4; ++channel) {
                const size_t at = (200 * 320 + x) * 4 + channel;
                gradient_error = std::max(gradient_error, std::abs(static_cast<int>(actual[at]) - expected[at]));
            }
        std::cout << "Gradient maximum channel error: " << gradient_error << '\n';
        // Bounded 8-bit differences between Skia and GL/Vulkan are permitted; the original
        // straight-alpha interpolation error was tens of levels and visibly changed the hue.
        check(actual.size() == expected.size() && gradient_error <= 3, "premultiplied gradient colors (including zero alpha)");
        if (bc) {
            const size_t at = (150 * 320 + 50) * 4;
            check(actual[at + 2] > 240 && actual[at] < 8 && actual[at + 1] < 8, "BC1 texture sampled red");
            const auto entry = lm_impeller::resources().images.find(image->uniqueID());
            check(entry != lm_impeller::resources().images.end() && entry->second.bytes == 8, "BC1 upload retains block storage");
        }
        capture(rendered.get(), "impeller-" + backend);
        capture(reference->makeImageSnapshot().get(), "skia-reference-" + backend);
    }
    if (bc) {
        std::vector<uint8_t> alpha(4 * 4 * 4);
        for (size_t i = 0; i < alpha.size(); i += 4) { alpha[i + 2] = 128; alpha[i + 3] = 128; }
        const auto blocks = block_compression::encodeBc3(alpha.data(), 4, 4);
        auto logo = lm_impeller::compressedImage(3, 4, 4, blocks.data(), blocks.size());
        auto next = lm_impeller::beginFrame(window, 320, 240);
        next->clear(SK_ColorBLACK);
        next->drawImage(logo, 0, 0);
        const auto result = pixels(lm_impeller::snapshot(next.get()).get());
        check(!result.empty() && result[2] > 110 && result[2] < 145 && result[3] == 255, "BC3 premultiplied alpha");
        check(lm_impeller::present(window), "BC3 present");
    }
    for (int i = 0; i < 12; ++i) {
        const int width = i % 2 ? 240 : 320;
        SDL_SetWindowSize(window, width, 240);
        SDL_SyncWindow(window);
        auto next = lm_impeller::beginFrame(window, width, 240);
        check(next != nullptr, "resize/recreate canvas");
        if (next) { next->clear(SK_ColorBLUE); check(lm_impeller::present(window), "resized present"); }
    }
    lm_impeller::releaseWindow(window);
    canvas.reset();
    lm_impeller::shutdown();
    check(!lm_impeller::active(), "context teardown");
    if (gl) SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window); SDL_Quit();
    return failures ? 1 : 0;
}
