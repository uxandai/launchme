// Exercises the Impeller kernel on real GL/Vulkan: uploads, drawing, text, readback, resize and
// lifecycle. No account, application profile, network, or store actions are involved.
#include "linux/impeller/kernel.hpp"
#include "artwork/image_codec.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace gx = lm_impeller;

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    std::cout << (condition ? "ok: " : "FAIL: ") << message << '\n';
    if (!condition) ++failures;
}
struct Rgb { int r, g, b, a; };
class Pixels {
public:
    explicit Pixels(const std::shared_ptr<gx::Image>& image) : width_(image ? image->width() : 0), bytes_(image ? image->pixels() : nullptr) {}
    bool ok() const { return bytes_ != nullptr; }
    Rgb at(int x, int y) const {
        const uint8_t* p = bytes_->data() + (static_cast<size_t>(y) * width_ + x) * 4;
        return {p[2], p[1], p[0], p[3]};
    }
private:
    int width_;
    std::shared_ptr<const std::vector<uint8_t>> bytes_;
};
void capture(const std::shared_ptr<gx::Image>& image, const std::string& name) {
    const char* dir = std::getenv("LAUNCHME_UI_CAPTURE_DIR");
    if (!dir || !image || !image->pixels()) return;
    const auto encoded = image_codec::encodePng({image_codec::Format::png, static_cast<uint32_t>(image->width()),
        static_cast<uint32_t>(image->height()), *image->pixels()});
    std::filesystem::create_directories(dir);
    std::ofstream out(std::filesystem::path(dir) / (name + ".png"), std::ios::binary);
    out.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
    check(out.good() && !encoded.empty(), "PNG capture");
}
gx::Paint solid(float r, float g, float b, float a = 1) {
    gx::Paint paint;
    paint.color = {r, g, b, a};
    return paint;
}
void scene(gx::Canvas& canvas, const std::shared_ptr<gx::Image>& image, const gx::TextParagraph& text) {
    canvas.clear({20 / 255.0f, 25 / 255.0f, 30 / 255.0f, 1});
    canvas.drawRect(gx::Rect::XYWH(8, 8, 40, 32), solid(1, 0, 0));
    canvas.save();
    canvas.translate(90, 10);
    canvas.clipPath(gx::Path::RoundRect(gx::Rect::XYWH(0, 0, 80, 70), 12, 12));
    canvas.saveLayer(nullptr, 0.5f);
    canvas.drawRect(gx::Rect::XYWH(0, 0, 100, 100), solid(0, 1, 0));
    canvas.restore();
    canvas.restore();
    gx::Paint line = solid(1, 1, 1);
    line.stroke = true;
    line.width = 3;
    line.dashes = {6, 3};
    canvas.drawLine({10, 95}, {200, 95}, line);
    if (image) canvas.drawImageRect(image, gx::Rect::XYWH(0, 0, 4, 4), gx::Rect::XYWH(10, 120, 96, 96), gx::Sampling{true}, nullptr);
    text.draw(canvas, {115, 130});
    auto gradient = std::make_shared<gx::Shader>();
    gradient->kind = gx::Shader::Kind::linear;
    gradient->p0 = {115, 195};
    gradient->p1 = {300, 195};
    gradient->colors = {{1, 0, 0, 1}, {0, 1, 0, 0.25f}, {0, 0, 1, 0}};
    gradient->positions = {0, 0.5f, 1};
    gx::Paint fill = solid(0, 0, 0);
    fill.shader = gradient;
    canvas.drawRect(gx::Rect::XYWH(115, 185, 185, 30), fill);
}
} // namespace

int main(int argc, char** argv) {
    const bool vulkan = argc > 1 && std::string_view(argv[1]) == "vulkan";
    const std::string backend = vulkan ? "vulkan" : "gl";
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::cerr << SDL_GetError() << '\n'; return 77; }
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* window = SDL_CreateWindow("Launchme Impeller test", 320, 240,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | (vulkan ? SDL_WINDOW_VULKAN : SDL_WINDOW_OPENGL));
    SDL_GLContext gl = !vulkan && window ? SDL_GL_CreateContext(window) : nullptr;
    if (!window || (!vulkan && !gl)) { std::cerr << SDL_GetError() << '\n'; SDL_Quit(); return 77; }
    if (gl) SDL_GL_MakeCurrent(window, gl);
    if (!(vulkan ? gx::tryVulkan(true) : gx::tryOpenGl())) {
        std::cerr << "Impeller context unavailable\n";
        if (gl) SDL_GL_DestroyContext(gl);
        SDL_DestroyWindow(window); SDL_Quit(); return 77;
    }
    const auto context = gx::context();
    check(LaunchmeImpellerGetAbiVersion() == LAUNCHME_IMPELLER_ABI, "extension ABI");
    std::vector<uint8_t> red(4 * 4 * 4);
    for (size_t i = 0; i < red.size(); i += 4) { red[i + 2] = 255; red[i + 3] = 255; } // B G R A
    check(!LaunchmeImpellerTextureNew(context, kLaunchmeImpellerBGRA, 4, 4, red.data(), 63), "truncated pixels refused");
    check(!LaunchmeImpellerTextureNew(context, kLaunchmeImpellerBGRA, 0, 4, red.data(), 64), "empty dimensions refused");
    check(!LaunchmeImpellerTextureNew(context, static_cast<LaunchmeImpellerFormat>(99), 4, 4, red.data(), 64), "unknown format refused");
    check(!LaunchmeImpellerTextureNew(nullptr, kLaunchmeImpellerBGRA, 4, 4, red.data(), 64), "null context refused");
    {
        // A texture's contents replaced in place (video frames) keep the handle.
        const ImpellerTexture texture = LaunchmeImpellerTextureNew(context, kLaunchmeImpellerBGRA, 4, 4, red.data(), red.size());
        check(texture != nullptr, "BGRA texture");
        check(texture && LaunchmeImpellerTextureUpdate(context, texture, red.data(), red.size()), "texture contents replaced");
        check(!texture || !LaunchmeImpellerTextureUpdate(context, texture, red.data(), 8), "update of another size refused");
        ImpellerTextureRelease(texture);
    }
    std::shared_ptr<gx::Image> image = gx::Image::fromPixels(4, 4, red);
    check(image != nullptr, "BGRA lazy image");
    gx::ParagraphSpec spec;
    gx::TextStyle style;
    style.family = "DejaVu Sans";
    style.size = 18;
    style.ink.color = {1, 1, 1, 1};
    spec.runs.push_back({"Impeller", style});
    spec.width = 200;
    const gx::TextParagraph text(spec);
    check(text.valid() && text.longestLine() > 20 && text.height() > 10 && text.lines().size() == 1, "text fixture lays out");
    gx::GlyphBox box;
    check(text.glyphAt(3, box) && box.bounds.width() > 2 && box.begin == 3, "glyph bounds");
    {
        // Wrapped text: several lines, line metrics and cluster boxes of every character.
        gx::ParagraphSpec wrapped;
        gx::TextStyle plain = style;
        plain.size = 16;
        wrapped.runs.push_back({"Steam isn't installed and the other stores have no games. Add a game from disk or turn on GOG, Epic, EA, Ubisoft, Battle.net or Xbox in the settings.", plain});
        wrapped.width = 300;
        const gx::TextParagraph many(wrapped);
        const auto lines = many.lines();
        check(many.valid() && lines.size() >= 3, "wrapped text has several lines");
        bool continuous = !lines.empty() && lines.front().start == 0;
        for (size_t i = 1; i < lines.size(); ++i) continuous = continuous && lines[i].start == lines[i - 1].end;
        check(continuous, "lines continue each other");
        bool boxes = true;
        for (size_t unit = 0; unit < 40; ++unit) {
            gx::GlyphBox glyph;
            boxes = boxes && many.glyphAt(unit, glyph) && glyph.bounds.right >= glyph.bounds.left;
        }
        check(boxes, "cluster boxes of the first characters");
    }
    auto canvas = gx::beginFrame(window, 320, 240);
    check(canvas != nullptr, "frame canvas");
    std::shared_ptr<gx::Image> rendered;
    if (canvas) {
        scene(*canvas, image, text);
        rendered = canvas->snapshot();
        check(rendered != nullptr, "GPU snapshot/readback");
        check(gx::present(window), "window present after snapshot");
    }
    const Pixels actual(rendered);
    if (actual.ok()) {
        const auto near = [](int a, int b, int tolerance) { return std::abs(a - b) <= tolerance; };
        Rgb p = actual.at(20, 20);
        check(p.r > 245 && p.g < 8 && p.b < 8, "solid rectangle");
        p = actual.at(130, 40);
        check(near(p.g, 140, 12) && p.r < 30, "half-transparent layer over the background");
        p = actual.at(92, 12);
        check(near(p.r, 20, 4) && near(p.g, 25, 4) && near(p.b, 30, 4), "rounded clip corner leaves the background");
        p = actual.at(300, 100);
        check(near(p.r, 20, 4) && near(p.g, 25, 4), "outside everything stays the background");
        bool ink = false;
        for (int y = 128; y < 160 && !ink; ++y)
            for (int x = 115; x < 200 && !ink; ++x) ink = actual.at(x, y).r > 200;
        check(ink, "paragraph draws glyphs");
        p = actual.at(115 + 2, 200);
        check(p.r > 200 && p.g < 40, "gradient starts red");
        p = actual.at(115 + 92, 200);
        check(p.g > 30 && p.r < 140, "gradient passes through premultiplied green (no hue shift)");
        p = actual.at(50, 150);
        check(p.r > 240 && p.g < 8 && p.b < 8, "BGRA texture sampled red");
        capture(rendered, "impeller-" + backend);
    }
    {
        // Drawn far smaller than it is: through reductions made on the GPU (stripes average to grey).
        std::vector<uint8_t> stripes(256 * 256 * 4);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x) {
                uint8_t* px = &stripes[(static_cast<size_t>(y) * 256 + x) * 4];
                px[0] = px[1] = px[2] = x % 2 ? 255 : 0;
                px[3] = 255;
            }
        auto big = gx::Image::fromPixels(256, 256, std::move(stripes));
        auto next = gx::beginFrame(window, 320, 240);
        next->clear({0, 0, 0, 1});
        next->drawImageRect(big, gx::Rect::XYWH(0, 0, 256, 256), gx::Rect::XYWH(0, 0, 16, 16), gx::Sampling{false}, nullptr);
        const auto reduced = big->reduced(3);
        check(reduced && reduced->width() == 32 && reduced->height() == 32 && reduced->onGpu(), "reduction made on the GPU");
        const Pixels result(next->snapshot());
        const int grey = result.ok() ? result.at(8, 8).r : 0;
        check(result.ok() && grey > 100 && grey < 155, "reduced drawing averages the stripes");
        check(gx::present(window), "reduced present");
    }
    {
        // Path booleans and measuring, on the CPU.
        auto a = gx::Path::Rect_(gx::Rect::XYWH(0, 0, 10, 10)), b = gx::Path::Rect_(gx::Rect::XYWH(5, 5, 10, 10));
        const auto both = gx::Path::combine(a, b, gx::BoolOp::intersect);
        const gx::Rect overlap = both ? both->tightBounds() : gx::Rect{};
        check(overlap.width() > 4.9f && overlap.width() < 5.1f && overlap.height() > 4.9f && overlap.height() < 5.1f, "path intersection");
        check(a.contains(5, 5) && !a.contains(12, 5), "path containment");
        gx::PathMeasure measure(gx::Path::Rect_(gx::Rect::XYWH(0, 0, 10, 10)));
        check(measure.length() > 39.9f && measure.length() < 40.1f, "path length");
    }
    for (int i = 0; i < 12; ++i) {
        const int width = i % 2 ? 240 : 320;
        SDL_SetWindowSize(window, width, 240);
        SDL_SyncWindow(window);
        auto next = gx::beginFrame(window, width, 240);
        check(next != nullptr, "resize/recreate canvas");
        if (next) { next->clear({0, 0, 1, 1}); check(gx::present(window), "resized present"); }
    }
    gx::releaseWindow(window);
    canvas.reset();
    gx::shutdown();
    check(!gx::active(), "context teardown");
    if (gl) SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window); SDL_Quit();
    return failures ? 1 : 0;
}
