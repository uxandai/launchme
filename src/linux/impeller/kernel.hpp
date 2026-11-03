#pragma once
// Linux only: the drawing kernel under the Direct2D / DirectWrite / WIC ports. Impeller (the
// standalone SDK, libimpeller.so) is the only renderer: geometry is Path, brushes are Shader and
// Paint, pixels are Image, and drawing is recorded by Canvas into Impeller display lists that a
// window presents (Vulkan swapchain or the SDL OpenGL framebuffer) or that render to a texture.
// Nothing here depends on another graphics library.
#include <impeller.h>
#include <launchme_impeller.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;

namespace lm_impeller {
// ---- Plain geometry -----------------------------------------------------------------------

struct Point {
    float x = 0, y = 0;
    bool operator==(const Point&) const = default;
};

struct Rect {
    float left = 0, top = 0, right = 0, bottom = 0;
    static Rect LTRB(float l, float t, float r, float b) { return {l, t, r, b}; }
    static Rect XYWH(float x, float y, float w, float h) { return {x, y, x + w, y + h}; }
    float width() const { return right - left; }
    float height() const { return bottom - top; }
    bool empty() const { return !(left < right && top < bottom); }
    Rect sorted() const { return {std::min(left, right), std::min(top, bottom), std::max(left, right), std::max(top, bottom)}; }
    Rect offset(float dx, float dy) const { return {left + dx, top + dy, right + dx, bottom + dy}; }
    Rect outset(float dx, float dy) const { return {left - dx, top - dy, right + dx, bottom + dy}; }
    void join(const Rect& other) {
        if (other.empty()) return;
        if (empty()) { *this = other; return; }
        left = std::min(left, other.left); top = std::min(top, other.top);
        right = std::max(right, other.right); bottom = std::max(bottom, other.bottom);
    }
    bool intersect(const Rect& other) {
        const Rect r{std::max(left, other.left), std::max(top, other.top), std::min(right, other.right), std::min(bottom, other.bottom)};
        if (r.empty()) return false;
        *this = r;
        return true;
    }
    bool operator==(const Rect&) const = default;
};

struct IRect {
    int left = 0, top = 0, right = 0, bottom = 0;
    static IRect XYWH(int x, int y, int w, int h) { return {x, y, x + w, y + h}; }
    static IRect WH(int w, int h) { return {0, 0, w, h}; }
    int width() const { return right - left; }
    int height() const { return bottom - top; }
    bool empty() const { return !(left < right && top < bottom); }
    IRect outset(int dx, int dy) const { return {left - dx, top - dy, right + dx, bottom + dy}; }
    void join(const IRect& other) {
        if (other.empty()) return;
        if (empty()) { *this = other; return; }
        left = std::min(left, other.left); top = std::min(top, other.top);
        right = std::max(right, other.right); bottom = std::max(bottom, other.bottom);
    }
    bool intersect(const IRect& other) {
        const IRect r{std::max(left, other.left), std::max(top, other.top), std::min(right, other.right), std::min(bottom, other.bottom)};
        if (r.empty()) return false;
        *this = r;
        return true;
    }
    Rect toRect() const { return {static_cast<float>(left), static_cast<float>(top), static_cast<float>(right), static_cast<float>(bottom)}; }
    bool operator==(const IRect&) const = default;
};
inline IRect roundOut(const Rect& r) {
    return {static_cast<int>(std::floor(r.left)), static_cast<int>(std::floor(r.top)), static_cast<int>(std::ceil(r.right)),
            static_cast<int>(std::ceil(r.bottom))};
}
inline IRect roundNearest(const Rect& r) {
    return {static_cast<int>(std::lround(r.left)), static_cast<int>(std::lround(r.top)), static_cast<int>(std::lround(r.right)),
            static_cast<int>(std::lround(r.bottom))};
}

// Straight (not premultiplied) colour with float channels.
struct Color {
    float r = 0, g = 0, b = 0, a = 0;
    bool operator==(const Color&) const = default;
};

// A 3x3 matrix mapping column vectors (x, y, 1): the row-major cells of an affine matrix, the
// last row being the perspective.   | sx kx tx |
//                                   | ky sy ty |
//                                   | p0 p1 p2 |
struct Matrix {
    float v[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    static Matrix MakeAll(float sx, float kx, float tx, float ky, float sy, float ty, float p0, float p1, float p2) {
        Matrix m;
        const float cells[9] = {sx, kx, tx, ky, sy, ty, p0, p1, p2};
        std::copy(cells, cells + 9, m.v);
        return m;
    }
    static Matrix Translate(float x, float y) { return MakeAll(1, 0, x, 0, 1, y, 0, 0, 1); }
    static Matrix Scale(float x, float y) { return MakeAll(x, 0, 0, 0, y, 0, 0, 0, 1); }
    // Maps `from` onto `to`; identity for an empty source.
    static Matrix RectToRect(const Rect& from, const Rect& to) {
        if (from.empty()) return {};
        const float sx = to.width() / from.width(), sy = to.height() / from.height();
        return MakeAll(sx, 0, to.left - from.left * sx, 0, sy, to.top - from.top * sy, 0, 0, 1);
    }
    float scaleX() const { return v[0]; }
    float scaleY() const { return v[4]; }
    float skewX() const { return v[1]; }
    float skewY() const { return v[3]; }
    bool hasPerspective() const { return v[6] != 0 || v[7] != 0 || v[8] != 1; }
    bool isFinite() const {
        for (const float cell : v) if (!std::isfinite(cell)) return false;
        return true;
    }
    // this * other: `other` applies first.
    Matrix operator*(const Matrix& o) const {
        Matrix r;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                r.v[row * 3 + col] = v[row * 3] * o.v[col] + v[row * 3 + 1] * o.v[3 + col] + v[row * 3 + 2] * o.v[6 + col];
        return r;
    }
    Matrix& preTranslate(float x, float y) { return *this = *this * Translate(x, y); }
    Matrix& preScale(float x, float y) { return *this = *this * Scale(x, y); }
    Matrix& postScale(float x, float y) { return *this = Scale(x, y) * *this; }
    bool invert(Matrix* out) const;
    Point map(Point p) const {
        const float w = v[6] * p.x + v[7] * p.y + v[8];
        const float x = v[0] * p.x + v[1] * p.y + v[2], y = v[3] * p.x + v[4] * p.y + v[5];
        return w != 0 && w != 1 ? Point{x / w, y / w} : Point{x, y};
    }
    // The bounds of the mapped rectangle's corners.
    Rect mapRect(const Rect& r) const {
        const Point c[4] = {map({r.left, r.top}), map({r.right, r.top}), map({r.right, r.bottom}), map({r.left, r.bottom})};
        Rect out{c[0].x, c[0].y, c[0].x, c[0].y};
        for (const Point& p : c) { out.left = std::min(out.left, p.x); out.top = std::min(out.top, p.y); out.right = std::max(out.right, p.x); out.bottom = std::max(out.bottom, p.y); }
        return out;
    }
    // The largest stretch the matrix applies to a direction (affine part).
    float maxScale() const {
        const float a = v[0], b = v[1], c = v[3], d = v[4];
        const float s = a * a + b * b + c * c + d * d, det = a * d - b * c;
        const float disc = std::sqrt(std::max(0.0f, s * s - 4 * det * det));
        return std::sqrt(std::max(0.0f, (s + disc) * 0.5f));
    }
    ImpellerMatrix native() const {
        ImpellerMatrix m{};
        const float cells[16] = {v[0], v[3], 0, v[6], v[1], v[4], 0, v[7], 0, 0, 1, 0, v[2], v[5], 0, v[8]};
        std::copy(cells, cells + 16, m.m);
        return m;
    }
    bool operator==(const Matrix&) const = default;
};

// ---- Paths --------------------------------------------------------------------------------

enum class Verb : uint8_t { move, line, quad, cubic, close };
enum class BoolOp { unite, intersect, exclusive_or, difference };

// A flattened figure.
struct Polyline {
    std::vector<Point> points;
    bool closed = false;
};

class Path {
public:
    bool even_odd = false;
    std::vector<Verb> verbs;
    std::vector<Point> points;

    bool empty() const { return verbs.empty(); }
    Path& moveTo(float x, float y);
    Path& lineTo(float x, float y);
    Path& quadTo(float cx, float cy, float x, float y);
    Path& cubicTo(float x1, float y1, float x2, float y2, float x3, float y3);
    Path& close();
    // SVG's elliptical arc from the current point: `clockwise` is the sweep in y-down space.
    Path& arcTo(float rx, float ry, float rotation_degrees, bool large, bool clockwise, float x, float y);
    Path& addPath(const Path& other);
    Path& addRect(const Rect& r);                       // Clockwise from the top-left corner.
    Path& addRoundRect(const Rect& r, float rx, float ry);
    Path& addOval(const Rect& r);
    Path& addLine(Point a, Point b);
    static Path Rect_(const Rect& r) { Path p; p.addRect(r); return p; }
    static Path RoundRect(const Rect& r, float rx, float ry) { Path p; p.addRoundRect(r, rx, ry); return p; }
    static Path Oval(const Rect& r) { Path p; p.addOval(r); return p; }

    Path transformed(const Matrix& m) const;
    // The control points' bounds, and the bounds of what is actually drawn.
    Rect bounds() const;
    Rect tightBounds() const;
    // Figures as polylines (curves cut so no point is farther than `tolerance` from them).
    std::vector<Polyline> flatten(float tolerance = 0.05f) const;
    // Inside the filled area by the path's fill rule (every figure counts as closed).
    bool contains(float x, float y) const;
    // Dashes: `intervals` (on, off, on, off…) in path units, starting `phase` into the pattern.
    Path dashed(const std::vector<float>& intervals, float phase) const;
    // A boolean combination of the filled areas; none when the result cannot be computed.
    static std::optional<Path> combine(const Path& a, const Path& b, BoolOp op);
};

// Distances along a path's figures (arc length of the flattened curves).
class PathMeasure {
public:
    explicit PathMeasure(const Path& path);
    float length() const { return total_; }
    // The point and unit tangent `distance` along; past the end it is the end of the last figure.
    // False for a path without any point.
    bool positionAt(float distance, Point* position, Point* tangent) const;
private:
    struct Figure { std::vector<Point> points; std::vector<float> at; float length = 0; };
    std::vector<Figure> figures_;
    float total_ = 0;
};

// ---- Paint --------------------------------------------------------------------------------

class Image;

struct Shader {
    enum class Kind { linear, radial, conical, image };
    Kind kind = Kind::linear;
    Point p0, p1;          // linear: start, end; radial: centre in p0; conical: start centre p0, end centre p1.
    float r0 = 0, r1 = 0;  // radial: radius in r0; conical: start and end radius.
    std::vector<Color> colors;
    std::vector<float> positions;
    bool premultiplied = true;           // Interpolate premultiplied colour (Direct2D's way).
    ImpellerTileMode tile_x = kImpellerTileModeClamp, tile_y = kImpellerTileModeClamp;
    std::shared_ptr<Image> image;        // image
    bool nearest = false;                // image: nearest-neighbour sampling.
    Matrix local;                        // Brush space → the space drawn in.
};
using ShaderPtr = std::shared_ptr<const Shader>;

struct Paint {
    Color color{0, 0, 0, 1};             // The solid colour; a shader's alpha multiplier is in color.a.
    ShaderPtr shader;
    bool stroke = false;
    float width = 0;
    ImpellerStrokeCap cap = kImpellerStrokeCapButt;
    ImpellerStrokeJoin join = kImpellerStrokeJoinMiter;
    float miter = 4;
    ImpellerBlendMode blend = kImpellerBlendModeSourceOver;
    std::vector<float> dashes;           // On/off lengths in path units; empty for a solid stroke.
    float dash_phase = 0;
    // Filters on what the paint draws (Impeller's own): a Gaussian blur, and a 4x5 colour matrix
    // (rows: output red, green, blue, alpha; the last column adds) applied to straight colour.
    float blur_sigma = 0;
    ImpellerTileMode blur_tile = kImpellerTileModeDecal;
    bool has_color_matrix = false;
    float color_matrix[20] = {};
};

struct Sampling {
    bool nearest = false;
    bool operator==(const Sampling&) const = default;
};

// ---- Images -------------------------------------------------------------------------------

// Premultiplied BGRA pixels, or BC1/BC3 blocks, that live in memory, on the GPU or both. Created
// from memory on any thread; the GPU copy is made on the thread that owns the context, the first
// time a canvas draws the image. Immutable once made.
class Image : public std::enable_shared_from_this<Image> {
public:
    ~Image();
    static std::shared_ptr<Image> fromPixels(int width, int height, std::vector<uint8_t> bgra_premultiplied);
    static std::shared_ptr<Image> fromCompressed(unsigned format, int width, int height, const uint8_t* blocks, size_t bytes);
    // A render result (RGBA8 on the GPU).
    static std::shared_ptr<Image> fromTexture(ImpellerTexture texture, int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }
    uint64_t id() const { return id_; }
    bool compressed() const { return format_ != 0; }
    bool onGpu() const;
    // The pixels as premultiplied BGRA (decoded or read back when needed); null when impossible.
    std::shared_ptr<const std::vector<uint8_t>> pixels();
    // The GPU texture, uploaded now when needed; null off the context's thread or on failure.
    ImpellerTexture texture();
    // A copy at 1/2^level of the size (box filtered), the source of reduced drawing.
    std::shared_ptr<Image> reduced(int level);
    size_t gpuBytes() const;
    void dropGpu(); // Forget the texture (kept blocks or pixels upload again).

private:
    Image() = default;
    friend void releaseAllImages();
    friend void trimImages(uint64_t frame);
    int width_ = 0, height_ = 0;
    uint64_t id_ = 0;
    unsigned format_ = 0; // 0: pixels; 1: BC1; 3: BC3.
    std::vector<uint8_t> blocks_;
    std::shared_ptr<const std::vector<uint8_t>> pixels_;
    ImpellerTexture texture_ = nullptr;
    bool rgba_ = false; // The texture's channel order, for read-back.
    uint64_t epoch_ = 0;
    uint64_t used_ = 0;
    std::vector<std::shared_ptr<Image>> levels_;
    friend struct ImageAccess;
};

// ---- Canvas -------------------------------------------------------------------------------

// Records drawing as an Impeller display list, in pixels of a target `width` x `height`.
class Canvas {
public:
    Canvas(int width, int height);
    ~Canvas();
    bool healthy() const { return healthy_; }
    int width() const { return width_; }
    int height() const { return height_; }

    int saveCount() const { return static_cast<int>(stack_.size()) + 1; }
    void save();
    // A layer composited at `alpha` on restore; `bounds` (in the current space) limit it.
    void saveLayer(const Rect* bounds, float alpha);
    void restore();
    void restoreToCount(int count);

    void setMatrix(const Matrix& m);
    void concat(const Matrix& m) { setMatrix(matrix_ * m); }
    void translate(float x, float y) { concat(Matrix::Translate(x, y)); }
    void scale(float x, float y) { concat(Matrix::Scale(x, y)); }
    void resetMatrix() { setMatrix({}); }
    const Matrix& matrix() const { return matrix_; }

    void clipRect(const Rect& r);
    void clipPath(const Path& p);

    void drawPaint(const Paint& paint);
    void drawRect(const Rect& r, const Paint& paint);
    void drawRoundRect(const Rect& r, float rx, float ry, const Paint& paint);
    void drawOval(const Rect& r, const Paint& paint);
    void drawPath(const Path& p, const Paint& paint);
    void drawLine(Point a, Point b, const Paint& paint);
    // `source` (pixels of the image) stretched over `dest`; the paint gives alpha, blend and tint.
    void drawImageRect(const std::shared_ptr<Image>& image, const Rect& source, const Rect& dest, Sampling sampling, const Paint* paint);
    void drawParagraph(ImpellerParagraph paragraph, Point origin);
    // Replaces everything in the current clip with `color`.
    void clear(Color color);

    // Finishes recording: balanced saves required. The list stays valid until the canvas dies;
    // recording may continue afterwards (the list is then drawn first in the next one).
    ImpellerDisplayList finish();
    // What was recorded so far, rendered on the GPU.
    std::shared_ptr<Image> snapshot();

private:
    void applyMatrix();
    struct Saved { Matrix matrix; };
    ImpellerDisplayListBuilder builder_ = nullptr;
    ImpellerDisplayList finished_ = nullptr;
    Matrix matrix_;
    std::vector<Saved> stack_;
    int width_, height_;
    bool healthy_ = true;
};

// ---- Context and windows (backend.cpp) ------------------------------------------------------

bool tryVulkan(bool allow_software);  // A Vulkan device that presents to SDL windows.
bool tryOpenGl();                      // An SDL GL context must be current on this thread.
bool tryHeadless();                    // A Vulkan device without a window (tests, captures).
bool active();                         // A context exists and this is its thread.
ImpellerContext context();
std::shared_ptr<Canvas> beginFrame(SDL_Window* window, int width, int height);
bool present(SDL_Window* window);
void releaseWindow(SDL_Window* window);
void shutdown();
bool supportsCompression(unsigned format); // 1: BC1; 3: BC3.
// Vulkan samples B8G8R8A8 textures as they are; Impeller's OpenGL ES backend takes the same bytes as RGBA, so there
// pictures are uploaded as RGBA (Image::texture).
bool bgraNative();

// ---- Text (typography.cpp) ----------------------------------------------------------------

// The process-wide typography context (system fonts through fontconfig, plus the registered ones).
ImpellerTypographyContext typography();
// Registers a font file's faces under the family names they carry; true when something was added.
bool registerFontFile(const char* path);

// A laid-out paragraph (Impeller's engine): runs of UTF-8 text in styles, laid out at a width.
// Indices are UTF-16 code units.
struct TextStyle {
    std::string family;
    float size = 14;
    int weight = 400; // 100..900
    bool italic = false;
    bool underline = false;
    std::string locale;
    Paint ink;        // What the glyphs are filled with.
};
struct TextRun {
    std::string utf8;
    TextStyle style;
};
enum class TextAlign { left, right, center, justify };
struct ParagraphSpec {
    std::vector<TextRun> runs;
    TextAlign align = TextAlign::left;
    unsigned max_lines = 0; // 0: unlimited.
    bool ellipsis = false;  // Sign the last line's overflow with "…".
    float width = 100000;
};
struct LineInfo {
    size_t start = 0, end = 0, end_excluding_whitespace = 0, end_including_newline = 0;
    float ascent = 0, descent = 0, baseline = 0, width = 0, height = 0, left = 0;
    bool hard_break = false;
};
struct GlyphBox {
    Rect bounds;
    size_t begin = 0, end = 0;
    bool ellipsis = false;
};
class TextParagraph {
public:
    explicit TextParagraph(const ParagraphSpec& spec);
    ~TextParagraph();
    TextParagraph(const TextParagraph&) = delete;
    TextParagraph& operator=(const TextParagraph&) = delete;
    bool valid() const { return paragraph_ != nullptr; }
    float height() const;
    float longestLine() const;
    std::vector<LineInfo> lines() const;
    // The cluster holding the UTF-16 unit; false past the text.
    bool glyphAt(size_t unit, GlyphBox& box) const;
    void draw(Canvas& canvas, Point origin) const;
private:
    ImpellerParagraph paragraph_ = nullptr;
};
} // namespace lm_impeller
