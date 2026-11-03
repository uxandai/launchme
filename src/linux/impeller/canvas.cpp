// Canvas: records drawing as an Impeller display list. Paint and path conversions live here too.
#include "state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lm_impeller {
namespace {
using PathPtr = PathHandle;

ImpellerRect nativeRect(const Rect& r) { return {r.left, r.top, r.width(), r.height()}; }
ImpellerColor nativeColor(const Color& c) { return {c.r, c.g, c.b, c.a, kImpellerColorSpaceSRGB}; }

PathPtr nativePath(const Path& path) {
    PathBuilderHandle builder(ImpellerPathBuilderNew());
    if (!builder) return {};
    size_t at = 0;
    const auto point = [&](size_t i) { return ImpellerPoint{path.points[i].x, path.points[i].y}; };
    for (const Verb verb : path.verbs) {
        switch (verb) {
        case Verb::move: { const auto a = point(at++); ImpellerPathBuilderMoveTo(builder.get(), &a); break; }
        case Verb::line: { const auto a = point(at++); ImpellerPathBuilderLineTo(builder.get(), &a); break; }
        case Verb::quad: {
            const auto a = point(at), b = point(at + 1);
            at += 2;
            ImpellerPathBuilderQuadraticCurveTo(builder.get(), &a, &b);
            break;
        }
        case Verb::cubic: {
            const auto a = point(at), b = point(at + 1), c = point(at + 2);
            at += 3;
            ImpellerPathBuilderCubicCurveTo(builder.get(), &a, &b, &c);
            break;
        }
        case Verb::close: ImpellerPathBuilderClose(builder.get()); break;
        }
    }
    return PathPtr(ImpellerPathBuilderTakePathNew(builder.get(), path.even_odd ? kImpellerFillTypeOdd : kImpellerFillTypeNonZero));
}

// The standalone API interpolates straight colours; Direct2D's gradients interpolate premultiplied.
// Stops are subdivided where the straight interpolation would be off by more than 1/1024 of a
// premultiplied channel, which keeps gradients on the GPU without dark fringes at transparent stops.
struct Stops {
    std::vector<ImpellerColor> colors;
    std::vector<float> positions;
};
Stops gradientStops(const std::vector<Color>& colors, const std::vector<float>& positions, bool premultiplied) {
    Stops result;
    const auto push = [&](const Color& color, float position) {
        result.colors.push_back(nativeColor(color));
        result.positions.push_back(position);
    };
    if (!premultiplied || colors.size() < 2) {
        for (size_t i = 0; i < colors.size(); ++i) push(colors[i], positions[i]);
        return result;
    }
    for (size_t i = 1; i < colors.size(); ++i) {
        const Color a = colors[i - 1], b = colors[i];
        const auto at = [&](float t) {
            Color c;
            c.a = a.a + (b.a - a.a) * t;
            const auto mix = [&](float from, float to) {
                return c.a > 0 ? (from * a.a * (1 - t) + to * b.a * t) / c.a : (a.a > 0 ? from : to);
            };
            c.r = mix(a.r, b.r); c.g = mix(a.g, b.g); c.b = mix(a.b, b.b);
            return c;
        };
        const auto append = [&](auto&& self, float left, float right, Color start, Color end, unsigned depth) -> void {
            const float difference = std::max({std::abs(end.r - start.r), std::abs(end.g - start.g), std::abs(end.b - start.b)});
            if (depth < 12 && difference * std::abs(end.a - start.a) * 0.25f > 1.0f / 1024) {
                const float middle = (left + right) * 0.5f;
                const Color color = at(middle);
                self(self, left, middle, start, color, depth + 1);
                self(self, middle, right, color, end, depth + 1);
            } else {
                push(end, positions[i - 1] + (positions[i] - positions[i - 1]) * right);
            }
        };
        push(at(0), positions[i - 1]);
        append(append, 0, 1, at(0), at(1), 0);
    }
    return result;
}

ColorSourceHandle nativeSource(const Shader& shader) {
    const ImpellerMatrix transform = shader.local.native();
    if (shader.kind == Shader::Kind::image) {
        if (!shader.image) return {};
        const ImpellerTexture texture = shader.image->texture();
        if (!texture) return {};
        return ColorSourceHandle(ImpellerColorSourceCreateImageNew(texture, shader.tile_x, shader.tile_y,
            shader.nearest ? kImpellerTextureSamplingNearestNeighbor : kImpellerTextureSamplingLinear, &transform));
    }
    if (shader.colors.empty()) return {};
    const Stops stops = gradientStops(shader.colors, shader.positions, shader.premultiplied);
    const auto count = static_cast<uint32_t>(stops.colors.size());
    const ImpellerPoint a{shader.p0.x, shader.p0.y}, b{shader.p1.x, shader.p1.y};
    switch (shader.kind) {
    case Shader::Kind::linear:
        return ColorSourceHandle(ImpellerColorSourceCreateLinearGradientNew(&a, &b, count, stops.colors.data(), stops.positions.data(), shader.tile_x, &transform));
    case Shader::Kind::radial:
        return ColorSourceHandle(ImpellerColorSourceCreateRadialGradientNew(&a, shader.r0, count, stops.colors.data(), stops.positions.data(), shader.tile_x, &transform));
    default:
        return ColorSourceHandle(ImpellerColorSourceCreateConicalGradientNew(&a, shader.r0, &b, shader.r1, count, stops.colors.data(),
                                                                             stops.positions.data(), shader.tile_x, &transform));
    }
}

PaintHandle nativePaint(const Paint& paint) {
    PaintHandle result(ImpellerPaintNew());
    if (!result) return {};
    const ImpellerColor color = nativeColor(paint.color);
    ImpellerPaintSetColor(result.get(), &color);
    ImpellerPaintSetBlendMode(result.get(), paint.blend);
    ImpellerPaintSetDrawStyle(result.get(), paint.stroke ? kImpellerDrawStyleStroke : kImpellerDrawStyleFill);
    ImpellerPaintSetStrokeWidth(result.get(), paint.width);
    ImpellerPaintSetStrokeCap(result.get(), paint.cap);
    ImpellerPaintSetStrokeJoin(result.get(), paint.join);
    ImpellerPaintSetStrokeMiter(result.get(), paint.miter);
    if (paint.shader) {
        auto source = nativeSource(*paint.shader);
        if (!source) return {};
        ImpellerPaintSetColorSource(result.get(), source.get());
    }
    if (paint.blur_sigma > 0) {
        Handle<ImpellerImageFilter, ImpellerImageFilterRelease> blur(ImpellerImageFilterCreateBlurNew(paint.blur_sigma, paint.blur_sigma, paint.blur_tile));
        if (!blur) return {};
        ImpellerPaintSetImageFilter(result.get(), blur.get());
    }
    if (paint.has_color_matrix) {
        ImpellerColorMatrix matrix;
        std::copy(paint.color_matrix, paint.color_matrix + 20, matrix.m);
        Handle<ImpellerColorFilter, ImpellerColorFilterRelease> filter(ImpellerColorFilterCreateColorMatrixNew(&matrix));
        if (!filter) return {};
        ImpellerPaintSetColorFilter(result.get(), filter.get());
    }
    return result;
}
} // namespace

Canvas::Canvas(int width, int height) : width_(width), height_(height) {
    const ImpellerRect bounds{0, 0, static_cast<float>(width), static_cast<float>(height)};
    builder_ = ImpellerDisplayListBuilderNew(&bounds);
    healthy_ = builder_ != nullptr;
}

Canvas::~Canvas() {
    if (finished_) ImpellerDisplayListRelease(finished_);
    if (builder_) ImpellerDisplayListBuilderRelease(builder_);
}

void Canvas::applyMatrix() {
    if (!builder_) return;
    const ImpellerMatrix native = matrix_.native();
    ImpellerDisplayListBuilderSetTransform(builder_, &native);
}

void Canvas::setMatrix(const Matrix& m) {
    matrix_ = m;
    applyMatrix();
}

void Canvas::save() {
    stack_.push_back({matrix_});
    if (builder_) ImpellerDisplayListBuilderSave(builder_);
}

void Canvas::saveLayer(const Rect* bounds, float alpha) {
    stack_.push_back({matrix_});
    if (!builder_) return;
    Rect area;
    if (bounds) {
        area = *bounds;
    } else {
        // The target as seen from the current space.
        Matrix inverse;
        area = matrix_.invert(&inverse) ? inverse.mapRect(Rect::XYWH(0, 0, static_cast<float>(width_), static_cast<float>(height_)))
                                        : Rect::XYWH(0, 0, static_cast<float>(width_), static_cast<float>(height_));
    }
    const ImpellerRect native = nativeRect(area);
    PaintHandle paint;
    if (alpha < 1) {
        Paint own;
        own.color = {0, 0, 0, std::clamp(alpha, 0.0f, 1.0f)};
        paint = nativePaint(own);
    }
    ImpellerDisplayListBuilderSaveLayer(builder_, &native, paint.get(), nullptr);
}

void Canvas::restore() {
    if (stack_.empty()) return;
    matrix_ = stack_.back().matrix;
    stack_.pop_back();
    if (builder_) ImpellerDisplayListBuilderRestore(builder_);
}

void Canvas::restoreToCount(int count) {
    while (saveCount() > std::max(1, count)) restore();
}

void Canvas::clipRect(const Rect& r) {
    if (!builder_) return;
    const ImpellerRect native = nativeRect(r.sorted());
    ImpellerDisplayListBuilderClipRect(builder_, &native, kImpellerClipOperationIntersect);
}

void Canvas::clipPath(const Path& p) {
    if (!builder_) return;
    const auto native = nativePath(p);
    if (!native) { healthy_ = false; return; }
    ImpellerDisplayListBuilderClipPath(builder_, native.get(), kImpellerClipOperationIntersect);
}

void Canvas::drawPaint(const Paint& paint) {
    const auto native = nativePaint(paint);
    if (native && builder_) ImpellerDisplayListBuilderDrawPaint(builder_, native.get());
}

void Canvas::drawRect(const Rect& r, const Paint& paint) {
    if (paint.stroke && !paint.dashes.empty()) { drawPath(Path::Rect_(r), paint); return; }
    const auto native = nativePaint(paint);
    if (!native || !builder_) return;
    const ImpellerRect box = nativeRect(r.sorted());
    ImpellerDisplayListBuilderDrawRect(builder_, &box, native.get());
}

void Canvas::drawRoundRect(const Rect& r, float rx, float ry, const Paint& paint) {
    if (rx <= 0 || ry <= 0) { drawRect(r, paint); return; }
    drawPath(Path::RoundRect(r.sorted(), rx, ry), paint);
}

void Canvas::drawOval(const Rect& r, const Paint& paint) {
    if (paint.stroke && !paint.dashes.empty()) { drawPath(Path::Oval(r), paint); return; }
    const auto native = nativePaint(paint);
    if (!native || !builder_) return;
    const ImpellerRect box = nativeRect(r.sorted());
    ImpellerDisplayListBuilderDrawOval(builder_, &box, native.get());
}

void Canvas::drawPath(const Path& p, const Paint& paint) {
    if (!builder_ || p.empty()) return;
    if (paint.stroke && !paint.dashes.empty()) {
        Paint solid = paint;
        solid.dashes.clear();
        const Path pieces = p.dashed(paint.dashes, paint.dash_phase);
        if (!pieces.empty()) drawPath(pieces, solid);
        return;
    }
    const auto native = nativePaint(paint);
    if (!native) return;
    const auto shape = nativePath(p);
    if (!shape) { healthy_ = false; return; }
    ImpellerDisplayListBuilderDrawPath(builder_, shape.get(), native.get());
}

void Canvas::drawLine(Point a, Point b, const Paint& paint) {
    if (!builder_) return;
    if (!paint.dashes.empty()) { Path line; line.addLine(a, b); drawPath(line, paint); return; }
    const auto native = nativePaint(paint);
    if (!native) return;
    const ImpellerPoint from{a.x, a.y}, to{b.x, b.y};
    ImpellerDisplayListBuilderDrawLine(builder_, &from, &to, native.get());
}

void Canvas::drawImageRect(const std::shared_ptr<Image>& image, const Rect& source, const Rect& dest, Sampling sampling, const Paint* paint) {
    if (!image || !builder_ || source.empty() || dest.empty()) return;
    std::shared_ptr<Image> use = image;
    Rect from = source;
    // Drawn much smaller than it is: from a box-filtered reduction, as mipmapping would.
    if (!sampling.nearest) {
        const float device = dest.width() * matrix_.maxScale();
        if (device > 0 && source.width() / device >= 2) {
            const int level = std::min(8, static_cast<int>(std::floor(std::log2(source.width() / device))));
            if (auto reduced = image->reduced(level); reduced && reduced != image) {
                const float sx = static_cast<float>(reduced->width()) / image->width(), sy = static_cast<float>(reduced->height()) / image->height();
                use = std::move(reduced);
                from = {source.left * sx, source.top * sy, source.right * sx, source.bottom * sy};
            }
        }
    }
    const ImpellerTexture texture = use->texture();
    if (!texture) { healthy_ = false; return; }
    PaintHandle native;
    if (paint) {
        native = nativePaint(*paint);
        if (!native) return;
    }
    const ImpellerRect src = nativeRect(from), dst = nativeRect(dest);
    ImpellerDisplayListBuilderDrawTextureRect(builder_, texture, &src, &dst,
        sampling.nearest ? kImpellerTextureSamplingNearestNeighbor : kImpellerTextureSamplingLinear, native.get());
}

void Canvas::drawParagraph(ImpellerParagraph paragraph, Point origin) {
    if (!builder_ || !paragraph) return;
    const ImpellerPoint at{origin.x, origin.y};
    ImpellerDisplayListBuilderDrawParagraph(builder_, paragraph, &at);
}

void Canvas::clear(Color color) {
    Paint paint;
    paint.color = color;
    paint.blend = kImpellerBlendModeSource;
    drawPaint(paint);
}

ImpellerDisplayList Canvas::finish() {
    if (!healthy_ || !builder_ || !stack_.empty()) return nullptr;
    ImpellerDisplayList list = ImpellerDisplayListBuilderCreateDisplayListNew(builder_);
    if (!list) { healthy_ = false; return nullptr; }
    if (finished_) ImpellerDisplayListRelease(finished_);
    finished_ = list;
    const ImpellerRect bounds{0, 0, static_cast<float>(width_), static_cast<float>(height_)};
    ImpellerDisplayListBuilder next = ImpellerDisplayListBuilderNew(&bounds);
    if (!next) { healthy_ = false; return nullptr; }
    ImpellerDisplayListBuilderRelease(builder_);
    builder_ = next;
    ImpellerDisplayListBuilderDrawDisplayList(builder_, finished_, 1);
    applyMatrix();
    return finished_;
}

std::shared_ptr<Image> Canvas::snapshot() {
    if (!active()) return nullptr;
    const auto list = finish();
    if (!list) return nullptr;
    ImpellerTexture texture = LaunchmeImpellerRenderToTextureNew(context(), list, width_, height_);
    return texture ? Image::fromTexture(texture, width_, height_) : nullptr;
}
PaintHandle makeNativePaint(const Paint& paint) { return nativePaint(paint); }
} // namespace lm_impeller
